/* native.c - strings, exceptions, stack traces and the native methods of
 * the runtime library (ports/java/lib) */
#include <math.h>
#include <stdarg.h>
#include "jvm.h"
#ifdef MYOS_LIBC
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#else
#include <sys/stat.h>
#endif

Class *cls_object, *cls_string, *cls_class, *cls_throwable, *cls_chararray;
int slot_str_value, slot_str_hash, slot_thr_message, slot_thr_backtrace, slot_thr_cause;
int slot_class_vm;
static int slot_ste_class, slot_ste_method, slot_ste_file, slot_ste_line;

/* ------------------------------------------------------------ strings */
int jstrlen(Object *s) {
    Object *v = FIELDS(s)[slot_str_value].l;
    return v ? v->len : 0;
}

jchar *jstrchars(Object *s) {
    Object *v = FIELDS(s)[slot_str_value].l;
    return v ? ADATA(v, jchar) : NULL;
}

Object *new_string_chars(const jchar *c, jint n) {
    Object *arr = alloc_array(cls_chararray, n);
    if (!arr) return NULL;
    if (n) memcpy(ADATA(arr, jchar), c, (size_t)n * 2);
    Object *s = alloc_object(cls_string);
    if (!s) return NULL;
    FIELDS(s)[slot_str_value].l = arr;
    return s;
}

/* UTF-8 (also Java's modified UTF-8) -> UTF-16 chars; returns the count */
static jint utf8_decode(const char *src, size_t n, jchar *out) {
    const uint8_t *s = (const uint8_t *)src, *e = s + n;
    jint k = 0;
    while (s < e) {
        uint32_t c = *s++;
        if (c >= 0x80) {
            int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
            if (!more) c = 0xFFFD;
            else {
                c &= 0x3F >> more;
                for (int i = 0; i < more; i++) {
                    if (s >= e || (*s & 0xC0) != 0x80) { c = 0xFFFD; break; }
                    c = c << 6 | (*s++ & 0x3F);
                }
            }
        }
        if (c >= 0x10000) {
            c -= 0x10000;
            if (out) { out[k] = (jchar)(0xD800 | (c >> 10)); out[k + 1] = (jchar)(0xDC00 | (c & 0x3FF)); }
            k += 2;
        } else {
            if (out) out[k] = (jchar)c;
            k++;
        }
    }
    return k;
}

Object *new_string_utf8(const char *s) {
    size_t n = strlen(s);
    jint len = utf8_decode(s, n, NULL);
    Object *arr = alloc_array(cls_chararray, len);
    if (!arr) return NULL;
    utf8_decode(s, n, ADATA(arr, jchar));
    Object *str = alloc_object(cls_string);
    if (!str) return NULL;
    FIELDS(str)[slot_str_value].l = arr;
    return str;
}

/* UTF-16 -> UTF-8 into out (cap bytes incl. the 0); returns the length */
static size_t utf8_encode(const jchar *c, jint n, char *out, size_t cap) {
    size_t k = 0;
    for (jint i = 0; i < n; i++) {
        uint32_t ch = c[i];
        if (ch >= 0xD800 && ch < 0xDC00 && i + 1 < n && c[i + 1] >= 0xDC00 && c[i + 1] < 0xE000) {
            ch = 0x10000 + ((ch - 0xD800) << 10) + (c[i + 1] - 0xDC00);
            i++;
        }
        char b[4];
        int m;
        if (ch < 0x80) { b[0] = (char)ch; m = 1; }
        else if (ch < 0x800) { b[0] = (char)(0xC0 | ch >> 6); b[1] = (char)(0x80 | (ch & 63)); m = 2; }
        else if (ch < 0x10000) {
            b[0] = (char)(0xE0 | ch >> 12); b[1] = (char)(0x80 | ((ch >> 6) & 63)); b[2] = (char)(0x80 | (ch & 63)); m = 3;
        } else {
            b[0] = (char)(0xF0 | ch >> 18); b[1] = (char)(0x80 | ((ch >> 12) & 63));
            b[2] = (char)(0x80 | ((ch >> 6) & 63)); b[3] = (char)(0x80 | (ch & 63)); m = 4;
        }
        if (out) {
            if (k + m + 1 > cap) break;
            memcpy(out + k, b, m);
        }
        k += m;
    }
    if (out && cap) out[k < cap ? k : cap - 1] = 0;
    return k;
}

char *string_utf8(Object *s) {
    if (!s) {
        char *r = xmalloc(5);
        strcpy(r, "null");
        return r;
    }
    jint n = jstrlen(s);
    size_t len = utf8_encode(jstrchars(s), n, NULL, 0);
    char *out = xmalloc(len + 1);
    utf8_encode(jstrchars(s), n, out, len + 1);
    return out;
}

static uint32_t jhash(Object *s) {
    uint32_t h = 0;
    jchar *c = jstrchars(s);
    for (jint i = 0, n = jstrlen(s); i < n; i++) h = 31 * h + c[i];
    return h;
}

static Object **itab;
static size_t icap, icount;

Object *intern_string(Object *s) {
    if (icount * 2 >= icap) {
        size_t ncap = icap ? icap * 2 : 1024;
        Object **n = xcalloc(ncap, sizeof(Object *));
        for (size_t i = 0; i < icap; i++) {
            if (!itab[i]) continue;
            size_t h = jhash(itab[i]) & (ncap - 1);
            while (n[h]) h = (h + 1) & (ncap - 1);
            n[h] = itab[i];
        }
        free(itab);
        itab = n;
        icap = ncap;
    }
    size_t h = jhash(s) & (icap - 1);
    jint len = jstrlen(s);
    while (itab[h]) {
        Object *o = itab[h];
        if (jstrlen(o) == len && !memcmp(jstrchars(o), jstrchars(s), (size_t)len * 2)) return o;
        h = (h + 1) & (icap - 1);
    }
    itab[h] = s;
    icount++;
    return s;
}

