/* stdio.c - MyOS C library: FILE on top of MyFS and the console.
 *
 * MyFS reads and writes whole files, so an open file is kept in memory:
 * fopen() loads it, fwrite()/fseek() work on the copy, and fclose() or
 * fflush() writes it back. stdin reads a line at a time (the kernel
 * shows what you type), stdout/stderr go to the screen.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../myos.h"

enum { F_FREE, F_FILE, F_STDIN, F_STDOUT };

struct myos_file {
    int kind;
    char name[48];              /* "" for tmpfile() */
    unsigned char *buf;
    size_t len, cap, pos;
    int can_read, can_write, append, dirty, eof, err;
    int unget;                  /* pushed-back character or EOF */
    char line[260];             /* stdin: the current line; stdout: waiting text */
    size_t line_len, line_pos;
};

static struct myos_file files[3 + FOPEN_MAX];
FILE *stdin = &files[0], *stdout = &files[1], *stderr = &files[2];

static void init_std(void) {
    static int done;
    if (done) return;
    done = 1;
    files[0].kind = F_STDIN;
    files[1].kind = F_STDOUT;
    files[2].kind = F_STDOUT;
    for (int i = 0; i < 3; i++) files[i].unget = EOF;
}

/* ---------------------------------------------------------------- screen */
static void out_flush(FILE *f) {
    if (f->line_len == 0) return;
    size_t start = 0;
    for (size_t i = 0; i <= f->line_len; i++) {        /* sys_print stops at a 0 byte */
        if (i == f->line_len || f->line[i] == 0) {
            char save = f->line[i];
            f->line[i] = 0;
            if (i > start) sys_print(&f->line[start]);
            if (i < f->line_len) sys_putc(0);
            f->line[i] = save;
            start = i + 1;
        }
    }
    f->line_len = 0;
}

static void out_put(FILE *f, char c) {
    f->line[f->line_len++] = c;
    if (c == '\n' || f->line_len >= sizeof f->line - 1 || f == stderr) out_flush(f);
}

/* ---------------------------------------------------------------- keyboard */
static int in_get(FILE *f) {
    if (f->line_pos >= f->line_len) {
        out_flush(stdout);                      /* show the prompt first */
        long n = sys_readline(f->line, sizeof f->line - 2);
        if (n < 0) n = 0;
        f->line[n] = '\n';
        f->line_len = (size_t)n + 1;
        f->line_pos = 0;
    }
    return (unsigned char)f->line[f->line_pos++];
}

/* ---------------------------------------------------------------- files */
static int grow(FILE *f, size_t need) {
    if (need <= f->cap) return 1;
    size_t cap = f->cap ? f->cap : 256;
    while (cap < need) cap *= 2;
    unsigned char *b = realloc(f->buf, cap);
    if (!b) {
        errno = ENOMEM;
        return 0;
    }
    f->buf = b;
    f->cap = cap;
    return 1;
}

static FILE *slot(void) {
    init_std();
    for (int i = 3; i < 3 + FOPEN_MAX; i++) {
        if (files[i].kind == F_FREE) {
            memset(&files[i], 0, sizeof files[i]);
            files[i].unget = EOF;
            return &files[i];
        }
    }
    errno = EMFILE;
    return NULL;
}

static int load(FILE *f, const char *name) {
    long size = sys_file_size(name);
    if (size < 0) {
        errno = ENOENT;
        return 0;
    }
    if (!grow(f, (size_t)size + 1)) return 0;
    if (size && sys_read_file(name, f->buf, (unsigned long)size) != size) {
        errno = EIO;
        return 0;
    }
    f->len = (size_t)size;
    return 1;
}

