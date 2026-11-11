// http.c - a small HTTP/1.1 client for 'pkg': plain HTTP to the PC over the
// USB cable (tools/repo.py on port 8080), and HTTPS (tls.c) to the internet
// over Wi-Fi, e.g. the package repository on GitHub.
//
// One download at a time; the shell waits for it. Names are looked up with
// lwIP's DNS client (the DNS server comes from the router's DHCP).
#include <string.h>
#include "console.h"
#include "http.h"
#include "timer.h"

#ifdef NO_USB
int http_get(const char *path, uint8_t *buf, uint32_t max, uint32_t *len) {
    (void)path; (void)buf; (void)max; (void)len;
    return HTTP_ERR_NONET;
}
int http_get_url(const char *url, uint8_t *buf, uint32_t max, uint32_t *len) {
    (void)url; (void)buf; (void)max; (void)len;
    return HTTP_ERR_NONET;
}
const char *http_host(void) { return "(no network)"; }
int http_tls_error(void) { return 0; }
#else
#include "lwip/dns.h"
#include "lwip/tcp.h"
#include "net.h"
#include "tls.h"

#define IDLE_MS 15000                       // give up when nothing arrives for this long

// ---------------------------------------------------------------- one TCP connection
static struct tcp_pcb *pcb;
static volatile int state;                  // 0 connecting, 1 connected, 2 server closed, -1 failed
static uint8_t rxb[16 * 1024];              // bytes received, not read yet
static volatile uint32_t rx_len;
static uint32_t rx_pos;
static char host_text[24];
static int last_tls_error;

static err_t on_connected(void *arg, struct tcp_pcb *p, err_t err) {
    (void)arg; (void)p;
    state = (err == ERR_OK) ? 1 : -1;
    return ERR_OK;
}

static err_t on_recv(void *arg, struct tcp_pcb *p, struct pbuf *b, err_t err) {
    (void)arg; (void)p; (void)err;
    if (b == NULL) {                        // the server finished sending
        state = 2;
        return ERR_OK;
    }
    if (rx_pos && rx_pos == rx_len) rx_pos = rx_len = 0;
    if (b->tot_len > sizeof rxb - rx_len) {
        if (rx_pos) {                       // make room
            memmove(rxb, rxb + rx_pos, rx_len - rx_pos);
            rx_len -= rx_pos;
            rx_pos = 0;
        }
        if (b->tot_len > sizeof rxb - rx_len) return ERR_MEM;   // lwIP keeps it, tries later
    }
    pbuf_copy_partial(b, rxb + rx_len, b->tot_len, 0);
    rx_len += b->tot_len;
    pbuf_free(b);                           // (tcp_recved when the data is read: flow control)
    return ERR_OK;
}

static void on_err(void *arg, err_t err) {
    (void)arg; (void)err;
    pcb = NULL;                             // lwIP freed it
    if (state != 2) state = -1;
}

static void tcp_finish(void) {
    if (!pcb) return;
    net_lock();
    tcp_arg(pcb, NULL);
    tcp_recv(pcb, NULL);
    tcp_err(pcb, NULL);
    if (tcp_close(pcb) != ERR_OK) tcp_abort(pcb);
    net_unlock();
    pcb = NULL;
}

static int tcp_open(const ip_addr_t *addr, uint16_t port) {
    state = 0;
    rx_len = rx_pos = 0;
    net_lock();
    pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (pcb) {
        tcp_recv(pcb, on_recv);
        tcp_err(pcb, on_err);
        if (tcp_connect(pcb, addr, port, on_connected) != ERR_OK) state = -1;
    }
    net_unlock();
    if (!pcb) return -1;
    uint64_t end = millis() + 8000;
    while (state == 0 && millis() < end) console_service();
    if (state != 1) {
        tcp_finish();
        return -1;
    }
    return 0;
}

