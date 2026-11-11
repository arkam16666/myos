/* mathx.c - MyOS C library: math functions not in MicroPython's libm */
#include <math.h>

#undef fabs
#undef fabsf
double fabs(double x) { return __builtin_fabs(x); }
float fabsf(float x) { return __builtin_fabsf(x); }
double log2(double x) { return log(x) / M_LN2; }
double exp2(double x) { return pow(2.0, x); }
double cbrt(double x) { return x < 0 ? -pow(-x, 1.0 / 3) : pow(x, 1.0 / 3); }
double hypot(double a, double b) { return sqrt(a * a + b * b); }
double fmin(double a, double b) { return a < b ? a : b; }
double fmax(double a, double b) { return a > b ? a : b; }
float floorf(float x) { return (float)floor(x); }
float sqrtf(float x) { return (float)sqrt(x); }
long lround(double x) { return (long)round(x); }
long lrint(double x) { return (long)rint(x); }
long long llrint(double x) { return (long long)rint(x); }
