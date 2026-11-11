// pkg.c - the package manager, like apt:
//   pkg update             get the list of packages from the PC (tools/repo.py)
//   pkg search [WORD]      what the repository has
//   pkg install NAME...    from NAME.pkg on the disk, or downloaded from the PC
//   pkg remove NAME        pkg upgrade        pkg list        pkg info NAME
//
// A package (NAME.pkg) = 128-byte header + content:
//   0 "MYPK", 4 u32 kind, 8 name[16], 24 version[16], 40 description[64],
//   104 u32 content size.
//   kind 1 = a program ("MYEX" ...)      -> installed as file NAME
//   kind 2 = a Python app (.py source)   -> installed as NAME.py, needs 'python'
// Files the package manager keeps in MyFS:
//   pkg.idx  the repository list from 'pkg update'
//            one line per package: name version kind size sha256 description
//   pkg.db   installed packages: one "name version" line each
#include <stdint.h>
#include <string.h>
#include "console.h"
#include "http.h"
#include "myfs.h"
#include "pkg.h"
#include "print.h"
#include "sha256.h"
#include "user.h"

#define HDR_SIZE  128
#define P_KIND    4
#define P_NAME    8
#define P_VERSION 24
#define P_DESC    40
#define P_SIZE    104
#define KIND_PROGRAM 1
#define KIND_PYTHON  2
#define PKG_BUF   ((uint8_t *)0x01000000)   // free RAM while installing
#define PKG_MAX   (USER_MAX_SIZE + HDR_SIZE)
#define IDX_BUF   ((char *)0x01800000)      // the repository list while working on it
#define IDX_MAX   (64 * 1024)
#define IDX_FILE  "pkg.idx"
#define DB_FILE   "pkg.db"

static const char usage[] =
    "Usage: pkg list | update | search [WORD] | info NAME | install NAME...\n"
    "       pkg remove NAME | upgrade\n";

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

static void pad_to(int col, int used) {
    for (; used < col; used++) console_putc(' ');
}

static void py_name(const char *name, char *out) {      // "snake" -> "snake.py"
    strcpy(out, name);
    strcat(out, ".py");
}

// ------------------------------------------------ pkg.db: what is installed
static char db[4096];
static uint32_t db_len;

static void db_load(void) {
    int32_t n = myfs_size(DB_FILE);
    db_len = 0;
    if (n > 0 && n < (int32_t)sizeof db && myfs_read(DB_FILE, 0, db, (uint32_t)n) == n) db_len = (uint32_t)n;
    db[db_len] = 0;
}

static char *db_find(const char *name) {                // start of the "name version" line
    size_t n = strlen(name);
    for (char *p = db; *p; ) {
        if (strncmp(p, name, n) == 0 && p[n] == ' ') return p;
        while (*p && *p != '\n') p++;
        if (*p) p++;
    }
    return 0;
}

static int db_version(const char *name, char *out) {    // 1 if installed through pkg
    db_load();
    char *p = db_find(name);
    if (!p) return 0;
    p += strlen(name) + 1;
    int i = 0;
    while (*p && *p != '\n' && i < 15) out[i++] = *p++;
    out[i] = 0;
    return 1;
}

static void db_remove_line(const char *name) {
    char *p = db_find(name);
    if (!p) return;
    char *e = p;
    while (*e && *e != '\n') e++;
    if (*e) e++;
    memmove(p, e, strlen(e) + 1);
    db_len = (uint32_t)strlen(db);
}

static void db_save(void) {
    if (db_len) myfs_write(DB_FILE, db, db_len);
    else myfs_remove(DB_FILE);
}

static void db_set(const char *name, const char *version) {
    db_load();
    db_remove_line(name);
    if (db_len + strlen(name) + strlen(version) + 3 < sizeof db) {
        strcat(db, name);
        strcat(db, " ");
        strcat(db, version);
        strcat(db, "\n");
        db_len = (uint32_t)strlen(db);
    }
    db_save();
}

static void db_unset(const char *name) {
    db_load();
    db_remove_line(name);
    db_save();
}

static int installed(const char *name) {
    char v[16], py[24];
    py_name(name, py);
    return myfs_size(name) >= 0 || (db_version(name, v) && myfs_size(py) >= 0);
}

// ------------------------------------------------ pkg.idx: the repository list
typedef struct {
    char name[16], version[16];
    int kind;
    uint32_t size;
    char sha[65];
    char desc[64];
} entry_t;

