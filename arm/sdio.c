// sdio.c - SDIO bus to the Wi-Fi chip (Pi Zero 2 W / Pi 3).
//
// The Wi-Fi chip (Broadcom/Cypress CYW43436 or 43430) sits on the "EMMC"
// controller (an Arasan SDHCI) through GPIO 34-39 (alternate function 3).
// The SD card is on the other controller (sdhost.c), so the two don't
// get in each other's way. Its power/reset line WL_ON is GPIO 41.
//
// SDIO is SD with "functions" instead of a memory card: function 0 is the
// card's common registers (CCCR), function 1 the chip's backplane (its
// internal bus), function 2 the network frames. CMD52 reads/writes one
// byte, CMD53 many. Polled, no DMA, no interrupts.
#include <stdint.h>
#include "broadcom/vcmailbox.h"
#include "sdio.h"
#include "timer.h"

#define GPIO_BASE   0x3F200000UL
#define EMMC_BASE   0x3F300000UL
#define REG(base, off) (*(volatile uint32_t *)((base) + (off)))

#define GPFSEL(n)   REG(GPIO_BASE, 0x00 + 4 * (n))
#define GPSET1      REG(GPIO_BASE, 0x20)
#define GPCLR1      REG(GPIO_BASE, 0x2C)
#define GPPUD       REG(GPIO_BASE, 0x94)
#define GPPUDCLK1   REG(GPIO_BASE, 0x9C)

#define BLKSIZECNT  0x04
#define ARG1        0x08
#define CMDTM       0x0C
#define RESP0       0x10
#define DATA        0x20
#define STATUS      0x24
#define CONTROL0    0x28
#define CONTROL1    0x2C
#define INTERRUPT   0x30
#define INT_MASK    0x34
#define INT_EN      0x38

#define SR_CMD_INHIBIT  (1u << 0)
#define SR_DAT_INHIBIT  (1u << 1)
#define C0_4BIT         (1u << 1)
#define C0_HISPEED      (1u << 2)
#define C1_CLK_INTLEN   (1u << 0)
#define C1_CLK_STABLE   (1u << 1)
#define C1_CLK_EN       (1u << 2)
#define C1_TOUNIT_MAX   (0xEu << 16)
#define C1_SRST_HC      (1u << 24)
#define C1_SRST_CMD     (1u << 25)
#define C1_SRST_DATA    (1u << 26)

#define INT_CMD_DONE    (1u << 0)
#define INT_DATA_DONE   (1u << 1)
#define INT_WRITE_RDY   (1u << 4)
#define INT_READ_RDY    (1u << 5)
#define INT_CARD        (1u << 8)
#define INT_ERROR_MASK  0xFFFF8000u

// CMDTM: index << 24 | response | checks | data
#define RESP_NONE   0x00000000u
#define RESP_48     0x00020000u
#define RESP_48B    0x00030000u
#define CRC_CHECK   0x00080000u
#define IDX_CHECK   0x00100000u
#define IS_DATA     0x00200000u
#define TM_READ     0x00000010u
#define TM_MULTI    0x00000020u
#define TM_BLKCNT   0x00000002u

#define CMD0  (0u  << 24 | RESP_NONE)
#define CMD3  (3u  << 24 | RESP_48 | CRC_CHECK | IDX_CHECK)
#define CMD5  (5u  << 24 | RESP_48)                         // R4: no CRC, no index
#define CMD7  (7u  << 24 | RESP_48B | CRC_CHECK | IDX_CHECK)
#define CMD52 (52u << 24 | RESP_48 | CRC_CHECK | IDX_CHECK)
#define CMD53 (53u << 24 | RESP_48 | CRC_CHECK | IDX_CHECK | IS_DATA)

#define R5_ERRORS 0xCB00u           // CRC, illegal command, error, function number, range

static uint32_t base_clock, card_clock = 400000;
static uint32_t rca;
static uint32_t blksize[8] = { 64, 64, 64, 64, 64, 64, 64, 64 };
static int ready;
static const char *last_error = "";
static uint32_t err_flags, err_status;      // controller registers when something failed

static void delay_us(uint64_t us) {
    uint64_t end = timer_count() + us * (timer_freq() / 1000000);
    while (timer_count() < end) {
    }
}