static FILE *open_into(FILE *f, const char *name, const char *mode) {
    if (strlen(name) >= sizeof f->name || !*name) {
        errno = EINVAL;
        return NULL;
    }
    strcpy(f->name, name);
    int plus = strchr(mode, '+') != NULL;
    f->kind = F_FILE;
    if (mode[0] == 'r') {
        f->can_read = 1;
        f->can_write = plus;
        if (!load(f, name)) goto fail;
    } else if (mode[0] == 'w') {
        f->can_write = 1;
        f->can_read = plus;
        f->dirty = 1;                           /* creates/empties the file */
    } else if (mode[0] == 'a') {
        f->can_write = 1;
        f->can_read = plus;
        f->append = 1;
        if (sys_file_size(name) >= 0 && !load(f, name)) goto fail;
        f->pos = f->len;
        f->dirty = 1;
    } else {
        errno = EINVAL;
        goto fail;
    }
    return f;
fail:
    free(f->buf);
    f->kind = F_FREE;
    return NULL;
}

FILE *fopen(const char *name, const char *mode) {
    FILE *f = slot();
    return f ? open_into(f, name, mode) : NULL;
}

static int save(FILE *f) {
    if (f->kind != F_FILE || !f->dirty || !f->name[0]) return 0;
    if (sys_write_file(f->name, f->buf ? (const void *)f->buf : "", f->len) < 0) {
        errno = ENOSPC;
        f->err = 1;
        return EOF;
    }
    f->dirty = 0;
    return 0;
}

int fflush(FILE *f) {
    init_std();
    if (!f) {
        out_flush(stdout);
        out_flush(stderr);
        int r = 0;
        for (int i = 3; i < 3 + FOPEN_MAX; i++)
            if (files[i].kind == F_FILE && save(&files[i])) r = EOF;
        return r;
    }
    if (f->kind == F_STDOUT) {
        out_flush(f);
        return 0;
    }
    return save(f);
}

void myos_stdio_flush_all(void) {
    fflush(NULL);
}

int fclose(FILE *f) {
    if (!f) return EOF;
    if (f->kind == F_STDOUT) {
        out_flush(f);
        return 0;
    }
    if (f->kind != F_FILE) return 0;
    int r = save(f);
    free(f->buf);
    f->buf = NULL;
    f->kind = F_FREE;
    return r;
}

FILE *freopen(const char *name, const char *mode, FILE *f) {
    if (!f) return NULL;
    if (!name) {                                /* only change the mode: keep the file */
        if (f->kind == F_FILE) f->pos = 0;
        return f;
    }
    if (f->kind == F_FILE) fclose(f);
    memset(f, 0, sizeof *f);
    f->unget = EOF;
    return open_into(f, name, mode);
}

FILE *tmpfile(void) {
    FILE *f = slot();
    if (!f) return NULL;
    f->kind = F_FILE;
    f->can_read = f->can_write = 1;             /* no name: never saved */
    return f;
}

char *tmpnam(char *s) {
    static char buf[L_tmpnam];
    static int n;
    char *out = s ? s : buf;
    do {
        snprintf(out, L_tmpnam, "tmp%d.tmp", n++);
    } while (sys_file_size(out) >= 0);
    return out;
}

int fgetc(FILE *f) {
    init_std();
    if (f->unget != EOF) {
        int c = f->unget;
        f->unget = EOF;
        return c;
    }
    if (f->kind == F_STDIN) return in_get(f);
    if (f->kind != F_FILE || !f->can_read) {
        f->err = 1;
        return EOF;
    }
    if (f->pos >= f->len) {
        f->eof = 1;
        return EOF;
    }
    return f->buf[f->pos++];
}

int getc(FILE *f) { return fgetc(f); }
int getchar(void) { return fgetc(stdin); }

int ungetc(int c, FILE *f) {
    if (c == EOF) return EOF;
    f->unget = (unsigned char)c;
    f->eof = 0;
    return c;
}

char *fgets(char *s, int n, FILE *f) {
    int i = 0;
    while (i < n - 1) {
        int c = fgetc(f);
        if (c == EOF) break;
        s[i++] = (char)c;
        if (c == '\n') break;
    }
    if (i == 0) return NULL;
    s[i] = 0;
    return s;
}

