// console.c - terminals: where text goes out and keys come in.
//
// Every way of reaching MyOS is its own terminal (tty), with its own shell,
// keyboard queue and screen:
//   tty 0     the serial ports: GPIO serial + USB serial (COM port)
//   tty 1     telnet (remote.c)
//   tty 2..   SSH sessions (ssh.c), one each
// A process prints on, and reads keys from, the terminal it belongs to
// (proc.c: proc->tty; programs inherit it from the shell that started them).
#include "console.h"
#include "led.h"
#include "proc.h"
#include "timer.h"
#include "uart.h"

#define KEYQ 256
typedef struct {
    uint8_t q[KEYQ];
    unsigned head, tail;
    int open;
    int at_line_start;
} tty_t;

static tty_t ttys[NTTY];

void terminal_opened(int t);            // kernel.c: start a shell on it
static int current_tty(void) {
    proc_t *p = proc_current();
    return p ? p->tty : 0;
}

#ifdef NO_USB
static void usb_service(void) {}
static void usb_write(char c) { (void)c; }
static void usb_flush(void) {}
static int usb_read(void) { return -1; }
static void net_putc(int t, char c) { (void)t; (void)c; }
static void net_flush(int t) { (void)t; }
static int net_getc(int t) { (void)t; return -1; }
static int net_open(int t) { (void)t; return 0; }
static void net_poll_all(void) {}
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

// the network terminals: 1 = telnet, 2.. = SSH sessions
static void net_putc(int t, char c) {
    if (t == TTY_TELNET) remote_putc(c);
    else ssh_putc(t - TTY_SSH, c);
}

static void net_flush(int t) {
    if (t == TTY_TELNET) remote_flush();
    else ssh_flush(t - TTY_SSH);
}

static int net_getc(int t) {
    return t == TTY_TELNET ? remote_getc() : ssh_getc(t - TTY_SSH);
}

static int net_open(int t) {
    return t == TTY_TELNET ? remote_active() : ssh_shell_open(t - TTY_SSH);
}

static void net_poll_all(void) {
    ssh_poll();
    for (int t = TTY_TELNET; t < NTTY; t++)
        if (ttys[t].open) net_flush(t);         // e.g. the echo of a typed key
}

// is someone using a faster terminal than the GPIO serial port?
static int other_terminal(void) {
    if (polled) return 0;
    if (usb_ready()) return 1;
    for (int t = TTY_TELNET; t < NTTY; t++)
        if (ttys[t].open) return 1;
    return 0;
}

void console_panic_mode(void) {
    polled = 1;
}
#endif

static void local_putc(char c) {
    // uart_putc turns '\n' into "\r\n" itself. The GPIO serial port (115200
    // baud, ~11 KB/s) would slow everything down while someone uses USB or
    // the network, so then it only gets what fits in its FIFO.
    if (other_terminal()) uart_putc_nowait(c);
    else uart_putc(c);
    if (c == '\n') usb_write('\r');
    usb_write(c);
    if (c == '\n') usb_flush();
}

void console_putc(char c) {
    int t = polled ? 0 : current_tty();
    if (t == 0) local_putc(c);
    else net_putc(t, c);                        // (the network side adds its own '\r')
    ttys[t].at_line_start = (c == '\n');
}

void console_puts(const char *s) {
    int t = polled ? 0 : current_tty();
    while (*s) console_putc(*s++);
    if (t == 0) usb_flush();
    else net_flush(t);
}

int console_at_line_start(void) {
    return ttys[current_tty()].at_line_start;
}

int console_tty(void) {
    return current_tty();
}

int console_tty_open(int t) {
    return t == 0 || (t > 0 && t < NTTY && ttys[t].open);
}

void console_service(void) {
    usb_service();
}

// ------------------------------------------------------------ keys
static void key_arrived(int t, int c) {
    if (c == 3 && proc_interrupt(t)) return;    // Ctrl+C stopped the foreground program
    tty_t *y = &ttys[t];
    unsigned next = (y->head + 1) % KEYQ;
    if (next == y->tail) return;                // full: drop
    y->q[y->head] = (uint8_t)c;
    y->head = next;
}

static int room(int t) {
    return ((ttys[t].head + 1) % KEYQ) != ttys[t].tail;
}

static void pump_keys(void) {
    int c;
    while (room(0)) {                           // serial ports -> tty 0
        if (uart_has_char()) c = (unsigned char)uart_getc();
        else if ((c = usb_read()) < 0) break;
        key_arrived(0, c);
    }
    if (polled) return;
    for (int t = TTY_TELNET; t < NTTY; t++)
        while (ttys[t].open && room(t) && (c = net_getc(t)) >= 0) key_arrived(t, c);
}

// a network terminal appeared or went away
static void check_terminals(void) {
    if (polled) return;
    for (int t = TTY_TELNET; t < NTTY; t++) {
        int now = net_open(t);
        if (now && !ttys[t].open) {
            ttys[t].open = 1;
            ttys[t].head = ttys[t].tail = 0;
            ttys[t].at_line_start = 1;
            proc_tty_open(t);
            terminal_opened(t);
        } else if (!now && ttys[t].open) {
            ttys[t].open = 0;
            proc_tty_close(t);                  // its shell and programs stop
        }
    }
}

void console_poll(void) {
    usb_service();
    if (!polled) net_poll_all();
    pump_keys();
    check_terminals();
    led_heartbeat(millis());
    if (!polled) proc_yield();      // let other processes run while we wait
}

int console_getc_nowait(void) {
    console_poll();
    tty_t *y = &ttys[current_tty()];
    if (y->tail == y->head) return -1;
    int c = y->q[y->tail];
    y->tail = (y->tail + 1) % KEYQ;
    return c;
}

void console_init(void) {
    for (int t = 0; t < NTTY; t++) ttys[t].at_line_start = 1;
    ttys[0].open = 1;
}
