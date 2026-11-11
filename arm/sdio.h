// sdio.h - SDIO on the EMMC controller: the Wi-Fi chip's bus
#ifndef SDIO_H
#define SDIO_H
#include <stdint.h>

int sdio_init(void);        // power the Wi-Fi chip up, find it (CMD5/3/7), 4-bit bus. 0 = ok
int sdio_ready(void);
int sdio_cmd52(int write, int fn, uint32_t addr, uint8_t data, uint8_t *out);  // one byte
// several bytes: block mode for whole blocks, byte mode for the rest;
// incr = 0 keeps the address (a FIFO, like function 2)
int sdio_cmd53(int write, int fn, uint32_t addr, int incr, void *buf, uint32_t len);
int sdio_set_blocksize(int fn, uint32_t size);
int sdio_enable_function(int fn);       // set IOEx, wait for IORx
uint32_t sdio_clock_hz(void);           // SD bus clock
int sdio_high_speed(void);              // 50 MHz if the card allows it; 1 = switched
const char *sdio_error(void);           // what went wrong last
uint32_t sdio_error_flags(void);        // interrupt flags then (debugging)
uint32_t sdio_error_status(void);

#endif
