// minimal stdlib.h for MyOS
#ifndef MYOS_STDLIB_H
#define MYOS_STDLIB_H
#include <stddef.h>

void abort(void) __attribute__((noreturn));
int atoi(const char *s);

#endif
