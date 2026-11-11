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
    SYS_KILL,       /* MyOS Pi only: (pid) stop a program -> 0, or -1 */
    SYS_WIFI = 48   /* MyOS Pi only: (op, a, b) Wi-Fi, see MYOS_WIFI_* in myos_info.h */
};
/* MyOS Pi only: folders on MyFS v2. They start at 32 (17-31 are free for other
 * new calls). Paths may be relative to the program's current folder, which it
 * gets from the shell that started it. Errors are negative MYOS_E* numbers. */
enum {
    SYS_MKDIR = 32, /* (path) -> 0 */
    SYS_RMDIR,      /* (path) -> 0; only empty folders */
    SYS_RENAME,     /* (from, to) -> 0; rename or move a file or folder */
    SYS_STAT,       /* (path, struct myos_stat *) -> 0 */
    SYS_READDIR,    /* (folder, index, struct myos_dirent *) -> 1 = got one, 0 = no more */
    SYS_CHDIR,      /* (path) -> 0 */
    SYS_GETCWD      /* (buf, size) -> length of the current folder's path, or -1 */
};
enum {              /* errors of the calls above (same numbers as arm/myfs.h) */
    MYOS_EIO = -1, MYOS_ENOFS = -2, MYOS_ENOENT = -3, MYOS_EFULL = -4, MYOS_ENAME = -5,
    MYOS_EEXIST = -6, MYOS_ENOTDIR = -7, MYOS_EISDIR = -8, MYOS_ENOTEMPTY = -9,
    MYOS_EV1 = -10, MYOS_ECORRUPT = -11
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
static inline long sys_wifi(long op, long a, long b) { return myos_syscall3(SYS_WIFI, op, a, b); }

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
/* name of the index-th file in the current folder (buffer of 48 bytes, longer
 * names are cut: use sys_readdir); returns its size, -1 = no more */
static inline long sys_list_files(unsigned long index, char *name48) {
    return myos_syscall(SYS_LISTFILES, (long)index, (long)name48);
}

/* folders (MyOS Pi, MyFS v2) */
static inline long sys_mkdir(const char *path)     { return myos_syscall(SYS_MKDIR, (long)path, 0); }
static inline long sys_rmdir(const char *path)     { return myos_syscall(SYS_RMDIR, (long)path, 0); }
static inline long sys_rename(const char *from, const char *to) {
    return myos_syscall(SYS_RENAME, (long)from, (long)to);
}
static inline long sys_stat(const char *path, struct myos_stat *st) {
    return myos_syscall(SYS_STAT, (long)path, (long)st);
}
static inline long sys_readdir(const char *dir, unsigned long index, struct myos_dirent *d) {
    return myos_syscall3(SYS_READDIR, (long)dir, (long)index, (long)d);
}
static inline long sys_chdir(const char *path)     { return myos_syscall(SYS_CHDIR, (long)path, 0); }
static inline long sys_getcwd(char *buf, unsigned long size) {
    return myos_syscall(SYS_GETCWD, (long)buf, (long)size);
}

/* tiny C library (csdk/libc.c) */
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);

#endif