// The Arasan controller can lose a register write that follows another
// within two SD clock cycles (Linux's sdhci-bcm2835 waits the same way).
static void wr(uint32_t off, uint32_t v) {
    REG(EMMC_BASE, off) = v;
    if (card_clock < 10000000) delay_us(2000000 / card_clock + 1);
}

static uint32_t rd(uint32_t off) { return REG(EMMC_BASE, off); }

static int fail(const char *why) {
    last_error = why;
    return -1;
}

static int wait_status_clear(uint32_t mask) {
    uint64_t end = timer_count() + timer_freq() / 10;      // 100 ms
    while (rd(STATUS) & mask) {
        if (timer_count() > end) return -1;
    }
    return 0;
}

static void reset_lines(void) {         // after an error: reset the command and data circuits
    wr(CONTROL1, rd(CONTROL1) | C1_SRST_CMD | C1_SRST_DATA);
    for (int i = 0; i < 100000 && (rd(CONTROL1) & (C1_SRST_CMD | C1_SRST_DATA)); i++) {
    }
}

static int wait_interrupt(uint32_t mask, uint32_t ms) {
    uint64_t end = timer_count() + ms * (timer_freq() / 1000);
    for (;;) {
        uint32_t flags = rd(INTERRUPT);
        if (flags & INT_ERROR_MASK) {
            err_flags = flags;
            err_status = rd(STATUS);
            wr(INTERRUPT, flags);
            reset_lines();
            return -2;
        }
        if (flags & mask) {
            wr(INTERRUPT, flags & mask);
            return 0;
        }
        if (timer_count() > end) {
            err_flags = flags;
            err_status = rd(STATUS);
            reset_lines();
            return -1;
        }
    }
}

static int command(uint32_t cmd, uint32_t arg) {
    if (wait_status_clear(SR_CMD_INHIBIT)) return fail("command line busy");
    if ((cmd & IS_DATA) && wait_status_clear(SR_DAT_INHIBIT)) return fail("data line busy");
    wr(INTERRUPT, rd(INTERRUPT));
    wr(ARG1, arg);
    wr(CMDTM, cmd);
    int r = wait_interrupt(INT_CMD_DONE, 100);
    if (r) return fail(r == -1 ? "no answer" : "command error");
    return 0;
}

static int set_clock(uint32_t hz) {
    if (wait_status_clear(SR_CMD_INHIBIT | SR_DAT_INHIBIT)) return -1;
    wr(CONTROL1, rd(CONTROL1) & ~C1_CLK_EN);
    delay_us(10);
    uint32_t div = (base_clock + 2 * hz - 1) / (2 * hz);    // f = base / (2 * div)
    if (div > 0x3FF) div = 0x3FF;
    if (div == 0) div = 1;
    uint32_t c1 = rd(CONTROL1) & ~0xFFE0u;
    c1 |= ((div & 0xFF) << 8) | (((div >> 8) & 3) << 6) | C1_CLK_INTLEN;
    wr(CONTROL1, c1);
    for (int i = 0; i < 100000 && !(rd(CONTROL1) & C1_CLK_STABLE); i++) {
    }
    if (!(rd(CONTROL1) & C1_CLK_STABLE)) return -1;
    wr(CONTROL1, rd(CONTROL1) | C1_CLK_EN);
    card_clock = base_clock / (2 * div);
    delay_us(10);
    return 0;
}

// GPIO 34-39 -> EMMC (alt 3), pull-ups on CMD and DAT0-3; GPIO 41 (WL_ON) -> output
static void setup_pins(void) {
    uint32_t sel3 = GPFSEL(3) & ~0x3FFFF000u;
    GPFSEL(3) = sel3 | (7u << 12) | (7u << 15) | (7u << 18) | (7u << 21) | (7u << 24) | (7u << 27);
    GPPUD = 2;
    delay_us(5);
    GPPUDCLK1 = 0x1Fu << 3;         // pins 35-39
    delay_us(5);
    GPPUD = 0;
    GPPUDCLK1 = 0;
    uint32_t sel4 = GPFSEL(4) & ~(7u << 3);
    GPFSEL(4) = sel4 | (1u << 3);   // pin 41: output
}

