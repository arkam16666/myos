/* class.c - class path, jar files, class file parsing, linking, resolution */
#include "jvm.h"

Class *class_list;

/* ------------------------------------------------------------ symbols */
typedef struct Sym { struct Sym *next; char s[]; } Sym;
#define SYM_BUCKETS 4096
static Sym *syms[SYM_BUCKETS];

static uint32_t hash_n(const char *s, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ (uint8_t)s[i]) * 16777619u;
    return h;
}

const char *intern_n(const char *s, size_t n) {
    uint32_t h = hash_n(s, n) % SYM_BUCKETS;
    for (Sym *p = syms[h]; p; p = p->next)
        if (strncmp(p->s, s, n) == 0 && p->s[n] == 0) return p->s;
    Sym *p = xmalloc(sizeof(Sym) + n + 1);
    memcpy(p->s, s, n);
    p->s[n] = 0;
    p->next = syms[h];
    syms[h] = p;
    return p->s;
}

const char *intern(const char *s) { return intern_n(s, strlen(s)); }

void java_name(const char *internal, char *out, size_t n) {
    size_t i = 0;
    for (; internal[i] && i + 1 < n; i++) out[i] = internal[i] == '/' ? '.' : internal[i];
    out[i] = 0;
}

/* ------------------------------------------------------------ jar files */
typedef struct ZipEntry {
    const char *name;
    uint32_t method, csize, usize, off;
    struct ZipEntry *next;
} ZipEntry;

typedef struct Jar {
    const uint8_t *data;
    size_t len;
    ZipEntry *buckets[256];
} Jar;

typedef struct CpPath {
    char *dir;          /* NULL for jars */
    Jar *jar;
    struct CpPath *next;
} CpPath;

static Jar *runtime_jar;
static CpPath *classpath, **cp_tail = &classpath;

static uint32_t le16(const uint8_t *p) { return p[0] | p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static Jar *open_jar(const uint8_t *data, size_t len) {
    if (len < 22) return NULL;
    size_t eocd = 0;
    int found = 0;
    for (size_t i = len - 22 + 1; i-- > 0 && len - i < 65600;) {
        if (le32(data + i) == 0x06054b50) { eocd = i; found = 1; break; }
    }
    if (!found) return NULL;
    uint32_t n = le16(data + eocd + 10), cd = le32(data + eocd + 16);
    Jar *j = xcalloc(1, sizeof(Jar));
    j->data = data;
    j->len = len;
    const uint8_t *p = data + cd;
    for (uint32_t i = 0; i < n; i++) {
        if (p + 46 > data + len || le32(p) != 0x02014b50) break;
        uint32_t nlen = le16(p + 28), xlen = le16(p + 30), clen = le16(p + 32);
        ZipEntry *e = xmalloc(sizeof(ZipEntry));
        e->method = le16(p + 10);
        e->csize = le32(p + 20);
        e->usize = le32(p + 24);
        e->off = le32(p + 42);
        e->name = intern_n((const char *)p + 46, nlen);
        uint32_t h = hash_n(e->name, nlen) & 255;
        e->next = j->buckets[h];
        j->buckets[h] = e;
        p += 46 + nlen + xlen + clen;
    }
    return j;
}

/* contents of a jar entry (malloc'd), or NULL */
static uint8_t *jar_read(Jar *j, const char *name, size_t *len) {
    uint32_t h = hash_n(name, strlen(name)) & 255;
    for (ZipEntry *e = j->buckets[h]; e; e = e->next) {
        if (strcmp(e->name, name)) continue;
        const uint8_t *lh = j->data + e->off;
        if (e->off + 30 > j->len || le32(lh) != 0x04034b50) return NULL;
        const uint8_t *src = lh + 30 + le16(lh + 26) + le16(lh + 28);
        if (src + e->csize > j->data + j->len) return NULL;
        uint8_t *out = xmalloc(e->usize + 1);
        if (e->method == 0) memcpy(out, src, e->usize);
        else if (e->method != 8 || inflate_raw(src, e->csize, out, e->usize) != (int)e->usize) {
            free(out);
            return NULL;
        }
        out[e->usize] = 0;
        *len = e->usize;
        return out;
    }
    return NULL;
}

static uint8_t *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }
    uint8_t *buf = xmalloc((size_t)n + 1);
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = 0;
    *len = got;
    return buf;
}

