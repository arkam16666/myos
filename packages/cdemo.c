// @version 1.0
// @description A program written in C
#include "myos.h"

static unsigned long fib(unsigned n) {
    return n < 2 ? n : fib(n - 1) + fib(n - 2);
}

int main(const char *args) {
    sys_print("Hello from C on MyOS!\n");

    sys_print("fib(25) = ");
    sys_printnum(fib(25));
    sys_putc('\n');

    volatile double r = 2.5;            /* floating point needs SSE */
    double area = 3.14159 * r * r;
    sys_print("circle area x 100 = ");
    sys_printnum((unsigned long)(area * 100));
    sys_putc('\n');

    if (*args) {
        sys_print("args: ");
        sys_print(args);
        sys_putc('\n');
    }
    return 0;
}
