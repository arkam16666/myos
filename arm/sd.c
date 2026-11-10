// sd.c - SD card driver for the Raspberry Pi 3 / Zero 2 W.
//
// Uses the "EMMC" controller (an Arasan SDHCI, the standard SD host
// controller design) by polling, one 512-byte sector at a time.
// The card's pins (GPIO 48-53) are switched over to this controller;
// the Wi-Fi chip, which normally uses it, is disconnected.
#include <stdint.h>
#include "broadcom/vcmailbox.h"
#include "sd.h"
#include "timer.h"

#define PERIPHERAL_BASE 0x3F000000UL
#define GPIO_BASE       (PERIPHERAL_BASE + 0x200000)
#define EMMC_BASE       (PERIPHERAL_BASE + 0x300000)

#define REG(base, off)  (*(volatile uint32_t *)((base) + (off)))
#define GPFSEL(n)       REG(GPIO_BASE, 0x00 + 4 * (n))
#define GPPUD           REG(GPIO_BASE, 0x94)
#define GPPUDCLK1       REG(GPIO_BASE, 0x9C)

#define EMMC_BLKSIZECNT REG(EMMC_BASE, 0x04)
#define EMMC_ARG1       REG(EMMC_BASE, 0x08)
#define EMMC_CMDTM      REG(EMMC_BASE, 0x0C)
#define EMMC_RESP0      REG(EMMC_BASE, 0x10)
#define EMMC_RESP1      REG(EMMC_BASE, 0x14)
#define EMMC_RESP2      REG(EMMC_BASE, 0x18)
#define EMMC_RESP3      REG(EMMC_BASE, 0x1C)
#define EMMC_DATA       REG(EMMC_BASE, 0x20)
#define EMMC_STATUS     REG(EMMC_BASE, 0x24)
#define EMMC_CONTROL1   REG(EMMC_BASE, 0x2C)
#define EMMC_INTERRUPT  REG(EMMC_BASE, 0x30)
#define EMMC_INT_MASK   REG(EMMC_BASE, 0x34)
#define EMMC_INT_EN     REG(EMMC_BASE, 0x38)

#define SR_CMD_INHIBIT  (1u << 0)
#define SR_DAT_INHIBIT  (1u << 1)

#define C1_CLK_INTLEN   (1u << 0)
#define C1_CLK_STABLE   (1u << 1)
#define C1_CLK_EN       (1u << 2)
#define C1_TOUNIT_MAX   (0xEu << 16)
#define C1_SRST_HC      (1u << 24)

#define INT_CMD_DONE    (1u << 0)
#define INT_DATA_DONE   (1u << 1)
#define INT_WRITE_RDY   (1u << 4)
#define INT_READ_RDY    (1u << 5)
#define INT_ERROR_MASK  0xFFFF8000u

// command register values: index << 24 | response type | flags
#define RESP_NONE   0x00000000u
#define RESP_136    0x00010000u
#define RESP_48     0x00020000u
#define RESP_48B    0x00030000u     // 48 bits, card may be busy afterwards
#define CRC_CHECK   0x00080000u
#define DATA_READ   (0x00200000u | 0x10u)   // data present, card -> host
#define DATA_WRITE  0x00200000u             // data present, host -> card

#define CMD_GO_IDLE        (0u  << 24 | RESP_NONE)
#define CMD_ALL_SEND_CID   (2u  << 24 | RESP_136 | CRC_CHECK)
#define CMD_SEND_REL_ADDR  (3u  << 24 | RESP_48  | CRC_CHECK)
#define CMD_SELECT_CARD    (7u  << 24 | RESP_48B | CRC_CHECK)
#define CMD_SEND_IF_COND   (8u  << 24 | RESP_48  | CRC_CHECK)
#define CMD_SEND_CSD       (9u  << 24 | RESP_136 | CRC_CHECK)
#define CMD_SET_BLOCKLEN   (16u << 24 | RESP_48  | CRC_CHECK)
#define CMD_READ_SINGLE    (17u << 24 | RESP_48  | CRC_CHECK | DATA_READ)
#define CMD_WRITE_SINGLE   (24u << 24 | RESP_48  | CRC_CHECK | DATA_WRITE)
#define CMD_APP_CMD        (55u << 24 | RESP_48  | CRC_CHECK)
#define ACMD_SEND_OP_COND  (41u << 24 | RESP_48)