void set_runtime_jar(const unsigned char *data, size_t len) {
    runtime_jar = open_jar(data, len);
    if (!runtime_jar) fatal("runtime library is damaged");
}

static int ends_with(const char *s, const char *t) {
    size_t a = strlen(s), b = strlen(t);
    return a >= b && strcmp(s + a - b, t) == 0;
}

void add_classpath(const char *entry) {
    CpPath *c = xcalloc(1, sizeof(CpPath));
    if (ends_with(entry, ".jar") || ends_with(entry, ".zip") || ends_with(entry, ".JAR")) {
        size_t len;
        uint8_t *data = read_file(entry, &len);
        if (!data || !(c->jar = open_jar(data, len))) {
            free(c);
            return;
        }
    } else {
        c->dir = xmalloc(strlen(entry) + 1);
        strcpy(c->dir, entry);
        size_t l = strlen(c->dir);
        while (l > 1 && (c->dir[l - 1] == '/' || c->dir[l - 1] == '\\')) c->dir[--l] = 0;
    }
    *cp_tail = c;
    cp_tail = &c->next;
}

/* Main-Class of a jar: 1 found, 0 no manifest entry, -1 can't open */
int classpath_has_jar(const char *path, char *main_class, size_t n) {
    size_t len;
    uint8_t *data = read_file(path, &len);
    if (!data) return -1;
    Jar *j = open_jar(data, len);
    if (!j) return -1;
    main_class[0] = 0;
    uint8_t *mf = jar_read(j, "META-INF/MANIFEST.MF", &len);
    if (mf) {
        char *p = strstr((char *)mf, "Main-Class:");
        if (p) {
            p += 11;
            while (*p == ' ') p++;
            size_t i = 0;
            while (*p && *p != '\r' && *p != '\n' && i + 1 < n) main_class[i++] = *p++;
            main_class[i] = 0;
        }
        free(mf);
    }
    return main_class[0] ? 1 : 0;
}

int class_resource(const char *name, unsigned char **data, size_t *len) {
    for (CpPath *c = classpath; c; c = c->next) {
        if (c->jar) {
            if ((*data = jar_read(c->jar, name, len))) return 1;
        } else {
            char path[512];
            snprintf(path, sizeof path, "%s/%s", c->dir, name);
            if ((*data = read_file(path, len))) return 1;
        }
    }
    return 0;
}

static uint8_t *class_bytes(const char *name, size_t *len) {
    char file[512];
    snprintf(file, sizeof file, "%s.class", name);
    uint8_t *d = jar_read(runtime_jar, file, len);
    if (d) return d;
    if (strncmp(name, "java/", 5) == 0) return NULL;   /* the runtime's packages stay ours */
    unsigned char *data;
    return class_resource(file, &data, len) ? data : NULL;
}

/* ------------------------------------------------------------ class table */
#define CLASS_BUCKETS 1024
static Class *classes[CLASS_BUCKETS];

Class *find_loaded(const char *name) {
    uint32_t h = (uint32_t)((uintptr_t)name >> 3) % CLASS_BUCKETS;
    for (Class *c = classes[h]; c; c = c->hash_next)
        if (c->name == name) return c;
    return NULL;
}

static void add_class(Class *c) {
    uint32_t h = (uint32_t)((uintptr_t)c->name >> 3) % CLASS_BUCKETS;
    c->hash_next = classes[h];
    classes[h] = c;
    c->all_next = class_list;
    class_list = c;
}

static void remove_class(Class *c) {
    uint32_t h = (uint32_t)((uintptr_t)c->name >> 3) % CLASS_BUCKETS;
    for (Class **p = &classes[h]; *p; p = &(*p)->hash_next)
        if (*p == c) { *p = c->hash_next; break; }
    for (Class **p = &class_list; *p; p = &(*p)->all_next)
        if (*p == c) { *p = c->all_next; break; }
}

