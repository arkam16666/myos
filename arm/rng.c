// rng.c - random numbers for cryptography.
//
// The BCM2835/2837 has a hardware random number generator. Its output and
// the CPU timer are mixed through SHA-256, so a single weak source can't
// make the result predictable. (The Pi 4 has a different generator that
// MyOS doesn't drive yet: there only the timer is used - not for real keys.)
#include <string.h>
#include "rng.h"
#include "sha256.h"
#include "timer.h"

#ifndef PI4
#define RNG_BASE        0x3F104000UL
#define RNG_CTRL        (*(volatile uint32_t *)(RNG_BASE + 0x00))
#define RNG_STATUS      (*(volatile uint32_t *)(RNG_BASE + 0x04))
#define RNG_DATA        (*(volatile uint32_t *)(RNG_BASE + 0x08))
#define RNG_INT_MASK    (*(volatile uint32_t *)(RNG_BASE + 0x10))
#endif

static uint8_t pool[32];
static uint64_t counter;
static int hw_ok;

static int hw_word(uint32_t *out) {
#ifndef PI4
    uint64_t end = millis() + 50;
    while ((RNG_STATUS >> 24) == 0) {           // words ready in the FIFO
        if (millis() > end) return 0;
    }
    *out = RNG_DATA;
    return 1;
#else
    (void)out;
    return 0;
#endif
}

void rng_init(void) {
#ifndef PI4
    RNG_STATUS = 0x40000;                       // throw away the first numbers (warm-up)
    RNG_INT_MASK |= 1;                          // no interrupts
    RNG_CTRL |= 1;                              // start
#endif
    sha256_ctx c;
    sha256_init(&c);
    hw_ok = 1;
    for (int i = 0; i < 64; i++) {              // 2048 bits from the hardware
        uint32_t w = 0;
        if (!hw_word(&w)) hw_ok = 0;
        uint64_t t = timer_count();
        sha256_update(&c, &w, sizeof w);
        sha256_update(&c, &t, sizeof t);
    }
    sha256_final(&c, pool);
}

int rng_hardware(void) {
    return hw_ok;
}

void rng_bytes(void *out, size_t n) {
    uint8_t *p = out;
    while (n) {
        sha256_ctx c;
        uint8_t block[32];
        uint32_t w[4] = { 0 };
        for (int i = 0; i < 4; i++) hw_word(&w[i]);
        uint64_t t = timer_count();
        counter++;
        sha256_init(&c);                        // output = SHA256(pool, counter, fresh input)
        sha256_update(&c, pool, sizeof pool);
        sha256_update(&c, &counter, sizeof counter);
        sha256_update(&c, w, sizeof w);
        sha256_update(&c, &t, sizeof t);
        sha256_final(&c, block);
        sha256_init(&c);                        // new pool, so old output can't be recomputed
        sha256_update(&c, block, sizeof block);
        sha256_update(&c, pool, sizeof pool);
        sha256_final(&c, pool);
        size_t take = n < 32 ? n : 32;
        memcpy(p, block, take);
        memset(block, 0, sizeof block);
        p += take;
        n -= take;
    }
}
