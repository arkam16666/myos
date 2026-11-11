/* node.c - JavaScript for MyOS: QuickJS (third_party/quickjs, MIT licence)
 * with a small Node.js-like front end instead of QuickJS's own qjs.c, whose
 * std/os modules need a Unix system (file descriptors, select, termios ...).
 *
 *   node                   interactive prompt (REPL)
 *   node file.js [args]    run a script (CommonJS like Node; ES module if it
 *                          uses import/export or ends in .mjs)
 *   node -e "code"         run code      node -p "expr"   print its value
 *
 * What scripts get: console.*, print, prompt, process, setTimeout and
 * friends, require('fs' / 'path' / 'os' / 'util' / './file.js' / packages
 * in node_modules, found the Node way), and the same modules through import.
 * The JavaScript half is in prelude.js.
 *
 * Built a second time with -DNODE_NPM as 'npm' (ports/npm/npm.js runs instead
 * of a script); npmlib.c has its native helpers (gunzip, SHA-512/SHA-1, JSON scan).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "quickjs.h"
#include "myos.h"

#define JS_VERSION CONFIG_VERSION       /* from config.h */
#define TICK_MS 10                      /* sys_ticks() counts 1/100 s */
#define STACK_KB 3072                   /* C stack from the heap: ~3300 JavaScript calls deep */

static const char prelude_js[] = {
#embed "prelude.js"
    , 0
};
#ifdef NODE_NPM
static const char npm_js[] = {
#embed "../npm/npm.js"
    , 0
};
#endif

#include "npmlib.c"

static JSRuntime *rt;
static JSContext *ctx;
static int repl_mode;                   /* errors don't end the program */

/* --------------------------------------------------------------- helpers */
static void out(const char *s, size_t n) { fwrite(s, 1, n, stdout); }

/* paths go to the kernel as they are: from the current folder, or from / */
static const char *fs_name(const char *name) {
    while (name[0] == '.' && name[1] == '/') name += 2;
    return name;
}

/* 1 file, 2 folder, <0 not there (MYOS_E*) */
static int path_type(const char *name, uint32_t *size) {
    struct myos_stat st;
    long r = *name ? sys_stat(name, &st) : MYOS_ENOENT;
    if (r < 0) return (int)r;
    if (size) *size = st.size;
    return (int)st.type;
}

/* whole file in memory, 0-terminated; NULL if it isn't there */
static char *load_file(const char *name, size_t *len) {
    name = fs_name(name);
    if (path_type(name, NULL) == 2) return NULL;
    long size = sys_file_size(name);
    if (size < 0) return NULL;
    char *buf = malloc((size_t)size + 1);
    if (!buf) return NULL;
    if (size && sys_read_file(name, buf, (unsigned long)size) != size) {
        free(buf);
        return NULL;
    }
    buf[size] = 0;
    if (len) *len = (size_t)size;
    return buf;
}

typedef struct { char *buf; size_t len, cap; } strbuf;

static void sb_write(void *opaque, const char *s, size_t n) {
    strbuf *b = opaque;
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 256;
        while (cap < b->len + n + 1) cap *= 2;
        char *nb = realloc(b->buf, cap);
        if (!nb) return;
        b->buf = nb;
        b->cap = cap;
    }
    memcpy(b->buf + b->len, s, n);
    b->len += n;
    b->buf[b->len] = 0;
}

/* a value as text, like Node's util.inspect (depth = how many levels of
 * nested objects to show, Node's default 2) */
static void inspect_to(JSContext *c, strbuf *b, JSValueConst v, int depth) {
    JSPrintValueOptions opt;
    JS_PrintValueSetDefaultOptions(&opt);
    opt.max_depth = depth < 0 ? 0 : (uint32_t)depth + 1;   /* QuickJS counts the top level too */
    JS_PrintValue(c, sb_write, b, v, &opt);
    if (!b->buf) return;
    /* QuickJS shows empty objects as "{  }": make them "{}" like Node (not inside strings) */
    size_t w = 0;
    int in_str = 0;
    for (size_t r = 0; r < b->len; r++) {
        char ch = b->buf[r];
        if (in_str) {
            if (ch == '\\' && r + 1 < b->len) b->buf[w++] = b->buf[r++];
            else if (ch == '"') in_str = 0;
        } else if (ch == '"') {
            in_str = 1;
        } else if ((ch == '{' || ch == '[') && r + 3 < b->len && b->buf[r + 1] == ' ' && b->buf[r + 2] == ' ' &&
                   b->buf[r + 3] == (ch == '{' ? '}' : ']')) {
            b->buf[w++] = ch;
            b->buf[w++] = b->buf[r + 3];
            r += 3;
            continue;
        }
        b->buf[w++] = b->buf[r];
    }
    b->len = w;
    b->buf[w] = 0;
}

static void print_value(JSValueConst v) {
    strbuf b = { 0 };
    inspect_to(ctx, &b, v, 2);
    if (b.buf) out(b.buf, b.len);
    free(b.buf);
}

static void dump_error_value(JSValueConst e) {
    fflush(stdout);
    out("Uncaught ", 9);
    print_value(e);
    out("\n", 1);
    fflush(stdout);
}

static void dump_error(void) {
    JSValue e = JS_GetException(ctx);
    dump_error_value(e);
    JS_FreeValue(ctx, e);
}

static void quit(int code) {
    fflush(stdout);
    exit(code);
}

/* --------------------------------------------------------------- unhandled promise rejections */
typedef struct rejection { struct rejection *next; JSValue promise, reason; } rejection;
static rejection *rejections;

static void rejection_tracker(JSContext *c, JSValueConst promise, JSValueConst reason, JS_BOOL handled, void *op) {
    (void)op;
    if (!handled) {
        rejection *r = malloc(sizeof *r);
        if (!r) return;
        r->promise = JS_DupValue(c, promise);
        r->reason = JS_DupValue(c, reason);
        r->next = rejections;
        rejections = r;
        return;
    }
    for (rejection **p = &rejections; *p; p = &(*p)->next) {
        if (JS_VALUE_GET_PTR((*p)->promise) == JS_VALUE_GET_PTR(promise)) {
            rejection *r = *p;
            *p = r->next;
            JS_FreeValue(c, r->promise);
            JS_FreeValue(c, r->reason);
            free(r);
            return;
        }
    }
}

