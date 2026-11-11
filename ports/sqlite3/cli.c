/* cli.c - the sqlite3 command for MyOS: SQLite (third_party/sqlite, public
 * domain) with a small shell of our own, because SQLite's shell.c needs a
 * Unix system, and the "operating system layer" (VFS) that keeps databases
 * on MyFS.
 *
 *   sqlite3                    a database in memory
 *   sqlite3 shop.db            open (or create) shop.db
 *   sqlite3 shop.db "SQL"      run the SQL and quit
 *
 * MyFS reads and writes whole files, so an open database lives in memory and
 * goes back to the disk at each commit. The rollback journal stays in
 * memory too.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "sqlite3.h"
#include "myos.h"

/* =============================================================== the VFS */
typedef struct {
    sqlite3_file base;
    char name[48];
    unsigned char *data;
    sqlite3_int64 size, cap;
    int dirty;
    int on_disk;                /* the main database: written back to MyFS */
} mem_file;

static const char *fs_name(const char *name) {
    while (name[0] == '.' && name[1] == '/') name += 2;
    while (name[0] == '/') name++;
    return name;
}

static int mf_sync(sqlite3_file *f, int flags) {
    mem_file *m = (mem_file *)f;
    (void)flags;
    if (m->on_disk && m->dirty) {
        if (sys_write_file(m->name, m->data, (unsigned long)m->size) < 0) return SQLITE_IOERR_WRITE;
        m->dirty = 0;
    }
    return SQLITE_OK;
}

static int mf_close(sqlite3_file *f) {
    mem_file *m = (mem_file *)f;
    int rc = mf_sync(f, 0);
    free(m->data);
    m->data = NULL;
    return rc;
}

static int mf_read(sqlite3_file *f, void *buf, int amt, sqlite3_int64 off) {
    mem_file *m = (mem_file *)f;
    sqlite3_int64 n = off < m->size ? m->size - off : 0;
    if (n > amt) n = amt;
    if (n > 0) memcpy(buf, m->data + off, (size_t)n);
    if (n < amt) {
        memset((char *)buf + n, 0, (size_t)(amt - n));
        return SQLITE_IOERR_SHORT_READ;
    }
    return SQLITE_OK;
}

static int mf_grow(mem_file *m, sqlite3_int64 need) {
    if (need <= m->cap) return 1;
    sqlite3_int64 cap = m->cap ? m->cap : 4096;
    while (cap < need) cap *= 2;
    unsigned char *d = realloc(m->data, (size_t)cap);
    if (!d) return 0;
    m->data = d;
    m->cap = cap;
    return 1;
}

static int mf_write(sqlite3_file *f, const void *buf, int amt, sqlite3_int64 off) {
    mem_file *m = (mem_file *)f;
    if (!mf_grow(m, off + amt)) return SQLITE_IOERR_NOMEM;
    if (off > m->size) memset(m->data + m->size, 0, (size_t)(off - m->size));
    memcpy(m->data + off, buf, (size_t)amt);
    if (off + amt > m->size) m->size = off + amt;
    m->dirty = 1;
    return SQLITE_OK;
}

static int mf_truncate(sqlite3_file *f, sqlite3_int64 size) {
    mem_file *m = (mem_file *)f;
    if (size < m->size) {
        m->size = size;
        m->dirty = 1;
    }
    return SQLITE_OK;
}

static int mf_file_size(sqlite3_file *f, sqlite3_int64 *size) {
    *size = ((mem_file *)f)->size;
    return SQLITE_OK;
}

static int mf_lock(sqlite3_file *f, int level) { (void)f; (void)level; return SQLITE_OK; }
static int mf_check_lock(sqlite3_file *f, int *out) { (void)f; *out = 0; return SQLITE_OK; }
static int mf_control(sqlite3_file *f, int op, void *arg) { (void)f; (void)op; (void)arg; return SQLITE_NOTFOUND; }
static int mf_sector(sqlite3_file *f) { (void)f; return 512; }
static int mf_caps(sqlite3_file *f) { (void)f; return SQLITE_IOCAP_SAFE_APPEND | SQLITE_IOCAP_POWERSAFE_OVERWRITE; }

