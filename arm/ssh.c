// ssh.c - a small SSH-2 server: the MyOS shell over the USB network.
//
// On the PC:  ssh myos@192.168.7.1      (any user name; password set with 'passwd')
//
// One algorithm of each kind, all supported by OpenSSH (Windows' ssh.exe):
//   key exchange  curve25519-sha256          (RFC 8731)
//   host key      ssh-ed25519                (RFC 8709)
//   cipher + MAC  chacha20-poly1305@openssh.com
//   plus "strict KEX" (kex-strict-s-v00@openssh.com, the Terrapin fix).
// The cryptography comes from Monocypher; SHA-256 is in sha256.c.
// One connection with one shell at a time.
#include <string.h>
#include "console.h"
#include "lwip/tcp.h"
#include "monocypher.h"
#include "monocypher-ed25519.h"
#include "myfs.h"
#include "net.h"
#include "rng.h"
#include "sha256.h"
#include "ssh.h"
#include "layout.h"
#include "timer.h"

#define SSH_PORT        22
#define SERVER_VERSION  "SSH-2.0-MyOS_0.6"
#define HOSTKEY_FILE    sys_file(F_SSH_KEY)        // /etc/ssh/... (layout.h)
#define PASSWORD_FILE   sys_file(F_SSH_PASSWORD)
#define RX_SIZE         (20 * 1024)     // bytes from the client not yet handled
#define MAX_PACKET      (18 * 1024)     // biggest packet accepted
#define OUR_WINDOW      8192            // how much the client may send before we say "more"
#define OUR_MAX_PACKET  4096
#define DATA_CHUNK      2048            // biggest CHANNEL_DATA we send
#define MAX_SSH         3               // connections at the same time
#define LOGIN_TIMEOUT   60000           // ms to log in
#define PW_ROUNDS       4096

enum {
    MSG_DISCONNECT = 1, MSG_IGNORE = 2, MSG_UNIMPLEMENTED = 3, MSG_DEBUG = 4,
    MSG_SERVICE_REQUEST = 5, MSG_SERVICE_ACCEPT = 6,
    MSG_KEXINIT = 20, MSG_NEWKEYS = 21, MSG_KEX_ECDH_INIT = 30, MSG_KEX_ECDH_REPLY = 31,
    MSG_USERAUTH_REQUEST = 50, MSG_USERAUTH_FAILURE = 51, MSG_USERAUTH_SUCCESS = 52,
    MSG_USERAUTH_BANNER = 53,
    MSG_GLOBAL_REQUEST = 80, MSG_REQUEST_FAILURE = 82,
    MSG_CHANNEL_OPEN = 90, MSG_CHANNEL_OPEN_CONFIRMATION = 91, MSG_CHANNEL_OPEN_FAILURE = 92,
    MSG_CHANNEL_WINDOW_ADJUST = 93, MSG_CHANNEL_DATA = 94, MSG_CHANNEL_EOF = 96,
    MSG_CHANNEL_CLOSE = 97, MSG_CHANNEL_REQUEST = 98, MSG_CHANNEL_SUCCESS = 99,
    MSG_CHANNEL_FAILURE = 100,
};
enum { DISC_PROTOCOL_ERROR = 2, DISC_KEX_FAILED = 3, DISC_MAC_ERROR = 5,
       DISC_SERVICE_NOT_AVAILABLE = 7, DISC_TOO_MANY_AUTH = 12, DISC_BY_APPLICATION = 11 };

// host key (made on first start, kept in MyFS)
static uint8_t host_secret[64], host_public[32];
static uint8_t host_blob[51];           // string "ssh-ed25519" + string key
static int host_ready, host_saved;

static struct tcp_pcb *listener;
static int busy;                        // ssh_poll() is running

// one SSH connection (a session)
typedef struct {
    int version_done;
    char v_c[256];                      // client's version line
    unsigned v_c_len;
    uint8_t i_c[4096];                  // client's KEXINIT (part of the exchange hash)
    unsigned i_c_len;
    uint8_t i_s[512];                   // our KEXINIT
    unsigned i_s_len;
    int sent_kexinit, got_kexinit, keys_ready, first_kex_done, strict;
    uint32_t seq_in, seq_out;
    int enc_in, enc_out;
    uint8_t key_in[64], key_out[64];    // chacha20-poly1305: K_2 (payload) || K_1 (length)
    uint8_t next_in[64], next_out[64];
    uint8_t session_id[32];
    int have_session_id;
    int service_ok, authed, auth_failures;
    char user[32];
    int channel;                        // a session channel is open
    uint32_t peer_channel, send_window, peer_max, recv_window;
    int shell;
    int last_cr;                        // to fold CR LF into one Enter
    uint64_t closing;                   // we said goodbye: drop TCP at this time at the latest
    uint64_t started;
    struct tcp_pcb *client;
    ip_addr_t client_ip;
    uint8_t rx[RX_SIZE];                // bytes from the client not handled yet
    unsigned rx_len;
    unsigned unacked;                   // bytes handled but not yet tcp_recved()
    uint8_t keys[512];                  // what the client typed
    unsigned keys_head, keys_tail;
    char tx[DATA_CHUNK];                // shell output waiting to be sent
    unsigned tx_len;
} session_t;

