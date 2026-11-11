/* dir.c - MyOS C library: folders on MyFS v2 (MyOS Pi)
 *   opendir/readdir/closedir, stat, mkdir, rmdir, chdir, getcwd, unlink
 */
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../myos.h"

/* a MyFS error from the kernel -> an errno value */
int myos_errno(long r) {
    switch (r) {
        case MYOS_ENOENT: return ENOENT;
        case MYOS_EFULL: return ENOSPC;
        case MYOS_ENAME: return EINVAL;
        case MYOS_EEXIST: return EEXIST;
        case MYOS_ENOTDIR: return ENOTDIR;
        case MYOS_EISDIR: return EISDIR;
        case MYOS_ENOTEMPTY: return ENOTEMPTY;
        case MYOS_EV1: return ENOSYS;
        default: return EIO;
    }
}

static int result(long r) {
    if (r >= 0) return 0;
    errno = myos_errno(r);
    return -1;
}

struct myos_dir {
    char path[FILENAME_MAX];
    unsigned long next;
    struct dirent ent;
};

DIR *opendir(const char *path) {
    struct myos_stat st;
    if (strlen(path) >= FILENAME_MAX) {
        errno = EINVAL;
        return NULL;
    }
    if (result(sys_stat(path, &st))) return NULL;
    if (st.type != 2) {
        errno = ENOTDIR;
        return NULL;
    }
    DIR *d = malloc(sizeof *d);
    if (!d) {
        errno = ENOMEM;
        return NULL;
    }
    strcpy(d->path, path);
    d->next = 0;
    return d;
}

struct dirent *readdir(DIR *d) {
    struct myos_dirent e;
    long r = sys_readdir(d->path, d->next, &e);
    if (r <= 0) {
        if (r < 0) errno = myos_errno(r);
        return NULL;
    }
    d->next++;
    memset(&d->ent, 0, sizeof d->ent);
    memcpy(d->ent.d_name, e.name, sizeof d->ent.d_name - 1);
    d->ent.d_type = e.type == 2 ? DT_DIR : DT_REG;
    d->ent.d_size = e.size;
    return &d->ent;
}

void rewinddir(DIR *d) { d->next = 0; }

int closedir(DIR *d) {
    free(d);
    return 0;
}

int stat(const char *path, struct stat *st) {
    struct myos_stat s;
    if (result(sys_stat(path, &s))) return -1;
    memset(st, 0, sizeof *st);
    st->st_mode = s.type == 2 ? S_IFDIR | 0755 : S_IFREG | 0644;
    st->st_size = (off_t)s.size;
    return 0;
}

int mkdir(const char *path, mode_t mode) {
    (void)mode;
    return result(sys_mkdir(path));
}

int rmdir(const char *path) { return result(sys_rmdir(path)); }
int chdir(const char *path) { return result(sys_chdir(path)); }
int unlink(const char *path) { return remove(path); }

char *getcwd(char *buf, size_t size) {
    if (sys_getcwd(buf, size) < 0) {
        errno = ERANGE;
        return NULL;
    }
    return buf;
}
