// alloca.h for MyOS: the compiler does the work
#ifndef MYOS_ALLOCA_H
#define MYOS_ALLOCA_H
#define alloca(size) __builtin_alloca(size)
#endif
