// uart.c - driver for the PL011 serial port (UART0) of the Raspberry Pi.
//
// Hardware is controlled by reading and writing "registers" at fixed
// memory addresses (memory-mapped I/O), the ARM way of doing what
// in/out ports did on x86.
#include <stdint.h>
#include "uart.h"

#ifdef PI4
#define PERIPHERAL_BASE 0xFE000000UL    // Raspberry Pi 4
#else
#define PERIPHERAL_BASE 0x3F000000UL    // Raspberry Pi 2/3, Zero 2 W
#endif

#define GPIO_BASE   (PERIPHERAL_BASE + 0x200000)
#define GPFSEL1     ((volatile uint32_t *)(GPIO_BASE + 0x04))

#define UART0_BASE  (PERIPHERAL_BASE + 0x201000)
#define UART0_DR    ((volatile uint32_t *)(UART0_BASE + 0x00))   // data
#define UART0_FR    ((volatile uint32_t *)(UART0_BASE + 0x18))   // flags
#define UART0_IBRD  ((volatile uint32_t *)(UART0_BASE + 0x24))   // baud rate
#define UART0_FBRD  ((volatile uint32_t *)(UART0_BASE + 0x28))
#define UART0_LCRH  ((volatile uint32_t *)(UART0_BASE + 0x2C))   // line control
#define UART0_CR    ((volatile uint32_t *)(UART0_BASE + 0x30))   // control
#define UART0_ICR   ((volatile uint32_t *)(UART0_BASE + 0x44))   // clear interrupts

#define FR_RXFE     (1 << 4)            // receive FIFO empty
#define FR_TXFF     (1 << 5)            // transmit FIFO full

void uart_init(void) {
    *UART0_CR = 0;                      // turn the UART off while setting up

    // GPIO 14 and 15 -> "alternate function 0" = UART0 TX/RX
    uint32_t sel = *GPFSEL1;
    sel &= ~((7u << 12) | (7u << 15));
    sel |= (4u << 12) | (4u << 15);
    *GPFSEL1 = sel;

    *UART0_ICR = 0x7FF;                 // clear pending interrupts
    *UART0_IBRD = 26;                   // 115200 baud with a 48 MHz clock
    *UART0_FBRD = 3;
    *UART0_LCRH = (3 << 5) | (1 << 4);  // 8 data bits, FIFOs on
    *UART0_CR = (1 << 0) | (1 << 8) | (1 << 9);   // UART, TX, RX on
}

void uart_putc(char c) {
    if (c == '\n') {
        uart_putc('\r');
    }
    while (*UART0_FR & FR_TXFF) {
    }
    *UART0_DR = (uint32_t)c;
}

// like uart_putc, but if the transmit FIFO is full the character is dropped
// (used while a faster terminal - USB or network - is in use)
void uart_putc_nowait(char c) {
    if (c == '\n') {
        uart_putc_nowait('\r');
    }
    if (!(*UART0_FR & FR_TXFF)) {
        *UART0_DR = (uint32_t)c;
    }
}

void uart_puts(const char *s) {
    while (*s) {
        uart_putc(*s++);
    }
}

int uart_has_char(void) {
    return !(*UART0_FR & FR_RXFE);
}

char uart_getc(void) {
    while (*UART0_FR & FR_RXFE) {
    }
    return (char)(*UART0_DR & 0xFF);
}