/* forget the rejection of a promise we report ourselves (REPL line, module) */
static void forget_rejection(JSValueConst promise) {
    rejection_tracker(ctx, promise, JS_UNDEFINED, 1, NULL);
}

static void check_rejections(void) {
    while (rejections) {
        rejection *r = rejections;
        rejections = r->next;
        fflush(stdout);
        out("Uncaught (in promise) ", 22);
        print_value(r->reason);
        out("\n", 1);
        JS_FreeValue(ctx, r->promise);
        JS_FreeValue(ctx, r->reason);
        free(r);
        if (!repl_mode) quit(1);
    }
}

/* --------------------------------------------------------------- timers and the event loop */
typedef struct timer {
    struct timer *next;
    int id;
    unsigned long due;                  /* in ticks */
    unsigned long interval;             /* 0 = setTimeout */
    JSValue func;
} timer;
static timer *timers;
static int next_timer_id = 1;

static unsigned long ms_to_ticks(double ms) {
    if (!(ms > 0)) return 0;
    if (ms > 1e9) ms = 1e9;
    return (unsigned long)((ms + TICK_MS - 1) / TICK_MS);
}

static void free_timer(timer *t) {
    JS_FreeValue(ctx, t->func);
    free(t);
}

static void run_jobs(void) {
    JSContext *c;
    for (;;) {
        int r = JS_ExecutePendingJob(rt, &c);
        if (r == 0) break;
        if (r < 0) {
            dump_error();
            if (!repl_mode) quit(1);
        }
    }
    check_rejections();
}

/* run the earliest timer; wait for it only if 'wait'. 0 = nothing (ready) to run */
static int run_timer(int wait) {
    timer **pe = NULL;
    for (timer **p = &timers; *p; p = &(*p)->next)
        if (!pe || (*p)->due < (*pe)->due) pe = p;
    if (!pe) return 0;
    timer *t = *pe;
    unsigned long now = sys_ticks();
    if (t->due > now) {
        if (!wait) return 0;
        fflush(stdout);
        sys_sleep(t->due - now);
        return 1;                       /* look again: the list may have changed */
    }
    JSValue func = JS_DupValue(ctx, t->func);
    if (t->interval) {
        t->due = now + t->interval;
    } else {
        *pe = t->next;
        free_timer(t);
    }
    JSValue r = JS_Call(ctx, func, JS_UNDEFINED, 0, NULL);
    JS_FreeValue(ctx, func);
    if (JS_IsException(r)) {
        dump_error();
        if (!repl_mode) quit(1);
    }
    JS_FreeValue(ctx, r);
    return 1;
}

/* keep going until no promise job or timer is left */
static void event_loop(void) {
    do run_jobs();
    while (run_timer(1));
}

/* wait for a promise, running jobs and timers meanwhile; returns its result
 * or JS_EXCEPTION (with the reason thrown) */
static JSValue await_value(JSValue p) {
    for (;;) {
        int state = JS_PromiseState(ctx, p);
        if (state == JS_PROMISE_FULFILLED) {
            JSValue r = JS_PromiseResult(ctx, p);
            JS_FreeValue(ctx, p);
            return r;
        }
        if (state == JS_PROMISE_REJECTED) {
            JSValue r = JS_PromiseResult(ctx, p);
            forget_rejection(p);
            JS_FreeValue(ctx, p);
            return JS_Throw(ctx, r);
        }
        if (state != JS_PROMISE_PENDING) return p;      /* not a promise */
        JSContext *c;
        int r = JS_ExecutePendingJob(rt, &c);
        if (r < 0) {
            dump_error();
            if (!repl_mode) quit(1);
        }
        if (r == 0) {
            forget_rejection(p);
            check_rejections();
            if (!run_timer(1)) {        /* nothing left that could settle it */
                JS_FreeValue(ctx, p);
                return JS_UNDEFINED;
            }
        }
    }
}

/* --------------------------------------------------------------- native functions for prelude.js */
static JSValue n_write(JSContext *c, JSValueConst this, int argc, JSValueConst *argv) {
    size_t len;
    const char *s = JS_ToCStringLen(c, &len, argv[1]);
    if (!s) return JS_EXCEPTION;
    out(s, len);
    JS_FreeCString(c, s);
    return JS_UNDEFINED;
}

static JSValue n_flush(JSContext *c, JSValueConst this, int argc, JSValueConst *argv) {
    fflush(stdout);
    return JS_UNDEFINED;
}

static JSValue n_inspect(JSContext *c, JSValueConst this, int argc, JSValueConst *argv) {
    strbuf b = { 0 };
    int depth = 2;
    if (argc > 1 && JS_IsNumber(argv[1])) {
        double d;
        JS_ToFloat64(c, &d, argv[1]);
        depth = d > 50 ? -1 : (int)d;               /* Infinity: everything */
    }
    inspect_to(c, &b, argv[0], depth);
    JSValue r = JS_NewStringLen(c, b.buf ? b.buf : "", b.len);
    free(b.buf);
    return r;
}

static JSValue throw_fs(JSContext *c, const char *what, const char *name) {
    return JS_ThrowReferenceError(c, "%s: no such file '%s'", what, name);
}

static JSValue n_read_file(JSContext *c, JSValueConst this, int argc, JSValueConst *argv) {
    const char *name = JS_ToCString(c, argv[0]);
    if (!name) return JS_EXCEPTION;
    size_t len;
    char *buf = load_file(name, &len);
    if (!buf) {
        JSValue e = throw_fs(c, "ENOENT", name);
        JS_FreeCString(c, name);
        return e;
    }
    JS_FreeCString(c, name);
    JSValue r = JS_ToBool(c, argv[1]) ? JS_NewUint8ArrayCopy(c, (uint8_t *)buf, len)
                                      : JS_NewStringLen(c, buf, len);
    free(buf);
    return r;
}