static uint32_t base_clock;
static uint32_t rca;                // card address, chosen by the card
static int high_capacity;           // SDHC/SDXC: addresses are sectors, not bytes
static uint64_t card_sectors;
static int ready;

static void delay_us(uint64_t us) {
    uint64_t end = timer_count() + us * (timer_freq() / 1000000);
    while (timer_count() < end) {
    }
}

static int wait_status_clear(uint32_t mask) {
    for (int i = 0; i < 1000000; i++) {
        if (!(EMMC_STATUS & mask)) {
            return 0;
        }
    }
    return -1;
}

// wait for an interrupt flag (polled, not a real interrupt); -1 on error/timeout
static int wait_interrupt(uint32_t mask) {
    uint64_t end = timer_count() + timer_freq();        // 1 second
    while (timer_count() < end) {
        uint32_t flags = EMMC_INTERRUPT;
        if (flags & INT_ERROR_MASK) {
            EMMC_INTERRUPT = flags;
            return -1;
        }
        if (flags & mask) {
            EMMC_INTERRUPT = flags & mask;
            return 0;
        }
    }
    return -1;
}

static int command(uint32_t cmd, uint32_t arg) {
    if (wait_status_clear(SR_CMD_INHIBIT)) {
        return -1;
    }
    if ((cmd & DATA_WRITE) && wait_status_clear(SR_DAT_INHIBIT)) {
        return -1;
    }
    EMMC_INTERRUPT = EMMC_INTERRUPT;    // clear old flags
    EMMC_ARG1 = arg;
    EMMC_CMDTM = cmd;
    return wait_interrupt(INT_CMD_DONE);
}

static int app_command(uint32_t cmd, uint32_t arg) {
    if (command(CMD_APP_CMD, rca << 16)) {
        return -1;
    }
    return command(cmd, arg);
}

static int set_clock(uint32_t hz) {
    if (wait_status_clear(SR_CMD_INHIBIT | SR_DAT_INHIBIT)) {
        return -1;
    }
    EMMC_CONTROL1 &= ~C1_CLK_EN;
    delay_us(10);
    uint32_t div = (base_clock + 2 * hz - 1) / (2 * hz);    // SDHCI v3: f = base / (2 * div)
    if (div > 0x3FF) {
        div = 0x3FF;
    }
    uint32_t c1 = EMMC_CONTROL1 & ~0xFFE0u;
    c1 |= ((div & 0xFF) << 8) | (((div >> 8) & 3) << 6) | C1_CLK_INTLEN;
    EMMC_CONTROL1 = c1;
    for (int i = 0; i < 100000 && !(EMMC_CONTROL1 & C1_CLK_STABLE); i++) {
    }
    if (!(EMMC_CONTROL1 & C1_CLK_STABLE)) {
        return -1;
    }
    EMMC_CONTROL1 |= C1_CLK_EN;
    delay_us(10);
    return 0;
}

// connect the SD card slot (GPIO 48-53) to the EMMC controller
static void setup_pins(void) {
    // GPIO 34-39 (Wi-Fi) -> input, so they let go of the controller
    GPFSEL(3) &= ~0x3FFFF000u;
    // GPIO 48-53 -> alternate function 3 (EMMC)
    uint32_t sel4 = GPFSEL(4) & ~(0x3Fu << 24);     // pins 48, 49
    GPFSEL(4) = sel4 | (7u << 24) | (7u << 27);
    uint32_t sel5 = GPFSEL(5) & ~0xFFFu;            // pins 50-53
    GPFSEL(5) = sel5 | 7u | (7u << 3) | (7u << 6) | (7u << 9);
    // pull-ups on CMD and DAT0-3 (pins 49-53)
    GPPUD = 2;
    delay_us(5);
    GPPUDCLK1 = 0x3Eu << 16;
    delay_us(5);
    GPPUD = 0;
    GPPUDCLK1 = 0;
}