static uint32_t idx_len;

static int idx_load(void) {                             // 0 = no list yet
    int32_t n = myfs_size(IDX_FILE);
    if (n <= 0 || n >= IDX_MAX || myfs_read(IDX_FILE, 0, IDX_BUF, (uint32_t)n) != n) return 0;
    IDX_BUF[n] = 0;
    idx_len = (uint32_t)n;
    return 1;
}

static const char *word(const char *p, char *out, int max) {  // next space-separated word
    int i = 0;
    while (*p == ' ') p++;
    while (*p && *p != ' ' && *p != '\n') {
        if (i < max - 1) out[i++] = *p;
        p++;
    }
    out[i] = 0;
    return p;
}

// the i-th package in the list (lines starting with '#' are comments); 0 = no more
static int idx_entry(int i, entry_t *e) {
    const char *p = IDX_BUF;
    while (*p) {
        const char *line = p;
        while (*p && *p != '\n') p++;
        if (*p) p++;
        if (*line == '#' || *line == '\n' || *line == '\r') continue;
        if (i-- > 0) continue;
        char num[12];
        const char *q = word(line, e->name, sizeof e->name);
        q = word(q, e->version, sizeof e->version);
        q = word(q, num, sizeof num);
        e->kind = num[0] == 'p' ? KIND_PYTHON : KIND_PROGRAM;     // "py" or "app"
        q = word(q, num, sizeof num);
        e->size = 0;
        for (char *d = num; *d >= '0' && *d <= '9'; d++) e->size = e->size * 10 + (uint32_t)(*d - '0');
        q = word(q, e->sha, sizeof e->sha);
        while (*q == ' ') q++;
        int k = 0;
        while (*q && *q != '\n' && *q != '\r' && k < 63) e->desc[k++] = *q++;
        e->desc[k] = 0;
        return 1;
    }
    return 0;
}

static int idx_find(const char *name, entry_t *e) {
    for (int i = 0; idx_entry(i, e); i++) {
        if (strcmp(e->name, name) == 0) return 1;
    }
    return 0;
}

// ------------------------------------------------ commands
static int read_header(const char *file) {
    if (myfs_read(file, 0, hdr, HDR_SIZE) != HDR_SIZE || memcmp(hdr, "MYPK", 4) != 0) return 0;
    hdr[P_NAME + 15] = hdr[P_VERSION + 15] = hdr[P_DESC + 63] = 0;   // make sure strings end
    return 1;
}

static void pkg_list(void) {
    char file[MYFS_NAME_MAX + 1];
    uint32_t size;
    int found = 0;
    console_puts("  NAME          VERSION  STATUS      DESCRIPTION\n");
    for (int i = 0; myfs_entry(i, file, &size) == 0; i++) {     // package files on the disk
        if (!ends_with_pkg(file) || !read_header(file)) continue;
        found++;
        const char *name = (const char *)&hdr[P_NAME];
        char v[16];
        const char *version = db_version(name, v) ? v : (const char *)&hdr[P_VERSION];
        console_puts("  ");
        console_puts(name);
        pad_to(14, (int)strlen(name));
        console_puts(version);                  // the installed version if pkg knows it
        pad_to(9, (int)strlen(version));
        console_puts(installed(name) ? "installed   " : "available   ");
        console_puts((const char *)&hdr[P_DESC]);
        console_putc('\n');
    }
    db_load();                                                  // installed from the PC
    for (const char *p = db; *p; ) {
        char name[16], version[16], pkgfile[24];
        p = word(p, name, sizeof name);
        p = word(p, version, sizeof version);
        while (*p == '\n' || *p == ' ') p++;
        strcpy(pkgfile, name);
        strcat(pkgfile, ".pkg");
        if (myfs_size(pkgfile) >= 0) continue;                  // already listed above
        found++;
        entry_t e;
        int known = idx_load() && idx_find(name, &e);
        console_puts("  ");
        console_puts(name);
        pad_to(14, (int)strlen(name));
        console_puts(version);
        pad_to(9, (int)strlen(version));
        console_puts("installed   ");
        console_puts(known ? e.desc : "");
        console_putc('\n');
    }
    if (!found) console_puts("  (nothing yet: try 'pkg update' then 'pkg search')\n");
}

