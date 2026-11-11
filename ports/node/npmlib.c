/* npmlib.c - the native half of npm on MyOS (included by node.c):
 *   gunzip     .tgz packages: gzip wrapper + inflate (RFC 1951/1952)
 *   SHA-512 / SHA-1   to check tarballs against the registry's dist.integrity / dist.shasum
 *   JSON scan  find one value (or the keys of one object) in a big document
 *              (a registry "packument" can be megabytes) without building it
 * Plain C with no QuickJS in it, so tests/host/npmlib_test.c can check it on a PC.
 * Written for MyOS (MIT licence); the inflate follows the structure of the
 * RFC, decoding Huffman codes one bit at a time (canonical codes, like puff). */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ================================================================ inflate */
typedef struct {
    const uint8_t *in;
    size_t inlen, inpos;
    uint32_t bitbuf;
    int bitcnt, err;
    uint8_t *out;
    size_t outlen, outpos;
} inf_state;

typedef struct { short count[16], symbol[288]; } huff;

static int inf_bits(inf_state *s, int need) {
    uint32_t val = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->inpos >= s->inlen) { s->err = 1; return 0; }
        val |= (uint32_t)s->in[s->inpos++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = val >> need;
    s->bitcnt -= need;
    return (int)(val & ((1u << need) - 1));
}

/* canonical Huffman code from code lengths; <0 over-subscribed, >0 incomplete, 0 complete */
static int huff_build(huff *h, const uint8_t *len, int n) {
    short offs[16];
    memset(h->count, 0, sizeof h->count);
    for (int i = 0; i < n; i++) h->count[len[i]]++;
    if (h->count[0] == n) return 0;
    int left = 1;
    for (int i = 1; i < 16; i++) {
        left = (left << 1) - h->count[i];
        if (left < 0) return left;
    }
    offs[1] = 0;
    for (int i = 1; i < 15; i++) offs[i + 1] = (short)(offs[i] + h->count[i]);
    for (int i = 0; i < n; i++)
        if (len[i]) h->symbol[offs[len[i]]++] = (short)i;
    return left;
}

