/* myos.h - write MyOS programs in C.
 *
 * Programs run in ring 3 and talk to the kernel only through
 * the 'syscall' instruction (same services as apps/myos.inc).
 */
#ifndef MYOS_H
#define MYOS_H

#include <stddef.h>
#include <stdint.h>

enum {
    SYS_EXIT, SYS_PRINT, SYS_PUTC, SYS_GETKEY,
    SYS_READLINE, SYS_PRINTNUM, SYS_TICKS, SYS_SLEEP,
    SYS_FILESIZE, SYS_READFILE, SYS_WRITEFILE, SYS_DELETE, SYS_LISTFILES,
    SYS_KEYNOW,     /* MyOS Pi only: a key if one is waiting, else -1 */
    SYS_PROCINFO,   /* MyOS Pi only: (index, struct myos_procinfo *) -> 0, or -1 past the end */
    SYS_SYSINFO,    /* MyOS Pi only: (struct myos_sysinfo *) */
    SYS_KILL        /* MyOS Pi only: (pid) stop a program -> 0, or -1 */
};
#include "myos_info.h"

#if defined(__aarch64__)
/* MyOS Pi: 'svc #0', service number in x8, arguments x0-x2, result in x0 */
static inline long myos_syscall3(long n, long a, long b, long c) {
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    __asm__ volatile ("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory");
    return x0;
}
#else
/* MyOS PC: 'syscall', service number in rax, arguments rdi/rsi/rdx */
static inline long myos_syscall3(long n, long a, long b, long c) {
    long ret;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(n), "D"(a), "S"(b), "d"(c)
                      : "rcx", "r11", "memory");
    return ret;
}
#endif

static inline long myos_syscall(long n, long a, long b) {
    return myos_syscall3(n, a, b, 0);
}

static inline __attribute__((noreturn)) void sys_exit(int code) {
    myos_syscall(SYS_EXIT, code, 0);
    __builtin_unreachable();
}
static inline void sys_print(const char *s)        { myos_syscall(SYS_PRINT, (long)s, 0); }
static inline void sys_putc(int c)                 { myos_syscall(SYS_PUTC, c, 0); }
static inline int  sys_getkey(void)                { return (int)myos_syscall(SYS_GETKEY, 0, 0); }
static inline long sys_readline(char *buf, long n) { return myos_syscall(SYS_READLINE, (long)buf, n); }
static inline void sys_printnum(unsigned long n)   { myos_syscall(SYS_PRINTNUM, (long)n, 0); }
static inline unsigned long sys_ticks(void)        { return (unsigned long)myos_syscall(SYS_TICKS, 0, 0); }
static inline void sys_sleep(unsigned long ticks)  { myos_syscall(SYS_SLEEP, (long)ticks, 0); }
static inline int  sys_keynow(void)                { return (int)myos_syscall(SYS_KEYNOW, 0, 0); }
static inline long sys_procinfo(long i, struct myos_procinfo *p) { return myos_syscall(SYS_PROCINFO, i, (long)p); }
static inline long sys_sysinfo(struct myos_sysinfo *s) { return myos_syscall(SYS_SYSINFO, (long)s, 0); }
static inline long sys_kill(long pid)              { return myos_syscall(SYS_KILL, pid, 0); }

/* files on the MyFS disk; every call returns -1 on error */
static inline long sys_file_size(const char *name) {
    return myos_syscall(SYS_FILESIZE, (long)name, 0);
}
static inline long sys_read_file(const char *name, void *buf, unsigned long max) {
    return myos_syscall3(SYS_READFILE, (long)name, (long)buf, (long)max);
}
static inline long sys_write_file(const char *name, const void *data, unsigned long size) {
    return myos_syscall3(SYS_WRITEFILE, (long)name, (long)data, (long)size);
}
static inline long sys_delete_file(const char *name) {
    return myos_syscall(SYS_DELETE, (long)name, 0);
}
/* name of the index-th file (buffer of 48 bytes); returns its size, -1 = no more */
static inline long sys_list_files(unsigned long index, char *name48) {
    return myos_syscall(SYS_LISTFILES, (long)index, (long)name48);
}

/* tiny C library (csdk/libc.c) */
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);

#endif