static void pkg_update(void) {
    console_puts("Getting the package list from ");
    console_puts(http_host());
    console_puts(" ...\n");
    uint32_t len;
    int r = http_get("index.txt", (uint8_t *)IDX_BUF, IDX_MAX - 1, &len);
    if (r < 0) {
        console_puts(http_error(r));
        console_putc('\n');
        return;
    }
    if (r != 200) {
        console_puts("The server answered ");
        print_dec((uint64_t)r);
        console_puts(" (no index.txt?)\n");
        return;
    }
    IDX_BUF[len] = 0;
    idx_len = len;
    if (myfs_write(IDX_FILE, IDX_BUF, len)) {
        console_puts("Disk is full or a disk error happened.\n");
        return;
    }
    entry_t e;
    int n = 0, newer = 0;
    for (; idx_entry(n, &e); n++) {
        char v[16];
        if (db_version(e.name, v) && strcmp(v, e.version) != 0) newer++;
    }
    print_dec((uint64_t)n);
    console_puts(" packages available. Type 'pkg search' to see them.\n");
    if (newer) {
        print_dec((uint64_t)newer);
        console_puts(" installed package(s) can be upgraded: type 'pkg upgrade'.\n");
    }
}

static int contains(const char *text, const char *w) {          // case-insensitive search
    size_t n = strlen(w);
    for (; *text; text++) {
        size_t i = 0;
        while (i < n) {
            char a = text[i], b = w[i];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) break;
            i++;
        }
        if (i == n) return 1;
    }
    return 0;
}

static void pkg_search(const char *w) {
    if (!idx_load()) {
        console_puts("No package list yet. Type 'pkg update' first.\n");
        return;
    }
    entry_t e;
    int shown = 0;
    console_puts("  NAME          VERSION  TYPE    DESCRIPTION\n");
    for (int i = 0; idx_entry(i, &e); i++) {
        if (*w && !contains(e.name, w) && !contains(e.desc, w)) continue;
        shown++;
        console_puts(installed(e.name) ? "* " : "  ");
        console_puts(e.name);
        pad_to(14, (int)strlen(e.name));
        console_puts(e.version);
        pad_to(9, (int)strlen(e.version));
        console_puts(e.kind == KIND_PYTHON ? "python  " : "program ");
        console_puts(e.desc);
        console_putc('\n');
    }
    if (!shown) console_puts("  (nothing found)\n");
    else console_puts("* = installed.  Install with: pkg install NAME\n");
}

static void pkg_info(const char *name) {
    char file[MYFS_NAME_MAX + 1], v[16];
    entry_t e;
    if (!valid_prog_name(name)) {
        console_puts(usage);
        return;
    }
    strcpy(file, name);
    strcat(file, ".pkg");
    const char *version, *desc;
    uint32_t size;
    int kind;
    if (read_header(file)) {
        version = (const char *)&hdr[P_VERSION];
        desc = (const char *)&hdr[P_DESC];
        size = rd32(&hdr[P_SIZE]);
        kind = (int)rd32(&hdr[P_KIND]);
    } else if (idx_load() && idx_find(name, &e)) {
        version = e.version;
        desc = e.desc;
        size = e.size - HDR_SIZE;
        kind = e.kind;
    } else {
        console_puts("No such package: ");
        console_puts(name);
        console_puts(". Try 'pkg search'.\n");
        return;
    }
    console_puts("Package:     ");
    console_puts(name);
    console_puts("\nVersion:     ");
    console_puts(version);
    console_puts("\nType:        ");
    console_puts(kind == KIND_PYTHON ? "Python app (needs python)" : "program");
    console_puts("\nDescription: ");
    console_puts(desc);
    console_puts("\nSize:        ");
    print_dec(size);
    console_puts(" bytes\nInstalled:   ");
    if (db_version(name, v) && installed(name)) {
        console_puts("yes, version ");
        console_puts(v);
        console_putc('\n');
    } else {
        console_puts(installed(name) ? "yes\n" : "no\n");
    }
}

static int hex_matches(const uint8_t h[32], const char *hex) {
    static const char digits[] = "0123456789abcdef";
    if (strlen(hex) != 64) return 0;
    for (int i = 0; i < 32; i++) {
        if (hex[2 * i] != digits[h[i] >> 4] || hex[2 * i + 1] != digits[h[i] & 15]) return 0;
    }
    return 1;
}

static int install(const char *name, int from_repo, int depth);

