// rng.h - random numbers for cryptography (the Pi's hardware generator)
#ifndef RNG_H
#define RNG_H
#include <stddef.h>
#include <stdint.h>

void rng_init(void);
void rng_bytes(void *out, size_t n);
int rng_hardware(void);         // 1 if the hardware generator is working

#endif