int sdio_cmd52(int write, int fn, uint32_t addr, uint8_t data, uint8_t *out) {
    uint32_t arg = (write ? 1u << 31 : 0) | (uint32_t)fn << 28 | (write && out ? 1u << 27 : 0) |
                   (addr & 0x1FFFF) << 9 | data;
    if (command(CMD52, arg)) return -1;
    uint32_t r = rd(RESP0);
    if (r & R5_ERRORS) return fail("CMD52 error flags");
    if (out) *out = (uint8_t)r;
    return 0;
}

// one CMD53: count blocks of blksize[fn] (block mode) or count bytes (byte mode)
static int cmd53_once(int write, int fn, uint32_t addr, int incr, uint8_t *buf, uint32_t count, int block) {
    uint32_t size = block ? blksize[fn] : count;
    uint32_t nblocks = block ? count : 1;
    uint32_t arg = (write ? 1u << 31 : 0) | (uint32_t)fn << 28 | (block ? 1u << 27 : 0) |
                   (incr ? 1u << 26 : 0) | (addr & 0x1FFFF) << 9 | (count & 0x1FF);
    uint32_t cmd = CMD53 | (write ? 0 : TM_READ);
    if (block) cmd |= TM_BLKCNT | (nblocks > 1 ? TM_MULTI : 0);
    if (wait_status_clear(SR_CMD_INHIBIT | SR_DAT_INHIBIT)) return fail("bus busy");
    wr(BLKSIZECNT, nblocks << 16 | size);
    if (command(cmd, arg)) return -1;
    if (rd(RESP0) & R5_ERRORS) return fail("CMD53 error flags");
    uint32_t words = (size + 3) / 4;
    for (uint32_t b = 0; b < nblocks; b++) {
        if (wait_interrupt(write ? INT_WRITE_RDY : INT_READ_RDY, 200)) return fail("data timeout");
        uint8_t *p = buf + b * size;
        if (((uintptr_t)p & 3) == 0) {
            uint32_t *w = (uint32_t *)p;
            uint32_t full = size / 4;
            for (uint32_t i = 0; i < full; i++) {
                if (write) REG(EMMC_BASE, DATA) = w[i];
                else w[i] = REG(EMMC_BASE, DATA);
            }
            if (full < words) {                 // 1-3 bytes left over
                uint32_t v = write ? 0 : REG(EMMC_BASE, DATA);
                for (uint32_t k = 0; k < (size & 3); k++) {
                    if (write) v |= (uint32_t)p[full * 4 + k] << (8 * k);
                    else p[full * 4 + k] = (uint8_t)(v >> (8 * k));
                }
                if (write) REG(EMMC_BASE, DATA) = v;
            }
        } else {
            for (uint32_t i = 0; i < words; i++) {
                uint32_t v = 0;
                uint32_t n = size - i * 4 < 4 ? size - i * 4 : 4;
                if (write) {
                    for (uint32_t k = 0; k < n; k++) v |= (uint32_t)p[i * 4 + k] << (8 * k);
                    REG(EMMC_BASE, DATA) = v;
                } else {
                    v = REG(EMMC_BASE, DATA);
                    for (uint32_t k = 0; k < n; k++) p[i * 4 + k] = (uint8_t)(v >> (8 * k));
                }
            }
        }
    }
    if (wait_interrupt(INT_DATA_DONE, 200)) return fail("transfer not finished");
    return 0;
}

int sdio_cmd53(int write, int fn, uint32_t addr, int incr, void *buf, uint32_t len) {
    if (!ready) return fail("no SDIO card");
    uint8_t *p = buf;
    uint32_t bs = blksize[fn];
    while (len >= bs) {                         // whole blocks (at most 511 at once)
        uint32_t n = len / bs;
        if (n > 511) n = 511;
        if (cmd53_once(write, fn, addr, incr, p, n, 1)) return -1;
        p += n * bs;
        len -= n * bs;
        if (incr) addr += n * bs;
    }
    if (len) {
        if (cmd53_once(write, fn, addr, incr, p, len, 0)) return -1;
    }
    return 0;
}

int sdio_set_blocksize(int fn, uint32_t size) {
    uint32_t base = (uint32_t)fn * 0x100;
    if (sdio_cmd52(1, 0, base + 0x10, size & 0xFF, 0)) return -1;
    if (sdio_cmd52(1, 0, base + 0x11, (size >> 8) & 0xFF, 0)) return -1;
    blksize[fn] = size;
    return 0;
}