void gc_mark_extra(void (*markfn)(Object *)) {
    for (size_t i = 0; i < icap; i++) if (itab[i]) markfn(itab[i]);
}

/* ------------------------------------------------------------ exceptions */
void fill_stack_trace(Object *thr, int skip_inits) {
    const char *init = intern("<init>"), *fill = intern("fillInStackTrace");
    int start = depth - 1;
    while (skip_inits && start >= 0) {
        Method *m = frames[start].m;
        if ((m->name == init || m->name == fill) && is_subclass(m->cls, cls_throwable)) start--;
        else break;
    }
    int n = start + 1;
    if (n > 1024) n = 1024;
    Object *arr = alloc_prim_array('J', 2 * n);
    if (!arr) { exception = NULL; return; }
    jlong *d = ADATA(arr, jlong);
    for (int k = 0; k < n; k++) {
        Frame *f = &frames[start - k];
        d[2 * k] = (jlong)(uintptr_t)f->m;
        d[2 * k + 1] = frame_line(f->m, f->pc);
    }
    FIELDS(thr)[slot_thr_backtrace].l = arr;
}

static int throw_nesting;

void throw_new(const char *cn, const char *fmt, ...) {
    if (throw_nesting > 4) fatal("can't create %s", cn);
    throw_nesting++;
    exception = NULL;
    Class *c = load_class(cn);
    if (!c || init_class(c)) { throw_nesting--; if (!exception) fatal("can't create %s", cn); return; }
    Object *ex = alloc_object(c);
    if (!ex) { throw_nesting--; return; }
    if (fmt) {
        char msg[600];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(msg, sizeof msg, fmt, ap);
        va_end(ap);
        Object *s = new_string_utf8(msg);
        if (s) FIELDS(ex)[slot_thr_message].l = s;
    }
    fill_stack_trace(ex, 0);
    throw_nesting--;
    exception = ex;
}

void throw_npe(void) { throw_new("java/lang/NullPointerException", NULL); }

/* last resort when Java code can't print an exception */
void print_exception_native(Object *ex) {
    char name[256];
    fflush(stdout);
    java_name(ex->cls->name, name, sizeof name);
    Object *msg = FIELDS(ex)[slot_thr_message].l;
    char *m = msg ? string_utf8(msg) : NULL;
    fprintf(stderr, "Exception in thread \"main\" %s%s%s\n", name, m ? ": " : "", m ? m : "");
    free(m);
    Object *bt = FIELDS(ex)[slot_thr_backtrace].l;
    if (bt) {
        jlong *d = ADATA(bt, jlong);
        for (jint i = 0; i + 1 < bt->len; i += 2) {
            Method *mm = (Method *)(uintptr_t)d[i];
            java_name(mm->cls->name, name, sizeof name);
            fprintf(stderr, "\tat %s.%s(%s:%d)\n", name, mm->name, mm->cls->source ? mm->cls->source : "Unknown Source", (int)d[i + 1]);
        }
    }
    fflush(stderr);
}

void jvm_exit(int code) {
    extern int gc_verbose;
    if (gc_verbose > 0) {
        int nc = 0;
        for (Class *k = class_list; k; k = k->all_next) nc++;
#ifdef MYOS_LIBC
        extern size_t myos_heap_free(void);
        fprintf(stderr, "[java: exit %d, %d classes, C heap free %lu KB]\n", code, nc, (unsigned long)(myos_heap_free() / 1024));
#else
        fprintf(stderr, "[java: exit %d, %d classes]\n", code, nc);
#endif
    }
    fflush(stdout);
    fflush(stderr);
    exit(code);
}

/* ------------------------------------------------------------ natives */
#define ARG_OBJ(i) (a[i].l)
#define STR(s) new_string_utf8(s)

static int check_null(Object *o) {
    if (!o) { throw_npe(); return 1; }
    return 0;
}

static void N_Object_getClass(Slot *a, Slot *r) { r->l = class_mirror(a[0].l->cls); }
static void N_Object_hashCode(Slot *a, Slot *r) {
    uintptr_t p = (uintptr_t)a[0].l;
    r->i = (jint)((p >> 3) * 2654435761u) & 0x7fffffff;
}
static void N_Object_clone(Slot *a, Slot *r) {
    Object *o = a[0].l;
    Class *c = o->cls;
    if (!c->is_array) {
        Class *cl = load_class("java/lang/Cloneable");
        if (!cl || !is_subclass(c, cl)) {
            char b[200];
            java_name(c->name, b, sizeof b);
            throw_new("java/lang/CloneNotSupportedException", "%s", b);
            return;
        }
    }
    Object *n = c->is_array ? alloc_array(c, o->len) : alloc_object(c);
    if (!n) return;
    size_t bytes = c->is_array ? (size_t)o->len * c->elem_size : (size_t)c->n_inst * 8;
    memcpy(n + 1, o + 1, bytes);
    r->l = n;
}

static void N_System_identityHashCode(Slot *a, Slot *r) {
    if (!a[0].l) { r->i = 0; return; }
    N_Object_hashCode(a, r);
}

