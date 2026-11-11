// wifi.c - Wi-Fi for MyOS on the Pi Zero 2 W (and Pi 3 / Zero W).
//
// The Wi-Fi chip (Cypress/Broadcom CYW43436, which calls itself a 43430)
// is a small computer of its own: an ARM Cortex-M3 with 512 KB of RAM that
// runs Broadcom's Wi-Fi firmware. MyOS talks to it over SDIO (sdio.c):
//
//  1. bring-up: through SDIO function 1 we see the chip's internal bus (the
//     "backplane") 32 KB at a time. We stop its ARM, copy the firmware file
//     and the board settings ("NVRAM", a text file) into its RAM, and start
//     the ARM again.
//  2. frames: function 2 carries "SDPCM" frames in both directions. Channel 0
//     are commands ("ioctls", e.g. scan, join, get MAC address) and their
//     answers, channel 1 events (a scan result, link up ...), channel 2
//     network packets (Ethernet frames).
//  3. the firmware does all the Wi-Fi work itself, even WPA2's 4-way
//     handshake ("sup_wpa"): MyOS only gives it the network name and password.
//  4. packets become a second lwIP network interface ("wl"), next to the
//     USB one; DHCP asks the router for an address.
//
// Files needed on MyFS (from Raspberry Pi OS / RPi-Distro firmware-nonfree,
// see tools/get-deps.ps1):  brcmfmac43436-sdio.bin, .txt, .clm_blob  for
// chip revision 2 (most Zero 2 W),  brcmfmac43436s-sdio.bin, .txt  for rev 1.
#include <stdint.h>
#include <string.h>
#include "console.h"
#include "myfs.h"
#include "print.h"
#include "proc.h"
#include "sdio.h"
#include "timer.h"
#include "wifi.h"
#ifndef NO_USB
#include "lwip/dhcp.h"
#include "lwip/etharp.h"
#include "lwip/inet_chksum.h"
#include "lwip/ip4.h"
#include "lwip/netif.h"
#include "lwip/prot/icmp.h"
#include "lwip/prot/ip4.h"
#include "lwip/raw.h"
#include "lwip/timeouts.h"
#include "netif/ethernet.h"
#include "net.h"
#endif

// ---------------------------------------------------------------- chip registers
// function 1 (backplane) registers
#define SB_ADDR_LOW     0x1000A         // which 32 KB of the backplane function 1 shows
#define SB_ADDR_MID     0x1000B
#define SB_ADDR_HIGH    0x1000C
#define FRAMECTRL       0x1000D
#define CHIPCLKCSR      0x1000E
#define PULLUP          0x1000F
#define WATERMARK       0x10008
#define RFRAMEBC_LO     0x1001B
#define RFRAMEBC_HI     0x1001C
#define SB_32BIT        0x8000          // window offset | this = 32-bit access

#define CLK_FORCE_ALP   0x01
#define CLK_FORCE_HT    0x02
#define CLK_ALP_REQ     0x08
#define CLK_HT_REQ      0x10
#define CLK_HW_REQ_OFF  0x20
#define CLK_ALP_AVAIL   0x40
#define CLK_HT_AVAIL    0x80

// cores on the backplane (43430); each has a "wrapper" 1 MB higher
#define CHIPCOMMON      0x18000000
#define SDIOD_CORE      0x18002000
#define D11_CORE        0x18001000
#define ARM_CORE        0x18003000
#define SOCRAM_CORE     0x18004000
#define WRAPPER         0x00100000
#define AI_IOCTRL       0x408
#define AI_RESETCTRL    0x800
#define IOCTRL_CLK      0x01
#define IOCTRL_FGC      0x02
#define D11_PHYCLOCKEN  0x04
#define D11_PHYRESET    0x08

// SDIO device core registers
#define SD_INTSTATUS        (SDIOD_CORE + 0x20)
#define SD_HOSTINTMASK      (SDIOD_CORE + 0x24)
#define SD_FUNCINTMASK      (SDIOD_CORE + 0x34)
#define SD_TOSBMAILBOX      (SDIOD_CORE + 0x40)
#define SD_TOSBMAILBOXDATA  (SDIOD_CORE + 0x48)
#define SD_TOHOSTMAILBOXDATA (SDIOD_CORE + 0x4C)
#define I_HMB_FRAME_IND     (1u << 6)
#define I_HMB_HOST_INT      (1u << 7)
#define HOSTINTMASK         0x000000F0u
#define SMB_INT_ACK         (1u << 1)
#define HMB_DATA_FWHALT     0x10

#define RAM_SIZE        0x80000         // 512 KB in the 43430
#define CHIP_43430      0xA9A6          // 43430 and 43436 both say this
#define F2_ADDR         0x8000

// firmware commands (ioctls) and events
#define WLC_UP              2
#define WLC_SET_INFRA       20
#define WLC_SET_AUTH        22
#define WLC_GET_BSSID       23
#define WLC_GET_SSID        25
#define WLC_SET_SSID        26
#define WLC_GET_CHANNEL     29
#define WLC_DISASSOC        52
#define WLC_SET_PM          86
#define WLC_GET_RSSI        127
#define WLC_SET_WSEC        134
#define WLC_SET_WPA_AUTH    165
#define WLC_GET_VAR         262
#define WLC_SET_VAR         263
#define WLC_SET_WSEC_PMK    268

#define E_SET_SSID      0
#define E_AUTH          3
#define E_DEAUTH        5
#define E_DEAUTH_IND    6
#define E_ASSOC         7
#define E_DISASSOC      11
#define E_DISASSOC_IND  12
#define E_LINK          16
#define E_PSK_SUP       46
#define E_ESCAN_RESULT  69

// ---------------------------------------------------------------- state
enum { ST_NOCHIP = -1, ST_OFF, ST_STARTING, ST_ON, ST_JOINING, ST_JOINED, ST_ONLINE };
static int state = ST_OFF;
static int busy_pid = -1;               // who is starting / talking to the chip
static int auto_reconnect;              // the network dropped us: join again by ourselves
static uint64_t reconnect_at;           // ... at this time (millis)
static int reconnect_tries;
static int connect_start(const char *ssid, const char *pass, int save);
static uint32_t chip_id, chip_rev;
static char fw_version[96];
static uint8_t mac[6];
static char message[64];                // the last thing that happened (for 'wifi status' and apps)
static int verbose;

static uint8_t ctl_tx[1600] __attribute__((aligned(8)));    // commands
static uint8_t data_tx[1600] __attribute__((aligned(8), unused));   // packets
static uint8_t rxbuf[4608] __attribute__((aligned(8)));     // a received frame (+ padding)
static uint8_t tx_seq, tx_max = 4;      // frame counter, and how far the chip lets us count
static uint16_t ioctl_id;
static int resp_ready;
static int32_t resp_status;
static uint32_t resp_len;
static uint8_t resp[1600] __attribute__((aligned(8)));
static int in_pump;
static uint64_t last_poll;
static uint32_t frames_in, frames_out, rx_errors;

// scan results
#define MAX_NETS 32
typedef struct {
    char ssid[33];
    uint8_t bssid[6];
    int16_t rssi;
    uint8_t channel;
    uint8_t security;                   // 0 open, 1 WEP, 2 WPA, 3 WPA2, 4 WPA3 only
} net_t;
static net_t nets[MAX_NETS];
static int nnets;
static int scanning;
static uint64_t scan_started, scan_done_at;

// joining
static char join_ssid[33], join_pass[64];
static int join_security;
static int join_pending;                // 1 = scan first, then join; 2 = join now
static int join_assoc, join_link, join_keyed, join_failed;
static uint64_t join_started;
static int join_save;                   // remember it once it works

// ---------------------------------------------------------------- small helpers
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t get32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t get32be(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }

static void delay_ms(uint32_t ms) {
    uint64_t end = millis() + ms;
    while (millis() < end) {
    }
}

static void copy_text(char *dst, const char *src, size_t max) {    // at most max chars + 0
    size_t n = 0;
    while (n < max && src[n]) { dst[n] = src[n]; n++; }
    dst[n] = 0;
}

static void set_message(const char *s) { copy_text(message, s, sizeof message - 1); }

static void say(const char *s) {
    if (verbose) console_puts(s);
}

static void say_hex(const char *s, uint32_t v) {
    if (!verbose) return;
    console_puts(s);
    console_puts("0x");
    print_hex(v, 8);
    console_puts("\n");
}

static void mac_text(const uint8_t *m, char *out) {
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 6; i++) {
        out[i * 3] = hex[m[i] >> 4];
        out[i * 3 + 1] = hex[m[i] & 15];
        out[i * 3 + 2] = i < 5 ? ':' : 0;
    }
}

// ---------------------------------------------------------------- backplane
static uint32_t window = 1;             // (not a valid window: set on first use)

static int bp_window(uint32_t addr) {
    uint32_t w = addr & ~0x7FFFu;
    if (w == window) return 0;
    window = 1;
    if (sdio_cmd52(1, 1, SB_ADDR_LOW, (uint8_t)((w >> 8) & 0x80), 0) ||
        sdio_cmd52(1, 1, SB_ADDR_MID, (uint8_t)(w >> 16), 0) ||
        sdio_cmd52(1, 1, SB_ADDR_HIGH, (uint8_t)(w >> 24), 0)) return -1;
    window = w;
    return 0;
}

static int bp_read32(uint32_t addr, uint32_t *v) {
    uint8_t b[4] __attribute__((aligned(4)));
    if (bp_window(addr) || sdio_cmd53(0, 1, (addr & 0x7FFF) | SB_32BIT, 1, b, 4)) return -1;
    *v = get32(b);
    return 0;
}

