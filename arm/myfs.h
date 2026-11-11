// myfs.h - the MyFS file system
//
// Two formats (both handled by arm/myfs.c and tools/myfs.py):
//   v1 "MYFS0001": flat, 64 files, 47-char names (MyOS PC uses this one)
//   v2 "MYFS0002": folders, as many files as fit, 63-char names, free space
//                  bitmap (space comes back when files are deleted)
// A v1 volume keeps working as before; 'fsupgrade' turns it into v2 in place
// (myfs_upgrade), keeping every file.
//
// Paths: "/apps/snake.py", "notes.txt" (no leading '/' = from the top
// folder), "." and ".." work. The shell and the system calls turn a path
// relative to the current folder into an absolute one with myfs_normalize.
#ifndef MYFS_H
#define MYFS_H
#include <stdint.h>

#define MYFS_MAX_FILES    64        // v1 only: size of its directory
#define MYFS_V1_NAME_MAX  47        // v1 only
#define MYFS_NAME_MAX     63        // longest name of one file or folder (v2)
#define MYFS_PATH_MAX     256       // longest path, with its terminating 0
#define MYFS_DEPTH_MAX    32        // folders inside folders, at most

enum {
    MYFS_ERR_IO = -1,
    MYFS_ERR_NOFS = -2,         // no MyFS here
    MYFS_ERR_NOTFOUND = -3,
    MYFS_ERR_FULL = -4,         // disk or directory full
    MYFS_ERR_NAME = -5,         // bad file name / path
    MYFS_ERR_EXISTS = -6,       // there is already something with that name
    MYFS_ERR_NOTDIR = -7,       // a part of the path is a file, not a folder
    MYFS_ERR_ISDIR = -8,        // it's a folder (rmdir / rename are for those)
    MYFS_ERR_NOTEMPTY = -9,     // rmdir: the folder still has something in it
    MYFS_ERR_V1 = -10,          // folders need MyFS v2 ('fsupgrade')
    MYFS_ERR_CORRUPT = -11,     // the file system is damaged here ('fsck')
};

enum { MYFS_FILE = 1, MYFS_DIR = 2 };   // what myfs_stat / myfs_readdir return

// where MyFS lives: sectors [start, start + sectors) of a block device
typedef struct {
    uint32_t start;
    uint32_t sectors;
    int (*read)(uint32_t lba, void *buf);
    int (*write)(uint32_t lba, const void *buf);
} myfs_dev_t;

int myfs_mount(const myfs_dev_t *dev);
int myfs_format(const myfs_dev_t *dev);         // a new, empty v2 file system
int myfs_mounted(void);
int myfs_version(void);                         // 1, 2, or 0 = nothing mounted
int myfs_count(void);                           // files on the whole volume
int myfs_dir_count(void);                       // folders (not counting the top one)
uint32_t myfs_free_kb(void);
uint32_t myfs_total_kb(void);
const char *myfs_error(int err);                // "No such file or folder", ...

// path helpers
int myfs_normalize(const char *cwd, const char *path, char *out);  // -> "/a/b" in out[MYFS_PATH_MAX]
int myfs_valid_name(const char *path);          // a path this volume can store?

// files (paths allowed everywhere)
int myfs_entry(int index, char *name, uint32_t *size);     // index-th file in "/"; -1 = no more
int myfs_file_entry(const char *dir, int index, char *name, uint32_t *size);  // same, any folder
int32_t myfs_size(const char *name);
int myfs_read(const char *name, uint32_t offset, void *out, uint32_t len);  // bytes read
int myfs_write(const char *name, const void *data, uint32_t size);         // create/replace
int myfs_remove(const char *name);              // a file (folders: myfs_rmdir)
int myfs_copy(const char *from, const char *to);    // file -> new/replaced file

// folders
int myfs_stat(const char *path, uint32_t *size);    // MYFS_FILE / MYFS_DIR, or an error
// index-th thing in folder dir: MYFS_FILE / MYFS_DIR, 0 = no more, or an error.
// name needs MYFS_NAME_MAX + 1 bytes; size may be 0.
int myfs_readdir(const char *dir, int index, char *name, uint32_t *size);
int myfs_mkdir(const char *path);
int myfs_rmdir(const char *path);               // only empty folders
int myfs_rename(const char *from, const char *to);  // rename or move a file or folder

// checking and upgrading
typedef struct {
    uint32_t files, dirs;
    uint32_t used_kb;
    uint32_t leaked_kb;         // marked used but nobody had it: given back
    uint32_t missing_kb;        // in use but marked free: fixed
    uint32_t damaged;           // entries with bad or shared sectors (listed below)
    uint32_t too_deep;          // folders nested deeper than the checker goes
    char damaged_path[4][80];
} myfs_check_t;
int myfs_check(int fix, myfs_check_t *report);  // v2: walk everything, repair the bitmap
const myfs_check_t *myfs_mount_check(void);     // what the check at mount time found
int myfs_upgrade(void);                         // v1 -> v2 in place: files kept, or an error

#endif
