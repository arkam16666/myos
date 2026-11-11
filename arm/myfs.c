// myfs.c - the MyFS file system, versions 1 and 2 (see myfs.h; the PC side
// is tools/myfs.py, which must stay byte-for-byte compatible).
//
// v1 "MYFS0001" (also MyOS PC, kernel.asm):
//   sector 0     superblock: "MYFS0001", u32 total sectors, u32 next free sector
//   sector 1-8   directory: 64 entries of name[48], start, size, alloc, flags
//   sector 16..  file data, each file one run of sectors (never reclaimed)
//
// v2 "MYFS0002" - every block is one 512-byte sector:
//   sector 0     superblock: "MYFS0002", u32 total sectors, u32 bitmap start,
//                u32 bitmap sectors, u32 flags; bytes 128-255 = the entry of
//                the top folder "/"
//   bitmap       1 bit per sector (1 = in use), bit s%8 of byte s/8
//   anything else: file data, folder tables and extent blocks, allocated from
//                the bitmap
//   entry (128 bytes): name[64] (0-terminated, 1-63 chars), u32 type (0 free,
//                1 file, 2 folder), u32 size (bytes), u32 more (first extent
//                block, 0 = none), u32 reserved, 6 extents {u32 start, u32 count}
//   extent block: "MYXT", u32 next extent block, 63 extents
//   folder: its sectors hold its entries, 4 per sector (count = sum of extents)
//
// Safety against power loss (writes go in this order):
//   new data -> extent blocks -> bitmap (new sectors marked used) ->
//   the one sector holding the entry (the "commit") -> bitmap (old freed).
// A crash leaves either the old or the new file; at worst some sectors stay
// marked used for nobody. The check at mount walks every folder, rebuilds the
// bitmap from what is really used and so gives that space back.
#include <stdint.h>
#include <string.h>
#include "myfs.h"

static myfs_dev_t dev;
static int mounted;                 // 0, 1 (v1) or 2 (v2)
static uint32_t generation;         // changes with every change (readdir cursors)
static uint8_t buf[512] __attribute__((aligned(4)));   // file data passes through here
static uint8_t tmp[512] __attribute__((aligned(4)));   // a metadata sector being changed

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static void wr32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

// (the kernel's tiny C library has no strchr / strrchr / strnlen)
static char *find_char(const char *s, char c) {
    for (; *s; s++) if (*s == c) return (char *)s;
    return 0;
}
static char *last_char(const char *s, char c) {
    char *found = 0;
    for (; *s; s++) if (*s == c) found = (char *)s;
    return found;
}
static size_t name_len(const char *s, size_t max) {
    size_t n = 0;
    while (n < max && s[n]) n++;
    return n;
}

static int dev_read(uint32_t n, void *b) {
    return n < dev.sectors ? dev.read(dev.start + n, b) : -1;
}
static int dev_write(uint32_t n, const void *b) {
    return n < dev.sectors ? dev.write(dev.start + n, b) : -1;
}

// ---------------------------------------------------------------
//  path helpers (both versions)
// ---------------------------------------------------------------
// cwd + path -> "/a/b" (no ".", "..", "//"); "/" for the top folder
int myfs_normalize(const char *cwd, const char *path, char *out) {
    size_t len = 0;
    for (int pass = 0; pass < 2; pass++) {
        const char *s = pass == 1 ? path : (path[0] == '/' || !cwd) ? "" : cwd;
        while (*s) {
            while (*s == '/') s++;
            if (!*s) break;
            const char *c = s;
            while (*s && *s != '/') s++;
            size_t n = (size_t)(s - c);
            if (n == 1 && c[0] == '.') continue;
            if (n == 2 && c[0] == '.' && c[1] == '.') {     // up one folder ("/.." = "/")
                while (len && out[len - 1] != '/') len--;
                if (len) len--;
                continue;
            }
            if (n > MYFS_NAME_MAX || len + 1 + n >= MYFS_PATH_MAX) return MYFS_ERR_NAME;
            for (size_t i = 0; i < n; i++) {
                if (c[i] <= ' ' || c[i] > '~') return MYFS_ERR_NAME;
            }
            out[len++] = '/';
            memcpy(out + len, c, n);
            len += n;
        }
    }
    if (len == 0) out[len++] = '/';
    out[len] = 0;
    return 0;
}

static int depth_of(const char *norm) {         // "/" 0, "/a" 1, "/a/b" 2
    int d = 0;
    if (norm[1] == 0) return 0;
    for (; *norm; norm++) d += *norm == '/';
    return d;
}

const char *myfs_error(int err) {
    switch (err) {
        case MYFS_ERR_IO: return "Disk error";
        case MYFS_ERR_NOFS: return "No file system";
        case MYFS_ERR_NOTFOUND: return "No such file or folder";
        case MYFS_ERR_FULL: return "Disk or folder is full";
        case MYFS_ERR_NAME: return "Bad name";
        case MYFS_ERR_EXISTS: return "Already exists";
        case MYFS_ERR_NOTDIR: return "Not a folder";
        case MYFS_ERR_ISDIR: return "Is a folder";
        case MYFS_ERR_NOTEMPTY: return "Folder is not empty";
        case MYFS_ERR_V1: return "Folders need MyFS v2 (type 'fsupgrade')";
        case MYFS_ERR_CORRUPT: return "File system damaged here (type 'fsck')";
        default: return "Error";
    }
}

// ===============================================================
//  v1: the flat file system (kept exactly as before)
// ===============================================================
#define V1_DIR_START   1
#define V1_DIR_SECTORS 8
#define V1_DATA_START  16
#define V1_ENTRY       64

static uint8_t v1_super[512] __attribute__((aligned(4)));
static uint8_t v1_dir[V1_DIR_SECTORS * 512] __attribute__((aligned(4)));

static uint8_t *v1_entry(int i) { return &v1_dir[i * V1_ENTRY]; }
static int v1_used(int i) { return rd32(v1_entry(i) + 60) & 1; }

static int v1_flush(void) {
    if (dev_write(0, v1_super)) return MYFS_ERR_IO;
    for (int i = 0; i < V1_DIR_SECTORS; i++) {
        if (dev_write(V1_DIR_START + i, &v1_dir[i * 512])) return MYFS_ERR_IO;
    }
    generation++;
    return 0;
}

// path -> the flat v1 name ("/x" or "x"); folders don't exist in v1
static int v1_name(const char *path, char *out) {
    char norm[MYFS_PATH_MAX];
    if (myfs_normalize(0, path, norm)) return MYFS_ERR_NAME;
    if (norm[1] == 0) return MYFS_ERR_ISDIR;                // "/" itself
    if (find_char(norm + 1, '/')) return MYFS_ERR_NOTFOUND;    // inside a folder: not in v1
    if (strlen(norm + 1) > MYFS_V1_NAME_MAX) return MYFS_ERR_NAME;
    strcpy(out, norm + 1);
    return 0;
}

static int v1_find(const char *name) {
    for (int i = 0; i < MYFS_MAX_FILES; i++) {
        if (v1_used(i) && strncmp((const char *)v1_entry(i), name, MYFS_V1_NAME_MAX) == 0) return i;
    }
    return -1;
}

static int v1_find_path(const char *path) {        // index, or an error
    char name[MYFS_V1_NAME_MAX + 1];
    int r = v1_name(path, name);
    if (r) return r == MYFS_ERR_ISDIR ? MYFS_ERR_ISDIR : MYFS_ERR_NOTFOUND;
    int i = v1_find(name);
    return i < 0 ? MYFS_ERR_NOTFOUND : i;
}

