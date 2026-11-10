// myfs.h - the MyFS file system (same format as MyOS PC and tools/myfs.py)
#ifndef MYFS_H
#define MYFS_H
#include <stdint.h>

#define MYFS_MAX_FILES 64
#define MYFS_NAME_MAX  47

enum {
    MYFS_ERR_IO = -1,
    MYFS_ERR_NOFS = -2,         // no MyFS here
    MYFS_ERR_NOTFOUND = -3,
    MYFS_ERR_FULL = -4,         // disk or directory full
    MYFS_ERR_NAME = -5,         // bad file name
};

// where MyFS lives: sectors [start, start + sectors) of a block device
typedef struct {
    uint32_t start;
    uint32_t sectors;
    int (*read)(uint32_t lba, void *buf);
    int (*write)(uint32_t lba, const void *buf);
} myfs_dev_t;

int myfs_mount(const myfs_dev_t *dev);
int myfs_format(const myfs_dev_t *dev);
int myfs_mounted(void);
int myfs_count(void);
uint32_t myfs_free_kb(void);
int myfs_entry(int index, char *name, uint32_t *size);     // index-th file; -1 = no more
int myfs_valid_name(const char *name);
int32_t myfs_size(const char *name);
int myfs_read(const char *name, uint32_t offset, void *out, uint32_t len);  // bytes read
int myfs_write(const char *name, const void *data, uint32_t size);         // create/replace
int myfs_remove(const char *name);

#endif