/* the bytes of a TypedArray or ArrayBuffer: 1, or 0 if it's neither (no exception left) */
static int get_bytes(JSContext *c, JSValueConst v, const uint8_t **data, size_t *len) {
    size_t off, elem, total;
    *data = NULL;
    *len = 0;
    if (!JS_IsObject(v)) return 0;
    JSValue ab = JS_GetTypedArrayBuffer(c, v, &off, len, &elem);
    if (!JS_IsException(ab)) {
        const uint8_t *p = JS_GetArrayBuffer(c, &total, ab);
        JS_FreeValue(c, ab);                    /* (the typed array keeps it alive) */
        *data = p ? p + off : NULL;
        return 1;
    }
    JS_FreeValue(c, JS_GetException(c));
    *data = JS_GetArrayBuffer(c, len, v);
    if (*data) return 1;
    JS_FreeValue(c, JS_GetException(c));
    *len = 0;
    return 0;
}

/* write a string or bytes (TypedArray / ArrayBuffer); append if argv[2] */
static JSValue n_write_file(JSContext *c, JSValueConst this, int argc, JSValueConst *argv) {
    const char *name = JS_ToCString(c, argv[0]);
    if (!name) return JS_EXCEPTION;
    const char *str = NULL;
    const uint8_t *data;
    size_t len;
    if (JS_IsString(argv[1]) || !get_bytes(c, argv[1], &data, &len)) {
        str = JS_ToCStringLen(c, &len, argv[1]);
        data = (const uint8_t *)str;
    }
    JSValue ret = JS_UNDEFINED;
    if (!data && len) ret = JS_EXCEPTION;
    else {
        const char *fname = fs_name(name);
        uint8_t *all = NULL;
        size_t old = 0;
        if (JS_ToBool(c, argv[2])) {                    /* append: old content first */
            char *prev = load_file(fname, &old);
            if (prev) {
                all = malloc(old + len + 1);
                if (all) {
                    memcpy(all, prev, old);
                    memcpy(all + old, data, len);
                }
                free(prev);
                if (!all) ret = JS_ThrowOutOfMemory(c);
            }
        }
        if (!JS_IsException(ret) &&
            sys_write_file(fname, all ? all : data, all ? old + len : len) < 0)
            ret = JS_ThrowInternalError(c, "cannot write '%s' (disk full or bad name?)", fname);
        free(all);
    }
    if (str) JS_FreeCString(c, str);
    JS_FreeCString(c, name);
    return ret;
}

/* size of a file, -1 if there is no such file (folders: -1 too) */
static JSValue n_file_size(JSContext *c, JSValueConst this, int argc, JSValueConst *argv) {
    const char *name = JS_ToCString(c, argv[0]);
    if (!name) return JS_EXCEPTION;
    uint32_t size = 0;
    long n = path_type(fs_name(name), &size) == 1 ? (long)size : -1;
    JS_FreeCString(c, name);
    return JS_NewInt64(c, n);
}

static JSValue n_unlink(JSContext *c, JSValueConst this, int argc, JSValueConst *argv) {
    const char *name = JS_ToCString(c, argv[0]);
    if (!name) return JS_EXCEPTION;
    JSValue r = sys_delete_file(fs_name(name)) < 0 ? throw_fs(c, "ENOENT", name) : JS_UNDEFINED;
    JS_FreeCString(c, name);
    return r;
}

/* folders: stat(path) -> {type: 1 file / 2 folder, size} or null */
static JSValue n_stat(JSContext *c, JSValueConst this, int argc, JSValueConst *argv) {
    const char *name = JS_ToCString(c, argv[0]);
    if (!name) return JS_EXCEPTION;
    uint32_t size = 0;
    int t = path_type(fs_name(name), &size);
    JS_FreeCString(c, name);
    if (t < 0) return JS_NULL;
    JSValue o = JS_NewObject(c);
    JS_SetPropertyStr(c, o, "type", JS_NewInt32(c, t));
    JS_SetPropertyStr(c, o, "size", JS_NewInt64(c, size));
    return o;
}

/* readdir(path) -> [[name, type, size], ...], or the MYOS_E* error number */
static JSValue n_readdir(JSContext *c, JSValueConst this, int argc, JSValueConst *argv) {
    const char *name = JS_ToCString(c, argv[0]);
    if (!name) return JS_EXCEPTION;
    const char *dir = *fs_name(name) ? fs_name(name) : ".";
    int t = path_type(dir, NULL);
    if (t != 2) {
        JS_FreeCString(c, name);
        return JS_NewInt32(c, t < 0 ? t : MYOS_ENOTDIR);
    }
    JSValue arr = JS_NewArray(c);
    struct myos_dirent d;
    uint32_t n = 0;
    for (unsigned long i = 0; sys_readdir(dir, i, &d) == 1; i++) {
        d.name[63] = 0;
        JSValue e = JS_NewArray(c);
        JS_SetPropertyUint32(c, e, 0, JS_NewString(c, d.name));
        JS_SetPropertyUint32(c, e, 1, JS_NewInt32(c, (int32_t)d.type));
        JS_SetPropertyUint32(c, e, 2, JS_NewInt64(c, d.size));
        JS_SetPropertyUint32(c, arr, n++, e);
    }
    JS_FreeCString(c, name);
    return arr;
}

/* mkdir / rmdir / chdir (path) and rename (from, to) -> 0 or a MYOS_E* error number */
static JSValue n_dirop(JSContext *c, JSValueConst this, int argc, JSValueConst *argv, int magic) {
    const char *a = JS_ToCString(c, argv[0]);
    if (!a) return JS_EXCEPTION;
    const char *b = magic == 3 ? JS_ToCString(c, argv[1]) : NULL;
    long r = MYOS_ENAME;
    if (magic == 0) r = sys_mkdir(fs_name(a));
    else if (magic == 1) r = sys_rmdir(fs_name(a));
    else if (magic == 2) r = sys_chdir(fs_name(a));
    else if (b) r = sys_rename(fs_name(a), fs_name(b));
    JS_FreeCString(c, a);
    if (b) JS_FreeCString(c, b);
    return JS_NewInt32(c, (int32_t)r);
}