static int huff_decode(inf_state *s, const huff *h) {
    int code = 0, first = 0, index = 0;
    for (int l = 1; l < 16; l++) {
        code |= inf_bits(s, 1);
        int count = h->count[l];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    s->err = 1;
    return 0;
}

static const short lbase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const uint8_t lext[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const unsigned short dbase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385,
                                          513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const uint8_t dext[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

/* 0 = end of block, -1 = bad data, -2 = output full */
static int inf_codes(inf_state *s, const huff *lc, const huff *dc) {
    for (;;) {
        int sym = huff_decode(s, lc);
        if (s->err) return -1;
        if (sym < 256) {
            if (s->outpos >= s->outlen) return -2;
            s->out[s->outpos++] = (uint8_t)sym;
        } else if (sym == 256) {
            return 0;
        } else {
            sym -= 257;
            if (sym >= 29) return -1;
            size_t len = (size_t)lbase[sym] + (size_t)inf_bits(s, lext[sym]);
            int ds = huff_decode(s, dc);
            if (s->err || ds >= 30) return -1;
            size_t dist = (size_t)dbase[ds] + (size_t)inf_bits(s, dext[ds]);
            if (s->err || dist > s->outpos) return -1;
            if (s->outpos + len > s->outlen) return -2;
            uint8_t *o = s->out + s->outpos;
            for (size_t i = 0; i < len; i++) o[i] = o[(ptrdiff_t)i - (ptrdiff_t)dist];
            s->outpos += len;
        }
    }
}

static int inf_stored(inf_state *s) {
    s->bitbuf = 0;                              /* the rest of the current byte */
    s->bitcnt = 0;
    if (s->inpos + 4 > s->inlen) return -1;
    const uint8_t *p = s->in + s->inpos;
    unsigned len = p[0] | (unsigned)p[1] << 8, nlen = p[2] | (unsigned)p[3] << 8;
    if (len != (~nlen & 0xFFFF)) return -1;
    s->inpos += 4;
    if (s->inpos + len > s->inlen) return -1;
    if (s->outpos + len > s->outlen) return -2;
    memcpy(s->out + s->outpos, s->in + s->inpos, len);
    s->inpos += len;
    s->outpos += len;
    return 0;
}

static int inf_fixed(inf_state *s) {
    static huff lc, dc;
    static int built;
    if (!built) {
        uint8_t l[288];
        int i = 0;
        for (; i < 144; i++) l[i] = 8;
        for (; i < 256; i++) l[i] = 9;
        for (; i < 280; i++) l[i] = 7;
        for (; i < 288; i++) l[i] = 8;
        huff_build(&lc, l, 288);
        for (i = 0; i < 30; i++) l[i] = 5;
        huff_build(&dc, l, 30);
        built = 1;
    }
    return inf_codes(s, &lc, &dc);
}

static int inf_dynamic(inf_state *s) {
    static const uint8_t order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    uint8_t lengths[320];
    huff lc, dc;
    int nlen = inf_bits(s, 5) + 257, ndist = inf_bits(s, 5) + 1, ncode = inf_bits(s, 4) + 4;
    if (s->err || nlen > 286 || ndist > 30) return -1;
    memset(lengths, 0, 19);
    for (int i = 0; i < ncode; i++) lengths[order[i]] = (uint8_t)inf_bits(s, 3);
    if (s->err || huff_build(&lc, lengths, 19) != 0) return -1;
    int idx = 0;
    while (idx < nlen + ndist) {
        int sym = huff_decode(s, &lc), len = 0;
        if (s->err) return -1;
        if (sym < 16) {
            lengths[idx++] = (uint8_t)sym;
            continue;
        }
        if (sym == 16) {
            if (idx == 0) return -1;
            len = lengths[idx - 1];
            sym = 3 + inf_bits(s, 2);
        } else if (sym == 17) {
            sym = 3 + inf_bits(s, 3);
        } else {
            sym = 11 + inf_bits(s, 7);
        }
        if (s->err || idx + sym > nlen + ndist) return -1;
        while (sym--) lengths[idx++] = (uint8_t)len;
    }
    if (lengths[256] == 0) return -1;
    int e = huff_build(&lc, lengths, nlen);
    if (e < 0 || (e > 0 && nlen - lc.count[0] != 1)) return -1;
    e = huff_build(&dc, lengths + nlen, ndist);
    if (e < 0 || (e > 0 && ndist - dc.count[0] != 1)) return -1;
    return inf_codes(s, &lc, &dc);
}

/* raw deflate data -> out; returns bytes written, -1 bad data, -2 doesn't fit.
 * *used (if given) = input bytes consumed */
long npm_inflate(const uint8_t *in, size_t inlen, uint8_t *out, size_t outlen, size_t *used) {
    inf_state s;
    memset(&s, 0, sizeof s);
    s.in = in;
    s.inlen = inlen;
    s.out = out;
    s.outlen = outlen;
    int last, r;
    do {
        last = inf_bits(&s, 1);
        int type = inf_bits(&s, 2);
        if (s.err) return -1;
        r = type == 0 ? inf_stored(&s) : type == 1 ? inf_fixed(&s) : type == 2 ? inf_dynamic(&s) : -1;
        if (r) return r;
    } while (!last);
    if (used) *used = s.inpos;
    return (long)s.outpos;
}

/* ================================================================ CRC-32 (gzip) */
uint32_t npm_crc32(const uint8_t *p, size_t n) {
    static uint32_t table[256];
    if (!table[1]) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
    }
    uint32_t c = 0xFFFFFFFFu;
    while (n--) c = table[(c ^ *p++) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

static uint32_t rd32le(const uint8_t *p) {
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* size of the unpacked data from the gzip trailer (mod 4 GB), or -1 if it isn't gzip */
long npm_gzip_size(const uint8_t *in, size_t n) {
    if (n < 18 || in[0] != 0x1F || in[1] != 0x8B || in[2] != 8) return -1;
    return (long)rd32le(in + n - 4);
}

/* gzip -> out (at least npm_gzip_size bytes); returns the length, or
 * -1 not gzip / broken, -2 too big, -3 CRC mismatch */
long npm_gunzip(const uint8_t *in, size_t n, uint8_t *out, size_t outlen) {
    if (npm_gzip_size(in, n) < 0) return -1;
    int flags = in[3];
    size_t p = 10;
    if (flags & 4) {                            /* FEXTRA */
        if (p + 2 > n) return -1;
        p += 2 + (in[p] | (size_t)in[p + 1] << 8);
    }
    if (flags & 8) while (p < n && in[p++]) {}  /* FNAME */
    if (flags & 16) while (p < n && in[p++]) {} /* FCOMMENT */
    if (flags & 2) p += 2;                      /* FHCRC */
    if (p >= n) return -1;
    size_t used = 0;
    long len = npm_inflate(in + p, n - p, out, outlen, &used);
    if (len < 0) return len;
    p += used;
    if (p + 8 > n) return -1;
    if (rd32le(in + p) != npm_crc32(out, (size_t)len)) return -3;
    if (rd32le(in + p + 4) != (uint32_t)len) return -1;
    return len;
}

/* ================================================================ SHA-512 */
static const uint64_t k512[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL, 0x3956c25bf348b538ULL,
    0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL, 0xd807aa98a3030242ULL, 0x12835b0145706fbeULL,
    0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL, 0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL,
    0xc19bf174cf692694ULL, 0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL, 0x983e5152ee66dfabULL,
    0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL, 0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
    0x06ca6351e003826fULL, 0x142929670a0e6e70ULL, 0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL,
    0x53380d139d95b3dfULL, 0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL, 0xd192e819d6ef5218ULL,
    0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL, 0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL,
    0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL, 0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL,
    0x682e6ff3d6b2b8a3ULL, 0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL, 0xca273eceea26619cULL,
    0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL, 0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL,
    0x113f9804bef90daeULL, 0x1b710b35131c471bULL, 0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL,
    0x431d67c49c100d4cULL, 0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL,
};

#define ROR64(x, n) (((x) >> (n)) | ((x) << (64 - (n))))

static void sha512_block(uint64_t h[8], const uint8_t *p) {
    uint64_t w[80], a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 16; i++) {
        uint64_t v = 0;
        for (int k = 0; k < 8; k++) v = v << 8 | p[i * 8 + k];
        w[i] = v;
    }
    for (int i = 16; i < 80; i++) {
        uint64_t s0 = ROR64(w[i - 15], 1) ^ ROR64(w[i - 15], 8) ^ (w[i - 15] >> 7);
        uint64_t s1 = ROR64(w[i - 2], 19) ^ ROR64(w[i - 2], 61) ^ (w[i - 2] >> 6);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    for (int i = 0; i < 80; i++) {
        uint64_t t1 = hh + (ROR64(e, 14) ^ ROR64(e, 18) ^ ROR64(e, 41)) + ((e & f) ^ (~e & g)) + k512[i] + w[i];
        uint64_t t2 = (ROR64(a, 28) ^ ROR64(a, 34) ^ ROR64(a, 39)) + ((a & b) ^ (a & c) ^ (b & c));
        hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

void npm_sha512(const uint8_t *data, size_t n, uint8_t out[64]) {
    uint64_t h[8] = { 0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
                      0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL };
    size_t i = 0;
    for (; i + 128 <= n; i += 128) sha512_block(h, data + i);
    uint8_t last[256];
    size_t rest = n - i, total = rest + 17 <= 128 ? 128 : 256;
    memset(last, 0, sizeof last);
    memcpy(last, data + i, rest);
    last[rest] = 0x80;
    uint64_t bits = (uint64_t)n * 8;
    for (int k = 0; k < 8; k++) last[total - 1 - k] = (uint8_t)(bits >> (8 * k));
    sha512_block(h, last);
    if (total == 256) sha512_block(h, last + 128);
    for (int j = 0; j < 8; j++)
        for (int k = 0; k < 8; k++) out[j * 8 + k] = (uint8_t)(h[j] >> (56 - 8 * k));
}

/* ================================================================ SHA-1 */
#define ROL32(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

static void sha1_block(uint32_t h[5], const uint8_t *p) {
    uint32_t w[80], a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
    for (int i = 16; i < 80; i++) w[i] = ROL32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
        else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
        else { f = b ^ c ^ d; k = 0xCA62C1D6; }
        uint32_t t = ROL32(a, 5) + f + e + k + w[i];
        e = d; d = c; c = ROL32(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}

void npm_sha1(const uint8_t *data, size_t n, uint8_t out[20]) {
    uint32_t h[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
    size_t i = 0;
    for (; i + 64 <= n; i += 64) sha1_block(h, data + i);
    uint8_t last[128];
    size_t rest = n - i, total = rest + 9 <= 64 ? 64 : 128;
    memset(last, 0, sizeof last);
    memcpy(last, data + i, rest);
    last[rest] = 0x80;
    uint64_t bits = (uint64_t)n * 8;
    for (int k = 0; k < 8; k++) last[total - 1 - k] = (uint8_t)(bits >> (8 * k));
    sha1_block(h, last);
    if (total == 128) sha1_block(h, last + 64);
    for (int j = 0; j < 5; j++)
        for (int k = 0; k < 4; k++) out[j * 4 + k] = (uint8_t)(h[j] >> (24 - 8 * k));
}

/* ================================================================ JSON scanning */
#define JNONE ((size_t)-1)

static size_t js_ws(const char *s, size_t n, size_t i) {
    while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) i++;
    return i;
}

/* the index just after the value starting at i (after spaces), or JNONE */
size_t npm_json_skip(const char *s, size_t n, size_t i) {
    i = js_ws(s, n, i);
    if (i >= n) return JNONE;
    if (s[i] == '{' || s[i] == '[') {
        int depth = 0;
        for (; i < n; i++) {
            char ch = s[i];
            if (ch == '"') {
                for (i++; i < n && s[i] != '"'; i++)
                    if (s[i] == '\\') i++;
                if (i >= n) return JNONE;
            } else if (ch == '{' || ch == '[') {
                depth++;
            } else if (ch == '}' || ch == ']') {
                if (--depth == 0) return i + 1;
            }
        }
        return JNONE;
    }
    if (s[i] == '"') {
        for (i++; i < n; i++) {
            if (s[i] == '\\') i++;
            else if (s[i] == '"') return i + 1;
        }
        return JNONE;
    }
    size_t start = i;
    while (i < n && !strchr(",}] \t\r\n", s[i])) i++;
    return i > start ? i : JNONE;
}

/* walk the members of the object at i: calls fn(key, keylen, value index, arg) for
 * each until it returns nonzero (that value's index is returned), else JNONE.
 * Keys are compared raw (no escapes), which is fine for package names and versions. */
size_t npm_json_members(const char *s, size_t n, size_t i,
                        int (*fn)(const char *key, size_t len, size_t value, void *arg), void *arg) {
    i = js_ws(s, n, i);
    if (i >= n || s[i] != '{') return JNONE;
    i = js_ws(s, n, i + 1);
    if (i < n && s[i] == '}') return JNONE;
    while (i < n && s[i] == '"') {
        size_t end = npm_json_skip(s, n, i);
        if (end == JNONE) return JNONE;
        size_t key = i + 1, klen = end - 1 - key;
        i = js_ws(s, n, end);
        if (i >= n || s[i] != ':') return JNONE;
        size_t value = js_ws(s, n, i + 1);
        if (fn(s + key, klen, value, arg)) return value;
        i = npm_json_skip(s, n, value);
        if (i == JNONE) return JNONE;
        i = js_ws(s, n, i);
        if (i < n && s[i] == ',') i = js_ws(s, n, i + 1);
        else break;
    }
    return JNONE;
}

static int js_key_is(const char *key, size_t len, size_t value, void *arg) {
    (void)value;
    return strlen((const char *)arg) == len && memcmp(key, arg, len) == 0;
}

/* the index of member 'key' of the object at i, or JNONE */
size_t npm_json_get(const char *s, size_t n, size_t i, const char *key) {
    return npm_json_members(s, n, i, js_key_is, (void *)key);
}
