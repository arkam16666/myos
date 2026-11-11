/* stdlib.c - MyOS C library: number parsing, sorting, random numbers */
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

double ldexp(double x, int e);

static int digit_val(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return 99;
}

unsigned long long strtoull(const char *s, char **end, int base) {
    const char *p = s;
    while (isspace((unsigned char)*p)) p++;
    int neg = 0;
    if (*p == '+' || *p == '-') neg = *p++ == '-';
    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X') && digit_val(p[2]) < 16) {
        p += 2;
        base = 16;
    } else if (base == 0) base = (*p == '0') ? 8 : 10;
    unsigned long long v = 0;
    const char *start = p;
    int over = 0;
    for (; digit_val(*p) < base; p++) {
        unsigned long long nv = v * (unsigned)base + (unsigned)digit_val(*p);
        if (nv / (unsigned)base != v) over = 1;
        v = nv;
    }
    if (end) *end = (char *)(p == start ? s : p);
    if (over) {
        errno = ERANGE;
        return ULLONG_MAX;
    }
    return neg ? -v : v;
}

long long strtoll(const char *s, char **end, int base) {
    const char *p = s;
    while (isspace((unsigned char)*p)) p++;
    int neg = *p == '-';
    unsigned long long u = strtoull(s, end, base);
    if (neg) {
        u = -u;
        if (u > (unsigned long long)LLONG_MAX + 1) {
            errno = ERANGE;
            return LLONG_MIN;
        }
        return (long long)(0 - u);
    }
    if (u > LLONG_MAX) {
        errno = ERANGE;
        return LLONG_MAX;
    }
    return (long long)u;
}

long strtol(const char *s, char **end, int base) { return (long)strtoll(s, end, base); }
unsigned long strtoul(const char *s, char **end, int base) { return (unsigned long)strtoull(s, end, base); }
long atol(const char *s) { return strtol(s, NULL, 10); }
long long atoll(const char *s) { return strtoll(s, NULL, 10); }

static double pow10_int(int k) {
    static const double t[23] = { 1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11,
                                  1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22 };
    double r = 1;
    while (k > 22) {
        r *= 1e22;
        k -= 22;
    }
    return r * t[k];
}

double strtod(const char *s, char **end) {
    const char *p = s;
    while (isspace((unsigned char)*p)) p++;
    int neg = 0;
    if (*p == '+' || *p == '-') neg = *p++ == '-';
    double v;
    if ((p[0] | 32) == 'i' && (p[1] | 32) == 'n' && (p[2] | 32) == 'f') {
        p += 3;
        if (strncmp(p, "inity", 5) == 0 || strncmp(p, "INITY", 5) == 0) p += 5;
        v = __builtin_inf();
    } else if ((p[0] | 32) == 'n' && (p[1] | 32) == 'a' && (p[2] | 32) == 'n') {
        p += 3;
        v = __builtin_nan("");
    } else if (p[0] == '0' && (p[1] | 32) == 'x') {        /* hexadecimal: 0x1.8p3 */
        const char *q = p + 2;
        uint64_t m = 0;
        int exp2 = 0, any = 0, dot = 0;
        for (;; q++) {
            if (*q == '.' && !dot) {
                dot = 1;
                continue;
            }
            int d = digit_val(*q);
            if (d >= 16) break;
            any = 1;
            if (m >> 56) exp2 += dot ? 0 : 4;               /* too many digits: drop */
            else {
                m = m * 16 + (unsigned)d;
                if (dot) exp2 -= 4;
            }
        }
        if (!any) {
            if (end) *end = (char *)s;
            return 0;
        }
        if ((*q | 32) == 'p') {
            const char *r = q + 1;
            int eneg = 0, e = 0;
            if (*r == '+' || *r == '-') eneg = *r++ == '-';
            if (isdigit((unsigned char)*r)) {
                while (isdigit((unsigned char)*r)) {
                    if (e < 100000) e = e * 10 + (*r - '0');
                    r++;
                }
                exp2 += eneg ? -e : e;
                q = r;
            }
        }
        p = q;
        v = ldexp((double)m, exp2);
    } else {                                                /* decimal */
        uint64_t m = 0;
        int exp10 = 0, digits = 0, any = 0, dot = 0;
        for (;; p++) {
            if (*p == '.' && !dot) {
                dot = 1;
                continue;
            }
            if (!isdigit((unsigned char)*p)) break;
            any = 1;
            if (digits < 19) {
                m = m * 10 + (unsigned)(*p - '0');
                if (m) digits++;
                if (dot) exp10--;
            } else if (!dot) exp10++;
        }
        if (!any) {
            if (end) *end = (char *)s;
            return 0;
        }
        if ((*p | 32) == 'e') {
            const char *r = p + 1;
            int eneg = 0, e = 0;
            if (*r == '+' || *r == '-') eneg = *r++ == '-';
            if (isdigit((unsigned char)*r)) {
                while (isdigit((unsigned char)*r)) {
                    if (e < 100000) e = e * 10 + (*r - '0');
                    r++;
                }
                exp10 += eneg ? -e : e;
                p = r;
            }
        }
        v = (double)m;
        if (m == 0) v = 0;
        else if (exp10 > 308 + 19) {
            v = __builtin_inf();
            errno = ERANGE;
        } else if (exp10 >= 0) {
            v *= pow10_int(exp10);
        } else if (exp10 >= -308) {
            v /= pow10_int(-exp10);
        } else if (exp10 >= -360) {                         /* tiny: two steps */
            v /= 1e300;
            v /= pow10_int(-exp10 - 300);
        } else v = 0;
    }
    if (end) *end = (char *)p;
    return neg ? -v : v;
}

float strtof(const char *s, char **end) { return (float)strtod(s, end); }
double atof(const char *s) { return strtod(s, NULL); }

int abs(int x) { return x < 0 ? -x : x; }
long labs(long x) { return x < 0 ? -x : x; }
long long llabs(long long x) { return x < 0 ? -x : x; }

div_t div(int a, int b) {
    div_t r = { a / b, a % b };
    return r;
}

ldiv_t ldiv(long a, long b) {
    ldiv_t r = { a / b, a % b };
    return r;
}

static unsigned long long rand_state = 1;

int rand(void) {
    rand_state = rand_state * 6364136223846793005ULL + 1442695040888963407ULL;
    return (int)(rand_state >> 33);
}

void srand(unsigned seed) {
    rand_state = seed;
}

static void swap_bytes(char *a, char *b, size_t n) {
    while (n--) {
        char t = *a;
        *a++ = *b;
        *b++ = t;
    }
}

void qsort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *)) {
    char *b = base;                             /* shell sort: small and good enough */
    for (size_t gap = n / 2; gap > 0; gap /= 2) {
        for (size_t i = gap; i < n; i++) {
            for (size_t j = i; j >= gap && cmp(b + (j - gap) * size, b + j * size) > 0; j -= gap)
                swap_bytes(b + (j - gap) * size, b + j * size, size);
        }
    }
}

void *bsearch(const void *key, const void *base, size_t n, size_t size,
              int (*cmp)(const void *, const void *)) {
    const char *b = base;
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        int c = cmp(key, b + mid * size);
        if (c == 0) return (void *)(b + mid * size);
        if (c < 0) hi = mid;
        else lo = mid + 1;
    }
    return NULL;
}
