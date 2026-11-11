// wifi.h - Wi-Fi on the Pi Zero 2 W (CYW43436 / 43430 over SDIO)
#ifndef WIFI_H
#define WIFI_H
#include <stdint.h>
#include "../csdk/myos_info.h"

void wifi_command(const char *arg);     // the shell's 'wifi ...'
void ping_command(const char *arg);     // the shell's 'ping ADDRESS'
void wifi_poll(void);                   // frames from the chip (net.c calls it often)
void wifi_boot(void);                   // at boot: connect to a saved network in the background
int64_t wifi_syscall(uint64_t op, uint64_t a, uint64_t b);   // SYS_WIFI (user.c)

#endif
