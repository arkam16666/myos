// console.c - text in/out for the shell, over both the GPIO serial port
// and (on real hardware) the USB serial port.
#include "console.h"
#include "led.h"
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

void console_poll(void) {
    usb_service();
    if (!polled) {
        ssh_poll();
        remote_flush();             // e.g. the echo of a typed key
        ssh_flush();
    }
    led_heartbeat(millis());
}

int console_getc_nowait(void) {
    console_poll();
    last_source = CONSOLE_LOCAL;
    if (uart_has_char()) return (unsigned char)uart_getc();
    int c = usb_read();
    if (c >= 0 || polled) return c;
    if ((c = remote_getc()) >= 0) {
        last_source = CONSOLE_TELNET;
        return c;
    }
    if ((c = ssh_getc()) >= 0) last_source = CONSOLE_SSH;
    return c;
}