static void N_System_arraycopy(Slot *a, Slot *r) {
    (void)r;
    Object *src = a[0].l, *dst = a[2].l;
    jint sp = a[1].i, dp = a[3].i, n = a[4].i;
    if (!src || !dst) { throw_npe(); return; }
    if (!src->cls->is_array || !dst->cls->is_array) {
        throw_new("java/lang/ArrayStoreException", "arraycopy: not an array");
        return;
    }
    if (src->cls->elem != dst->cls->elem) {
        throw_new("java/lang/ArrayStoreException", "arraycopy: type mismatch");
        return;
    }
    if (n < 0 || sp < 0 || dp < 0 || sp > src->len - n || dp > dst->len - n) {
        throw_new("java/lang/ArrayIndexOutOfBoundsException", "arraycopy: last source index %d out of bounds for length %d",
                  sp + n, src->len);
        return;
    }
    int es = src->cls->elem_size;
    if (src->cls->elem == 'L' && dst->cls->component != cls_object && !is_subclass(src->cls, dst->cls)) {
        Object **s = ADATA(src, Object *), **d = ADATA(dst, Object *);
        if (src == dst) { memmove(d + dp, s + sp, (size_t)n * es); return; }
        for (jint i = 0; i < n; i++) {
            Object *v = s[sp + i];
            if (v && !is_subclass(v->cls, dst->cls->component)) {
                throw_new("java/lang/ArrayStoreException", "arraycopy: element type mismatch");
                return;
            }
            d[dp + i] = v;
        }
        return;
    }
    memmove((uint8_t *)(dst + 1) + (size_t)dp * es, (uint8_t *)(src + 1) + (size_t)sp * es, (size_t)n * es);
}

static void N_System_currentTimeMillis(Slot *a, Slot *r) { (void)a; r->j = now_millis(); }
static void N_System_nanoTime(Slot *a, Slot *r) { (void)a; r->j = now_nanos(); }
static void N_System_exit(Slot *a, Slot *r) { (void)r; jvm_exit(a[0].i); }
static void N_System_gc(Slot *a, Slot *r) { (void)a; (void)r; gc_collect(); }

extern const char *jvm_property(const char *key);
static void N_System_getProperty0(Slot *a, Slot *r) {
    if (check_null(a[0].l)) return;
    char *k = string_utf8(a[0].l);
    const char *v = jvm_property(k);
    free(k);
    r->l = v ? STR(v) : NULL;
}

static void N_Runtime_freeMemory(Slot *a, Slot *r) { (void)a; r->j = (jlong)heap_free_bytes(); }
static void N_Runtime_totalMemory(Slot *a, Slot *r) { (void)a; r->j = (jlong)heap_total_bytes(); }

static void N_Thread_sleep(Slot *a, Slot *r) { (void)r; sleep_millis(a[0].j); }

static void N_String_intern(Slot *a, Slot *r) { r->l = intern_string(a[0].l); }

/* Class */
static void N_Class_getName(Slot *a, Slot *r) {
    Class *c = mirror_class(a[0].l);
    char b[300];
    java_name(c->name, b, sizeof b);
    r->l = STR(b);
}
static void N_Class_getSimpleName(Slot *a, Slot *r) {
    Class *c = mirror_class(a[0].l);
    char b[300];
    if (c->is_array) {
        Object *save = a[0].l;
        (void)save;
        int dims = 0;
        Class *e = c;
        while (e->is_array) { dims++; e = e->component; }
        java_name(e->name, b, sizeof b);
        char *s = strrchr(b, '.');
        s = s ? s + 1 : b;
        char *d = strrchr(s, '$');
        if (d) s = d + 1;
        char out[300];
        snprintf(out, sizeof out, "%s", s);
        for (int i = 0; i < dims && strlen(out) + 3 < sizeof out; i++) strcat(out, "[]");
        r->l = STR(out);
        return;
    }
    java_name(c->name, b, sizeof b);
    char *s = strrchr(b, '.');
    s = s ? s + 1 : b;
    char *d = strrchr(s, '$');
    if (d) {
        s = d + 1;
        while (*s >= '0' && *s <= '9') s++;     /* anonymous: "", local: digits then name */
    }
    r->l = STR(s);
}
static void N_Class_isInstance(Slot *a, Slot *r) {
    Class *c = mirror_class(a[0].l);
    r->i = a[1].l && is_subclass(a[1].l->cls, c);
}
static void N_Class_isAssignableFrom(Slot *a, Slot *r) {
    if (check_null(a[1].l)) return;
    r->i = is_subclass(mirror_class(a[1].l), mirror_class(a[0].l));
}
static void N_Class_isArray(Slot *a, Slot *r) { r->i = mirror_class(a[0].l)->is_array; }
static void N_Class_isInterface(Slot *a, Slot *r) { r->i = (mirror_class(a[0].l)->access & ACC_INTERFACE) != 0; }
static void N_Class_isPrimitive(Slot *a, Slot *r) { r->i = mirror_class(a[0].l)->prim != 0; }
static void N_Class_getModifiers(Slot *a, Slot *r) { r->i = mirror_class(a[0].l)->access; }
static void N_Class_getSuperclass(Slot *a, Slot *r) {
    Class *c = mirror_class(a[0].l);
    r->l = (c->super && !(c->access & ACC_INTERFACE)) ? class_mirror(c->super) : NULL;
}
static void N_Class_getComponentType(Slot *a, Slot *r) {
    Class *c = mirror_class(a[0].l);
    r->l = c->is_array ? class_mirror(c->component) : NULL;
}
static void N_Class_getEnumConstants0(Slot *a, Slot *r) {
    Class *c = mirror_class(a[0].l);
    char desc[300];
    snprintf(desc, sizeof desc, "()[L%s;", c->name);
    Method *m = find_method(c, intern("values"), intern(desc));
    if (!m || !(m->access & ACC_STATIC)) { r->l = NULL; return; }
    if (init_class(c) || run_method(m, NULL, 0)) return;
    r->l = retval.l;
}
static void N_Class_forName(Slot *a, Slot *r) {
    if (check_null(a[0].l)) return;
    char *n = string_utf8(a[0].l);
    for (char *p = n; *p; p++) if (*p == '.') *p = '/';
    Class *c = n[0] == '[' || !strchr(n, ';') ? load_class(n) : NULL;
    if (!c) {
        exception = NULL;
        for (char *p = n; *p; p++) if (*p == '/') *p = '.';
        throw_new("java/lang/ClassNotFoundException", "%s", n);
        free(n);
        return;
    }
    free(n);
    if (init_class(c)) return;
    r->l = class_mirror(c);
}
static void N_Class_newInstance(Slot *a, Slot *r) {
    Class *c = mirror_class(a[0].l);
    if (c->access & (ACC_ABSTRACT | ACC_INTERFACE) || c->is_array || c->prim) {
        throw_new("java/lang/InstantiationException", "%s", c->name);
        return;
    }
    Method *m = find_method(c, intern("<init>"), intern("()V"));
    if (!m) { throw_new("java/lang/InstantiationException", "%s has no no-argument constructor", c->name); return; }
    if (init_class(c)) return;
    Object *o = alloc_object(c);
    if (!o) return;
    Slot arg;
    arg.l = o;
    if (run_method(m, &arg, 1)) return;
    r->l = o;
}
static void N_Class_hasNoArgConstructor0(Slot *a, Slot *r) {
    Class *c = mirror_class(a[0].l);
    r->i = !c->is_array && !c->prim && find_method(c, intern("<init>"), intern("()V")) != NULL;
}
static void N_Class_getPrimitiveClass(Slot *a, Slot *r) {
    char *n = string_utf8(a[0].l);
    static const char *names[] = { "int", "long", "boolean", "byte", "char", "short", "float", "double", "void" };
    static const char codes[] = "IJZBCSFDV";
    Class *c = NULL;
    for (int i = 0; i < 9; i++) if (!strcmp(n, names[i])) c = prim_class(codes[i]);
    free(n);
    r->l = c ? class_mirror(c) : NULL;
}

