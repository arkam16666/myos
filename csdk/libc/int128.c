/* int128.c - MyOS C library: 128-bit integer division, which the compiler
 * turns into calls to these helpers (normally in compiler-rt / libgcc).
 * QuickJS uses it for BigInt. Plain shift-and-subtract. */
#ifdef __SIZEOF_INT128__
typedef unsigned __int128 u128;
typedef __int128 s128;

static u128 udivmod(u128 n, u128 d, u128 *rem) {
    if (d == 0) {                               /* like hardware: no trap, garbage result */
        if (rem) *rem = n;
        return ~(u128)0;
    }
    if (d > n) {
        if (rem) *rem = n;
        return 0;
    }
    if ((n >> 64) == 0) {                       /* fits in 64 bits: the CPU can do it */
        unsigned long a = (unsigned long)n, b = (unsigned long)d;
        if (rem) *rem = a % b;
        return a / b;
    }
    unsigned long dhi = (unsigned long)(d >> 64);
    int dzeros = dhi ? __builtin_clzll(dhi) : 64 + __builtin_clzll((unsigned long)d);
    int shift = dzeros - __builtin_clzll((unsigned long)(n >> 64));    /* line d up under n */
    u128 q = 0;
    d <<= shift;
    for (int i = 0; i <= shift; i++) {
        q <<= 1;
        if (n >= d) {
            n -= d;
            q |= 1;
        }
        d >>= 1;
    }
    if (rem) *rem = n;
    return q;
}

u128 __udivti3(u128 n, u128 d) { return udivmod(n, d, 0); }

u128 __umodti3(u128 n, u128 d) {
    u128 r;
    udivmod(n, d, &r);
    return r;
}

s128 __divti3(s128 a, s128 b) {
    int neg = (a < 0) != (b < 0);
    u128 q = udivmod(a < 0 ? -(u128)a : (u128)a, b < 0 ? -(u128)b : (u128)b, 0);
    return neg ? -(s128)q : (s128)q;
}

s128 __modti3(s128 a, s128 b) {
    u128 r;
    udivmod(a < 0 ? -(u128)a : (u128)a, b < 0 ? -(u128)b : (u128)b, &r);
    return a < 0 ? -(s128)r : (s128)r;
}
#endif