static session_t sessions[MAX_SSH];
static session_t *s = &sessions[0];     // the session being worked on

// --------------------------------------------------------- byte helpers
static void wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static void nonce_of(uint8_t n[8], uint32_t seq) {      // 64-bit big-endian sequence number
    memset(n, 0, 4);
    wr32(n + 4, seq);
}

// building a message
static uint8_t out[OUR_MAX_PACKET + 64];
static unsigned out_len;
static void put_u8(uint8_t v) { if (out_len < sizeof out) out[out_len++] = v; }
static void put_u32(uint32_t v) { if (out_len + 4 <= sizeof out) { wr32(out + out_len, v); out_len += 4; } }
static void put_bytes(const void *p, unsigned n) {
    if (out_len + n <= sizeof out) { memcpy(out + out_len, p, n); out_len += n; }
}
static void put_string(const void *p, unsigned n) { put_u32(n); put_bytes(p, n); }
static void put_cstr(const char *str) { put_string(str, (unsigned)strlen(str)); }

// reading a message
typedef struct { const uint8_t *p; uint32_t n, pos; int bad; } reader;
static uint8_t get_u8(reader *r) {
    if (r->pos + 1 > r->n) { r->bad = 1; return 0; }
    return r->p[r->pos++];
}
static uint32_t get_u32(reader *r) {
    if (r->pos + 4 > r->n) { r->bad = 1; return 0; }
    uint32_t v = rd32(r->p + r->pos);
    r->pos += 4;
    return v;
}
static const uint8_t *get_string(reader *r, uint32_t *len) {
    uint32_t n = get_u32(r);
    if (r->bad || n > r->n - r->pos) { r->bad = 1; *len = 0; return (const uint8_t *)""; }
    const uint8_t *p = r->p + r->pos;
    r->pos += n;
    *len = n;
    return p;
}
static int str_is(const uint8_t *p, uint32_t n, const char *want) {
    return n == strlen(want) && memcmp(p, want, n) == 0;
}
static int list_has(const uint8_t *list, uint32_t n, const char *want) {   // "a,b,c" contains want?
    uint32_t i = 0;
    while (i <= n) {
        uint32_t j = i;
        while (j < n && list[j] != ',') j++;
        if (str_is(list + i, j - i, want)) return 1;
        i = j + 1;
    }
    return 0;
}

// --------------------------------------------------------- sending
static void drop_client(void);

static int tcp_send_all(const uint8_t *p, unsigned n) {
    uint64_t give_up = 0;
    while (n && s->client) {
        unsigned room = tcp_sndbuf(s->client);
        unsigned take = n < room ? n : room;
        err_t e = ERR_MEM;
        if (take) {
            net_lock();
            e = tcp_write(s->client, p, (u16_t)take, TCP_WRITE_FLAG_COPY);
            if (e == ERR_OK) tcp_output(s->client);
            net_unlock();
        }
        if (e == ERR_OK) {
            p += take;
            n -= take;
            give_up = 0;
        } else if (e == ERR_MEM) {          // wait for the PC to take what we sent
            if (!give_up) give_up = millis() + 10000;
            else if (millis() > give_up) { drop_client(); return 0; }
            console_service();
        } else {
            drop_client();
            return 0;
        }
    }
    return s->client != NULL;
}

static uint8_t pkt[sizeof out + 4 + 1 + 255 + 16];

// wrap the message in out[] into an SSH packet (encrypted once keys are set)
static int send_packet(void) {
    unsigned base = (s->enc_out ? 1 : 5) + out_len;  // the length field isn't counted when encrypted
    unsigned pad = 8 - base % 8;
    if (pad < 4) pad += 8;
    uint32_t len = 1 + out_len + pad;
    wr32(pkt, len);
    pkt[4] = (uint8_t)pad;
    memcpy(pkt + 5, out, out_len);
    rng_bytes(pkt + 5 + out_len, pad);
    unsigned total = 4 + len;
    if (s->enc_out) {
        uint8_t nonce[8], polykey[64], zero[64] = { 0 };
        nonce_of(nonce, s->seq_out);
        crypto_chacha20_djb(pkt, pkt, 4, s->key_out + 32, nonce, 0);         // length: K_1
        crypto_chacha20_djb(polykey, zero, 64, s->key_out, nonce, 0);        // MAC key: K_2 block 0
        crypto_chacha20_djb(pkt + 4, pkt + 4, len, s->key_out, nonce, 1);    // payload: K_2 from block 1
        crypto_poly1305(pkt + total, pkt, total, polykey);
        crypto_wipe(polykey, sizeof polykey);
        total += 16;
    }
    s->seq_out++;
    out_len = 0;
    return tcp_send_all(pkt, total);
}

static void disconnect(uint32_t reason, const char *why) {
    if (!s->client) return;
    out_len = 0;
    put_u8(MSG_DISCONNECT);
    put_u32(reason);
    put_cstr(why);
    put_cstr("");
    send_packet();
    drop_client();
}

