// console.h - shell text over serial (GPIO) and USB
#ifndef CONSOLE_H
#define CONSOLE_H

void console_putc(char c);
void console_puts(const char *s);   // also sends anything waiting
int console_at_line_start(void);
void console_poll(void);            // keep USB and the LED going; call often
void console_service(void);         // keep USB going (no LED)
int console_getc_nowait(void);      // a key, or -1 if none is waiting
void console_enable_usb(void);
void console_panic_mode(void);      // interrupts are off: poll the USB controller

#endif