// put the package in PKG_BUF[0..total) on the disk
static int install_buffer(uint32_t total, int depth) {
    const char *bad = "That package file is damaged or not a package.\n";
    if (total < HDR_SIZE + 1 || memcmp(PKG_BUF, "MYPK", 4) != 0) {
        console_puts(bad);
        return 0;
    }
    uint32_t kind = rd32(PKG_BUF + P_KIND), size = rd32(PKG_BUF + P_SIZE);
    if (size + HDR_SIZE > total || (kind != KIND_PROGRAM && kind != KIND_PYTHON)) {
        console_puts(bad);
        return 0;
    }
    char prog[16], version[16], file[24];
    memcpy(prog, PKG_BUF + P_NAME, 15);
    prog[15] = 0;
    memcpy(version, PKG_BUF + P_VERSION, 15);
    version[15] = 0;
    if (!valid_prog_name(prog)) {                       // e.g. no "readme.txt" overwrites
        console_puts(bad);
        return 0;
    }
    if (kind == KIND_PROGRAM) {
        int arch = program_header_arch(PKG_BUF + HDR_SIZE, size);
        if (arch == MYEX_ARCH_X86) {
            console_puts("That package is for MyOS PC (x86), not for the Pi.\n");
            return 0;
        }
        if (arch != MYEX_ARCH_ARM64) {
            console_puts(bad);
            return 0;
        }
        strcpy(file, prog);
    } else {
        py_name(prog, file);
    }
    console_puts("Installing ");
    console_puts(prog);
    console_putc(' ');
    console_puts(version);
    console_puts(" ...\n");
    if (myfs_write(file, PKG_BUF + HDR_SIZE, size)) {
        console_puts("Disk is full or a disk error happened.\n");
        return 0;
    }
    db_set(prog, version);
    char pv[16];
    if (kind == KIND_PYTHON &&                          // dependency: python 1.3+ (myos module)
        (myfs_size("python") < 0 || !db_version("python", pv) || strcmp(pv, "1.3") < 0)) {
        console_puts(prog);
        console_puts(" is a Python app; it needs python 1.3 or newer.\n");
        if (!install("python", 0, depth + 1)) return 0;
    }
    console_puts("Done. Type '");
    console_puts(prog);
    console_puts("' to run it.\n");
    return 1;
}

// from_repo: 1 = always download (upgrade), 0 = use NAME.pkg on the disk if there
static int install(const char *name, int from_repo, int depth) {
    char file[MYFS_NAME_MAX + 1];
    if (!valid_prog_name(name) || depth > 2) {
        console_puts(usage);
        return 0;
    }
    strcpy(file, name);
    strcat(file, ".pkg");
    int32_t total = myfs_size(file);
    entry_t e;
    int in_repo = idx_load() && idx_find(name, &e);
    // a package file on the disk is used unless the repository has another version
    int local = total >= 0 && !from_repo &&
                (!in_repo || (read_header(file) && strcmp((const char *)&hdr[P_VERSION], e.version) == 0));
    if (local) {
        if (total > (int32_t)PKG_MAX || myfs_read(file, 0, PKG_BUF, (uint32_t)total) != total) {
            console_puts("That package file is damaged or not a package.\n");
            return 0;
        }
        return install_buffer((uint32_t)total, depth);
    }
    if (!in_repo) {
        if (myfs_size(IDX_FILE) < 0) console_puts("No package list yet. Type 'pkg update' first.\n");
        else {
            console_puts("No such package: ");
            console_puts(name);
            console_puts(". Try 'pkg search'.\n");
        }
        return 0;
    }
    console_puts("Downloading ");
    console_puts(e.name);
    console_putc(' ');
    console_puts(e.version);
    console_puts(" (");
    print_dec(e.size);
    console_puts(" bytes) ...\n");
    uint32_t len;
    int r = http_get(file, PKG_BUF, PKG_MAX, &len);
    if (r < 0) {
        console_puts(http_error(r));
        console_putc('\n');
        return 0;
    }
    uint8_t h[32];
    sha256(PKG_BUF, len, h);
    if (r != 200 || len != e.size || !hex_matches(h, e.sha)) {
        console_puts(r != 200 ? "The server doesn't have that file. Try 'pkg update'.\n"
                              : "The download is damaged (checksum doesn't match). Try 'pkg update'.\n");
        return 0;
    }
    return install_buffer(len, depth);
}

