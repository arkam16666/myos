/* util.c - inflate (for jar files), double <-> decimal, memory, time */
#include <math.h>
#include <stdarg.h>
#include <time.h>
#include "jvm.h"
#ifdef MYOS_LIBC
#include "myos.h"
#endif

void fatal(const char *fmt, ...) {
    va_list ap;
    fflush(stdout);
    fprintf(stderr, "java: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    fflush(stderr);
    exit(1);
}

void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) fatal("out of memory (C heap)");
    return p;
}

void *xcalloc(size_t n, size_t m) {
    void *p = calloc(n ? n : 1, m ? m : 1);
    if (!p) fatal("out of memory (C heap)");
    return p;
}

/* ------------------------------------------------------------ time */
jlong now_millis(void) {
    struct timespec ts;
#ifdef MYOS_LIBC
    clock_gettime(CLOCK_REALTIME, &ts);
#else
    timespec_get(&ts, TIME_UTC);
#endif
    return (jlong)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

jlong now_nanos(void) {
    struct timespec ts;
#ifdef MYOS_LIBC
    clock_gettime(CLOCK_MONOTONIC, &ts);
#else
    timespec_get(&ts, TIME_UTC);
#endif
    return (jlong)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

void sleep_millis(jlong ms) {
    if (ms <= 0) return;
#ifdef MYOS_LIBC
    sys_sleep((unsigned long)((ms + 9) / 10));
#else
    jlong end = now_millis() + ms;
    while (now_millis() < end) {}
#endif
}

/* ------------------------------------------------------------ inflate (RFC 1951) */
typedef struct { uint16_t count[16], sym[288]; } Huff;
typedef struct {
    const uint8_t *s, *se;
    uint8_t *d, *d0, *de;
    uint32_t bits;
    int nbits, err;
} Inf;

static int getbit(Inf *f) {
    if (!f->nbits) {
        if (f->s >= f->se) { f->err = 1; return 0; }
        f->bits = *f->s++;
        f->nbits = 8;
    }
    int b = f->bits & 1;
    f->bits >>= 1;
    f->nbits--;
    return b;
}

static uint32_t getbits(Inf *f, int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; i++) v |= (uint32_t)getbit(f) << i;
    return v;
}

static void build(Huff *h, const uint8_t *len, int n) {
    uint16_t offs[16];
    memset(h->count, 0, sizeof h->count);
    for (int i = 0; i < n; i++) h->count[len[i]]++;
    h->count[0] = 0;
    offs[1] = 0;
    for (int i = 1; i < 15; i++) offs[i + 1] = offs[i] + h->count[i];
    for (int i = 0; i < n; i++) if (len[i]) h->sym[offs[len[i]]++] = (uint16_t)i;
}

static int decode(Inf *f, Huff *h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; len++) {
        code |= getbit(f);
        int count = h->count[len];
        if (code - first < count) return h->sym[index + code - first];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
        if (f->err) return -1;
    }
    f->err = 1;
    return -1;
}

static const uint16_t lbase[] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83,
                                  99, 115, 131, 163, 195, 227, 258 };
static const uint8_t lextra[] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const uint16_t dbase[] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025,
                                  1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const uint8_t dextra[] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12,
                                  12, 13, 13 };

static int inflate_block(Inf *f, Huff *lt, Huff *dt) {
    for (;;) {
        int sym = decode(f, lt);
        if (f->err) return -1;
        if (sym < 256) {
            if (f->d >= f->de) return -1;
            *f->d++ = (uint8_t)sym;
        } else if (sym == 256) return 0;
        else {
            sym -= 257;
            if (sym >= 29) return -1;
            int len = lbase[sym] + (int)getbits(f, lextra[sym]);
            int ds = decode(f, dt);
            if (ds < 0 || ds >= 30) return -1;
            int dist = dbase[ds] + (int)getbits(f, dextra[ds]);
            if (dist > f->d - f->d0 || f->d + len > f->de) return -1;
            for (int i = 0; i < len; i++, f->d++) *f->d = f->d[-dist];
        }
    }
}

