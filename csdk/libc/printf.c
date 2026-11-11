/* printf.c - MyOS C library: printf and friends.
 * Integers: d i u x X o c s p %, flags - + space # 0, width, precision, *,
 * sizes hh h l ll z t j L. Floating point: f F e E g G a A (up to 17
 * correct significant digits; more are printed as zeros).
 */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    char *buf;
    size_t cap, len;
    FILE *file;
} out_t;

static void emit(out_t *o, char c) {
    if (o->file) fputc(c, o->file);
    else if (o->len + 1 < o->cap) o->buf[o->len] = c;
    o->len++;
}

static void emit_n(out_t *o, const char *s, size_t n) {
    while (n--) emit(o, *s++);
}

static void pad(out_t *o, char c, int n) {
    while (n-- > 0) emit(o, c);
}

enum { F_LEFT = 1, F_PLUS = 2, F_SPACE = 4, F_ALT = 8, F_ZERO = 16 };

/* sign + prefix + body, padded to width */
static void emit_field(out_t *o, const char *sign, const char *body, size_t blen, int width, int flags,
                       int zero_ok) {
    int slen = (int)strlen(sign);
    int fill = width - slen - (int)blen;
    if (!(flags & F_LEFT) && !((flags & F_ZERO) && zero_ok)) pad(o, ' ', fill);
    emit_n(o, sign, (size_t)slen);
    if (!(flags & F_LEFT) && (flags & F_ZERO) && zero_ok) pad(o, '0', fill);
    emit_n(o, body, blen);
    if (flags & F_LEFT) pad(o, ' ', fill);
}

/* ------------------------------------------------------------ integers */
static void fmt_int(out_t *o, uint64_t v, int neg, int base, int upper, int flags, int width, int prec) {
    char tmp[72];
    int n = 0;
    const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (prec == 0 && v == 0) n = 0;
    else {
        do {
            tmp[sizeof tmp - 1 - n++] = dig[v % (unsigned)base];
            v /= (unsigned)base;
        } while (v);
    }
    while (n < prec && n < 64) tmp[sizeof tmp - 1 - n++] = '0';
    if ((flags & F_ALT) && base == 8 && (n == 0 || tmp[sizeof tmp - n] != '0')) tmp[sizeof tmp - 1 - n++] = '0';
    char sign[4] = "";
    if (neg) strcpy(sign, "-");
    else if (flags & F_PLUS) strcpy(sign, "+");
    else if (flags & F_SPACE) strcpy(sign, " ");
    if ((flags & F_ALT) && base == 16 && tmp[sizeof tmp - 1] != 0 && n) strcat(sign, upper ? "0X" : "0x");
    emit_field(o, sign, tmp + sizeof tmp - n, (size_t)n, width, flags, prec < 0);
}

/* ------------------------------------------------------------ floating point */
static const double p10_table[23] = {
    1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11,
    1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22 };

static double p10(int k) {                  /* 10^k for k >= 0 (exact up to 22) */
    double r = 1;
    while (k > 22) {
        r *= 1e22;
        k -= 22;
    }
    return r * p10_table[k];
}

static double scale10(double x, int k) {    /* x * 10^k, staying in range */
    while (k > 300) { x *= 1e300; k -= 300; }
    while (k < -300) { x /= 1e300; k += 300; }
    return k >= 0 ? x * p10(k) : x / p10(-k);
}

static int ilog10(double x) {               /* about floor(log10(x)), x > 0 */
    union { double d; uint64_t u; } u = { x };
    int e2 = (int)((u.u >> 52) & 0x7ff) - 1023;
    if (e2 == -1023) {                      /* subnormal */
        u.d = x * 18446744073709551616.0;
        e2 = (int)((u.u >> 52) & 0x7ff) - 1023 - 64;
    }
    int e = (int)(e2 * 0.30102999566398120);
    if (e2 < 0) e--;
    return e;
}

/* nd (1-17) significant digits of x > 0, rounded; returns the decimal
 * exponent of the first digit */
static int sig_digits(double x, int nd, char *out) {
    int e = ilog10(x);
    uint64_t lim = (uint64_t)p10(nd);
    double s = 0;
    for (int tries = 0; tries < 4; tries++) {
        s = scale10(x, nd - 1 - e);
        if (s >= (double)lim) e++;
        else if (s < (double)lim / 10) e--;
        else break;
    }
    uint64_t n = (uint64_t)(s + 0.5);
    if (n >= lim) {                         /* 9.99... rounded up to 10.0 */
        n /= 10;
        e++;
    }
    for (int i = nd - 1; i >= 0; i--) {
        out[i] = (char)('0' + n % 10);
        n /= 10;
    }
    return e;
}