// --------------------------------------------------------- host key and password
static void make_host_blob(void) {
    uint8_t *p = host_blob;
    wr32(p, 11); memcpy(p + 4, "ssh-ed25519", 11);
    wr32(p + 15, 32); memcpy(p + 19, host_public, 32);
}

static void load_host_key(void) {
    uint8_t seed[32];
    host_saved = 0;
    if (myfs_mounted() && myfs_size(HOSTKEY_FILE) == 32 &&
        myfs_read(HOSTKEY_FILE, 0, seed, 32) == 32) {
        host_saved = 1;
    } else {
        rng_bytes(seed, 32);                    // first start: make a new key
        if (myfs_mounted() && myfs_write(HOSTKEY_FILE, seed, 32) == 0) host_saved = 1;
    }
    crypto_ed25519_key_pair(host_secret, host_public, seed);   // (wipes seed)
    make_host_blob();
    host_ready = 1;
}

static void hash_password(const uint8_t salt[16], const char *pw, uint8_t out_hash[32]) {
    size_t n = strlen(pw);
    sha256_ctx c;
    sha256_init(&c);
    sha256_update(&c, salt, 16);
    sha256_update(&c, pw, n);
    sha256_final(&c, out_hash);
    for (int i = 1; i < PW_ROUNDS; i++) {       // slow it down a little against guessing
        sha256_init(&c);
        sha256_update(&c, out_hash, 32);
        sha256_update(&c, salt, 16);
        sha256_update(&c, pw, n);
        sha256_final(&c, out_hash);
    }
}

int ssh_password_set(void) {
    return myfs_mounted() && myfs_size(PASSWORD_FILE) == 48;
}

int ssh_set_password(const char *pw) {
    uint8_t rec[48];                            // salt (16) + hash (32)
    rng_bytes(rec, 16);
    hash_password(rec, pw, rec + 16);
    int r = myfs_write(PASSWORD_FILE, rec, sizeof rec);
    crypto_wipe(rec, sizeof rec);
    return r;
}

static int check_password(const uint8_t *pw, uint32_t n) {
    uint8_t rec[48], h[32];
    char buf[128];
    if (n >= sizeof buf || !ssh_password_set()) return 0;
    if (myfs_read(PASSWORD_FILE, 0, rec, 48) != 48) return 0;
    memcpy(buf, pw, n);
    buf[n] = 0;
    hash_password(rec, buf, h);
    int ok = crypto_verify32(h, rec + 16) == 0;
    crypto_wipe(buf, sizeof buf);
    crypto_wipe(h, sizeof h);
    return ok;
}

int ssh_fingerprint(char *outs) {               // "SHA256:..." as OpenSSH shows it
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if (!host_ready) return 0;
    uint8_t h[33];
    sha256(host_blob, sizeof host_blob, h);
    h[32] = 0;
    char *p = outs;
    memcpy(p, "SHA256:", 7);
    p += 7;
    for (int i = 0; i < 32; i += 3) {           // base64 without '=' padding: 43 characters
        uint32_t v = (uint32_t)h[i] << 16 | (uint32_t)h[i + 1] << 8 | (i + 2 < 33 ? h[i + 2] : 0);
        *p++ = b64[(v >> 18) & 63];
        *p++ = b64[(v >> 12) & 63];
        if (i + 1 < 32) *p++ = b64[(v >> 6) & 63];
        if (i + 2 < 32) *p++ = b64[v & 63];
    }
    *p = 0;
    return 1;
}

// --------------------------------------------------------- key exchange
static void send_kexinit(void) {
    out_len = 0;
    put_u8(MSG_KEXINIT);
    uint8_t cookie[16];
    rng_bytes(cookie, 16);
    put_bytes(cookie, 16);
    put_cstr("curve25519-sha256,curve25519-sha256@libssh.org,kex-strict-s-v00@openssh.com");
    put_cstr("ssh-ed25519");
    put_cstr("chacha20-poly1305@openssh.com");
    put_cstr("chacha20-poly1305@openssh.com");
    put_cstr("hmac-sha2-256");                  // not used: the cipher has its own MAC
    put_cstr("hmac-sha2-256");
    put_cstr("none");
    put_cstr("none");
    put_cstr("");
    put_cstr("");
    put_u8(0);                                  // no guessed packet follows
    put_u32(0);
    memcpy(s->i_s, out, out_len);
    s->i_s_len = out_len;
    s->sent_kexinit = 1;
    send_packet();
}

