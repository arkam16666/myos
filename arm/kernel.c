// kernel.c - MyOS for Raspberry Pi (ARM64)
// Boots (boot8.s drops from EL2 to EL1), turns on the MMU and caches,
// starts USB, and runs a small shell over serial and USB.
#include <stdint.h>
#include "console.h"
#include "led.h"
#include "timer.h"
#include "uart.h"
#ifndef NO_USB
#include "broadcom/caches.h"
#include "broadcom/mmu.h"
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

static void print_hex(uint64_t n) {
    console_puts("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        console_putc("0123456789ABCDEF"[(n >> shift) & 0xF]);
    }
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

static const char banner[] =
    "  __  __        ___  ____  \n"
    " |  \\/  |_   _ / _ \\/ ___| \n"
    " | |\\/| | | | | | | \\___ \\ \n"
    " | |  | | |_| | |_| |___) |\n"
    " |_|  |_|\\__, |\\___/|____/ \n"
    "         |___/   for Raspberry Pi (ARM64) v0.2\n\n"
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
//  shell commands
// ---------------------------------------------------------------
static void cmd_help(void) {
    console_puts("Commands:\n"
                 "  help      show this list\n"
                 "  echo X    print X\n"
                 "  info      CPU and privilege level\n"
                 "  uptime    time since boot\n"
                 "  clear     clear the screen\n");
}

static void cmd_info(void) {
    uint64_t id = cpu_id();
    uint32_t part = (uint32_t)(id >> 4) & 0xFFF;
    console_puts("CPU:        ");
    if (part == 0xD03) console_puts("ARM Cortex-A53");
    else if (part == 0xD08) console_puts("ARM Cortex-A72");
    else console_puts("ARM (unknown model)");
    console_puts("  (MIDR ");
    print_hex(id);
    console_puts(")\nMode:       64-bit (AArch64), exception level EL");
    print_dec(current_el());
    console_puts("\nTimer:      ");
    print_dec(timer_freq() / 1000);
    console_puts(" kHz\nConsole:    serial (GPIO 14/15)");
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

int main(void) {
    uart_init();
    led_init();
#ifndef NO_USB
    setup_mmu_flat_map();           // normal memory + device memory, caches on
    init_caches();
    usb_init();
    console_enable_usb();
#endif
    console_puts("\n");
    console_puts(banner);

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
        } else {
            console_puts("Unknown command. Type 'help'.\n");
        }
    }
}