static int v1_read(int i, uint32_t offset, void *out, uint32_t len) {
    uint32_t start = rd32(v1_entry(i) + 48), size = rd32(v1_entry(i) + 52);
    if (offset >= size) return 0;
    if (len > size - offset) len = size - offset;
    uint8_t *o = out;
    uint32_t done = 0;
    while (done < len) {
        uint32_t pos = offset + done;
        if (dev_read(start + pos / 512, buf)) return MYFS_ERR_IO;
        uint32_t n = 512 - pos % 512;
        if (n > len - done) n = len - done;
        memcpy(o + done, buf + pos % 512, n);
        done += n;
    }
    return (int)done;
}

// create/replace; data comes from memory, or (src >= 0) from another v1 file
static int v1_write(const char *path, const void *data, int src, uint32_t size) {
    char name[MYFS_V1_NAME_MAX + 1];
    int r = v1_name(path, name);
    if (r) return r == MYFS_ERR_NOTFOUND ? MYFS_ERR_V1 : r;
    uint32_t need = (size + 511) / 512;
    int i = v1_find(name);
    if (i < 0) {
        for (i = 0; i < MYFS_MAX_FILES && v1_used(i); i++) {
        }
        if (i == MYFS_MAX_FILES) return MYFS_ERR_FULL;
        memset(v1_entry(i), 0, V1_ENTRY);
        memcpy(v1_entry(i), name, strlen(name));
        wr32(v1_entry(i) + 56, 0);
    }
    uint8_t *e = v1_entry(i);
    if (src == i) return 0;                         // a copy onto itself
    if (need > rd32(e + 56)) {                      // doesn't fit: take new space
        uint32_t next = rd32(v1_super + 12);
        if (next + need > rd32(v1_super + 8)) return MYFS_ERR_FULL;
        wr32(e + 48, next);
        wr32(e + 56, need);
        wr32(v1_super + 12, next + need);
    }
    const uint8_t *d = data;
    uint32_t start = rd32(e + 48);
    uint32_t src_start = src >= 0 ? rd32(v1_entry(src) + 48) : 0;
    for (uint32_t s = 0; s < need; s++) {
        if (src >= 0) {
            if (dev_read(src_start + s, buf)) return MYFS_ERR_IO;
        } else {
            uint32_t n = size - s * 512;
            if (n > 512) n = 512;
            memset(buf, 0, 512);
            memcpy(buf, d + s * 512, n);
        }
        if (dev_write(start + s, buf)) return MYFS_ERR_IO;
    }
    wr32(e + 52, size);
    wr32(e + 60, 1);
    return v1_flush();
}

static int v1_rename(const char *from, const char *to) {
    int i = v1_find_path(from);
    if (i < 0) return i;
    char name[MYFS_V1_NAME_MAX + 1];
    int r = v1_name(to, name);
    if (r) return r == MYFS_ERR_NOTFOUND ? MYFS_ERR_V1 : r;
    int j = v1_find(name);
    if (j == i) return 0;
    if (j >= 0) wr32(v1_entry(j) + 60, 0);          // replaces that file
    memset(v1_entry(i), 0, 48);
    memcpy(v1_entry(i), name, strlen(name));
    return v1_flush();
}

// ===============================================================
//  v2
// ===============================================================
#define V2_MAGIC     "MYFS0002"
#define ENT          128            // bytes per entry
#define E_TYPE       64
#define E_SIZE       68
#define E_MORE       72
#define E_X          80
#define NX_INLINE    6              // extents inside the entry
#define NX_BLOCK     63             // extents in one extent block
#define T_FREE       0
#define MAX_SECTORS  (8u << 20)     // 4 GB: as big as a FAT file can be
#define DIR_FIRST    4              // sectors of a new folder (16 entries)
#define DIR_GROW     8              // sectors added when a folder is full (32 entries)
#define DEPTH_GUARD  40             // the checker goes no deeper (kernel stack)
#define MAX_X        (NX_INLINE + NX_BLOCK * 130)   // extents of one file being written
#define MAX_CHAINS   130

static uint32_t total, bm_start, bm_sectors, free_sectors, alloc_hint;
static int nfiles, ndirs;
static uint8_t bm[MAX_SECTORS / 8];                 // the bitmap, all of it in RAM
static uint8_t bm_dirty[MAX_SECTORS / 4096 / 8];    // bitmap sectors to write back
static uint32_t xs[MAX_X][2];                       // extents being allocated
static int nxs;
static uint32_t chains[MAX_CHAINS];                 // their extent blocks
static int nchains;
static myfs_check_t mount_report;

// ---- a small cache for metadata sectors (folders, extent blocks) ----
#define CACHE_N 256
static uint8_t cache_data[CACHE_N][512] __attribute__((aligned(4)));
static uint32_t cache_tag[CACHE_N];                 // lba + 1, 0 = empty

static void cache_clear(void) { memset(cache_tag, 0, sizeof cache_tag); }

// a sector of metadata; the pointer is only good until the next call
static const uint8_t *meta(uint32_t lba) {
    if (lba >= total) return 0;
    uint32_t i = lba % CACHE_N;
    if (cache_tag[i] != lba + 1) {
        cache_tag[i] = 0;
        if (dev_read(lba, cache_data[i])) return 0;
        cache_tag[i] = lba + 1;
    }
    return cache_data[i];
}

// every v2 write goes through here, so the cache never goes stale
static int put_sector(uint32_t lba, const uint8_t *data) {
    uint32_t i = lba % CACHE_N;
    if (lba >= total || dev_write(lba, data)) {
        if (lba < total && cache_tag[i] == lba + 1) cache_tag[i] = 0;
        return MYFS_ERR_IO;
    }
    if (cache_tag[i] == lba + 1 && data != cache_data[i]) memcpy(cache_data[i], data, 512);
    return 0;
}

// ---- the bitmap ----
static int bit(uint32_t s) { return bm[s >> 3] >> (s & 7) & 1; }

static void mark(uint32_t s, uint32_t n, int used) {
    for (uint32_t k = s; k < s + n && k < total; k++) {
        if (bit(k) == used) continue;
        if (used) {
            bm[k >> 3] |= (uint8_t)(1u << (k & 7));
            free_sectors--;
        } else {
            bm[k >> 3] &= (uint8_t)~(1u << (k & 7));
            free_sectors++;
        }
        uint32_t b = k >> 12;                       // its bitmap sector
        bm_dirty[b >> 3] |= (uint8_t)(1u << (b & 7));
    }
}

static int flush_bitmap(void) {
    for (uint32_t i = 0; i < bm_sectors; i++) {
        if (!(bm_dirty[i >> 3] >> (i & 7) & 1)) continue;
        if (put_sector(bm_start + i, &bm[i * 512])) return MYFS_ERR_IO;
        bm_dirty[i >> 3] &= (uint8_t)~(1u << (i & 7));
    }
    return 0;
}

// bits past the end of the volume are always "used"
static void mark_tail(void) {
    for (uint32_t k = total; k < bm_sectors * 4096; k++) bm[k >> 3] |= (uint8_t)(1u << (k & 7));
}

static void recount_free(void) {
    free_sectors = 0;
    for (uint32_t k = 0; k < total; k++) free_sectors += !bit(k);
}

// first run of `need` free sectors in [from, to); 0 = none (sector 0 is never free)
static uint32_t find_run(uint32_t from, uint32_t to, uint32_t need) {
    uint32_t run = 0, start = 0;
    for (uint32_t k = from; k < to;) {
        if ((k & 7) == 0 && k + 8 <= to && bm[k >> 3] == 0xFF) {
            run = 0;
            k += 8;
            continue;
        }
        if (bit(k)) {
            run = 0;
        } else {
            if (run == 0) start = k;
            if (++run == need) return start;
        }
        k++;
    }
    return 0;
}

// ---- extents of an entry ----
typedef struct {
    uint32_t x[NX_INLINE * 2], more;
    uint32_t i;                 // extents handed out so far
    uint32_t chain;             // extent block being read (0 = still the inline ones)
    int ci;                     // next extent in it
    int err;
} xit_t;

