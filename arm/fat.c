// fat.c - just enough FAT16/FAT32 to find one file on the SD card's first
// partition (the Pi's "bootfs") and learn which sectors it occupies.
// Nothing here ever writes to the card.
#include <stdint.h>
#include "fat.h"
#include "sd.h"

static uint8_t sector[512] __attribute__((aligned(4)));

static uint32_t rd16(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8; }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | rd16(p + 2) << 16; }

static int same_name(const uint8_t *entry, const char *name83) {
    for (int i = 0; i < 11; i++) {
        if (entry[i] != (uint8_t)name83[i]) {
            return 0;
        }
    }
    return 1;
}

int fat_find_contiguous(const char *name83, fat_file_t *out) {
    // MBR: first partition entry at byte 446
    if (sd_read(0, sector)) return FAT_ERR_IO;
    if (sector[510] != 0x55 || sector[511] != 0xAA) return FAT_ERR_NOFS;
    uint8_t type = sector[446 + 4];
    uint32_t part = rd32(&sector[446 + 8]);
    int fat32 = (type == 0x0B || type == 0x0C);
    if (!fat32 && type != 0x04 && type != 0x06 && type != 0x0E) return FAT_ERR_NOFS;

    // boot sector of the partition
    if (sd_read(part, sector)) return FAT_ERR_IO;
    if (rd16(&sector[11]) != 512) return FAT_ERR_NOFS;
    uint32_t spc = sector[13];
    uint32_t reserved = rd16(&sector[14]);
    uint32_t fats = sector[16];
    uint32_t root_entries = rd16(&sector[17]);
    uint32_t fat_size = rd16(&sector[22]);
    uint32_t root_cluster = 0;
    if (fat_size == 0) {
        fat_size = rd32(&sector[36]);
        root_cluster = rd32(&sector[44]);
        fat32 = 1;
    }
    if (spc == 0) return FAT_ERR_NOFS;

    uint32_t fat_start = part + reserved;
    uint32_t root_start = fat_start + fats * fat_size;              // FAT16 root
    uint32_t root_sectors = fat32 ? 0 : (root_entries * 32 + 511) / 512;
    uint32_t data_start = root_start + root_sectors;
    uint32_t eoc = fat32 ? 0x0FFFFFF8 : 0xFFF8;

    // look up the next cluster in the FAT
    #define NEXT_CLUSTER(c, result) do {                                    \
        uint32_t off = fat32 ? (c) * 4 : (c) * 2;                           \
        if (sd_read(fat_start + off / 512, sector)) return FAT_ERR_IO;      \
        result = fat32 ? rd32(&sector[off % 512]) & 0x0FFFFFFF               \
                       : rd16(&sector[off % 512]);                          \
    } while (0)

    // walk the root directory
    uint32_t cluster = root_cluster;
    uint32_t dir_sector = fat32 ? data_start + (cluster - 2) * spc : root_start;
    uint32_t sectors_left = fat32 ? spc : root_sectors;
    for (int guard = 0; guard < 4096; guard++) {
        if (sectors_left == 0) {
            if (!fat32) return FAT_ERR_NOTFOUND;
            NEXT_CLUSTER(cluster, cluster);
            if (cluster < 2 || cluster >= eoc) return FAT_ERR_NOTFOUND;
            dir_sector = data_start + (cluster - 2) * spc;
            sectors_left = spc;
        }
        if (sd_read(dir_sector, sector)) return FAT_ERR_IO;
        for (int i = 0; i < 512; i += 32) {
            uint8_t *e = &sector[i];
            if (e[0] == 0x00) return FAT_ERR_NOTFOUND;              // end of directory
            if (e[0] == 0xE5 || e[11] == 0x0F || (e[11] & 0x18)) continue;   // deleted, long name, label, dir
            if (!same_name(e, name83)) continue;

            uint32_t first = rd16(&e[26]) | (fat32 ? rd16(&e[20]) << 16 : 0);
            uint32_t size = rd32(&e[28]);
            if (first < 2 || size < 512) return FAT_ERR_NOTFOUND;
            uint32_t need = (size + spc * 512 - 1) / (spc * 512);  // clusters
            // the file must be one unbroken run of clusters
            uint32_t c = first;
            for (uint32_t n = 1; n < need; n++) {
                uint32_t next;
                NEXT_CLUSTER(c, next);
                if (next != c + 1) return FAT_ERR_FRAGMENTED;
                c = next;
            }
            out->start_lba = data_start + (first - 2) * spc;
            out->sectors = size / 512;
            return 0;
        }
        dir_sector++;
        sectors_left--;
    }
    return FAT_ERR_NOTFOUND;
    #undef NEXT_CLUSTER
}