static void handle_kexinit(const uint8_t *msg, uint32_t n, uint32_t seq) {
    if (n > sizeof s->i_c) { disconnect(DISC_PROTOCOL_ERROR, "KEXINIT too big"); return; }
    memcpy(s->i_c, msg, n);
    s->i_c_len = n;
    reader r = { msg, n, 17, 0 };               // after message number and cookie
    uint32_t len[10];
    const uint8_t *list[10];
    for (int i = 0; i < 10; i++) list[i] = get_string(&r, &len[i]);
    if (r.bad) { disconnect(DISC_PROTOCOL_ERROR, "bad KEXINIT"); return; }
    if (!s->first_kex_done && list_has(list[0], len[0], "kex-strict-c-v00@openssh.com")) {
        s->strict = 1;
        if (seq != 0) { disconnect(DISC_PROTOCOL_ERROR, "strict KEX: KEXINIT must come first"); return; }
    }
    if (!(list_has(list[0], len[0], "curve25519-sha256") ||
          list_has(list[0], len[0], "curve25519-sha256@libssh.org")) ||
        !list_has(list[1], len[1], "ssh-ed25519") ||
        !list_has(list[2], len[2], "chacha20-poly1305@openssh.com") ||
        !list_has(list[3], len[3], "chacha20-poly1305@openssh.com") ||
        !list_has(list[6], len[6], "none") || !list_has(list[7], len[7], "none")) {
        disconnect(DISC_KEX_FAILED, "no common algorithms (MyOS offers curve25519-sha256, "
                                    "ssh-ed25519, chacha20-poly1305@openssh.com)");
        return;
    }
    s->got_kexinit = 1;
    if (!s->sent_kexinit) send_kexinit();        // the client started a new key exchange
}

static void hash_string(sha256_ctx *c, const void *p, uint32_t n) {
    uint8_t l[4];
    wr32(l, n);
    sha256_update(c, l, 4);
    sha256_update(c, p, n);
}

static void derive(uint8_t key[64], const uint8_t *k, unsigned k_len, const uint8_t h[32], char letter) {
    sha256_ctx c;                               // K1 = HASH(K || H || letter || session_id)
    sha256_init(&c);
    sha256_update(&c, k, k_len);
    sha256_update(&c, h, 32);
    sha256_update(&c, &letter, 1);
    sha256_update(&c, s->session_id, 32);
    sha256_final(&c, key);
    sha256_init(&c);                            // K2 = HASH(K || H || K1)
    sha256_update(&c, k, k_len);
    sha256_update(&c, h, 32);
    sha256_update(&c, key, 32);
    sha256_final(&c, key + 32);
}

static void handle_ecdh_init(reader *r) {
    uint32_t qc_len;
    const uint8_t *q_c = get_string(r, &qc_len);
    if (r->bad || qc_len != 32 || !s->got_kexinit) { disconnect(DISC_KEX_FAILED, "bad ECDH_INIT"); return; }

    uint8_t secret[32], q_s[32], shared[32];
    rng_bytes(secret, 32);
    crypto_x25519_public_key(q_s, secret);
    crypto_x25519(shared, secret, q_c);
    crypto_wipe(secret, 32);
    uint8_t any = 0;
    for (int i = 0; i < 32; i++) any |= shared[i];
    if (!any) { disconnect(DISC_KEX_FAILED, "bad public key"); return; }

    // K as an SSH mpint (the 32 bytes are a big-endian number)
    uint8_t k[4 + 33];
    unsigned skip = 0;
    while (skip < 32 && shared[skip] == 0) skip++;
    unsigned klen = 32 - skip, lead = (shared[skip] & 0x80) ? 1 : 0;
    wr32(k, klen + lead);
    k[4] = 0;
    memcpy(k + 4 + lead, shared + skip, klen);
    unsigned k_total = 4 + lead + klen;
    crypto_wipe(shared, 32);

    // exchange hash H
    uint8_t h[32];
    sha256_ctx c;
    sha256_init(&c);
    hash_string(&c, s->v_c, s->v_c_len);
    hash_string(&c, SERVER_VERSION, sizeof SERVER_VERSION - 1);
    hash_string(&c, s->i_c, s->i_c_len);
    hash_string(&c, s->i_s, s->i_s_len);
    hash_string(&c, host_blob, sizeof host_blob);
    hash_string(&c, q_c, 32);
    hash_string(&c, q_s, 32);
    sha256_update(&c, k, k_total);
    sha256_final(&c, h);
    if (!s->have_session_id) {
        memcpy(s->session_id, h, 32);
        s->have_session_id = 1;
    }

    uint8_t sig[64];
    crypto_ed25519_sign(sig, host_secret, h, 32);
    out_len = 0;
    put_u8(MSG_KEX_ECDH_REPLY);
    put_string(host_blob, sizeof host_blob);
    put_string(q_s, 32);
    put_u32(4 + 11 + 4 + 64);
    put_cstr("ssh-ed25519");
    put_string(sig, 64);
    if (!send_packet()) return;

    derive(s->next_in, k, k_total, h, 'C');      // client -> server
    derive(s->next_out, k, k_total, h, 'D');     // server -> client
    crypto_wipe(k, sizeof k);
    s->keys_ready = 1;

    out_len = 0;
    put_u8(MSG_NEWKEYS);
    if (!send_packet()) return;
    memcpy(s->key_out, s->next_out, 64);          // everything we send from now on is encrypted
    crypto_wipe(s->next_out, 64);
    s->enc_out = 1;
    if (s->strict) s->seq_out = 0;
}

