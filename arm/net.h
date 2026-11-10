// net.h - networking over the USB cable (Pi = 192.168.7.1)
#ifndef NET_H
#define NET_H
#include <stdint.h>

void net_init(void);
void net_poll(void);            // lwIP timers; call often
int net_link_up(void);
uint32_t net_packets_in(void);
uint32_t net_packets_out(void);
int net_pc_address(char *out);  // "192.168.7.2" once the PC got an address

#endif