static const sqlite3_io_methods mem_io = {
    1, mf_close, mf_read, mf_write, mf_truncate, mf_sync, mf_file_size,
    mf_lock, mf_lock, mf_check_lock, mf_control, mf_sector, mf_caps,
};

static int vfs_open(sqlite3_vfs *vfs, sqlite3_filename zname, sqlite3_file *f, int flags, int *out_flags) {
    mem_file *m = (mem_file *)f;
    (void)vfs;
    memset(m, 0, sizeof *m);
    if (zname && (flags & SQLITE_OPEN_MAIN_DB)) {
        const char *name = fs_name(zname);
        if (!*name || strlen(name) >= sizeof m->name) return SQLITE_CANTOPEN;
        strcpy(m->name, name);
        m->on_disk = 1;
        long size = sys_file_size(name);
        if (size >= 0) {
            if (!mf_grow(m, size ? size : 1)) return SQLITE_NOMEM;
            if (size && sys_read_file(name, m->data, (unsigned long)size) != size) {
                free(m->data);
                return SQLITE_IOERR_READ;
            }
            m->size = size;
        } else if (!(flags & SQLITE_OPEN_CREATE)) {
            return SQLITE_CANTOPEN;
        }
    }
    /* journals and temporary files live only in memory */
    m->base.pMethods = &mem_io;
    if (out_flags) *out_flags = flags;
    return SQLITE_OK;
}

static int vfs_delete(sqlite3_vfs *vfs, const char *name, int sync_dir) {
    (void)vfs;
    (void)sync_dir;
    sys_delete_file(fs_name(name));     /* journals were never on the disk */
    return SQLITE_OK;
}

static int vfs_access(sqlite3_vfs *vfs, const char *name, int flags, int *out) {
    (void)vfs;
    (void)flags;
    size_t n = strlen(name);
    /* journal files are only ever in memory: never a "hot journal" to roll back */
    *out = (n > 8 && strcmp(name + n - 8, "-journal") == 0) ? 0 : sys_file_size(fs_name(name)) >= 0;
    return SQLITE_OK;
}

static int vfs_full_name(sqlite3_vfs *vfs, const char *name, int n, char *out) {
    (void)vfs;
    sqlite3_snprintf(n, out, "%s", fs_name(name));
    return SQLITE_OK;
}

static int vfs_random(sqlite3_vfs *vfs, int n, char *out) {
    static unsigned long x;
    (void)vfs;
    if (!x) x = sys_ticks() * 2654435761UL + 88172645463325252UL;
    for (int i = 0; i < n; i++) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        out[i] = (char)(x >> 24);
    }
    return n;
}

static int vfs_sleep(sqlite3_vfs *vfs, int us) {
    (void)vfs;
    sys_sleep((unsigned long)(us + 9999) / 10000);
    return us;
}

static int vfs_time(sqlite3_vfs *vfs, double *t) {
    (void)vfs;
    *t = 2440587.5 + (double)time(NULL) / 86400.0;     /* Julian day */
    return SQLITE_OK;
}

static int vfs_error(sqlite3_vfs *vfs, int n, char *buf) {
    (void)vfs;
    (void)n;
    (void)buf;
    return 0;
}

static sqlite3_vfs myfs_vfs = {
    1, sizeof(mem_file), 47, NULL, "myfs", NULL,
    vfs_open, vfs_delete, vfs_access, vfs_full_name, NULL, NULL, NULL, NULL,
    vfs_random, vfs_sleep, vfs_time, vfs_error,
};

int sqlite3_os_init(void) { return sqlite3_vfs_register(&myfs_vfs, 1); }
int sqlite3_os_end(void) { return SQLITE_OK; }

/* =============================================================== the shell */
enum { M_LIST, M_CSV, M_LINE, M_COLUMN, M_TABLE, M_BOX, M_JSON, M_INSERT };
static const char *const mode_names[] = { "list", "csv", "line", "column", "table", "box", "json", "insert" };

static sqlite3 *db;
static char db_name[48];
static FILE *out;
static char out_name[48];           /* .output FILE ("" = the screen) */
static int once;                    /* .once: back to the screen after one command */
static int mode = M_LIST, headers = 0, timer = 0, changes_shown = 0;
static char separator[8] = "|";
static char null_text[16] = "";
static int nerrors;