static void handle_newkeys(void) {
    if (!s->keys_ready) { disconnect(DISC_PROTOCOL_ERROR, "unexpected NEWKEYS"); return; }
    memcpy(s->key_in, s->next_in, 64);
    crypto_wipe(s->next_in, 64);
    s->enc_in = 1;
    if (s->strict) s->seq_in = 0;
    s->keys_ready = s->sent_kexinit = s->got_kexinit = 0;
    s->first_kex_done = 1;
}

static int kex_running(void) {
    return s->sent_kexinit || s->got_kexinit;
}

// --------------------------------------------------------- login and the shell channel
static void wait_ms(unsigned ms) {
    uint64_t end = millis() + ms;
    while (millis() < end && s->client) console_service();
}

static void handle_userauth(reader *r) {
    uint32_t ul, sl, ml;
    const uint8_t *user = get_string(r, &ul);
    const uint8_t *service = get_string(r, &sl);
    const uint8_t *method = get_string(r, &ml);
    if (r->bad || !s->service_ok) { disconnect(DISC_PROTOCOL_ERROR, "bad USERAUTH_REQUEST"); return; }
    if (s->authed) return;
    if (!str_is(service, sl, "ssh-connection")) {
        disconnect(DISC_SERVICE_NOT_AVAILABLE, "unknown service");
        return;
    }
    if (!ssh_password_set()) {
        out_len = 0;
        put_u8(MSG_USERAUTH_BANNER);
        put_cstr("\r\nMyOS has no SSH password yet.\r\n"
                 "Set one first: type 'passwd' in the MyOS shell (COM port or telnet).\r\n\r\n");
        put_cstr("");
        send_packet();
    } else if (str_is(method, ml, "password")) {
        get_u8(r);                              // "change password" flag
        uint32_t pl;
        const uint8_t *pw = get_string(r, &pl);
        if (!r->bad && check_password(pw, pl)) {
            unsigned n = ul < sizeof s->user - 1 ? ul : sizeof s->user - 1;
            memcpy(s->user, user, n);
            s->user[n] = 0;
            s->authed = 1;
            out_len = 0;
            put_u8(MSG_USERAUTH_SUCCESS);
            send_packet();
            return;
        }
        if (++s->auth_failures >= 5) {
            disconnect(DISC_TOO_MANY_AUTH, "too many wrong passwords");
            return;
        }
        wait_ms(1000);                          // slow down guessing
    }
    out_len = 0;
    put_u8(MSG_USERAUTH_FAILURE);
    put_cstr("password");
    put_u8(0);
    send_packet();
}

static void key_put(uint8_t c) {
    int after_cr = s->last_cr;
    s->last_cr = (c == '\r');
    if (after_cr && (c == '\n' || c == 0)) return;  // CR LF / CR NUL = one Enter
    unsigned next = (s->keys_head + 1) % sizeof s->keys;
    if (next != s->keys_tail) {
        s->keys[s->keys_head] = c;
        s->keys_head = next;
    }
}

static void close_channel(void) {
    if (s->channel) {
        out_len = 0;
        put_u8(MSG_CHANNEL_CLOSE);
        put_u32(s->peer_channel);
        send_packet();
    }
    s->channel = s->shell = 0;
}

static void handle_channel_open(reader *r) {
    uint32_t tl;
    const uint8_t *type = get_string(r, &tl);
    uint32_t sender = get_u32(r), window = get_u32(r), maxpkt = get_u32(r);
    if (r->bad || !s->authed) { disconnect(DISC_PROTOCOL_ERROR, "bad CHANNEL_OPEN"); return; }
    out_len = 0;
    if (!str_is(type, tl, "session") || s->channel) {
        put_u8(MSG_CHANNEL_OPEN_FAILURE);
        put_u32(sender);
        put_u32(1);                             // administratively prohibited
        put_cstr("MyOS: only one shell session");
        put_cstr("");
        send_packet();
        return;
    }
    s->channel = 1;
    s->peer_channel = sender;
    s->send_window = window;
    s->peer_max = maxpkt;
    s->recv_window = OUR_WINDOW;
    put_u8(MSG_CHANNEL_OPEN_CONFIRMATION);
    put_u32(sender);
    put_u32(0);                                 // our channel number
    put_u32(OUR_WINDOW);
    put_u32(OUR_MAX_PACKET);
    send_packet();
}

static void handle_channel_request(reader *r) {
    get_u32(r);
    uint32_t tl;
    const uint8_t *type = get_string(r, &tl);
    uint8_t want_reply = get_u8(r);
    if (r->bad || !s->channel) return;
    int ok = 0;
    if (str_is(type, tl, "pty-req") || str_is(type, tl, "env") || str_is(type, tl, "window-change")) {
        ok = 1;                                 // accepted (MyOS doesn't need the details)
    } else if (str_is(type, tl, "shell")) {
        ok = 1;
        s->shell = 1;
        s->keys_head = s->keys_tail = 0;
        s->tx_len = 0;
                                                // (console.c sees the shell and opens a terminal)
    }
    if (want_reply) {
        out_len = 0;
        put_u8(ok ? MSG_CHANNEL_SUCCESS : MSG_CHANNEL_FAILURE);
        put_u32(s->peer_channel);
        send_packet();
    }
}

