/* sys/stat.h - MyOS C library: file information and folders (MyFS v2, MyOS Pi) */
#ifndef MYOS_SYS_STAT_H
#define MYOS_SYS_STAT_H

typedef unsigned int mode_t;
typedef long off_t;

#define S_IFMT   0170000
#define S_IFDIR  0040000
#define S_IFREG  0100000
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)

struct stat {
    mode_t st_mode;             /* S_IFDIR or S_IFREG, plus rwx bits (always 0755 / 0644) */
    off_t st_size;
    long st_mtime;              /* always 0: MyFS keeps no times */
};

int stat(const char *path, struct stat *st);
int mkdir(const char *path, mode_t mode);    /* mode is ignored */

#endif