static void xit_init(xit_t *it, const uint8_t *e) {
    for (int k = 0; k < NX_INLINE * 2; k++) it->x[k] = rd32(e + E_X + 4 * k);
    it->more = rd32(e + E_MORE);
    it->i = 0;
    it->chain = 0;
    it->ci = 0;
    it->err = 0;
}

static const uint8_t *extent_block(uint32_t lba) {
    if (lba == 0 || lba >= total) return 0;
    const uint8_t *p = meta(lba);
    return p && memcmp(p, "MYXT", 4) == 0 ? p : 0;
}

// next extent: 1 = got one, 0 = no more, <0 = damaged
static int xit_next(xit_t *it, uint32_t *s, uint32_t *n) {
    if (it->i < NX_INLINE) {
        *s = it->x[it->i * 2];
        *n = it->x[it->i * 2 + 1];
        if (*n == 0) return 0;
    } else {
        if (it->chain == 0) {
            if (!it->more) return 0;
            it->chain = it->more;
            it->ci = 0;
        } else if (it->ci == NX_BLOCK) {
            const uint8_t *c = extent_block(it->chain);
            if (!c) return it->err = MYFS_ERR_CORRUPT;
            uint32_t next = rd32(c + 4);
            if (!next) return 0;
            it->chain = next;
            it->ci = 0;
        }
        const uint8_t *c = extent_block(it->chain);
        if (!c) return it->err = MYFS_ERR_CORRUPT;
        *s = rd32(c + 8 + it->ci * 8);
        *n = rd32(c + 12 + it->ci * 8);
        if (*n == 0) return 0;
        it->ci++;
    }
    it->i++;
    if (*s == 0 || *s >= total || *n > total - *s || it->i > total) return it->err = MYFS_ERR_CORRUPT;
    return 1;
}

typedef struct { xit_t x; uint32_t s, n; } sit_t;      // one sector at a time

static void sit_init(sit_t *it, const uint8_t *e) { xit_init(&it->x, e); it->n = 0; }

static int sit_next(sit_t *it, uint32_t *lba) {
    if (it->n == 0) {
        int r = xit_next(&it->x, &it->s, &it->n);
        if (r <= 0) return r;
    }
    *lba = it->s++;
    it->n--;
    return 1;
}

// ---- entries and where they are ----
typedef struct {
    uint32_t lba;               // sector holding the entry (0 for "/": in the superblock)
    int slot;                   // which of its four 128-byte places
    uint8_t e[ENT];
} loc_t;

static uint32_t etype(const uint8_t *e) { return rd32(e + E_TYPE); }

static int root_loc(loc_t *l) {
    const uint8_t *s = meta(0);
    if (!s) return MYFS_ERR_IO;
    l->lba = 0;
    l->slot = 1;
    memcpy(l->e, s + ENT, ENT);
    return 0;
}

// write one entry: the single-sector write that makes a change real
static int put_entry(uint32_t lba, int slot, const uint8_t *e) {
    const uint8_t *s = meta(lba);
    if (!s) return MYFS_ERR_IO;
    memcpy(tmp, s, 512);
    memcpy(tmp + slot * ENT, e, ENT);
    int r = put_sector(lba, tmp);
    generation++;
    return r;
}

// look for name in folder dir; also notes the first free place (if wanted)
static int dir_find(const uint8_t *dir, const char *name, loc_t *found, loc_t *free_slot) {
    sit_t it;
    uint32_t lba;
    int r;
    if (free_slot) free_slot->lba = 0;
    sit_init(&it, dir);
    while ((r = sit_next(&it, &lba)) == 1) {
        const uint8_t *p = meta(lba);
        if (!p) return MYFS_ERR_IO;
        for (int slot = 0; slot < 4; slot++) {
            const uint8_t *q = p + slot * ENT;
            uint32_t t = etype(q);
            if (t == T_FREE) {
                if (free_slot && !free_slot->lba) {
                    free_slot->lba = lba;
                    free_slot->slot = slot;
                }
            } else if ((t == MYFS_FILE || t == MYFS_DIR) && strncmp((const char *)q, name, 64) == 0) {
                if (found) {
                    found->lba = lba;
                    found->slot = slot;
                    memcpy(found->e, q, ENT);
                }
                return 0;
            }
        }
    }
    return r < 0 ? r : MYFS_ERR_NOTFOUND;
}

// path -> its entry
static int lookup(const char *path, loc_t *out) {
    char norm[MYFS_PATH_MAX];
    if (myfs_normalize(0, path, norm)) return MYFS_ERR_NAME;
    int r = root_loc(out);
    if (r) return r;
    const char *p = norm + 1;
    while (*p) {
        char name[MYFS_NAME_MAX + 1];
        size_t n = 0;
        while (*p && *p != '/') name[n++] = *p++;
        name[n] = 0;
        if (*p == '/') p++;
        if (etype(out->e) != MYFS_DIR) return MYFS_ERR_NOTDIR;
        r = dir_find(out->e, name, out, 0);
        if (r) return r;
    }
    return 0;
}

// path -> its folder (which must exist) and its last name
static int lookup_parent(const char *path, loc_t *dir, char *leaf, char *norm) {
    if (myfs_normalize(0, path, norm)) return MYFS_ERR_NAME;
    if (norm[1] == 0) return MYFS_ERR_NAME;                 // "/" has no parent
    char parent[MYFS_PATH_MAX];
    strcpy(parent, norm);
    char *slash = last_char(parent, '/');
    strcpy(leaf, slash + 1);
    if (slash == parent) slash[1] = 0;
    else *slash = 0;
    int r = lookup(parent, dir);
    if (r) return r == MYFS_ERR_NOTFOUND ? MYFS_ERR_NOTFOUND : r;
    return etype(dir->e) == MYFS_DIR ? 0 : MYFS_ERR_NOTDIR;
}

// ---- allocation ----
static void unmark_alloc(void) {
    for (int k = 0; k < nxs; k++) mark(xs[k][0], xs[k][1], 0);
    for (int k = 0; k < nchains; k++) mark(chains[k], 1, 0);
    nxs = nchains = 0;
}

// find room for `need` sectors -> xs[] (+ extent blocks in chains[]), marked used
static int alloc_file(uint32_t need) {
    nxs = nchains = 0;
    if (need == 0) return 0;
    if (need > free_sectors) return MYFS_ERR_FULL;
    uint32_t s = find_run(alloc_hint, total, need);         // best: one piece
    if (!s) s = find_run(1, total, need);
    if (s) {
        xs[0][0] = s;
        xs[0][1] = need;
        nxs = 1;
        mark(s, need, 1);
    } else {                                                // else: the free pieces in order
        uint32_t left = need;
        for (uint32_t k = 1; k < total && left;) {
            if (bit(k)) { k++; continue; }
            uint32_t n = 0;
            while (k + n < total && n < left && !bit(k + n)) n++;
            if (nxs == MAX_X) { unmark_alloc(); return MYFS_ERR_FULL; }
            xs[nxs][0] = k;
            xs[nxs][1] = n;
            nxs++;
            mark(k, n, 1);
            left -= n;
            k += n;
        }
        if (left) { unmark_alloc(); return MYFS_ERR_FULL; }
    }
    int want = nxs > NX_INLINE ? (nxs - NX_INLINE + NX_BLOCK - 1) / NX_BLOCK : 0;
    while (nchains < want) {
        uint32_t c = find_run(1, total, 1);
        if (!c) { unmark_alloc(); return MYFS_ERR_FULL; }
        mark(c, 1, 1);
        chains[nchains++] = c;
    }
    alloc_hint = xs[nxs - 1][0] + xs[nxs - 1][1];
    if (alloc_hint >= total) alloc_hint = 1;
    return 0;
}

