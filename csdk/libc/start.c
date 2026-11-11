/* start.c - MyOS C library: turn the command line into main(argc, argv),
 * and the small things around starting and ending a program. */
#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../myos.h"

int errno;
extern int main(int argc, char **argv);
void myos_stdio_flush_all(void);

#define MAX_ARGS 32
static char argbuf[256];
static char *argv[MAX_ARGS + 1];
static void (*exit_fns[8])(void);
static int n_exit_fns;

/* the kernel calls this (through crt0.c) with the text after the program name */
int myos_libc_main(const char *args, const char *progname) {
    int argc = 0;
    argv[argc++] = (char *)progname;
    strncpy(argbuf, args ? args : "", sizeof argbuf - 1);
    char *p = argbuf;
    while (*p && argc < MAX_ARGS) {             /* split at spaces; "..." keeps spaces */
        while (*p == ' ') p++;
        if (!*p) break;
        char quote = (*p == '"') ? *p++ : 0;
        argv[argc++] = p;
        while (*p && (quote ? *p != quote : *p != ' ')) p++;
        if (*p) *p++ = 0;
    }
    argv[argc] = NULL;
    exit(main(argc, argv));
}

int atexit(void (*fn)(void)) {
    if (n_exit_fns >= 8) return -1;
    exit_fns[n_exit_fns++] = fn;
    return 0;
}

void exit(int code) {
    while (n_exit_fns) exit_fns[--n_exit_fns]();
    myos_stdio_flush_all();
    sys_exit(code);
}

void _Exit(int code) {
    sys_exit(code);
}

void abort(void) {
    myos_stdio_flush_all();
    sys_print("abort() called\n");
    sys_exit(134);
}

void __myos_assert_fail(const char *expr, const char *file, int line) {
    fprintf(stderr, "Assertion failed: %s (%s line %d)\n", expr, file, line);
    abort();
}

char *getenv(const char *name) {
    (void)name;
    return NULL;                                /* no environment variables yet */
}

int system(const char *cmd) {
    return cmd ? -1 : 0;                        /* no shell to run commands from a program */
}

static struct lconv c_locale = { ".", "", "" };

char *setlocale(int category, const char *locale) {
    (void)category;
    if (!locale || !*locale || strcmp(locale, "C") == 0 || strcmp(locale, "POSIX") == 0) return "C";
    return NULL;
}

struct lconv *localeconv(void) {
    return &c_locale;
}

sighandler_t signal(int sig, sighandler_t handler) {
    (void)sig;
    (void)handler;
    return SIG_DFL;
}

int raise(int sig) {
    (void)sig;
    return 0;
}

char *strerror(int err) {
    switch (err) {
        case 0:      return "No error";
        case ENOENT: return "No such file";
        case EIO:    return "Disk error";
        case EBADF:  return "Bad file";
        case ENOMEM: return "Out of memory";
        case EACCES: return "Not allowed";
        case EINVAL: return "Invalid argument";
        case EMFILE: return "Too many open files";
        case ENOSPC: return "Disk full";
        case ERANGE: return "Result out of range";
        case EDOM:   return "Math argument out of domain";
        case ENOSYS: return "Not supported on MyOS";
        default:     return "Unknown error";
    }
}
