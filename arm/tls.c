// tls.c - a small TLS client, enough to download over HTTPS.
//
// TLS 1.3 (RFC 8446) with TLS_CHACHA20_POLY1305_SHA256, and TLS 1.2 (RFC 5246)
// with ECDHE-RSA/ECDSA-CHACHA20-POLY1305 (RFC 7905) for servers that don't
// speak 1.3 yet (npm, micropython.org). Key exchange X25519 in both. All the
// cryptography is Monocypher's, like the SSH server's. One connection at a time.
//
// The server's certificate is NOT checked: MyOS has no RSA/ECDSA code or
// root certificates. That's fine for 'pkg' because what matters is signed:
// the package list carries an Ed25519 signature checked against a key built
// into MyOS (like apt's signed Release files), and every package has its
// SHA-256 in that list. TLS here only gets the bytes past servers that
// refuse plain HTTP; it doesn't vouch for them.
#include <string.h>
#include "monocypher.h"
#include "rng.h"
#include "sha256.h"
#include "tls.h"

#define REC_MAX (16384 + 256)               // biggest record body (ciphertext)
enum { CT_CCS = 20, CT_ALERT = 21, CT_HANDSHAKE = 22, CT_APP = 23 };
enum { HS_CLIENT_HELLO = 1, HS_SERVER_HELLO = 2, HS_ENCRYPTED_EXT = 8, HS_CERTIFICATE = 11,
       HS_SERVER_KEY_EXCHANGE = 12, HS_CERT_REQUEST = 13, HS_SERVER_HELLO_DONE = 14,
       HS_CERT_VERIFY = 15, HS_CLIENT_KEY_EXCHANGE = 16, HS_FINISHED = 20, HS_CERT_STATUS = 22 };

static int tls12;                           // 1 = the server chose TLS 1.2
static int expect_ccs;                      // TLS 1.2: the server's ChangeCipherSpec turns decryption on
static uint8_t client_random[32], server_random[32];

static const tls_io_t *io;
static uint8_t rec[5 + REC_MAX];            // the record being read
static uint8_t out[5 + REC_MAX + 32];       // the record being written
static uint8_t hs[24 * 1024];               // handshake bytes not parsed yet
static uint32_t hs_len;
static sha256_ctx transcript;               // hash of all handshake messages so far
static uint8_t c_key[32], c_iv[12], s_key[32], s_iv[12];
static uint64_t c_seq, s_seq;
static int enc_in, enc_out;
static uint8_t *app;                        // decrypted application data not handed out yet
static uint32_t app_len;
static int closed;

// ---------------------------------------------------------------- HMAC / HKDF (SHA-256)
static void hmac(const uint8_t *key, size_t klen, const uint8_t *msg, size_t mlen, uint8_t mac[32]) {
    uint8_t k[64] = { 0 }, pad[64], inner[32];
    if (klen > 64) sha256(key, klen, k);
    else memcpy(k, key, klen);
    sha256_ctx c;
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x36;
    sha256_init(&c);
    sha256_update(&c, pad, 64);
    sha256_update(&c, msg, mlen);
    sha256_final(&c, inner);
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x5C;
    sha256_init(&c);
    sha256_update(&c, pad, 64);
    sha256_update(&c, inner, 32);
    sha256_final(&c, mac);
    crypto_wipe(k, sizeof k);
}

// HKDF-Expand-Label(secret, label, context) -> n <= 32 bytes
static void expand_label(const uint8_t secret[32], const char *label, const uint8_t *ctx, size_t ctx_len,
                         uint8_t *result, size_t n) {
    uint8_t info[2 + 1 + 6 + 32 + 1 + 32 + 1], t[32];
    size_t ll = strlen(label), i = 0;
    info[i++] = (uint8_t)(n >> 8);
    info[i++] = (uint8_t)n;
    info[i++] = (uint8_t)(6 + ll);
    memcpy(info + i, "tls13 ", 6);
    i += 6;
    memcpy(info + i, label, ll);
    i += ll;
    info[i++] = (uint8_t)ctx_len;
    memcpy(info + i, ctx, ctx_len);
    i += ctx_len;
    info[i++] = 1;                          // the first (and only) HKDF block
    hmac(secret, 32, info, i, t);
    memcpy(result, t, n);
}

