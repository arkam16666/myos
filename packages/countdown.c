// @version 1.1
// @description Counts down and launches a rocket
#include "myos.h"

int main(const char *args) {
    unsigned long n = 0;                // "countdown 5" starts at 5 (default 10)
    while (*args >= '0' && *args <= '9') n = n * 10 + (unsigned long)(*args++ - '0');
    if (n == 0) n = 10;
    if (n > 99) n = 99;

    for (; n > 0; n--) {
        sys_putc(' ');
        sys_printnum(n);
        sys_print(" ...\n");
        sys_sleep(100);                 // one second
    }
    sys_print("  Liftoff!\n"
              "      /\\\n"
              "     |  |\n"
              "     |  |\n"
              "    /|/\\|\\\n"
              "     ^^^^\n");
    return 0;
}
