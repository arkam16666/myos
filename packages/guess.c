// @version 1.1
// @description Guess the number between 1 and 100
#include "myos.h"

static long parse_number(const char *s) {
    long n = 0;
    if (*s == 0) return -1;
    for (; *s; s++) {
        if (*s < '0' || *s > '9') return -1;
        n = n * 10 + (*s - '0');
    }
    return n;
}

int main(const char *args) {
    (void)args;
    char line[16];
    unsigned long secret = sys_ticks() % 100 + 1;
    unsigned long tries = 0;

    sys_print("I'm thinking of a number between 1 and 100.\n");
    for (;;) {
        sys_print("Your guess: ");
        sys_readline(line, sizeof line);
        long guess = parse_number(line);
        if (guess < 1 || guess > 100) {
            sys_print("  Please type a number from 1 to 100.\n");
            continue;
        }
        tries++;
        if ((unsigned long)guess < secret) {
            sys_print("  Too low!\n");
        } else if ((unsigned long)guess > secret) {
            sys_print("  Too high!\n");
        } else {
            sys_print("  Correct! You got it in ");
            sys_printnum(tries);
            sys_print(" tries.\n");
            return 0;
        }
    }
}