/* ------------------------------------------------------------ descriptors */
int desc_arg_slots(const char *d) {
    int n = 0;
    if (*d == '(') d++;
    while (*d && *d != ')') {
        if (*d == 'J' || *d == 'D') { n += 2; d++; continue; }
        n++;
        while (*d == '[') d++;
        if (*d == 'L') while (*d && *d != ';') d++;
        d++;
    }
    return n;
}

char desc_ret(const char *d) {
    const char *r = strchr(d, ')');
    char c = r ? r[1] : 'V';
    switch (c) {
        case 'Z': case 'B': case 'C': case 'S': case 'I': return 'I';
        case '[': case 'L': return 'L';
        default: return c;
    }
}

static int elem_size_of(char t) {
    switch (t) {
        case 'Z': case 'B': return 1;
        case 'C': case 'S': return 2;
        case 'I': case 'F': return 4;
        default: return 8;
    }
}

/* ------------------------------------------------------------ parsing */
typedef struct { const uint8_t *p, *end; int bad; } Rd;
static uint32_t u1(Rd *r) { if (r->p + 1 > r->end) { r->bad = 1; return 0; } return *r->p++; }
static uint32_t u2(Rd *r) {
    if (r->p + 2 > r->end) { r->bad = 1; return 0; }
    uint32_t v = (uint32_t)r->p[0] << 8 | r->p[1];
    r->p += 2;
    return v;
}
static uint32_t u4(Rd *r) {
    if (r->p + 4 > r->end) { r->bad = 1; return 0; }
    uint32_t v = (uint32_t)r->p[0] << 24 | (uint32_t)r->p[1] << 16 | (uint32_t)r->p[2] << 8 | r->p[3];
    r->p += 4;
    return v;
}

static const char *cp_utf8(Class *c, uint32_t i) {
    return i && i < c->cp_n && c->cp_tag[i] == CP_UTF8 ? c->cp[i].utf8 : "";
}
static const char *cp_class_name(Class *c, uint32_t i) {
    return i && i < c->cp_n && c->cp_tag[i] == CP_CLASS ? cp_utf8(c, c->cp[i].ref.a) : NULL;
}