static JSValue n_cwd(JSContext *c, JSValueConst this, int argc, JSValueConst *argv) {
    char buf[260];
    long n = sys_getcwd(buf, sizeof buf);
    return JS_NewString(c, n > 0 && n < (long)sizeof buf ? buf : "/");
}

/* ------------------------------------------------------- npm helpers (npmlib.c) */
static void free_buf(JSRuntime *r, void *opaque, void *ptr) { free(ptr); }

/* fetch(url, max) -> {status, error, body: ArrayBuffer}; error is a MYOS_FETCH_E* number */
static JSValue n_fetch(JSContext *c, JSValueConst this, int argc, JSValueConst *argv) {
    const char *url = JS_ToCString(c, argv[0]);
    if (!url) return JS_EXCEPTION;
    double want = 0;
    JS_ToFloat64(c, &want, argv[1]);
    size_t max = want >= 4096 && want <= 64e6 ? (size_t)want : 1 << 20;
    uint8_t *buf = malloc(max);
    while (!buf && max > (2 << 20)) buf = malloc(max /= 2);     /* less memory than asked for */
    if (!buf) { JS_FreeCString(c, url); return JS_ThrowOutOfMemory(c); }
    struct myos_fetch f;
    memset(&f, 0, sizeof f);
    f.url = (uint64_t)(uintptr_t)url;
    f.buf = (uint64_t)(uintptr_t)buf;
    f.max = (uint32_t)max;
    long r = sys_fetch(&f);
    JS_FreeCString(c, url);
    JSValue o = JS_NewObject(c);
    JS_SetPropertyStr(c, o, "status", JS_NewInt32(c, f.status));
    JS_SetPropertyStr(c, o, "error", JS_NewInt32(c, f.error ? f.error : r < 0 ? (int32_t)r : 0));
    uint8_t *body = NULL;
    if (!f.error && r >= 0 && f.len <= max && (body = malloc(f.len ? f.len : 1)))
        memcpy(body, buf, f.len);               /* (realloc can't shrink: copy what came) */
    free(buf);
    if (body) JS_SetPropertyStr(c, o, "body", JS_NewArrayBuffer(c, body, f.len, free_buf, NULL, 0));
    return o;
}

/* gunzip(bytes) -> ArrayBuffer; throws if it's broken */
static JSValue n_gunzip(JSContext *c, JSValueConst this, int argc, JSValueConst *argv) {
    const uint8_t *in;
    size_t n;
    if (!get_bytes(c, argv[0], &in, &n)) return JS_ThrowTypeError(c, "gunzip: not bytes");
    long size = npm_gzip_size(in, n);
    if (size < 0) return JS_ThrowTypeError(c, "not a gzip file (.tgz)");
    if (size > 48L << 20) return JS_ThrowRangeError(c, "unpacks to %ld MB: too big for MyOS", size >> 20);
    uint8_t *out = malloc(size ? (size_t)size : 1);
    if (!out) return JS_ThrowOutOfMemory(c);
    long len = npm_gunzip(in, n, out, (size_t)size);
    if (len < 0) {
        free(out);
        return JS_ThrowTypeError(c, len == -3 ? "gzip: CRC mismatch (damaged download)" : "gzip: damaged data");
    }
    return JS_NewArrayBuffer(c, out, (size_t)len, free_buf, NULL, 0);
}

/* hash("sha512" | "sha1", bytes) -> hex */
static JSValue n_hash(JSContext *c, JSValueConst this, int argc, JSValueConst *argv) {
    const char *alg = JS_ToCString(c, argv[0]);
    if (!alg) return JS_EXCEPTION;
    int sha1 = strcmp(alg, "sha1") == 0;
    JS_FreeCString(c, alg);
    const uint8_t *in;
    size_t n;
    if (!get_bytes(c, argv[1], &in, &n)) return JS_ThrowTypeError(c, "hash: not bytes");
    uint8_t d[64];
    char hex[129];
    size_t dl = sha1 ? 20 : 64;
    if (sha1) npm_sha1(in, n, d);
    else npm_sha512(in, n, d);
    for (size_t i = 0; i < dl; i++) {
        hex[2 * i] = "0123456789abcdef"[d[i] >> 4];
        hex[2 * i + 1] = "0123456789abcdef"[d[i] & 15];
    }
    return JS_NewStringLen(c, hex, dl * 2);
}

/* bytes(string) -> Uint8Array (UTF-8) */
static JSValue n_bytes(JSContext *c, JSValueConst this, int argc, JSValueConst *argv) {
    size_t len;
    const char *s = JS_ToCStringLen(c, &len, argv[0]);
    if (!s) return JS_EXCEPTION;
    JSValue r = JS_NewUint8ArrayCopy(c, (const uint8_t *)s, len);
    JS_FreeCString(c, s);
    return r;
}

/* text(bytes [, start, end]) -> string (UTF-8) */
static JSValue n_text(JSContext *c, JSValueConst this, int argc, JSValueConst *argv) {
    const uint8_t *in;
    size_t n;
    if (!get_bytes(c, argv[0], &in, &n)) return JS_ThrowTypeError(c, "text: not bytes");
    int64_t a = 0, b = (int64_t)n;
    if (argc > 1 && !JS_IsUndefined(argv[1])) JS_ToInt64(c, &a, argv[1]);
    if (argc > 2 && !JS_IsUndefined(argv[2])) JS_ToInt64(c, &b, argv[2]);
    if (b > (int64_t)n) b = (int64_t)n;
    if (a < 0) a = 0;
    if (b < a) b = a;
    return JS_NewStringLen(c, (const char *)in + a, (size_t)(b - a));
}