// put xs[]/chains[] into entry e and write the extent blocks
static int store_extents(uint8_t *e) {
    memset(e + E_X, 0, NX_INLINE * 8);
    for (int k = 0; k < nxs && k < NX_INLINE; k++) {
        wr32(e + E_X + k * 8, xs[k][0]);
        wr32(e + E_X + k * 8 + 4, xs[k][1]);
    }
    wr32(e + E_MORE, nchains ? chains[0] : 0);
    for (int c = 0; c < nchains; c++) {
        memset(tmp, 0, 512);
        memcpy(tmp, "MYXT", 4);
        wr32(tmp + 4, c + 1 < nchains ? chains[c + 1] : 0);
        for (int k = 0; k < NX_BLOCK; k++) {
            int x = NX_INLINE + c * NX_BLOCK + k;
            if (x >= nxs) break;
            wr32(tmp + 8 + k * 8, xs[x][0]);
            wr32(tmp + 12 + k * 8, xs[x][1]);
        }
        if (put_sector(chains[c], tmp)) return MYFS_ERR_IO;
    }
    return 0;
}

// mark (used = 1) or free (0) everything an entry owns; problems counts trouble
static void own_sectors(const uint8_t *e, int used, uint32_t *problems) {
    xit_t it;
    uint32_t s, n;
    int r;
    xit_init(&it, e);
    while ((r = xit_next(&it, &s, &n)) == 1) mark(s, n, used);
    if (r < 0 && problems) (*problems)++;
    uint32_t c = rd32(e + E_MORE);
    for (uint32_t guard = 0; c && guard < total; guard++) {
        const uint8_t *p = extent_block(c);
        if (!p) {
            if (problems) (*problems)++;
            break;
        }
        uint32_t next = rd32(p + 4);
        mark(c, 1, used);
        c = next;
    }
}

static int zero_sectors(uint32_t s, uint32_t n) {
    memset(tmp, 0, 512);
    for (uint32_t k = 0; k < n; k++) {
        if (put_sector(s + k, tmp)) return MYFS_ERR_IO;
    }
    return 0;
}

// a full folder gets DIR_GROW more sectors; *slot = the first new free place
static int dir_grow(loc_t *dir, loc_t *slot) {
    xit_t it;
    uint32_t s, n, last_s = 0, last_n = 0, count = 0, sectors = 0;
    int r;
    xit_init(&it, dir->e);
    while ((r = xit_next(&it, &s, &n)) == 1) {
        last_s = s;
        last_n = n;
        count++;
        sectors += n;
    }
    if (r < 0) return r;
    uint32_t chain = it.chain;                  // extent block holding the last extent (if any)
    int chain_i = it.ci;
    uint32_t want = DIR_GROW;
    if (want > free_sectors) want = 1;
    if (want > free_sectors) return MYFS_ERR_FULL;
    int extend = count && last_s + last_n + want <= total &&
                 find_run(last_s + last_n, last_s + last_n + want, want) == last_s + last_n;
    uint32_t new_s;
    if (extend) {
        new_s = last_s + last_n;
    } else {
        new_s = find_run(alloc_hint, total, want);
        if (!new_s) new_s = find_run(1, total, want);
        if (!new_s) {
            want = 1;
            new_s = find_run(1, total, 1);
            if (!new_s) return MYFS_ERR_FULL;
        }
    }
    mark(new_s, want, 1);
    if (zero_sectors(new_s, want)) { mark(new_s, want, 0); return MYFS_ERR_IO; }
    uint8_t e[ENT];
    memcpy(e, dir->e, ENT);
    if (extend && count <= NX_INLINE) {                     // the last extent grows
        wr32(e + E_X + (count - 1) * 8 + 4, last_n + want);
    } else if (extend) {                                    // ...inside an extent block
        const uint8_t *c = extent_block(chain);
        if (!c) { mark(new_s, want, 0); return MYFS_ERR_CORRUPT; }
        memcpy(tmp, c, 512);
        wr32(tmp + 12 + (chain_i - 1) * 8, last_n + want);
        if (flush_bitmap() || put_sector(chain, tmp)) return MYFS_ERR_IO;
    } else if (count < NX_INLINE) {                         // a new inline extent
        wr32(e + E_X + count * 8, new_s);
        wr32(e + E_X + count * 8 + 4, want);
    } else {                                                // a new extent in an extent block
        uint32_t k = count - NX_INLINE;                     // its number among the block extents
        if (k % NX_BLOCK != 0) {                            // room in the last block
            const uint8_t *c = extent_block(chain);
            if (!c) { mark(new_s, want, 0); return MYFS_ERR_CORRUPT; }
            memcpy(tmp, c, 512);
            wr32(tmp + 8 + (k % NX_BLOCK) * 8, new_s);
            wr32(tmp + 12 + (k % NX_BLOCK) * 8, want);
            if (flush_bitmap() || put_sector(chain, tmp)) return MYFS_ERR_IO;
        } else {                                            // a new block, linked from the last
            uint32_t b = find_run(1, total, 1);
            if (!b) { mark(new_s, want, 0); return MYFS_ERR_FULL; }
            mark(b, 1, 1);
            memset(tmp, 0, 512);
            memcpy(tmp, "MYXT", 4);
            wr32(tmp + 8, new_s);
            wr32(tmp + 12, want);
            if (put_sector(b, tmp) || flush_bitmap()) return MYFS_ERR_IO;
            if (k == 0) {
                wr32(e + E_MORE, b);
            } else {
                const uint8_t *c = extent_block(chain);
                if (!c) return MYFS_ERR_CORRUPT;
                memcpy(tmp, c, 512);
                wr32(tmp + 4, b);
                if (put_sector(chain, tmp)) return MYFS_ERR_IO;
            }
        }
    }
    wr32(e + E_SIZE, (sectors + want) * 512);
    if (flush_bitmap() || put_entry(dir->lba, dir->slot, e)) return MYFS_ERR_IO;
    memcpy(dir->e, e, ENT);
    slot->lba = new_s;
    slot->slot = 0;
    return 0;
}

// create/replace a file; data from memory, or from the file whose entry is src
static int v2_write(const char *path, const uint8_t *data, const uint8_t *src, uint32_t size) {
    loc_t dir, old, slot;
    char leaf[MYFS_NAME_MAX + 1], norm[MYFS_PATH_MAX];
    int r = lookup_parent(path, &dir, leaf, norm);
    if (r) return r;
    r = dir_find(dir.e, leaf, &old, &slot);
    int have_old = r == 0;
    if (r && r != MYFS_ERR_NOTFOUND) return r;
    if (have_old && etype(old.e) != MYFS_FILE) return MYFS_ERR_ISDIR;
    if (!have_old && depth_of(norm) > MYFS_DEPTH_MAX) return MYFS_ERR_NAME;
    if (!have_old && !slot.lba) {
        r = dir_grow(&dir, &slot);
        if (r) return r;
    }
    uint32_t need = (size + 511) / 512;
    int freed_early = 0;
    r = alloc_file(need);
    if (r == MYFS_ERR_FULL && have_old) {
        // not room for old and new at once: reuse the old file's space
        // (a crash now can lose this file, but nothing else)
        own_sectors(old.e, 0, 0);
        freed_early = 1;
        r = alloc_file(need);
    }
    if (r) {
        if (freed_early) own_sectors(old.e, 1, 0);
        return r;
    }
    // 1. the data
    sit_t from;
    if (src) sit_init(&from, src);
    uint32_t done = 0;
    for (int k = 0; k < nxs && !r; k++) {
        for (uint32_t i = 0; i < xs[k][1]; i++, done++) {
            if (src) {
                uint32_t lba;
                if (sit_next(&from, &lba) != 1 || dev_read(lba, buf)) { r = MYFS_ERR_IO; break; }
            } else {
                uint32_t n = size - done * 512;
                if (n > 512) n = 512;
                memset(buf, 0, 512);
                memcpy(buf, data + done * 512, n);
            }
            if (put_sector(xs[k][0] + i, buf)) { r = MYFS_ERR_IO; break; }
        }
    }
    // 2. the extent blocks, 3. the bitmap, 4. the entry
    uint8_t e[ENT];
    memset(e, 0, ENT);
    memcpy(e, leaf, strlen(leaf));
    wr32(e + E_TYPE, MYFS_FILE);
    wr32(e + E_SIZE, size);
    if (!r) r = store_extents(e);
    if (!r) r = flush_bitmap();
    if (!r) r = put_entry(have_old ? old.lba : slot.lba, have_old ? old.slot : slot.slot, e);
    if (r) {
        unmark_alloc();
        if (freed_early) own_sectors(old.e, 1, 0);
        flush_bitmap();
        return r;
    }
    // 5. the old file's space is free now
    if (have_old && !freed_early) own_sectors(old.e, 0, 0);
    if (!have_old) nfiles++;
    return flush_bitmap();
}