static Class *parse_class(const uint8_t *data, size_t len) {
    Rd r = { data, data + len, 0 };
    if (u4(&r) != 0xCAFEBABE) return NULL;
    u2(&r);
    u2(&r);
    Class *c = xcalloc(1, sizeof(Class));
    c->cp_n = (uint16_t)u2(&r);
    c->cp_tag = xcalloc(c->cp_n, 1);
    c->cp = xcalloc(c->cp_n, sizeof(CpEntry));
    c->cp_res = xcalloc(c->cp_n, sizeof(void *));
    for (uint32_t i = 1; i < c->cp_n && !r.bad; i++) {
        uint32_t tag = u1(&r);
        c->cp_tag[i] = (uint8_t)tag;
        switch (tag) {
            case CP_UTF8: {
                uint32_t n = u2(&r);
                if (r.p + n > r.end) { r.bad = 1; break; }
                c->cp[i].utf8 = intern_n((const char *)r.p, n);
                r.p += n;
                break;
            }
            case CP_INT: case CP_FLOAT: c->cp[i].i = (jint)u4(&r); break;
            case CP_LONG: case CP_DOUBLE: {
                uint64_t hi = u4(&r), lo = u4(&r);
                c->cp[i].j = (jlong)(hi << 32 | lo);
                i++;
                break;
            }
            case CP_CLASS: case CP_STRING: case CP_MTYPE: case CP_MODULE: case CP_PACKAGE:
                c->cp[i].ref.a = (uint16_t)u2(&r);
                break;
            case CP_MHANDLE:
                c->cp[i].ref.a = (uint16_t)u1(&r);
                c->cp[i].ref.b = (uint16_t)u2(&r);
                break;
            case CP_FIELD: case CP_METHOD: case CP_IMETHOD: case CP_NAT: case CP_DYNAMIC: case CP_INDY:
                c->cp[i].ref.a = (uint16_t)u2(&r);
                c->cp[i].ref.b = (uint16_t)u2(&r);
                break;
            default: r.bad = 1;
        }
    }
    if (r.bad) return NULL;
    c->access = (uint16_t)u2(&r);
    const char *nm = cp_class_name(c, u2(&r));
    if (!nm) return NULL;
    c->name = nm;
    uint32_t sup = u2(&r);
    c->super = (Class *)(sup ? cp_class_name(c, sup) : NULL);    /* name for now */
    c->n_ifaces = (uint16_t)u2(&r);
    c->ifaces = xcalloc(c->n_ifaces + 1, sizeof(Class *));
    for (int i = 0; i < c->n_ifaces; i++) c->ifaces[i] = (Class *)cp_class_name(c, u2(&r));

    c->n_fields = (uint16_t)u2(&r);
    c->fields = xcalloc(c->n_fields + 1, sizeof(Field));
    for (int i = 0; i < c->n_fields && !r.bad; i++) {
        Field *f = &c->fields[i];
        f->cls = c;
        f->access = (uint16_t)u2(&r);
        f->name = cp_utf8(c, u2(&r));
        f->desc = cp_utf8(c, u2(&r));
        uint32_t na = u2(&r);
        for (uint32_t a = 0; a < na && !r.bad; a++) {
            const char *an = cp_utf8(c, u2(&r));
            uint32_t al = u4(&r);
            if (!strcmp(an, "ConstantValue") && al == 2) f->constval = (uint16_t)u2(&r);
            else r.p += al;
        }
    }
    c->n_methods = (uint16_t)u2(&r);
    c->methods = xcalloc(c->n_methods + 1, sizeof(Method));
    for (int i = 0; i < c->n_methods && !r.bad; i++) {
        Method *m = &c->methods[i];
        m->cls = c;
        m->access = (uint16_t)u2(&r);
        m->name = cp_utf8(c, u2(&r));
        m->desc = cp_utf8(c, u2(&r));
        m->arg_slots = (uint16_t)(desc_arg_slots(m->desc) + ((m->access & ACC_STATIC) ? 0 : 1));
        m->ret = desc_ret(m->desc);
        uint32_t na = u2(&r);
        for (uint32_t a = 0; a < na && !r.bad; a++) {
            const char *an = cp_utf8(c, u2(&r));
            uint32_t al = u4(&r);
            const uint8_t *next = r.p + al;
            if (!strcmp(an, "Code")) {
                m->max_stack = (uint16_t)u2(&r);
                m->max_locals = (uint16_t)u2(&r);
                m->code_len = u4(&r);
                m->code = (uint8_t *)r.p;
                r.p += m->code_len;
                m->n_ex = (uint16_t)u2(&r);
                m->ex = xcalloc(m->n_ex + 1, sizeof(ExEntry));
                for (int e = 0; e < m->n_ex; e++) {
                    m->ex[e].start = (uint16_t)u2(&r);
                    m->ex[e].end = (uint16_t)u2(&r);
                    m->ex[e].handler = (uint16_t)u2(&r);
                    m->ex[e].type = (uint16_t)u2(&r);
                }
                uint32_t nca = u2(&r);
                for (uint32_t ca = 0; ca < nca && !r.bad; ca++) {
                    const char *cn = cp_utf8(c, u2(&r));
                    uint32_t cl = u4(&r);
                    const uint8_t *cnext = r.p + cl;
                    if (!strcmp(cn, "LineNumberTable")) {
                        uint32_t nl = u2(&r);
                        LineEntry *le = xmalloc((m->n_lines + nl + 1) * sizeof(LineEntry));
                        if (m->n_lines) memcpy(le, m->lines, m->n_lines * sizeof(LineEntry));
                        for (uint32_t l = 0; l < nl; l++) {
                            le[m->n_lines + l].pc = (uint16_t)u2(&r);
                            le[m->n_lines + l].line = (uint16_t)u2(&r);
                        }
                        m->lines = le;
                        m->n_lines = (uint16_t)(m->n_lines + nl);
                    }
                    r.p = cnext;
                }
            }
            r.p = next;
        }
    }
    uint32_t na = u2(&r);
    for (uint32_t a = 0; a < na && !r.bad; a++) {
        const char *an = cp_utf8(c, u2(&r));
        uint32_t al = u4(&r);
        const uint8_t *next = r.p + al;
        if (!strcmp(an, "SourceFile")) c->source = cp_utf8(c, u2(&r));
        else if (!strcmp(an, "BootstrapMethods")) {
            c->n_bsm = (uint16_t)u2(&r);
            c->bsm = xcalloc(c->n_bsm + 1, sizeof(uint16_t *));
            for (int b = 0; b < c->n_bsm; b++) {
                uint32_t mh = u2(&r), nargs = u2(&r);
                uint16_t *e = xmalloc((nargs + 2) * sizeof(uint16_t));
                e[0] = (uint16_t)mh;
                e[1] = (uint16_t)nargs;
                for (uint32_t k = 0; k < nargs; k++) e[2 + k] = (uint16_t)u2(&r);
                c->bsm[b] = e;
            }
        }
        r.p = next;
    }
    if (r.bad) return NULL;
    return c;
}

