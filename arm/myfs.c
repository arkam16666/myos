// myfs.c - the MyFS file system in C, byte-for-byte the same layout as
// MyOS PC (kernel.asm) and tools/myfs.py:
//   sector 0     superblock: "MYFS0001", u32 total sectors, u32 next free sector
//   sector 1-8   directory: 64 entries of name[48], start, size, alloc, flags
//   sector 16..  file data, each file one run of sectors
// It lives inside a region of a block device (here: myfs.img on the SD card)
// and never reads or writes outside that region.
#include <stdint.h>
#include <string.h>
#include "myfs.h"

#define DIR_START   1
#define DIR_SECTORS 8
#define DATA_START  16
#define ENTRY_SIZE  64

static myfs_dev_t dev;
static uint8_t super[512] __attribute__((aligned(4)));
static uint8_t dir[DIR_SECTORS * 512] __attribute__((aligned(4)));
static uint8_t buf[512] __attribute__((aligned(4)));
static int mounted;

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static void wr32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

static uint8_t *entry(int i) { return &dir[i * ENTRY_SIZE]; }
static int used(int i) { return rd32(entry(i) + 60) & 1; }

static int dev_read(uint32_t n, void *b) {
    return n < dev.sectors ? dev.read(dev.start + n, b) : -1;
}
static int dev_write(uint32_t n, const void *b) {
    return n < dev.sectors ? dev.write(dev.start + n, b) : -1;
}

static int flush(void) {
    if (dev_write(0, super)) return MYFS_ERR_IO;
    for (int i = 0; i < DIR_SECTORS; i++) {
        if (dev_write(DIR_START + i, &dir[i * 512])) return MYFS_ERR_IO;
    }
    return 0;
}

int myfs_mount(const myfs_dev_t *d) {
    dev = *d;
    mounted = 0;
    if (dev_read(0, super)) return MYFS_ERR_IO;
    if (memcmp(super, "MYFS0001", 8) != 0) return MYFS_ERR_NOFS;
    for (int i = 0; i < DIR_SECTORS; i++) {
        if (dev_read(DIR_START + i, &dir[i * 512])) return MYFS_ERR_IO;
    }
    mounted = 1;
    return 0;
}

int myfs_mounted(void) { return mounted; }

int myfs_format(const myfs_dev_t *d) {
    dev = *d;
    memset(super, 0, sizeof super);
    memset(dir, 0, sizeof dir);
    memcpy(super, "MYFS0001", 8);
    wr32(super + 8, dev.sectors);
    wr32(super + 12, DATA_START);
    int r = flush();
    mounted = (r == 0);
    return r;
}

uint32_t myfs_free_kb(void) {
    return (rd32(super + 8) - rd32(super + 12)) / 2;
}

int myfs_count(void) {
    int n = 0;
    for (int i = 0; i < MYFS_MAX_FILES; i++) n += used(i);
    return n;
}

int myfs_entry(int index, char *name, uint32_t *size) {
    for (int i = 0; i < MYFS_MAX_FILES; i++) {
        if (!used(i)) continue;
        if (index-- == 0) {
            memcpy(name, entry(i), MYFS_NAME_MAX);
            name[MYFS_NAME_MAX] = 0;
            *size = rd32(entry(i) + 52);
            return 0;
        }
    }
    return -1;
}

static int find(const char *name) {
    for (int i = 0; i < MYFS_MAX_FILES; i++) {
        if (used(i) && strncmp((const char *)entry(i), name, MYFS_NAME_MAX) == 0) {
            return i;
        }
    }
    return -1;
}

int myfs_valid_name(const char *name) {
    size_t n = strlen(name);
    if (n == 0 || n > MYFS_NAME_MAX) return 0;
    for (size_t i = 0; i < n; i++) {
        if (name[i] <= ' ' || name[i] > '~') return 0;
    }
    return 1;
}

int32_t myfs_size(const char *name) {
    if (!mounted) return MYFS_ERR_NOFS;
    int i = find(name);
    return i < 0 ? MYFS_ERR_NOTFOUND : (int32_t)rd32(entry(i) + 52);
}

int myfs_read(const char *name, uint32_t offset, void *out, uint32_t len) {
    if (!mounted) return MYFS_ERR_NOFS;
    int i = find(name);
    if (i < 0) return MYFS_ERR_NOTFOUND;
    uint32_t start = rd32(entry(i) + 48), size = rd32(entry(i) + 52);
    if (offset >= size) return 0;
    if (len > size - offset) len = size - offset;
    uint8_t *o = out;
    uint32_t done = 0;
    while (done < len) {
        uint32_t pos = offset + done;
        if (dev_read(start + pos / 512, buf)) return MYFS_ERR_IO;
        uint32_t n = 512 - pos % 512;
        if (n > len - done) n = len - done;
        memcpy(o + done, buf + pos % 512, n);
        done += n;
    }
    return (int)done;
}

int myfs_write(const char *name, const void *data, uint32_t size) {
    if (!mounted) return MYFS_ERR_NOFS;
    if (!myfs_valid_name(name)) return MYFS_ERR_NAME;
    uint32_t need = (size + 511) / 512;
    int i = find(name);
    if (i < 0) {
        for (i = 0; i < MYFS_MAX_FILES && used(i); i++) {
        }
        if (i == MYFS_MAX_FILES) return MYFS_ERR_FULL;
        memset(entry(i), 0, ENTRY_SIZE);
        memcpy(entry(i), name, strlen(name));
        wr32(entry(i) + 56, 0);
    }
    uint8_t *e = entry(i);
    if (need > rd32(e + 56)) {                      // doesn't fit: take new space
        uint32_t next = rd32(super + 12);
        if (next + need > rd32(super + 8)) return MYFS_ERR_FULL;
        wr32(e + 48, next);
        wr32(e + 56, need);
        wr32(super + 12, next + need);
    }
    const uint8_t *d = data;
    uint32_t start = rd32(e + 48);
    for (uint32_t s = 0; s < need; s++) {
        uint32_t n = size - s * 512;
        if (n > 512) n = 512;
        memset(buf, 0, 512);
        memcpy(buf, d + s * 512, n);
        if (dev_write(start + s, buf)) return MYFS_ERR_IO;
    }
    wr32(e + 52, size);
    wr32(e + 60, 1);
    return flush();
}

int myfs_remove(const char *name) {
    if (!mounted) return MYFS_ERR_NOFS;
    int i = find(name);
    if (i < 0) return MYFS_ERR_NOTFOUND;
    wr32(entry(i) + 60, 0);
    return flush();
}