static void set_output(const char *name) {
    if (out != stdout) fclose(out);
    out = stdout;
    out_name[0] = 0;
    if (name && *name && strcmp(name, "stdout") != 0) {
        FILE *f = fopen(fs_name(name), "w");
        if (!f) printf("Error: cannot write to \"%s\"\n", name);
        else {
            out = f;
            snprintf(out_name, sizeof out_name, "%s", fs_name(name));
        }
    }
}

static const char *cell(sqlite3_stmt *st, int i) {
    const char *s = (const char *)sqlite3_column_text(st, i);
    return s ? s : null_text;
}

static void put_csv(const char *s) {
    if (strpbrk(s, ",\"\n\r") || (*s == ' ')) {
        fputc('"', out);
        for (; *s; s++) {
            if (*s == '"') fputc('"', out);
            fputc(*s, out);
        }
        fputc('"', out);
    } else {
        fputs(s, out);
    }
}

static void put_json_string(const char *s) {
    fputc('"', out);
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') fprintf(out, "\\%c", c);
        else if (c == '\n') fputs("\\n", out);
        else if (c == '\t') fputs("\\t", out);
        else if (c == '\r') fputs("\\r", out);
        else if (c < 32) fprintf(out, "\\u%04x", c);
        else fputc(c, out);
    }
    fputc('"', out);
}

/* SQL text for one value: 12, 1.5, 'it''s', NULL, X'00ff' */
static void put_sql_value(sqlite3_stmt *st, int i) {
    switch (sqlite3_column_type(st, i)) {
        case SQLITE_NULL: fputs("NULL", out); break;
        case SQLITE_INTEGER:
        case SQLITE_FLOAT: fputs((const char *)sqlite3_column_text(st, i), out); break;
        case SQLITE_BLOB: {
            const unsigned char *b = sqlite3_column_blob(st, i);
            int n = sqlite3_column_bytes(st, i);
            fputs("X'", out);
            for (int k = 0; k < n; k++) fprintf(out, "%02x", b[k]);
            fputc('\'', out);
            break;
        }
        default: {
            char *q = sqlite3_mprintf("%Q", sqlite3_column_text(st, i));
            fputs(q, out);
            sqlite3_free(q);
        }
    }
}

/* how many characters a UTF-8 string shows as (Thai marks take no room) */
static int width_of(const char *s) {
    int w = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if ((*p & 0xC0) == 0x80) continue;
        if (p[0] == 0xE0 && p[1] == 0xB8 && (p[2] == 0xB1 || (p[2] >= 0xB4 && p[2] <= 0xBA))) continue;
        if (p[0] == 0xE0 && p[1] == 0xB9 && p[2] >= 0x87 && p[2] <= 0x8E) continue;
        w++;
    }
    return w;
}

static void pad(const char *s, int w, int right) {
    int n = w - width_of(s);
    if (right) while (n-- > 0) fputc(' ', out);
    fputs(s, out);
    if (!right) while (n-- > 0) fputc(' ', out);
}

