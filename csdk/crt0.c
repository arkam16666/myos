/* crt0.c - where every C program starts.
 * The kernel jumps to _start in user mode with the command-line text as
 * the first argument (RDI on x86, x0 on ARM) and a ready stack.
 */
#include "myos.h"

int main(const char *args);

__attribute__((used)) void myos_start(const char *args) {
    sys_exit(main(args));
}

#if defined(__aarch64__)
__attribute__((section(".text.start"), naked, noreturn)) void _start(void) {
    __asm__ volatile (
        "bl myos_start\n"       /* x0 (args) is passed straight through */
        "brk #0\n");
}
#else
__attribute__((section(".text.start"), naked, noreturn)) void _start(void) {
    __asm__ volatile (
        "and $-16, %rsp\n"      /* C code expects a 16-byte aligned stack */
        "call myos_start\n"     /* RDI (args) is passed straight through */
        "ud2\n");
}
#endif