static uint32_t rd32(uint32_t addr) {
    uint32_t v = 0xFFFFFFFF;
    bp_read32(addr, &v);
    return v;
}

static int wr32(uint32_t addr, uint32_t v) {
    uint8_t b[4] __attribute__((aligned(4)));
    put32(b, v);
    if (bp_window(addr)) return -1;
    return sdio_cmd53(1, 1, (addr & 0x7FFF) | SB_32BIT, 1, b, 4);
}

// copy to/from the chip's RAM (any length, crossing 32 KB windows)
static int bp_copy(int write, uint32_t addr, uint8_t *data, uint32_t len) {
    while (len) {
        uint32_t n = 0x8000 - (addr & 0x7FFF);
        if (n > len) n = len;
        if (bp_window(addr)) return -1;
        if (sdio_cmd53(write, 1, (addr & 0x7FFF) | SB_32BIT, 1, data, n)) return -1;
        addr += n;
        data += n;
        len -= n;
    }
    return 0;
}

static uint8_t f1_read(uint32_t reg) {
    uint8_t v = 0;
    sdio_cmd52(0, 1, reg, 0, &v);
    return v;
}

static void f1_write(uint32_t reg, uint8_t v) {
    sdio_cmd52(1, 1, reg, v, 0);
}

// cores: hold in reset / take out of reset, with core-specific flags
// (like Linux's brcmf_chip_ai_coredisable / _resetcore and Plan 9's sbdisable / sbreset)
static void core_disable(uint32_t core, uint32_t prereset, uint32_t reset) {
    uint32_t w = core + WRAPPER;
    if (!(rd32(w + AI_RESETCTRL) & 1)) {
        wr32(w + AI_IOCTRL, prereset | IOCTRL_FGC | IOCTRL_CLK);
        rd32(w + AI_IOCTRL);
        wr32(w + AI_RESETCTRL, 1);
        delay_ms(1);
        for (int i = 0; i < 300 && !(rd32(w + AI_RESETCTRL) & 1); i++) delay_ms(1);
    }
    wr32(w + AI_IOCTRL, reset | IOCTRL_FGC | IOCTRL_CLK);
    rd32(w + AI_IOCTRL);
}

static void core_reset(uint32_t core, uint32_t prereset, uint32_t reset) {
    uint32_t w = core + WRAPPER;
    core_disable(core, prereset, reset);
    for (int i = 0; i < 50 && (rd32(w + AI_RESETCTRL) & 1); i++) {
        wr32(w + AI_RESETCTRL, 0);
        delay_ms(1);
    }
    wr32(w + AI_IOCTRL, reset | IOCTRL_CLK);
    rd32(w + AI_IOCTRL);
}

static int core_is_up(uint32_t core) {
    uint32_t w = core + WRAPPER;
    return (rd32(w + AI_IOCTRL) & (IOCTRL_FGC | IOCTRL_CLK)) == IOCTRL_CLK &&
           !(rd32(w + AI_RESETCTRL) & 1);
}

// ---------------------------------------------------------------- firmware files
static const char *first_file(const char *const *names) {
    for (int i = 0; names[i]; i++)
        if (myfs_size(names[i]) > 0) return names[i];
    return 0;
}

static const char *fw_name, *nvram_name, *clm_name;

static void pick_files(void) {
    static const char *const bin1[] = { "brcmfmac43436s-sdio.bin", "brcmfmac43430-sdio.bin", 0 };
    static const char *const txt1[] = { "brcmfmac43436s-sdio.txt", "brcmfmac43430-sdio.txt", 0 };
    static const char *const clm1[] = { "brcmfmac43430-sdio.clm_blob", 0 };
    static const char *const bin2[] = { "brcmfmac43436-sdio.bin", "brcmfmac43430b0-sdio.bin", 0 };
    static const char *const txt2[] = { "brcmfmac43436-sdio.txt", "brcmfmac43430b0-sdio.txt", 0 };
    static const char *const clm2[] = { "brcmfmac43436-sdio.clm_blob", "brcmfmac43430b0-sdio.clm_blob", 0 };
    int b0 = chip_rev >= 2;
    fw_name = first_file(b0 ? bin2 : bin1);
    nvram_name = first_file(b0 ? txt2 : txt1);
    clm_name = first_file(b0 ? clm2 : clm1);
}

static uint8_t chunk[4096] __attribute__((aligned(8)));

static int load_firmware(void) {
    int32_t size = myfs_size(fw_name);
    if (size <= 0 || size > RAM_SIZE - 8192) return -1;
    for (uint32_t off = 0; off < (uint32_t)size; off += sizeof chunk) {
        int n = myfs_read(fw_name, off, chunk, sizeof chunk);
        if (n <= 0) return -1;
        while (n & 3) chunk[n++] = 0;
        if (bp_copy(1, off, chunk, (uint32_t)n)) {
            if (verbose) {
                console_puts("copy failed at ");
                print_dec(off);
                console_puts(": ");
                console_puts(sdio_error());
                console_puts("\n");
            }
            return -1;
        }
        if ((off & 0xFFFF) == 0) console_poll();    // keep USB going
    }
    // read the start back: a quick check that the copy worked
    static uint8_t check[64] __attribute__((aligned(8)));
    if (myfs_read(fw_name, 0, chunk, sizeof check) != (int)sizeof check) return -1;
    if (bp_copy(0, 0, check, sizeof check) || memcmp(check, chunk, sizeof check) != 0) return -2;
    return 0;
}

// NVRAM: "key=value" lines -> "key=value\0key=value\0\0", at the very end of RAM,
// followed by its length in words (and the length inverted, as a check)
static int load_nvram(void) {
    int32_t size = myfs_size(nvram_name);
    if (size <= 0 || size > (int32_t)sizeof chunk) return -1;
    static char text[4096];
    if (myfs_read(nvram_name, 0, text, (uint32_t)size) != size) return -1;
    uint32_t n = 0;
    int i = 0;
    while (i < size) {
        int start = i;
        while (i < size && text[i] != '\n') i++;
        int end = i++;
        while (start < end && (text[start] == ' ' || text[start] == '\t')) start++;
        while (end > start && (text[end - 1] == '\r' || text[end - 1] == ' ' || text[end - 1] == '\t')) end--;
        if (start == end || text[start] == '#') continue;
        if (n + (uint32_t)(end - start) + 8 > sizeof chunk) return -1;
        memcpy(chunk + n, text + start, (size_t)(end - start));
        n += (uint32_t)(end - start);
        chunk[n++] = 0;
    }
    chunk[n++] = 0;
    while (n & 3) chunk[n++] = 0;
    uint32_t words = n / 4;
    put32(chunk + n, (~words << 16) | (words & 0xFFFF));
    return bp_copy(1, RAM_SIZE - n - 4, chunk, n + 4);
}

// ---------------------------------------------------------------- frames
// SDPCM header: u16 length, u16 ~length, seq, channel, next length, data offset,
// flow control, credit (the highest seq we may send), 2 reserved
#define SDPCM_HDR 12

static void rx_abort(void) {            // throw away a frame we couldn't read
    sdio_cmd52(1, 0, 0x06, 2, 0);       // CCCR abort: function 2
    f1_write(FRAMECTRL, 1);             // terminate the frame
    for (int i = 0; i < 100; i++)
        if (f1_read(RFRAMEBC_LO) == 0 && f1_read(RFRAMEBC_HI) == 0) break;
    rx_errors++;
}

static int can_send(void) {
    uint8_t room = (uint8_t)(tx_max - tx_seq);
    return room != 0 && !(room & 0x80);
}

static int send_frame(uint8_t *f, uint32_t len, int channel) {
    put16(f, (uint16_t)len);
    put16(f + 2, (uint16_t)~len);
    f[4] = tx_seq;
    f[5] = (uint8_t)channel;
    f[6] = 0;
    f[7] = SDPCM_HDR;
    f[8] = f[9] = f[10] = f[11] = 0;
    uint32_t padded = (len + 3) & ~3u;
    if (padded > 512) padded = (padded + 511) & ~511u; // whole 512-byte blocks
    for (uint32_t i = len; i < padded; i++) f[i] = 0;
    if (sdio_cmd53(1, 2, F2_ADDR, 1, f, padded)) return -1;
    tx_seq++;
    frames_out++;
    return 0;
}

#ifndef NO_USB
static struct netif wl_netif;
static int netif_added;
#define RXQ 16
static struct pbuf *rxq[RXQ];           // packets that came while lwIP was busy
static int rxq_head, rxq_tail;
static void deliver_packet(struct pbuf *p, int now);
#endif
static void handle_event(const uint8_t *eth, uint32_t len);