static void transcript_hash(uint8_t h[32]) {
    sha256_ctx copy = transcript;           // keep the running hash going
    sha256_final(&copy, h);
}

static void traffic_keys(const uint8_t secret[32], uint8_t key[32], uint8_t iv[12]) {
    expand_label(secret, "key", 0, 0, key, 32);
    expand_label(secret, "iv", 0, 0, iv, 12);
}

// TLS 1.2 PRF (RFC 5246 5): P_SHA256(secret, label + seed) -> n bytes
static void prf(const uint8_t *secret, size_t slen, const char *label, const uint8_t *seed, size_t seed_len,
                uint8_t *result, size_t n) {
    uint8_t ls[96], a[32], buf[32 + 96], t[32];
    size_t ll = strlen(label), lsn = ll + seed_len;
    memcpy(ls, label, ll);
    memcpy(ls + ll, seed, seed_len);
    hmac(secret, slen, ls, lsn, a);                         // A(1)
    while (n) {
        memcpy(buf, a, 32);
        memcpy(buf + 32, ls, lsn);
        hmac(secret, slen, buf, 32 + lsn, t);
        size_t take = n < 32 ? n : 32;
        memcpy(result, t, take);
        result += take;
        n -= take;
        hmac(secret, slen, a, 32, a);                       // A(i+1)
    }
    crypto_wipe(t, sizeof t);
}

static void finished_mac(const uint8_t secret[32], const uint8_t hash[32], uint8_t mac[32]) {
    uint8_t fk[32];
    expand_label(secret, "finished", 0, 0, fk, 32);
    hmac(fk, 32, hash, 32, mac);
    crypto_wipe(fk, sizeof fk);
}

// ---------------------------------------------------------------- records
static void nonce_for(const uint8_t iv[12], uint64_t seq, uint8_t nonce[12]) {
    memcpy(nonce, iv, 12);
    for (int i = 0; i < 8; i++) nonce[11 - i] ^= (uint8_t)(seq >> (8 * i));
}

static int send_record(uint8_t type, const uint8_t *data, uint32_t n) {
    if (n > 16384) return TLS_ERR_PROTOCOL;
    if (!enc_out) {                         // the ClientHello (1.2: ClientKeyExchange): in the clear
        out[0] = type;
        out[1] = 3;
        out[2] = tls12 ? 3 : 1;
        out[3] = (uint8_t)(n >> 8);
        out[4] = (uint8_t)n;
        memcpy(out + 5, data, n);
        return io->write(out, 5 + n) < 0 ? TLS_ERR_IO : 0;
    }
    uint8_t nonce[12];
    crypto_aead_ctx ctx;
    if (tls12) {                            // RFC 7905: real type in the header, no padding,
        uint8_t aad[13];                    // AAD = seq, type, version, plaintext length
        for (int i = 0; i < 8; i++) aad[i] = (uint8_t)(c_seq >> (56 - 8 * i));
        aad[8] = type;
        aad[9] = 3;
        aad[10] = 3;
        aad[11] = (uint8_t)(n >> 8);
        aad[12] = (uint8_t)n;
        out[0] = type;
        out[1] = 3;
        out[2] = 3;
        out[3] = (uint8_t)((n + 16) >> 8);
        out[4] = (uint8_t)(n + 16);
        memcpy(out + 5, data, n);
        nonce_for(c_iv, c_seq++, nonce);
        crypto_aead_init_ietf(&ctx, c_key, nonce);
        crypto_aead_write(&ctx, out + 5, out + 5 + n, aad, 13, out + 5, n);
        crypto_wipe(&ctx, sizeof ctx);
        return io->write(out, 5 + n + 16) < 0 ? TLS_ERR_IO : 0;
    }
    uint32_t len = n + 1 + 16;              // content, its real type, the tag
    out[0] = CT_APP;                        // everything encrypted looks like app data
    out[1] = 3;
    out[2] = 3;
    out[3] = (uint8_t)(len >> 8);
    out[4] = (uint8_t)len;
    memcpy(out + 5, data, n);
    out[5 + n] = type;
    nonce_for(c_iv, c_seq++, nonce);
    crypto_aead_init_ietf(&ctx, c_key, nonce);  // a fresh context per record = RFC 8439 AEAD
    crypto_aead_write(&ctx, out + 5, out + 5 + n + 1, out, 5, out + 5, n + 1);
    crypto_wipe(&ctx, sizeof ctx);
    return io->write(out, 5 + len) < 0 ? TLS_ERR_IO : 0;
}

