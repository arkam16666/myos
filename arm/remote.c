// remote.c - the MyOS shell over the USB network (telnet, TCP port 23).
//
// On the PC:  PuTTY -> Connection type "Telnet" -> Host 192.168.7.1
// One client at a time. Not encrypted and no password: only the PC at the
// other end of the USB cable can reach 192.168.7.1. (SSH comes later.)
//
// The console sends every character here as well as to the serial ports,
// and reads keys from here too, so everything that works on the COM port
// (shell, programs, Python) works over the network.
#include <string.h>
#include "console.h"
#include "lwip/tcp.h"
#include "net.h"
#include "remote.h"
#include "timer.h"

#define TELNET_PORT 23
enum { IAC = 255, DONT = 254, DO = 253, WONT = 252, WILL = 251, SB = 250, SE = 240 };
enum { OPT_ECHO = 1, OPT_SGA = 3 };

static struct tcp_pcb *listener, *client;
static int connect_event;
static ip_addr_t client_ip;

static uint8_t rx[1024];                 // keys from the client
static unsigned rx_head, rx_tail;
static char tx[1460];                    // text waiting to be sent
static unsigned tx_len;

// incoming telnet stream -> plain keys
static enum { T_DATA, T_IAC, T_OPT, T_SB, T_SB_IAC, T_CR } tstate;

static void rx_put(uint8_t c) {
    unsigned next = (rx_head + 1) % sizeof rx;
    if (next != rx_tail) {                  // full: drop (typing never fills it)
        rx[rx_head] = c;
        rx_head = next;
    }
}

static void telnet_in(uint8_t c) {
    switch (tstate) {
    case T_CR:                              // Enter arrives as CR LF or CR NUL
        tstate = T_DATA;
        if (c == '\n' || c == 0) break;
        /* fall through */
    case T_DATA:
        if (c == IAC) tstate = T_IAC;
        else {
            rx_put(c);
            if (c == '\r') tstate = T_CR;
        }
        break;
    case T_IAC:
        if (c == IAC) { rx_put(IAC); tstate = T_DATA; }
        else if (c >= WILL && c <= DONT) tstate = T_OPT;  // option answer: ignore
        else if (c == SB) tstate = T_SB;
        else tstate = T_DATA;
        break;
    case T_OPT:
        tstate = T_DATA;
        break;
    case T_SB:                              // skip sub-negotiation until IAC SE
        if (c == IAC) tstate = T_SB_IAC;
        break;
    case T_SB_IAC:
        tstate = (c == SE) ? T_DATA : T_SB;
        break;
    }
}

static void drop_client(void) {
    if (!client) return;
    struct tcp_pcb *pcb = client;
    client = NULL;
    tx_len = 0;
    tcp_arg(pcb, NULL);
    tcp_recv(pcb, NULL);
    tcp_err(pcb, NULL);
    net_lock();
    if (tcp_close(pcb) != ERR_OK) tcp_abort(pcb);
    net_unlock();
}

void remote_logout(void) {
    remote_flush();
    drop_client();
}

static err_t on_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    (void)arg;
    (void)err;
    if (p == NULL) {                        // the client closed the connection
        if (pcb == client) drop_client();
        return ERR_OK;
    }
    for (struct pbuf *q = p; q; q = q->next) {
        const uint8_t *d = q->payload;
        for (u16_t i = 0; i < q->len; i++) telnet_in(d[i]);
    }
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

static void on_err(void *arg, err_t err) {  // lwIP already freed the connection
    (void)arg;
    (void)err;
    client = NULL;
    tx_len = 0;
}

static err_t on_accept(void *arg, struct tcp_pcb *pcb, err_t err) {
    (void)arg;
    if (err != ERR_OK || pcb == NULL) return ERR_VAL;
    // someone is already connected: the new connection takes the terminal over.
    // (A client that vanished while MyOS was busy - e.g. unpacking a big
    // package - can leave a connection lwIP still thinks is open, and telnet
    // would refuse everyone until a reboot. It's USB-only, one person's PC.)
    int took_over = client != NULL;
    if (took_over) drop_client();
    client = pcb;
    client_ip = pcb->remote_ip;
    tcp_nagle_disable(pcb);                 // send each keystroke echo at once
    tcp_arg(pcb, NULL);
    tcp_recv(pcb, on_recv);
    tcp_err(pcb, on_err);
    rx_head = rx_tail = 0;
    tstate = T_DATA;
    // "I will echo, and no line-at-a-time": the client sends each key as typed
    static const char hello[] = { (char)IAC, (char)WILL, OPT_ECHO, (char)IAC, (char)WILL, OPT_SGA,
                                  (char)IAC, (char)DO, OPT_SGA };
    memcpy(tx, hello, sizeof hello);
    tx_len = sizeof hello;
    if (took_over) {                        // (the same shell goes on: no new banner)
        static const char note[] = "\r\n(this telnet session took over the earlier one: press Enter)\r\n";
        memcpy(tx + tx_len, note, sizeof note - 1);
        tx_len += sizeof note - 1;
    }
    connect_event = 1;
    return ERR_OK;
}

void remote_init(void) {
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (!pcb) return;
    // telnet has no password: only over the USB cable (192.168.7.1), never on Wi-Fi
    static const ip_addr_t usb_ip = IPADDR4_INIT_BYTES(192, 168, 7, 1);
    if (tcp_bind(pcb, &usb_ip, TELNET_PORT) != ERR_OK) {
        tcp_close(pcb);
        return;
    }
    listener = tcp_listen_with_backlog(pcb, 1);
    if (listener) tcp_accept(listener, on_accept);
}

void remote_flush(void) {
    uint64_t give_up = 0;
    while (client && tx_len) {
        unsigned n = tx_len;
        if (n > tcp_sndbuf(client)) n = tcp_sndbuf(client);
        err_t e = ERR_MEM;
        if (n) {
            net_lock();
            e = tcp_write(client, tx, (u16_t)n, TCP_WRITE_FLAG_COPY);
            if (e == ERR_OK) tcp_output(client);
            net_unlock();
        }
        if (e == ERR_OK) {
            memmove(tx, tx + n, tx_len - n);
            tx_len -= n;
            give_up = 0;
        } else if (e == ERR_MEM) {          // send buffer full: let the PC catch up
            if (!give_up) give_up = millis() + 5000;
            else if (millis() > give_up) { drop_client(); break; }
            console_service();
        } else {
            drop_client();
        }
    }
}

void remote_putc(char c) {
    if (!client) return;
    if (tx_len + 2 > sizeof tx) remote_flush();
    if (!client) return;
    if (c == '\n') tx[tx_len++] = '\r';
    else if ((uint8_t)c == IAC) tx[tx_len++] = (char)IAC;   // a real 255 byte is sent twice
    tx[tx_len++] = c;
}

int remote_getc(void) {
    if (rx_tail == rx_head) return -1;
    uint8_t c = rx[rx_tail];
    rx_tail = (rx_tail + 1) % sizeof rx;
    return c;
}

int remote_take_connect_event(void) {
    int e = connect_event;
    connect_event = 0;
    return e;
}

int remote_client(char *out) {
    if (!client) return 0;
    strcpy(out, ipaddr_ntoa(&client_ip));
    return 1;
}

int remote_active(void) {
    return client != NULL;
}