static void handle_frame(uint32_t len) {
    uint8_t *f = rxbuf;
    uint8_t credit = f[9];
    if ((uint8_t)(credit - tx_seq) > 0x40) credit = (uint8_t)(tx_seq + 2);  // nonsense: be careful
    tx_max = credit;
    int channel = f[5] & 0x0F;
    uint32_t off = f[7];
    if (off < SDPCM_HDR || off > len) return;
    uint8_t *p = f + off;
    uint32_t plen = len - off;
    if (channel == 0) {                         // the answer to a command
        if (plen < 16) return;
        uint32_t flags = get32(p + 8);
        if ((flags >> 16) != ioctl_id) return;  // an old answer
        resp_status = (flags & 1) ? (int32_t)get32(p + 12) : 0;
        resp_len = plen - 16 > sizeof resp ? sizeof resp : plen - 16;
        memcpy(resp, p + 16, resp_len);
        resp_ready = 1;
    } else if (channel == 1 || channel == 2) {  // an event, or a packet (both: BDC header + Ethernet)
        if (plen < 4) return;
        uint32_t skip = 4 + (uint32_t)p[3] * 4;
        if (skip > plen) return;
        uint8_t *eth = p + skip;
        uint32_t elen = plen - skip;
        if (elen >= 14 && eth[12] == 0x88 && eth[13] == 0x6C) {
            handle_event(eth, elen);
            return;
        }
#ifndef NO_USB
        if (channel == 2 && netif_added && elen >= 14) {
            struct pbuf *q = pbuf_alloc(PBUF_RAW, (u16_t)elen, PBUF_POOL);
            if (q) {
                pbuf_take(q, eth, (u16_t)elen);
                deliver_packet(q, 0);           // (rxbuf may be reused from here on)
            }
        }
#endif
    }
}

// read one frame from the chip; 1 = got one, 0 = none waiting, -1 = error
static int read_frame(void) {
    if (sdio_cmd53(0, 2, F2_ADDR, 0, rxbuf, 4)) {
        rx_abort();
        return -1;
    }
    uint16_t len = get16(rxbuf), check = get16(rxbuf + 2);
    if ((len | check) == 0) return 0;
    if ((uint16_t)(len ^ check) != 0xFFFF || len < SDPCM_HDR || len > 4000) {
        rx_abort();
        return -1;
    }
    uint32_t rest = len - 4u;
    rest = (rest + 3) & ~3u;
    if (rest > 512) rest = (rest + 511) & ~511u;
    if (sdio_cmd53(0, 2, F2_ADDR, 0, rxbuf + 4, rest)) {
        rx_abort();
        return -1;
    }
    frames_in++;
    if (verbose > 1) {
        console_puts("  <- frame len ");
        print_dec(len);
        console_puts(" seq ");
        print_dec(rxbuf[4]);
        console_puts(" ch ");
        print_dec(rxbuf[5]);
        console_puts(" off ");
        print_dec(rxbuf[7]);
        console_puts(" credit ");
        print_dec(rxbuf[9]);
        console_puts("\n");
    }
    handle_frame(len);
    return 1;
}

// look at the chip's interrupt flags and read what it has for us
static void pump(void) {
    if (in_pump || state < ST_ON) return;
    in_pump = 1;
    uint32_t st;
    if (bp_read32(SD_INTSTATUS, &st) == 0 && st != 0xFFFFFFFF) {
        st &= HOSTINTMASK;                      // (the other bits belong to the chip itself)
        if (st) wr32(SD_INTSTATUS, st);         // clear what we saw
        if (st & I_HMB_HOST_INT) {              // a mailbox message: acknowledge it
            uint32_t d = rd32(SD_TOHOSTMAILBOXDATA);
            wr32(SD_TOSBMAILBOX, SMB_INT_ACK);
            if (d & HMB_DATA_FWHALT) set_message("the Wi-Fi firmware stopped");
        }
        if (st & I_HMB_FRAME_IND) {
            for (int i = 0; i < 32; i++)
                if (read_frame() <= 0) break;
        }
    }
    in_pump = 0;
}

static int wait_credit(void) {
    uint64_t end = millis() + 200;
    while (!can_send()) {
        if (millis() > end) return -1;
        int was = in_pump;
        in_pump = 0;
        pump();
        in_pump = was;
    }
    return 0;
}

// ---------------------------------------------------------------- commands (BCDC)
// after the SDPCM header: u32 command, u32 length, u32 flags (id << 16 | set), u32 status
static int ioctl(uint32_t cmd, int set, const void *data, uint32_t len, void *out, uint32_t outlen) {
    uint32_t dlen = len > outlen ? len : outlen;
    if (SDPCM_HDR + 16 + dlen > sizeof ctl_tx - 64) return -100;
    uint8_t *f = ctl_tx;
    memset(f, 0, SDPCM_HDR + 16 + dlen);
    ioctl_id++;
    put32(f + 12, cmd);
    put32(f + 16, dlen);
    put32(f + 20, (uint32_t)ioctl_id << 16 | (set ? 2u : 0u));
    if (len) memcpy(f + 28, data, len);
    if (wait_credit()) {
        if (verbose > 1) console_puts("  ioctl: no credit\n");
        return -101;
    }
    resp_ready = 0;
    if (send_frame(f, SDPCM_HDR + 16 + dlen, 0)) return -102;
    uint64_t end = millis() + 2000;
    while (!resp_ready) {
        if (millis() > end) {
            if (verbose > 1) {
                console_puts("  ioctl ");
                print_dec(cmd);
                console_puts(": no answer\n");
            }
            return -103;
        }
        int was = in_pump;
        in_pump = 0;
        pump();
        in_pump = was;
    }
    if (verbose > 1) {
        console_puts("  ioctl ");
        print_dec(cmd);
        console_puts(" status ");
        print_signed(resp_status);
        console_puts("\n");
    }
    if (resp_status) return resp_status < 0 ? resp_status : -resp_status;
    if (out && outlen) memcpy(out, resp, resp_len < outlen ? resp_len : outlen);
    return 0;
}

static int ioctl_set32(uint32_t cmd, uint32_t v) {
    uint8_t b[4];
    put32(b, v);
    return ioctl(cmd, 1, b, 4, 0, 0);
}

static int iovar_get(const char *name, void *out, uint32_t outlen) {
    uint32_t n = (uint32_t)strlen(name) + 1;
    return ioctl(WLC_GET_VAR, 0, name, n, out, outlen);  // (asks for max(n, outlen) bytes)
}

static int iovar_set(const char *name, const void *val, uint32_t vlen) {
    static uint8_t b[1600];
    uint32_t n = (uint32_t)strlen(name) + 1;
    if (n + vlen > sizeof b) return -100;
    memcpy(b, name, n);
    memcpy(b + n, val, vlen);
    return ioctl(WLC_SET_VAR, 1, b, n + vlen, 0, 0);
}

static int iovar_set32(const char *name, uint32_t v) {
    uint8_t b[4];
    put32(b, v);
    return iovar_set(name, b, 4);
}

static int iovar_set_bsscfg32(const char *name, uint32_t v) {   // "bsscfg:NAME" for interface 0
    uint8_t b[8];
    put32(b, 0);
    put32(b + 4, v);
    char full[40] = "bsscfg:";
    strcat(full, name);
    return iovar_set(full, b, 8);
}

// the CLM blob (which channels/power the chip may use where), 1400 bytes at a time
static int load_clm(void) {
    int32_t size = myfs_size(clm_name);
    if (size <= 0) return -1;
    static uint8_t b[12 + 1400];
    for (int32_t off = 0; off < size; off += 1400) {
        int32_t n = size - off < 1400 ? size - off : 1400;
        uint16_t flag = 1u << 12;                   // handler version 1
        if (off == 0) flag |= 2;                    // first piece
        if (off + n >= size) flag |= 4;             // last piece
        put16(b, flag);
        put16(b + 2, 2);                            // type: CLM
        put32(b + 4, (uint32_t)n);
        put32(b + 8, 0);
        if (myfs_read(clm_name, (uint32_t)off, b + 12, (uint32_t)n) != n) return -1;
        int r = iovar_set("clmload", b, 12 + (uint32_t)n);
        if (r) return r;
    }
    uint8_t st[4] = {0};
    if (iovar_get("clmload_status", st, 4) == 0 && get32(st) != 0) return -2;
    return 0;
}

// ---------------------------------------------------------------- bring-up
static int fail_start(const char *why) {
    set_message(why);
    say(why);
    say("\n");
    state = ST_OFF;
    return -1;
}

