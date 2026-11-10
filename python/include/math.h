// minimal math.h for MyOS
#ifndef MYOS_MATH_H
#define MYOS_MATH_H

typedef float float_t;
typedef double double_t;

#define NAN       __builtin_nan("")
#define INFINITY  __builtin_inf()
#define HUGE_VAL  __builtin_huge_val()
#define HUGE_VALF __builtin_huge_valf()

#define FP_NAN       0
#define FP_INFINITE  1
#define FP_ZERO      2
#define FP_SUBNORMAL 3
#define FP_NORMAL    4

#define isnan(x)      __builtin_isnan(x)
#define isinf(x)      __builtin_isinf(x)
#define isfinite(x)   __builtin_isfinite(x)
#define signbit(x)    __builtin_signbit(x)
#define fpclassify(x) __builtin_fpclassify(FP_NAN, FP_INFINITE, FP_NORMAL, FP_SUBNORMAL, FP_ZERO, x)

#define nan(tag)      __builtin_nan(tag)

#define M_PI 3.14159265358979323846
#define M_E  2.7182818284590452354

double sqrt(double);
double pow(double, double);
double exp(double);
double log(double);
double log10(double);
double log2(double);
double sin(double);
double cos(double);
double tan(double);
double asin(double);
double acos(double);
double atan(double);
double atan2(double, double);
double sinh(double);
double cosh(double);
double tanh(double);
double asinh(double);
double acosh(double);
double atanh(double);
double floor(double);
double ceil(double);
double trunc(double);
double fmod(double, double);
double fabs(double);
double modf(double, double *);
double frexp(double, int *);
double ldexp(double, int);
double copysign(double, double);
double nearbyint(double);
double rint(double);
double round(double);
double scalbn(double, int);
double expm1(double);
double log1p(double);
double erf(double);
double erfc(double);
double tgamma(double);
double lgamma(double);

// these two are single CPU instructions: let the compiler emit them
#define fabs(x)        __builtin_fabs(x)
#define copysign(x, y) __builtin_copysign(x, y)

#endif
