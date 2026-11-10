// console.c - text in/out for the shell, over both the GPIO serial port
// and (on real hardware) the USB serial port.
#include "console.h"
#include "led.h"
#include "timer.h"
#include "uart.h"

#ifdef NO_USB
#define usb_ready() 0
static void usb_service(void) {}
static void usb_write(char c) { (void)c; }
static void usb_flush(void) {}
static int usb_read(void) { return -1; }
#else
#include "tusb.h"

static int usb_on;

static int usb_ready(void) {
    return usb_on && tud_cdc_connected();
}

static void usb_service(void) {
    if (usb_on) {
        tud_task();
    }
}

static void usb_write(char c) {
    if (!usb_ready()) {
        return;
    }
    while (tud_cdc_write_available() == 0) {    // wait for room, keep USB alive
        tud_task();
        if (!tud_cdc_connected()) {
            return;
        }
    }
    tud_cdc_write_char(c);
}

static void usb_flush(void) {
    if (usb_ready()) {
        tud_cdc_write_flush();
    }
}

static int usb_read(void) {
    if (usb_on && tud_cdc_available()) {
        return tud_cdc_read_char();
    }
    return -1;
}

void console_enable_usb(void) {
    usb_on = 1;
}
#endif

void console_putc(char c) {
    uart_putc(c);                   // uart_putc turns '\n' into "\r\n" itself
    if (c == '\n') {
        usb_write('\r');
    }
    usb_write(c);
    if (c == '\n') {
        usb_flush();
    }
}

void console_puts(const char *s) {
    while (*s) {
        console_putc(*s++);
    }
    usb_flush();
}

void console_poll(void) {
    usb_service();
    led_heartbeat(millis());
}

int console_getc_nowait(void) {
    console_poll();
    if (uart_has_char()) {
        return (unsigned char)uart_getc();
    }
    return usb_read();
}