static int v2_read(const loc_t *f, uint32_t offset, void *out, uint32_t len) {
    uint32_t size = rd32(f->e + E_SIZE);
    if (offset >= size) return 0;
    if (len > size - offset) len = size - offset;
    xit_t it;
    uint32_t s, n, skip = offset / 512;
    int r;
    xit_init(&it, f->e);
    while ((r = xit_next(&it, &s, &n)) == 1 && skip >= n) skip -= n;   // to the first sector
    if (r != 1) return r < 0 ? r : MYFS_ERR_CORRUPT;
    s += skip;
    n -= skip;
    uint8_t *o = out;
    uint32_t done = 0, pos = offset % 512;
    while (done < len) {
        if (n == 0 && xit_next(&it, &s, &n) != 1) return MYFS_ERR_CORRUPT;
        if (dev_read(s, buf)) return MYFS_ERR_IO;
        uint32_t c = 512 - pos;
        if (c > len - done) c = len - done;
        memcpy(o + done, buf + pos, c);
        done += c;
        pos = 0;
        s++;
        n--;
    }
    return (int)done;
}

// ---- reading folders in order: a few remembered positions make
//      "index 0, 1, 2, ..." fast even with thousands of entries ----
typedef struct {
    uint32_t key, gen;          // the folder (its first sector) and when
    int files_only;
    int index;                  // entries handed out so far
    sit_t it;
    uint32_t lba;
    int slot;
} cursor_t;

static cursor_t cursors[4];
static int cursor_next_slot;

static int cursor_next(cursor_t *c, uint8_t *e) {      // 1 = an entry, 0 = no more
    for (;;) {
        if (c->slot >= 4) {
            int r = sit_next(&c->it, &c->lba);
            if (r <= 0) return r;
            c->slot = 0;
        }
        const uint8_t *p = meta(c->lba);
        if (!p) return MYFS_ERR_IO;
        const uint8_t *q = p + c->slot++ * ENT;
        uint32_t t = etype(q);
        if (t == MYFS_FILE || (t == MYFS_DIR && !c->files_only)) {
            memcpy(e, q, ENT);
            c->index++;
            return 1;
        }
    }
}

static int v2_readdir(const char *path, int index, int files_only, char *name, uint32_t *size) {
    loc_t d;
    int r = lookup(path, &d);
    if (r) return r;
    if (etype(d.e) != MYFS_DIR) return MYFS_ERR_NOTDIR;
    if (index < 0) return 0;
    uint32_t key = rd32(d.e + E_X);
    cursor_t *c = 0;
    for (int i = 0; i < 4; i++) {
        cursor_t *k = &cursors[i];
        if (k->key == key && k->gen == generation && k->files_only == files_only && k->index == index) c = k;
    }
    if (!c) {
        c = &cursors[cursor_next_slot++ % 4];
        c->key = key;
        c->gen = generation;
        c->files_only = files_only;
        c->index = 0;
        sit_init(&c->it, d.e);
        c->slot = 4;
    }
    uint8_t e[ENT];
    do {
        r = cursor_next(c, e);
        if (r <= 0) {
            c->key = 0;
            return r;
        }
    } while (c->index <= index);
    memcpy(name, e, MYFS_NAME_MAX);
    name[MYFS_NAME_MAX] = 0;
    if (size) *size = rd32(e + E_SIZE);
    return (int)etype(e);
}

// ---- the checker: walk every folder, rebuild the bitmap ----
static struct {
    uint32_t files, dirs, damaged, deep, io;
    loc_t bad[4];
    int nbad;
} W;
static char wpath[MYFS_PATH_MAX + MYFS_NAME_MAX + 2];
static myfs_check_t *wreport;

static void note_damage(uint32_t lba, int slot, const uint8_t *e) {
    if (W.nbad < 4) {
        W.bad[W.nbad].lba = lba;
        W.bad[W.nbad].slot = slot;
        if (wreport) {
            char *out = wreport->damaged_path[W.nbad];
            size_t n = strlen(wpath);
            if (n > 70) n = 70;
            memcpy(out, wpath, n);
            out[n++] = '/';
            size_t m = name_len((const char *)e, MYFS_NAME_MAX);
            if (n + m > 79) m = 79 - n;
            memcpy(out + n, e, m);
            out[n + m] = 0;
        }
        W.nbad++;
    }
    W.damaged++;
}

// claim everything entry e owns; 1 if it was bad or shared with another
static int claim(const uint8_t *e) {
    xit_t it;
    uint32_t s, n, owned = 0, clash = 0;
    int r;
    xit_init(&it, e);
    while ((r = xit_next(&it, &s, &n)) == 1) {
        for (uint32_t k = s; k < s + n; k++) {
            if (bit(k)) clash++;
            else mark(k, 1, 1);
        }
        owned += n;
    }
    if (r < 0) return 1;
    uint32_t c = rd32(e + E_MORE);
    for (uint32_t guard = 0; c; guard++) {
        const uint8_t *p = extent_block(c);
        if (!p || guard > total) return 1;
        if (bit(c)) clash++;
        else mark(c, 1, 1);
        c = rd32(p + 4);
    }
    if (etype(e) == MYFS_FILE && owned < (rd32(e + E_SIZE) + 511) / 512) return 1;
    return clash != 0;
}

static void walk(const uint8_t *dir, int depth) {
    sit_t it;
    uint32_t lba;
    int r;
    sit_init(&it, dir);
    while ((r = sit_next(&it, &lba)) == 1) {
        for (int slot = 0; slot < 4; slot++) {
            const uint8_t *p = meta(lba);
            if (!p) { W.io++; return; }
            uint8_t e[ENT];
            memcpy(e, p + slot * ENT, ENT);
            uint32_t t = etype(e);
            if (t == T_FREE) continue;
            if (t != MYFS_FILE && t != MYFS_DIR) { note_damage(lba, slot, e); continue; }
            if (t == MYFS_FILE) W.files++;
            else W.dirs++;
            if (claim(e)) { note_damage(lba, slot, e); continue; }
            if (t == MYFS_DIR) {
                if (depth >= DEPTH_GUARD) { W.deep++; continue; }
                size_t n = strlen(wpath);
                if (n + 1 + name_len((const char *)e, MYFS_NAME_MAX) < sizeof wpath) {
                    wpath[n] = '/';
                    memcpy(wpath + n + 1, e, MYFS_NAME_MAX);
                    wpath[n + 1 + MYFS_NAME_MAX] = 0;
                }
                walk(e, depth + 1);
                wpath[n] = 0;
            }
        }
    }
    if (r < 0) W.io++;
}

