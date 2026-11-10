// math functions MicroPython's libm_dbl doesn't include
#include <math.h>

double log2(double x) {
    return log(x) * 1.44269504088896340736;     // log(x) / log(2)
}
