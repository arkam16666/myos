// uart.h - serial port (PL011 "UART0") on the Raspberry Pi
#ifndef UART_H
#define UART_H

void uart_init(void);
void uart_putc(char c);         // '\n' is sent as "\r\n"
void uart_putc_nowait(char c);  // drops the character if the FIFO is full
void uart_puts(const char *s);
char uart_getc(void);           // waits for a key
int uart_has_char(void);        // 1 if a key is waiting

#endif