// the next record; *type is its real content type
static int read_record(uint8_t *type, uint8_t **data, uint32_t *n) {
    if (io->read(rec, 5) < 0) return TLS_ERR_IO;
    uint32_t len = (uint32_t)rec[3] << 8 | rec[4];
    if (len > REC_MAX) return TLS_ERR_PROTOCOL;
    if (len && io->read(rec + 5, len) < 0) return TLS_ERR_IO;
    if (rec[0] == CT_CCS) {                 // "change cipher spec": 1.3 compatibility noise;
        if (expect_ccs) {                   // in 1.2 everything after it is encrypted
            enc_in = 1;
            s_seq = 0;
            expect_ccs = 0;
        }
        *type = CT_CCS;
        *n = 0;
        return 0;
    }
    if (tls12 && enc_in) {
        if (len < 16) return TLS_ERR_PROTOCOL;
        uint32_t m = len - 16;
        uint8_t aad[13], nonce[12];
        for (int i = 0; i < 8; i++) aad[i] = (uint8_t)(s_seq >> (56 - 8 * i));
        aad[8] = rec[0];
        aad[9] = 3;
        aad[10] = 3;
        aad[11] = (uint8_t)(m >> 8);
        aad[12] = (uint8_t)m;
        nonce_for(s_iv, s_seq++, nonce);
        crypto_aead_ctx ctx;
        crypto_aead_init_ietf(&ctx, s_key, nonce);
        int bad = crypto_aead_read(&ctx, rec + 5, rec + 5 + m, aad, 13, rec + 5, m);
        crypto_wipe(&ctx, sizeof ctx);
        if (bad) return TLS_ERR_CIPHER;
        *type = rec[0];
        *data = rec + 5;
        *n = m;
        return 0;
    }
    if (!enc_in || rec[0] != CT_APP) {
        if (enc_in && rec[0] != CT_ALERT) return TLS_ERR_PROTOCOL;
        *type = rec[0];
        *data = rec + 5;
        *n = len;
        return 0;
    }
    if (len < 17) return TLS_ERR_PROTOCOL;
    uint8_t nonce[12];
    nonce_for(s_iv, s_seq++, nonce);
    crypto_aead_ctx ctx;
    crypto_aead_init_ietf(&ctx, s_key, nonce);
    int bad = crypto_aead_read(&ctx, rec + 5, rec + 5 + len - 16, rec, 5, rec + 5, len - 16);
    crypto_wipe(&ctx, sizeof ctx);
    if (bad) return TLS_ERR_CIPHER;
    uint32_t m = len - 16;
    while (m && rec[5 + m - 1] == 0) m--;   // padding
    if (!m) return TLS_ERR_PROTOCOL;
    *type = rec[5 + m - 1];
    *data = rec + 5;
    *n = m - 1;
    return 0;
}

// the next whole handshake message (4-byte header + body) -> its length
static int next_handshake(uint8_t **msg, uint32_t *msg_len) {
    for (;;) {
        if (hs_len >= 4) {
            uint32_t body = (uint32_t)hs[1] << 16 | (uint32_t)hs[2] << 8 | hs[3];
            if (4 + body > sizeof hs) return TLS_ERR_PROTOCOL;
            if (hs_len >= 4 + body) {
                *msg = hs;
                *msg_len = 4 + body;
                return 0;
            }
        }
        uint8_t type, *data;
        uint32_t n;
        int r = read_record(&type, &data, &n);
        if (r) return r;
        if (type == CT_CCS) continue;
        if (type == CT_ALERT) return TLS_ERR_ALERT;
        if (type != CT_HANDSHAKE || hs_len + n > sizeof hs) return TLS_ERR_PROTOCOL;
        memcpy(hs + hs_len, data, n);
        hs_len += n;
    }
}