// rebuild the bitmap from the folders; fix the copy on disk
static int rebuild(myfs_check_t *rep) {
    memset(&W, 0, sizeof W);
    memset(rep, 0, sizeof *rep);
    wreport = rep;
    wpath[0] = 0;
    memset(bm, 0, bm_sectors * 512);
    mark_tail();
    free_sectors = total;
    mark(0, 1, 1);
    mark(bm_start, bm_sectors, 1);
    loc_t root;
    if (root_loc(&root)) return MYFS_ERR_IO;
    if (etype(root.e) != MYFS_DIR || claim(root.e)) return MYFS_ERR_CORRUPT;
    walk(root.e, 0);
    int trouble = W.damaged || W.deep || W.io;
    uint32_t leaked = 0, missing = 0;
    for (uint32_t i = 0; i < bm_sectors; i++) {
        if (dev_read(bm_start + i, tmp)) return MYFS_ERR_IO;
        uint8_t *mine = &bm[i * 512];
        int differs = 0;
        for (int k = 0; k < 512; k++) {
            uint8_t d = tmp[k], m = mine[k];
            if (trouble) m |= d;        // not sure what's in use: free nothing
            for (uint8_t x = (uint8_t)(d & ~m); x; x &= x - 1) leaked++;
            for (uint8_t x = (uint8_t)(m & ~d); x; x &= x - 1) missing++;
            mine[k] = m;
            differs |= m != d;
        }
        if (differs && put_sector(bm_start + i, mine)) return MYFS_ERR_IO;
    }
    memset(bm_dirty, 0, sizeof bm_dirty);
    recount_free();
    nfiles = (int)W.files;
    ndirs = (int)W.dirs;
    rep->files = W.files;
    rep->dirs = W.dirs;
    rep->used_kb = (total - free_sectors) / 2;
    rep->leaked_kb = (leaked + 1) / 2;
    rep->missing_kb = (missing + 1) / 2;
    rep->damaged = W.damaged;
    rep->too_deep = W.deep + W.io;
    wreport = 0;
    return 0;
}

static int v2_mount(void) {
    const uint8_t *s = tmp;             // sector 0, read by myfs_mount
    total = rd32(s + 8);
    bm_start = rd32(s + 12);
    bm_sectors = rd32(s + 16);
    if (total < 32 || total > dev.sectors || total > MAX_SECTORS ||
        bm_sectors != (total + 4095) / 4096 || bm_start == 0 || bm_start + bm_sectors > total) {
        return MYFS_ERR_CORRUPT;            // MyFS v2, but not a sane one: never "format" it
    }
    cache_clear();
    alloc_hint = bm_start + bm_sectors;
    int r = rebuild(&mount_report);
    if (r) return r;
    mounted = 2;
    generation++;
    return 0;
}

// a fresh v2 volume: bitmap right after the superblock, then "/"
static int v2_format(uint32_t sectors) {
    if (sectors > MAX_SECTORS) sectors = MAX_SECTORS;
    if (sectors < 64) return MYFS_ERR_FULL;
    total = sectors;
    bm_start = 1;
    bm_sectors = (total + 4095) / 4096;
    uint32_t root = bm_start + bm_sectors;
    cache_clear();
    memset(bm, 0, bm_sectors * 512);
    memset(bm_dirty, 0, sizeof bm_dirty);
    mark_tail();
    free_sectors = total;
    mark(0, root + DIR_GROW, 1);
    if (zero_sectors(root, DIR_GROW)) return MYFS_ERR_IO;
    for (uint32_t i = 0; i < bm_sectors; i++) {
        if (put_sector(bm_start + i, &bm[i * 512])) return MYFS_ERR_IO;
    }
    memset(bm_dirty, 0, sizeof bm_dirty);
    memset(tmp, 0, 512);
    memcpy(tmp, V2_MAGIC, 8);
    wr32(tmp + 8, total);
    wr32(tmp + 12, bm_start);
    wr32(tmp + 16, bm_sectors);
    uint8_t *e = tmp + ENT;                         // the entry of "/"
    wr32(e + E_TYPE, MYFS_DIR);
    wr32(e + E_SIZE, DIR_GROW * 512);
    wr32(e + E_X, root);
    wr32(e + E_X + 4, DIR_GROW);
    if (put_sector(0, tmp)) return MYFS_ERR_IO;
    nfiles = ndirs = 0;
    alloc_hint = root + DIR_GROW;
    memset(&mount_report, 0, sizeof mount_report);
    mounted = 2;
    generation++;
    return 0;
}

// ===============================================================
//  the API: v1 or v2, whichever is mounted
// ===============================================================
int myfs_mount(const myfs_dev_t *d) {
    dev = *d;
    mounted = 0;
    total = 0;
    if (dev_read(0, tmp)) return MYFS_ERR_IO;
    if (memcmp(tmp, V2_MAGIC, 8) == 0) return v2_mount();
    if (memcmp(tmp, "MYFS0001", 8) != 0) return MYFS_ERR_NOFS;
    memcpy(v1_super, tmp, 512);
    for (int i = 0; i < V1_DIR_SECTORS; i++) {
        if (dev_read(V1_DIR_START + i, &v1_dir[i * 512])) return MYFS_ERR_IO;
    }
    mounted = 1;
    generation++;
    return 0;
}

int myfs_mounted(void) { return mounted != 0; }
int myfs_version(void) { return mounted; }

int myfs_format(const myfs_dev_t *d) {
    dev = *d;
    mounted = 0;
    return v2_format(dev.sectors);
}

uint32_t myfs_free_kb(void) {
    if (mounted == 1) return (rd32(v1_super + 8) - rd32(v1_super + 12)) / 2;
    return mounted == 2 ? free_sectors / 2 : 0;
}

uint32_t myfs_total_kb(void) {
    if (mounted == 1) return rd32(v1_super + 8) / 2;
    return mounted == 2 ? total / 2 : 0;
}

int myfs_count(void) {
    if (mounted == 2) return nfiles;
    int n = 0;
    if (mounted == 1) {
        for (int i = 0; i < MYFS_MAX_FILES; i++) n += v1_used(i);
    }
    return n;
}

int myfs_dir_count(void) { return mounted == 2 ? ndirs : 0; }

int myfs_valid_name(const char *path) {
    char norm[MYFS_PATH_MAX];
    if (myfs_normalize(0, path, norm) || norm[1] == 0) return 0;
    if (mounted == 1) return find_char(norm + 1, '/') == 0 && strlen(norm + 1) <= MYFS_V1_NAME_MAX;
    return 1;
}

int myfs_file_entry(const char *dir, int index, char *name, uint32_t *size) {
    if (mounted == 2) {
        uint32_t sz = 0;
        int r = v2_readdir(dir, index, 1, name, &sz);
        if (r != MYFS_FILE) return -1;
        *size = sz;
        return 0;
    }
    if (mounted != 1) return -1;
    char norm[MYFS_PATH_MAX];
    if (myfs_normalize(0, dir, norm) || norm[1] != 0) return -1;      // v1: only "/"
    for (int i = 0; i < MYFS_MAX_FILES; i++) {
        if (!v1_used(i)) continue;
        if (index-- == 0) {
            memcpy(name, v1_entry(i), MYFS_V1_NAME_MAX);
            name[MYFS_V1_NAME_MAX] = 0;
            *size = rd32(v1_entry(i) + 52);
            return 0;
        }
    }
    return -1;
}

int myfs_entry(int index, char *name, uint32_t *size) {
    return myfs_file_entry("/", index, name, size);
}

int myfs_readdir(const char *dir, int index, char *name, uint32_t *size) {
    if (mounted == 2) return v2_readdir(dir, index, 0, name, size);
    if (mounted != 1) return MYFS_ERR_NOFS;
    uint32_t sz;
    char norm[MYFS_PATH_MAX];
    if (myfs_normalize(0, dir, norm)) return MYFS_ERR_NAME;
    if (norm[1] != 0) return v1_find_path(norm) >= 0 ? MYFS_ERR_NOTDIR : MYFS_ERR_NOTFOUND;
    if (myfs_file_entry("/", index, name, &sz)) return 0;
    if (size) *size = sz;
    return MYFS_FILE;
}

