// console.c - text in/out for the shell, over both the GPIO serial port
// and (on real hardware) the USB serial port.
#include "console.h"
#include "led.h"
#include "proc.h"
#include "timer.h"
#include "uart.h"

static int at_line_start = 1;
static int last_source;

int console_last_source(void) {
    return last_source;
}

#ifdef NO_USB
static void usb_service(void) {}
static void usb_write(char c) { (void)c; }
static void usb_flush(void) {}
static int usb_read(void) { return -1; }
static void remote_putc(char c) { (void)c; }
static void remote_flush(void) {}
static int remote_getc(void) { return -1; }
static void ssh_putc(char c) { (void)c; }
static void ssh_flush(void) {}
static void ssh_poll(void) {}
static int ssh_getc(void) { return -1; }
static int other_terminal(void) { return 0; }
static const int polled = 0;
void console_enable_usb(void) {}
void console_panic_mode(void) {}
#else
#include "net.h"
#include "remote.h"
#include "ssh.h"
#include "tusb.h"

static int usb_on;
static int polled;      // after a crash interrupts are off: drive USB by polling

static int usb_ready(void) {
    return usb_on && tud_cdc_connected();
}

static void usb_service(void) {
    if (!usb_on) return;
    if (polled) tusb_int_handler(0, true);
    tud_task();
    if (!polled) net_poll();        // network timers (not after a crash)
}

static void usb_write(char c) {
    if (!usb_ready()) return;
    while (tud_cdc_write_available() == 0) {    // wait for room, keep USB alive
        usb_service();
        if (!tud_cdc_connected()) return;
    }
    tud_cdc_write_char(c);
}

static void usb_flush(void) {
    if (usb_ready()) tud_cdc_write_flush();
}

static int usb_read(void) {
    if (usb_on && tud_cdc_available()) return tud_cdc_read_char();
    return -1;
}

void console_enable_usb(void) {
    usb_on = 1;
}

// is someone using a faster terminal than the GPIO serial port?
static int other_terminal(void) {
    return !polled && (usb_ready() || remote_active() || ssh_active());
}

void console_panic_mode(void) {
    polled = 1;
}
#endif

void console_putc(char c) {
    // uart_putc turns '\n' into "\r\n" itself. The GPIO serial port (115200
    // baud, ~11 KB/s) would slow everything down while someone uses USB or
    // the network, so then it only gets what fits in its FIFO.
    if (other_terminal()) uart_putc_nowait(c);
    else uart_putc(c);
    if (c == '\n') usb_write('\r');
    usb_write(c);
    if (!polled) {                  // the network shells add their own CR
        remote_putc(c);
        ssh_putc(c);
    }
    if (c == '\n') usb_flush();
    at_line_start = (c == '\n');
}

void console_puts(const char *s) {
    while (*s) console_putc(*s++);
    usb_flush();
    if (!polled) {
        remote_flush();
        ssh_flush();
    }
}

int console_at_line_start(void) {
    return at_line_start;
}

void console_service(void) {
    usb_service();
}

// Keys from every terminal go into one queue as soon as they arrive, so
// Ctrl+C is seen even while the foreground program is busy computing.
#define KEYQ 256
static uint8_t keyq[KEYQ], keyq_src[KEYQ];
static unsigned keyq_head, keyq_tail;

static void key_arrived(int c, int src) {
    if (c == 3 && proc_foreground() != 0) {         // Ctrl+C: stop the foreground program
        proc_kill(proc_foreground());
        return;
    }
    unsigned next = (keyq_head + 1) % KEYQ;
    if (next == keyq_tail) return;                  // full: drop
    keyq[keyq_head] = (uint8_t)c;
    keyq_src[keyq_head] = (uint8_t)src;
    keyq_head = next;
}

static void pump_keys(void) {
    int c;
    while (((keyq_head + 1) % KEYQ) != keyq_tail) {
        if (uart_has_char()) c = (unsigned char)uart_getc(), key_arrived(c, CONSOLE_LOCAL);
        else if ((c = usb_read()) >= 0) key_arrived(c, CONSOLE_LOCAL);
        else if (polled) break;
        else if ((c = remote_getc()) >= 0) key_arrived(c, CONSOLE_TELNET);
        else if ((c = ssh_getc()) >= 0) key_arrived(c, CONSOLE_SSH);
        else break;
    }
}

void console_poll(void) {
    usb_service();
    if (!polled) {
        ssh_poll();
        remote_flush();             // e.g. the echo of a typed key
        ssh_flush();
    }
    pump_keys();
    led_heartbeat(millis());
    if (!polled) proc_yield();      // let other processes run while we wait
}

int console_getc_nowait(void) {
    console_poll();
    if (keyq_tail == keyq_head) return -1;
    int c = keyq[keyq_tail];
    last_source = keyq_src[keyq_tail];
    keyq_tail = (keyq_tail + 1) % KEYQ;
    return c;
}
