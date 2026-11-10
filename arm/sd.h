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

#endif