// other names people type for a package: pkg install nodejs -> node
static const char *pkg_alias(const char *name) {
    static const char *const alias[][2] = {
        { "nodejs", "node" }, { "quickjs", "node" }, { "qjs", "node" }, { "js", "node" },
    };
    for (size_t i = 0; i < sizeof alias / sizeof alias[0]; i++)
        if (strcmp(name, alias[i][0]) == 0) return alias[i][1];
    return name;
}

static void pkg_install(const char *args) {
    if (!*args) {
        console_puts(usage);
        return;
    }
    while (*args) {                                     // pkg install snake tetris 2048
        char name[16];
        args = word(args, name, sizeof name);
        while (*args == ' ') args++;
        if (name[0]) install(pkg_alias(name), 0, 0);
    }
}

static void pkg_remove(const char *name) {
    char py[24], v[16];
    if (!valid_prog_name(name)) {
        console_puts("Usage: pkg remove NAME\n");
        return;
    }
    int done = myfs_remove(name) == 0;
    py_name(name, py);
    if (db_version(name, v)) {                          // only scripts pkg put there
        if (myfs_remove(py) == 0) done = 1;
        db_unset(name);
    }
    if (done) {
        console_puts("Removed ");
        console_puts(name);
        console_putc('\n');
    } else {
        console_puts(name);
        console_puts(" is not installed.\n");
    }
}

static void pkg_upgrade(void) {
    if (!idx_load()) {
        console_puts("No package list yet. Type 'pkg update' first.\n");
        return;
    }
    entry_t e;
    int n = 0;
    for (int i = 0; idx_entry(i, &e); i++) {
        char v[16];
        if (!db_version(e.name, v) || strcmp(v, e.version) == 0) continue;
        console_puts(e.name);
        console_puts(": ");
        console_puts(v);
        console_puts(" -> ");
        console_puts(e.version);
        console_putc('\n');
        if (install(e.name, 1, 0)) n++;
        idx_load();                                     // (install may reuse the buffers)
    }
    if (n) {
        print_dec((uint64_t)n);
        console_puts(" package(s) upgraded.\n");
    } else {
        console_puts("Everything is up to date.\n");
    }
}

int pkg_python_app(const char *name) {                  // installed Python app called NAME?
    char py[24], v[16];
    if (!valid_prog_name(name)) return 0;
    py_name(name, py);
    return myfs_size(py) >= 0 && db_version(name, v);
}

void pkg_command(const char *args) {
    if (!myfs_mounted()) {
        console_puts("No file system. Type 'sd' for details.\n");
        return;
    }
    char cmd[16];
    args = word(args, cmd, sizeof cmd);
    while (*args == ' ') args++;
    if (strcmp(cmd, "list") == 0) pkg_list();
    else if (strcmp(cmd, "update") == 0) pkg_update();
    else if (strcmp(cmd, "search") == 0) pkg_search(args);
    else if (strcmp(cmd, "info") == 0) pkg_info(args);
    else if (strcmp(cmd, "install") == 0) pkg_install(args);
    else if (strcmp(cmd, "remove") == 0) pkg_remove(args);
    else if (strcmp(cmd, "upgrade") == 0) pkg_upgrade();
    else console_puts(usage);
}

// "apt" / "apt-get": the same as pkg, for fingers used to Debian / Raspberry Pi OS.
// Options like -y are dropped (pkg never asks), and apt's other names are understood.
void apt_command(const char *args) {
    static const char *const alias[][2] = {
        { "show", "info" }, { "purge", "remove" }, { "full-upgrade", "upgrade" },
        { "dist-upgrade", "upgrade" },
    };
    char line[128], w[48];
    size_t n = 0;
    int first = 1;
    for (;;) {
        args = word(args, w, sizeof w);
        if (!w[0]) break;
        if (w[0] == '-') continue;              // -y, --yes, --installed ...
        const char *out = w;
        if (first)
            for (size_t i = 0; i < sizeof alias / sizeof alias[0]; i++)
                if (strcmp(w, alias[i][0]) == 0) out = alias[i][1];
        first = 0;
        size_t len = strlen(out);
        if (n + len + 2 > sizeof line) break;
        if (n) line[n++] = ' ';
        memcpy(line + n, out, len);
        n += len;
    }
    line[n] = 0;
    if (n == 0) {
        console_puts("Usage: apt update | upgrade | install NAME... | remove NAME | search [WORD]\n"
                     "       apt show NAME | list      (the same as 'pkg')\n");
        return;
    }
    pkg_command(line);
}