typedef struct { JSContext *c; JSValue arr; uint32_t n; } key_list;

static int collect_key(const char *key, size_t len, size_t value, void *arg) {
    key_list *k = arg;
    JS_SetPropertyUint32(k->c, k->arr, k->n++, JS_NewStringLen(k->c, key, len));
    return 0;
}

/* jsonPick(bytes, [key, key, ...], keys) -> the JSON text of that value, or
 * with keys = true the names of its members; undefined if it isn't there */
static JSValue n_json_pick(JSContext *c, JSValueConst this, int argc, JSValueConst *argv) {
    const uint8_t *in;
    size_t n;
    if (!get_bytes(c, argv[0], &in, &n)) return JS_ThrowTypeError(c, "jsonPick: not bytes");
    const char *s = (const char *)in;
    size_t at = 0;
    uint32_t depth = 0;
    JSValue l = JS_GetPropertyStr(c, argv[1], "length");
    JS_ToUint32(c, &depth, l);
    JS_FreeValue(c, l);
    for (uint32_t i = 0; i < depth && at != JNONE; i++) {
        JSValue k = JS_GetPropertyUint32(c, argv[1], i);
        const char *key = JS_ToCString(c, k);
        JS_FreeValue(c, k);
        if (!key) return JS_EXCEPTION;
        at = npm_json_get(s, n, at, key);
        JS_FreeCString(c, key);
    }
    if (at == JNONE) return JS_UNDEFINED;
    if (JS_ToBool(c, argv[2])) {
        key_list k = { c, JS_NewArray(c), 0 };
        npm_json_members(s, n, at, collect_key, &k);
        return k.arr;
    }
    size_t end = npm_json_skip(s, n, at);
    if (end == JNONE) return JS_UNDEFINED;
    return JS_NewStringLen(c, s + at, end - at);
}

static JSValue n_eval(JSContext *c, JSValueConst this, int argc, JSValueConst *argv) {
    size_t len;
    const char *src = JS_ToCStringLen(c, &len, argv[0]);
    if (!src) return JS_EXCEPTION;
    const char *file = JS_ToCString(c, argv[1]);
    JSValue r = file ? JS_Eval(c, src, len, file, JS_EVAL_TYPE_GLOBAL) : JS_EXCEPTION;
    JS_FreeCString(c, src);
    if (file) JS_FreeCString(c, file);
    return r;
}

/* evalModule(src, name): run ES module code to the end now (require() of an ES module) */
static JSValue n_eval_module(JSContext *c, JSValueConst this, int argc, JSValueConst *argv) {
    size_t len;
    const char *src = JS_ToCStringLen(c, &len, argv[0]);
    if (!src) return JS_EXCEPTION;
    const char *file = JS_ToCString(c, argv[1]);
    JSValue f = file ? JS_Eval(c, src, len, file, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY) : JS_EXCEPTION;
    JS_FreeCString(c, src);
    if (file) JS_FreeCString(c, file);
    if (JS_IsException(f)) return f;
    return await_value(JS_EvalFunction(c, f));
}

/* detectModule(src): does it use import/export (an ES module)? */
static JSValue n_detect_module(JSContext *c, JSValueConst this, int argc, JSValueConst *argv) {
    size_t len;
    const char *src = JS_ToCStringLen(c, &len, argv[0]);
    if (!src) return JS_EXCEPTION;
    int r = JS_DetectModule(src, len);
    JS_FreeCString(c, src);
    return JS_NewBool(c, r);
}

static JSValue n_read_line(JSContext *c, JSValueConst this, int argc, JSValueConst *argv) {
    char line[300];
    fflush(stdout);
    if (!fgets(line, sizeof line, stdin)) return JS_NULL;
    size_t n = strlen(line);
    if (n && line[n - 1] == '\n') line[--n] = 0;
    return JS_NewStringLen(c, line, n);
}

static JSValue n_set_timer(JSContext *c, JSValueConst this, int argc, JSValueConst *argv) {
    double ms = 0;
    if (!JS_IsFunction(c, argv[0])) return JS_ThrowTypeError(c, "the callback must be a function");
    JS_ToFloat64(c, &ms, argv[1]);
    timer *t = malloc(sizeof *t);
    if (!t) return JS_ThrowOutOfMemory(c);
    t->id = next_timer_id++;
    t->due = sys_ticks() + ms_to_ticks(ms);
    t->interval = 0;
    if (JS_ToBool(c, argv[2])) {
        t->interval = ms_to_ticks(ms);
        if (!t->interval) t->interval = 1;
    }
    t->func = JS_DupValue(c, argv[0]);
    t->next = timers;
    timers = t;
    return JS_NewInt32(c, t->id);
}

static JSValue n_clear_timer(JSContext *c, JSValueConst this, int argc, JSValueConst *argv) {
    int id;
    if (JS_ToInt32(c, &id, argv[0])) return JS_EXCEPTION;
    for (timer **p = &timers; *p; p = &(*p)->next) {
        if ((*p)->id == id) {
            timer *t = *p;
            *p = t->next;
            free_timer(t);
            break;
        }
    }
    return JS_UNDEFINED;
}

static JSValue n_exit(JSContext *c, JSValueConst this, int argc, JSValueConst *argv) {
    int code = 0;
    JS_ToInt32(c, &code, argv[0]);
    quit(code);
    return JS_UNDEFINED;
}

static JSValue n_ticks(JSContext *c, JSValueConst this, int argc, JSValueConst *argv) {
    return JS_NewInt64(c, (int64_t)sys_ticks());
}

