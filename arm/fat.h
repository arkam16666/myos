// fat.h - find a file on the SD card's FAT partition
#ifndef FAT_H
#define FAT_H
#include <stdint.h>

typedef struct {
    uint32_t start_lba;     // first sector of the file on the card
    uint32_t sectors;       // file size in sectors
} fat_file_t;

enum {
    FAT_ERR_IO = -1,
    FAT_ERR_NOFS = -2,          // no FAT16/FAT32 first partition
    FAT_ERR_NOTFOUND = -3,
    FAT_ERR_FRAGMENTED = -4,    // file is not in one piece
};

// name83: 11 characters, e.g. "MYFS    IMG"
int fat_find_contiguous(const char *name83, fat_file_t *out);

#endif