int inflate_raw(const unsigned char *src, size_t srclen, unsigned char *dst, size_t dstlen) {
    Inf f = { src, src + srclen, dst, dst, dst + dstlen, 0, 0, 0 };
    static Huff lt, dt;
    uint8_t lens[320];
    int last;
    do {
        last = getbit(&f);
        int type = (int)getbits(&f, 2);
        if (type == 0) {
            f.nbits = 0;
            if (f.s + 4 > f.se) return -1;
            uint32_t len = f.s[0] | f.s[1] << 8;
            f.s += 4;
            if (f.s + len > f.se || f.d + len > f.de) return -1;
            memcpy(f.d, f.s, len);
            f.d += len;
            f.s += len;
        } else if (type == 1) {
            int i = 0;
            for (; i < 144; i++) lens[i] = 8;
            for (; i < 256; i++) lens[i] = 9;
            for (; i < 280; i++) lens[i] = 7;
            for (; i < 288; i++) lens[i] = 8;
            build(&lt, lens, 288);
            for (i = 0; i < 30; i++) lens[i] = 5;
            build(&dt, lens, 30);
            if (inflate_block(&f, &lt, &dt)) return -1;
        } else if (type == 2) {
            static const uint8_t order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
            int hlit = (int)getbits(&f, 5) + 257, hdist = (int)getbits(&f, 5) + 1, hclen = (int)getbits(&f, 4) + 4;
            uint8_t cl[19] = { 0 };
            for (int i = 0; i < hclen; i++) cl[order[i]] = (uint8_t)getbits(&f, 3);
            Huff ct;
            build(&ct, cl, 19);
            int n = 0;
            while (n < hlit + hdist) {
                int sym = decode(&f, &ct);
                if (f.err || sym < 0) return -1;
                if (sym < 16) lens[n++] = (uint8_t)sym;
                else {
                    int rep, val = 0;
                    if (sym == 16) {
                        if (!n) return -1;
                        val = lens[n - 1];
                        rep = 3 + (int)getbits(&f, 2);
                    } else if (sym == 17) rep = 3 + (int)getbits(&f, 3);
                    else rep = 11 + (int)getbits(&f, 7);
                    if (n + rep > hlit + hdist) return -1;
                    while (rep--) lens[n++] = (uint8_t)val;
                }
            }
            build(&lt, lens, hlit);
            build(&dt, lens + hlit, hdist);
            if (inflate_block(&f, &lt, &dt)) return -1;
        } else return -1;
        if (f.err) return -1;
    } while (!last);
    return (int)(f.d - dst);
}

/* ------------------------------------------------------------ big integers */
#define BN 300
typedef struct { int n; uint32_t d[BN]; } Big;

static void big_set(Big *b, uint64_t v) {
    b->n = 0;
    while (v) { b->d[b->n++] = (uint32_t)v; v >>= 32; }
}
static void big_mul_small(Big *b, uint32_t m) {
    uint64_t carry = 0;
    for (int i = 0; i < b->n; i++) {
        uint64_t t = (uint64_t)b->d[i] * m + carry;
        b->d[i] = (uint32_t)t;
        carry = t >> 32;
    }
    if (carry && b->n < BN) b->d[b->n++] = (uint32_t)carry;
}
static void big_add_small(Big *b, uint32_t a) {
    uint64_t carry = a;
    for (int i = 0; i < b->n && carry; i++) {
        uint64_t t = (uint64_t)b->d[i] + carry;
        b->d[i] = (uint32_t)t;
        carry = t >> 32;
    }
    if (carry && b->n < BN) b->d[b->n++] = (uint32_t)carry;
}
static void big_pow10(Big *b, int k) {
    while (k >= 9) { big_mul_small(b, 1000000000u); k -= 9; }
    static const uint32_t p[] = { 1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000 };
    if (k > 0) big_mul_small(b, p[k]);
}
static void big_shl(Big *b, int s) {
    if (!b->n || s <= 0) return;
    int w = s / 32, r = s % 32;
    if (b->n + w + 1 > BN) return;
    if (r) {
        uint32_t carry = 0;
        for (int i = 0; i < b->n; i++) {
            uint32_t v = b->d[i];
            b->d[i] = v << r | carry;
            carry = v >> (32 - r);
        }
        if (carry) b->d[b->n++] = carry;
    }
    if (w) {
        memmove(b->d + w, b->d, (size_t)b->n * 4);
        memset(b->d, 0, (size_t)w * 4);
        b->n += w;
    }
}
static int big_cmp(const Big *a, const Big *b) {
    if (a->n != b->n) return a->n < b->n ? -1 : 1;
    for (int i = a->n - 1; i >= 0; i--)
        if (a->d[i] != b->d[i]) return a->d[i] < b->d[i] ? -1 : 1;
    return 0;
}
static void big_add(Big *r, const Big *a, const Big *b) {    /* r may alias a */
    uint64_t carry = 0;
    int n = a->n > b->n ? a->n : b->n;
    for (int i = 0; i < n; i++) {
        uint64_t t = carry + (i < a->n ? a->d[i] : 0) + (i < b->n ? b->d[i] : 0);
        r->d[i] = (uint32_t)t;
        carry = t >> 32;
    }
    r->n = n;
    if (carry && r->n < BN) r->d[r->n++] = (uint32_t)carry;
}
static void big_sub(Big *a, const Big *b) {                    /* a -= b, a >= b */
    int64_t borrow = 0;
    for (int i = 0; i < a->n; i++) {
        int64_t t = (int64_t)a->d[i] - (i < b->n ? b->d[i] : 0) - borrow;
        borrow = t < 0;
        a->d[i] = (uint32_t)(t + (borrow ? ((int64_t)1 << 32) : 0));
    }
    while (a->n && !a->d[a->n - 1]) a->n--;
}

