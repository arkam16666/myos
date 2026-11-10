// kernel.c - MyOS for Raspberry Pi (ARM64)
// Boots (boot8.s drops from EL2 to EL1), turns on the MMU and caches,
// starts USB and the SD card, and runs a shell over serial and USB.
#include <stdint.h>
#include <string.h>
#include "broadcom/caches.h"
#include "broadcom/mmu.h"
#include "console.h"
#include "fat.h"
#include "led.h"
#include "myfs.h"
#include "sd.h"
#include "timer.h"
#include "uart.h"
#ifndef NO_USB
#include "usb.h"
#endif

// ---------------------------------------------------------------
//  CPU information (ARM "system registers", read with mrs)
// ---------------------------------------------------------------
static unsigned current_el(void) {
    uint64_t v;
    __asm__ volatile ("mrs %0, CurrentEL" : "=r"(v));
    return (unsigned)((v >> 2) & 3);
}

static uint64_t cpu_id(void) {
    uint64_t v;
    __asm__ volatile ("mrs %0, midr_el1" : "=r"(v));
    return v;
}

// a crash (bad memory access, unknown instruction...) lands here
void HardFault_IRQHandler(void) {
    led_panic();
}

// ---------------------------------------------------------------
//  small helpers
// ---------------------------------------------------------------
static void print_dec(uint64_t n) {
    char buf[21];
    int i = 20;
    buf[i] = 0;
    do {
        buf[--i] = (char)('0' + n % 10);
        n /= 10;
    } while (n);
    console_puts(&buf[i]);
}

static void print_hex(uint64_t n, int digits) {
    for (int shift = (digits - 1) * 4; shift >= 0; shift -= 4) {
        console_putc("0123456789ABCDEF"[(n >> shift) & 0xF]);
    }
}

static void print_signed(int n) {
    if (n < 0) {
        console_putc('-');
        n = -n;
    }
    print_dec((uint64_t)n);
}

// does line start with word (followed by a space or the end)? -> rest of line
static const char *command(const char *line, const char *word) {
    while (*word) {
        if (*line++ != *word++) {
            return 0;
        }
    }
    if (*line == ' ') {
        while (*line == ' ') line++;
        return line;
    }
    return *line == 0 ? line : 0;
}

// copy the first word of s into out[48]; returns the rest of s, or 0 if no word
static const char *first_word(const char *s, char *out) {
    int n = 0;
    while (*s && *s != ' ') {
        if (n < MYFS_NAME_MAX) out[n++] = *s;
        s++;
    }
    out[n] = 0;
    while (*s == ' ') s++;
    return n ? s : 0;
}

static const char banner[] =
    "  __  __        ___  ____  \n"
    " |  \\/  |_   _ / _ \\/ ___| \n"
    " | |\\/| | | | | | | \\___ \\ \n"
    " | |  | | |_| | |_| |___) |\n"
    " |_|  |_|\\__, |\\___/|____/ \n"
    "         |___/   for Raspberry Pi (ARM64) v0.4\n\n"
    "Type 'help' to see commands.\n\n";

static const char prompt[] = "myos> ";

static void read_line(char *buf, int size) {
    int len = 0;
    for (;;) {
        int c = console_getc_nowait();
#ifndef NO_USB
        if (usb_take_connect_event()) {     // a terminal just opened the USB port
            console_puts("\n");
            console_puts(banner);
            console_puts(prompt);
            buf[len] = 0;
            console_puts(buf);
        }
#endif
        if (c < 0) {
            continue;
        }
        if (c == '\r' || c == '\n') {
            console_putc('\n');
            buf[len] = 0;
            return;
        }
        if ((c == 8 || c == 127) && len > 0) {      // backspace
            len--;
            console_puts("\b \b");
        } else if (c >= ' ' && c <= '~' && len < size - 1) {
            buf[len++] = (char)c;
            console_putc((char)c);
            console_puts("");                       // send it now
        }
    }
}

// ---------------------------------------------------------------
//  storage: SD card -> FAT partition -> myfs.img -> MyFS
// ---------------------------------------------------------------
static int sd_status = 1;           // 1 = not tried, 0 = ok, <0 = init step that failed
static int fat_status = 1;
static myfs_dev_t myfs_dev;

static void storage_init(void) {
    sd_status = sd_init();
    if (sd_status) return;
    fat_file_t f;
    fat_status = fat_find_contiguous("MYFS    IMG", &f);
    if (fat_status) return;
    myfs_dev.start = f.start_lba;
    myfs_dev.sectors = f.sectors;
    myfs_dev.read = sd_read;
    myfs_dev.write = sd_write;
    myfs_mount(&myfs_dev);
}

