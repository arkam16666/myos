// net.c - networking over the USB cable.
//
// The Pi shows up on the PC as a USB network adapter (CDC-NCM, via TinyUSB)
// and runs the lwIP TCP/IP stack:
//   Pi = 192.168.7.1, the PC gets 192.168.7.2 from a small DHCP server,
//   and the name "myos.usb" points at the Pi (DNS server).
#include <string.h>
#include "dhserver.h"
#include "dnserver.h"
#include "led.h"
#include "lwip/init.h"
#include "lwip/timeouts.h"
#include "lwip/etharp.h"
#include "lwip/ethip6.h"
#include "net.h"
#include "timer.h"
#include "tusb.h"

#define INIT_IP4(a, b, c, d) { PP_HTONL(LWIP_MAKEU32(a, b, c, d)) }

uint8_t tud_network_mac_address[6] = {0x02, 0x02, 0x84, 0x6A, 0x96, 0x00};

static struct netif netif_data;
static const ip4_addr_t ipaddr = INIT_IP4(192, 168, 7, 1);
static const ip4_addr_t netmask = INIT_IP4(255, 255, 255, 0);
static const ip4_addr_t gateway = INIT_IP4(0, 0, 0, 0);
static int started;
static uint32_t packets_in, packets_out;

// lwIP must not be entered twice at once. Sending a frame can run TinyUSB,
// which can hand us a received frame: while lwIP is busy such frames are
// left in TinyUSB and picked up again by net_poll().
static int busy, deferred;

void net_lock(void) { busy++; }
void net_unlock(void) { busy--; }

static dhcp_entry_t entries[] = {
    { {0}, INIT_IP4(192, 168, 7, 2), 24 * 60 * 60 },
    { {0}, INIT_IP4(192, 168, 7, 3), 24 * 60 * 60 },
};

static const dhcp_config_t dhcp_config = {
    .router = INIT_IP4(0, 0, 0, 0),
    .port = 67,
    .dns = INIT_IP4(192, 168, 7, 1),
    .domain = "usb",
    .num_entry = TU_ARRAY_SIZE(entries),
    .entries = entries,
};

// lwIP glue -------------------------------------------------------
u32_t sys_now(void) {
    return (u32_t)millis();
}

unsigned int myos_random(void) {
    static uint64_t x;
    x = x * 6364136223846793005ull + timer_count() + 1442695040888963407ull;
    return (unsigned int)(x >> 33);
}

void myos_lwip_assert(void) {
    led_panic();
}

// a frame lwIP wants to send: hand it to TinyUSB
static err_t linkoutput_fn(struct netif *netif, struct pbuf *p) {
    (void)netif;
    if (p->tot_len > CFG_TUD_NET_MTU) return ERR_BUF;
    for (;;) {
        if (!tud_ready()) return ERR_USE;
        if (tud_network_can_xmit(p->tot_len)) {
            tud_network_xmit(p, 0);
            packets_out++;
            return ERR_OK;
        }
        tud_task();             // let the previous frame go out
    }
}

static err_t ip4_output_fn(struct netif *netif, struct pbuf *p, const ip4_addr_t *addr) {
    return etharp_output(netif, p, addr);
}

static err_t netif_init_cb(struct netif *netif) {
    netif->mtu = CFG_TUD_NET_MTU - SIZEOF_ETH_HDR;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP | NETIF_FLAG_UP;
    netif->name[0] = 'u';
    netif->name[1] = 's';
    netif->linkoutput = linkoutput_fn;
    netif->output = ip4_output_fn;
    return ERR_OK;
}

// a frame arrived from the PC
bool tud_network_recv_cb(const uint8_t *src, uint16_t size) {
    if (size && started) {
        if (busy) {                             // inside lwIP: later
            deferred = 1;
            return false;
        }
        struct pbuf *p = pbuf_alloc(PBUF_RAW, size, PBUF_POOL);
        if (p == NULL) {                        // out of buffers: later
            deferred = 1;
            return false;
        }
        pbuf_take(p, src, size);
        busy++;
        if (netif_data.input(p, &netif_data) != ERR_OK) pbuf_free(p);
        busy--;
        packets_in++;
    }
    tud_network_recv_renew();
    return true;
}

uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg) {
    (void)arg;
    struct pbuf *p = (struct pbuf *)ref;
    return pbuf_copy_partial(p, dst, p->tot_len, 0);
}

void tud_network_init_cb(void) {
}

static bool dns_query(const char *name, ip4_addr_t *addr) {
    if (strcmp(name, "myos.usb") == 0 || strcmp(name, "myos") == 0) {
        *addr = ipaddr;
        return true;
    }
    return false;
}

// -----------------------------------------------------------------
void net_init(void) {
    struct netif *netif = &netif_data;
    lwip_init();
    netif->hwaddr_len = sizeof(tud_network_mac_address);
    memcpy(netif->hwaddr, tud_network_mac_address, sizeof(tud_network_mac_address));
    netif->hwaddr[5] ^= 0x01;           // must differ from the PC side's MAC
    netif_add(netif, &ipaddr, &netmask, &gateway, NULL, netif_init_cb, ethernet_input);
    netif_set_default(netif);
    dhserv_init(&dhcp_config);
    dnserv_init(IP_ADDR_ANY, 53, dns_query);
    started = 1;
}

void net_poll(void) {
    if (!started || busy) return;
    busy++;
    sys_check_timeouts();
    busy--;
    if (deferred) {                             // frames left waiting in TinyUSB
        deferred = 0;
        tud_network_recv_renew();
    }
}

int net_link_up(void) {
    return started && tud_ready();
}

uint32_t net_packets_in(void) { return packets_in; }
uint32_t net_packets_out(void) { return packets_out; }

int net_pc_address(char *out) {          // the address our DHCP server gave out, if any
    for (unsigned i = 0; i < TU_ARRAY_SIZE(entries); i++) {
        if (entries[i].mac[0] | entries[i].mac[1] | entries[i].mac[2]) {
            const uint8_t *a = (const uint8_t *)&entries[i].addr;
            char *p = out;
            for (int k = 0; k < 4; k++) {
                unsigned v = a[k];
                if (v >= 100) *p++ = (char)('0' + v / 100);
                if (v >= 10) *p++ = (char)('0' + v / 10 % 10);
                *p++ = (char)('0' + v % 10);
                *p++ = k < 3 ? '.' : 0;
            }
            return 1;
        }
    }
    return 0;
}