/* ------------------------------------------------------------ shortest digits (Burger & Dybvig) */
static int shortest(uint64_t f, int e, int p, int min_e, double v, char *out, int *k10) {
    static Big r, s, mp, mm, t;
    int even = !(f & 1);
    uint64_t hidden = (uint64_t)1 << (p - 1);
    if (e >= 0) {
        big_set(&r, f);
        if (f != hidden) {
            big_shl(&r, e + 1);
            big_set(&s, 2);
            big_set(&mp, 1); big_shl(&mp, e);
            big_set(&mm, 1); big_shl(&mm, e);
        } else {
            big_shl(&r, e + 2);
            big_set(&s, 4);
            big_set(&mp, 1); big_shl(&mp, e + 1);
            big_set(&mm, 1); big_shl(&mm, e);
        }
    } else {
        if (e == min_e || f != hidden) {
            big_set(&r, f); big_shl(&r, 1);
            big_set(&s, 1); big_shl(&s, 1 - e);
            big_set(&mp, 1);
            big_set(&mm, 1);
        } else {
            big_set(&r, f); big_shl(&r, 2);
            big_set(&s, 1); big_shl(&s, 2 - e);
            big_set(&mp, 2);
            big_set(&mm, 1);
        }
    }
    int k = (int)ceil(log10(v) - 1e-10);
    if (k >= 0) big_pow10(&s, k);
    else {
        big_pow10(&r, -k);
        big_pow10(&mp, -k);
        big_pow10(&mm, -k);
    }
    big_add(&t, &r, &mp);
    int c = big_cmp(&t, &s);
    if (even ? c >= 0 : c > 0) k++;
    else {
        big_mul_small(&r, 10);
        big_mul_small(&mp, 10);
        big_mul_small(&mm, 10);
    }
    int n = 0;
    for (;;) {
        int d = 0;
        while (big_cmp(&r, &s) >= 0) { big_sub(&r, &s); d++; }
        int tc1 = even ? big_cmp(&r, &mm) <= 0 : big_cmp(&r, &mm) < 0;
        big_add(&t, &r, &mp);
        c = big_cmp(&t, &s);
        int tc2 = even ? c >= 0 : c > 0;
        if (!tc1 && !tc2) {
            out[n++] = (char)('0' + d);
            big_mul_small(&r, 10);
            big_mul_small(&mp, 10);
            big_mul_small(&mm, 10);
            if (n >= 25) break;
            continue;
        }
        if (tc1 && tc2) {
            big_add(&t, &r, &r);
            if (big_cmp(&t, &s) >= 0) d++;
        } else if (tc2) d++;
        out[n++] = (char)('0' + d);
        break;
    }
    /* a digit may have become 10 */
    for (int i = n - 1; i > 0; i--)
        if (out[i] > '9') { out[i] = '0'; out[i - 1]++; }
    if (out[0] > '9') { memmove(out + 1, out, (size_t)n); out[0] = '1'; out[1] = '0'; k++; }
    while (n > 1 && out[n - 1] == '0') n--;
    out[n] = 0;
    *k10 = k;
    return n;
}

