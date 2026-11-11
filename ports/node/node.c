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
 * friends, require('fs' / 'path' / 'os' / 'util' / './file.js'), and the
 * same modules through import. The JavaScript half is in prelude.js.
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

static JSRuntime *rt;
static JSContext *ctx;
static int repl_mode;                   /* errors don't end the program */

/* --------------------------------------------------------------- helpers */
static void out(const char *s, size_t n) { fwrite(s, 1, n, stdout); }

/* MyFS is flat: "./a.js" and "/a.js" are both "a.js" */
static const char *fs_name(const char *name) {
    while (name[0] == '.' && name[1] == '/') name += 2;
    while (name[0] == '/') name++;
    return name;
}

/* whole file in memory, 0-terminated; NULL if it isn't there */
static char *load_file(const char *name, size_t *len) {
    name = fs_name(name);
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

/* write a string or bytes (TypedArray / ArrayBuffer); append if argv[2] */
static JSValue n_write_file(JSContext *c, JSValueConst this, int argc, JSValueConst *argv) {
    const char *name = JS_ToCString(c, argv[0]);
    if (!name) return JS_EXCEPTION;
    const char *str = NULL;
    const uint8_t *data;
    size_t len, off, elem;
    JSValue abuf = JS_UNDEFINED;
    if (JS_IsString(argv[1])) {
        str = JS_ToCStringLen(c, &len, argv[1]);
        data = (const uint8_t *)str;
    } else if (!JS_IsException(abuf = JS_GetTypedArrayBuffer(c, argv[1], &off, &len, &elem))) {
        size_t total;
        data = JS_GetArrayBuffer(c, &total, abuf);
        if (data) data += off;
    } else {
        JS_FreeValue(c, JS_GetException(c));
        abuf = JS_UNDEFINED;
        data = JS_GetArrayBuffer(c, &len, argv[1]);
        if (!data) {
            JS_FreeValue(c, JS_GetException(c));
            str = JS_ToCStringLen(c, &len, argv[1]);
            data = (const uint8_t *)str;
        }
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
    JS_FreeValue(c, abuf);
    JS_FreeCString(c, name);
    return ret;
}

static JSValue n_file_size(JSContext *c, JSValueConst this, int argc, JSValueConst *argv) {
    const char *name = JS_ToCString(c, argv[0]);
    if (!name) return JS_EXCEPTION;
    long n = *fs_name(name) ? sys_file_size(fs_name(name)) : -1;
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

static JSValue n_list(JSContext *c, JSValueConst this, int argc, JSValueConst *argv) {
    JSValue arr = JS_NewArray(c);
    char name[48];
    uint32_t n = 0;
    for (unsigned long i = 0; sys_list_files(i, name) >= 0; i++) {
        name[47] = 0;
        if (name[0]) JS_SetPropertyUint32(c, arr, n++, JS_NewString(c, name));
    }
    return arr;
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
    JS_CFUNC_DEF("list", 0, n_list),
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
static JSValue call_prelude(const char *fn, JSValueConst arg) {
    JSValue g = JS_GetGlobalObject(ctx);
    JSValue hidden = JS_GetPropertyStr(ctx, g, "__myos_hooks");
    JSValue f = JS_GetPropertyStr(ctx, hidden, fn);
    JSValue r = JS_Call(ctx, f, JS_UNDEFINED, 1, &arg);
    JS_FreeValue(ctx, f);
    JS_FreeValue(ctx, hidden);
    JS_FreeValue(ctx, g);
    return r;
}

static void set_import_meta(JSValueConst func, const char *name, int is_main) {
    JSModuleDef *m = JS_VALUE_GET_PTR(func);
    JSValue meta = JS_GetImportMeta(ctx, m);
    if (JS_IsException(meta)) return;
    char url[64];
    snprintf(url, sizeof url, "file:///%s", fs_name(name));
    JS_DefinePropertyValueStr(ctx, meta, "url", JS_NewString(ctx, url), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, meta, "main", JS_NewBool(ctx, is_main), JS_PROP_C_W_E);
    JS_FreeValue(ctx, meta);
}

static JSModuleDef *module_loader(JSContext *c, const char *name, void *opaque, JSValueConst attrs) {
    (void)opaque;
    (void)attrs;
    size_t len;
    char *src;
    JSValue s = call_prelude("builtinModuleSource", JS_NewString(c, name)); /* fs, path, os ... */
    if (JS_IsString(s)) {
        const char *t = JS_ToCStringLen(c, &len, s);
        src = t ? strdup(t) : NULL;
        if (t) JS_FreeCString(c, t);
    } else {
        src = load_file(name, &len);
        if (!src) {
            char alt[64];
            snprintf(alt, sizeof alt, "%s.js", fs_name(name));
            src = load_file(alt, &len);
        }
    }
    JS_FreeValue(c, s);
    if (!src) {
        JS_ThrowReferenceError(c, "cannot find module '%s'", name);
        return NULL;
    }
    JSValue func;
    size_t nl = strlen(name);
    if (nl > 5 && strcmp(name + nl - 5, ".json") == 0) {  /* JSON: export default the value */
        JSValue v = JS_ParseJSON(c, src, len, name);
        free(src);
        if (JS_IsException(v)) return NULL;
        JSValue m = call_prelude("jsonModuleSource", v);
        JS_FreeValue(c, v);
        const char *t = JS_ToCStringLen(c, &len, m);
        if (!t) { JS_FreeValue(c, m); return NULL; }
        func = JS_Eval(c, t, len, name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
        JS_FreeCString(c, t);
        JS_FreeValue(c, m);
    } else {
        func = JS_Eval(c, src, len, name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
        free(src);
    }
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
    char name[64];
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
    JS_SetModuleLoaderFunc2(rt, NULL, module_loader, NULL, NULL);
    ctx = JS_NewContext(rt);
    if (!ctx) {
        printf("node: out of memory\n");
        return 1;
    }

    /* the script name and its arguments: everything after the options */
    const char *code = NULL;
    int print = 0, first = 1;
    if (argc > 1 && argv[1][0] == '-') {
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
    for (int i = code ? first : 1, n = 1; i < argc; i++, n++)
        JS_SetPropertyUint32(ctx, args, (uint32_t)n, JS_NewString(ctx, argv[i]));
    JS_SetPropertyStr(ctx, natives_obj, "argv", args);
    JS_SetPropertyStr(ctx, natives_obj, "version", JS_NewString(ctx, JS_VERSION));

    JSValue init = JS_Eval(ctx, prelude_js, sizeof prelude_js - 1, "prelude.js", JS_EVAL_TYPE_GLOBAL);
    if (!JS_IsException(init)) {
        JSValue r = JS_Call(ctx, init, JS_UNDEFINED, 1, (JSValueConst *)&natives_obj);
        JS_FreeValue(ctx, init);
        init = r;
    }
    JS_FreeValue(ctx, natives_obj);
    if (JS_IsException(init)) {
        dump_error();
        return 1;
    }
    JS_FreeValue(ctx, init);

    int r = 0;
    if (code) r = run_code(code, print);
    else if (argc > 1) r = run_file(argv[1]);
    else {
        repl();
        quit(0);
    }
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
