// layout.h - where MyOS keeps things on the disk
//
// MyFS v2 (folders), like Linux:
//   /home            people's files; shells start here
//   /bin             programs from pkg (and Python apps as NAME.py)
//   /usr/bin, /usr/lib   file-tree packages (python3)
//   /lib             libraries (pip's for MicroPython, musl's loader); /lib/firmware: Wi-Fi
//   /etc             settings (wifi.cfg, ssh/)
//   /var/lib/pkg     pkg's own files; /var/cache/pkg: .pkg files sent or downloaded
// MyFS v1 (no folders, MyOS PC disks): everything stays in "/", as before.
#ifndef LAYOUT_H
#define LAYOUT_H

enum { F_PKG_DB, F_PKG_IDX, F_PKG_SRC, F_PKG_FROM, F_WIFI_CFG, F_SSH_KEY, F_SSH_PASSWORD, F_COUNT };

#define HOME_DIR      "/home"
#define PKG_CACHE_DIR "/var/cache/pkg"

int layout_v2(void);                        // the folders above are in use
const char *sys_file(int which);            // F_*: its path (v1: the old name in "/")
void bin_file(const char *name, char *out); // "/bin/NAME" (v1: "NAME")
int find_firmware(const char *name, char *out);     // /lib/firmware/NAME or /NAME: 1 if found
const char *busybox_path(void);             // "/bin/busybox" or the old "/busybox"
// make the folders, and once (a marker in /var/lib/myos) move an older disk's
// files from "/" to where they belong; returns how many moved
int layout_setup(void);

#endif