/* ------------------------------------------------------------ linking */
static void dotted_throw(const char *exc, const char *name, const char *extra) {
    char buf[300];
    java_name(name, buf, sizeof buf);
    throw_new(exc, "%s%s", buf, extra ? extra : "");
}

static int link_class(Class *c) {
    const char *supname = (const char *)c->super;
    c->super = NULL;
    if (supname) {
        Class *s = load_class(supname);
        if (!s) return -1;
        if (s->access & ACC_INTERFACE) {
            throw_new("java/lang/IncompatibleClassChangeError", "class %s has interface %s as super class", c->name, s->name);
            return -1;
        }
        c->super = s;
    }
    for (int i = 0; i < c->n_ifaces; i++) {
        Class *f = load_class((const char *)c->ifaces[i]);
        if (!f) return -1;
        c->ifaces[i] = f;
    }
    /* field layout */
    uint32_t ni = c->super ? c->super->n_inst : 0, ns = 0;
    for (int i = 0; i < c->n_fields; i++) {
        Field *f = &c->fields[i];
        f->slot = (f->access & ACC_STATIC) ? ns++ : ni++;
    }
    c->n_inst = ni;
    c->n_statics = (uint16_t)ns;
    c->refmap = xcalloc(ni + 1, 1);
    if (c->super) memcpy(c->refmap, c->super->refmap, c->super->n_inst);
    c->statics = xcalloc(ns + 1, sizeof(Slot));
    c->static_refmap = xcalloc(ns + 1, 1);
    for (int i = 0; i < c->n_fields; i++) {
        Field *f = &c->fields[i];
        int ref = f->desc[0] == 'L' || f->desc[0] == '[';
        if (f->access & ACC_STATIC) c->static_refmap[f->slot] = (uint8_t)ref;
        else c->refmap[f->slot] = (uint8_t)ref;
    }
    for (int i = 0; i < c->n_methods; i++) {
        Method *m = &c->methods[i];
        if (m->access & ACC_NATIVE) m->native = find_native(c->name, m->name, m->desc);
    }
    c->state = CS_LINKED;
    return 0;
}

static int loading_depth;

Class *load_class(const char *name) {
    name = intern(name);
    Class *c = find_loaded(name);
    if (c) return c;
    if (name[0] == '[') {
        const char *e = name + 1;
        Class *comp;
        if (*e == 'L') {
            size_t n = strlen(e);
            if (n < 3 || e[n - 1] != ';') { dotted_throw("java/lang/NoClassDefFoundError", name, NULL); return NULL; }
            comp = load_class(intern_n(e + 1, n - 2));
        } else if (*e == '[') comp = load_class(e);
        else comp = prim_class(*e);
        if (!comp) {
            if (!exception) dotted_throw("java/lang/NoClassDefFoundError", name, NULL);
            return NULL;
        }
        return array_class_of(comp);
    }
    if (loading_depth > 200) { dotted_throw("java/lang/ClassCircularityError", name, NULL); return NULL; }
    size_t len;
    uint8_t *data = class_bytes(name, &len);
    if (!data) { dotted_throw("java/lang/NoClassDefFoundError", name, NULL); return NULL; }
    c = parse_class(data, len);
    if (!c) { dotted_throw("java/lang/ClassFormatError", name, " (damaged class file)"); return NULL; }
    if (c->name != name) {
        char buf[300];
        java_name(c->name, buf, sizeof buf);
        dotted_throw("java/lang/NoClassDefFoundError", name, NULL);
        return NULL;
    }
    add_class(c);
    loading_depth++;
    int bad = link_class(c);
    loading_depth--;
    if (bad) {
        remove_class(c);
        return NULL;
    }
    return c;
}