/* column / table / box: all rows first, to know how wide each column is */
static void print_grid(sqlite3_stmt *st, int ncol, int *rc) {
    int cap = 64, nrows = 0;
    char **cells = malloc(sizeof(char *) * (size_t)ncol * (size_t)cap);
    unsigned char *right = calloc((size_t)ncol, 1);        /* numbers go right */
    int *w = calloc((size_t)ncol, sizeof(int));
    if (!cells || !right || !w) {
        free(cells); free(right); free(w);
        *rc = SQLITE_NOMEM;
        return;
    }
    for (int i = 0; i < ncol; i++) w[i] = width_of(sqlite3_column_name(st, i));
    while ((*rc = sqlite3_step(st)) == SQLITE_ROW) {
        if (nrows == cap) {
            char **c2 = realloc(cells, sizeof(char *) * (size_t)ncol * (size_t)(cap * 2));
            if (!c2) { *rc = SQLITE_NOMEM; break; }
            cells = c2;
            cap *= 2;
        }
        for (int i = 0; i < ncol; i++) {
            int t = sqlite3_column_type(st, i);
            char *s = strdup(cell(st, i));
            cells[nrows * ncol + i] = s;
            if (t == SQLITE_INTEGER || t == SQLITE_FLOAT) right[i] = 1;
            int sw = s ? width_of(s) : 0;
            if (sw > w[i]) w[i] = sw;
        }
        nrows++;
    }
    static const char *const box[3][4] = {      /* left, middle, right, line */
        { "\xe2\x94\x8c", "\xe2\x94\xac", "\xe2\x94\x90", "\xe2\x94\x80" },
        { "\xe2\x94\x9c", "\xe2\x94\xbc", "\xe2\x94\xa4", "\xe2\x94\x80" },
        { "\xe2\x94\x94", "\xe2\x94\xb4", "\xe2\x94\x98", "\xe2\x94\x80" },
    };
    const char *bar = mode == M_BOX ? "\xe2\x94\x82" : "|";
    for (int part = 0; part < 4; part++) {             /* 0 top, 1 header, 2 rows, 3 bottom */
        if ((part == 0 || part == 3) && mode == M_COLUMN) continue;
        if (part == 0 || part == 3) {
            int k = part == 0 ? 0 : 2;
            for (int i = 0; i < ncol; i++) {
                fputs(mode == M_BOX ? box[k][i ? 1 : 0] : "+", out);
                for (int j = 0; j < w[i] + 2; j++) fputs(mode == M_BOX ? box[k][3] : "-", out);
            }
            fputs(mode == M_BOX ? box[k][2] : "+", out);
            fputc('\n', out);
            continue;
        }
        if (part == 1 && !headers && mode == M_COLUMN) continue;
        int first = part == 1 ? 0 : 0, last = part == 1 ? 1 : nrows;
        for (int r = first; r < last; r++) {
            for (int i = 0; i < ncol; i++) {
                const char *s = part == 1 ? sqlite3_column_name(st, i) : cells[r * ncol + i];
                if (!s) s = "";
                if (mode == M_COLUMN) {
                    if (i) fputs("  ", out);
                    pad(s, w[i], part == 2 && right[i]);
                } else {
                    fputs(bar, out);
                    fputc(' ', out);
                    pad(s, w[i], part == 2 && right[i]);
                    fputc(' ', out);
                }
            }
            if (mode != M_COLUMN) fputs(bar, out);
            fputc('\n', out);
        }
        if (part == 1) {                                /* the line under the header */
            for (int i = 0; i < ncol; i++) {
                if (mode == M_COLUMN) {
                    if (i) fputs("  ", out);
                    for (int j = 0; j < w[i]; j++) fputc('-', out);
                } else {
                    fputs(mode == M_BOX ? box[1][i ? 1 : 0] : "+", out);
                    for (int j = 0; j < w[i] + 2; j++) fputs(mode == M_BOX ? box[1][3] : "-", out);
                }
            }
            if (mode != M_COLUMN) fputs(mode == M_BOX ? box[1][2] : "+", out);
            fputc('\n', out);
        }
    }
    for (int i = 0; i < nrows * ncol; i++) free(cells[i]);
    free(cells);
    free(right);
    free(w);
}

/* run one prepared statement and show its rows */
static int show_rows(sqlite3_stmt *st, const char *table) {
    int ncol = sqlite3_column_count(st), rc, row = 0;
    if (ncol == 0) {
        while ((rc = sqlite3_step(st)) == SQLITE_ROW) {}
        return rc;
    }
    if (mode == M_COLUMN || mode == M_TABLE || mode == M_BOX) {
        print_grid(st, ncol, &rc);
        return rc;
    }
    if (headers && (mode == M_LIST || mode == M_CSV)) {
        for (int i = 0; i < ncol; i++) {
            if (i) fputs(mode == M_CSV ? "," : separator, out);
            if (mode == M_CSV) put_csv(sqlite3_column_name(st, i));
            else fputs(sqlite3_column_name(st, i), out);
        }
        fputc('\n', out);
    }
    while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
        switch (mode) {
            case M_LINE: {
                int wn = 0;
                for (int i = 0; i < ncol; i++) {
                    int n = (int)strlen(sqlite3_column_name(st, i));
                    if (n > wn) wn = n;
                }
                if (row) fputc('\n', out);
                for (int i = 0; i < ncol; i++) fprintf(out, "%*s = %s\n", wn, sqlite3_column_name(st, i), cell(st, i));
                break;
            }
            case M_JSON:
                fputs(row ? ",\n{" : "[{", out);
                for (int i = 0; i < ncol; i++) {
                    if (i) fputc(',', out);
                    put_json_string(sqlite3_column_name(st, i));
                    fputc(':', out);
                    int t = sqlite3_column_type(st, i);
                    if (t == SQLITE_NULL) fputs("null", out);
                    else if (t == SQLITE_INTEGER || t == SQLITE_FLOAT) fputs(cell(st, i), out);
                    else put_json_string(cell(st, i));
                }
                fputc('}', out);
                break;
            case M_INSERT:
                fprintf(out, "INSERT INTO %s VALUES(", table ? table : "\"table\"");
                for (int i = 0; i < ncol; i++) {
                    if (i) fputc(',', out);
                    put_sql_value(st, i);
                }
                fputs(");\n", out);
                break;
            default:
                for (int i = 0; i < ncol; i++) {
                    if (i) fputs(mode == M_CSV ? "," : separator, out);
                    if (mode == M_CSV) put_csv(cell(st, i));
                    else fputs(cell(st, i), out);
                }
                fputc('\n', out);
        }
        row++;
    }
    if (mode == M_JSON && row) fputs("]\n", out);
    return rc;
}