static int chip_start(void) {
    if (state >= ST_ON) return 0;
#ifdef PI4
    state = ST_NOCHIP;
    set_message("Wi-Fi on the Pi 4 is not supported yet");
    return -1;
#endif
    if (state == ST_NOCHIP) return -1;
    state = ST_STARTING;
    window = 1;
    tx_seq = 0;
    tx_max = 4;
    say("SDIO: powering the Wi-Fi chip up...\n");
    if (sdio_init()) {
        state = ST_NOCHIP;
        set_message("no Wi-Fi chip");
        if (verbose) {
            console_puts("SDIO: ");
            console_puts(sdio_error());
            console_puts("\n");
        }
        return -1;
    }
    if (verbose) {
        console_puts("SDIO: card found, 4-bit bus, ");
        print_dec(sdio_clock_hz() / 1000);
        console_puts(" kHz\n");
    }
    if (sdio_set_blocksize(0, 64) || sdio_set_blocksize(1, 64) || sdio_set_blocksize(2, 512))
        return fail_start("SDIO: can't set block sizes");
    if (sdio_enable_function(1)) return fail_start("SDIO: function 1 didn't start");
    sdio_cmd52(1, 0, 0x04, 0x07, 0);            // interrupts: master, F1, F2 (we poll anyway)
    if (0 && sdio_high_speed() && verbose) {
        console_puts("SDIO: high speed, ");
        print_dec(sdio_clock_hz() / 1000);
        console_puts(" kHz\n");
    }

    // the backplane needs a clock: ask for the slow one (ALP) first
    f1_write(CHIPCLKCSR, 0);
    delay_ms(1);
    f1_write(CHIPCLKCSR, CLK_HW_REQ_OFF | CLK_ALP_REQ);
    uint64_t end = millis() + 500;
    while (!(f1_read(CHIPCLKCSR) & (CLK_ALP_AVAIL | CLK_HT_AVAIL))) {
        if (millis() > end) return fail_start("Wi-Fi chip: no backplane clock (ALP)");
    }
    f1_write(CHIPCLKCSR, CLK_HW_REQ_OFF | CLK_FORCE_ALP);
    delay_ms(1);
    f1_write(PULLUP, 0);

    uint32_t id = rd32(CHIPCOMMON);
    chip_id = id & 0xFFFF;
    chip_rev = (id >> 16) & 0xF;
    say_hex("Chip ID register: ", id);
    if (chip_id != CHIP_43430) {
        state = ST_NOCHIP;
        set_message("unknown Wi-Fi chip");
        return -1;
    }
    pick_files();
    if (!fw_name || !nvram_name) {
        state = ST_OFF;
        set_message(chip_rev >= 2 ? "firmware missing: brcmfmac43436-sdio.bin/.txt"
                                  : "firmware missing: brcmfmac43436s-sdio.bin/.txt");
        return -1;
    }
    wr32(CHIPCOMMON + 0x58, 0);                 // GPIO pull-ups / pull-downs off
    wr32(CHIPCOMMON + 0x5C, 0);

    // stop the chip's ARM, reset the radio (D11) and the RAM, copy firmware +
    // NVRAM, start the ARM (the order of Plan 9's ether4330.c / Linux brcmfmac)
    core_disable(ARM_CORE, 0, 0);
    core_reset(D11_CORE, D11_PHYRESET | D11_PHYCLOCKEN, D11_PHYCLOCKEN);
    core_reset(SOCRAM_CORE, 0, 0);
    wr32(SOCRAM_CORE + 0x10, 3);                // 43430: bank 3 is not "remapped"
    wr32(SOCRAM_CORE + 0x44, 0);
    say("Copying the firmware...\n");
    uint64_t t0 = millis();
    int r = load_firmware();
    if (r) return fail_start(r == -2 ? "Wi-Fi firmware: copy check failed" : "Wi-Fi firmware: copy failed");
    if (load_nvram()) return fail_start("Wi-Fi NVRAM: copy failed");
    if (verbose) {
        console_puts("Copied in ");
        print_dec(millis() - t0);
        console_puts(" ms\n");
    }
    wr32(SD_INTSTATUS, 0xFFFFFFFF);
    core_reset(ARM_CORE, 0, 0);
    if (!core_is_up(ARM_CORE)) return fail_start("Wi-Fi chip: its ARM didn't start");

    // the fast clock (HT), forced on from now on
    f1_write(CHIPCLKCSR, 0);
    delay_ms(1);
    f1_write(CHIPCLKCSR, CLK_HT_REQ);
    end = millis() + 3000;
    while (!(f1_read(CHIPCLKCSR) & CLK_HT_AVAIL)) {
        if (millis() > end) return fail_start("Wi-Fi chip: no HT clock after starting");
        console_poll();
    }
    f1_write(CHIPCLKCSR, f1_read(CHIPCLKCSR) | CLK_FORCE_HT);
    delay_ms(10);
    say_hex("Clock: ", f1_read(CHIPCLKCSR));

    // tell the firmware our protocol version, which interrupts we want,
    // then switch on function 2 (frames) and wait until the firmware is ready
    wr32(SD_TOSBMAILBOXDATA, 4u << 16);
    wr32(SD_HOSTINTMASK, HOSTINTMASK);
    uint8_t ioen = 0;
    sdio_cmd52(0, 0, 0x02, 0, &ioen);
    sdio_cmd52(1, 0, 0x02, ioen | 0x04, 0);
    end = millis() + 3000;
    uint8_t rdy = 0;
    while (!(rdy & 0x04)) {
        if (millis() > end) return fail_start("Wi-Fi firmware didn't start (function 2 not ready)");
        sdio_cmd52(0, 0, 0x03, 0, &rdy);
        console_poll();
    }
    sdio_cmd52(1, 0, 0x04, 0x07, 0);            // interrupts: master, F1, F2 (we poll anyway)
    f1_write(WATERMARK, 8);
    state = ST_ON;
    say("Firmware running.\n");

    // first commands: CLM, versions, MAC address, events, up
    if (clm_name) {
        int c = load_clm();
        if (c && verbose) {
            console_puts("CLM blob: failed (");
            print_signed(c);
            console_puts(")\n");
        }
    }
    memset(fw_version, 0, sizeof fw_version);
    iovar_get("ver", fw_version, sizeof fw_version - 1);
    for (char *s = fw_version; *s; s++)
        if (*s == '\n' || *s == '\r') *s = 0;
    iovar_get("cur_etheraddr", mac, 6);
    iovar_set32("bus:txglom", 0);
    iovar_set32("apsta", 1);
    iovar_set32("ampdu_ba_wsize", 8);
    uint8_t events[16];
    memset(events, 0, sizeof events);
    static const uint8_t wanted[] = { E_SET_SSID, E_AUTH, E_DEAUTH, E_DEAUTH_IND, E_ASSOC, E_DISASSOC,
                                      E_DISASSOC_IND, E_LINK, E_PSK_SUP, E_ESCAN_RESULT };
    for (unsigned i = 0; i < sizeof wanted; i++) events[wanted[i] / 8] |= (uint8_t)(1u << (wanted[i] % 8));
    iovar_set("event_msgs", events, sizeof events);
    if (ioctl(WLC_UP, 1, 0, 0, 0, 0)) return fail_start("Wi-Fi firmware didn't accept 'up'");
    ioctl_set32(WLC_SET_PM, 0);                 // no power saving: answers come quickly
    set_message("Wi-Fi is on");
    return 0;
}

// ---------------------------------------------------------------- scanning
static void parse_bss(const uint8_t *b, uint32_t len) {
    if (len < 128) return;
    uint32_t blen = get32(b + 4);
    if (blen > len) blen = len;
    net_t n;
    memset(&n, 0, sizeof n);
    memcpy(n.bssid, b + 8, 6);
    uint16_t cap = get16(b + 16);
    uint32_t sl = b[18] > 32 ? 32 : b[18];
    memcpy(n.ssid, b + 19, sl);
    n.ssid[sl] = 0;
    uint16_t chanspec = get16(b + 72);
    n.rssi = (int16_t)get16(b + 78);
    n.channel = b[88] ? b[88] : (uint8_t)(chanspec & 0xFF);
    uint32_t ie_off = get16(b + 116), ie_len = get32(b + 120);
    int wpa = 0, rsn_psk = 0, rsn_sae = 0;
    if ((uint64_t)ie_off + ie_len <= blen) {
        const uint8_t *ie = b + ie_off, *ie_end = ie + ie_len;
        while (ie + 2 <= ie_end && ie + 2 + ie[1] <= ie_end) {
            const uint8_t *d = ie + 2;
            uint32_t dl = ie[1];
            if (ie[0] == 48 && dl >= 8) {               // RSN (WPA2/WPA3)
                uint32_t pos = 6;                       // version, group cipher
                uint32_t pc = get16(d + pos);
                pos += 2 + 4 * pc;
                if (pos + 2 <= dl) {
                    uint32_t ac = get16(d + pos);
                    pos += 2;
                    for (uint32_t k = 0; k < ac && pos + 4 <= dl; k++, pos += 4) {
                        if (d[pos] == 0x00 && d[pos + 1] == 0x0F && d[pos + 2] == 0xAC) {
                            if (d[pos + 3] == 2 || d[pos + 3] == 6) rsn_psk = 1;
                            if (d[pos + 3] == 8) rsn_sae = 1;
                        }
                    }
                } else {
                    rsn_psk = 1;
                }
            } else if (ie[0] == 221 && dl >= 4 && d[0] == 0x00 && d[1] == 0x50 && d[2] == 0xF2 && d[3] == 1) {
                wpa = 1;                                // WPA (version 1)
            }
            ie += 2 + dl;
        }
    }
    n.security = rsn_psk ? 3 : rsn_sae ? 4 : wpa ? 2 : (cap & 0x10) ? 1 : 0;
    if (n.ssid[0] == 0) return;                         // a hidden network
    for (int i = 0; i < nnets; i++) {                   // the same network again: keep the strongest
        if (strcmp(nets[i].ssid, n.ssid) == 0) {
            if (n.rssi > nets[i].rssi) nets[i] = n;
            return;
        }
    }
    if (nnets < MAX_NETS) nets[nnets++] = n;
}

static int scan_start(void) {
    uint8_t p[4 + 2 + 2 + 36 + 6 + 1 + 1 + 4 * 5 + 4];
    memset(p, 0, sizeof p);
    put32(p, 1);                                        // version
    put16(p + 4, 1);                                    // action: start
    put16(p + 6, 0x1234);                               // sync id
    memset(p + 44, 0xFF, 6);                            // any BSSID
    p[50] = 2;                                          // any BSS type
    p[51] = 0;                                          // active scan
    put32(p + 52, 0xFFFFFFFF);                          // probes, active/passive/home time: defaults
    put32(p + 56, 0xFFFFFFFF);
    put32(p + 60, 0xFFFFFFFF);
    put32(p + 64, 0xFFFFFFFF);
    put32(p + 68, 0);                                   // all channels
    nnets = 0;
    scanning = 1;
    scan_started = millis();
    int r = iovar_set("escan", p, sizeof p);
    if (r) scanning = 0;
    return r;
}