int sdio_enable_function(int fn) {
    uint8_t v;
    if (sdio_cmd52(0, 0, 0x02, 0, &v)) return -1;
    if (sdio_cmd52(1, 0, 0x02, v | (1u << fn), 0)) return -1;
    uint64_t end = millis() + 500;
    while (millis() < end) {
        if (sdio_cmd52(0, 0, 0x03, 0, &v) == 0 && (v & (1u << fn))) return 0;
        delay_us(1000);
    }
    return fail("function didn't get ready");
}

int sdio_init(void) {
    ready = 0;
    setup_pins();
    // power-cycle the Wi-Fi chip: WL_ON low, then high (it may still be
    // running from before a reboot)
    GPCLR1 = 1u << (41 - 32);
    delay_us(20000);
    GPSET1 = 1u << (41 - 32);
    delay_us(150000);

    base_clock = vcmailbox_get_clock_rate_measured(VCMAILBOX_CLOCK_EMMC);
    if (base_clock == 0) base_clock = 50000000;
    card_clock = 400000;
    wr(CONTROL0, 0);
    wr(CONTROL1, rd(CONTROL1) | C1_SRST_HC);
    for (int i = 0; i < 100000 && (rd(CONTROL1) & C1_SRST_HC); i++) {
    }
    if (rd(CONTROL1) & C1_SRST_HC) return fail("controller reset failed");
    wr(CONTROL1, rd(CONTROL1) | C1_CLK_INTLEN | C1_TOUNIT_MAX);
    delay_us(10);
    if (set_clock(400000)) return fail("clock didn't start");
    wr(INT_EN, 0xFFFFFFFF & ~INT_CARD);
    wr(INT_MASK, 0xFFFFFFFF & ~INT_CARD);

    command(CMD0, 0);
    delay_us(1000);
    // CMD5: an SDIO card answers with its voltage range (OCR) ...
    if (command(CMD5, 0)) return fail("no SDIO card (no answer to CMD5)");
    uint32_t ocr = rd(RESP0);
    // ... then asked again with 3.3 V until it says it is ready (bit 31)
    int ok = 0;
    for (int i = 0; i < 100 && !ok; i++) {
        if (command(CMD5, (ocr & 0x00FFFF00u) ? (ocr & 0x00FFFF00u) : 0x00300000u) == 0 &&
            (rd(RESP0) & 0x80000000u)) ok = 1;
        else delay_us(10000);
    }
    if (!ok) return fail("SDIO card never got ready (CMD5)");
    if (command(CMD3, 0)) return fail("no address (CMD3)");
    rca = rd(RESP0) >> 16;
    if (command(CMD7, rca << 16)) return fail("couldn't select the card (CMD7)");
    if (wait_interrupt(INT_DATA_DONE, 50)) {
        // R1b: busy signalling is optional for SDIO
    }
    ready = 1;
    // 4-bit bus: card (CCCR bus interface control, card detect pull-up off) and host
    uint8_t v;
    if (sdio_cmd52(0, 0, 0x07, 0, &v)) { ready = 0; return -1; }
    if (sdio_cmd52(1, 0, 0x07, (v & ~3u) | 2 | 0x80, 0)) { ready = 0; return -1; }
    wr(CONTROL0, rd(CONTROL0) | C0_4BIT);
    if (set_clock(25000000)) { ready = 0; return fail("25 MHz clock didn't start"); }
    return 0;
}

int sdio_high_speed(void) {
    uint8_t v;
    if (!ready || sdio_cmd52(0, 0, 0x13, 0, &v) || !(v & 1)) return 0;
    if (sdio_cmd52(1, 0, 0x13, v | 2, 0)) return 0;
    wr(CONTROL0, rd(CONTROL0) | C0_HISPEED);
    if (set_clock(50000000)) return 0;
    return 1;
}

int sdio_ready(void) { return ready; }
uint32_t sdio_clock_hz(void) { return card_clock; }
const char *sdio_error(void) { return last_error; }
uint32_t sdio_error_flags(void) { return err_flags; }
uint32_t sdio_error_status(void) { return err_status; }
