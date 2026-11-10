// minimal stdio.h for MyOS (implemented in shared/libc/printf.c)
#ifndef MYOS_STDIO_H
#define MYOS_STDIO_H
#include <stdarg.h>
#include <stddef.h>

#define EOF (-1)

int printf(const char *fmt, ...);
int vprintf(const char *fmt, va_list ap);
int putchar(int c);
int puts(const char *s);
int snprintf(char *str, size_t size, const char *fmt, ...);
int vsnprintf(char *str, size_t size, const char *fmt, va_list ap);

#endif