// read exactly n bytes (handing them to lwIP as read, so the window opens)
static int tcp_read(uint8_t *out, uint32_t n) {
    uint64_t idle = millis() + IDLE_MS;
    while (n) {
        uint32_t avail = rx_len - rx_pos;
        if (avail) {
            uint32_t take = avail < n ? avail : n;
            memcpy(out, rxb + rx_pos, take);
            rx_pos += take;
            out += take;
            n -= take;
            if (pcb) {
                net_lock();
                tcp_recved(pcb, (u16_t)take);
                net_unlock();
            }
            idle = millis() + IDLE_MS;
            continue;
        }
        if (state != 1 || millis() > idle) return -1;   // closed, failed or silent
        console_service();
    }
    return 0;
}

// whatever arrives until the server closes -> bytes, or -1 on time out / overflow
static int tcp_read_rest(uint8_t *out, uint32_t max, uint32_t *got) {
    uint64_t idle = millis() + IDLE_MS;
    for (;;) {
        uint32_t avail = rx_len - rx_pos;
        if (avail) {
            if (*got + avail > max) return -1;
            memcpy(out + *got, rxb + rx_pos, avail);
            *got += avail;
            rx_pos += avail;
            if (pcb) {
                net_lock();
                tcp_recved(pcb, (u16_t)avail);
                net_unlock();
            }
            idle = millis() + IDLE_MS;
            continue;
        }
        if (state != 1) return 0;           // closed (2) - or broken (-1) after some data
        if (millis() > idle) return -1;
        console_service();
    }
}

static int tcp_write_all(const uint8_t *p, uint32_t n) {
    uint64_t give_up = millis() + IDLE_MS;
    while (n) {
        if (!pcb || state != 1) return -1;
        net_lock();
        uint32_t room = tcp_sndbuf(pcb);
        uint32_t take = n < room ? n : room;
        err_t e = take ? tcp_write(pcb, p, (u16_t)take, TCP_WRITE_FLAG_COPY) : ERR_MEM;
        if (e == ERR_OK) tcp_output(pcb);
        net_unlock();
        if (e == ERR_OK) {
            p += take;
            n -= take;
            give_up = millis() + IDLE_MS;
        } else if (e == ERR_MEM && millis() < give_up) {
            console_service();              // wait for the other side to take what we sent
        } else {
            return -1;
        }
    }
    return 0;
}

static int io_read(uint8_t *buf, uint32_t n) { return tcp_read(buf, n); }
static int io_write(const uint8_t *buf, uint32_t n) { return tcp_write_all(buf, n); }
static const tls_io_t tls_io = { io_read, io_write };

// ---------------------------------------------------------------- names
static volatile int dns_state;              // 0 waiting, 1 found, -1 not found
static ip_addr_t dns_addr;

static void on_dns(const char *name, const ip_addr_t *addr, void *arg) {
    (void)name; (void)arg;
    if (addr) {
        dns_addr = *addr;
        dns_state = 1;
    } else {
        dns_state = -1;
    }
}

static int resolve(const char *name, ip_addr_t *out) {
    if (ipaddr_aton(name, out)) return 0;   // already a number
    dns_state = 0;
    net_lock();
    err_t e = dns_gethostbyname(name, &dns_addr, on_dns, NULL);
    net_unlock();
    if (e == ERR_OK) {
        *out = dns_addr;
        return 0;
    }
    if (e != ERR_INPROGRESS) return -1;
    uint64_t end = millis() + 10000;
    while (dns_state == 0 && millis() < end) console_service();
    if (dns_state != 1) return -1;
    *out = dns_addr;
    return 0;
}

// ---------------------------------------------------------------- HTTP
typedef struct {
    int tls;
    char host[64];
    uint16_t port;
    const char *path;                       // starts with '/'
} url_t;

