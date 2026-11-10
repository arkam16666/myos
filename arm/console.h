// console.h - shell text over serial (GPIO) and USB
#ifndef CONSOLE_H
#define CONSOLE_H

void console_putc(char c);
void console_puts(const char *s);
void console_poll(void);            // keep USB and the LED going; call often
int console_getc_nowait(void);      // a key, or -1 if none is waiting
void console_enable_usb(void);

#endif