/* run all the statements in sql; 0 = ok */
static int run_sql(const char *sql, const char *table) {
    unsigned long t0 = sys_ticks();
    int failed = 0;
    while (sql && *sql) {
        sqlite3_stmt *st = NULL;
        const char *tail = NULL;
        int rc = sqlite3_prepare_v2(db, sql, -1, &st, &tail);
        if (rc != SQLITE_OK) {
            printf("Parse error: %s\n", sqlite3_errmsg(db));
            failed = 1;
            break;
        }
        if (st) {
            rc = show_rows(st, table);
            if (rc != SQLITE_DONE) {
                printf("Runtime error: %s\n", sqlite3_errmsg(db));
                failed = 1;
            }
            sqlite3_finalize(st);
            if (failed) break;
        }
        sql = tail;
    }
    if (changes_shown && !failed) printf("changes: %d   total_changes: %d\n", sqlite3_changes(db), sqlite3_total_changes(db));
    if (timer) printf("Run Time: real %.2f\n", (double)(sys_ticks() - t0) / 100.0);
    if (failed) nerrors++;
    fflush(out);
    return failed;
}

/* every row of a query, given as a printf-like format */
static void query_print(const char *fmt, const char *arg) {
    char *sql = sqlite3_mprintf(fmt, arg);
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) fprintf(out, "%s\n", cell(st, 0));
        sqlite3_finalize(st);
    } else {
        printf("Error: %s\n", sqlite3_errmsg(db));
    }
    sqlite3_free(sql);
}

static int open_db(const char *name) {
    if (db) sqlite3_close(db);
    db = NULL;
    snprintf(db_name, sizeof db_name, "%s", name && *name ? fs_name(name) : ":memory:");
    if (sqlite3_open(db_name, &db) != SQLITE_OK) {
        printf("Error: cannot open \"%s\": %s\n", db_name, db ? sqlite3_errmsg(db) : "out of memory");
        sqlite3_close(db);
        db = NULL;
        sqlite3_open(":memory:", &db);
        strcpy(db_name, ":memory:");
        return 0;
    }
    return 1;
}

static void dump(const char *table) {
    fputs("PRAGMA foreign_keys=OFF;\nBEGIN TRANSACTION;\n", out);
    sqlite3_stmt *st;
    char *sql = sqlite3_mprintf("SELECT name, sql FROM sqlite_schema WHERE type='table' AND sql NOT NULL"
                                " AND name NOT LIKE 'sqlite_%%' AND (%Q IS NULL OR name LIKE %Q) ORDER BY rowid",
                                table, table);
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) == SQLITE_OK) {
        int saved = mode;
        while (sqlite3_step(st) == SQLITE_ROW) {
            const char *name = cell(st, 0);
            fprintf(out, "%s;\n", cell(st, 1));
            char *q = sqlite3_mprintf("SELECT * FROM \"%w\"", name);
            char *qname = sqlite3_mprintf("\"%w\"", name);
            mode = M_INSERT;
            run_sql(q, qname);
            mode = saved;
            sqlite3_free(q);
            sqlite3_free(qname);
        }
        sqlite3_finalize(st);
    }
    sqlite3_free(sql);
    query_print("SELECT sql || ';' FROM sqlite_schema WHERE type IN ('index','trigger','view') AND sql NOT NULL"
                " AND (%Q IS NULL OR tbl_name LIKE %Q) ORDER BY rowid", table);
    fputs("COMMIT;\n", out);
}

