// sha256.h - SHA-256 hash (FIPS 180-4), used by SSH
#ifndef SHA256_H
#define SHA256_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t h[8];
    uint64_t total;         // bytes hashed so far
    uint8_t block[64];
    unsigned used;          // bytes waiting in block
} sha256_ctx;

void sha256_init(sha256_ctx *c);
void sha256_update(sha256_ctx *c, const void *data, size_t n);
void sha256_final(sha256_ctx *c, uint8_t out[32]);
void sha256(const void *data, size_t n, uint8_t out[32]);

#endif