// ---------------------------------------------------------------- saved networks (wifi.cfg)
#define CFG_FILE "wifi.cfg"
#define MAX_SAVED 8
typedef struct { char ssid[33]; char pass[64]; } saved_t;
static saved_t saved[MAX_SAVED];
static int nsaved;

static void cfg_load(void) {
    nsaved = 0;
    static char text[MAX_SAVED * 100];
    int32_t size = myfs_size(CFG_FILE);
    if (size <= 0) return;
    if (size > (int32_t)sizeof text - 1) size = sizeof text - 1;
    if (myfs_read(CFG_FILE, 0, text, (uint32_t)size) != size) return;
    text[size] = 0;
    char *s = text;
    while (*s && nsaved < MAX_SAVED) {
        char *line = s;
        while (*s && *s != '\n') s++;
        if (*s) *s++ = 0;
        char *tab = line;
        while (*tab && *tab != '\t') tab++;
        if (!*tab) continue;
        *tab = 0;
        char *pass = tab + 1;
        size_t pl = strlen(pass);
        if (pl && pass[pl - 1] == '\r') pass[--pl] = 0;
        if (strlen(line) == 0 || strlen(line) > 32 || pl > 63) continue;
        strcpy(saved[nsaved].ssid, line);
        strcpy(saved[nsaved].pass, pass);
        nsaved++;
    }
}

static int cfg_store(void) {
    static char text[MAX_SAVED * 100];
    uint32_t n = 0;
    for (int i = 0; i < nsaved; i++) {
        size_t a = strlen(saved[i].ssid), b = strlen(saved[i].pass);
        memcpy(text + n, saved[i].ssid, a);
        n += (uint32_t)a;
        text[n++] = '\t';
        memcpy(text + n, saved[i].pass, b);
        n += (uint32_t)b;
        text[n++] = '\n';
    }
    if (nsaved == 0) return myfs_remove(CFG_FILE) == 0 || myfs_size(CFG_FILE) < 0 ? 0 : -1;
    return myfs_write(CFG_FILE, text, n);
}

static int cfg_find(const char *ssid) {
    for (int i = 0; i < nsaved; i++)
        if (strcmp(saved[i].ssid, ssid) == 0) return i;
    return -1;
}

static void cfg_remember(const char *ssid, const char *pass) {   // newest first
    if (!myfs_mounted()) return;
    cfg_load();
    int i = cfg_find(ssid);
    if (i < 0) i = nsaved < MAX_SAVED ? nsaved++ : MAX_SAVED - 1;
    for (; i > 0; i--) saved[i] = saved[i - 1];
    strcpy(saved[0].ssid, ssid);
    strcpy(saved[0].pass, pass);
    cfg_store();
}

static int cfg_forget(const char *ssid) {
    cfg_load();
    int i = cfg_find(ssid);
    if (i < 0) return -1;
    for (; i < nsaved - 1; i++) saved[i] = saved[i + 1];
    nsaved--;
    return cfg_store();
}

// ---------------------------------------------------------------- joining
static const char *security_name(int s) {
    static const char *const names[] = { "open", "WEP", "WPA", "WPA2", "WPA3" };
    return s >= 0 && s <= 4 ? names[s] : "?";
}

static net_t *find_net(const char *ssid) {
    for (int i = 0; i < nnets; i++)
        if (strcmp(nets[i].ssid, ssid) == 0) return &nets[i];
    return 0;
}

static void link_down(void);

static int join_now(void) {
    join_assoc = join_link = join_keyed = join_failed = 0;
    int sec = join_security;
    if (sec == 1) {
        set_message("WEP networks are not supported");
        return -1;
    }
    if (sec == 4) {
        set_message("WPA3-only networks are not supported");
        return -1;
    }
    if (sec >= 2 && strlen(join_pass) < 8) {
        set_message("the password must have 8-63 characters");
        return -1;
    }
    if (state >= ST_JOINED) {
        ioctl(WLC_DISASSOC, 1, 0, 0, 0, 0);
        link_down();
    }
    uint32_t wsec = sec == 3 ? 6 : sec == 2 ? 2 : 0;     // AES+TKIP, TKIP, none
    uint32_t wpa_auth = sec == 3 ? 0x80 : sec == 2 ? 0x04 : 0;
    int r = 0;
    r |= ioctl_set32(WLC_SET_WSEC, wsec);
    r |= iovar_set_bsscfg32("sup_wpa", sec >= 2 ? 1 : 0);
    if (sec >= 2) {
        iovar_set_bsscfg32("sup_wpa2_eapver", 0xFFFFFFFF);
        iovar_set_bsscfg32("sup_wpa_tmo", 5000);
        uint8_t pmk[68];
        memset(pmk, 0, sizeof pmk);
        size_t pl = strlen(join_pass);
        put16(pmk, (uint16_t)pl);
        put16(pmk + 2, 1);                              // it's a passphrase, not a key
        memcpy(pmk + 4, join_pass, pl);
        delay_ms(2);
        r |= ioctl(WLC_SET_WSEC_PMK, 1, pmk, sizeof pmk, 0, 0);
    }
    r |= ioctl_set32(WLC_SET_INFRA, 1);
    r |= ioctl_set32(WLC_SET_AUTH, 0);
    r |= ioctl_set32(WLC_SET_WPA_AUTH, wpa_auth);
    if (r) {
        set_message("the firmware refused the settings");
        return -1;
    }
    uint8_t ssid[36];
    memset(ssid, 0, sizeof ssid);
    size_t sl = strlen(join_ssid);
    put32(ssid, (uint32_t)sl);
    memcpy(ssid + 4, join_ssid, sl);
    state = ST_JOINING;
    join_started = millis();
    if (ioctl(WLC_SET_SSID, 1, ssid, sizeof ssid, 0, 0)) {
        state = ST_ON;
        set_message("the firmware refused to join");
        return -1;
    }
    set_message("joining ...");
    return 0;
}

static void join_fail(const char *why) {
    join_failed = 1;
    if (state == ST_JOINING) state = ST_ON;
    set_message(why);
}

#ifndef NO_USB
static struct netif *usb_default;

static void link_up(void) {
    state = ST_JOINED;
    set_message("connected, asking for an IP address");
    net_lock();
    netif_set_link_up(&wl_netif);
    dhcp_start(&wl_netif);
    net_unlock();
}

static void link_down(void) {
    if (state >= ST_JOINED) {
        net_lock();
        dhcp_release_and_stop(&wl_netif);
        netif_set_link_down(&wl_netif);
        if (netif_default == &wl_netif) netif_set_default(usb_default);
        net_unlock();
    }
    if (state > ST_ON) state = ST_ON;
}
#else
static void link_up(void) { state = ST_JOINED; }
static void link_down(void) { if (state > ST_ON) state = ST_ON; }
#endif