/* Throwable */
static void N_Throwable_fillIn0(Slot *a, Slot *r) { (void)r; fill_stack_trace(a[0].l, 1); }

static void N_Throwable_buildStackTrace(Slot *a, Slot *r) {
    Object *thr = a[0].l;
    Object *bt = FIELDS(thr)[slot_thr_backtrace].l;
    jint n = bt ? bt->len / 2 : 0;
    Class *ste = load_class("java/lang/StackTraceElement");
    if (!ste) return;
    Object *arr = alloc_array(array_class_of(ste), n);
    if (!arr) return;
    for (jint i = 0; i < n; i++) {
        bt = FIELDS(thr)[slot_thr_backtrace].l;
        Method *m = (Method *)(uintptr_t)ADATA(bt, jlong)[2 * i];
        jint line = (jint)ADATA(bt, jlong)[2 * i + 1];
        Object *e = alloc_object(ste);
        if (!e) return;
        ADATA(arr, Object *)[i] = e;
        char b[300];
        java_name(m->cls->name, b, sizeof b);
        Object *s;
        if (!(s = STR(b))) return;
        FIELDS(e)[slot_ste_class].l = s;
        if (!(s = STR(m->name))) return;
        FIELDS(e)[slot_ste_method].l = s;
        if (m->cls->source) {
            if (!(s = STR(m->cls->source))) return;
            FIELDS(e)[slot_ste_file].l = s;
        }
        FIELDS(e)[slot_ste_line].i = (m->access & ACC_NATIVE) ? -2 : line;
    }
    r->l = arr;
}

/* Double / Float */
static void N_Double_doubleToRawLongBits(Slot *a, Slot *r) { r->j = a[0].j; }
static void N_Double_longBitsToDouble(Slot *a, Slot *r) { r->j = a[0].j; }
static void N_Float_floatToRawIntBits(Slot *a, Slot *r) { r->i = a[0].i; }
static void N_Float_intBitsToFloat(Slot *a, Slot *r) { r->i = a[0].i; }
static void N_Double_toString(Slot *a, Slot *r) {
    char b[64];
    java_double_string(a[0].d, b);
    r->l = STR(b);
}
static void N_Float_toString(Slot *a, Slot *r) {
    char b[64];
    java_float_string(a[0].f, b);
    r->l = STR(b);
}
static int parse_num(Object *s, double *out) {
    if (check_null(s)) return -1;
    char *t = string_utf8(s);
    int ok;
    *out = parse_double(t, &ok);
    if (!ok) {
        throw_new("java/lang/NumberFormatException", jstrlen(s) ? "For input string: \"%s\"" : "empty String", t);
        free(t);
        return -1;
    }
    free(t);
    return 0;
}
static void N_Double_parseDouble(Slot *a, Slot *r) {
    double d;
    if (!parse_num(a[0].l, &d)) r->d = d;
}
static void N_Float_parseFloat(Slot *a, Slot *r) {
    double d;
    if (!parse_num(a[0].l, &d)) r->f = (float)d;
}

