// layout.c - where MyOS keeps things (see layout.h), and moving an older
// disk's files out of "/" into that layout, once.
#include <stdint.h>
#include <string.h>
#include "layout.h"
#include "myfs.h"

static const char *const files[F_COUNT][2] = {     // { MyFS v2 path, old name in "/" }
    [F_PKG_DB]       = { "/var/lib/pkg/pkg.db",   "pkg.db" },
    [F_PKG_IDX]      = { "/var/lib/pkg/pkg.idx",  "pkg.idx" },
    [F_PKG_SRC]      = { "/var/lib/pkg/pkg.src",  "pkg.src" },
    [F_PKG_FROM]     = { "/var/lib/pkg/pkg.from", "pkg.from" },
    [F_WIFI_CFG]     = { "/etc/wifi.cfg",         "wifi.cfg" },
    [F_SSH_KEY]      = { "/etc/ssh/ssh_host_ed25519.key", "ssh_host_ed25519.key" },
    [F_SSH_PASSWORD] = { "/etc/ssh/ssh_password", "ssh_password" },
};

static const char *const dirs[] = {
    "/bin", "/etc", "/etc/ssh", "/home", "/lib", "/lib/firmware", "/tmp", "/usr", "/usr/bin", "/usr/lib",
    "/var", "/var/lib", "/var/lib/pkg", "/var/lib/myos", "/var/cache", PKG_CACHE_DIR,
};
#define MARKER "/var/lib/myos/layout"

int layout_v2(void) { return myfs_version() == 2; }

const char *sys_file(int which) {
    return files[which][layout_v2() ? 0 : 1];
}

void bin_file(const char *name, char *out) {
    strcpy(out, layout_v2() ? "/bin/" : "");
    strcat(out, name);
}

int find_firmware(const char *name, char *out) {
    if (layout_v2()) {
        strcpy(out, "/lib/firmware/");
        strcat(out, name);
        if (myfs_stat(out, 0) == MYFS_FILE) return 1;
    }
    strcpy(out, "/");
    strcat(out, name);
    return myfs_stat(out, 0) == MYFS_FILE;
}

const char *busybox_path(void) {
    return layout_v2() && myfs_stat("/bin/busybox", 0) == MYFS_FILE ? "/bin/busybox" : "/busybox";
}

static int ends_with(const char *s, const char *tail) {
    size_t n = strlen(s), t = strlen(tail);
    return n >= t && strcmp(s + n - t, tail) == 0;
}

// is NAME a package pkg.db lists (the old disk kept programs in "/")?
static int in_db(const char *db, const char *name) {
    size_t n = strlen(name);
    for (const char *p = db; *p; ) {
        if (!strncmp(p, name, n) && p[n] == ' ') return 1;
        while (*p && *p != '\n') p++;
        if (*p) p++;
    }
    return 0;
}

static int move_to(const char *name, const char *dir) {    // "/NAME" -> "DIR/NAME"
    char from[MYFS_PATH_MAX], to[MYFS_PATH_MAX];
    strcpy(from, "/");
    strcat(from, name);
    strcpy(to, dir);
    strcat(to, "/");
    strcat(to, name);
    if (myfs_stat(to, 0) >= 0) return 0;                   // (already there: leave both)
    return myfs_rename(from, to) == 0;
}

int layout_setup(void) {
    if (!layout_v2()) return 0;
    for (unsigned i = 0; i < sizeof dirs / sizeof dirs[0]; i++) myfs_mkdir(dirs[i]);
    if (myfs_stat(MARKER, 0) == MYFS_FILE) return 0;
    int moved = 0;
    // the system's own files
    for (int i = 0; i < F_COUNT; i++) {
        char old[MYFS_PATH_MAX];
        strcpy(old, "/");
        strcat(old, files[i][1]);
        if (myfs_stat(old, 0) == MYFS_FILE && myfs_stat(files[i][0], 0) < 0 && !myfs_rename(old, files[i][0]))
            moved++;
    }
    // what is left in "/": list it first (moving changes the order)
    static char names[160][MYFS_NAME_MAX + 1];
    int n = 0;
    uint32_t size;
    char name[MYFS_NAME_MAX + 1];
    for (int i = 0; n < 160 && myfs_readdir("/", i, name, &size) > 0; i++) {
        if (myfs_stat(name, 0) == MYFS_FILE) strcpy(names[n++], name);
    }
    static char db[4096];
    int32_t dl = myfs_size(files[F_PKG_DB][0]);
    db[0] = 0;
    if (dl > 0 && dl < (int32_t)sizeof db && myfs_read(files[F_PKG_DB][0], 0, db, (uint32_t)dl) == dl) db[dl] = 0;
    for (int i = 0; i < n; i++) {
        const char *f = names[i];
        char base[MYFS_NAME_MAX + 1];
        strcpy(base, f);
        if (ends_with(base, ".py")) base[strlen(base) - 3] = 0;
        const char *dir;
        if (!strncmp(f, "brcmfmac", 8)) dir = "/lib/firmware";         // Wi-Fi firmware
        else if (ends_with(f, ".pkg")) dir = PKG_CACHE_DIR;             // package files
        else if (in_db(db, base)) dir = "/bin";                         // programs and apps from pkg
        else dir = HOME_DIR;                                            // people's files
        moved += move_to(f, dir);
    }
    myfs_write(MARKER, "1\n", 2);
    return moved;
}
