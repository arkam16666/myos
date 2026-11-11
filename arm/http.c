// http.c - a tiny HTTP/1.0 client: downloads packages from the PC.
// The PC runs tools/repo.py, which serves the package repository on port
// 8080; the Pi reaches it at the address its DHCP server gave the PC.
#include <string.h>
#include "console.h"
#include "http.h"
#include "timer.h"

#ifdef NO_USB
int http_get(const char *path, uint8_t *buf, uint32_t max, uint32_t *len) {
    (void)path; (void)buf; (void)max; (void)len;
    return HTTP_ERR_NONET;
}
const char *http_host(void) { return "(no network)"; }
#else
#include "lwip/tcp.h"
#include "net.h"

static struct tcp_pcb *pcb;
static volatile int state;              // 0 connecting, 1 connected, 2 finished, -1 failed
static uint8_t *dst;
static uint32_t cap, got;
static int overflow;
static char host[24];

static err_t on_connected(void *arg, struct tcp_pcb *p, err_t err) {
    (void)arg; (void)p;
    state = (err == ERR_OK) ? 1 : -1;
    return ERR_OK;
}

static err_t on_recv(void *arg, struct tcp_pcb *p, struct pbuf *b, err_t err) {
    (void)arg; (void)err;
    if (b == NULL) {                    // the server finished sending
        state = 2;
        return ERR_OK;
    }
    uint32_t room = cap - got;
    uint32_t n = b->tot_len < room ? b->tot_len : room;
    if (n < b->tot_len) overflow = 1;
    pbuf_copy_partial(b, dst + got, (u16_t)n, 0);
    got += n;
    tcp_recved(p, b->tot_len);
    pbuf_free(b);
    return ERR_OK;
}

static void on_err(void *arg, err_t err) {
    (void)arg; (void)err;
    pcb = NULL;                         // lwIP freed it
    if (state != 2) state = -1;
}

static void finish(void) {
    if (!pcb) return;
    net_lock();
    tcp_arg(pcb, NULL);
    tcp_recv(pcb, NULL);
    tcp_err(pcb, NULL);
    if (tcp_close(pcb) != ERR_OK) tcp_abort(pcb);
    net_unlock();
    pcb = NULL;
}

// wait while state == from; 0 on time out
static int wait_state(int from, unsigned ms) {
    uint64_t end = millis() + ms;
    uint32_t last = got;
    while (state == from) {
        console_service();
        if (got != last) {              // data still coming: keep waiting
            last = got;
            end = millis() + ms;
        }
        if (millis() > end) return 0;
    }
    return 1;
}

const char *http_host(void) {
    char ip[16];
    if (!net_pc_address(ip)) return "(the PC has no address yet)";
    strcpy(host, ip);
    strcat(host, ":8080");
    return host;
}

int http_get(const char *path, uint8_t *buf, uint32_t max, uint32_t *len) {
    char ip[16];
    ip4_addr_t addr;
    *len = 0;
    if (!net_link_up() || !net_pc_address(ip) || !ip4addr_aton(ip, &addr)) return HTTP_ERR_NONET;

    dst = buf;
    cap = max;
    got = 0;
    overflow = 0;
    state = 0;
    net_lock();
    pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (pcb) {
        tcp_recv(pcb, on_recv);
        tcp_err(pcb, on_err);
        ip_addr_t a;
        ip_addr_copy_from_ip4(a, addr);
        if (tcp_connect(pcb, &a, REPO_PORT, on_connected) != ERR_OK) state = -1;
    }
    net_unlock();
    if (!pcb) return HTTP_ERR_CONNECT;
    if (!wait_state(0, 3000) || state != 1) {
        finish();
        return HTTP_ERR_CONNECT;
    }

    char req[200];
    strcpy(req, "GET /");
    size_t pl = strlen(path);
    if (pl > 100) pl = 100;
    memcpy(req + 5, path, pl);
    req[5 + pl] = 0;
    strcat(req, " HTTP/1.0\r\nHost: ");
    strcat(req, ip);
    strcat(req, "\r\nUser-Agent: MyOS-pkg\r\n\r\n");
    net_lock();
    err_t e = tcp_write(pcb, req, (u16_t)strlen(req), TCP_WRITE_FLAG_COPY);
    if (e == ERR_OK) tcp_output(pcb);
    net_unlock();
    if (e != ERR_OK) {
        finish();
        return HTTP_ERR_CONNECT;
    }
    int ok = wait_state(1, 10000);
    finish();
    if (!ok) return HTTP_ERR_TIMEOUT;
    if (overflow) return HTTP_ERR_TOOBIG;

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
    memmove(buf, buf + body, got - body);
    *len = got - body;
    return status;
}
#endif

const char *http_error(int code) {
    switch (code) {
        case HTTP_ERR_NONET:   return "No network. Is the USB cable in? (type 'net')";
        case HTTP_ERR_CONNECT: return "Can't reach the package server on the PC. Start it there with:\n"
                                      "  python tools\\repo.py";
        case HTTP_ERR_TIMEOUT: return "The package server stopped answering.";
        case HTTP_ERR_TOOBIG:  return "The file is too big.";
        default:               return "The package server sent something odd.";
    }
}