static Class *new_synthetic(const char *name) {
    Class *c = xcalloc(1, sizeof(Class));
    c->name = intern(name);
    c->access = ACC_PUBLIC | ACC_FINAL;
    c->refmap = xcalloc(1, 1);
    c->statics = xcalloc(1, sizeof(Slot));
    c->static_refmap = xcalloc(1, 1);
    c->ifaces = xcalloc(1, sizeof(Class *));
    c->fields = xcalloc(1, sizeof(Field));
    c->methods = xcalloc(1, sizeof(Method));
    c->state = CS_INITED;
    return c;
}

Class *prim_class(char t) {
    static Class *prims[128];
    if ((unsigned char)t >= 128) return NULL;
    if (prims[(int)t]) return prims[(int)t];
    const char *n;
    switch (t) {
        case 'I': n = "int"; break;
        case 'J': n = "long"; break;
        case 'Z': n = "boolean"; break;
        case 'B': n = "byte"; break;
        case 'C': n = "char"; break;
        case 'S': n = "short"; break;
        case 'F': n = "float"; break;
        case 'D': n = "double"; break;
        case 'V': n = "void"; break;
        default: return NULL;
    }
    Class *c = new_synthetic(n);
    c->prim = t;
    c->access = ACC_PUBLIC | ACC_FINAL | ACC_ABSTRACT;
    prims[(int)t] = c;
    add_class(c);
    return c;
}

Class *array_class_of(Class *e) {
    if (e->array_class) return e->array_class;
    char buf[512];
    if (e->prim) snprintf(buf, sizeof buf, "[%c", e->prim);
    else if (e->name[0] == '[') snprintf(buf, sizeof buf, "[%s", e->name);
    else snprintf(buf, sizeof buf, "[L%s;", e->name);
    Class *c = find_loaded(intern(buf));
    if (!c) {
        c = new_synthetic(buf);
        c->super = cls_object;
        c->is_array = 1;
        c->elem = e->prim ? e->prim : 'L';
        c->elem_size = (uint8_t)elem_size_of(c->elem);
        c->component = e;
        c->n_inst = 0;
        Class *cl = load_class("java/lang/Cloneable"), *se = load_class("java/io/Serializable");
        exception = NULL;
        c->ifaces = xcalloc(3, sizeof(Class *));
        if (cl) c->ifaces[c->n_ifaces++] = cl;
        if (se) c->ifaces[c->n_ifaces++] = se;
        add_class(c);
    }
    e->array_class = c;
    return c;
}

Class *define_lambda_class(Class *host, const char *iface_name) {
    static int counter;
    char buf[300];
    snprintf(buf, sizeof buf, "%s$$Lambda$%d", host->name, ++counter);
    Class *iface = load_class(iface_name);
    if (!iface) return NULL;
    Class *c = new_synthetic(buf);
    c->super = cls_object;
    c->ifaces = xcalloc(2, sizeof(Class *));
    c->ifaces[0] = iface;
    c->n_ifaces = 1;
    c->source = host->source;
    add_class(c);
    return c;
}