static void handle_channel_data(reader *r) {
    get_u32(r);
    uint32_t n;
    const uint8_t *d = get_string(r, &n);
    if (r->bad || !s->channel) return;
    for (uint32_t i = 0; i < n; i++) key_put(d[i]);
    s->recv_window = n < s->recv_window ? s->recv_window - n : 0;
    if (s->recv_window < OUR_WINDOW / 2) {       // let the client send more
        out_len = 0;
        put_u8(MSG_CHANNEL_WINDOW_ADJUST);
        put_u32(s->peer_channel);
        put_u32(OUR_WINDOW - s->recv_window);
        send_packet();
        s->recv_window = OUR_WINDOW;
    }
}

// --------------------------------------------------------- incoming packets
static void handle_message(const uint8_t *msg, uint32_t n, uint32_t seq) {
    if (n == 0) { disconnect(DISC_PROTOCOL_ERROR, "empty packet"); return; }
    uint8_t type = msg[0];
    reader r = { msg, n, 1, 0 };

    if (kex_running() || !s->first_kex_done) {   // during key exchange only these are allowed
        int kex_msg = type == MSG_KEXINIT || type == MSG_KEX_ECDH_INIT || type == MSG_NEWKEYS;
        int transport = type == MSG_DISCONNECT || type == MSG_IGNORE || type == MSG_DEBUG ||
                        type == MSG_UNIMPLEMENTED;
        if (!kex_msg && (s->strict && !s->first_kex_done ? 1 : !transport)) {
            disconnect(DISC_PROTOCOL_ERROR, "unexpected message during key exchange");
            return;
        }
    }

    switch (type) {
    case MSG_DISCONNECT:
        drop_client();
        break;
    case MSG_IGNORE: case MSG_DEBUG: case MSG_UNIMPLEMENTED:
        break;
    case MSG_KEXINIT:
        handle_kexinit(msg, n, seq);
        break;
    case MSG_KEX_ECDH_INIT:
        handle_ecdh_init(&r);
        break;
    case MSG_NEWKEYS:
        handle_newkeys();
        break;
    case MSG_SERVICE_REQUEST: {
        uint32_t l;
        const uint8_t *name = get_string(&r, &l);
        if (r.bad || !str_is(name, l, "ssh-userauth")) {
            disconnect(DISC_SERVICE_NOT_AVAILABLE, "unknown service");
            break;
        }
        s->service_ok = 1;
        out_len = 0;
        put_u8(MSG_SERVICE_ACCEPT);
        put_cstr("ssh-userauth");
        send_packet();
        break;
    }
    case MSG_USERAUTH_REQUEST:
        handle_userauth(&r);
        break;
    case MSG_GLOBAL_REQUEST: {
        uint32_t l;
        get_string(&r, &l);
        if (get_u8(&r) && !r.bad) {             // want_reply
            out_len = 0;
            put_u8(MSG_REQUEST_FAILURE);
            send_packet();
        }
        break;
    }
    case MSG_CHANNEL_OPEN:
        handle_channel_open(&r);
        break;
    case MSG_CHANNEL_REQUEST:
        handle_channel_request(&r);
        break;
    case MSG_CHANNEL_DATA:
        handle_channel_data(&r);
        break;
    case MSG_CHANNEL_WINDOW_ADJUST:
        get_u32(&r);
        if (s->channel) s->send_window += get_u32(&r);
        break;
    case MSG_CHANNEL_EOF:
        break;
    case MSG_CHANNEL_CLOSE:
        if (!s->closing) close_channel();        // (if we started closing, ours was sent)
        drop_client();                          // one session per connection
        break;
    default:
        if (type >= 90 && !s->authed) { disconnect(DISC_PROTOCOL_ERROR, "log in first"); break; }
        out_len = 0;
        put_u8(MSG_UNIMPLEMENTED);
        put_u32(seq);
        send_packet();
        break;
    }
}

// one whole packet waiting in rx[]? Returns bytes it used (0 = not yet, -1 = bad)
static int next_packet(const uint8_t **payload, uint32_t *n, uint32_t *seq) {
    if (s->rx_len < 4) return 0;
    uint32_t len;
    uint8_t nonce[8];
    if (s->enc_in) {
        uint8_t l[4];
        nonce_of(nonce, s->seq_in);
        crypto_chacha20_djb(l, s->rx, 4, s->key_in + 32, nonce, 0);
        len = rd32(l);
    } else {
        len = rd32(s->rx);
    }
    if (len < 5 || len > MAX_PACKET) return -1;
    unsigned total = 4 + len + (s->enc_in ? 16 : 0);
    if (s->rx_len < total) return 0;
    if (s->enc_in) {
        uint8_t polykey[64], tag[16], zero[64] = { 0 };
        crypto_chacha20_djb(polykey, zero, 64, s->key_in, nonce, 0);
        crypto_poly1305(tag, s->rx, 4 + len, polykey);
        crypto_wipe(polykey, sizeof polykey);
        if (crypto_verify16(tag, s->rx + 4 + len) != 0) return -2;
        crypto_chacha20_djb(s->rx + 4, s->rx + 4, len, s->key_in, nonce, 1);
    }
    uint8_t pad = s->rx[4];
    if (pad < 4 || pad > len - 1) return -1;
    *payload = s->rx + 5;
    *n = len - 1 - pad;
    *seq = s->seq_in++;
    return (int)total;
}

