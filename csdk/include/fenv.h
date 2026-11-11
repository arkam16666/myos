/* fenv.h - MyOS C library: floating-point environment (round to nearest only) */
#ifndef MYOS_FENV_H
#define MYOS_FENV_H

#define FE_TONEAREST  0
#define FE_ALL_EXCEPT 0

static inline int fegetround(void) { return FE_TONEAREST; }
static inline int fesetround(int mode) { return mode == FE_TONEAREST ? 0 : -1; }
static inline int feclearexcept(int e) { (void)e; return 0; }

#endif