/* ------------------------------------------------------------ initialisation */
int init_class(Class *c) {
    if (c->state == CS_INITED || c->state == CS_INITING) return 0;
    if (c->state == CS_ERROR) {
        char buf[300];
        java_name(c->name, buf, sizeof buf);
        throw_new("java/lang/NoClassDefFoundError", "Could not initialize class %s", buf);
        return -1;
    }
    c->state = CS_INITING;
    if (c->super && init_class(c->super)) {
        c->state = CS_ERROR;
        return -1;
    }
    for (int i = 0; i < c->n_fields; i++) {
        Field *f = &c->fields[i];
        if (!(f->access & ACC_STATIC) || !f->constval) continue;
        int k = f->constval;
        switch (c->cp_tag[k]) {
            case CP_INT: case CP_FLOAT: c->statics[f->slot].i = c->cp[k].i; break;
            case CP_LONG: case CP_DOUBLE: c->statics[f->slot].j = c->cp[k].j; break;
            case CP_STRING: {
                Object *s = resolve_string_ref(c, k);
                if (!s) { c->state = CS_ERROR; return -1; }
                c->statics[f->slot].l = s;
                break;
            }
        }
    }
    Method *clinit = find_method(c, intern("<clinit>"), intern("()V"));
    if (clinit && run_method(clinit, NULL, 0)) {
        c->state = CS_ERROR;
        Object *ex = exception;
        Class *err = load_class("java/lang/Error");
        if (ex && err && !is_subclass(ex->cls, err)) {
            exception = NULL;
            throw_new("java/lang/ExceptionInInitializerError", NULL);
            if (exception) FIELDS(exception)[slot_thr_cause].l = ex;
        } else exception = ex;
        return -1;
    }
    c->state = CS_INITED;
    return 0;
}

/* ------------------------------------------------------------ lookup */
Method *find_method(Class *c, const char *name, const char *desc) {
    for (int i = 0; i < c->n_methods; i++) {
        Method *m = &c->methods[i];
        if (m->name == name && m->desc == desc) return m;
    }
    return NULL;
}

static Method *iface_search(Class *c, const char *name, const char *desc, int want_concrete) {
    for (Class *k = c; k; k = k->super) {
        for (int i = 0; i < k->n_ifaces; i++) {
            Class *f = k->ifaces[i];
            Method *m = find_method(f, name, desc);
            if (m && !(m->access & ACC_STATIC) && !(m->access & ACC_PRIVATE) &&
                (!want_concrete || !(m->access & ACC_ABSTRACT))) return m;
            m = iface_search(f, name, desc, want_concrete);
            if (m) return m;
        }
    }
    return NULL;
}

Method *resolve_method(Class *c, const char *name, const char *desc) {
    for (Class *k = c; k; k = k->super) {
        Method *m = find_method(k, name, desc);
        if (m) return m;
    }
    Method *m = iface_search(c, name, desc, 1);
    if (!m) m = iface_search(c, name, desc, 0);
    if (!m && (c->access & ACC_INTERFACE)) {        /* interfaces inherit Object's methods */
        m = find_method(cls_object, name, desc);
    }
    return m;
}

#define VCACHE 4096
static struct { Class *c; const char *name, *desc; Method *m; } vcache[VCACHE];

Method *find_virtual(Class *c, const char *name, const char *desc) {
    uint32_t h = (uint32_t)(((uintptr_t)c >> 4) ^ ((uintptr_t)name >> 3) ^ ((uintptr_t)desc >> 5)) % VCACHE;
    if (vcache[h].c == c && vcache[h].name == name && vcache[h].desc == desc) return vcache[h].m;
    Method *found = NULL, *abstract = NULL;
    for (Class *k = c; k && !found; k = k->super) {
        Method *m = find_method(k, name, desc);
        if (m && !(m->access & ACC_STATIC)) {
            if (m->access & ACC_ABSTRACT) { if (!abstract) abstract = m; }
            else found = m;
        }
    }
    if (!found) found = iface_search(c, name, desc, 1);
    if (!found) return abstract;
    vcache[h].c = c;
    vcache[h].name = name;
    vcache[h].desc = desc;
    vcache[h].m = found;
    return found;
}

static Field *field_in(Class *c, const char *name, const char *desc) {
    for (int i = 0; i < c->n_fields; i++)
        if (c->fields[i].name == name && (!desc || c->fields[i].desc == desc)) return &c->fields[i];
    return NULL;
}