static void fmt_float(out_t *o, double v, char conv, int flags, int width, int prec) {
    char sign[2] = "";
    int upper = conv >= 'A' && conv <= 'Z';
    char lc = (char)(upper ? conv + 32 : conv);
    if (__builtin_signbit(v)) {
        sign[0] = '-';
        v = -v;
    } else if (flags & F_PLUS) sign[0] = '+';
    else if (flags & F_SPACE) sign[0] = ' ';
    if (__builtin_isnan(v) || __builtin_isinf(v)) {
        const char *t = __builtin_isnan(v) ? (upper ? "NAN" : "nan") : (upper ? "INF" : "inf");
        emit_field(o, sign, t, 3, width, flags & ~F_ZERO, 0);
        return;
    }
    char body[400];
    size_t bl = 0;
    if (prec < 0) prec = 6;
    if (prec > 340) prec = 340;

    if (lc == 'a') {                        /* hexadecimal: 0x1.8p+1 */
        union { double d; uint64_t u; } u = { v };
        int e = (int)((u.u >> 52) & 0x7ff);
        uint64_t m = u.u & 0xfffffffffffffULL;
        int lead = e ? 1 : 0;
        e = v == 0 ? 0 : (e ? e - 1023 : -1022);
        const char *hx = upper ? "0123456789ABCDEF" : "0123456789abcdef";
        body[bl++] = '0';
        body[bl++] = upper ? 'X' : 'x';
        body[bl++] = (char)('0' + lead);
        char frac[13];
        int nf = 13;
        for (int i = 0; i < 13; i++) frac[i] = hx[(m >> (48 - 4 * i)) & 15];
        if (flags & 0x100) nf = prec < 13 ? prec : 13;      /* precision given */
        else while (nf > 0 && frac[nf - 1] == '0') nf--;
        if (nf || (flags & F_ALT)) body[bl++] = '.';
        memcpy(body + bl, frac, (size_t)nf);
        bl += (size_t)nf;
        bl += (size_t)snprintf(body + bl, 16, "%c%+d", upper ? 'P' : 'p', e);
        emit_field(o, sign, body, bl, width, flags, 1);
        return;
    }

    int use_e = lc == 'e';
    int strip = 0;
    if (lc == 'g') {                        /* %g picks %e or %f */
        int p = prec ? prec : 1;
        int x = 0;
        if (v != 0) {
            char d[17];
            x = sig_digits(v, p < 17 ? p : 17, d);
        }
        if (p > x && x >= -4) prec = p - 1 - x;
        else {
            use_e = 1;
            prec = p - 1;
        }
        strip = !(flags & F_ALT);
    }

    char d[360];
    if (use_e) {
        int nd = prec + 1, e = 0;
        int real = nd < 17 ? nd : 17;
        if (v == 0) memset(d, '0', (size_t)nd);
        else {
            e = sig_digits(v, real, d);
            memset(d + real, '0', (size_t)(nd - real));
        }
        body[bl++] = d[0];
        if (prec || (flags & F_ALT)) body[bl++] = '.';
        memcpy(body + bl, d + 1, (size_t)prec);
        bl += (size_t)prec;
        if (strip && prec) {
            while (body[bl - 1] == '0') bl--;
            if (body[bl - 1] == '.') bl--;
        }
        bl += (size_t)snprintf(body + bl, 8, "%c%c%02d", upper ? 'E' : 'e', e < 0 ? '-' : '+', e < 0 ? -e : e);
    } else {                                /* %f */
        int e = v == 0 ? 0 : ilog10(v);
        int nd = e + 1 + prec;              /* digits needed */
        int intlen;
        if (v == 0 || nd <= 0) {
            /* all digits are below the last place: 0 or a single 1 */
            int up = v != 0 && nd == 0 && scale10(v, prec) >= 0.5;
            memset(d, '0', (size_t)prec + 1);
            if (up) d[prec] = '1';
            intlen = 1;
        } else {
            int x = e;
            for (int tries = 0; tries < 3; tries++) {   /* the estimate of e can be 1 off */
                int real = nd < 17 ? nd : 17;
                x = sig_digits(v, real, d);
                int want = x + 1 + prec;
                if (want > 350) want = 350;
                memset(d + real, '0', (size_t)(want > real ? want - real : 0));
                if (want == nd || want <= 0) {
                    nd = want;
                    break;
                }
                nd = want;
            }
            if (x < 0) {                    /* 0.00ddd */
                memmove(d - x, d, (size_t)(nd > 0 ? nd : 0));
                memset(d, '0', (size_t)-x);
                nd = prec + 1;
                intlen = 1;
            } else intlen = x + 1;
            (void)nd;
        }
        memcpy(body, d, (size_t)intlen);
        bl = (size_t)intlen;
        if (prec || (flags & F_ALT)) body[bl++] = '.';
        memcpy(body + bl, d + intlen, (size_t)prec);
        bl += (size_t)prec;
        if (strip && prec) {
            while (body[bl - 1] == '0') bl--;
            if (body[bl - 1] == '.') bl--;
        }
    }
    emit_field(o, sign, body, bl, width, flags, 1);
}