static void consume_handshake(uint32_t n) {
    sha256_update(&transcript, hs, n);
    memmove(hs, hs + n, hs_len - n);
    hs_len -= n;
}

// ---------------------------------------------------------------- the handshake
static uint8_t *put16(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
    return p + 2;
}

static uint32_t client_hello(uint8_t *b, const char *host, const uint8_t pub[32]) {
    static const uint16_t sigs[] = { 0x0403, 0x0804, 0x0401, 0x0503, 0x0805, 0x0501, 0x0806, 0x0601, 0x0807 };
    size_t hl = strlen(host);
    uint8_t *p = b + 4;
    p = put16(p, 0x0303);                   // legacy version "TLS 1.2"
    rng_bytes(client_random, 32);           // random
    memcpy(p, client_random, 32);
    p += 32;
    *p++ = 32;                              // a session id (middlebox compatibility)
    rng_bytes(p, 32);
    p += 32;
    p = put16(p, 6);
    p = put16(p, 0x1303);                   // TLS 1.3: TLS_CHACHA20_POLY1305_SHA256
    p = put16(p, 0xCCA9);                   // TLS 1.2: ECDHE-ECDSA-CHACHA20-POLY1305
    p = put16(p, 0xCCA8);                   //          ECDHE-RSA-CHACHA20-POLY1305
    *p++ = 1;
    *p++ = 0;                               // no compression
    uint8_t *ext = p;
    p += 2;
    p = put16(p, 0x0000);                   // server_name
    p = put16(p, (uint32_t)hl + 5);
    p = put16(p, (uint32_t)hl + 3);
    *p++ = 0;
    p = put16(p, (uint32_t)hl);
    memcpy(p, host, hl);
    p += hl;
    p = put16(p, 0x000A);                   // supported_groups: x25519
    p = put16(p, 4);
    p = put16(p, 2);
    p = put16(p, 0x001D);
    p = put16(p, 0x000D);                   // signature_algorithms (we don't check, but must ask)
    p = put16(p, 2 + 2 * (uint32_t)(sizeof sigs / sizeof sigs[0]));
    p = put16(p, 2 * (uint32_t)(sizeof sigs / sizeof sigs[0]));
    for (unsigned i = 0; i < sizeof sigs / sizeof sigs[0]; i++) p = put16(p, sigs[i]);
    p = put16(p, 0x000B);                   // ec_point_formats: uncompressed (TLS 1.2 ECDHE)
    p = put16(p, 2);
    *p++ = 1;
    *p++ = 0;
    p = put16(p, 0xFF01);                   // renegotiation_info: empty (TLS 1.2 servers want it)
    p = put16(p, 1);
    *p++ = 0;
    p = put16(p, 0x002B);                   // supported_versions: TLS 1.3, 1.2
    p = put16(p, 5);
    *p++ = 4;
    p = put16(p, 0x0304);
    p = put16(p, 0x0303);
    p = put16(p, 0x002D);                   // psk_key_exchange_modes: psk_dhe_ke
    p = put16(p, 2);
    *p++ = 1;
    *p++ = 1;
    p = put16(p, 0x0033);                   // key_share: our X25519 public key
    p = put16(p, 2 + 4 + 32);
    p = put16(p, 4 + 32);
    p = put16(p, 0x001D);
    p = put16(p, 32);
    memcpy(p, pub, 32);
    p += 32;
    put16(ext, (uint32_t)(p - ext - 2));
    uint32_t total = (uint32_t)(p - b);
    b[0] = HS_CLIENT_HELLO;
    b[1] = (uint8_t)((total - 4) >> 16);
    b[2] = (uint8_t)((total - 4) >> 8);
    b[3] = (uint8_t)(total - 4);
    return total;
}