/* Formatter: digits of |v| for %f / %e (Java rounds the shortest decimal form half-up) */
static void N_Formatter_fmtDouble(Slot *a, Slot *r) {
    double v = fabs(a[0].d);
    int conv = a[2].i, prec = a[3].i;
    char digits[32];
    int e10 = 0, nd;
    if (prec < 0) prec = 6;
    if (prec > 300) prec = 300;
    if (v == 0) { digits[0] = '0'; nd = 1; e10 = 0; }
    else nd = double_to_digits(v, digits, &e10);       /* v = 0.d1d2... * 10^e10 */
    char *out = xmalloc((size_t)prec + 400);
    char buf[400];
    int bl = 0;
    if (conv == 'e') {
        /* keep prec+1 significant digits */
        int keep = prec + 1;
        char d[400];
        memset(d, '0', sizeof d);
        memcpy(d, digits, (size_t)nd);
        int ex = e10 - 1;
        if (nd > keep) {
            int up = d[keep] >= '5';
            for (int i = keep; i < 400; i++) d[i] = '0';
            for (int i = keep - 1; up && i >= 0; i--) {
                if (d[i] == '9') d[i] = '0';
                else { d[i]++; up = 0; }
            }
            if (up) { memmove(d + 1, d, 398); d[0] = '1'; ex++; }
        }
        if (v == 0) ex = 0;
        bl = 0;
        out[bl++] = d[0];
        if (prec > 0) out[bl++] = '.';
        for (int i = 1; i <= prec; i++) out[bl++] = i < 400 ? d[i] : '0';
        out[bl] = 0;
        snprintf(buf, sizeof buf, "e%c%02d", ex < 0 ? '-' : '+', ex < 0 ? -ex : ex);
        strcat(out, buf);
        r->l = STR(out);
        free(out);
        return;
    }
    /* %f: integer digits = e10 (may be <= 0) */
    int total = e10 + prec;     /* digits kept counting from the first significant one */
    char d[700];
    int dl = 0;
    if (total < 0) {
        dl = 0;
    } else {
        memset(d, '0', sizeof d);
        int copy = nd < (int)sizeof d ? nd : (int)sizeof d;
        memcpy(d, digits, (size_t)copy);
        dl = total;
        if (nd > total) {
            int up = digits[total] >= '5';
            for (int i = total - 1; up && i >= 0; i--) {
                if (d[i] == '9') d[i] = '0';
                else { d[i]++; up = 0; }
            }
            if (up) { memmove(d + 1, d, (size_t)dl); d[0] = '1'; dl++; e10++; }
        }
    }
    /* now the value is 0.d[0..dl) * 10^e10, rounded to prec decimals */
    bl = 0;
    if (e10 <= 0) out[bl++] = '0';
    else for (int i = 0; i < e10; i++) out[bl++] = i < dl ? d[i] : '0';
    if (prec > 0) {
        out[bl++] = '.';
        for (int i = 0; i < prec; i++) {
            int pos = e10 + i;      /* index in d */
            out[bl++] = (pos >= 0 && pos < dl) ? d[pos] : '0';
        }
    }
    out[bl] = 0;
    (void)buf;
    r->l = STR(out);
    free(out);
}

/* Math */
#define MATH1(name, fn) static void N_Math_##name(Slot *a, Slot *r) { r->d = fn(a[0].d); }
MATH1(sin, sin) MATH1(cos, cos) MATH1(tan, tan) MATH1(asin, asin) MATH1(acos, acos) MATH1(atan, atan)
MATH1(exp, exp) MATH1(log, log) MATH1(log10, log10) MATH1(sqrt, sqrt) MATH1(cbrt, cbrt) MATH1(sinh, sinh)
MATH1(cosh, cosh) MATH1(tanh, tanh) MATH1(expm1, expm1) MATH1(log1p, log1p) MATH1(floor, floor)
MATH1(ceil, ceil) MATH1(rint, rint)
static void N_Math_atan2(Slot *a, Slot *r) { r->d = atan2(a[0].d, a[2].d); }
static void N_Math_pow(Slot *a, Slot *r) { r->d = pow(a[0].d, a[2].d); }
static void N_Math_hypot(Slot *a, Slot *r) { r->d = hypot(a[0].d, a[2].d); }

/* files: fd 0-2 are the console */
static FILE *ftab[32];

static FILE *fd_file(jint fd) {
    if (fd == 0) return stdin;
    if (fd == 1) return stdout;
    if (fd == 2) return stderr;
    if (fd < 0 || fd >= 32 || !ftab[fd]) return NULL;
    return ftab[fd];
}