static int parse_url(const char *s, url_t *u) {
    if (strncmp(s, "https://", 8) == 0) {
        u->tls = 1;
        u->port = 443;
        s += 8;
    } else if (strncmp(s, "http://", 7) == 0) {
        u->tls = 0;
        u->port = 80;
        s += 7;
    } else {
        return -1;
    }
    size_t n = 0;
    while (*s && *s != '/' && *s != ':' && n < sizeof u->host - 1) u->host[n++] = *s++;
    u->host[n] = 0;
    if (!n) return -1;
    if (*s == ':') {
        unsigned port = 0;
        for (s++; *s >= '0' && *s <= '9'; s++) port = port * 10 + (unsigned)(*s - '0');
        u->port = (uint16_t)port;
    }
    u->path = *s ? s : "/";
    return *u->path == '/' ? 0 : -1;
}

static int lower_eq(const uint8_t *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        uint8_t c = a[i];
        if (c >= 'A' && c <= 'Z') c += 32;
        if (c != (uint8_t)b[i]) return 0;
    }
    return 1;
}

// find "Name:" in the headers (buf[0..hdr_end)), value -> v/vlen
static int header(const uint8_t *buf, uint32_t hdr_end, const char *name, const uint8_t **v, uint32_t *vlen) {
    size_t nl = strlen(name);
    for (uint32_t i = 0; i + nl + 1 < hdr_end; i++) {
        if ((i == 0 || buf[i - 1] == '\n') && lower_eq(buf + i, name, nl) && buf[i + nl] == ':') {
            uint32_t j = i + (uint32_t)nl + 1;
            while (j < hdr_end && buf[j] == ' ') j++;
            uint32_t k = j;
            while (k < hdr_end && buf[k] != '\r' && buf[k] != '\n') k++;
            *v = buf + j;
            *vlen = k - j;
            return 1;
        }
    }
    return 0;
}

// "chunked" transfer encoding -> plain bytes, in place; -1 if broken
static int64_t dechunk(uint8_t *b, uint32_t n) {
    uint32_t in = 0, outp = 0;
    for (;;) {
        uint32_t size = 0;
        int digits = 0;
        while (in < n) {
            uint8_t c = b[in];
            int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10
                  : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
            if (d < 0) break;
            size = size * 16 + (uint32_t)d;
            digits++;
            in++;
        }
        while (in < n && b[in] != '\n') in++;   // chunk extensions, CR LF
        in++;
        if (!digits || in > n) return -1;
        if (size == 0) return outp;
        if (in + size > n) return -1;
        memmove(b + outp, b + in, size);
        outp += size;
        in += size + 2;                     // the data's CR LF
    }
}

static int fetch(const url_t *u, uint8_t *buf, uint32_t max, uint32_t *len, char *location, size_t loc_max) {
    ip_addr_t addr;
    *len = 0;
    last_tls_error = 0;
    if (resolve(u->host, &addr)) return HTTP_ERR_DNS;
    if (tcp_open(&addr, u->port)) return HTTP_ERR_CONNECT;
    if (u->tls) {
        int r = tls_connect(&tls_io, u->host);
        if (r) {
            last_tls_error = r;
            tcp_finish();
            return HTTP_ERR_TLS;
        }
    }
    char req[400];
    strcpy(req, "GET ");
    size_t pl = strlen(u->path);
    if (pl > 250) pl = 250;
    memcpy(req + 4, u->path, pl);
    req[4 + pl] = 0;
    strcat(req, " HTTP/1.1\r\nHost: ");
    strcat(req, u->host);
    strcat(req, "\r\nUser-Agent: MyOS-pkg/1.0\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n");
    int r = u->tls ? tls_write((const uint8_t *)req, (uint32_t)strlen(req))
                   : tcp_write_all((const uint8_t *)req, (uint32_t)strlen(req));
    if (r) {
        tcp_finish();
        return HTTP_ERR_CONNECT;
    }
    uint32_t got = 0;
    if (u->tls) {
        for (;;) {
            if (got >= max) {
                tcp_finish();
                return HTTP_ERR_TOOBIG;
            }
            int n = tls_read(buf + got, max - got);
            if (n == 0) break;
            if (n < 0) {
                tcp_finish();
                if (got) break;             // some servers just drop the connection at the end
                last_tls_error = n;
                return HTTP_ERR_TLS;
            }
            got += (uint32_t)n;
        }
    } else if (tcp_read_rest(buf, max, &got) < 0) {
        tcp_finish();
        return got >= max ? HTTP_ERR_TOOBIG : HTTP_ERR_TIMEOUT;
    }
    tcp_finish();

    // "HTTP/1.x 200 ...\r\n headers \r\n\r\n body"
    if (got < 12 || memcmp(buf, "HTTP/1.", 7) != 0) return HTTP_ERR_BAD;
    int status = (buf[9] - '0') * 100 + (buf[10] - '0') * 10 + (buf[11] - '0');
    uint32_t body = 0;
    for (uint32_t i = 0; i + 3 < got; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n') {
            body = i + 4;
            break;
        }
    }
    if (!body) return HTTP_ERR_BAD;
    const uint8_t *v;
    uint32_t vl;
    if (location && header(buf, body, "location", &v, &vl) && vl < loc_max) {
        memcpy(location, v, vl);
        location[vl] = 0;
    }
    int chunked = header(buf, body, "transfer-encoding", &v, &vl) && vl >= 7 && lower_eq(v, "chunked", 7);
    uint32_t n = got - body;
    memmove(buf, buf + body, n);
    if (chunked) {
        int64_t m = dechunk(buf, n);
        if (m < 0) return HTTP_ERR_BAD;
        n = (uint32_t)m;
    }
    *len = n;
    return status;
}