static void print_storage_status(void) {
    console_puts("Disk: ");
    if (sd_status) {
        console_puts("no SD card (error ");
        print_signed(sd_status);
        console_puts(")\n\n");
    } else if (fat_status == FAT_ERR_NOTFOUND) {
        console_puts("no myfs.img on the SD card's boot partition\n\n");
    } else if (fat_status == FAT_ERR_FRAGMENTED) {
        console_puts("myfs.img is in pieces; copy it to the card again\n\n");
    } else if (fat_status) {
        console_puts("can't read the boot partition\n\n");
    } else if (!myfs_mounted()) {
        console_puts("myfs.img has no file system. Type 'format' to create one.\n\n");
    } else {
        console_puts("MyFS mounted from myfs.img, ");
        print_dec((uint64_t)myfs_count());
        console_puts(" file(s). Type 'ls' to list them.\n\n");
    }
}

static int need_fs(void) {
    if (myfs_mounted()) return 1;
    console_puts("No file system. Type 'sd' for details.\n");
    return 0;
}

// ---------------------------------------------------------------
//  shell commands
// ---------------------------------------------------------------
static void cmd_help(void) {
    console_puts("Commands:\n"
                 "  help      show this list\n"
                 "  echo X    print X\n"
                 "  info      CPU and privilege level\n"
                 "  uptime    time since boot\n"
                 "  clear     clear the screen\n"
                 "  sd        SD card and file system details\n"
                 "  hexdump N show raw bytes of SD card sector N\n"
                 "  ls        list files\n"
                 "  cat F     show file F\n"
                 "  write F X write text X into file F\n"
                 "  rm F      delete file F\n"
                 "  format    erase myfs.img, create an empty file system\n"
                 "  update    receive a new MyOS from the PC (arm\\update-pi.ps1)\n"
                 "  reboot    restart the Pi\n");
}

static void cmd_info(void) {
    uint64_t id = cpu_id();
    uint32_t part = (uint32_t)(id >> 4) & 0xFFF;
    console_puts("CPU:        ");
    if (part == 0xD03) console_puts("ARM Cortex-A53");
    else if (part == 0xD08) console_puts("ARM Cortex-A72");
    else console_puts("ARM (unknown model)");
    console_puts("  (MIDR 0x");
    print_hex(id, 16);
    console_puts(")\nMode:       64-bit (AArch64), exception level EL");
    print_dec(current_el());
    console_puts("\nTimer:      ");
    print_dec(timer_freq() / 1000);
    console_puts(" kHz\nBuilt:      " __DATE__ " " __TIME__);
    console_puts("\nConsole:    serial (GPIO 14/15)");
#ifndef NO_USB
    console_puts(" + USB serial");
#endif
    console_puts("\n");
}

static void cmd_uptime(void) {
    uint64_t seconds = timer_count() / timer_freq();
    console_puts("Uptime: ");
    print_dec(seconds / 3600);
    console_puts("h ");
    print_dec(seconds / 60 % 60);
    console_puts("m ");
    print_dec(seconds % 60);
    console_puts("s\n");
}

static void cmd_sd(void) {
    if (sd_status) {
        console_puts("SD card: not ready (init failed at step ");
        print_signed(-sd_status);
        console_puts(")\n");
        return;
    }
    console_puts("SD card:  ");
    console_puts(sd_is_high_capacity() ? "SDHC/SDXC, " : "SDSC, ");
    print_dec(sd_sectors() / 2048);
    console_puts(" MB, controller clock ");
    print_dec(sd_base_clock() / 1000000);
    console_puts(" MHz\nmyfs.img: ");
    if (fat_status) {
        console_puts("not usable (");
        print_signed(fat_status);
        console_puts(")\n");
        return;
    }
    console_puts("sectors ");
    print_dec(myfs_dev.start);
    console_puts("-");
    print_dec(myfs_dev.start + myfs_dev.sectors - 1);
    console_puts(" (");
    print_dec(myfs_dev.sectors / 2048);
    console_puts(" MB)\nMyFS:     ");
    if (myfs_mounted()) {
        print_dec((uint64_t)myfs_count());
        console_puts(" file(s), ");
        print_dec(myfs_free_kb());
        console_puts(" KB free\n");
    } else {
        console_puts("not formatted\n");
    }
}

static uint8_t sector_buf[512] __attribute__((aligned(4)));

static void cmd_hexdump(const char *arg) {
    uint32_t n = 0;
    while (*arg >= '0' && *arg <= '9') n = n * 10 + (uint32_t)(*arg++ - '0');
    if (sd_read(n, sector_buf)) {
        console_puts("Can't read that sector.\n");
        return;
    }
    console_puts("Sector ");
    print_dec(n);
    console_puts(" (first 256 of 512 bytes):\n");
    for (int row = 0; row < 256; row += 16) {
        print_hex((uint64_t)row, 4);
        console_puts(": ");
        for (int i = 0; i < 16; i++) {
            print_hex(sector_buf[row + i], 2);
            console_putc(' ');
        }
        console_putc('|');
        for (int i = 0; i < 16; i++) {
            uint8_t c = sector_buf[row + i];
            console_putc(c >= ' ' && c <= '~' ? (char)c : '.');
        }
        console_puts("|\n");
    }
}

