// sdhost.c - SD card on the Pi 3 / Zero 2 W's "SDHOST" controller.
//
// The Pi has two SD controllers. Linux puts the SD card on this one
// (GPIO 48-53, alternate function 0) and the Wi-Fi chip on the other
// ("EMMC", an SDHCI, on GPIO 34-39). MyOS does the same, so EMMC is free
// for Wi-Fi. sd.c falls back to EMMC for the card if this one fails.
//
// Polled, one 512-byte sector at a time. Registers as in Linux's
// drivers/mmc/host/bcm2835.c.
#include <stdint.h>
#include "broadcom/vcmailbox.h"
#include "sd.h"
#include "timer.h"

#define SDHOST_BASE 0x3F202000UL
#define GPIO_BASE   0x3F200000UL
#define REG(base, off) (*(volatile uint32_t *)((base) + (off)))

#define SDCMD   REG(SDHOST_BASE, 0x00)
#define SDARG   REG(SDHOST_BASE, 0x04)
#define SDTOUT  REG(SDHOST_BASE, 0x08)
#define SDCDIV  REG(SDHOST_BASE, 0x0C)
#define SDRSP0  REG(SDHOST_BASE, 0x10)
#define SDRSP1  REG(SDHOST_BASE, 0x14)
#define SDRSP2  REG(SDHOST_BASE, 0x18)
#define SDRSP3  REG(SDHOST_BASE, 0x1C)
#define SDHSTS  REG(SDHOST_BASE, 0x20)
#define SDVDD   REG(SDHOST_BASE, 0x30)
#define SDEDM   REG(SDHOST_BASE, 0x34)
#define SDHCFG  REG(SDHOST_BASE, 0x38)
#define SDHBCT  REG(SDHOST_BASE, 0x3C)
#define SDDATA  REG(SDHOST_BASE, 0x40)
#define SDHBLC  REG(SDHOST_BASE, 0x50)

#define GPFSEL(n)   REG(GPIO_BASE, 0x00 + 4 * (n))
#define GPPUD       REG(GPIO_BASE, 0x94)
#define GPPUDCLK1   REG(GPIO_BASE, 0x9C)

// SDCMD
#define CMD_NEW         0x8000u
#define CMD_FAIL        0x4000u
#define CMD_NO_RESP     0x0400u
#define CMD_LONG_RESP   0x0200u
#define CMD_WRITE       0x0080u
#define CMD_READ        0x0040u
// SDHSTS
#define HSTS_ERRORS     0x00F8u     // CRC7, CRC16, command timeout, data timeout, FIFO
#define HSTS_CRC7       0x0010u
#define HSTS_CLEAR      0x07F8u
// SDHCFG
#define HCFG_SLOW_CARD  0x0008u
#define HCFG_WIDE_INT   0x0002u
// SDEDM
#define EDM_FIFO(e)     (((e) >> 4) & 0x1F)
#define EDM_STATE(e)    ((e) & 0xF)
#define EDM_DATAMODE    1
#define EDM_IDENTMODE   0

static uint32_t core_hz;
static uint32_t rca;
static int high_capacity;
static uint64_t card_sectors;
static int ready;

static void delay_us(uint64_t us) {
    uint64_t end = timer_count() + us * (timer_freq() / 1000000);
    while (timer_count() < end) {
    }
}

static int timed_out(uint64_t end) { return timer_count() >= end; }
static uint64_t after_ms(uint32_t ms) { return timer_count() + (uint64_t)ms * (timer_freq() / 1000); }

// send a command; flags: CMD_NO_RESP / CMD_LONG_RESP / CMD_READ / CMD_WRITE
static int command(uint32_t index, uint32_t arg, uint32_t flags, int crc_ok_to_fail) {
    uint64_t end = after_ms(100);
    while (SDCMD & CMD_NEW) {
        if (timed_out(end)) return -1;
    }
    SDHSTS = HSTS_CLEAR;
    SDARG = arg;
    SDCMD = index | flags | CMD_NEW;
    end = after_ms(500);
    while (SDCMD & CMD_NEW) {
        if (timed_out(end)) return -1;
    }
    if (SDCMD & CMD_FAIL) {
        uint32_t st = SDHSTS;
        SDHSTS = HSTS_CLEAR;
        // R3 (ACMD41's answer) has no real CRC: the host reports a CRC7 error
        if (!(crc_ok_to_fail && (st & HSTS_ERRORS) == HSTS_CRC7)) return -1;
    }
    return 0;
}

static int app_command(uint32_t index, uint32_t arg, int crc_ok_to_fail) {
    if (command(55, rca << 16, 0, 0)) return -1;
    return command(index, arg, 0, crc_ok_to_fail);
}

static void set_clock(uint32_t hz) {
    uint32_t div = core_hz / hz;                    // f = core / (div + 2)
    div = div > 2 ? div - 2 : 0;
    if (core_hz / (div + 2) > hz) div++;
    if (div > 0x7FF) div = 0x7FF;
    SDCDIV = div;
}

// connect the SD card slot (GPIO 48-53) to SDHOST: alternate function 0
static void setup_pins(void) {
    uint32_t sel4 = GPFSEL(4) & ~(0x3Fu << 24);     // pins 48, 49
    GPFSEL(4) = sel4 | (4u << 24) | (4u << 27);
    uint32_t sel5 = GPFSEL(5) & ~0xFFFu;            // pins 50-53
    GPFSEL(5) = sel5 | 4u | (4u << 3) | (4u << 6) | (4u << 9);
    GPPUD = 2;                                      // pull-ups on CMD, DAT0-3 (49-53)
    delay_us(5);
    GPPUDCLK1 = 0x3Eu << 16;
    delay_us(5);
    GPPUD = 0;
    GPPUDCLK1 = 0;
}

