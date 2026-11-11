/* dirent.h - MyOS C library: reading folders (MyFS v2, MyOS Pi) */
#ifndef MYOS_DIRENT_H
#define MYOS_DIRENT_H

#define DT_UNKNOWN 0
#define DT_DIR     4
#define DT_REG     8

struct dirent {
    unsigned long d_ino;        /* always 0 */
    unsigned char d_type;       /* DT_REG or DT_DIR */
    unsigned long d_size;       /* MyOS extra: size in bytes */
    char d_name[64];
};

typedef struct myos_dir DIR;

DIR *opendir(const char *path);
struct dirent *readdir(DIR *d);
int closedir(DIR *d);
void rewinddir(DIR *d);

#endif
