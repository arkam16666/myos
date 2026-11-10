// @version 1.1
// @description Calculator: 12 + 34, 7 * 6, 100 / 3
#include "myos.h"

static const char *skip(const char *s) {
    while (*s == ' ') s++;
    return s;
}

// optional '-' then digits; returns 0 if there is no number
static int number(const char **s, long *out) {
    const char *p = *s;
    int negative = 0;
    if (*p == '-') {
        negative = 1;
        p++;
    }
    if (*p < '0' || *p > '9') return 0;
    long n = 0;
    while (*p >= '0' && *p <= '9') n = n * 10 + (*p++ - '0');
    *out = negative ? -n : n;
    *s = p;
    return 1;
}

static void print_signed(long n) {
    if (n < 0) {
        sys_putc('-');
        n = -n;
    }
    sys_printnum((unsigned long)n);
}

int main(const char *args) {
    (void)args;
    char line[64];
    sys_print("calc - type a sum like 12 + 34  (+ - * /). Empty line quits.\n");
    for (;;) {
        sys_print("calc> ");
        sys_readline(line, sizeof line);
        const char *p = skip(line);
        if (*p == 0 || *p == 'q') return 0;

        long a, b, result;
        char op;
        if (!number(&p, &a)) goto error;
        p = skip(p);
        op = *p++;
        p = skip(p);
        if (!number(&p, &b)) goto error;
        if (*skip(p) != 0) goto error;

        switch (op) {
            case '+': result = a + b; break;
            case '-': result = a - b; break;
            case '*': case 'x': result = a * b; break;
            case '/':
                if (b == 0) {
                    sys_print("  Can't divide by zero!\n");
                    continue;
                }
                result = a / b;
                break;
            default: goto error;
        }
        sys_print("  = ");
        print_signed(result);
        sys_putc('\n');
        continue;
    error:
        sys_print("  I don't understand that. Example: 7 * 6\n");
    }
}