// wait until the controller has finished with the data (back in "data mode")
static int wait_idle(void) {
    uint64_t end = after_ms(500);
    for (;;) {
        uint32_t s = EDM_STATE(SDEDM);
        if (s == EDM_DATAMODE || s == EDM_IDENTMODE) return 0;
        if (timed_out(end)) return -1;
    }
}

// wait until the card is ready for the next command (state "tran", ready bit)
static int wait_card_ready(void) {
    uint64_t end = after_ms(1000);
    while (!timed_out(end)) {
        if (command(13, rca << 16, 0, 0) == 0) {
            uint32_t st = SDRSP0;
            if ((st & (1u << 8)) && ((st >> 9) & 0xF) == 4) return 0;
        }
    }
    return -1;
}

int sdhost_init(void) {
    ready = 0;
    setup_pins();
    core_hz = vcmailbox_get_clock_rate_measured(VCMAILBOX_CLOCK_CORE);
    // the firmware may raise the core clock later: divide for the highest one,
    // so the card is never clocked too fast (a lower clock is only slower)
    if (core_hz < 400000000u) core_hz = 400000000u;

    // reset, like Linux's bcm2835_reset_internal
    SDVDD = 0;
    SDCMD = 0;
    SDARG = 0;
    SDTOUT = 0xF00000;
    SDCDIV = 0;
    SDHSTS = HSTS_CLEAR;
    SDHCFG = 0;
    SDHBCT = 0;
    SDHBLC = 0;
    uint32_t edm = SDEDM & ~((0x1Fu << 14) | (0x1Fu << 9));
    SDEDM = edm | (4u << 14) | (4u << 9);           // FIFO read/write thresholds
    delay_us(20000);
    SDVDD = 1;                                      // power on
    delay_us(20000);
    SDHCFG = HCFG_SLOW_CARD | HCFG_WIDE_INT;        // 1-bit bus to the card
    set_clock(400000);                              // identification at 400 kHz
    delay_us(1000);

    rca = 0;
    command(0, 0, CMD_NO_RESP, 0);                  // GO_IDLE
    if (command(8, 0x1AA, 0, 0) || (SDRSP0 & 0xFFF) != 0x1AA) return -3;
    uint32_t ocr = 0;
    for (int i = 0; i < 100; i++) {
        if (app_command(41, 0x51FF8000, 1) == 0) {
            ocr = SDRSP0;
            if (ocr & 0x80000000u) break;
        }
        delay_us(10000);
    }
    if (!(ocr & 0x80000000u)) return -4;
    high_capacity = (ocr >> 30) & 1;

    if (command(2, 0, CMD_LONG_RESP, 0) || command(3, 0, 0, 0)) return -5;
    rca = SDRSP0 >> 16;
    if (command(9, rca << 16, CMD_LONG_RESP, 0) == 0) {
        // SDHOST keeps all 128 bits (CRC byte last); line them up like SDHCI does
        uint32_t s0 = SDRSP0, s1 = SDRSP1, s2 = SDRSP2, s3 = SDRSP3;
        card_sectors = sd_capacity_from_csd((s0 >> 8) | (s1 << 24), (s1 >> 8) | (s2 << 24),
                                            (s2 >> 8) | (s3 << 24), s3 >> 8);
    }
    if (command(7, rca << 16, 0, 0)) return -7;     // select the card
    set_clock(25000000);                            // normal speed: 25 MHz
    if (!high_capacity && command(16, 512, 0, 0)) return -8;
    SDHBCT = 512;
    ready = 1;
    return 0;
}

int sdhost_read(uint32_t lba, void *buf) {
    if (!ready) return -1;
    SDHBCT = 512;
    SDHBLC = 1;
    if (command(17, high_capacity ? lba : lba * 512, CMD_READ, 0)) return -1;
    uint32_t *p = buf;
    for (int i = 0; i < 128; i++) {
        uint64_t end = after_ms(500);
        while (EDM_FIFO(SDEDM) == 0) {
            if ((SDHSTS & HSTS_ERRORS) || timed_out(end)) { SDHSTS = HSTS_CLEAR; return -1; }
        }
        p[i] = SDDATA;
    }
    return wait_idle();
}

int sdhost_write(uint32_t lba, const void *buf) {
    if (!ready) return -1;
    SDHBCT = 512;
    SDHBLC = 1;
    if (command(24, high_capacity ? lba : lba * 512, CMD_WRITE, 0)) return -1;
    const uint32_t *p = buf;
    for (int i = 0; i < 128; i++) {
        uint64_t end = after_ms(500);
        while (EDM_FIFO(SDEDM) >= 16) {             // FIFO full (16 words)
            if ((SDHSTS & HSTS_ERRORS) || timed_out(end)) { SDHSTS = HSTS_CLEAR; return -1; }
        }
        SDDATA = p[i];
    }
    if (wait_idle()) return -1;
    if (SDHSTS & HSTS_ERRORS) { SDHSTS = HSTS_CLEAR; return -1; }
    return wait_card_ready();                       // the card writes it to flash
}

int sdhost_is_high_capacity(void) { return high_capacity; }
uint64_t sdhost_sectors(void) { return card_sectors; }
uint32_t sdhost_clock(void) { return core_hz; }