static void N_FOS_writeBytes0(Slot *a, Slot *r) {
    (void)r;
    Object *b = a[1].l;
    jint off = a[2].i, n = a[3].i;
    if (check_null(b)) return;
    if (off < 0 || n < 0 || off > b->len - n) { throw_new("java/lang/IndexOutOfBoundsException", NULL); return; }
    FILE *f = fd_file(a[0].i);
    if (!f) { throw_new("java/io/IOException", "Stream closed"); return; }
    if (f == stderr) fflush(stdout);
    fwrite(ADATA(b, jbyte) + off, 1, (size_t)n, f);
}
static void N_FOS_writeString0(Slot *a, Slot *r) {
    (void)r;
    Object *s = a[1].l;
    FILE *f = fd_file(a[0].i);
    if (!f) { throw_new("java/io/IOException", "Stream closed"); return; }
    if (f == stderr) fflush(stdout);
    char *t = string_utf8(s);
    fwrite(t, 1, strlen(t), f);
    free(t);
}
static void N_FOS_flush0(Slot *a, Slot *r) {
    (void)r;
    FILE *f = fd_file(a[0].i);
    if (f) fflush(f);
}
static int open_file(Object *path, const char *mode) {
    if (check_null(path)) return -1;
    char *p = string_utf8(path);
    int fd = -1;
    for (int i = 3; i < 32; i++) if (!ftab[i]) { fd = i; break; }
    FILE *f = fd >= 0 ? fopen(p, mode) : NULL;
    if (!f) {
        throw_new("java/io/FileNotFoundException", "%s (%s)", p, fd < 0 ? "Too many open files" : "No such file or directory");
        free(p);
        return -1;
    }
    free(p);
    ftab[fd] = f;
    return fd;
}
static void N_FOS_open0(Slot *a, Slot *r) { r->i = open_file(a[0].l, a[1].i ? "ab" : "wb"); }
static void N_FIS_open0(Slot *a, Slot *r) { r->i = open_file(a[0].l, "rb"); }
static void N_FS_close0(Slot *a, Slot *r) {
    (void)r;
    jint fd = a[0].i;
    if (fd >= 3 && fd < 32 && ftab[fd]) { fclose(ftab[fd]); ftab[fd] = NULL; }
}
static void N_FIS_read0(Slot *a, Slot *r) {
    FILE *f = fd_file(a[0].i);
    if (!f) { throw_new("java/io/IOException", "Stream closed"); return; }
    if (f == stdin) fflush(stdout);
    int c = fgetc(f);
    r->i = c == EOF ? -1 : (c & 255);
}
static void N_FIS_readBytes0(Slot *a, Slot *r) {
    Object *b = a[1].l;
    jint off = a[2].i, n = a[3].i;
    if (check_null(b)) return;
    if (off < 0 || n < 0 || off > b->len - n) { throw_new("java/lang/IndexOutOfBoundsException", NULL); return; }
    FILE *f = fd_file(a[0].i);
    if (!f) { throw_new("java/io/IOException", "Stream closed"); return; }
    if (n == 0) { r->i = 0; return; }
    jint k = 0;
    if (f == stdin) {           /* the console: up to the end of the line */
        fflush(stdout);
        while (k < n) {
            int c = fgetc(f);
            if (c == EOF) break;
            ADATA(b, jbyte)[off + k++] = (jbyte)c;
            if (c == '\n') break;
        }
    } else k = (jint)fread(ADATA(b, jbyte) + off, 1, (size_t)n, f);
    r->i = k == 0 ? -1 : k;
}
static void N_FIS_available0(Slot *a, Slot *r) {
    FILE *f = fd_file(a[0].i);
    if (!f || f == stdin) { r->i = 0; return; }
    long pos = ftell(f);
    fseek(f, 0, SEEK_END);
    long end = ftell(f);
    fseek(f, pos, SEEK_SET);
    r->i = (jint)(end - pos);
}

/* java.io.File */
static void N_File_stat0(Slot *a, Slot *r) {   /* -1 missing, else size; bit 62 = folder */
    char *p = string_utf8(a[0].l);
    struct stat st;
    if (stat(p, &st) != 0) r->j = -1;
    else r->j = ((st.st_mode & S_IFMT) == S_IFDIR) ? (1ll << 62) : (jlong)st.st_size;
    free(p);
}
static void N_File_delete0(Slot *a, Slot *r) {
    char *p = string_utf8(a[0].l);
    r->i = remove(p) == 0;
#ifdef MYOS_LIBC
    if (!r->i) r->i = rmdir(p) == 0;
#endif
    free(p);
}
static void N_File_mkdir0(Slot *a, Slot *r) {
    char *p = string_utf8(a[0].l);
#ifdef MYOS_LIBC
    r->i = mkdir(p, 0755) == 0;
#else
    r->i = 0;
#endif
    free(p);
}
static void N_File_rename0(Slot *a, Slot *r) {
    char *p = string_utf8(a[0].l), *q = string_utf8(a[1].l);
    r->i = rename(p, q) == 0;
    free(p);
    free(q);
}
static void N_File_list0(Slot *a, Slot *r) {
#ifdef MYOS_LIBC
    char *p = string_utf8(a[0].l);
    DIR *d = opendir(p);
    free(p);
    if (!d) { r->l = NULL; return; }
    char *names[512];
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) && n < 512) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        names[n] = xmalloc(strlen(e->d_name) + 1);
        strcpy(names[n++], e->d_name);
    }
    closedir(d);
    Object *arr = alloc_array(array_class_of(cls_string), n);
    for (int i = 0; i < n; i++) {
        if (arr) {
            Object *s = STR(names[i]);
            if (s) ADATA(arr, Object *)[i] = s;
            else arr = NULL;
        }
        free(names[i]);
    }
    r->l = arr;
#else
    (void)a;
    r->l = NULL;
#endif
}

/* Arrays.sort for primitives */
#define CMP(T, name) static int name(const void *x, const void *y) { T a = *(const T *)x, b = *(const T *)y; return a < b ? -1 : a > b; }
CMP(jint, cmp_i) CMP(jlong, cmp_j) CMP(jshort, cmp_s) CMP(jchar, cmp_c) CMP(jbyte, cmp_b)
static int cmp_d(const void *x, const void *y) {
    jdouble a = *(const jdouble *)x, b = *(const jdouble *)y;
    if (a < b) return -1;
    if (a > b) return 1;
    jlong ab, bb;
    if (a != a) a = __builtin_nan("");
    if (b != b) b = __builtin_nan("");
    memcpy(&ab, &a, 8);
    memcpy(&bb, &b, 8);
    return ab == bb ? 0 : (ab < bb ? -1 : 1);
}
static int cmp_f(const void *x, const void *y) {
    jdouble a = *(const jfloat *)x, b = *(const jfloat *)y;
    return cmp_d(&a, &b);
}
static void N_Arrays_sort0(Slot *a, Slot *r) {
    (void)r;
    Object *arr = a[0].l;
    if (check_null(arr)) return;
    jint from = a[1].i, to = a[2].i;
    if (from < 0 || to > arr->len || from > to) {
        throw_new("java/lang/ArrayIndexOutOfBoundsException", "Array index out of range: %d", to);
        return;
    }
    int es = arr->cls->elem_size;
    int (*cmp)(const void *, const void *);
    switch (arr->cls->elem) {
        case 'I': cmp = cmp_i; break;
        case 'J': cmp = cmp_j; break;
        case 'S': cmp = cmp_s; break;
        case 'C': cmp = cmp_c; break;
        case 'B': cmp = cmp_b; break;
        case 'F': cmp = cmp_f; break;
        case 'D': cmp = cmp_d; break;
        default: return;
    }
    qsort((uint8_t *)(arr + 1) + (size_t)from * es, (size_t)(to - from), (size_t)es, cmp);
}