// ServerHello -> 13 (TLS 1.3, server_pub = its X25519 key), 12 (TLS 1.2 with one of
// our ChaCha20 suites), or 0 if it isn't what we asked for
static int server_hello(const uint8_t *m, uint32_t n, uint8_t server_pub[32]) {
    static const uint8_t hrr[32] = {        // "HelloRetryRequest" marker random
        0xCF, 0x21, 0xAD, 0x74, 0xE5, 0x9A, 0x61, 0x11, 0xBE, 0x1D, 0x8C, 0x02, 0x1E, 0x65, 0xB8, 0x91,
        0xC2, 0xA2, 0x11, 0x16, 0x7A, 0xBB, 0x8C, 0x5E, 0x07, 0x9E, 0x09, 0xE2, 0xC8, 0xA8, 0x33, 0x9C };
    static const uint8_t downgrade[8] = { 'D', 'O', 'W', 'N', 'G', 'R', 'D', 1 };
    if (n < 4 + 2 + 32 + 1 || m[0] != HS_SERVER_HELLO) return 0;
    const uint8_t *p = m + 4, *end = m + n;
    uint32_t legacy = (uint32_t)p[0] << 8 | p[1];
    p += 2;
    if (memcmp(p, hrr, 32) == 0) return 0;
    memcpy(server_random, p, 32);
    p += 32;
    if (p + 1 + p[0] > end) return 0;
    p += 1 + p[0];                          // session id echo
    if (p + 3 > end || p[2] != 0) return 0;
    uint32_t suite = (uint32_t)p[0] << 8 | p[1];
    p += 3;                                 // cipher + compression
    int v13 = 0, key_ok = 0;
    if (p + 2 <= end) {                     // (a TLS 1.2 server may send no extensions)
        uint32_t ext_len = (uint32_t)p[0] << 8 | p[1];
        p += 2;
        if (p + ext_len > end) return 0;
        const uint8_t *e = p + ext_len;
        while (p + 4 <= e) {
            uint32_t type = (uint32_t)p[0] << 8 | p[1], len = (uint32_t)p[2] << 8 | p[3];
            const uint8_t *d = p + 4;
            if (d + len > e) return 0;
            if (type == 0x002B && len == 2 && d[0] == 3 && d[1] == 4) v13 = 1;
            if (type == 0x0033 && len == 36 && d[0] == 0x00 && d[1] == 0x1D && d[2] == 0 && d[3] == 32) {
                memcpy(server_pub, d + 4, 32);
                key_ok = 1;
            }
            p = d + len;
        }
    }
    if (v13) return suite == 0x1303 && key_ok ? 13 : 0;
    if (legacy != 0x0303 || (suite != 0xCCA8 && suite != 0xCCA9)) return 0;
    // a server that speaks 1.3 marks its random when it answers 1.2: someone in
    // between hid our 1.3 offer (RFC 8446 4.1.3)
    if (memcmp(server_random + 24, downgrade, 8) == 0) return 0;
    return 12;
}