/* ------------------------------------------------------------ the format loop */
static int format(out_t *o, const char *fmt, va_list ap) {
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            emit(o, *fmt);
            continue;
        }
        fmt++;
        int flags = 0, width = 0, prec = -1;
        for (;; fmt++) {
            if (*fmt == '-') flags |= F_LEFT;
            else if (*fmt == '+') flags |= F_PLUS;
            else if (*fmt == ' ') flags |= F_SPACE;
            else if (*fmt == '#') flags |= F_ALT;
            else if (*fmt == '0') flags |= F_ZERO;
            else break;
        }
        if (*fmt == '*') {
            width = va_arg(ap, int);
            if (width < 0) {
                flags |= F_LEFT;
                width = -width;
            }
            fmt++;
        } else while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            flags |= 0x100;
            if (*fmt == '*') {
                prec = va_arg(ap, int);
                fmt++;
            } else while (*fmt >= '0' && *fmt <= '9') prec = prec * 10 + (*fmt++ - '0');
        }
        int size = 0;                       /* -2 hh, -1 h, 1 l, 2 ll/j/z/t */
        for (;; fmt++) {
            if (*fmt == 'h') size--;
            else if (*fmt == 'l') size++;
            else if (*fmt == 'j' || *fmt == 'z' || *fmt == 't') size = 2;
            else if (*fmt == 'L') size = 3;
            else break;
        }
        char c = *fmt;
        if (!c) break;
        switch (c) {
            case 'd': case 'i': {
                long long v = size >= 1 ? (size == 1 ? va_arg(ap, long) : va_arg(ap, long long)) : va_arg(ap, int);
                if (size == -1) v = (short)v;
                if (size == -2) v = (signed char)v;
                fmt_int(o, v < 0 ? (uint64_t)(-(v + 1)) + 1 : (uint64_t)v, v < 0, 10, 0, flags, width, prec);
                break;
            }
            case 'u': case 'x': case 'X': case 'o': {
                unsigned long long v = size >= 1 ? (size == 1 ? va_arg(ap, unsigned long) : va_arg(ap, unsigned long long))
                                                 : va_arg(ap, unsigned int);
                if (size == -1) v = (unsigned short)v;
                if (size == -2) v = (unsigned char)v;
                fmt_int(o, v, 0, c == 'o' ? 8 : c == 'u' ? 10 : 16, c == 'X', flags & ~(F_PLUS | F_SPACE), width, prec);
                break;
            }
            case 'p':
                fmt_int(o, (uintptr_t)va_arg(ap, void *), 0, 16, 0, F_ALT | (flags & F_LEFT), width, -1);
                break;
            case 'c': {
                char ch = (char)va_arg(ap, int);
                emit_field(o, "", &ch, 1, width, flags, 0);
                break;
            }
            case 's': {
                const char *s = va_arg(ap, const char *);
                if (!s) s = "(null)";
                size_t n = prec >= 0 ? strnlen(s, (size_t)prec) : strlen(s);
                emit_field(o, "", s, n, width, flags, 0);
                break;
            }
            case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A':
                fmt_float(o, va_arg(ap, double), c, flags, width, prec);   /* no long double on MyOS */
                break;
            case 'n':
                *va_arg(ap, int *) = (int)o->len;
                break;
            default:
                emit(o, c);
        }
    }
    return (int)o->len;
}

int vsnprintf(char *s, size_t n, const char *fmt, va_list ap) {
    out_t o = { s, n, 0, NULL };
    format(&o, fmt, ap);
    if (n) s[o.len < n ? o.len : n - 1] = 0;
    return (int)o.len;
}

int vsprintf(char *s, const char *fmt, va_list ap) {
    return vsnprintf(s, (size_t)-1 >> 1, fmt, ap);
}

int vfprintf(FILE *f, const char *fmt, va_list ap) {
    out_t o = { NULL, 0, 0, f };
    return format(&o, fmt, ap);
}

int vprintf(const char *fmt, va_list ap) {
    return vfprintf(stdout, fmt, ap);
}

int snprintf(char *s, size_t n, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(s, n, fmt, ap);
    va_end(ap);
    return r;
}

int sprintf(char *s, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsprintf(s, fmt, ap);
    va_end(ap);
    return r;
}

int fprintf(FILE *f, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vfprintf(f, fmt, ap);
    va_end(ap);
    return r;
}

int printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return r;
}
