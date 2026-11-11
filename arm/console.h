// console.h - terminals: shell text over serial, USB, telnet and SSH
#ifndef CONSOLE_H
#define CONSOLE_H

// terminals (tty): 0 = serial ports (GPIO + USB serial), 1 = telnet, 2.. = SSH sessions
#define TTY_TELNET 1
#define TTY_SSH    2
#define NTTY       5                // TTY_SSH + 3 SSH sessions (ssh.c MAX_SSH)

void console_init(void);
void console_putc(char c);          // to the current process's terminal
void console_puts(const char *s);   // also sends anything waiting
int console_at_line_start(void);
void console_poll(void);            // keep USB, network and the LED going, let others run
void console_service(void);         // keep USB going (no LED, no switching)
int console_getc_nowait(void);      // a key from this terminal, or -1 if none is waiting
int console_tty(void);              // the current process's terminal
int console_tty_open(int t);
void console_enable_usb(void);
void console_panic_mode(void);      // interrupts are off: poll the USB controller

#endif