static uint64_t capacity_from_csd(void) {
    // the controller drops the CRC byte: CSD bits [127:8] sit in RESP3..RESP0
    uint32_t r0 = EMMC_RESP0, r1 = EMMC_RESP1, r2 = EMMC_RESP2, r3 = EMMC_RESP3;
    uint32_t structure = (r3 >> 22) & 3;
    if (structure == 1) {                           // CSD v2 (SDHC/SDXC)
        uint32_t c_size = ((r1 >> 8) & 0xFFFFFF) & 0x3FFFFF;
        return ((uint64_t)c_size + 1) * 1024;
    }
    uint32_t read_bl_len = (r2 >> 8) & 0xF;         // CSD v1 (SDSC)
    uint32_t c_size = ((r2 & 0x3) << 10) | (r1 >> 22);
    uint32_t c_size_mult = (r1 >> 7) & 0x7;
    (void)r0;
    return ((uint64_t)(c_size + 1) << (c_size_mult + 2)) << read_bl_len >> 9;
}

int sd_init(void) {
    ready = 0;
#ifdef PI4
    return -100;        // the Pi 4 has a different controller (EMMC2): not supported yet
#endif
    setup_pins();

    base_clock = vcmailbox_get_clock_rate_measured(VCMAILBOX_CLOCK_EMMC);
    if (base_clock == 0) {
        base_clock = 50000000;
    }

    EMMC_CONTROL1 |= C1_SRST_HC;                    // reset the controller
    for (int i = 0; i < 100000 && (EMMC_CONTROL1 & C1_SRST_HC); i++) {
    }
    if (EMMC_CONTROL1 & C1_SRST_HC) {
        return -1;
    }
    EMMC_CONTROL1 |= C1_CLK_INTLEN | C1_TOUNIT_MAX;
    delay_us(10);
    if (set_clock(400000)) {                        // identification runs at 400 kHz
        return -2;
    }
    EMMC_INT_EN = 0xFFFFFFFF;                       // report every flag (we poll)
    EMMC_INT_MASK = 0xFFFFFFFF;

    rca = 0;
    command(CMD_GO_IDLE, 0);
    if (command(CMD_SEND_IF_COND, 0x1AA) || (EMMC_RESP0 & 0xFFF) != 0x1AA) {
        return -3;                                  // not an SD v2 card
    }
    uint32_t ocr = 0;
    for (int i = 0; i < 100; i++) {                 // wait for the card to power up
        if (app_command(ACMD_SEND_OP_COND, 0x51FF8000) == 0) {
            ocr = EMMC_RESP0;
            if (ocr & 0x80000000u) {
                break;
            }
        }
        delay_us(10000);
    }
    if (!(ocr & 0x80000000u)) {
        return -4;
    }
    high_capacity = (ocr >> 30) & 1;

    if (command(CMD_ALL_SEND_CID, 0) || command(CMD_SEND_REL_ADDR, 0)) {
        return -5;
    }
    rca = EMMC_RESP0 >> 16;
    if (command(CMD_SEND_CSD, rca << 16) == 0) {
        card_sectors = capacity_from_csd();
    }
    if (set_clock(25000000)) {                      // normal speed: 25 MHz
        return -6;
    }
    if (command(CMD_SELECT_CARD, rca << 16)) {
        return -7;
    }
    if (!high_capacity && command(CMD_SET_BLOCKLEN, 512)) {
        return -8;
    }
    ready = 1;
    return 0;
}

int sd_read(uint32_t lba, void *buf) {
    if (!ready) {
        return -1;
    }
    EMMC_BLKSIZECNT = (1u << 16) | 512;
    if (command(CMD_READ_SINGLE, high_capacity ? lba : lba * 512)) {
        return -1;
    }
    if (wait_interrupt(INT_READ_RDY)) {
        return -1;
    }
    uint32_t *p = buf;
    for (int i = 0; i < 128; i++) {
        p[i] = EMMC_DATA;
    }
    return wait_interrupt(INT_DATA_DONE);
}

int sd_write(uint32_t lba, const void *buf) {
    if (!ready) {
        return -1;
    }
    EMMC_BLKSIZECNT = (1u << 16) | 512;
    if (command(CMD_WRITE_SINGLE, high_capacity ? lba : lba * 512)) {
        return -1;
    }
    if (wait_interrupt(INT_WRITE_RDY)) {
        return -1;
    }
    const uint32_t *p = buf;
    for (int i = 0; i < 128; i++) {
        EMMC_DATA = p[i];
    }
    return wait_interrupt(INT_DATA_DONE);
}

int sd_ready(void) { return ready; }
int sd_is_high_capacity(void) { return high_capacity; }
uint64_t sd_sectors(void) { return card_sectors; }
uint32_t sd_base_clock(void) { return base_clock; }