static void cmd_ls(void) {
    if (!need_fs()) return;
    char name[MYFS_NAME_MAX + 1];
    uint32_t size;
    int i;
    for (i = 0; myfs_entry(i, name, &size) == 0; i++) {
        console_puts("  ");
        console_puts(name);
        for (int pad = (int)strlen(name); pad < 34; pad++) console_putc(' ');
        print_dec(size);
        console_puts(" bytes\n");
    }
    if (i == 0) console_puts("  (no files)\n");
    print_dec((uint64_t)i);
    console_puts(" file(s), ");
    print_dec(myfs_free_kb());
    console_puts(" KB free\n");
}

static void cmd_cat(const char *arg) {
    char name[MYFS_NAME_MAX + 1];
    if (!need_fs()) return;
    if (!first_word(arg, name)) {
        console_puts("Usage: cat <file>\n");
        return;
    }
    int32_t size = myfs_size(name);
    if (size < 0) {
        console_puts("No such file: ");
        console_puts(name);
        console_putc('\n');
        return;
    }
    char chunk[128];
    char last = '\n';
    for (uint32_t off = 0; off < (uint32_t)size; off += sizeof chunk) {
        int n = myfs_read(name, off, chunk, sizeof chunk);
        if (n <= 0) break;
        for (int i = 0; i < n; i++) {
            char c = chunk[i];
            if (c == '\r') continue;                // Windows line endings
            if (c == '\t') c = ' ';
            if (c != '\n' && (c < ' ' || c > '~')) c = '.';
            console_putc(c);
            last = c;
        }
    }
    if (last != '\n') console_putc('\n');
}

static void cmd_write(const char *arg) {
    char name[MYFS_NAME_MAX + 1];
    if (!need_fs()) return;
    const char *text = first_word(arg, name);
    if (!text) {
        console_puts("Usage: write <file> <text>\n");
        return;
    }
    char data[96];
    size_t n = strlen(text);
    if (n > sizeof data - 1) n = sizeof data - 1;
    memcpy(data, text, n);
    data[n++] = '\n';
    int r = myfs_write(name, data, (uint32_t)n);
    if (r == MYFS_ERR_FULL) {
        console_puts("Disk or directory is full.\n");
    } else if (r) {
        console_puts("Disk error!\n");
    } else {
        console_puts("Wrote ");
        print_dec(n);
        console_puts(" bytes to ");
        console_puts(name);
        console_putc('\n');
    }
}

static void cmd_rm(const char *arg) {
    char name[MYFS_NAME_MAX + 1];
    if (!need_fs()) return;
    if (!first_word(arg, name)) {
        console_puts("Usage: rm <file>\n");
        return;
    }
    if (myfs_remove(name) == 0) {
        console_puts("Removed ");
        console_puts(name);
        console_putc('\n');
    } else {
        console_puts("No such file: ");
        console_puts(name);
        console_putc('\n');
    }
}

static void cmd_format(const char *arg) {
    if (sd_status || fat_status) {
        console_puts("No usable myfs.img. Type 'sd' for details.\n");
        return;
    }
    if (strcmp(arg, "yes") != 0) {
        console_puts("This ERASES everything in myfs.img. Type 'format yes' to continue.\n");
        return;
    }
    if (myfs_format(&myfs_dev)) {
        console_puts("Disk error!\n");
        return;
    }
    console_puts("myfs.img formatted with MyFS. ");
    print_dec(myfs_free_kb());
    console_puts(" KB free\n");
}

// ---------------------------------------------------------------
//  reboot and update over USB / serial
// ---------------------------------------------------------------
#ifdef PI4
#define PM_BASE 0xFE100000UL
#else
#define PM_BASE 0x3F100000UL
#endif
#define PM_RSTC (*(volatile uint32_t *)(PM_BASE + 0x1C))
#define PM_WDOG (*(volatile uint32_t *)(PM_BASE + 0x24))
#define PM_PASSWORD 0x5A000000u

static void cmd_reboot(void) {
    console_puts("Rebooting...\n");
    uint64_t end = millis() + 200;          // give USB time to send that
    while (millis() < end) console_poll();
    PM_WDOG = PM_PASSWORD | 10;             // watchdog fires in ~150 microseconds
    PM_RSTC = PM_PASSWORD | (PM_RSTC & ~0x30u) | 0x20;   // ...and does a full reset
    for (;;) {
    }
}

#define UPDATE_BUF ((uint8_t *)0x01000000)  // 16 MB: free RAM to receive into