int double_to_digits(double v, char *digits, int *exp10) {
    uint64_t bits;
    memcpy(&bits, &v, 8);
    int be = (int)((bits >> 52) & 0x7ff);
    uint64_t f = bits & ((1ull << 52) - 1);
    int e;
    if (be) { f |= 1ull << 52; e = be - 1075; }
    else e = -1074;
    return shortest(f, e, 53, -1074, fabs(v), digits, exp10);
}

int float_to_digits(float v, char *digits, int *exp10) {
    uint32_t bits;
    memcpy(&bits, &v, 4);
    int be = (int)((bits >> 23) & 0xff);
    uint64_t f = bits & ((1u << 23) - 1);
    int e;
    if (be) { f |= 1u << 23; e = be - 150; }
    else e = -149;
    return shortest(f, e, 24, -149, fabs((double)v), digits, exp10);
}

static void java_format(int neg, const char *d, int n, int k, char *out) {
    char *o = out;
    if (neg) *o++ = '-';
    /* value = 0.d * 10^k; plain if 1e-3 <= v < 1e7 */
    if (k >= -2 && k <= 7) {
        if (k <= 0) {
            *o++ = '0';
            *o++ = '.';
            for (int i = 0; i < -k; i++) *o++ = '0';
            for (int i = 0; i < n; i++) *o++ = d[i];
        } else if (k >= n) {
            for (int i = 0; i < n; i++) *o++ = d[i];
            for (int i = n; i < k; i++) *o++ = '0';
            *o++ = '.';
            *o++ = '0';
        } else {
            for (int i = 0; i < k; i++) *o++ = d[i];
            *o++ = '.';
            for (int i = k; i < n; i++) *o++ = d[i];
        }
        *o = 0;
        return;
    }
    *o++ = d[0];
    *o++ = '.';
    if (n > 1) for (int i = 1; i < n; i++) *o++ = d[i];
    else *o++ = '0';
    sprintf(o, "E%d", k - 1);
}

void java_double_string(double v, char *out) {
    if (v != v) { strcpy(out, "NaN"); return; }
    if (v == INFINITY) { strcpy(out, "Infinity"); return; }
    if (v == -INFINITY) { strcpy(out, "-Infinity"); return; }
    if (v == 0) { strcpy(out, signbit(v) ? "-0.0" : "0.0"); return; }
    char d[32];
    int k, n = double_to_digits(v, d, &k);
    java_format(v < 0, d, n, k, out);
}

void java_float_string(float v, char *out) {
    if (v != v) { strcpy(out, "NaN"); return; }
    if (v == INFINITY) { strcpy(out, "Infinity"); return; }
    if (v == -INFINITY) { strcpy(out, "-Infinity"); return; }
    if (v == 0) { strcpy(out, signbit(v) ? "-0.0" : "0.0"); return; }
    char d[32];
    int k, n = float_to_digits(v, d, &k);
    java_format(v < 0, d, n, k, out);
}

/* ------------------------------------------------------------ parsing (Java's Double.parseDouble) */
static double next_up(double z) {
    uint64_t b;
    memcpy(&b, &z, 8);
    b++;
    memcpy(&z, &b, 8);
    return z;
}
static double next_down(double z) {
    uint64_t b;
    memcpy(&b, &z, 8);
    b--;
    memcpy(&z, &b, 8);
    return z;
}

/* compare D * 10^E with the midpoint (2f+1) * 2^(e-1): returns <0, 0, >0 */
static int cmp_mid(const char *digits, int nd, int E, uint64_t f, int e) {
    static Big x, y;
    x.n = 0;
    for (int i = 0; i < nd; i++) {
        big_mul_small(&x, 10);
        big_add_small(&x, (uint32_t)(digits[i] - '0'));
    }
    big_set(&y, 2 * f + 1);
    int e2 = e - 1;
    if (E >= 0) big_pow10(&x, E); else big_pow10(&y, -E);
    if (e2 >= 0) big_shl(&y, e2); else big_shl(&x, -e2);
    return big_cmp(&x, &y);
}

