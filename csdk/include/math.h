/* math.h - MyOS C library (double precision; code from musl via MicroPython's libm) */
#ifndef MYOS_MATH_H
#define MYOS_MATH_H

typedef float float_t;
typedef double double_t;

#define NAN       __builtin_nan("")
#define INFINITY  __builtin_inf()
#define HUGE_VAL  __builtin_huge_val()
#define HUGE_VALF __builtin_huge_valf()
#define HUGE_VALL __builtin_huge_vall()

#define FP_NAN       0
#define FP_INFINITE  1
#define FP_ZERO      2
#define FP_SUBNORMAL 3
#define FP_NORMAL    4

#define isnan(x)      __builtin_isnan(x)
#define isinf(x)      __builtin_isinf(x)
#define isfinite(x)   __builtin_isfinite(x)
#define isnormal(x)   __builtin_isnormal(x)
#define signbit(x)    __builtin_signbit(x)
#define fpclassify(x) __builtin_fpclassify(FP_NAN, FP_INFINITE, FP_NORMAL, FP_SUBNORMAL, FP_ZERO, x)

#define M_PI    3.14159265358979323846
#define M_PI_2  1.57079632679489661923
#define M_E     2.7182818284590452354
#define M_LN2   0.69314718055994530942
#define M_LN10  2.30258509299404568402

double sqrt(double);
double cbrt(double);
double hypot(double, double);
double pow(double, double);
double exp(double);
double exp2(double);
double expm1(double);
double log(double);
double log10(double);
double log2(double);
double log1p(double);
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
double round(double);
double rint(double);
double nearbyint(double);
double fmod(double, double);
double modf(double, double *);
double frexp(double, int *);
double ldexp(double, int);
double scalbn(double, int);
double copysign(double, double);
double fabs(double);
double fmin(double, double);
double fmax(double, double);
double erf(double);
double erfc(double);
double lgamma(double);
double tgamma(double);
float floorf(float);
float sqrtf(float);
float fabsf(float);
long lround(double);
long lrint(double);
long long llrint(double);

#define fabs(x)  __builtin_fabs(x)
#define fabsf(x) __builtin_fabsf(x)

#endif