int myfs_stat(const char *path, uint32_t *size) {
    uint32_t sz = 0;
    int type;
    if (mounted == 2) {
        loc_t l;
        int r = lookup(path, &l);
        if (r) return r;
        type = (int)etype(l.e);
        sz = rd32(l.e + E_SIZE);
    } else if (mounted == 1) {
        int i = v1_find_path(path);
        if (i == MYFS_ERR_ISDIR) {
            type = MYFS_DIR;
        } else if (i < 0) {
            return i;
        } else {
            type = MYFS_FILE;
            sz = rd32(v1_entry(i) + 52);
        }
    } else {
        return MYFS_ERR_NOFS;
    }
    if (size) *size = sz;
    return type;
}

int32_t myfs_size(const char *name) {
    uint32_t size;
    int t = myfs_stat(name, &size);
    if (t < 0) return t;
    return t == MYFS_FILE ? (int32_t)size : MYFS_ERR_ISDIR;
}

int myfs_read(const char *name, uint32_t offset, void *out, uint32_t len) {
    if (mounted == 1) {
        int i = v1_find_path(name);
        return i < 0 ? i : v1_read(i, offset, out, len);
    }
    if (mounted != 2) return MYFS_ERR_NOFS;
    loc_t l;
    int r = lookup(name, &l);
    if (r) return r;
    if (etype(l.e) != MYFS_FILE) return MYFS_ERR_ISDIR;
    return v2_read(&l, offset, out, len);
}

int myfs_write(const char *name, const void *data, uint32_t size) {
    if (mounted == 1) return v1_write(name, data, -1, size);
    if (mounted != 2) return MYFS_ERR_NOFS;
    return v2_write(name, data, 0, size);
}

int myfs_copy(const char *from, const char *to) {
    if (mounted == 1) {
        int i = v1_find_path(from);
        return i < 0 ? i : v1_write(to, 0, i, rd32(v1_entry(i) + 52));
    }
    if (mounted != 2) return MYFS_ERR_NOFS;
    loc_t l;
    char a[MYFS_PATH_MAX], b[MYFS_PATH_MAX];
    if (myfs_normalize(0, from, a) || myfs_normalize(0, to, b)) return MYFS_ERR_NAME;
    int r = lookup(a, &l);
    if (r) return r;
    if (etype(l.e) != MYFS_FILE) return MYFS_ERR_ISDIR;
    if (strcmp(a, b) == 0) return 0;                // a copy onto itself
    return v2_write(b, 0, l.e, rd32(l.e + E_SIZE));
}

int myfs_remove(const char *name) {
    if (mounted == 1) {
        int i = v1_find_path(name);
        if (i < 0) return i;
        wr32(v1_entry(i) + 60, 0);
        return v1_flush();
    }
    if (mounted != 2) return MYFS_ERR_NOFS;
    loc_t l;
    int r = lookup(name, &l);
    if (r) return r;
    if (etype(l.e) != MYFS_FILE) return MYFS_ERR_ISDIR;
    uint8_t e[ENT];
    memset(e, 0, ENT);
    r = put_entry(l.lba, l.slot, e);                // gone from its folder...
    if (r) return r;
    own_sectors(l.e, 0, 0);                         // ...then its space is free
    nfiles--;
    return flush_bitmap();
}

int myfs_mkdir(const char *path) {
    if (mounted == 1) return MYFS_ERR_V1;
    if (mounted != 2) return MYFS_ERR_NOFS;
    loc_t dir, slot;
    char leaf[MYFS_NAME_MAX + 1], norm[MYFS_PATH_MAX];
    if (myfs_normalize(0, path, norm)) return MYFS_ERR_NAME;
    if (norm[1] == 0) return MYFS_ERR_EXISTS;               // "/"
    int r = lookup_parent(path, &dir, leaf, norm);
    if (r) return r;
    r = dir_find(dir.e, leaf, 0, &slot);
    if (r == 0) return MYFS_ERR_EXISTS;
    if (r != MYFS_ERR_NOTFOUND) return r;
    if (depth_of(norm) > MYFS_DEPTH_MAX) return MYFS_ERR_NAME;
    if (!slot.lba && (r = dir_grow(&dir, &slot))) return r;
    if ((r = alloc_file(DIR_FIRST))) return r;
    uint8_t e[ENT];
    memset(e, 0, ENT);
    memcpy(e, leaf, strlen(leaf));
    wr32(e + E_TYPE, MYFS_DIR);
    wr32(e + E_SIZE, DIR_FIRST * 512);
    for (int k = 0; k < nxs && !r; k++) r = zero_sectors(xs[k][0], xs[k][1]);
    if (!r) r = store_extents(e);
    if (!r) r = flush_bitmap();
    if (!r) r = put_entry(slot.lba, slot.slot, e);
    if (r) {
        unmark_alloc();
        flush_bitmap();
        return r;
    }
    ndirs++;
    return 0;
}

int myfs_rmdir(const char *path) {
    if (mounted == 1) return v1_find_path(path) >= 0 ? MYFS_ERR_NOTDIR : MYFS_ERR_V1;
    if (mounted != 2) return MYFS_ERR_NOFS;
    loc_t l;
    int r = lookup(path, &l);
    if (r) return r;
    if (etype(l.e) != MYFS_DIR) return MYFS_ERR_NOTDIR;
    if (l.lba == 0) return MYFS_ERR_NAME;           // not "/"
    char name[MYFS_NAME_MAX + 1];
    r = v2_readdir(path, 0, 0, name, 0);
    if (r < 0) return r;
    if (r > 0) return MYFS_ERR_NOTEMPTY;
    uint8_t e[ENT];
    memset(e, 0, ENT);
    if ((r = put_entry(l.lba, l.slot, e))) return r;
    own_sectors(l.e, 0, 0);
    ndirs--;
    return flush_bitmap();
}

int myfs_rename(const char *from, const char *to) {
    if (mounted == 1) return v1_rename(from, to);
    if (mounted != 2) return MYFS_ERR_NOFS;
    char nfrom[MYFS_PATH_MAX], nto[MYFS_PATH_MAX], leaf[MYFS_NAME_MAX + 1];
    if (myfs_normalize(0, from, nfrom)) return MYFS_ERR_NAME;
    loc_t src, dir, dst, slot;
    int r = lookup(nfrom, &src);
    if (r) return r;
    if (src.lba == 0) return MYFS_ERR_NAME;         // "/" stays where it is
    if ((r = lookup_parent(to, &dir, leaf, nto))) return r;
    if (strcmp(nfrom, nto) == 0) return 0;
    size_t n = strlen(nfrom);
    if (etype(src.e) == MYFS_DIR && strncmp(nto, nfrom, n) == 0 && nto[n] == '/') {
        return MYFS_ERR_NAME;                       // a folder can't go inside itself
    }
    if (depth_of(nto) > MYFS_DEPTH_MAX) return MYFS_ERR_NAME;
    uint8_t e[ENT];
    memcpy(e, src.e, ENT);
    memset(e, 0, 64);
    memcpy(e, leaf, strlen(leaf));
    r = dir_find(dir.e, leaf, &dst, &slot);
    if (r == 0) {                                   // the new name is taken
        if (etype(dst.e) != MYFS_FILE || etype(src.e) != MYFS_FILE) return MYFS_ERR_EXISTS;
        if ((r = put_entry(dst.lba, dst.slot, e))) return r;   // file over file: replace it
        uint8_t zero[ENT];
        memset(zero, 0, ENT);
        if ((r = put_entry(src.lba, src.slot, zero))) return r;
        own_sectors(dst.e, 0, 0);
        nfiles--;
        return flush_bitmap();
    }
    if (r != MYFS_ERR_NOTFOUND) return r;
    // same folder? then only the name changes, in place
    char pfrom[MYFS_PATH_MAX];
    strcpy(pfrom, nfrom);
    *last_char(pfrom, '/') = 0;
    const char *pto_end = last_char(nto, '/');
    if (strlen(pfrom) == (size_t)(pto_end - nto) && strncmp(pfrom, nto, strlen(pfrom)) == 0) {
        return put_entry(src.lba, src.slot, e);
    }
    // another folder: write the new entry, then clear the old one
    if (!slot.lba && (r = dir_grow(&dir, &slot))) return r;
    if ((r = put_entry(slot.lba, slot.slot, e))) return r;
    uint8_t zero[ENT];
    memset(zero, 0, ENT);
    return put_entry(src.lba, src.slot, zero);
}

