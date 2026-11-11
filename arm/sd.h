// sd.h - SD card, 512-byte sectors
#ifndef SD_H
#define SD_H
#include <stdint.h>

int sd_init(void);                          // 0 = card ready, negative = step that failed
int sd_read(uint32_t lba, void *buf);       // buf: 512 bytes, 4-byte aligned. 0 = ok
int sd_write(uint32_t lba, const void *buf);
int sd_ready(void);
int sd_is_high_capacity(void);
uint64_t sd_sectors(void);
uint32_t sd_base_clock(void);
const char *sd_controller(void);           // "SDHOST" (Wi-Fi can have EMMC) or "EMMC"

// inside the driver: sdhost.c (tried first) and sd.c (EMMC, the fallback)
int sdhost_init(void);
int sdhost_read(uint32_t lba, void *buf);
int sdhost_write(uint32_t lba, const void *buf);
int sdhost_is_high_capacity(void);
uint64_t sdhost_sectors(void);
uint32_t sdhost_clock(void);
uint64_t sd_capacity_from_csd(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3);

#endif