// the rest of a TLS 1.2 handshake (RFC 5246 7.3), after the ServerHello:
// Certificate, ServerKeyExchange (X25519), ServerHelloDone; we answer with
// ClientKeyExchange, ChangeCipherSpec, Finished, and check the server's Finished.
static int handshake12(uint8_t secret[32]) {
    uint8_t server_pub[32], pub[32], shared[32], master[48], kb[88], h[32];
    uint8_t *msg;
    uint32_t len;
    int r, have_key = 0;
    tls12 = 1;
    for (;;) {
        if ((r = next_handshake(&msg, &len))) return r;
        uint8_t type = msg[0];
        if (type == HS_SERVER_KEY_EXCHANGE) {
            // curve type 3 (named), x25519, 32-byte key, then a signature we don't check
            if (len < 4 + 4 + 32 || msg[4] != 3 || msg[5] != 0x00 || msg[6] != 0x1D || msg[7] != 32)
                return TLS_ERR_PROTOCOL;
            memcpy(server_pub, msg + 8, 32);
            have_key = 1;
        } else if (type == HS_SERVER_HELLO_DONE) {
            consume_handshake(len);
            break;
        } else if (type != HS_CERTIFICATE && type != HS_CERT_STATUS) {
            return TLS_ERR_PROTOCOL;                        // (incl. asking for a client certificate)
        }
        consume_handshake(len);
    }
    if (!have_key) return TLS_ERR_PROTOCOL;
    crypto_x25519_public_key(pub, secret);
    crypto_x25519(shared, secret, server_pub);
    crypto_wipe(secret, 32);
    uint8_t any = 0;
    for (int i = 0; i < 32; i++) any |= shared[i];
    if (!any) return TLS_ERR_PROTOCOL;

    uint8_t cke[4 + 1 + 32] = { HS_CLIENT_KEY_EXCHANGE, 0, 0, 33, 32 };
    memcpy(cke + 5, pub, 32);
    sha256_update(&transcript, cke, sizeof cke);
    if ((r = send_record(CT_HANDSHAKE, cke, sizeof cke))) return r;

    uint8_t seed[64];
    memcpy(seed, client_random, 32);
    memcpy(seed + 32, server_random, 32);
    prf(shared, 32, "master secret", seed, 64, master, 48);
    crypto_wipe(shared, sizeof shared);
    memcpy(seed, server_random, 32);
    memcpy(seed + 32, client_random, 32);
    prf(master, 48, "key expansion", seed, 64, kb, sizeof kb);
    memcpy(c_key, kb, 32);                                  // no MAC keys with an AEAD
    memcpy(s_key, kb + 32, 32);
    memcpy(c_iv, kb + 64, 12);
    memcpy(s_iv, kb + 76, 12);
    crypto_wipe(kb, sizeof kb);

    static const uint8_t ccs[6] = { CT_CCS, 3, 3, 0, 1, 1 };
    if (io->write(ccs, sizeof ccs) < 0) return TLS_ERR_IO;
    enc_out = 1;
    c_seq = 0;
    uint8_t fin[4 + 12] = { HS_FINISHED, 0, 0, 12 };
    transcript_hash(h);
    prf(master, 48, "client finished", h, 32, fin + 4, 12);
    sha256_update(&transcript, fin, sizeof fin);
    if ((r = send_record(CT_HANDSHAKE, fin, sizeof fin))) return r;

    expect_ccs = 1;                                         // read_record turns decryption on
    if ((r = next_handshake(&msg, &len))) return r;
    if (!enc_in || msg[0] != HS_FINISHED || len != 4 + 12) return TLS_ERR_PROTOCOL;
    uint8_t expect[12], diff = 0;
    transcript_hash(h);
    prf(master, 48, "server finished", h, 32, expect, 12);
    crypto_wipe(master, sizeof master);
    for (int i = 0; i < 12; i++) diff |= expect[i] ^ msg[4 + i];
    if (diff) return TLS_ERR_PROTOCOL;
    consume_handshake(len);
    return 0;
}