static JSValue n_sysinfo(JSContext *c, JSValueConst this, int argc, JSValueConst *argv) {
    struct myos_sysinfo si;
    memset(&si, 0, sizeof si);
    sys_sysinfo(&si);
    JSValue o = JS_NewObject(c);
    JS_SetPropertyStr(c, o, "ticks", JS_NewInt64(c, (int64_t)si.ticks));
    JS_SetPropertyStr(c, o, "memTotalKB", JS_NewInt64(c, si.mem_total_kb));
    JS_SetPropertyStr(c, o, "memFreeKB", JS_NewInt64(c, si.mem_free_kb));
    JS_SetPropertyStr(c, o, "armHz", JS_NewInt64(c, si.arm_hz));
    JS_SetPropertyStr(c, o, "tempMilliC", JS_NewInt32(c, si.temp_millic));
    JS_SetPropertyStr(c, o, "files", JS_NewInt64(c, si.files));
    JS_SetPropertyStr(c, o, "diskFreeKB", JS_NewInt64(c, si.disk_free_kb));
    return o;
}

static JSValue n_heap(JSContext *c, JSValueConst this, int argc, JSValueConst *argv) {
    JSMemoryUsage mu;
    JS_ComputeMemoryUsage(rt, &mu);
    JSValue o = JS_NewObject(c);
    JS_SetPropertyStr(c, o, "heapUsed", JS_NewInt64(c, mu.malloc_size));
    JS_SetPropertyStr(c, o, "objects", JS_NewInt64(c, mu.obj_count));
    return o;
}

static JSValue n_gc(JSContext *c, JSValueConst this, int argc, JSValueConst *argv) {
    JS_RunGC(rt);
    return JS_UNDEFINED;
}

static const JSCFunctionListEntry natives[] = {
    JS_CFUNC_DEF("write", 2, n_write),
    JS_CFUNC_DEF("flush", 0, n_flush),
    JS_CFUNC_DEF("inspect", 2, n_inspect),
    JS_CFUNC_DEF("readFile", 2, n_read_file),
    JS_CFUNC_DEF("writeFile", 3, n_write_file),
    JS_CFUNC_DEF("fileSize", 1, n_file_size),
    JS_CFUNC_DEF("unlink", 1, n_unlink),
    JS_CFUNC_DEF("stat", 1, n_stat),
    JS_CFUNC_DEF("readdir", 1, n_readdir),
    JS_CFUNC_MAGIC_DEF("mkdir", 1, n_dirop, 0),
    JS_CFUNC_MAGIC_DEF("rmdir", 1, n_dirop, 1),
    JS_CFUNC_MAGIC_DEF("chdir", 1, n_dirop, 2),
    JS_CFUNC_MAGIC_DEF("rename", 2, n_dirop, 3),
    JS_CFUNC_DEF("cwd", 0, n_cwd),
    JS_CFUNC_DEF("fetch", 2, n_fetch),
    JS_CFUNC_DEF("gunzip", 1, n_gunzip),
    JS_CFUNC_DEF("hash", 2, n_hash),
    JS_CFUNC_DEF("text", 3, n_text),
    JS_CFUNC_DEF("bytes", 1, n_bytes),
    JS_CFUNC_DEF("jsonPick", 3, n_json_pick),
    JS_CFUNC_DEF("evalModule", 2, n_eval_module),
    JS_CFUNC_DEF("detectModule", 1, n_detect_module),
    JS_CFUNC_DEF("evalScript", 2, n_eval),
    JS_CFUNC_DEF("readLine", 0, n_read_line),
    JS_CFUNC_DEF("setTimer", 3, n_set_timer),
    JS_CFUNC_DEF("clearTimer", 1, n_clear_timer),
    JS_CFUNC_DEF("exit", 1, n_exit),
    JS_CFUNC_DEF("ticks", 0, n_ticks),
    JS_CFUNC_DEF("sysinfo", 0, n_sysinfo),
    JS_CFUNC_DEF("heap", 0, n_heap),
    JS_CFUNC_DEF("gc", 0, n_gc),
};

/* --------------------------------------------------------------- ES modules from MyFS */
static JSValue call_prelude2(const char *fn, int argc, JSValueConst *argv) {
    JSValue g = JS_GetGlobalObject(ctx);
    JSValue hidden = JS_GetPropertyStr(ctx, g, "__myos_hooks");
    JSValue f = JS_GetPropertyStr(ctx, hidden, fn);
    JSValue r = JS_Call(ctx, f, JS_UNDEFINED, argc, argv);
    JS_FreeValue(ctx, f);
    JS_FreeValue(ctx, hidden);
    JS_FreeValue(ctx, g);
    return r;
}

/* (takes over arg) */
static JSValue call_prelude(const char *fn, JSValue arg) {
    JSValue r = call_prelude2(fn, 1, (JSValueConst *)&arg);
    JS_FreeValue(ctx, arg);
    return r;
}

static void set_import_meta(JSValueConst func, const char *name, int is_main) {
    JSModuleDef *m = JS_VALUE_GET_PTR(func);
    JSValue meta = JS_GetImportMeta(ctx, m);
    if (JS_IsException(meta)) return;
    char url[300];
    snprintf(url, sizeof url, "file://%s%s", name[0] == '/' ? "" : "/", fs_name(name));
    JS_DefinePropertyValueStr(ctx, meta, "url", JS_NewString(ctx, url), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, meta, "main", JS_NewBool(ctx, is_main), JS_PROP_C_W_E);
    JS_FreeValue(ctx, meta);
}

/* import 'x' from module 'base': prelude.js finds the file (./, node_modules ...) */
static char *module_normalize(JSContext *c, const char *base, const char *name, void *opaque) {
    (void)opaque;
    JSValue a[2] = { JS_NewString(c, name), JS_NewString(c, base) };
    JSValue r = call_prelude2("resolveImport", 2, a);
    JS_FreeValue(c, a[0]);
    JS_FreeValue(c, a[1]);
    if (JS_IsException(r)) return NULL;
    const char *s = JS_ToCString(c, r);
    JS_FreeValue(c, r);
    if (!s) return NULL;
    char *out = js_strdup(c, s);
    JS_FreeCString(c, s);
    return out;
}