static void handle_event(const uint8_t *eth, uint32_t len) {
    if (len < 24 + 48) return;
    const uint8_t *e = eth + 24;                        // after Ethernet + Broadcom headers
    uint32_t type = get32be(e + 4), status = get32be(e + 8), reason = get32be(e + 12);
    uint32_t dlen = get32be(e + 20);
    uint16_t flags = (uint16_t)(e[2] << 8 | e[3]);
    const uint8_t *data = e + 48;
    if (dlen > len - 72) dlen = len - 72;
    if (verbose && type != E_ESCAN_RESULT) {
        console_puts("[event ");
        print_dec(type);
        console_puts(" status ");
        print_dec(status);
        console_puts(" reason ");
        print_dec(reason);
        console_puts(" flags ");
        print_dec(flags);
        console_puts("]\n");
    }
    switch (type) {
        case E_ESCAN_RESULT:
            if (status == 8) {                          // one more result
                if (dlen >= 12 + 128 && get16(data + 10) >= 1) parse_bss(data + 12, dlen - 12);
            } else {                                    // finished (or aborted)
                scanning = 0;
                scan_done_at = millis();
                if (join_pending == 1) join_pending = 2;
            }
            break;
        case E_SET_SSID:
            if (state != ST_JOINING) break;
            if (status == 0) join_assoc = 1;
            else join_fail(status == 3 ? "network not found" : "couldn't join the network");
            break;
        case E_LINK:
            if (flags & 1) {
                join_link = 1;
            } else {
                join_link = 0;
                if (state >= ST_JOINED) {
                    link_down();
                    set_message("disconnected");
                } else if (state == ST_JOINING && join_assoc) {
                    join_fail("the network dropped us (wrong password?)");
                }
            }
            break;
        case E_PSK_SUP:
            if (state != ST_JOINING && state < ST_JOINED) break;
            if (status == 6) join_keyed = 1;            // keys agreed: WPA is up
            else if (state == ST_JOINING) join_fail("WPA handshake failed: wrong password?");
            break;
        case E_DEAUTH_IND:
        case E_DISASSOC_IND:
        case E_DEAUTH:
        case E_DISASSOC:
            if (state >= ST_JOINED) {
                link_down();
                set_message("disconnected by the network: reconnecting ...");
                auto_reconnect = 1;                     // work() joins again in a moment
                reconnect_tries = 0;
                reconnect_at = millis() + 3000;
            } else if (state == ST_JOINING && type != E_DISASSOC && type != E_DEAUTH) {
                join_fail("the network refused us (wrong password?)");
            }
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------- lwIP network interface
#ifndef NO_USB
static void deliver_packet(struct pbuf *p, int now) {
    if (!now && net_busy()) {                           // inside lwIP: later
        int next = (rxq_head + 1) % RXQ;
        if (next == rxq_tail) { pbuf_free(p); return; }
        rxq[rxq_head] = p;
        rxq_head = next;
        return;
    }
    net_lock();
    if (wl_netif.input(p, &wl_netif) != ERR_OK) pbuf_free(p);
    net_unlock();
}

static err_t wl_output(struct netif *netif, struct pbuf *p) {
    (void)netif;
    if (state < ST_JOINED || p->tot_len > 1514) return ERR_IF;
    if (wait_credit()) return ERR_MEM;
    uint8_t *f = data_tx;
    f[12] = 0x20;                                       // BDC header: version 2
    f[13] = 0;
    f[14] = 0;
    f[15] = 0;
    pbuf_copy_partial(p, f + 16, p->tot_len, 0);
    if (send_frame(f, 16u + p->tot_len, 2)) return ERR_IF;
    return ERR_OK;
}

static err_t wl_init(struct netif *netif) {
    netif->name[0] = 'w';
    netif->name[1] = 'l';
    netif->mtu = 1500;
    netif->hwaddr_len = 6;
    memcpy(netif->hwaddr, mac, 6);
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;
    netif->output = etharp_output;
    netif->linkoutput = wl_output;
#if LWIP_NETIF_HOSTNAME
    netif->hostname = "myos";
#endif
    return ERR_OK;
}

static void add_netif(void) {
    if (netif_added) return;
    net_lock();
    netif_add(&wl_netif, IP4_ADDR_ANY4, IP4_ADDR_ANY4, IP4_ADDR_ANY4, NULL, wl_init, ethernet_input);
    netif_set_up(&wl_netif);
    net_unlock();
    netif_added = 1;
}

static int have_address(void) {
    return netif_added && !ip4_addr_isany_val(*netif_ip4_addr(&wl_netif));
}
#else
static void add_netif(void) {}
static int have_address(void) { return 0; }
#endif

// things that can't be done inside pump() (they send commands themselves)
static void work(void) {
    if (join_pending == 2 && !scanning) {
        join_pending = 0;
        net_t *n = find_net(join_ssid);
        if (n) join_security = n->security;
        else join_security = join_pass[0] ? 3 : 0;      // not seen: maybe hidden, try anyway
        if (join_now()) join_failed = 1;
    }
    if (state == ST_JOINING && !join_failed) {
        if (join_assoc && join_link && (join_security < 2 || join_keyed)) {
            link_up();
            if (join_save) {
                join_save = 0;
                cfg_remember(join_ssid, join_pass);
            }
        } else if (millis() - join_started > 15000) {
            ioctl(WLC_DISASSOC, 1, 0, 0, 0, 0);
            join_fail(join_assoc ? "no answer to the password (wrong password?)" : "timed out joining");
        }
    }
    if (scanning && millis() - scan_started > 15000) scanning = 0;
    // dropped by the network (router restart, weak signal ...): try the same
    // network again every 10 s, after 5 tries every minute, until it works
    // or someone says 'wifi disconnect' / 'wifi connect' / 'wifi forget'
    if (auto_reconnect && state == ST_ON && !join_pending && !scanning && millis() >= reconnect_at) {
        reconnect_tries++;
        reconnect_at = millis() + (reconnect_tries < 5 ? 10000 : 60000);
        set_message("reconnecting ...");
        connect_start(join_ssid, join_pass, 0);
    }
#ifndef NO_USB
    if (state == ST_JOINED && have_address()) {
        auto_reconnect = 0;
        state = ST_ONLINE;
        set_message("online");
        net_lock();
        if (netif_default != &wl_netif) {
            usb_default = netif_default;
            netif_set_default(&wl_netif);               // the internet is out there
        }
        net_unlock();
    }
#endif
}

void wifi_poll(void) {
#ifndef NO_USB
    while (rxq_tail != rxq_head && !net_busy()) {       // packets that had to wait
        struct pbuf *p = rxq[rxq_tail];
        rxq_tail = (rxq_tail + 1) % RXQ;
        deliver_packet(p, 1);
    }
#endif
    if (state < ST_ON || in_pump) return;
    uint64_t now = millis();
    if (now == last_poll) return;                       // at most once a millisecond
    last_poll = now;
    pump();
    if (busy_pid < 0 || busy_pid == proc_current()->pid) work();
}

// ---------------------------------------------------------------- the shell's 'wifi'
static int take(void) {                                 // only one user of the chip at a time
    proc_t *me = proc_current();
    if (busy_pid >= 0 && busy_pid != me->pid && proc_by_pid(busy_pid)) return 0;
    busy_pid = me->pid;
    return 1;
}

static void give(void) { busy_pid = -1; }

static int start_or_say(void) {
    if (state >= ST_ON) return 0;
    if (!myfs_mounted()) {
        console_puts("No file system: the Wi-Fi firmware lives in MyFS.\n");
        return -1;
    }
    if (state == ST_NOCHIP) {
        console_puts("No Wi-Fi chip found (QEMU has none; the Pi 4 is not supported yet).\n");
        return -1;
    }
    console_puts("Starting Wi-Fi ...\n");
    if (chip_start() == 0) {
        add_netif();
        return 0;
    }
    if (state == ST_NOCHIP) console_puts("No Wi-Fi chip found (QEMU has none; the Pi 4 is not supported yet).\n");
    else {
        console_puts("Wi-Fi didn't start: ");
        console_puts(message);
        console_puts("\n");
        if (!fw_name || !nvram_name)
            console_puts("Copy the firmware into MyFS from the PC: python3 tools/pi.py push FILES\n"
                         "(tools/get-deps.ps1 downloads them to third_party/wifi-firmware)\n");
    }
    return -1;
}

// the firmware's own log (its "console" in its RAM): for debugging
static void cmd_log(void) {
    if (!sdio_ready()) return;
    uint32_t sh = rd32(RAM_SIZE - 4);
    if (sh >= RAM_SIZE) sh = rd32(RAM_SIZE - 0x10000 - 4);   // save/restore chips: 64 KB lower
    console_puts("shared pointer: ");
    print_hex(sh, 8);
    console_puts("\n");
    if (sh >= RAM_SIZE || (sh & 3)) {
        console_puts("(no shared area: the firmware didn't get that far). Text in its RAM:\n");
        // print runs of text after the firmware image (its variables, where the log lives)
        uint32_t from = (uint32_t)(fw_name ? (myfs_size(fw_name) + 4095) & ~4095 : 0);
        int run = 0;
        static char line[100];
        for (uint32_t a = from; a < RAM_SIZE; a += sizeof chunk) {
            if (bp_copy(0, a, chunk, sizeof chunk)) return;
            for (uint32_t i = 0; i < sizeof chunk; i++) {
                char c = (char)chunk[i];
                if (c >= ' ' && c <= '~' && run < 99) {
                    line[run++] = c;
                    continue;
                }
                if (run >= 12) {
                    line[run] = 0;
                    print_hex(a + i - (uint32_t)run, 5);
                    console_puts(": ");
                    console_puts(line);
                    console_puts("\n");
                }
                run = 0;
            }
            console_poll();
        }
        return;
    }
    uint8_t b[64] __attribute__((aligned(8)));
    if (bp_copy(0, sh, b, 40)) return;
    console_puts("flags ");
    print_hex(get32(b), 8);
    console_puts(" trap ");
    print_hex(get32(b + 4), 8);
    console_puts(" assert line ");
    print_dec(get32(b + 16));
    uint32_t con = get32(b + 20);
    console_puts(" console ");
    print_hex(con, 8);
    console_puts("\n");
    if (con >= RAM_SIZE) return;
    if (bp_copy(0, con, b, 24)) return;
    uint32_t buf = get32(b + 8), size = get32(b + 12), idx = get32(b + 16);
    if (buf >= RAM_SIZE || size > 8192 || size == 0) return;
    static uint8_t logb[8192] __attribute__((aligned(8)));
    if (bp_copy(0, buf, logb, (size + 3) & ~3u)) return;
    for (uint32_t i = 0; i < size; i++) {
        char c = (char)logb[(idx + i) % size];
        if (c == '\n' || (c >= ' ' && c <= '~')) console_putc(c);
    }
    console_puts("\n");
}

static void wait_ms_polling(uint32_t ms) {
    uint64_t end = millis() + ms;
    while (millis() < end) console_poll();
}

static void print_padded(const char *s, int width) {
    console_puts(s);
    for (int n = (int)strlen(s); n < width; n++) console_putc(' ');
}

static void cmd_scan(void) {
    if (start_or_say()) return;
    if (scan_start()) {
        console_puts("The scan didn't start.\n");
        return;
    }
    console_puts("Scanning ...\n");
    while (scanning) {
        console_poll();
        wifi_poll();
    }
    cfg_load();
    // strongest first
    for (int i = 1; i < nnets; i++)
        for (int j = i; j > 0 && nets[j].rssi > nets[j - 1].rssi; j--) {
            net_t t = nets[j];
            nets[j] = nets[j - 1];
            nets[j - 1] = t;
        }
    console_puts("  SSID                              SIGNAL  CHANNEL  SECURITY\n");
    for (int i = 0; i < nnets; i++) {
        console_puts(cfg_find(nets[i].ssid) >= 0 ? "* " : "  ");
        print_padded(nets[i].ssid, 34);
        print_signed(nets[i].rssi);
        console_puts(" dBm");
        int w = nets[i].rssi <= -100 ? 4 : nets[i].rssi <= -10 ? 3 : 2;
        for (int k = w + 4; k < 8; k++) console_putc(' ');
        char ch[4] = { 0 };
        int c = nets[i].channel;
        if (c >= 100) { ch[0] = (char)('0' + c / 100); ch[1] = (char)('0' + c / 10 % 10); ch[2] = (char)('0' + c % 10); }
        else if (c >= 10) { ch[0] = (char)('0' + c / 10); ch[1] = (char)('0' + c % 10); }
        else ch[0] = (char)('0' + c);
        console_puts("  ");
        print_padded(ch, 7);
        console_puts(security_name(nets[i].security));
        console_puts("\n");
    }
    print_dec((uint64_t)nnets);
    console_puts(" network(s). * = saved.  Join one with:  wifi connect NAME [PASSWORD]\n");
}

// "name with spaces" or name -> out; returns the rest
static const char *take_word(const char *s, char *out, int max) {
    int n = 0;
    while (*s == ' ') s++;
    if (*s == '"') {
        s++;
        while (*s && *s != '"') { if (n < max) out[n++] = *s; s++; }
        if (*s == '"') s++;
    } else {
        while (*s && *s != ' ') { if (n < max) out[n++] = *s; s++; }
    }
    out[n] = 0;
    while (*s == ' ') s++;
    return s;
}

static int connect_start(const char *ssid, const char *pass, int save) {
    copy_text(join_ssid, ssid, 32);
    copy_text(join_pass, pass, 63);
    join_save = save;
    join_failed = 0;
    if (!pass[0]) {                                     // no password given: a saved one?
        cfg_load();
        int i = cfg_find(ssid);
        if (i >= 0) strcpy(join_pass, saved[i].pass);
    }
    net_t *n = find_net(join_ssid);
    if (n && millis() - scan_done_at < 60000) {         // seen in a recent scan
        join_security = n->security;
        join_pending = 0;
        return join_now();
    }
    join_pending = 1;
    set_message("looking for the network ...");
    return scan_start();
}

static void print_ip_line(void);

static void cmd_connect(const char *arg) {
    char ssid[64], pass[100];
    arg = take_word(arg, ssid, sizeof ssid - 1);
    take_word(arg, pass, sizeof pass - 1);
    if (!ssid[0] || strlen(ssid) > 32 || strlen(pass) > 63) {
        console_puts("Usage: wifi connect NAME [PASSWORD]   (\"quotes\" around a name with spaces)\n");
        return;
    }
    if (start_or_say()) return;
    auto_reconnect = 0;                                 // the person decides now
    console_puts("Connecting to ");
    console_puts(ssid);
    console_puts(" ...\n");
    if (connect_start(ssid, pass, 1)) {
        console_puts("Failed: ");
        console_puts(message);
        console_puts("\n");
        return;
    }
    uint64_t end = millis() + 40000;
    while (millis() < end && !join_failed && state != ST_ONLINE) {
        if (console_getc_nowait() == 3) break;          // Ctrl+C (and keeps USB + Wi-Fi going)
        wifi_poll();
    }
    if (state == ST_ONLINE) {
        console_puts("Connected. ");
        print_ip_line();
    } else if (state == ST_JOINED) {
        console_puts("Connected, but no IP address from the network yet (DHCP).\n");
    } else {
        console_puts("Failed: ");
        console_puts(message);
        console_puts("\n");
        join_pending = 0;
    }
}

static void print_ip_line(void) {
#ifndef NO_USB
    console_puts("IP address ");
    console_puts(ip4addr_ntoa(netif_ip4_addr(&wl_netif)));
    console_puts(", gateway ");
    console_puts(ip4addr_ntoa(netif_ip4_gw(&wl_netif)));
    console_puts("\n");
#endif
}

static const char *state_name(void) {
    switch (state) {
        case ST_NOCHIP: return "no Wi-Fi chip";
        case ST_OFF: return "off";
        case ST_STARTING: return "starting";
        case ST_ON: return "on, not connected";
        case ST_JOINING: return "connecting";
        case ST_JOINED: return "connected, waiting for an IP address";
        default: return "online";
    }
}

static int32_t current_rssi(void) {
    uint8_t b[4] = {0};
    int me = proc_current()->pid;
    if (state < ST_JOINED || (busy_pid >= 0 && busy_pid != me && proc_by_pid(busy_pid))) return 0;
    int was = busy_pid;
    busy_pid = me;
    int r = ioctl(WLC_GET_RSSI, 0, 0, 0, b, 4);
    busy_pid = was;
    return r ? 0 : (int32_t)get32(b);
}

static void cmd_status(void) {
    console_puts("Wi-Fi:    ");
    console_puts(state_name());
    console_puts("\n");
    if (chip_id) {
        console_puts("Chip:     CYW");
        console_puts(chip_rev >= 2 ? "43436" : "43436s/43430");
        console_puts(" (ID 0x");
        print_hex(chip_id, 4);
        console_puts(" rev ");
        print_dec(chip_rev);
        console_puts(")\n");
    }
    if (fw_version[0]) {
        console_puts("Firmware: ");
        console_puts(fw_version);
        console_puts("\n");
    }
    if (state >= ST_ON) {
        char m[18];
        mac_text(mac, m);
        console_puts("MAC:      ");
        console_puts(m);
        console_puts("\n");
    }
    if (state >= ST_JOINING) {
        console_puts("Network:  ");
        console_puts(join_ssid);
        int32_t rssi = current_rssi();
        if (rssi) {
            console_puts("  (signal ");
            print_signed(rssi);
            console_puts(" dBm)");
        }
        console_puts("\n");
    }
    if (state == ST_ONLINE) {
        console_puts("Address:  ");
        print_ip_line();
    }
    if (message[0]) {
        console_puts("Last:     ");
        console_puts(message);
        console_puts("\n");
    }
    if (myfs_mounted()) {
        cfg_load();
        console_puts("Saved:    ");
        if (!nsaved) console_puts("(none)");
        for (int i = 0; i < nsaved; i++) {
            if (i) console_puts(", ");
            console_puts(saved[i].ssid);
        }
        console_puts("\n");
    }
    if (verbose) {
        console_puts("Frames:   ");
        print_dec(frames_in);
        console_puts(" in, ");
        print_dec(frames_out);
        console_puts(" out, ");
        print_dec(rx_errors);
        console_puts(" errors\n");
    }
}

static void cmd_disconnect(void) {
    join_pending = 0;
    auto_reconnect = 0;
    if (state < ST_JOINING) {
        console_puts("Not connected.\n");
        return;
    }
    ioctl(WLC_DISASSOC, 1, 0, 0, 0, 0);
    link_down();
    state = ST_ON;
    set_message("disconnected");
    console_puts("Disconnected.\n");
}

// a test: leave the network as if the router had dropped us (auto-reconnect follows)
static void cmd_drop(void) {
    if (state < ST_JOINED) {
        console_puts("Not connected.\n");
        return;
    }
    ioctl(WLC_DISASSOC, 1, 0, 0, 0, 0);
    link_down();
    state = ST_ON;
    set_message("dropped (test): reconnecting ...");
    auto_reconnect = 1;
    reconnect_tries = 0;
    reconnect_at = millis() + 3000;
    console_puts("Dropped. MyOS should join again by itself in a few seconds ('wifi' shows it).\n");
}

static void cmd_forget(const char *arg) {
    char ssid[64];
    take_word(arg, ssid, sizeof ssid - 1);
    if (!myfs_mounted()) {
        console_puts("No file system.\n");
        return;
    }
    if (!ssid[0]) {
        console_puts("Usage: wifi forget NAME   (see 'wifi status' for the saved ones)\n");
        return;
    }
    if (strcmp(ssid, join_ssid) == 0) auto_reconnect = 0;
    if (cfg_forget(ssid)) {
        console_puts("Not a saved network: ");
        console_puts(ssid);
        console_puts("\n");
        return;
    }
    console_puts("Forgot ");
    console_puts(ssid);
    console_puts(".\n");
}

void wifi_command(const char *arg) {
    if (strncmp(arg, "-v", 2) == 0) {                   // wifi -v ...: tell every step
        verbose = 1;
        arg += 2;
        if (*arg == 'v') { verbose = 2; arg++; }        // -vv: every frame too
        while (*arg == ' ') arg++;
    }
    if (!take()) {
        console_puts("Wi-Fi is busy (another terminal is using it). Try again in a moment.\n");
        verbose = 0;
        return;
    }
    if (*arg == 0 || strcmp(arg, "status") == 0) cmd_status();
    else if (strcmp(arg, "scan") == 0) cmd_scan();
    else if (strncmp(arg, "connect", 7) == 0 && (arg[7] == ' ' || arg[7] == 0)) cmd_connect(arg + 7);
    else if (strcmp(arg, "disconnect") == 0) cmd_disconnect();
    else if (strcmp(arg, "drop") == 0) cmd_drop();
    else if (strcmp(arg, "log") == 0) cmd_log();
    else if (strncmp(arg, "forget", 6) == 0 && (arg[6] == ' ' || arg[6] == 0)) cmd_forget(arg + 6);
    else if (strcmp(arg, "on") == 0) {
        if (start_or_say() == 0) cmd_status();
    } else {
        console_puts("Usage: wifi [status]            what's going on\n"
                     "       wifi scan                networks nearby\n"
                     "       wifi connect NAME [PASSWORD]   join (and remember) a network\n"
                     "       wifi disconnect          leave the network\n"
                     "       wifi forget NAME         drop a saved network\n"
                     "       wifi on                  just start the chip\n"
                     "       wifi log                 the Wi-Fi firmware's own log\n"
                     "       wifi -v ...              show every step (-vv: every frame)\n");
    }
    give();
    verbose = 0;
}

// ---------------------------------------------------------------- at boot
static void boot_thread(void) {
    busy_pid = proc_current()->pid;
    if (chip_start() == 0) {
        add_netif();
        cfg_load();
        if (nsaved && scan_start() == 0) {
            while (scanning) {
                console_poll();
                pump();
            }
            int best = -1;
            int16_t best_rssi = -1000;
            for (int i = 0; i < nsaved; i++) {
                net_t *n = find_net(saved[i].ssid);
                if (n && n->rssi > best_rssi) {
                    best = i;
                    best_rssi = n->rssi;
                }
            }
            if (best >= 0 && connect_start(saved[best].ssid, saved[best].pass, 0) == 0) {
                busy_pid = -1;
                uint64_t end = millis() + 30000;
                while (millis() < end && !join_failed && state != ST_ONLINE) console_poll();
                if (state == ST_ONLINE) {                       // tell the serial terminal
                    console_puts(console_at_line_start() ? "" : "\n");
                    console_puts("Wi-Fi: connected to ");
                    console_puts(join_ssid);
                    console_puts(", ");
                    print_ip_line();
                }
            } else if (best < 0) {
                set_message("no saved network nearby");
            }
        }
    }
    busy_pid = -1;
    proc_exit(0);
}

void wifi_boot(void) {
#if !defined(NO_USB) && !defined(PI4)
    if (!myfs_mounted() || myfs_size(CFG_FILE) <= 0) return;
    proc_spawn_kernel("wifi", boot_thread, 0);
#endif
}

// ---------------------------------------------------------------- for programs (SYS_WIFI)
static int user_ok(uint64_t addr, uint64_t len) {         // like user.c's user_range
    if (addr >= 0x400000 && len <= 0x200000 && addr + len <= 0x600000) return 1;
    if (addr >= 0x02000000 && len <= 0x02000000 && addr + len <= 0x04000000) return proc_user_ensure(addr, len);
    return 0;
}

int64_t wifi_syscall(uint64_t op, uint64_t a, uint64_t b) {
    switch (op) {
        case MYOS_WIFI_STATUS: {
            if (!user_ok(a, sizeof(struct myos_wifi_status))) return -1;
            struct myos_wifi_status *s = (struct myos_wifi_status *)a;
            memset(s, 0, sizeof *s);
            s->state = state;
            s->scanning = scanning;
            if (state >= ST_JOINING) strcpy(s->ssid, join_ssid);
            if (state >= ST_ON) mac_text(mac, s->mac);
            strcpy(s->message, message);
#ifndef NO_USB
            if (state == ST_ONLINE) {
                strcpy(s->ip, ip4addr_ntoa(netif_ip4_addr(&wl_netif)));
                strcpy(s->gateway, ip4addr_ntoa(netif_ip4_gw(&wl_netif)));
            }
#endif
            if (state >= ST_JOINED) s->rssi = current_rssi();
            return 0;
        }
        case MYOS_WIFI_SCAN:
            if (!take()) return -1;
            if (state < ST_ON && (state == ST_NOCHIP || !myfs_mounted() || chip_start())) { give(); return -1; }
            add_netif();
            {
                int r = scanning ? 0 : scan_start();
                give();
                return r ? -1 : 0;
            }
        case MYOS_WIFI_RESULT: {
            if (!user_ok(b, sizeof(struct myos_wifi_net)) || a >= (uint64_t)nnets) return -1;
            struct myos_wifi_net *n = (struct myos_wifi_net *)b;
            memset(n, 0, sizeof *n);
            n->rssi = nets[a].rssi;
            n->channel = nets[a].channel;
            n->security = nets[a].security;
            strcpy(n->ssid, nets[a].ssid);
            memcpy(n->bssid, nets[a].bssid, 6);
            if (a == 0) cfg_load();
            n->saved = cfg_find(nets[a].ssid) >= 0;
            return 0;
        }
        case MYOS_WIFI_CONNECT: {
            if (!user_ok(a, sizeof(struct myos_wifi_connect))) return -1;
            struct myos_wifi_connect c;
            memcpy(&c, (void *)a, sizeof c);
            c.ssid[32] = 0;
            c.password[63] = 0;
            if (!c.ssid[0] || !take()) return -1;
            auto_reconnect = 0;
            if (state < ST_ON && (state == ST_NOCHIP || !myfs_mounted() || chip_start())) { give(); return -1; }
            add_netif();
            int r = connect_start(c.ssid, c.password, 1);
            give();
            return r ? -1 : 0;
        }
        case MYOS_WIFI_DISCONNECT:
            if (!take()) return -1;
            join_pending = 0;
            auto_reconnect = 0;
            if (state >= ST_JOINING) {
                ioctl(WLC_DISASSOC, 1, 0, 0, 0, 0);
                link_down();
                state = ST_ON;
                set_message("disconnected");
            }
            give();
            return 0;
        case MYOS_WIFI_FORGET: {
            char ssid[33];
            if (!user_ok(a, 1) || !myfs_mounted()) return -1;
            int i = 0;
            for (; i < 32 && user_ok(a + (uint64_t)i, 1) && ((const char *)a)[i]; i++) ssid[i] = ((const char *)a)[i];
            ssid[i] = 0;
            if (strcmp(ssid, join_ssid) == 0) auto_reconnect = 0;
            return cfg_forget(ssid) ? -1 : 0;
        }
        case MYOS_WIFI_SAVED:
            if (!user_ok(b, 33) || !myfs_mounted()) return -1;
            if (a == 0) cfg_load();
            if (a >= (uint64_t)nsaved) return -1;
            strcpy((char *)b, saved[a].ssid);
            return 0;
        default:
            return -1;
    }
}

// ---------------------------------------------------------------- ping
#ifndef NO_USB
static volatile int ping_got;
static uint16_t ping_seq;
static uint64_t ping_time;
static uint8_t ping_ttl;

static u8_t ping_recv(void *arg, struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *addr) {
    (void)arg;
    (void)pcb;
    (void)addr;
    uint8_t h[28];
    if (p->tot_len < 28 || pbuf_copy_partial(p, h, 28, 0) != 28) return 0;
    uint32_t ihl = (h[0] & 0x0F) * 4u;
    if (ihl != 20) return 0;
    if (h[20] != ICMP_ER || h[24] != 0xAF || h[25] != 0xAF) return 0;  // echo reply with our id
    if ((uint16_t)(h[26] << 8 | h[27]) != ping_seq) return 0;
    ping_ttl = h[8];
    ping_time = millis();
    ping_got = 1;
    pbuf_free(p);
    return 1;
}
#endif

void ping_command(const char *arg) {
#ifdef NO_USB
    (void)arg;
    console_puts("No network in this build.\n");
#else
    ip4_addr_t dst;
    char host[40];
    take_word(arg, host, sizeof host - 1);
    if (!host[0] || !ip4addr_aton(host, &dst)) {
        console_puts("Usage: ping ADDRESS   (e.g. ping 192.168.7.2, or the router after 'wifi connect')\n");
        return;
    }
    net_lock();
    struct raw_pcb *pcb = raw_new(IP_PROTO_ICMP);
    if (pcb) raw_recv(pcb, ping_recv, 0);
    net_unlock();
    if (!pcb) {
        console_puts("Out of memory.\n");
        return;
    }
    console_puts("PING ");
    console_puts(host);
    console_puts("\n");
    int got = 0, sent = 0;
    for (int i = 0; i < 4; i++) {
        struct pbuf *p = pbuf_alloc(PBUF_IP, 8 + 32, PBUF_RAM);
        if (!p) break;
        uint8_t *icmp = p->payload;
        ping_seq = (uint16_t)(i + 1);
        icmp[0] = ICMP_ECHO;
        icmp[1] = 0;
        icmp[2] = icmp[3] = 0;
        icmp[4] = icmp[5] = 0xAF;
        icmp[6] = (uint8_t)(ping_seq >> 8);
        icmp[7] = (uint8_t)ping_seq;
        for (int k = 0; k < 32; k++) icmp[8 + k] = (uint8_t)('a' + k % 23);
        uint16_t sum = inet_chksum(icmp, 8 + 32);
        icmp[2] = (uint8_t)sum;
        icmp[3] = (uint8_t)(sum >> 8);
        ping_got = 0;
        uint64_t t0 = millis();
        net_lock();
        raw_sendto(pcb, p, (ip_addr_t *)&dst);
        net_unlock();
        pbuf_free(p);
        sent++;
        while (!ping_got && millis() - t0 < 1000) {
            console_poll();
            if (console_getc_nowait() == 3) i = 99;     // Ctrl+C
        }
        if (ping_got) {
            got++;
            console_puts("Reply from ");
            console_puts(host);
            console_puts(": time=");
            print_dec(ping_time - t0);
            console_puts(" ms ttl=");
            print_dec(ping_ttl);
            console_puts("\n");
            wait_ms_polling((uint32_t)(1000 - (millis() - t0 < 1000 ? millis() - t0 : 1000)));
        } else if (i < 99) {
            console_puts("No reply.\n");
        }
    }
    net_lock();
    raw_remove(pcb);
    net_unlock();
    print_dec((uint64_t)sent);
    console_puts(" sent, ");
    print_dec((uint64_t)got);
    console_puts(" received\n");
#endif
}
