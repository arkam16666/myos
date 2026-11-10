// print.c - numbers on the console
#include "console.h"
#include "print.h"

void print_dec(uint64_t n) {
    char buf[21];
    int i = 20;
    buf[i] = 0;
    do {
        buf[--i] = (char)('0' + n % 10);
        n /= 10;
    } while (n);
    console_puts(&buf[i]);
}

void print_signed(int64_t n) {
    if (n < 0) {
        console_putc('-');
        print_dec((uint64_t)-n);
    } else {
        print_dec((uint64_t)n);
    }
}

void print_hex(uint64_t n, int digits) {
    for (int shift = (digits - 1) * 4; shift >= 0; shift -= 4) {
        console_putc("0123456789ABCDEF"[(n >> shift) & 0xF]);
    }
}