size_t fread(void *p, size_t size, size_t n, FILE *f) {
    unsigned char *d = p;
    size_t total = size * n, got = 0;
    if (!total) return 0;
    if (f->kind == F_FILE && f->unget == EOF && f->can_read) {     /* fast path */
        size_t avail = f->pos < f->len ? f->len - f->pos : 0;
        got = total < avail ? total : avail;
        memcpy(d, f->buf + f->pos, got);
        f->pos += got;
        if (got < total) f->eof = 1;
        return got / size;
    }
    while (got < total) {
        int c = fgetc(f);
        if (c == EOF) break;
        d[got++] = (unsigned char)c;
    }
    return got / size;
}

int fputc(int c, FILE *f) {
    init_std();
    if (f->kind == F_STDOUT) {
        out_put(f, (char)c);
        return (unsigned char)c;
    }
    if (f->kind != F_FILE || !f->can_write) {
        f->err = 1;
        return EOF;
    }
    if (f->append) f->pos = f->len;
    if (!grow(f, f->pos + 1)) {
        f->err = 1;
        return EOF;
    }
    if (f->pos > f->len) memset(f->buf + f->len, 0, f->pos - f->len);
    f->buf[f->pos++] = (unsigned char)c;
    if (f->pos > f->len) f->len = f->pos;
    f->dirty = 1;
    return (unsigned char)c;
}

int putc(int c, FILE *f) { return fputc(c, f); }
int putchar(int c) { return fputc(c, stdout); }

size_t fwrite(const void *p, size_t size, size_t n, FILE *f) {
    const unsigned char *s = p;
    size_t total = size * n;
    init_std();
    if (f->kind == F_FILE && f->can_write && total) {               /* fast path */
        if (f->append) f->pos = f->len;
        if (!grow(f, f->pos + total)) {
            f->err = 1;
            return 0;
        }
        if (f->pos > f->len) memset(f->buf + f->len, 0, f->pos - f->len);
        memcpy(f->buf + f->pos, s, total);
        f->pos += total;
        if (f->pos > f->len) f->len = f->pos;
        f->dirty = 1;
        return n;
    }
    for (size_t i = 0; i < total; i++)
        if (fputc(s[i], f) == EOF) return i / (size ? size : 1);
    return n;
}

int fputs(const char *s, FILE *f) {
    size_t n = strlen(s);
    return fwrite(s, 1, n, f) == n ? 0 : EOF;
}

int puts(const char *s) {
    if (fputs(s, stdout) == EOF) return EOF;
    return fputc('\n', stdout) == EOF ? EOF : 0;
}

int fseek(FILE *f, long off, int whence) {
    if (f->kind != F_FILE) {
        errno = ESPIPE;
        return -1;
    }
    long base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (long)f->pos : (long)f->len;
    if (base + off < 0) {
        errno = EINVAL;
        return -1;
    }
    f->pos = (size_t)(base + off);
    f->eof = 0;
    f->unget = EOF;
    return 0;
}

long ftell(FILE *f) {
    if (f->kind != F_FILE) {
        errno = ESPIPE;
        return -1;
    }
    return (long)f->pos - (f->unget != EOF ? 1 : 0);
}

void rewind(FILE *f) {
    fseek(f, 0, SEEK_SET);
    f->err = 0;
}

int feof(FILE *f) { return f->eof; }
int ferror(FILE *f) { return f->err; }

void clearerr(FILE *f) {
    f->eof = 0;
    f->err = 0;
}

int setvbuf(FILE *f, char *buf, int mode, size_t size) {
    (void)f; (void)buf; (void)mode; (void)size;
    return 0;
}

int remove(const char *name) {
    if (sys_delete_file(name) < 0) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

int rename(const char *from, const char *to) {          /* MyFS has no rename: copy + delete */
    long size = sys_file_size(from);
    if (size < 0) {
        errno = ENOENT;
        return -1;
    }
    void *buf = malloc((size_t)size + 1);
    if (!buf) {
        errno = ENOMEM;
        return -1;
    }
    int ok = sys_read_file(from, buf, (unsigned long)size) == size &&
             sys_write_file(to, buf, (unsigned long)size) >= 0;
    free(buf);
    if (!ok) {
        errno = EIO;
        return -1;
    }
    return remove(from);
}

void perror(const char *s) {
    if (s && *s) fprintf(stderr, "%s: ", s);
    fprintf(stderr, "%s\n", strerror(errno));
}