Field *resolve_field(Class *c, const char *name, const char *desc) {
    for (Class *k = c; k; k = k->super) {
        Field *f = field_in(k, name, desc);
        if (f) return f;
        for (int i = 0; i < k->n_ifaces; i++) {
            f = resolve_field(k->ifaces[i], name, desc);
            if (f) return f;
        }
    }
    return NULL;
}

Field *find_field_by_name(Class *c, const char *name) {
    return resolve_field(c, intern(name), NULL);
}

static int implements(Class *s, Class *t) {
    for (Class *k = s; k; k = k->super)
        for (int i = 0; i < k->n_ifaces; i++)
            if (k->ifaces[i] == t || implements(k->ifaces[i], t)) return 1;
    return 0;
}

int is_subclass(Class *s, Class *t) {
    if (s == t || t == cls_object) return 1;
    if (s->is_array) {
        if (t->is_array) {
            if (s->elem != 'L' || t->elem != 'L') return s->elem == t->elem && s->component == t->component;
            return is_subclass(s->component, t->component);
        }
        return (t->access & ACC_INTERFACE) && implements(s, t);
    }
    if (t->is_array || s->prim || t->prim) return 0;
    if (t->access & ACC_INTERFACE) return implements(s, t);
    for (Class *k = s->super; k; k = k->super)
        if (k == t) return 1;
    return 0;
}

/* ------------------------------------------------------------ constant pool resolution */
Class *resolve_class_ref(Class *c, int idx) {
    if (c->cp_res[idx]) return c->cp_res[idx];
    Class *k = load_class(cp_utf8(c, c->cp[idx].ref.a));
    if (k) c->cp_res[idx] = k;
    return k;
}

static void nat_of(Class *c, int nat, const char **name, const char **desc) {
    *name = cp_utf8(c, c->cp[nat].ref.a);
    *desc = cp_utf8(c, c->cp[nat].ref.b);
}

Field *resolve_field_ref(Class *c, int idx) {
    if (c->cp_res[idx]) return c->cp_res[idx];
    Class *k = resolve_class_ref(c, c->cp[idx].ref.a);
    if (!k) return NULL;
    const char *name, *desc;
    nat_of(c, c->cp[idx].ref.b, &name, &desc);
    Field *f = resolve_field(k, name, desc);
    if (!f) {
        char buf[300];
        java_name(k->name, buf, sizeof buf);
        throw_new("java/lang/NoSuchFieldError", "%s.%s (not in MyOS Java's runtime library)", buf, name);
        return NULL;
    }
    c->cp_res[idx] = f;
    return f;
}

MethodRef *resolve_method_ref(Class *c, int idx) {
    if (c->cp_res[idx]) return c->cp_res[idx];
    Class *k = resolve_class_ref(c, c->cp[idx].ref.a);
    if (!k) return NULL;
    const char *name, *desc;
    nat_of(c, c->cp[idx].ref.b, &name, &desc);
    Method *m = resolve_method(k, name, desc);
    if (!m) {
        char buf[300];
        java_name(k->name, buf, sizeof buf);
        throw_new("java/lang/NoSuchMethodError", "%s.%s%s (not in MyOS Java's runtime library)", buf, name, desc);
        return NULL;
    }
    MethodRef *r = xcalloc(1, sizeof(MethodRef));
    r->m = m;
    c->cp_res[idx] = r;
    return r;
}

Object *resolve_string_ref(Class *c, int idx) {
    if (c->cp_res[idx]) return c->cp_res[idx];
    Object *s = new_string_utf8(cp_utf8(c, c->cp[idx].ref.a));
    if (!s) return NULL;
    s = intern_string(s);
    c->cp_res[idx] = s;
    return s;
}

/* ------------------------------------------------------------ mirrors */
Object *class_mirror(Class *c) {
    if (c->mirror) return c->mirror;
    Object *m = alloc_object(cls_class);
    if (!m) return NULL;
    FIELDS(m)[slot_class_vm].p = c;
    c->mirror = m;
    return m;
}

Class *mirror_class(Object *mirror) {
    return mirror ? (Class *)FIELDS(mirror)[slot_class_vm].p : NULL;
}