double parse_double(const char *s, int *ok) {
    *ok = 0;
    while (*s && (unsigned char)*s <= ' ') s++;
    size_t len = strlen(s);
    while (len && (unsigned char)s[len - 1] <= ' ') len--;
    if (!len || len > 2000) return 0;
    char buf[2048];
    memcpy(buf, s, len);
    buf[len] = 0;
    char *p = buf;
    int neg = 0;
    if (*p == '+' || *p == '-') neg = *p++ == '-';
    if (!strcmp(p, "NaN")) { *ok = 1; return NAN; }
    if (!strcmp(p, "Infinity")) { *ok = 1; return neg ? -INFINITY : INFINITY; }
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        char *end;
        double v = strtod(p, &end);
        if (*end == 'd' || *end == 'D' || *end == 'f' || *end == 'F') end++;
        if (*end || (!strchr(p, 'p') && !strchr(p, 'P'))) return 0;
        *ok = 1;
        return neg ? -v : v;
    }
    char digits[800];
    int nd = 0, E = 0, any = 0, dot = 0;
    for (; *p; p++) {
        if (*p == '.' && !dot) { dot = 1; continue; }
        if (*p < '0' || *p > '9') break;
        any = 1;
        if (nd == 0 && *p == '0') { if (dot) E--; continue; }
        if (nd < 780) { digits[nd++] = *p; if (dot) E--; }
        else if (!dot) E++;
    }
    if (!any) return 0;
    if (*p == 'e' || *p == 'E') {
        p++;
        int eneg = 0, ev = 0, ed = 0;
        if (*p == '+' || *p == '-') eneg = *p++ == '-';
        while (*p >= '0' && *p <= '9') { if (ev < 100000) ev = ev * 10 + (*p - '0'); p++; ed = 1; }
        if (!ed) return 0;
        E += eneg ? -ev : ev;
    }
    if (*p == 'd' || *p == 'D' || *p == 'f' || *p == 'F') p++;
    if (*p) return 0;
    *ok = 1;
    while (nd > 0 && digits[nd - 1] == '0') { nd--; E++; }
    if (nd == 0) return neg ? -0.0 : 0.0;
    if (nd + E > 310) return neg ? -INFINITY : INFINITY;
    if (nd + E < -330) return neg ? -0.0 : 0.0;
    /* exact fast path */
    if (nd <= 15 && E >= -22 && E <= 22) {
        double m = 0;
        for (int i = 0; i < nd; i++) m = m * 10 + (digits[i] - '0');
        static const double p10[] = { 1, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11, 1e12, 1e13,
                                      1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22 };
        double v = E >= 0 ? m * p10[E] : m / p10[-E];
        return neg ? -v : v;
    }
    /* approximate, then correct to the nearest double by exact comparison */
    char tmp[900];
    memcpy(tmp, digits, (size_t)nd);
    sprintf(tmp + nd, "e%d", E);
    double z = strtod(tmp, NULL);
    if (z == INFINITY) z = 1.7976931348623157e308;
    for (int iter = 0; iter < 4; iter++) {
        uint64_t bits;
        memcpy(&bits, &z, 8);
        int be = (int)((bits >> 52) & 0x7ff);
        uint64_t f = bits & ((1ull << 52) - 1);
        int e;
        if (be) { f |= 1ull << 52; e = be - 1075; } else e = -1074;
        if (be == 0x7ff) break;
        int up = cmp_mid(digits, nd, E, f, e);              /* vs upper midpoint */
        if (up > 0 || (up == 0 && (f & 1))) {
            z = next_up(z);
            if (z == INFINITY) break;
            continue;
        }
        /* lower midpoint: (2f-1) * 2^(e-1), or for a power of two (2^53 boundary) the gap is half */
        if (z == 0) break;
        double lo = next_down(z);
        uint64_t lb;
        memcpy(&lb, &lo, 8);
        int lbe = (int)((lb >> 52) & 0x7ff);
        uint64_t lf = lb & ((1ull << 52) - 1);
        int le;
        if (lbe) { lf |= 1ull << 52; le = lbe - 1075; } else le = -1074;
        int dn = cmp_mid(digits, nd, E, lf, le);            /* vs midpoint between lo and z */
        if (dn < 0 || (dn == 0 && (f & 1))) {               /* below, or a tie and lo is even */
            z = lo;
            continue;
        }
        break;
    }
    return neg ? -z : z;
}