static void consume(unsigned n) {
    memmove(s->rx, s->rx + n, s->rx_len - n);
    s->rx_len -= n;
    s->unacked += n;
}

static void read_version(void) {
    for (unsigned i = 0; i < s->rx_len; i++) {
        if (s->rx[i] != '\n') continue;
        unsigned len = i;
        if (len && s->rx[len - 1] == '\r') len--;
        if (len >= 8 && memcmp(s->rx, "SSH-2.0-", 8) == 0) {
            if (len > sizeof s->v_c) len = sizeof s->v_c;
            memcpy(s->v_c, s->rx, len);
            s->v_c_len = len;
            s->version_done = 1;
        } else if (len >= 4 && memcmp(s->rx, "SSH-", 4) == 0) {
            consume(i + 1);
            disconnect(DISC_PROTOCOL_ERROR, "only SSH version 2 is supported");
            return;
        }
        consume(i + 1);                         // other lines before the version are skipped
        return;
    }
    if (s->rx_len > 255) drop_client();            // no version line at all
}

static void poll_packets(void) {
    if (!s->version_done) {
        read_version();
        if (!s->version_done) return;
    }
    while (s->client) {
        const uint8_t *payload;
        uint32_t n, seq;
        int used = next_packet(&payload, &n, &seq);
        if (used == 0) break;
        if (used < 0) {
            s->rx_len = 0;
            disconnect(used == -2 ? DISC_MAC_ERROR : DISC_PROTOCOL_ERROR,
                       used == -2 ? "bad MAC" : "bad packet");
            break;
        }
        handle_message(payload, n, seq);        // payload stays valid: rx only grows meanwhile
        if (!s->client) break;
        consume((unsigned)used);
    }
}


// --------------------------------------------------------- TCP glue
// lwIP calls these with arg = the session; they can arrive while another
// session is being worked on, so they put s back afterwards.
static void reset_session(void) {
    crypto_wipe(s, sizeof *s);
}

static void drop_client(void) {
    if (!s->client) return;
    struct tcp_pcb *pcb = s->client;
    s->client = NULL;
    tcp_arg(pcb, NULL);
    tcp_recv(pcb, NULL);
    tcp_err(pcb, NULL);
    net_lock();
    if (tcp_close(pcb) != ERR_OK) tcp_abort(pcb);
    net_unlock();
    reset_session();
}

static err_t on_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    (void)err;
    session_t *saved = s;
    s = arg;
    err_t r = ERR_OK;
    if (!s || pcb != s->client) {
        if (p) pbuf_free(p);
    } else if (p == NULL) {                     // the client closed the connection
        s->client = NULL;
        tcp_arg(pcb, NULL);
        tcp_recv(pcb, NULL);
        tcp_err(pcb, NULL);
        if (tcp_close(pcb) != ERR_OK) tcp_abort(pcb);
        reset_session();
    } else if (p->tot_len > RX_SIZE - s->rx_len) {
        r = ERR_MEM;                            // lwIP keeps it and tries later
    } else {
        pbuf_copy_partial(p, s->rx + s->rx_len, p->tot_len, 0);
        s->rx_len += p->tot_len;
        pbuf_free(p);
    }
    s = saved;
    return r;
}

static void on_err(void *arg, err_t err) {      // lwIP already freed the connection
    (void)err;
    session_t *saved = s;
    s = arg;
    if (s) {
        s->client = NULL;
        reset_session();
    }
    s = saved;
}

static err_t on_accept(void *arg, struct tcp_pcb *pcb, err_t err) {
    (void)arg;
    if (err != ERR_OK || pcb == NULL) return ERR_VAL;
    session_t *free_one = NULL;
    for (int i = 0; i < MAX_SSH && !free_one; i++)
        if (!sessions[i].client) free_one = &sessions[i];
    if (!free_one || !host_ready) {             // all sessions in use
        tcp_abort(pcb);
        return ERR_ABRT;
    }
    session_t *saved = s;
    s = free_one;
    reset_session();
    s->client = pcb;
    s->client_ip = pcb->remote_ip;
    s->started = millis();
    tcp_nagle_disable(pcb);
    tcp_arg(pcb, s);
    tcp_recv(pcb, on_recv);
    tcp_err(pcb, on_err);
    s = saved;
    // our version line and KEXINIT go out from ssh_poll (not inside lwIP)
    return ERR_OK;
}

