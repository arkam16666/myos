// lwipopts.h - settings for the lwIP TCP/IP stack in MyOS-ARM
#ifndef LWIPOPTS_H
#define LWIPOPTS_H

#define NO_SYS                      1       // no threads: MyOS polls lwIP from its main loop
#define SYS_LIGHTWEIGHT_PROT        0
#define MEM_ALIGNMENT               8
#define MEM_SIZE                    (64 * 1024)
#define MEMP_NUM_TCP_PCB            8
#define MEMP_NUM_TCP_SEG            32
#define PBUF_POOL_SIZE              24

#define LWIP_RAW                    1       // ping
#define LWIP_NETCONN                0
#define LWIP_SOCKET                 0
#define LWIP_DHCP                   1       // Wi-Fi asks the router for an address
#define LWIP_NETIF_HOSTNAME         1
#define LWIP_ICMP                   1       // ping
#define LWIP_UDP                    1       // DHCP and DNS servers for the PC
#define LWIP_TCP                    1
#define LWIP_IPV4                   1
#define LWIP_IPV6                   0
#define LWIP_SINGLE_NETIF           0       // USB and Wi-Fi
#define LWIP_NETIF_LINK_CALLBACK    0
#define ETH_PAD_SIZE                0
#define LWIP_IP_ACCEPT_UDP_PORT(p)  ((p) == PP_NTOHS(67))   // DHCP before the PC has an address

#define TCP_MSS                     (1500 - 40)
#define TCP_SND_BUF                 (8 * TCP_MSS)
#define TCP_WND                     (8 * TCP_MSS)
#define TCP_SND_QUEUELEN            32

#define LWIP_STATS                  0
#define LWIP_DEBUG                  0
#define LWIP_PROVIDE_ERRNO          1
#define LWIP_RAND()                 (myos_random())
unsigned int myos_random(void);             // net.c

#endif