int tls_connect(const tls_io_t *io_, const char *host) {
    io = io_;
    hs_len = 0;
    app_len = 0;
    closed = 0;
    enc_in = enc_out = 0;
    tls12 = expect_ccs = 0;
    c_seq = s_seq = 0;
    sha256_init(&transcript);

    uint8_t secret[32], pub[32], server_pub[32], shared[32];
    rng_bytes(secret, 32);
    crypto_x25519_public_key(pub, secret);
    uint8_t ch[512];
    uint32_t ch_len = client_hello(ch, host, pub);
    sha256_update(&transcript, ch, ch_len);
    int r = send_record(CT_HANDSHAKE, ch, ch_len);
    if (r) return r;

    uint8_t *msg;
    uint32_t len;
    if ((r = next_handshake(&msg, &len))) return r;
    int version = server_hello(msg, len, server_pub);
    if (!version) return TLS_ERR_PROTOCOL;
    consume_handshake(len);
    if (version == 12) return handshake12(secret);
    crypto_x25519(shared, secret, server_pub);
    crypto_wipe(secret, sizeof secret);
    uint8_t any = 0;
    for (int i = 0; i < 32; i++) any |= shared[i];
    if (!any) return TLS_ERR_PROTOCOL;

    // key schedule (RFC 8446 7.1)
    static const uint8_t zero[32] = { 0 };
    uint8_t empty_hash[32], early[32], derived[32], hs_secret[32], h[32];
    uint8_t c_hs[32], s_hs[32], master[32], c_ap[32], s_ap[32];
    sha256("", 0, empty_hash);
    hmac(zero, 32, zero, 32, early);
    expand_label(early, "derived", empty_hash, 32, derived, 32);
    hmac(derived, 32, shared, 32, hs_secret);
    crypto_wipe(shared, sizeof shared);
    transcript_hash(h);
    expand_label(hs_secret, "c hs traffic", h, 32, c_hs, 32);
    expand_label(hs_secret, "s hs traffic", h, 32, s_hs, 32);
    traffic_keys(c_hs, c_key, c_iv);
    traffic_keys(s_hs, s_key, s_iv);
    enc_in = enc_out = 1;

    // EncryptedExtensions, Certificate, CertificateVerify, then the server's Finished
    for (;;) {
        if ((r = next_handshake(&msg, &len))) return r;
        uint8_t type = msg[0];
        if (type == HS_FINISHED) {
            uint8_t expect[32];
            transcript_hash(h);
            finished_mac(s_hs, h, expect);
            if (len != 4 + 32 || crypto_verify32(expect, msg + 4) != 0) return TLS_ERR_PROTOCOL;
            consume_handshake(len);
            break;
        }
        if (type == HS_CERT_REQUEST) return TLS_ERR_PROTOCOL;   // a client certificate: no
        if (type != HS_ENCRYPTED_EXT && type != HS_CERTIFICATE && type != HS_CERT_VERIFY)
            return TLS_ERR_PROTOCOL;
        consume_handshake(len);                                 // (the certificate isn't checked)
    }

    // application secrets come from the transcript up to the server's Finished
    transcript_hash(h);
    expand_label(hs_secret, "derived", empty_hash, 32, derived, 32);
    hmac(derived, 32, zero, 32, master);
    expand_label(master, "c ap traffic", h, 32, c_ap, 32);
    expand_label(master, "s ap traffic", h, 32, s_ap, 32);

    static const uint8_t ccs[6] = { CT_CCS, 3, 3, 0, 1, 1 };   // compatibility, in the clear
    if (io->write(ccs, sizeof ccs) < 0) return TLS_ERR_IO;
    uint8_t fin[4 + 32] = { HS_FINISHED, 0, 0, 32 };
    finished_mac(c_hs, h, fin + 4);
    if ((r = send_record(CT_HANDSHAKE, fin, sizeof fin))) return r;

    traffic_keys(c_ap, c_key, c_iv);
    traffic_keys(s_ap, s_key, s_iv);
    c_seq = s_seq = 0;
    crypto_wipe(c_hs, sizeof c_hs);
    crypto_wipe(s_hs, sizeof s_hs);
    crypto_wipe(master, sizeof master);
    crypto_wipe(hs_secret, sizeof hs_secret);
    return 0;
}

int tls_write(const uint8_t *data, uint32_t n) {
    while (n) {
        uint32_t take = n < 16384 ? n : 16384;
        int r = send_record(CT_APP, data, take);
        if (r) return r;
        data += take;
        n -= take;
    }
    return 0;
}

int tls_read(uint8_t *dst, uint32_t max) {
    while (!app_len) {
        if (closed) return 0;
        uint8_t type, *data;
        uint32_t n;
        int r = read_record(&type, &data, &n);
        if (r == TLS_ERR_IO) return 0;              // the connection ended
        if (r) return r;
        if (type == CT_APP) {
            app = data;
            app_len = n;
        } else if (type == CT_ALERT) {
            closed = 1;                             // close_notify (or a fatal alert)
            return (n >= 2 && data[1] == 0) ? 0 : TLS_ERR_ALERT;
        }
        // CT_HANDSHAKE after the handshake: NewSessionTicket, ignored
    }
    uint32_t take = app_len < max ? app_len : max;
    memcpy(dst, app, take);
    app += take;
    app_len -= take;
    return (int)take;
}

const char *tls_error(int code) {
    switch (code) {
        case TLS_ERR_IO:       return "the connection broke during the secure (TLS) set-up";
        case TLS_ERR_ALERT:    return "the server refused the secure (TLS) connection";
        case TLS_ERR_CIPHER:   return "a secure (TLS) record didn't decrypt";
        default:               return "the server spoke TLS in a way MyOS doesn't understand";
    }
}
