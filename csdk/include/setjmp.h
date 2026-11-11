/* setjmp.h - MyOS C library (AArch64: x19-x30, sp, d8-d15) */
#ifndef MYOS_SETJMP_H
#define MYOS_SETJMP_H

typedef unsigned long jmp_buf[22];

int setjmp(jmp_buf env) __attribute__((returns_twice));
void longjmp(jmp_buf env, int val) __attribute__((noreturn));
#define _setjmp setjmp
#define _longjmp longjmp

#endif
