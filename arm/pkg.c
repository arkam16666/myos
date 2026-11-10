// pkg.c - the package manager, same package format as MyOS PC:
//   NAME.pkg in MyFS = 128-byte header ("MYPK", u32 format, name[16],
//   version[16], description[64], u32 program size) + the program.
//   "Installed" means a program file NAME exists.
#include <stdint.h>
#include <string.h>
#include "console.h"
#include "myfs.h"
#include "pkg.h"
#include "print.h"
#include "user.h"

#define HDR_SIZE  128
#define P_NAME    8
#define P_VERSION 24
#define P_DESC    40
#define P_SIZE    104
#define PKG_BUF   ((uint8_t *)0x01000000)   // free RAM while installing

static uint8_t hdr[HDR_SIZE];

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static int valid_prog_name(const char *s) {
    size_t n = strlen(s);
    if (n == 0 || n > 15) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return 0;
    }
    return 1;
}

static int ends_with_pkg(const char *s) {
    size_t n = strlen(s);
    return n > 4 && strcmp(s + n - 4, ".pkg") == 0;
}

static int read_header(const char *file) {
    if (myfs_read(file, 0, hdr, HDR_SIZE) != HDR_SIZE || memcmp(hdr, "MYPK", 4) != 0) return 0;
    hdr[P_NAME + 15] = hdr[P_VERSION + 15] = hdr[P_DESC + 63] = 0;   // make sure strings end
    return 1;
}

static void pad_to(int col, int used) {
    for (; used < col; used++) console_putc(' ');
}

static void pkg_list(void) {
    char file[MYFS_NAME_MAX + 1];
    uint32_t size;
    int found = 0;
    console_puts("  NAME          VERSION  STATUS      DESCRIPTION\n");
    for (int i = 0; myfs_entry(i, file, &size) == 0; i++) {
        if (!ends_with_pkg(file) || !read_header(file)) continue;
        found++;
        const char *name = (const char *)&hdr[P_NAME];
        console_puts("  ");
        console_puts(name);
        pad_to(14, (int)strlen(name));
        console_puts((const char *)&hdr[P_VERSION]);
        pad_to(9, (int)strlen((const char *)&hdr[P_VERSION]));
        int installed = myfs_size(name) >= 0;
        console_puts(installed ? "installed   " : "available   ");
        console_puts((const char *)&hdr[P_DESC]);
        console_putc('\n');
    }
    if (!found) console_puts("  (no packages on the disk)\n");
}

// "NAME" -> file "NAME.pkg"; 0 (with a message) if there is no such package
static int find_pkg(const char *name, char *file) {
    if (!valid_prog_name(name)) {
        console_puts("Usage: pkg list | pkg info NAME | pkg install NAME | pkg remove NAME\n");
        return 0;
    }
    strcpy(file, name);
    strcat(file, ".pkg");
    if (myfs_size(file) < 0) {
        console_puts("No such package: ");
        console_puts(file);
        console_puts(". Try 'pkg list'.\n");
        return 0;
    }
    return 1;
}

static void pkg_info(const char *name) {
    char file[MYFS_NAME_MAX + 1];
    if (!find_pkg(name, file)) return;
    if (!read_header(file)) {
        console_puts("That package file is damaged or not a package.\n");
        return;
    }
    console_puts("Package:     ");
    console_puts((const char *)&hdr[P_NAME]);
    console_puts("\nVersion:     ");
    console_puts((const char *)&hdr[P_VERSION]);
    console_puts("\nDescription: ");
    console_puts((const char *)&hdr[P_DESC]);
    console_puts("\nSize:        ");
    print_dec(rd32(&hdr[P_SIZE]));
    console_puts(" bytes\nInstalled:   ");
    console_puts(myfs_size((const char *)&hdr[P_NAME]) >= 0 ? "yes\n" : "no\n");
}

static void pkg_install(const char *name) {
    char file[MYFS_NAME_MAX + 1];
    if (!find_pkg(name, file)) return;
    int32_t total = myfs_size(file);
    const char *bad = "That package file is damaged or not a package.\n";
    if (total < HDR_SIZE + 16 || (uint32_t)total > USER_MAX_SIZE + HDR_SIZE ||
        myfs_read(file, 0, PKG_BUF, (uint32_t)total) != total || memcmp(PKG_BUF, "MYPK", 4) != 0) {
        console_puts(bad);
        return;
    }
    uint32_t size = rd32(PKG_BUF + P_SIZE);
    if (size < 16 || size + HDR_SIZE > (uint32_t)total) {
        console_puts(bad);
        return;
    }
    int arch = program_header_arch(PKG_BUF + HDR_SIZE, size);
    if (arch == MYEX_ARCH_X86) {
        console_puts("That package is for MyOS PC (x86), not for the Pi.\n");
        return;
    }
    if (arch != MYEX_ARCH_ARM64) {
        console_puts(bad);
        return;
    }
    char prog[16];
    memcpy(prog, PKG_BUF + P_NAME, 15);
    prog[15] = 0;
    PKG_BUF[P_VERSION + 15] = 0;
    if (!valid_prog_name(prog)) {                   // e.g. no "readme.txt" overwrites
        console_puts(bad);
        return;
    }
    console_puts("Installing ");
    console_puts(prog);
    console_putc(' ');
    console_puts((const char *)PKG_BUF + P_VERSION);
    console_puts(" ...\n");
    if (myfs_write(prog, PKG_BUF + HDR_SIZE, size)) {
        console_puts("Disk is full or a disk error happened.\n");
        return;
    }
    console_puts("Done. Type '");
    console_puts(prog);
    console_puts("' to run it.\n");
}

static void pkg_remove(const char *name) {
    if (!valid_prog_name(name)) {
        console_puts("Usage: pkg remove NAME\n");
        return;
    }
    if (myfs_remove(name) == 0) {
        console_puts("Removed ");
        console_puts(name);
        console_putc('\n');
    } else {
        console_puts(name);
        console_puts(" is not installed.\n");
    }
}

void pkg_command(const char *args) {
    if (!myfs_mounted()) {
        console_puts("No file system. Type 'sd' for details.\n");
        return;
    }
    char word[16] = {0};
    int n = 0;
    while (*args && *args != ' ' && n < 15) word[n++] = *args++;
    while (*args == ' ') args++;
    if (strcmp(word, "list") == 0) pkg_list();
    else if (strcmp(word, "info") == 0) pkg_info(args);
    else if (strcmp(word, "install") == 0) pkg_install(args);
    else if (strcmp(word, "remove") == 0) pkg_remove(args);
    else console_puts("Usage: pkg list | pkg info NAME | pkg install NAME | pkg remove NAME\n");
}
