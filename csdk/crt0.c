/* crt0.c - where every C program starts.
 * The kernel jumps to _start in user mode with the command-line text as
 * the first argument (RDI on x86, x0 on ARM) and a ready stack.
 */
#include "myos.h"

#ifdef MYOS_LIBC
/* programs using the full C library (csdk/libc) get main(argc, argv) */
#ifndef MYOS_PROGNAME
#define MYOS_PROGNAME program           /* -DMYOS_PROGNAME=lua (no quotes) */
#endif
#define MYOS_STR2(x) #x
#define MYOS_STR(x) MYOS_STR2(x)
int myos_libc_main(const char *args, const char *progname);

__attribute__((used)) void myos_start(const char *args) {
    myos_libc_main(args, MYOS_STR(MYOS_PROGNAME));
}
#else
int main(const char *args);

__attribute__((used)) void myos_start(const char *args) {
    sys_exit(main(args));
}
#endif

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