/* .import FILE TABLE: CSV (the first line names the columns if TABLE is new) */
static void import_csv(const char *file, const char *table) {
    FILE *f = fopen(fs_name(file), "r");
    if (!f) {
        printf("Error: cannot open \"%s\"\n", file);
        return;
    }
    char *fields[64];
    static char line[4096];
    int ncol = 0, rows = 0, lineno = 0;
    sqlite3_stmt *ins = NULL;
    char *sql = sqlite3_mprintf("SELECT * FROM \"%w\"", table);
    sqlite3_stmt *probe;
    int exists = sqlite3_prepare_v2(db, sql, -1, &probe, NULL) == SQLITE_OK;
    if (exists) {
        ncol = sqlite3_column_count(probe);
        sqlite3_finalize(probe);
    }
    sqlite3_free(sql);
    sqlite3_exec(db, "BEGIN", NULL, NULL, NULL);
    while (fgets(line, sizeof line, f)) {
        lineno++;
        size_t len = strlen(line);
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = 0;
        if (!len) continue;
        int n = 0;                                      /* split, honouring "quoted, fields" */
        char *p = line, *w;
        while (n < 64) {
            fields[n++] = w = p;
            if (*p == '"') {
                fields[n - 1] = w = ++p;
                while (*p) {
                    if (*p == '"' && p[1] == '"') { *w++ = '"'; p += 2; }
                    else if (*p == '"') { p++; break; }
                    else *w++ = *p++;
                }
                while (*p && *p != ',') p++;
            } else {
                while (*p && *p != ',') w = ++p;
            }
            int more = *p == ',';
            *w = 0;
            if (!more) break;
            p++;
        }
        if (!ins) {
            if (!exists) {                              /* make the table from the header line */
                char *create = sqlite3_mprintf("CREATE TABLE \"%w\"(", table);
                for (int i = 0; i < n; i++) {
                    char *c = sqlite3_mprintf("%z%s\"%w\" TEXT", create, i ? ", " : "", fields[i]);
                    create = c;
                }
                create = sqlite3_mprintf("%z)", create);
                int rc = sqlite3_exec(db, create, NULL, NULL, NULL);
                sqlite3_free(create);
                if (rc != SQLITE_OK) {
                    printf("Error: %s\n", sqlite3_errmsg(db));
                    break;
                }
                ncol = n;
            }
            char *q = sqlite3_mprintf("INSERT INTO \"%w\" VALUES(?", table);
            for (int i = 1; i < ncol; i++) q = sqlite3_mprintf("%z,?", q);
            q = sqlite3_mprintf("%z)", q);
            int rc = sqlite3_prepare_v2(db, q, -1, &ins, NULL);
            sqlite3_free(q);
            if (rc != SQLITE_OK) {
                printf("Error: %s\n", sqlite3_errmsg(db));
                break;
            }
            if (!exists) continue;                      /* that line was the header */
        }
        for (int i = 0; i < ncol; i++) {
            if (i < n) sqlite3_bind_text(ins, i + 1, fields[i], -1, SQLITE_TRANSIENT);
            else sqlite3_bind_null(ins, i + 1);
        }
        if (sqlite3_step(ins) != SQLITE_DONE) printf("%s:%d: %s\n", file, lineno, sqlite3_errmsg(db));
        else rows++;
        sqlite3_reset(ins);
    }
    sqlite3_finalize(ins);
    sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);
    fclose(f);
    printf("Imported %d row(s) into %s\n", rows, table);
}

static void run_file(const char *name);