/* java.lang.reflect.Array */
static void N_Array_newInstance(Slot *a, Slot *r) {
    if (check_null(a[0].l)) return;
    Class *c = mirror_class(a[0].l);
    if (c->prim == 'V') { throw_new("java/lang/IllegalArgumentException", NULL); return; }
    r->l = alloc_array(array_class_of(c), a[1].i);
}
static void N_Array_getLength(Slot *a, Slot *r) {
    if (check_null(a[0].l)) return;
    if (!a[0].l->cls->is_array) { throw_new("java/lang/IllegalArgumentException", "Argument is not an array"); return; }
    r->i = a[0].l->len;
}

typedef struct NativeDef { const char *cls, *name, *desc; NativeFn fn; } NativeDef;

static const NativeDef natives[] = {
    { "java/lang/Object", "getClass", "()Ljava/lang/Class;", N_Object_getClass },
    { "java/lang/Object", "hashCode", "()I", N_Object_hashCode },
    { "java/lang/Object", "clone", "()Ljava/lang/Object;", N_Object_clone },
    { "java/lang/System", "identityHashCode", "(Ljava/lang/Object;)I", N_System_identityHashCode },
    { "java/lang/System", "arraycopy", "(Ljava/lang/Object;ILjava/lang/Object;II)V", N_System_arraycopy },
    { "java/lang/System", "currentTimeMillis", "()J", N_System_currentTimeMillis },
    { "java/lang/System", "nanoTime", "()J", N_System_nanoTime },
    { "java/lang/System", "exit", "(I)V", N_System_exit },
    { "java/lang/System", "gc", "()V", N_System_gc },
    { "java/lang/System", "getProperty0", "(Ljava/lang/String;)Ljava/lang/String;", N_System_getProperty0 },
    { "java/lang/Runtime", "freeMemory", "()J", N_Runtime_freeMemory },
    { "java/lang/Runtime", "totalMemory", "()J", N_Runtime_totalMemory },
    { "java/lang/Runtime", "maxMemory", "()J", N_Runtime_totalMemory },
    { "java/lang/Thread", "sleep", "(J)V", N_Thread_sleep },
    { "java/lang/String", "intern", "()Ljava/lang/String;", N_String_intern },
    { "java/lang/Class", "getName0", "()Ljava/lang/String;", N_Class_getName },
    { "java/lang/Class", "getSimpleName", "()Ljava/lang/String;", N_Class_getSimpleName },
    { "java/lang/Class", "isInstance", "(Ljava/lang/Object;)Z", N_Class_isInstance },
    { "java/lang/Class", "isAssignableFrom", "(Ljava/lang/Class;)Z", N_Class_isAssignableFrom },
    { "java/lang/Class", "isArray", "()Z", N_Class_isArray },
    { "java/lang/Class", "isInterface", "()Z", N_Class_isInterface },
    { "java/lang/Class", "isPrimitive", "()Z", N_Class_isPrimitive },
    { "java/lang/Class", "getModifiers", "()I", N_Class_getModifiers },
    { "java/lang/Class", "getSuperclass", "()Ljava/lang/Class;", N_Class_getSuperclass },
    { "java/lang/Class", "getComponentType", "()Ljava/lang/Class;", N_Class_getComponentType },
    { "java/lang/Class", "getEnumConstants0", "()[Ljava/lang/Object;", N_Class_getEnumConstants0 },
    { "java/lang/Class", "forName", "(Ljava/lang/String;)Ljava/lang/Class;", N_Class_forName },
    { "java/lang/Class", "newInstance", "()Ljava/lang/Object;", N_Class_newInstance },
    { "java/lang/Class", "getPrimitiveClass", "(Ljava/lang/String;)Ljava/lang/Class;", N_Class_getPrimitiveClass },
    { "java/lang/Class", "hasNoArgConstructor0", "()Z", N_Class_hasNoArgConstructor0 },
    { "java/lang/Throwable", "fillIn0", "()V", N_Throwable_fillIn0 },
    { "java/lang/Throwable", "buildStackTrace", "()[Ljava/lang/StackTraceElement;", N_Throwable_buildStackTrace },
    { "java/lang/Double", "doubleToRawLongBits", "(D)J", N_Double_doubleToRawLongBits },
    { "java/lang/Double", "longBitsToDouble", "(J)D", N_Double_longBitsToDouble },
    { "java/lang/Double", "toString", "(D)Ljava/lang/String;", N_Double_toString },
    { "java/lang/Double", "parseDouble", "(Ljava/lang/String;)D", N_Double_parseDouble },
    { "java/lang/Float", "floatToRawIntBits", "(F)I", N_Float_floatToRawIntBits },
    { "java/lang/Float", "intBitsToFloat", "(I)F", N_Float_intBitsToFloat },
    { "java/lang/Float", "toString", "(F)Ljava/lang/String;", N_Float_toString },
    { "java/lang/Float", "parseFloat", "(Ljava/lang/String;)F", N_Float_parseFloat },
    { "java/util/Formatter", "fmtDouble", "(DCI)Ljava/lang/String;", N_Formatter_fmtDouble },
    { "java/lang/Math", "sin", "(D)D", N_Math_sin },
    { "java/lang/Math", "cos", "(D)D", N_Math_cos },
    { "java/lang/Math", "tan", "(D)D", N_Math_tan },
    { "java/lang/Math", "asin", "(D)D", N_Math_asin },
    { "java/lang/Math", "acos", "(D)D", N_Math_acos },
    { "java/lang/Math", "atan", "(D)D", N_Math_atan },
    { "java/lang/Math", "exp", "(D)D", N_Math_exp },
    { "java/lang/Math", "log", "(D)D", N_Math_log },
    { "java/lang/Math", "log10", "(D)D", N_Math_log10 },
    { "java/lang/Math", "sqrt", "(D)D", N_Math_sqrt },
    { "java/lang/Math", "cbrt", "(D)D", N_Math_cbrt },
    { "java/lang/Math", "sinh", "(D)D", N_Math_sinh },
    { "java/lang/Math", "cosh", "(D)D", N_Math_cosh },
    { "java/lang/Math", "tanh", "(D)D", N_Math_tanh },
    { "java/lang/Math", "expm1", "(D)D", N_Math_expm1 },
    { "java/lang/Math", "log1p", "(D)D", N_Math_log1p },
    { "java/lang/Math", "floor", "(D)D", N_Math_floor },
    { "java/lang/Math", "ceil", "(D)D", N_Math_ceil },
    { "java/lang/Math", "rint", "(D)D", N_Math_rint },
    { "java/lang/Math", "atan2", "(DD)D", N_Math_atan2 },
    { "java/lang/Math", "pow", "(DD)D", N_Math_pow },
    { "java/lang/Math", "hypot", "(DD)D", N_Math_hypot },
    { "java/io/FileOutputStream", "writeBytes0", "(I[BII)V", N_FOS_writeBytes0 },
    { "java/io/FileOutputStream", "writeString0", "(ILjava/lang/String;)V", N_FOS_writeString0 },
    { "java/io/FileOutputStream", "flush0", "(I)V", N_FOS_flush0 },
    { "java/io/FileOutputStream", "open0", "(Ljava/lang/String;Z)I", N_FOS_open0 },
    { "java/io/FileOutputStream", "close0", "(I)V", N_FS_close0 },
    { "java/io/FileInputStream", "open0", "(Ljava/lang/String;)I", N_FIS_open0 },
    { "java/io/FileInputStream", "close0", "(I)V", N_FS_close0 },
    { "java/io/FileInputStream", "read0", "(I)I", N_FIS_read0 },
    { "java/io/FileInputStream", "readBytes0", "(I[BII)I", N_FIS_readBytes0 },
    { "java/io/FileInputStream", "available0", "(I)I", N_FIS_available0 },
    { "java/io/File", "stat0", "(Ljava/lang/String;)J", N_File_stat0 },
    { "java/io/File", "delete0", "(Ljava/lang/String;)Z", N_File_delete0 },
    { "java/io/File", "mkdir0", "(Ljava/lang/String;)Z", N_File_mkdir0 },
    { "java/io/File", "rename0", "(Ljava/lang/String;Ljava/lang/String;)Z", N_File_rename0 },
    { "java/io/File", "list0", "(Ljava/lang/String;)[Ljava/lang/String;", N_File_list0 },
    { "java/lang/reflect/Array", "newInstance", "(Ljava/lang/Class;I)Ljava/lang/Object;", N_Array_newInstance },
    { "java/lang/reflect/Array", "getLength", "(Ljava/lang/Object;)I", N_Array_getLength },
    { "java/util/Arrays", "sort0", "([III)V", N_Arrays_sort0 },
    { "java/util/Arrays", "sort0", "([JII)V", N_Arrays_sort0 },
    { "java/util/Arrays", "sort0", "([SII)V", N_Arrays_sort0 },
    { "java/util/Arrays", "sort0", "([CII)V", N_Arrays_sort0 },
    { "java/util/Arrays", "sort0", "([BII)V", N_Arrays_sort0 },
    { "java/util/Arrays", "sort0", "([FII)V", N_Arrays_sort0 },
    { "java/util/Arrays", "sort0", "([DII)V", N_Arrays_sort0 },
};