static uint32_t crc32(const uint8_t *p, uint32_t n) {
    uint32_t crc = 0xFFFFFFFF;
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1));
    }
    return ~crc;
}

static int read_byte(uint64_t timeout_ms) {
    uint64_t end = millis() + timeout_ms;
    while (millis() < end) {
        int c = console_getc_nowait();
        if (c >= 0) return c;
    }
    return -1;
}

static uint8_t verify_buf[512] __attribute__((aligned(4)));

// receive a new kernel and write it over myos8.img on the SD card.
// Protocol (tools: arm\update-pi.ps1):
//   MyOS: "UPDATE READY <capacity>"   PC: "MYUP", u32 size, u32 crc32, data
//   MyOS: "UPDATE OK ..." or "UPDATE FAILED <reason>"
static void cmd_update(void) {
    fat_file_t k;
    const char *why = 0;
    if (sd_status || fat_find_contiguous("MYOS8   IMG", &k)) {
        console_puts("UPDATE FAILED no usable myos8.img on the SD card\n");
        return;
    }
    uint32_t capacity = k.sectors * 512;
    console_puts("UPDATE READY ");
    print_dec(capacity);
    console_puts("\n");

    uint8_t hdr[12];
    for (int i = 0; i < 12; i++) {
        int c = read_byte(15000);
        if (c < 0) { why = "timeout"; goto fail; }
        hdr[i] = (uint8_t)c;
    }
    if (memcmp(hdr, "MYUP", 4) != 0) { why = "bad header"; goto fail; }
    uint32_t size = hdr[4] | hdr[5] << 8 | hdr[6] << 16 | (uint32_t)hdr[7] << 24;
    uint32_t crc = hdr[8] | hdr[9] << 8 | hdr[10] << 16 | (uint32_t)hdr[11] << 24;
    if (size == 0 || size > capacity) { why = "image too big for myos8.img"; goto fail; }
    for (uint32_t i = 0; i < size; i++) {
        int c = read_byte(5000);
        if (c < 0) { why = "timeout while receiving"; goto fail; }
        UPDATE_BUF[i] = (uint8_t)c;
    }
    if (crc32(UPDATE_BUF, size) != crc) { why = "checksum mismatch"; goto fail; }

    // write the new kernel (rest of the file: zeros), checking every sector
    for (uint32_t s = 0; s < k.sectors; s++) {
        uint32_t off = s * 512;
        memset(sector_buf, 0, 512);
        if (off < size) memcpy(sector_buf, UPDATE_BUF + off, size - off < 512 ? size - off : 512);
        if (sd_write(k.start_lba + s, sector_buf)) { why = "SD write error"; goto fail; }
        if (sd_read(k.start_lba + s, verify_buf) || memcmp(sector_buf, verify_buf, 512) != 0) {
            why = "SD verify error";
            goto fail;
        }
        if ((s & 63) == 0) console_poll();  // keep USB alive
    }
    console_puts("UPDATE OK ");
    print_dec(size);
    console_puts(" bytes written. Type 'reboot' to start it.\n");
    return;
fail:
    console_puts("UPDATE FAILED ");
    console_puts(why);
    console_puts("\n");
}

int main(void) {
    uart_init();
    led_init();
    setup_mmu_flat_map();           // normal memory + device memory, caches on
    init_caches();
#ifndef NO_USB
    usb_init();
    console_enable_usb();
#endif
    storage_init();
    console_puts("\n");
    console_puts(banner);
    print_storage_status();

    char line[80];
    for (;;) {
        console_puts(prompt);
        read_line(line, sizeof line);
        const char *arg;
        if (line[0] == 0) {
            continue;
        } else if (command(line, "help")) {
            cmd_help();
        } else if ((arg = command(line, "echo"))) {
            console_puts(arg);
            console_putc('\n');
        } else if (command(line, "info")) {
            cmd_info();
        } else if (command(line, "uptime")) {
            cmd_uptime();
        } else if (command(line, "clear")) {
            console_puts("\x1b[2J\x1b[H");  // terminal codes: clear + home
        } else if (command(line, "sd")) {
            cmd_sd();
        } else if ((arg = command(line, "hexdump"))) {
            cmd_hexdump(arg);
        } else if (command(line, "ls")) {
            cmd_ls();
        } else if ((arg = command(line, "cat"))) {
            cmd_cat(arg);
        } else if ((arg = command(line, "write"))) {
            cmd_write(arg);
        } else if ((arg = command(line, "rm"))) {
            cmd_rm(arg);
        } else if ((arg = command(line, "format"))) {
            cmd_format(arg);
        } else if (command(line, "update")) {
            cmd_update();
        } else if (command(line, "reboot")) {
            cmd_reboot();
        } else {
            console_puts("Unknown command. Type 'help'.\n");
        }
    }
}