static void help(void) {
    printf(".tables [PATTERN]     list the tables\n"
           ".schema [TABLE]       show CREATE statements\n"
           ".mode MODE            list csv line column table box json insert\n"
           ".headers on|off       column names above the rows\n"
           ".separator TEXT       between columns in list mode\n"
           ".nullvalue TEXT       how NULL is shown\n"
           ".open FILE            open another database\n"
           ".read FILE            run the SQL in a file\n"
           ".import FILE TABLE    load CSV into a table\n"
           ".dump [TABLE]         the database as SQL\n"
           ".output [FILE]        send results to a file (no FILE: the screen)\n"
           ".once FILE            the next result only to a file\n"
           ".indexes [TABLE]      list the indexes\n"
           ".databases            the open database\n"
           ".timer on|off         how long each command takes\n"
           ".changes on|off       how many rows changed\n"
           ".print TEXT           print it\n"
           ".show                 current settings\n"
           ".quit / .exit         leave\n"
           "SQL ends with ;  e.g.  CREATE TABLE t(a, b);  SELECT * FROM t;\n");
}

/* 1 = leave */
static int dot_command(char *line) {
    char *argv[4] = { 0 };
    int argc = 0;
    char *p = line + 1;
    while (*p && argc < 4) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        char quote = (*p == '"' || *p == '\'') ? *p++ : 0;
        argv[argc++] = p;
        while (*p && (quote ? *p != quote : (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r'))) p++;
        if (*p) *p++ = 0;
    }
    if (!argc) return 0;
    const char *c = argv[0], *a1 = argv[1];
    int on = a1 && (!strcmp(a1, "on") || !strcmp(a1, "1") || !strcmp(a1, "yes"));
    if (!strcmp(c, "quit") || !strcmp(c, "exit") || !strcmp(c, "q")) return 1;
    else if (!strcmp(c, "help")) help();
    else if (!strcmp(c, "tables") || !strcmp(c, "table"))
        query_print("SELECT name FROM sqlite_schema WHERE type IN ('table','view') AND name NOT LIKE 'sqlite_%%'"
                    " AND name LIKE coalesce(%Q, '%%') ORDER BY 1", a1);
    else if (!strcmp(c, "schema"))
        query_print("SELECT sql || ';' FROM sqlite_schema WHERE sql NOT NULL AND name NOT LIKE 'sqlite_%%'"
                    " AND (tbl_name LIKE coalesce(%Q, '%%')) ORDER BY rowid", a1);
    else if (!strcmp(c, "indexes") || !strcmp(c, "indices"))
        query_print("SELECT name FROM sqlite_schema WHERE type='index' AND tbl_name LIKE coalesce(%Q, '%%') ORDER BY 1", a1);
    else if (!strcmp(c, "headers") || !strcmp(c, "header")) headers = on;
    else if (!strcmp(c, "timer")) timer = on;
    else if (!strcmp(c, "changes")) changes_shown = on;
    else if (!strcmp(c, "mode")) {
        int found = -1;
        for (int i = 0; a1 && i < (int)(sizeof mode_names / sizeof mode_names[0]); i++)
            if (!strcmp(a1, mode_names[i])) found = i;
        if (a1 && !strcmp(a1, "tabs")) { found = M_LIST; strcpy(separator, "\t"); }
        if (found < 0) printf("current mode: %s\nmodes: list csv line column table box json insert\n", mode_names[mode]);
        else {
            mode = found;
            if (mode == M_COLUMN || mode == M_TABLE || mode == M_BOX) headers = 1;
        }
    } else if (!strcmp(c, "separator") && a1) snprintf(separator, sizeof separator, "%s", a1);
    else if (!strcmp(c, "nullvalue") && a1) snprintf(null_text, sizeof null_text, "%s", a1);
    else if (!strcmp(c, "open")) open_db(a1);
    else if (!strcmp(c, "read") && a1) run_file(a1);
    else if (!strcmp(c, "import") && a1 && argv[2]) import_csv(a1, argv[2]);
    else if (!strcmp(c, "dump")) dump(a1);
    else if (!strcmp(c, "output")) set_output(a1);
    else if (!strcmp(c, "once") && a1) { set_output(a1); once = 1; return 0; }
    else if (!strcmp(c, "databases")) printf("main: %s r/w\n", db_name);
    else if (!strcmp(c, "print")) {
        for (int i = 1; i < argc; i++) fprintf(out, "%s%s", i > 1 ? " " : "", argv[i]);
        fputc('\n', out);
    } else if (!strcmp(c, "show")) {
        printf("  database: %s\n   headers: %s\n      mode: %s\n nullvalue: \"%s\"\n    output: %s\n"
               " separator: \"%s\"\n     timer: %s\n",
               db_name, headers ? "on" : "off", mode_names[mode], null_text, out_name[0] ? out_name : "stdout",
               separator, timer ? "on" : "off");
    } else {
        printf("Error: unknown command or invalid arguments: \"%s\". Enter \".help\" for help\n", c);
        nerrors++;
    }
    if (once) {
        once = 0;
        set_output(NULL);
    }
    return 0;
}

/* feed lines of input; returns 1 when the user asked to leave */
static char *pending;
static size_t pending_len, pending_cap;

static int feed_line(const char *line) {
    if (pending_len == 0) {
        const char *t = line;
        while (*t == ' ' || *t == '\t') t++;
        if (*t == '.') {
            char buf[512];
            snprintf(buf, sizeof buf, "%s", t);
            return dot_command(buf);
        }
        if (!*t || *t == '\n') return 0;
    }
    size_t n = strlen(line);
    if (pending_len + n + 2 > pending_cap) {
        size_t cap = pending_cap ? pending_cap * 2 : 1024;
        while (cap < pending_len + n + 2) cap *= 2;
        char *p = realloc(pending, cap);
        if (!p) return 0;
        pending = p;
        pending_cap = cap;
    }
    memcpy(pending + pending_len, line, n);
    pending_len += n;
    if (!n || line[n - 1] != '\n') pending[pending_len++] = '\n';
    pending[pending_len] = 0;
    if (sqlite3_complete(pending)) {
        run_sql(pending, NULL);
        pending_len = 0;
        if (once) {
            once = 0;
            set_output(NULL);
        }
    }
    return 0;
}

static void run_file(const char *name) {
    FILE *f = fopen(fs_name(name), "r");
    if (!f) {
        printf("Error: cannot open \"%s\"\n", name);
        return;
    }
    static char line[1024];
    while (fgets(line, sizeof line, f))
        if (feed_line(line)) break;
    fclose(f);
}

int main(int argc, char **argv) {
    out = stdout;
    const char *file = NULL;
    int first_sql = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] == '-' && a[1]) {
            if (a[1] == '-') a++;
            if (!strcmp(a, "-header") || !strcmp(a, "-headers")) headers = 1;
            else if (!strcmp(a, "-noheader")) headers = 0;
            else if (!strcmp(a, "-version")) { printf("%s\n", sqlite3_libversion()); return 0; }
            else if (!strcmp(a, "-help")) {
                printf("usage: sqlite3 [-box|-table|-column|-csv|-json|-line|-list] [-header] [FILE] [SQL]\n");
                return 0;
            } else {
                int found = 0;
                for (int m = 0; m < (int)(sizeof mode_names / sizeof mode_names[0]); m++)
                    if (!strcmp(a + 1, mode_names[m])) { mode = m; found = 1; }
                if (!found) { printf("sqlite3: unknown option: %s\n", argv[i]); return 1; }
                if (mode == M_COLUMN || mode == M_TABLE || mode == M_BOX) headers = 1;
            }
        } else if (!file) {
            file = a;
        } else {
            first_sql = i;
            break;
        }
    }
    sqlite3_initialize();
    if (!open_db(file) && file) return 1;
    if (first_sql) {                                    /* sqlite3 shop.db "SELECT ..." */
        for (int i = first_sql; i < argc; i++) {
            if (argv[i][0] == '.') dot_command(argv[i]);
            else run_sql(argv[i], NULL);
        }
        set_output(NULL);
        sqlite3_close(db);
        return nerrors ? 1 : 0;
    }
    printf("SQLite version %s on MyOS\nEnter \".help\" for usage hints.\n", sqlite3_libversion());
    if (!file) printf("Connected to a transient in-memory database.\nUse \".open FILENAME\" to reopen on a persistent database.\n");
    static char line[300];
    for (;;) {
        fputs(pending_len ? "   ...> " : "sqlite> ", stdout);
        fflush(stdout);
        if (!fgets(line, sizeof line, stdin)) break;
        if (feed_line(line)) break;
    }
    set_output(NULL);
    sqlite3_close(db);
    return 0;
}