int myfs_check(int fix, myfs_check_t *rep) {
    if (mounted != 2) return mounted == 1 ? MYFS_ERR_V1 : MYFS_ERR_NOFS;
    cache_clear();                                  // read everything fresh from the card
    int r = rebuild(rep);
    if (r || !fix || !W.damaged) return r;
    // remove the damaged entries (their sectors stay with whoever else has them)
    uint8_t zero[ENT];
    memset(zero, 0, ENT);
    for (int i = 0; i < W.nbad; i++) {
        if ((r = put_entry(W.bad[i].lba, W.bad[i].slot, zero))) return r;
    }
    uint32_t damaged = rep->damaged;
    char paths[4][80];
    memcpy(paths, rep->damaged_path, sizeof paths);
    r = rebuild(rep);                               // and look again
    rep->damaged = damaged;
    memcpy(rep->damaged_path, paths, sizeof paths);
    return r;
}

const myfs_check_t *myfs_mount_check(void) { return &mount_report; }

// ---------------------------------------------------------------
//  v1 -> v2, in place. No file data moves: v2 extents point at the
//  sectors v1 already uses. The new bitmap and "/" folder are written
//  into free space; the old superblock and directory (sectors 0-15) are
//  not touched until the very last step, which writes sector 0 with the
//  v2 superblock. Power lost before that = still the old v1 volume.
// ---------------------------------------------------------------
int myfs_upgrade(void) {
    if (mounted != 1) return mounted == 2 ? MYFS_ERR_EXISTS : MYFS_ERR_NOFS;
    // v2 gets the whole myfs.img (v1 only knew the size it was formatted with;
    // anything after that is unused)
    uint32_t tot = dev.sectors;
    if (tot > MAX_SECTORS) tot = MAX_SECTORS;
    if (tot < rd32(v1_super + 8) || tot < 64) return MYFS_ERR_CORRUPT;
    int list[MYFS_MAX_FILES], n = 0;
    for (int i = 0; i < MYFS_MAX_FILES; i++) {
        if (!v1_used(i)) continue;
        uint8_t *e = v1_entry(i);
        uint32_t start = rd32(e + 48), size = rd32(e + 52), need = (size + 511) / 512;
        if (need && (need > rd32(e + 56) || start < V1_DATA_START || start + need > tot)) {
            return MYFS_ERR_CORRUPT;
        }
        for (int j = 0; j < n; j++) {               // two files must not share sectors
            uint8_t *o = v1_entry(list[j]);
            uint32_t os = rd32(o + 48), on = (rd32(o + 52) + 511) / 512;
            if (need && on && start < os + on && os < start + need) return MYFS_ERR_CORRUPT;
        }
        list[n++] = i;
    }
    // v2 names: no '/', not "." or "..", all different
    char names[MYFS_MAX_FILES][MYFS_NAME_MAX + 1];
    for (int j = 0; j < n; j++) {
        char *nm = names[j];
        memcpy(nm, v1_entry(list[j]), MYFS_V1_NAME_MAX);
        nm[MYFS_V1_NAME_MAX] = 0;
        for (char *c = nm; *c; c++) {
            if (*c == '/' || *c <= ' ' || *c > '~') *c = '_';
        }
        if (nm[0] == 0 || strcmp(nm, ".") == 0 || strcmp(nm, "..") == 0) {
            memmove(nm + 1, nm, strlen(nm) + 1);
            nm[0] = '_';
        }
        for (int k = 0, tries = 0; k < j && tries < 100; k++) {
            if (strcmp(nm, names[k]) != 0) continue;
            size_t len = strlen(nm);
            if (len > MYFS_NAME_MAX - 2) len = MYFS_NAME_MAX - 2;
            nm[len] = '~';
            nm[len + 1] = (char)('0' + tries % 10);
            nm[len + 2] = 0;
            tries++;
            k = -1;                                 // compare with all of them again
        }
    }
    // the new bitmap, in RAM: v1's sectors 0-15 + the files
    total = tot;
    bm_sectors = (tot + 4095) / 4096;
    cache_clear();
    memset(bm, 0, bm_sectors * 512);
    memset(bm_dirty, 0, sizeof bm_dirty);
    mark_tail();
    free_sectors = total;
    mark(0, V1_DATA_START, 1);
    for (int j = 0; j < n; j++) {
        uint8_t *e = v1_entry(list[j]);
        mark(rd32(e + 48), (rd32(e + 52) + 511) / 512, 1);
    }
    uint32_t root_sectors = (uint32_t)(n + 32 + 3) / 4;     // room for 32 more files
    uint32_t b = find_run(V1_DATA_START, total, bm_sectors);
    if (!b) { total = 0; return MYFS_ERR_FULL; }
    mark(b, bm_sectors, 1);
    uint32_t root = find_run(V1_DATA_START, total, root_sectors);
    if (!root) { total = 0; return MYFS_ERR_FULL; }
    mark(root, root_sectors, 1);
    bm_start = b;
    // "/" with one entry per file
    for (uint32_t k = 0; k < root_sectors; k++) {
        memset(tmp, 0, 512);
        for (int slot = 0; slot < 4; slot++) {
            int j = (int)k * 4 + slot;
            if (j >= n) break;
            uint8_t *v = v1_entry(list[j]), *e = tmp + slot * ENT;
            uint32_t size = rd32(v + 52);
            memcpy(e, names[j], strlen(names[j]));
            wr32(e + E_TYPE, MYFS_FILE);
            wr32(e + E_SIZE, size);
            if (size) {
                wr32(e + E_X, rd32(v + 48));
                wr32(e + E_X + 4, (size + 511) / 512);
            }
        }
        if (put_sector(root + k, tmp)) { total = 0; return MYFS_ERR_IO; }
    }
    mark(1, V1_DATA_START - 1, 0);          // the old directory: free once v2 is live
    for (uint32_t i = 0; i < bm_sectors; i++) {
        if (put_sector(b + i, &bm[i * 512])) { total = 0; return MYFS_ERR_IO; }
    }
    // the switch: one sector
    memset(tmp, 0, 512);
    memcpy(tmp, V2_MAGIC, 8);
    wr32(tmp + 8, total);
    wr32(tmp + 12, bm_start);
    wr32(tmp + 16, bm_sectors);
    wr32(tmp + 20, 1);                      // flags: upgraded from v1
    uint8_t *e = tmp + ENT;
    wr32(e + E_TYPE, MYFS_DIR);
    wr32(e + E_SIZE, root_sectors * 512);
    wr32(e + E_X, root);
    wr32(e + E_X + 4, root_sectors);
    if (put_sector(0, tmp)) { total = 0; return MYFS_ERR_IO; }
    // mount it and make sure every file is there, same size, same place
    uint8_t old[MYFS_MAX_FILES][3 * 4];
    for (int j = 0; j < n; j++) {
        uint8_t *v = v1_entry(list[j]);
        wr32(old[j], rd32(v + 48));
        wr32(old[j] + 4, rd32(v + 52));
    }
    myfs_dev_t d = dev;
    int r = myfs_mount(&d);
    if (r) return r;
    for (int j = 0; j < n; j++) {
        char path[MYFS_NAME_MAX + 2] = "/";
        strcpy(path + 1, names[j]);
        loc_t l;
        uint32_t size = rd32(old[j] + 4);
        if (lookup(path, &l) || rd32(l.e + E_SIZE) != size ||
            (size && rd32(l.e + E_X) != rd32(old[j]))) {
            return MYFS_ERR_CORRUPT;
        }
    }
    return n;
}