/* its source as an ES module: prelude.js wraps builtins, CommonJS files and JSON */
static JSModuleDef *module_loader(JSContext *c, const char *name, void *opaque, JSValueConst attrs) {
    (void)opaque;
    (void)attrs;
    size_t len;
    JSValue s = call_prelude("moduleSource", JS_NewString(c, name));
    if (JS_IsException(s)) return NULL;
    const char *src = JS_ToCStringLen(c, &len, s);
    JS_FreeValue(c, s);
    if (!src) return NULL;
    JSValue func = JS_Eval(c, src, len, name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    JS_FreeCString(c, src);
    if (JS_IsException(func)) return NULL;
    set_import_meta(func, name, 0);
    JSModuleDef *m = JS_VALUE_GET_PTR(func);
    JS_FreeValue(c, func);
    return m;
}

/* --------------------------------------------------------------- running code */
/* 0 = ok, -1 = it threw (already printed) */
static int run_module(const char *src, size_t len, const char *name) {
    JSValue func = JS_Eval(ctx, src, len, name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    if (!JS_IsException(func)) {
        set_import_meta(func, name, 1);
        func = await_value(JS_EvalFunction(ctx, func));
    }
    if (JS_IsException(func)) {
        dump_error();
        return -1;
    }
    JS_FreeValue(ctx, func);
    return 0;
}

/* -e / -p code: a plain script (or a module if it imports) */
static int run_code(const char *src, int print) {
    size_t len = strlen(src);
    if (!print && JS_DetectModule(src, len)) return run_module(src, len, "[eval]");
    JSValue v = JS_Eval(ctx, src, len, "[eval]", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(v)) {
        dump_error();
        return -1;
    }
    if (print) {
        if (JS_IsString(v)) {
            const char *s = JS_ToCStringLen(ctx, &len, v);
            if (s) { out(s, len); JS_FreeCString(ctx, s); }
        } else {
            print_value(v);
        }
        out("\n", 1);
    }
    JS_FreeValue(ctx, v);
    return 0;
}

static int run_file(const char *arg) {
    char name[260];
    size_t len;
    snprintf(name, sizeof name, "%s", fs_name(arg));
    char *src = load_file(name, &len);
    if (!src && strlen(name) + 3 < sizeof name) {       /* "node hello" runs hello.js */
        strcat(name, ".js");
        src = load_file(name, &len);
    }
    if (!src) {
        printf("node: cannot find '%s'\n", arg);
        return -1;
    }
    int r;
    size_t nl = strlen(name);
    if ((nl > 4 && strcmp(name + nl - 4, ".mjs") == 0) || JS_DetectModule(src, len)) {
        r = run_module(src, len, name);
    } else {                                            /* CommonJS, like Node */
        JSValue v = call_prelude("runMain", JS_NewString(ctx, name));
        r = JS_IsException(v) ? (dump_error(), -1) : 0;
        JS_FreeValue(ctx, v);
    }
    free(src);
    return r;
}

/* --------------------------------------------------------------- the interactive prompt */
/* is the input unfinished (open brackets, string or comment)? */
static int incomplete(const char *s) {
    char stack[128];
    int sp = 0;
    for (; *s; s++) {
        char top = sp ? stack[sp - 1] : 0;
        if (top == '`') {
            if (*s == '\\' && s[1]) s++;
            else if (*s == '`') sp--;
            else if (*s == '$' && s[1] == '{' && sp < 128) { stack[sp++] = '{'; s++; }
        } else if (top == '\'' || top == '"') {
            if (*s == '\\' && s[1]) s++;
            else if (*s == top || *s == '\n') sp--;
        } else if (top == '*') {
            if (*s == '*' && s[1] == '/') { sp--; s++; }
        } else if (*s == '/' && s[1] == '/') {
            while (s[1] && s[1] != '\n') s++;
        } else if (*s == '/' && s[1] == '*') {
            if (sp < 128) stack[sp++] = '*';
            s++;
        } else if (*s == '\'' || *s == '"' || *s == '`' || *s == '(' || *s == '[' || *s == '{') {
            if (sp < 128) stack[sp++] = *s;
        } else if (*s == ')' || *s == ']' || *s == '}') {
            char want = *s == ')' ? '(' : *s == ']' ? '[' : '{';
            if (top != want) return 0;                  /* a mistake: let the parser say so */
            sp--;
        }
    }
    return sp > 0;
}

static void repl(void) {
    static char code[8192];
    char line[300];
    size_t used = 0;
    repl_mode = 1;
    printf("Welcome to JavaScript on MyOS (QuickJS %s).\n"
           "Type JavaScript and press Enter. .help for help, .exit to leave.\n", JS_VERSION);
    for (;;) {
        run_jobs();
        while (run_timer(0)) run_jobs();                /* timers that are already due */
        out(used ? "... " : "> ", used ? 4 : 2);
        fflush(stdout);
        if (!fgets(line, sizeof line, stdin)) break;
        size_t n = strlen(line);
        if (!used) {
            const char *t = line;
            while (*t == ' ') t++;
            if (strncmp(t, ".exit", 5) == 0) break;
            if (strncmp(t, ".help", 5) == 0) {
                printf(".exit     leave (or process.exit(), Ctrl+C)\n"
                       ".clear    forget an unfinished multi-line entry\n"
                       "_         the last result\n"
                       "Try: [1,2,3].map(x => x * 2)   require('fs').readdirSync()\n");
                continue;
            }
        } else if (strncmp(line, ".clear", 6) == 0) {
            used = 0;
            continue;
        }
        if (used + n + 1 >= sizeof code) {
            printf("Too long - start again.\n");
            used = 0;
            continue;
        }
        memcpy(code + used, line, n + 1);
        used += n;
        if (incomplete(code)) continue;
        used = 0;
        JSValue v = JS_Eval(ctx, code, strlen(code), "<repl>", JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_ASYNC);
        if (!JS_IsException(v)) v = await_value(v);
        if (JS_IsException(v)) {
            dump_error();
            continue;
        }
        JSValue r = JS_GetPropertyStr(ctx, v, "value");
        JS_FreeValue(ctx, v);
        if (!JS_IsUndefined(r)) {
            print_value(r);
            out("\n", 1);
        }
        JSValue g = JS_GetGlobalObject(ctx);
        JS_SetPropertyStr(ctx, g, "_", r);
        JS_FreeValue(ctx, g);
    }
}

/* --------------------------------------------------------------- start */
static void usage(void) {
    printf("usage: node [file.js [args...]]   run a script, or the prompt without one\n"
           "       node -e \"code\"             run code\n"
           "       node -p \"expression\"       print the value\n"
           "       node -v                    version\n"
           "JavaScript by QuickJS %s (Fabrice Bellard, MIT licence).\n", JS_VERSION);
}

/* The normal stack (256 KB under 0x600000) holds only ~200 JavaScript calls
 * deep, so node runs on a bigger one taken from the heap. */
#define BIG_STACK (STACK_KB * 1024UL)
static size_t stack_size = 200 * 1024;
static int main_argc;
static char **main_argv;

static int node_main(int argc, char **argv);

static void __attribute__((noreturn, used)) on_big_stack(void) {
    quit(node_main(main_argc, main_argv));
}

int main(int argc, char **argv) {
    char *stack = malloc(BIG_STACK);
    if (!stack) return node_main(argc, argv);
    main_argc = argc;
    main_argv = argv;
    stack_size = BIG_STACK - 32 * 1024;         /* room for C code that doesn't check */
    uintptr_t top = ((uintptr_t)stack + BIG_STACK) & ~(uintptr_t)15;
    __asm__ volatile("mov sp, %0\n\tbr %1" : : "r"(top), "r"(on_big_stack) : "memory");
    __builtin_unreachable();
}

static int node_main(int argc, char **argv) {
    rt = JS_NewRuntime();
    if (!rt) {
        printf("node: out of memory\n");
        return 1;
    }
    JS_SetMaxStackSize(rt, stack_size);
    JS_SetHostPromiseRejectionTracker(rt, rejection_tracker, NULL);
    JS_SetModuleLoaderFunc2(rt, module_normalize, module_loader, NULL, NULL);
    ctx = JS_NewContext(rt);
    if (!ctx) {
        printf("node: out of memory\n");
        return 1;
    }

    /* the script name and its arguments: everything after the options */
    const char *code = NULL;
    int print = 0, first = 1;
#ifdef NODE_NPM
    if (0) {                                    /* npm: every argument is npm's */
#else
    if (argc > 1 && argv[1][0] == '-') {
#endif
        const char *o = argv[1];
        if (!strcmp(o, "-v") || !strcmp(o, "--version")) {
            printf("v%s (QuickJS)\n", JS_VERSION);
            return 0;
        }
        if ((!strcmp(o, "-e") || !strcmp(o, "--eval") || !strcmp(o, "-p") || !strcmp(o, "--print")) && argc > 2) {
            code = argv[2];
            print = o[1] == 'p' || o[2] == 'p';
            first = 3;
        } else {
            usage();
            return strcmp(o, "-h") && strcmp(o, "--help") ? 1 : 0;
        }
    }

    JSValue natives_obj = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, natives_obj, natives, sizeof natives / sizeof natives[0]);
    JSValue args = JS_NewArray(ctx);
    JS_SetPropertyUint32(ctx, args, 0, JS_NewString(ctx, "node"));
#ifdef NODE_NPM
    JS_SetPropertyUint32(ctx, args, 1, JS_NewString(ctx, "npm"));       /* argv: node npm install x */
    for (int i = 1, n = 2; i < argc; i++, n++)
#else
    for (int i = code ? first : 1, n = 1; i < argc; i++, n++)
#endif
        JS_SetPropertyUint32(ctx, args, (uint32_t)n, JS_NewString(ctx, argv[i]));
    JS_SetPropertyStr(ctx, natives_obj, "argv", args);
    JS_SetPropertyStr(ctx, natives_obj, "version", JS_NewString(ctx, JS_VERSION));

    JSValue init = JS_Eval(ctx, prelude_js, sizeof prelude_js - 1, "prelude.js", JS_EVAL_TYPE_GLOBAL);
    if (!JS_IsException(init)) {
        JSValue r = JS_Call(ctx, init, JS_UNDEFINED, 1, (JSValueConst *)&natives_obj);
        JS_FreeValue(ctx, init);
        init = r;
    }
    if (JS_IsException(init)) {
        dump_error();
        return 1;
    }
    JS_FreeValue(ctx, init);

    int r = 0;
#ifdef NODE_NPM
    /* npm.js is (function (N) { ... return main(); }): run it and wait for it */
    JSValue npm = JS_Eval(ctx, npm_js, sizeof npm_js - 1, "npm.js", JS_EVAL_TYPE_GLOBAL);
    if (!JS_IsException(npm)) {
        JSValue v = JS_Call(ctx, npm, JS_UNDEFINED, 1, (JSValueConst *)&natives_obj);
        JS_FreeValue(ctx, npm);
        npm = JS_IsException(v) ? v : await_value(v);
    }
    JS_FreeValue(ctx, natives_obj);
    if (JS_IsException(npm)) {
        dump_error();
        r = -1;
    }
    JS_FreeValue(ctx, npm);
#else
    JS_FreeValue(ctx, natives_obj);
    if (code) r = run_code(code, print);
    else if (argc > 1) r = run_file(argv[1]);
    else {
        repl();
        quit(0);
    }
#endif
    if (r == 0) event_loop();
    JSValue g = JS_GetGlobalObject(ctx);
    JSValue p = JS_GetPropertyStr(ctx, g, "process");
    JSValue ec = JS_GetPropertyStr(ctx, p, "exitCode");
    int code_out = r ? 1 : 0;
    if (JS_IsNumber(ec)) JS_ToInt32(ctx, &code_out, ec);
    JS_FreeValue(ctx, ec);
    JS_FreeValue(ctx, p);
    JS_FreeValue(ctx, g);
    quit(code_out);
    return 0;
}