void ssh_init(void) {
    rng_init();
    load_host_key();
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (!pcb) return;
    if (tcp_bind(pcb, IP_ADDR_ANY, SSH_PORT) != ERR_OK) {
        tcp_close(pcb);
        return;
    }
    listener = tcp_listen_with_backlog(pcb, MAX_SSH);
    if (listener) tcp_accept(listener, on_accept);
}

static void poll_session(void) {
    if (!s->sent_kexinit && !s->first_kex_done && !s->i_s_len) {   // just connected
        tcp_send_all((const uint8_t *)SERVER_VERSION "\r\n", sizeof SERVER_VERSION + 1);
        if (s->client) send_kexinit();
    }
    if (s->client) poll_packets();
    if (s->client && s->closing && millis() > s->closing) drop_client();   // client didn't answer
    if (s->client && !s->authed && millis() - s->started > LOGIN_TIMEOUT) {
        disconnect(DISC_BY_APPLICATION, "login timeout");
    }
    if (s->unacked && s->client) {              // tell lwIP we used the data: opens the TCP window
        net_lock();
        tcp_recved(s->client, (u16_t)s->unacked);
        net_unlock();
        s->unacked = 0;
    }
}

void ssh_poll(void) {
    if (busy) return;
    busy = 1;
    session_t *saved = s;
    for (int i = 0; i < MAX_SSH; i++) {
        s = &sessions[i];
        if (s->client) poll_session();
    }
    s = saved;
    busy = 0;
}

// --------------------------------------------------------- console side (per session)
static int pick(int i) {                        // s = session i; 0 if there's no such session
    if (i < 0 || i >= MAX_SSH) return 0;
    s = &sessions[i];
    return 1;
}

void ssh_flush(int i) {
    if (busy) return;                           // never from inside packet handling
    session_t *saved = s;
    if (!pick(i)) return;
    session_t *me = s;
    uint64_t give_up = 0;
    while (me->client && me->shell && me->tx_len) {
        s = me;
        if (kex_running()) {                    // new keys being made: wait
            busy = 1; poll_packets(); busy = 0;
            if (!me->client) break;
            console_service();
            continue;
        }
        unsigned n = me->tx_len;
        if (n > me->send_window) n = me->send_window;
        if (n > me->peer_max) n = me->peer_max;
        if (n == 0) {                           // the client's window is full: wait for it
            if (!give_up) give_up = millis() + 10000;
            else if (millis() > give_up) { drop_client(); break; }
            console_service();
            ssh_poll();
            continue;
        }
        give_up = 0;
        out_len = 0;
        put_u8(MSG_CHANNEL_DATA);
        put_u32(me->peer_channel);
        put_string(me->tx, n);
        me->send_window -= n;
        memmove(me->tx, me->tx + n, me->tx_len - n);
        me->tx_len -= n;
        busy = 1;
        send_packet();
        busy = 0;
    }
    s = saved;
}

void ssh_putc(int i, char c) {
    if (i < 0 || i >= MAX_SSH) return;
    session_t *me = &sessions[i];
    if (!me->client || !me->shell) return;
    if (me->tx_len + 2 > sizeof me->tx) ssh_flush(i);
    if (!me->client || !me->shell || me->tx_len + 2 > sizeof me->tx) return;
    if (c == '\n') me->tx[me->tx_len++] = '\r';
    me->tx[me->tx_len++] = c;
}

int ssh_getc(int i) {
    if (i < 0 || i >= MAX_SSH) return -1;
    session_t *me = &sessions[i];
    if (me->keys_tail == me->keys_head) return -1;
    uint8_t c = me->keys[me->keys_tail];
    me->keys_tail = (me->keys_tail + 1) % sizeof me->keys;
    return c;
}

int ssh_shell_open(int i) {                     // logged in and a shell was asked for
    return i >= 0 && i < MAX_SSH && sessions[i].client && sessions[i].shell;
}

int ssh_client(int i, char *outs) {
    if (i < 0 || i >= MAX_SSH || !sessions[i].client || !sessions[i].authed) return 0;
    strcpy(outs, sessions[i].user);
    strcat(outs, "@");
    strcat(outs, ipaddr_ntoa(&sessions[i].client_ip));
    return 1;
}

void ssh_logout(int i) {
    session_t *saved = s;
    if (!pick(i) || !s->client || !s->channel) {
        s = saved;
        return;
    }
    session_t *me = s;
    ssh_flush(i);
    s = me;
    out_len = 0;
    put_u8(MSG_CHANNEL_REQUEST);                // the "program" ended normally
    put_u32(s->peer_channel);
    put_cstr("exit-status");
    put_u8(0);
    put_u32(0);
    if (send_packet()) {
        out_len = 0;
        put_u8(MSG_CHANNEL_EOF);
        put_u32(s->peer_channel);
        if (send_packet()) {
            close_channel();
            s->closing = millis() + 3000;       // the client answers with its CLOSE
        }
    }
    s = saved;
}

int ssh_host_key_saved(void) {
    return host_saved;
}

int ssh_sessions(void) {
    return MAX_SSH;
}