int http_get_url(const char *url, uint8_t *buf, uint32_t max, uint32_t *len) {
    char where[300];
    size_t ul = strlen(url);
    if (ul >= sizeof where) return HTTP_ERR_BAD;
    memcpy(where, url, ul + 1);
    for (int hops = 0; hops < 4; hops++) {  // follow a few redirects
        url_t u;
        if (parse_url(where, &u)) return HTTP_ERR_BAD;
        char location[300];
        location[0] = 0;
        int status = fetch(&u, buf, max, len, location, sizeof location);
        if ((status == 301 || status == 302 || status == 307 || status == 308) && location[0]) {
            if (location[0] == '/') {       // same server, new path
                char next[300];
                strcpy(next, u.tls ? "https://" : "http://");
                strcat(next, u.host);
                if (strlen(next) + strlen(location) >= sizeof next) return HTTP_ERR_BAD;
                strcat(next, location);
                strcpy(where, next);
            } else {
                strcpy(where, location);
            }
            continue;
        }
        return status;
    }
    return HTTP_ERR_BAD;
}

const char *http_host(void) {
    char ip[16];
    if (!net_pc_address(ip)) return "(the PC has no address yet)";
    strcpy(host_text, ip);
    strcat(host_text, ":8080");
    return host_text;
}

int http_get(const char *path, uint8_t *buf, uint32_t max, uint32_t *len) {
    char ip[16], url[200];
    *len = 0;
    if (!net_link_up() || !net_pc_address(ip)) return HTTP_ERR_NONET;
    strcpy(url, "http://");
    strcat(url, ip);
    strcat(url, ":8080/");
    size_t n = strlen(url), pl = strlen(path);
    if (n + pl >= sizeof url) return HTTP_ERR_BAD;
    strcat(url, path);
    return http_get_url(url, buf, max, len);
}

int http_tls_error(void) {
    return last_tls_error;
}
#endif

const char *http_error(int code) {
    switch (code) {
        case HTTP_ERR_NONET:   return "No network. Is the USB cable in? (type 'net')";
        case HTTP_ERR_CONNECT: return "Can't reach the package server.";
        case HTTP_ERR_TIMEOUT: return "The package server stopped answering.";
        case HTTP_ERR_TOOBIG:  return "The file is too big.";
        case HTTP_ERR_DNS:     return "Can't find the server's address (DNS). Is Wi-Fi online? (type 'wifi')";
        case HTTP_ERR_TLS:     return "The secure (HTTPS) connection failed.";
        default:               return "The package server sent something odd.";
    }
}