NativeFn find_native(const char *cls, const char *name, const char *desc) {
    for (size_t i = 0; i < sizeof natives / sizeof natives[0]; i++)
        if (!strcmp(natives[i].cls, cls) && !strcmp(natives[i].name, name) && !strcmp(natives[i].desc, desc))
            return natives[i].fn;
    return NULL;
}

/* field slots the C code needs; called once the core classes are loaded */
void native_init(void) {
    Field *f;
#define NEED(c, n, var) do { f = find_field_by_name(c, n); if (!f) fatal("runtime library: no field %s.%s", (c)->name, n); var = (int)f->slot; } while (0)
    NEED(cls_string, "value", slot_str_value);
    NEED(cls_string, "hash", slot_str_hash);
    NEED(cls_throwable, "detailMessage", slot_thr_message);
    NEED(cls_throwable, "backtrace", slot_thr_backtrace);
    NEED(cls_throwable, "cause", slot_thr_cause);
    NEED(cls_class, "vmClass", slot_class_vm);
    Class *ste = load_class("java/lang/StackTraceElement");
    if (!ste) fatal("runtime library: no StackTraceElement");
    NEED(ste, "declaringClass", slot_ste_class);
    NEED(ste, "methodName", slot_ste_method);
    NEED(ste, "fileName", slot_ste_file);
    NEED(ste, "lineNumber", slot_ste_line);
}
