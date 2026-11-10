/* crt0.c - where every C program starts.
 * The kernel jumps to _start in ring 3 with RDI = command-line text.
 */
#include "myos.h"

int main(const char *args);

__attribute__((used)) void myos_start(const char *args) {
    sys_exit(main(args));
}

__attribute__((section(".text.start"), naked, noreturn)) void _start(void) {
    __asm__ volatile (
        "and $-16, %rsp\n"      /* C code expects a 16-byte aligned stack */
        "call myos_start\n"     /* RDI (args) is passed straight through */
        "ud2\n");
}
