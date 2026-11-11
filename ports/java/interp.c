/* interp.c - the bytecode interpreter, method calls, exceptions, invokedynamic */
#include <math.h>
#include "jvm.h"

Slot *stack_base, *stack_end;
Frame *frames;
int depth, max_depth;
Object *exception;
Slot retval;
int native_nesting;

#define U2(p) ((uint16_t)((p)[0] << 8 | (p)[1]))
#define S2(p) ((int16_t)U2(p))
#define S4(p) ((jint)((uint32_t)(p)[0] << 24 | (uint32_t)(p)[1] << 16 | (uint32_t)(p)[2] << 8 | (p)[3]))

/* ------------------------------------------------------------ frames */
int push_frame(Method *m, Slot *args) {
    if (depth >= max_depth || args + m->max_locals + m->max_stack + 16 > stack_end) {
        throw_new("java/lang/StackOverflowError", NULL);
        return -1;
    }
    static int trace = -1;
    if (trace < 0) trace = getenv("JTRACE") != NULL;
    if (trace) fprintf(stderr, "%*s> %s.%s%s\n", depth, "", m->cls->name, m->name, m->desc);
    Frame *f = &frames[depth++];
    f->m = m;
    f->locals = args;
    f->pc = m->code;
    f->sp = args + m->max_locals;
    f->ret_conv = 0;
    for (int i = m->arg_slots; i < m->max_locals; i++) args[i].u = 0;
    return 0;
}

int frame_line(Method *m, uint8_t *pc) {
    if (!m->code || !m->n_lines) return -1;
    uint32_t off = (uint32_t)(pc - 1 - m->code);
    int line = -1, best = -1;
    for (int i = 0; i < m->n_lines; i++)
        if (m->lines[i].pc <= off && (int)m->lines[i].pc > best) {
            best = m->lines[i].pc;
            line = m->lines[i].line;
        }
    return line;
}

/* ------------------------------------------------------------ boxing */
static const char *box_class_name(char t) {
    switch (t) {
        case 'Z': return "java/lang/Boolean";
        case 'B': return "java/lang/Byte";
        case 'C': return "java/lang/Character";
        case 'S': return "java/lang/Short";
        case 'I': return "java/lang/Integer";
        case 'J': return "java/lang/Long";
        case 'F': return "java/lang/Float";
        case 'D': return "java/lang/Double";
    }
    return NULL;
}

Object *box_value(char t, Slot v) {
    static Method *valueof[128];
    if (!valueof[(int)t]) {
        const char *cn = box_class_name(t);
        if (!cn) return NULL;
        Class *c = load_class(cn);
        if (!c) return NULL;
        char desc[32];
        snprintf(desc, sizeof desc, "(%c)L%s;", t, cn);
        valueof[(int)t] = find_method(c, intern("valueOf"), intern(desc));
        if (!valueof[(int)t] || init_class(c)) return NULL;
    }
    Slot a[2];
    a[0] = v;
    a[1].u = 0;
    if (run_method(valueof[(int)t], a, (t == 'J' || t == 'D') ? 2 : 1)) return NULL;
    return retval.l;
}

static char box_type(Class *c) {
    static Class *boxes[8];
    static const char types[8] = { 'Z', 'B', 'C', 'S', 'I', 'J', 'F', 'D' };
    for (int i = 0; i < 8; i++) {
        if (!boxes[i]) boxes[i] = find_loaded(intern(box_class_name(types[i])));
        if (boxes[i] == c) return types[i];
    }
    return 0;
}

static int is_intlike(char t) { return t == 'Z' || t == 'B' || t == 'C' || t == 'S' || t == 'I'; }

/* primitive conversion of a stack value from type f to type t */
static Slot widen(Slot v, char f, char t) {
    Slot r = v;
    if (is_intlike(f)) {
        if (t == 'J') r.j = v.i;
        else if (t == 'F') r.f = (jfloat)v.i;
        else if (t == 'D') r.d = (jdouble)v.i;
    } else if (f == 'J') {
        if (t == 'F') r.f = (jfloat)v.j;
        else if (t == 'D') r.d = (jdouble)v.j;
        else if (is_intlike(t)) r.i = (jint)v.j;
    } else if (f == 'F') {
        if (t == 'D') r.d = v.f;
    }
    return r;
}

int unbox_value(Object *o, char t, Slot *out) {
    if (!o) { throw_npe(); return -1; }
    char bt = box_type(o->cls);
    if (!bt) {
        char buf[200];
        java_name(o->cls->name, buf, sizeof buf);
        throw_new("java/lang/ClassCastException", "class %s cannot be unboxed", buf);
        return -1;
    }
    Field *f = find_field_by_name(o->cls, "value");
    Slot v = FIELDS(o)[f->slot];
    *out = widen(v, bt, t);
    return 0;
}

static char norm_type(char t) { return (t == 'L' || t == '[') ? 'L' : t; }

static const char *skip_type(const char *d) {
    while (*d == '[') d++;
    if (*d == 'L') while (*d && *d != ';') d++;
    return *d ? d + 1 : d;
}

static int convert(Slot v, char f, char t, Slot *out) {
    f = norm_type(f);
    t = norm_type(t);
    if (f == t || (f != 'L' && t != 'L' && is_intlike(f) && is_intlike(t))) { *out = v; return 0; }
    if (f == 'L' && t != 'L') return unbox_value(v.l, t, out);
    if (f != 'L' && t == 'L') {
        out->l = box_value(f, v);
        return out->l ? 0 : -1;
    }
    *out = widen(v, f, t);
    return 0;
}

/* ------------------------------------------------------------ lambdas */
struct LambdaInfo {
    int kind;                   /* method handle kind of the implementation */
    Method *impl;
    int n_cap;
    char cap_types[32];
};

static char ret_char(const char *desc) {
    const char *r = strchr(desc, ')');
    return r ? norm_type(r[1]) : 'V';
}

/* Rewrites a call of lambda method lm (receiver + interface arguments at
 * base) into a call of its implementation. Returns the method to call,
 * with its arguments at *nbase, and the return conversion in conv[0..2]. */
static Method *lambda_rewrite2(LambdaInfo *L, Method *impl, Method *target, Slot *in, char *it, int n,
                               Slot *base, Slot *out, Slot *targs, int k, char q, Method *lm,
                               Slot **nbase, char *conv);

static Method *lambda_rewrite(Method *lm, Slot *base, Slot **nbase, char *conv) {
    LambdaInfo *L = lm->lambda;
    Object *self = base[0].l;
    Slot in[80];
    char it[80];
    int n = 0;
    for (int i = 0; i < L->n_cap; i++) {
        in[n] = FIELDS(self)[i];
        it[n++] = L->cap_types[i];
    }
    const char *d = lm->desc + 1;
    Slot *a = base + 1;
    while (*d && *d != ')' && n < 78) {
        char t = norm_type(*d);
        it[n] = t;
        in[n++] = *a;
        a += (t == 'J' || t == 'D') ? 2 : 1;
        d = skip_type(d);
    }
    Method *impl = L->impl, *target = impl;
    Slot *out = base, *targs = base;
    int k = 0;
    if (base + 2 * n + 8 > stack_end) {
        throw_new("java/lang/StackOverflowError", NULL);
        return NULL;
    }
    conv[0] = 0;
    char q = ret_char(lm->desc);
    /* values written above the caller's stack top must be seen by the GC */
    Slot *saved_sp = depth ? frames[depth - 1].sp : NULL;
    if (depth && frames[depth - 1].sp < base + 2 * n + 4) frames[depth - 1].sp = base + 2 * n + 4;
    Method *res = lambda_rewrite2(L, impl, target, in, it, n, base, out, targs, k, q, lm, nbase, conv);
    if (depth) frames[depth - 1].sp = saved_sp;
    return res;
}

static Method *lambda_rewrite2(LambdaInfo *L, Method *impl, Method *target, Slot *in, char *it, int n,
                               Slot *base, Slot *out, Slot *targs, int k, char q, Method *lm,
                               Slot **nbase, char *conv) {
    if (L->kind == 8) {                         /* Foo::new */
        if (init_class(impl->cls)) return NULL;
        Object *o = alloc_object(impl->cls);
        if (!o) return NULL;
        if (q != 'V') { (out++)->l = o; targs = out; }
        (out++)->l = o;
    } else if (L->kind == 5 || L->kind == 9 || L->kind == 7) {
        Object *recv = n > 0 ? in[0].l : NULL;
        if (!recv) { throw_npe(); return NULL; }
        k = 1;
        (out++)->l = recv;
        if (L->kind != 7 && !(impl->access & (ACC_PRIVATE | ACC_FINAL))) {
            target = find_virtual(recv->cls, impl->name, impl->desc);
            if (!target) {
                throw_new("java/lang/AbstractMethodError", "%s.%s", recv->cls->name, impl->name);
                return NULL;
            }
        }
    } else {
        if (init_class(impl->cls)) return NULL;
    }
    const char *p = impl->desc + 1;
    while (*p && *p != ')') {
        char t = norm_type(*p);
        if (k >= n) {
            throw_new("java/lang/BootstrapMethodError", "lambda argument mismatch for %s", impl->name);
            return NULL;
        }
        Slot v;
        if (convert(in[k], it[k], t, &v)) return NULL;
        *out++ = v;
        if (t == 'J' || t == 'D') (out++)->u = 0;
        k++;
        p = skip_type(p);
    }
    if (L->kind != 8) {
        const char *rr = strchr(impl->desc, ')');
        char r = rr ? rr[1] : 'V';
        const char *qq = strchr(lm->desc, ')');
        char qf = qq ? qq[1] : 'V';
        if (qf == 'V' && r != 'V') conv[0] = 'V';
        else if (norm_type(qf) == 'L' && r != 'V' && norm_type(r) != 'L') { conv[0] = 'B'; conv[1] = r; }
        else if (norm_type(qf) != 'L' && qf != 'V' && norm_type(r) == 'L') { conv[0] = 'U'; conv[1] = qf; }
        else if (norm_type(qf) != 'L' && qf != 'V' && r != qf && !(is_intlike(r) && is_intlike(qf))) {
            conv[0] = 'W'; conv[1] = r; conv[2] = qf;
        }
    }
    *nbase = targs;
    return target;
}

/* apply a lambda return conversion to v (nslots in/out) */
static int apply_conv(char kind, char a, char b, Slot *v, int *nslots) {
    switch (kind) {
        case 'V': *nslots = 0; return 0;
        case 'B': {
            Object *o = box_value(a, *v);
            if (!o) return -1;
            v->l = o;
            *nslots = 1;
            return 0;
        }
        case 'U': {
            Slot r;
            if (unbox_value(v->l, a, &r)) return -1;
            *v = r;
            *nslots = (a == 'J' || a == 'D') ? 2 : 1;
            return 0;
        }
        case 'W':
            *v = widen(*v, a, b);
            *nslots = (b == 'J' || b == 'D') ? 2 : 1;
            return 0;
    }
    return 0;
}

/* ------------------------------------------------------------ invokedynamic */
enum { IND_CONCAT = 1, IND_LAMBDA, IND_RECORD, IND_TYPESWITCH, IND_ENUMSWITCH };

typedef struct Piece { char type; int16_t off; Object *lit; } Piece;

typedef struct CallSite {
    int kind;
    const char *desc;
    int arg_slots;
    int n;                      /* pieces / labels / record fields */
    Piece *pieces;
    Class *lcls;                /* lambda class */
    int record_op;              /* 0 toString, 1 hashCode, 2 equals */
    Field **rfields;
    Object *rname, *rnames;
    Object **labels;            /* switch labels: mirrors, interned strings or boxed Integers */
} CallSite;

static Method *concat_helper, *record_helper[3];

static const char *cp_utf(Class *c, int i) { return c->cp[i].utf8; }

static Object *bsm_string(Class *c, int idx) {
    if (c->cp_tag[idx] == CP_STRING) return resolve_string_ref(c, idx);
    return NULL;
}

/* string form of a primitive for string concatenation */
static Object *prim_string(char t, Slot v) {
    char buf[64];
    switch (t) {
        case 'Z': return new_string_utf8(v.i ? "true" : "false");
        case 'C': { jchar ch = (jchar)v.i; return new_string_chars(&ch, 1); }
        case 'J': snprintf(buf, sizeof buf, "%lld", (long long)v.j); break;
        case 'F': java_float_string(v.f, buf); break;
        case 'D': java_double_string(v.d, buf); break;
        default: snprintf(buf, sizeof buf, "%d", v.i);
    }
    return new_string_utf8(buf);
}

static CallSite *link_concat(Class *c, uint16_t *bsm, const char *desc) {
    CallSite *cs = xcalloc(1, sizeof(CallSite));
    cs->kind = IND_CONCAT;
    if (bsm[1] < 1) return NULL;
    const char *recipe = cp_utf(c, c->cp[bsm[2]].ref.a);
    /* argument offsets */
    char types[256];
    int offs[256], na = 0, off = 0;
    for (const char *d = desc + 1; *d && *d != ')' && na < 256; d = skip_type(d)) {
        types[na] = norm_type(*d);
        offs[na++] = off;
        off += (*d == 'J' || *d == 'D') ? 2 : 1;
    }
    cs->arg_slots = off;
    cs->pieces = xcalloc(strlen(recipe) + na + 2, sizeof(Piece));
    int ai = 0, ci = 3;
    char *lit = xmalloc(strlen(recipe) + 1);      /* literal text between arguments */
    size_t ll = 0;
    /* the recipe is modified UTF-8: \1 = argument, \2 = constant */
    for (const char *r = recipe;; r++) {
        if (!*r || *r == 1 || *r == 2) {
            if (ll) {
                lit[ll] = 0;
                Object *s = new_string_utf8(lit);
                if (!s) return NULL;
                Piece *p = &cs->pieces[cs->n++];
                p->type = '"';     /* a literal piece */
                p->lit = intern_string(s);
                ll = 0;
            }
            if (!*r) break;
            if (*r == 1 && ai < na) {
                Piece *p = &cs->pieces[cs->n++];
                p->type = types[ai];
                p->off = (int16_t)offs[ai++];
            } else if (*r == 2 && ci < bsm[1] + 2) {
                int k = bsm[ci++];
                Object *s = NULL;
                Slot v;
                switch (c->cp_tag[k]) {
                    case CP_STRING: s = resolve_string_ref(c, k); break;
                    case CP_INT: v.i = c->cp[k].i; s = prim_string('I', v); break;
                    case CP_FLOAT: v.f = c->cp[k].f; s = prim_string('F', v); break;
                    case CP_LONG: v.j = c->cp[k].j; s = prim_string('J', v); break;
                    case CP_DOUBLE: v.d = c->cp[k].d; s = prim_string('D', v); break;
                }
                if (s) {
                    Piece *p = &cs->pieces[cs->n++];
                    p->type = '"';
                    p->lit = intern_string(s);
                }
            } else if (*r != 1 && *r != 2) lit[ll++] = *r;
            continue;
        }
        lit[ll++] = *r;
    }
    free(lit);
    if (!concat_helper) {
        Class *h = load_class("java/lang/StringConcatHelper");
        if (!h) return NULL;
        concat_helper = find_method(h, intern("concat"), intern("([Ljava/lang/Object;)Ljava/lang/String;"));
        if (!concat_helper || init_class(h)) return NULL;
    }
    return cs;
}

static const char *mh_desc_of_type(Class *c, int idx) {     /* MethodType constant -> descriptor */
    return cp_utf(c, c->cp[idx].ref.a);
}

static CallSite *link_lambda(Class *c, uint16_t *bsm, const char *name, const char *desc, int alt) {
    int nargs = bsm[1];
    if (nargs < 3) return NULL;
    const char *sam = mh_desc_of_type(c, bsm[2]);
    int mh = bsm[3];
    int kind = c->cp[mh].ref.a, ref = c->cp[mh].ref.b;
    MethodRef *r = resolve_method_ref(c, ref);
    if (!r) return NULL;
    /* the interface comes from the call site's return type */
    const char *rt = strchr(desc, ')');
    if (!rt || rt[1] != 'L') return NULL;
    char iname[256];
    size_t il = strlen(rt + 2);
    if (il < 1 || il >= sizeof iname) return NULL;
    memcpy(iname, rt + 2, il - 1);
    iname[il - 1] = 0;
    Class *lc = define_lambda_class(c, iname);
    if (!lc) return NULL;
    const char *bridges[16];
    int nb = 0;
    if (alt && nargs >= 4) {
        int flags = c->cp[bsm[5]].i, at = 6;
        if (flags & 1) {                /* serializable: also a java.io.Serializable */
            Class *ser = load_class("java/io/Serializable");
            if (ser) {
                Class **ni = xcalloc(lc->n_ifaces + 2, sizeof(Class *));
                memcpy(ni, lc->ifaces, lc->n_ifaces * sizeof(Class *));
                ni[lc->n_ifaces++] = ser;
                lc->ifaces = ni;
            } else exception = NULL;
        }
        if (flags & 2) {                /* marker interfaces */
            int cnt = c->cp[bsm[at]].i;
            at++;
            Class **ni = xcalloc(lc->n_ifaces + cnt + 1, sizeof(Class *));
            memcpy(ni, lc->ifaces, lc->n_ifaces * sizeof(Class *));
            for (int i = 0; i < cnt && at < nargs + 2; i++, at++) {
                Class *m = resolve_class_ref(c, bsm[at]);
                if (m) ni[lc->n_ifaces++] = m;
                else exception = NULL;
            }
            lc->ifaces = ni;
        }
        if (flags & 4) {                /* bridges */
            int cnt = c->cp[bsm[at]].i;
            at++;
            for (int i = 0; i < cnt && at < nargs + 2 && nb < 16; i++, at++)
                bridges[nb++] = mh_desc_of_type(c, bsm[at]);
        }
    }
    LambdaInfo *L = xcalloc(1, sizeof(LambdaInfo));
    L->kind = kind;
    L->impl = r->m;
    for (const char *d = desc + 1; *d && *d != ')' && L->n_cap < 31; d = skip_type(d))
        L->cap_types[L->n_cap++] = norm_type(*d);
    lc->n_inst = (uint32_t)L->n_cap;
    lc->refmap = xcalloc(L->n_cap + 1, 1);
    for (int i = 0; i < L->n_cap; i++) lc->refmap[i] = L->cap_types[i] == 'L';
    lc->methods = xcalloc(nb + 2, sizeof(Method));
    const char *descs[17];
    int nd = 0;
    descs[nd++] = sam;
    for (int i = 0; i < nb; i++) if (intern(bridges[i]) != intern(sam)) descs[nd++] = bridges[i];
    for (int i = 0; i < nd; i++) {
        Method *m = &lc->methods[lc->n_methods++];
        m->cls = lc;
        m->name = name;
        m->desc = intern(descs[i]);
        m->access = ACC_PUBLIC;
        m->arg_slots = (uint16_t)(desc_arg_slots(m->desc) + 1);
        m->ret = desc_ret(m->desc);
        m->lambda = L;
    }
    CallSite *cs = xcalloc(1, sizeof(CallSite));
    cs->kind = IND_LAMBDA;
    cs->lcls = lc;
    cs->arg_slots = desc_arg_slots(desc);
    return cs;
}

static CallSite *link_record(Class *c, uint16_t *bsm, const char *name, const char *desc) {
    CallSite *cs = xcalloc(1, sizeof(CallSite));
    cs->kind = IND_RECORD;
    cs->arg_slots = desc_arg_slots(desc);
    cs->record_op = !strcmp(name, "toString") ? 0 : !strcmp(name, "hashCode") ? 1 : 2;
    int nargs = bsm[1];
    if (nargs < 2) return NULL;
    Class *rc = resolve_class_ref(c, bsm[2]);
    if (!rc) return NULL;
    char simple[256];
    java_name(rc->name, simple, sizeof simple);
    char *sn = strrchr(simple, '.');
    sn = sn ? sn + 1 : simple;
    char *dollar = strrchr(sn, '$');
    if (dollar) sn = dollar + 1;
    Object *s = new_string_utf8(sn);
    if (!s) return NULL;
    cs->rname = intern_string(s);
    cs->rnames = bsm_string(c, bsm[3]);
    if (!cs->rnames) return NULL;
    cs->n = nargs - 2;
    cs->rfields = xcalloc(cs->n + 1, sizeof(Field *));
    for (int i = 0; i < cs->n; i++) {
        int mh = bsm[4 + i];
        cs->rfields[i] = resolve_field_ref(c, c->cp[mh].ref.b);
        if (!cs->rfields[i]) return NULL;
    }
    if (!record_helper[0]) {
        Class *h = load_class("java/lang/Record");
        if (!h) return NULL;
        record_helper[0] = find_method(h, intern("toStringHelper"),
                                       intern("(Ljava/lang/String;Ljava/lang/String;[Ljava/lang/Object;)Ljava/lang/String;"));
        record_helper[1] = find_method(h, intern("hashCodeHelper"), intern("([Ljava/lang/Object;)I"));
        record_helper[2] = find_method(h, intern("equalsHelper"), intern("([Ljava/lang/Object;[Ljava/lang/Object;)Z"));
        if (!record_helper[0] || !record_helper[1] || !record_helper[2]) return NULL;
    }
    return cs;
}

static CallSite *link_switch(Class *c, uint16_t *bsm, const char *desc, int is_enum) {
    CallSite *cs = xcalloc(1, sizeof(CallSite));
    cs->kind = is_enum ? IND_ENUMSWITCH : IND_TYPESWITCH;
    cs->arg_slots = desc_arg_slots(desc);
    cs->n = bsm[1];
    cs->labels = xcalloc(cs->n + 1, sizeof(Object *));
    cs->pieces = xcalloc(cs->n + 1, sizeof(Piece));
    for (int i = 0; i < cs->n; i++) {
        int k = bsm[2 + i];
        Piece *p = &cs->pieces[i];
        switch (c->cp_tag[k]) {
            case CP_CLASS: {
                Class *lk = resolve_class_ref(c, k);
                if (!lk) return NULL;
                p->type = 'C';
                cs->labels[i] = class_mirror(lk);
                break;
            }
            case CP_STRING: p->type = 'S'; cs->labels[i] = resolve_string_ref(c, k); break;
            case CP_INT: p->type = 'I'; p->off = 0; p->lit = NULL; cs->labels[i] = NULL;
                         p->off = 0; cs->pieces[i].lit = (Object *)(intptr_t)c->cp[k].i; break;
            default: p->type = '?';
        }
    }
    return cs;
}

static CallSite *link_callsite(Class *c, int idx) {
    if (c->cp_res[idx]) return c->cp_res[idx];
    int b = c->cp[idx].ref.a, nat = c->cp[idx].ref.b;
    const char *name = cp_utf(c, c->cp[nat].ref.a), *desc = cp_utf(c, c->cp[nat].ref.b);
    if (b >= c->n_bsm) {
        throw_new("java/lang/BootstrapMethodError", "bad bootstrap method index");
        return NULL;
    }
    uint16_t *bsm = c->bsm[b];
    int mh = bsm[0];
    int mref = c->cp[mh].ref.b;
    int cidx = c->cp[mref].ref.a, mnat = c->cp[mref].ref.b;
    const char *bcls = cp_utf(c, c->cp[cidx].ref.a), *bname = cp_utf(c, c->cp[mnat].ref.a);
    CallSite *cs = NULL;
    if (!strcmp(bcls, "java/lang/invoke/StringConcatFactory") && !strcmp(bname, "makeConcatWithConstants"))
        cs = link_concat(c, bsm, desc);
    else if (!strcmp(bcls, "java/lang/invoke/LambdaMetafactory"))
        cs = link_lambda(c, bsm, name, desc, !strcmp(bname, "altMetafactory"));
    else if (!strcmp(bcls, "java/lang/runtime/ObjectMethods"))
        cs = link_record(c, bsm, name, desc);
    else if (!strcmp(bcls, "java/lang/runtime/SwitchBootstraps"))
        cs = link_switch(c, bsm, desc, !strcmp(bname, "enumSwitch"));
    else {
        char a[200];
        java_name(bcls, a, sizeof a);
        throw_new("java/lang/BootstrapMethodError", "invokedynamic %s.%s is not supported by MyOS Java", a, bname);
        return NULL;
    }
    if (!cs) {
        if (!exception) {
            char a[200];
            java_name(bcls, a, sizeof a);
            throw_new("java/lang/BootstrapMethodError", "can't link %s.%s for %s", a, bname, name);
        }
        return NULL;
    }
    cs->desc = desc;
    c->cp_res[idx] = cs;
    return cs;
}

/* objects referenced by call sites are interned strings or class mirrors (already roots) */
void gc_mark_extra(void (*markfn)(Object *));

static int switch_match(CallSite *cs, Object *o, int restart) {
    if (!o) return -1;
    for (int i = restart < 0 ? 0 : restart; i < cs->n; i++) {
        Piece *p = &cs->pieces[i];
        if (p->type == 'C') {
            Class *k = mirror_class(cs->labels[i]);
            if (k && is_subclass(o->cls, k)) return i;
        } else if (p->type == 'S') {
            Object *s = cs->labels[i];
            if (cs->kind == IND_ENUMSWITCH) {
                Field *nf = find_field_by_name(o->cls, "name");
                Object *nm = nf ? FIELDS(o)[nf->slot].l : NULL;
                if (nm && jstrlen(nm) == jstrlen(s) &&
                    !memcmp(jstrchars(nm), jstrchars(s), (size_t)jstrlen(s) * 2)) return i;
            } else if (o->cls == cls_string && jstrlen(o) == jstrlen(s) &&
                       !memcmp(jstrchars(o), jstrchars(s), (size_t)jstrlen(s) * 2)) return i;
        } else if (p->type == 'I') {
            char bt = box_type(o->cls);
            if (bt && bt != 'J' && bt != 'F' && bt != 'D' && bt != 'Z') {
                Field *f = find_field_by_name(o->cls, "value");
                if (FIELDS(o)[f->slot].i == (jint)(intptr_t)p->lit) return i;
            }
        }
    }
    return cs->n;
}

/* ------------------------------------------------------------ helpers */
static jint d2i(jdouble d) {
    if (d != d) return 0;
    if (d >= 2147483647.0) return INT32_MAX;
    if (d <= -2147483648.0) return INT32_MIN;
    return (jint)d;
}
static jlong d2l(jdouble d) {
    if (d != d) return 0;
    if (d >= 9223372036854775807.0) return INT64_MAX;
    if (d <= -9223372036854775808.0) return INT64_MIN;
    return (jlong)d;
}

static void npe_msg(const char *what, Class *c, int idx) {
    char buf[300] = "";
    if (c && idx) {
        int nat = c->cp[idx].ref.b;
        char cn[200];
        java_name(c->cp[c->cp[c->cp[idx].ref.a].ref.a].utf8, cn, sizeof cn);
        const char *simple = strrchr(cn, '.');
        simple = simple ? simple + 1 : cn;
        if (!strcmp(what, "invoke")) {
            snprintf(buf, sizeof buf, "Cannot invoke \"%s.%s()\"", simple, c->cp[c->cp[nat].ref.a].utf8);
        } else snprintf(buf, sizeof buf, "Cannot %s field \"%s\"", what, c->cp[c->cp[nat].ref.a].utf8);
    } else snprintf(buf, sizeof buf, "%s", what);
    throw_new("java/lang/NullPointerException", "%s", buf);
}

static void aioobe(jint i, jint len) {
    throw_new("java/lang/ArrayIndexOutOfBoundsException", "Index %d out of bounds for length %d", i, len);
}

static Object *multi_new(Class *ac, jint *counts, int dims) {
    Object *a = alloc_array(ac, counts[0]);
    if (!a || dims == 1 || ac->elem != 'L') return a;
    for (jint i = 0; i < counts[0]; i++) {
        Object *sub = multi_new(ac->component, counts + 1, dims - 1);
        if (!sub) return NULL;
        ADATA(a, Object *)[i] = sub;
    }
    return a;
}

static void class_cast(Class *from, Class *to) {
    char a[200], b[200];
    java_name(from->name, a, sizeof a);
    java_name(to->name, b, sizeof b);
    throw_new("java/lang/ClassCastException", "class %s cannot be cast to class %s", a, b);
}

/* ------------------------------------------------------------ the interpreter */
int interp(int base) {
    Frame *fr;
    Method *m;
    uint8_t *pc;
    Slot *sp, *locals;
    Class *cls;
    Method *t;
    Slot *args;
    int len;

#define SYNC() (fr->pc = pc + 1, fr->sp = sp)
    uint8_t op;
#if (defined(__GNUC__) || defined(__clang__)) && !defined(JVM_NO_CGOTO)
    /* the simple handlers jump straight to the next one */
    static void *const dispatch[256] = {
        &&op_00, &&op_01, &&op_02, &&op_03, &&op_04, &&op_05, &&op_06, &&op_07,
        &&op_08, &&op_09, &&op_0a, &&op_0b, &&op_0c, &&op_0d, &&op_0e, &&op_0f,
        &&op_10, &&op_11, &&op_12, &&op_13, &&op_14, &&op_15, &&op_16, &&op_17,
        &&op_18, &&op_19, &&op_1a, &&op_1b, &&op_1c, &&op_1d, &&op_1e, &&op_1f,
        &&op_20, &&op_21, &&op_22, &&op_23, &&op_24, &&op_25, &&op_26, &&op_27,
        &&op_28, &&op_29, &&op_2a, &&op_2b, &&op_2c, &&op_2d, &&op_2e, &&op_2f,
        &&op_30, &&op_31, &&op_32, &&op_33, &&op_34, &&op_35, &&op_36, &&op_37,
        &&op_38, &&op_39, &&op_3a, &&op_3b, &&op_3c, &&op_3d, &&op_3e, &&op_3f,
        &&op_40, &&op_41, &&op_42, &&op_43, &&op_44, &&op_45, &&op_46, &&op_47,
        &&op_48, &&op_49, &&op_4a, &&op_4b, &&op_4c, &&op_4d, &&op_4e, &&op_4f,
        &&op_50, &&op_51, &&op_52, &&op_53, &&op_54, &&op_55, &&op_56, &&op_57,
        &&op_58, &&op_59, &&op_5a, &&op_5b, &&op_5c, &&op_5d, &&op_5e, &&op_5f,
        &&op_60, &&op_61, &&op_62, &&op_63, &&op_64, &&op_65, &&op_66, &&op_67,
        &&op_68, &&op_69, &&op_6a, &&op_6b, &&op_6c, &&op_6d, &&op_6e, &&op_6f,
        &&op_70, &&op_71, &&op_72, &&op_73, &&op_74, &&op_75, &&op_76, &&op_77,
        &&op_78, &&op_79, &&op_7a, &&op_7b, &&op_7c, &&op_7d, &&op_7e, &&op_7f,
        &&op_80, &&op_81, &&op_82, &&op_83, &&op_84, &&op_85, &&op_86, &&op_87,
        &&op_88, &&op_89, &&op_8a, &&op_8b, &&op_8c, &&op_8d, &&op_8e, &&op_8f,
        &&op_90, &&op_91, &&op_92, &&op_93, &&op_94, &&op_95, &&op_96, &&op_97,
        &&op_98, &&op_99, &&op_9a, &&op_9b, &&op_9c, &&op_9d, &&op_9e, &&op_9f,
        &&op_a0, &&op_a1, &&op_a2, &&op_a3, &&op_a4, &&op_a5, &&op_a6, &&op_a7,
        &&op_a8, &&op_a9, &&op_aa, &&op_ab, &&op_ac, &&op_ad, &&op_ae, &&op_af,
        &&op_b0, &&op_b1, &&op_b2, &&op_b3, &&op_b4, &&op_b5, &&op_b6, &&op_b7,
        &&op_b8, &&op_b9, &&op_ba, &&op_bb, &&op_bc, &&op_bd, &&op_be, &&op_bf,
        &&op_c0, &&op_c1, &&op_c2, &&op_c3, &&op_c4, &&op_c5, &&op_c6, &&op_c7,
        &&op_c8, &&op_c9, &&op_default, &&op_default, &&op_default, &&op_default, &&op_default, &&op_default,
        &&op_default, &&op_default, &&op_default, &&op_default, &&op_default, &&op_default, &&op_default, &&op_default,
        &&op_default, &&op_default, &&op_default, &&op_default, &&op_default, &&op_default, &&op_default, &&op_default,
        &&op_default, &&op_default, &&op_default, &&op_default, &&op_default, &&op_default, &&op_default, &&op_default,
        &&op_default, &&op_default, &&op_default, &&op_default, &&op_default, &&op_default, &&op_default, &&op_default,
        &&op_default, &&op_default, &&op_default, &&op_default, &&op_default, &&op_default, &&op_default, &&op_default,
        &&op_default, &&op_default, &&op_default, &&op_default, &&op_default, &&op_default, &&op_default, &&op_default,
    };
#define NEXT do { op = *pc; goto *dispatch[op]; } while (0)
#else
#define NEXT break
#endif
#define THROW() goto handle_exception
#define NULLCHK(o, what) do { if (!(o)) { SYNC(); npe_msg(what, NULL, 0); THROW(); } } while (0)

reload:
    fr = &frames[depth - 1];
    m = fr->m;
    pc = fr->pc;
    sp = fr->sp;
    locals = fr->locals;
    cls = m->cls;

    for (;;) {
        op = *pc;
        switch (op) {
        case 0x00: op_00: pc++; NEXT;
        case 0x01: op_01: (sp++)->l = NULL; pc++; NEXT;
        case 0x02: case 0x03: case 0x04: case 0x05: case 0x06: case 0x07: case 0x08: op_02: op_03: op_04: op_05: op_06: op_07: op_08:
            (sp++)->i = op - 3; pc++; NEXT;
        case 0x09: case 0x0a: op_09: op_0a: sp->j = op - 9; sp += 2; pc++; NEXT;
        case 0x0b: case 0x0c: case 0x0d: op_0b: op_0c: op_0d: (sp++)->f = (jfloat)(op - 0x0b); pc++; NEXT;
        case 0x0e: case 0x0f: op_0e: op_0f: sp->d = op - 0x0e; sp += 2; pc++; NEXT;
        case 0x10: op_10: (sp++)->i = (int8_t)pc[1]; pc += 2; NEXT;
        case 0x11: op_11: (sp++)->i = S2(pc + 1); pc += 3; NEXT;
        case 0x12: case 0x13: op_12: op_13: {
            int idx = op == 0x12 ? pc[1] : U2(pc + 1);
            switch (cls->cp_tag[idx]) {
                case CP_INT: case CP_FLOAT: (sp++)->i = cls->cp[idx].i; break;
                case CP_STRING: {
                    Object *s = cls->cp_res[idx];
                    if (!s) { SYNC(); s = resolve_string_ref(cls, idx); if (!s) THROW(); }
                    (sp++)->l = s;
                    break;
                }
                case CP_CLASS: {
                    SYNC();
                    Class *k = resolve_class_ref(cls, idx);
                    if (!k) THROW();
                    Object *mi = class_mirror(k);
                    if (!mi) THROW();
                    (sp++)->l = mi;
                    break;
                }
                default:
                    SYNC();
                    throw_new("java/lang/UnsupportedOperationException", "ldc of constant type %d is not supported by MyOS Java", cls->cp_tag[idx]);
                    THROW();
            }
            pc += op == 0x12 ? 2 : 3;
            break;
        }
        case 0x14: op_14: sp->j = cls->cp[U2(pc + 1)].j; sp += 2; pc += 3; NEXT;
        case 0x15: case 0x17: case 0x19: op_15: op_17: op_19: *sp++ = locals[pc[1]]; pc += 2; NEXT;
        case 0x16: case 0x18: op_16: op_18: sp[0] = locals[pc[1]]; sp[1].u = 0; sp += 2; pc += 2; NEXT;
        case 0x1a: case 0x1b: case 0x1c: case 0x1d: op_1a: op_1b: op_1c: op_1d: *sp++ = locals[op - 0x1a]; pc++; NEXT;
        case 0x1e: case 0x1f: case 0x20: case 0x21: op_1e: op_1f: op_20: op_21: sp[0] = locals[op - 0x1e]; sp[1].u = 0; sp += 2; pc++; NEXT;
        case 0x22: case 0x23: case 0x24: case 0x25: op_22: op_23: op_24: op_25: *sp++ = locals[op - 0x22]; pc++; NEXT;
        case 0x26: case 0x27: case 0x28: case 0x29: op_26: op_27: op_28: op_29: sp[0] = locals[op - 0x26]; sp[1].u = 0; sp += 2; pc++; NEXT;
        case 0x2a: case 0x2b: case 0x2c: case 0x2d: op_2a: op_2b: op_2c: op_2d: *sp++ = locals[op - 0x2a]; pc++; NEXT;

        /* array loads */
        case 0x2e: case 0x2f: case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: case 0x35: op_2e: op_2f: op_30: op_31: op_32: op_33: op_34: op_35: {
            jint i = sp[-1].i;
            Object *a = sp[-2].l;
            if (!a) {
                static const char *nm[] = { "int", "long", "float", "double", "object", "byte/boolean", "char", "short" };
                char msg[64];
                snprintf(msg, sizeof msg, "Cannot load from %s array", nm[op - 0x2e]);
                SYNC(); npe_msg(msg, NULL, 0); THROW();
            }
            if ((uint32_t)i >= (uint32_t)a->len) { SYNC(); aioobe(i, a->len); THROW(); }
            sp -= 2;
            switch (op) {
                case 0x2e: (sp++)->i = ADATA(a, jint)[i]; break;
                case 0x2f: sp->j = ADATA(a, jlong)[i]; sp[1].u = 0; sp += 2; break;
                case 0x30: (sp++)->f = ADATA(a, jfloat)[i]; break;
                case 0x31: sp->d = ADATA(a, jdouble)[i]; sp[1].u = 0; sp += 2; break;
                case 0x32: (sp++)->l = ADATA(a, Object *)[i]; break;
                case 0x33: (sp++)->i = ADATA(a, jbyte)[i]; break;
                case 0x34: (sp++)->i = ADATA(a, jchar)[i]; break;
                case 0x35: (sp++)->i = ADATA(a, jshort)[i]; break;
            }
            pc++;
            NEXT;
        }
        case 0x36: case 0x38: case 0x3a: op_36: op_38: op_3a: locals[pc[1]] = *--sp; pc += 2; NEXT;
        case 0x37: case 0x39: op_37: op_39: sp -= 2; locals[pc[1]] = sp[0]; locals[pc[1] + 1].u = 0; pc += 2; NEXT;
        case 0x3b: case 0x3c: case 0x3d: case 0x3e: op_3b: op_3c: op_3d: op_3e: locals[op - 0x3b] = *--sp; pc++; NEXT;
        case 0x3f: case 0x40: case 0x41: case 0x42: op_3f: op_40: op_41: op_42: sp -= 2; locals[op - 0x3f] = sp[0]; pc++; NEXT;
        case 0x43: case 0x44: case 0x45: case 0x46: op_43: op_44: op_45: op_46: locals[op - 0x43] = *--sp; pc++; NEXT;
        case 0x47: case 0x48: case 0x49: case 0x4a: op_47: op_48: op_49: op_4a: sp -= 2; locals[op - 0x47] = sp[0]; pc++; NEXT;
        case 0x4b: case 0x4c: case 0x4d: case 0x4e: op_4b: op_4c: op_4d: op_4e: locals[op - 0x4b] = *--sp; pc++; NEXT;

        /* array stores */
        case 0x4f: case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: op_4f: op_50: op_51: op_52: op_53: op_54: op_55: op_56: {
            int wide = op == 0x50 || op == 0x52;
            Slot v = sp[-1 - wide];
            jint i = sp[-2 - wide].i;
            Object *a = sp[-3 - wide].l;
            if (!a) {
                static const char *nm[] = { "int", "long", "float", "double", "object", "byte/boolean", "char", "short" };
                char msg[64];
                snprintf(msg, sizeof msg, "Cannot store to %s array", nm[op - 0x4f]);
                SYNC(); npe_msg(msg, NULL, 0); THROW();
            }
            if ((uint32_t)i >= (uint32_t)a->len) { SYNC(); aioobe(i, a->len); THROW(); }
            switch (op) {
                case 0x4f: ADATA(a, jint)[i] = v.i; break;
                case 0x50: ADATA(a, jlong)[i] = v.j; break;
                case 0x51: ADATA(a, jfloat)[i] = v.f; break;
                case 0x52: ADATA(a, jdouble)[i] = v.d; break;
                case 0x53:
                    if (v.l && a->cls->component != cls_object && !is_subclass(v.l->cls, a->cls->component)) {
                        char b[200];
                        java_name(v.l->cls->name, b, sizeof b);
                        SYNC();
                        throw_new("java/lang/ArrayStoreException", "%s", b);
                        THROW();
                    }
                    ADATA(a, Object *)[i] = v.l;
                    break;
                case 0x54:
                    ADATA(a, jbyte)[i] = (jbyte)(a->cls->elem == 'Z' ? (v.i & 1) : v.i);
                    break;
                case 0x55: ADATA(a, jchar)[i] = (jchar)v.i; break;
                case 0x56: ADATA(a, jshort)[i] = (jshort)v.i; break;
            }
            sp -= 3 + wide;
            pc++;
            NEXT;
        }

        /* stack */
        case 0x57: op_57: sp--; pc++; NEXT;
        case 0x58: op_58: sp -= 2; pc++; NEXT;
        case 0x59: op_59: sp[0] = sp[-1]; sp++; pc++; NEXT;
        case 0x5a: op_5a: { Slot a = sp[-1], b = sp[-2]; sp[-2] = a; sp[-1] = b; sp[0] = a; sp++; pc++; break; }
        case 0x5b: op_5b: { Slot a = sp[-1], b = sp[-2], c = sp[-3];
                     sp[-3] = a; sp[-2] = c; sp[-1] = b; sp[0] = a; sp++; pc++; break; }
        case 0x5c: op_5c: sp[0] = sp[-2]; sp[1] = sp[-1]; sp += 2; pc++; NEXT;
        case 0x5d: op_5d: { Slot a = sp[-1], b = sp[-2], c = sp[-3];
                     sp[-3] = b; sp[-2] = a; sp[-1] = c; sp[0] = b; sp[1] = a; sp += 2; pc++; break; }
        case 0x5e: op_5e: { Slot a = sp[-1], b = sp[-2], c = sp[-3], d = sp[-4];
                     sp[-4] = b; sp[-3] = a; sp[-2] = d; sp[-1] = c; sp[0] = b; sp[1] = a; sp += 2; pc++; break; }
        case 0x5f: op_5f: { Slot a = sp[-1]; sp[-1] = sp[-2]; sp[-2] = a; pc++; break; }

        /* arithmetic */
        case 0x60: op_60: sp[-2].i = (jint)((uint32_t)sp[-2].i + (uint32_t)sp[-1].i); sp--; pc++; NEXT;
        case 0x61: op_61: sp[-4].j = (jlong)((uint64_t)sp[-4].j + (uint64_t)sp[-2].j); sp -= 2; pc++; NEXT;
        case 0x62: op_62: sp[-2].f = sp[-2].f + sp[-1].f; sp--; pc++; NEXT;
        case 0x63: op_63: sp[-4].d = sp[-4].d + sp[-2].d; sp -= 2; pc++; NEXT;
        case 0x64: op_64: sp[-2].i = (jint)((uint32_t)sp[-2].i - (uint32_t)sp[-1].i); sp--; pc++; NEXT;
        case 0x65: op_65: sp[-4].j = (jlong)((uint64_t)sp[-4].j - (uint64_t)sp[-2].j); sp -= 2; pc++; NEXT;
        case 0x66: op_66: sp[-2].f = sp[-2].f - sp[-1].f; sp--; pc++; NEXT;
        case 0x67: op_67: sp[-4].d = sp[-4].d - sp[-2].d; sp -= 2; pc++; NEXT;
        case 0x68: op_68: sp[-2].i = (jint)((uint32_t)sp[-2].i * (uint32_t)sp[-1].i); sp--; pc++; NEXT;
        case 0x69: op_69: sp[-4].j = (jlong)((uint64_t)sp[-4].j * (uint64_t)sp[-2].j); sp -= 2; pc++; NEXT;
        case 0x6a: op_6a: sp[-2].f = sp[-2].f * sp[-1].f; sp--; pc++; NEXT;
        case 0x6b: op_6b: sp[-4].d = sp[-4].d * sp[-2].d; sp -= 2; pc++; NEXT;
        case 0x6c: case 0x70: op_6c: op_70: {
            jint b = sp[-1].i, a = sp[-2].i;
            if (b == 0) { SYNC(); throw_new("java/lang/ArithmeticException", "/ by zero"); THROW(); }
            if (b == -1) sp[-2].i = op == 0x6c ? (jint)(0u - (uint32_t)a) : 0;
            else sp[-2].i = op == 0x6c ? a / b : a % b;
            sp--; pc++;
            NEXT;
        }
        case 0x6d: case 0x71: op_6d: op_71: {
            jlong b = sp[-2].j, a = sp[-4].j;
            if (b == 0) { SYNC(); throw_new("java/lang/ArithmeticException", "/ by zero"); THROW(); }
            if (b == -1) sp[-4].j = op == 0x6d ? (jlong)(0ull - (uint64_t)a) : 0;
            else sp[-4].j = op == 0x6d ? a / b : a % b;
            sp -= 2; pc++;
            NEXT;
        }
        case 0x6e: op_6e: sp[-2].f = sp[-2].f / sp[-1].f; sp--; pc++; NEXT;
        case 0x6f: op_6f: sp[-4].d = sp[-4].d / sp[-2].d; sp -= 2; pc++; NEXT;
        case 0x72: op_72: sp[-2].f = (jfloat)fmod(sp[-2].f, sp[-1].f); sp--; pc++; NEXT;
        case 0x73: op_73: sp[-4].d = fmod(sp[-4].d, sp[-2].d); sp -= 2; pc++; NEXT;
        case 0x74: op_74: sp[-1].i = (jint)(0u - (uint32_t)sp[-1].i); pc++; NEXT;
        case 0x75: op_75: sp[-2].j = (jlong)(0ull - (uint64_t)sp[-2].j); pc++; NEXT;
        case 0x76: op_76: sp[-1].f = -sp[-1].f; pc++; NEXT;
        case 0x77: op_77: sp[-2].d = -sp[-2].d; pc++; NEXT;
        case 0x78: op_78: sp[-2].i = (jint)((uint32_t)sp[-2].i << (sp[-1].i & 31)); sp--; pc++; NEXT;
        case 0x79: op_79: sp[-3].j = (jlong)((uint64_t)sp[-3].j << (sp[-1].i & 63)); sp--; pc++; NEXT;
        case 0x7a: op_7a: sp[-2].i = sp[-2].i >> (sp[-1].i & 31); sp--; pc++; NEXT;
        case 0x7b: op_7b: sp[-3].j = sp[-3].j >> (sp[-1].i & 63); sp--; pc++; NEXT;
        case 0x7c: op_7c: sp[-2].i = (jint)((uint32_t)sp[-2].i >> (sp[-1].i & 31)); sp--; pc++; NEXT;
        case 0x7d: op_7d: sp[-3].j = (jlong)((uint64_t)sp[-3].j >> (sp[-1].i & 63)); sp--; pc++; NEXT;
        case 0x7e: op_7e: sp[-2].i &= sp[-1].i; sp--; pc++; NEXT;
        case 0x7f: op_7f: sp[-4].j &= sp[-2].j; sp -= 2; pc++; NEXT;
        case 0x80: op_80: sp[-2].i |= sp[-1].i; sp--; pc++; NEXT;
        case 0x81: op_81: sp[-4].j |= sp[-2].j; sp -= 2; pc++; NEXT;
        case 0x82: op_82: sp[-2].i ^= sp[-1].i; sp--; pc++; NEXT;
        case 0x83: op_83: sp[-4].j ^= sp[-2].j; sp -= 2; pc++; NEXT;
        case 0x84: op_84: locals[pc[1]].i = (jint)((uint32_t)locals[pc[1]].i + (uint32_t)(int8_t)pc[2]); pc += 3; NEXT;

        /* conversions */
        case 0x85: op_85: sp[-1].j = sp[-1].i; sp[0].u = 0; sp++; pc++; NEXT;
        case 0x86: op_86: sp[-1].f = (jfloat)sp[-1].i; pc++; NEXT;
        case 0x87: op_87: sp[-1].d = (jdouble)sp[-1].i; sp[0].u = 0; sp++; pc++; NEXT;
        case 0x88: op_88: sp[-2].i = (jint)sp[-2].j; sp--; pc++; NEXT;
        case 0x89: op_89: sp[-2].f = (jfloat)sp[-2].j; sp--; pc++; NEXT;
        case 0x8a: op_8a: sp[-2].d = (jdouble)sp[-2].j; pc++; NEXT;
        case 0x8b: op_8b: sp[-1].i = d2i(sp[-1].f); pc++; NEXT;
        case 0x8c: op_8c: sp[-1].j = d2l(sp[-1].f); sp[0].u = 0; sp++; pc++; NEXT;
        case 0x8d: op_8d: sp[-1].d = sp[-1].f; sp[0].u = 0; sp++; pc++; NEXT;
        case 0x8e: op_8e: sp[-2].i = d2i(sp[-2].d); sp--; pc++; NEXT;
        case 0x8f: op_8f: sp[-2].j = d2l(sp[-2].d); pc++; NEXT;
        case 0x90: op_90: sp[-2].f = (jfloat)sp[-2].d; sp--; pc++; NEXT;
        case 0x91: op_91: sp[-1].i = (jbyte)sp[-1].i; pc++; NEXT;
        case 0x92: op_92: sp[-1].i = (jchar)sp[-1].i; pc++; NEXT;
        case 0x93: op_93: sp[-1].i = (jshort)sp[-1].i; pc++; NEXT;

        /* comparisons */
        case 0x94: op_94: { jlong a = sp[-4].j, b = sp[-2].j; sp -= 3; sp[-1].i = a > b ? 1 : a == b ? 0 : -1; pc++; break; }
        case 0x95: case 0x96: op_95: op_96: {
            jfloat a = sp[-2].f, b = sp[-1].f;
            sp--;
            sp[-1].i = a > b ? 1 : a == b ? 0 : a < b ? -1 : (op == 0x95 ? -1 : 1);
            pc++;
            break;
        }
        case 0x97: case 0x98: op_97: op_98: {
            jdouble a = sp[-4].d, b = sp[-2].d;
            sp -= 3;
            sp[-1].i = a > b ? 1 : a == b ? 0 : a < b ? -1 : (op == 0x97 ? -1 : 1);
            pc++;
            break;
        }

        /* branches */
        case 0x99: op_99: pc += (--sp)->i == 0 ? S2(pc + 1) : 3; NEXT;
        case 0x9a: op_9a: pc += (--sp)->i != 0 ? S2(pc + 1) : 3; NEXT;
        case 0x9b: op_9b: pc += (--sp)->i < 0 ? S2(pc + 1) : 3; NEXT;
        case 0x9c: op_9c: pc += (--sp)->i >= 0 ? S2(pc + 1) : 3; NEXT;
        case 0x9d: op_9d: pc += (--sp)->i > 0 ? S2(pc + 1) : 3; NEXT;
        case 0x9e: op_9e: pc += (--sp)->i <= 0 ? S2(pc + 1) : 3; NEXT;
        case 0x9f: op_9f: sp -= 2; pc += sp[0].i == sp[1].i ? S2(pc + 1) : 3; NEXT;
        case 0xa0: op_a0: sp -= 2; pc += sp[0].i != sp[1].i ? S2(pc + 1) : 3; NEXT;
        case 0xa1: op_a1: sp -= 2; pc += sp[0].i < sp[1].i ? S2(pc + 1) : 3; NEXT;
        case 0xa2: op_a2: sp -= 2; pc += sp[0].i >= sp[1].i ? S2(pc + 1) : 3; NEXT;
        case 0xa3: op_a3: sp -= 2; pc += sp[0].i > sp[1].i ? S2(pc + 1) : 3; NEXT;
        case 0xa4: op_a4: sp -= 2; pc += sp[0].i <= sp[1].i ? S2(pc + 1) : 3; NEXT;
        case 0xa5: op_a5: sp -= 2; pc += sp[0].l == sp[1].l ? S2(pc + 1) : 3; NEXT;
        case 0xa6: op_a6: sp -= 2; pc += sp[0].l != sp[1].l ? S2(pc + 1) : 3; NEXT;
        case 0xa7: op_a7: pc += S2(pc + 1); NEXT;
        case 0xa8: op_a8: (sp++)->i = (jint)(pc + 3 - m->code); pc += S2(pc + 1); NEXT;
        case 0xa9: op_a9: pc = m->code + locals[pc[1]].i; NEXT;
        case 0xaa: op_aa: {
            uint32_t a = ((uint32_t)(pc - m->code) + 4) & ~3u;
            uint8_t *p = m->code + a;
            jint def = S4(p), lo = S4(p + 4), hi = S4(p + 8), i = (--sp)->i;
            pc += (i < lo || i > hi) ? def : S4(p + 12 + (uint32_t)(i - lo) * 4);
            break;
        }
        case 0xab: op_ab: {
            uint32_t a = ((uint32_t)(pc - m->code) + 4) & ~3u;
            uint8_t *p = m->code + a;
            jint def = S4(p), n = S4(p + 4), key = (--sp)->i, lo = 0, hi = n - 1, off = def;
            while (lo <= hi) {
                jint mid = (lo + hi) / 2, k = S4(p + 8 + mid * 8);
                if (k == key) { off = S4(p + 12 + mid * 8); break; }
                if (k < key) lo = mid + 1; else hi = mid - 1;
            }
            pc += off;
            break;
        }

        /* returns */
        case 0xac: case 0xae: case 0xb0: case 0xad: case 0xaf: case 0xb1: op_ac: op_ae: op_b0: op_ad: op_af: op_b1: {
            Slot v;
            int n;
            if (op == 0xb1) { v.u = 0; n = 0; }
            else if (op == 0xad || op == 0xaf) { v = sp[-2]; n = 2; }
            else { v = sp[-1]; n = 1; }
            Slot *dst = fr->locals;
            char conv = fr->ret_conv, ca = fr->ret_box, cb = fr->ret_box2;
            depth--;
            if (conv) {
                if (apply_conv(conv, ca, cb, &v, &n)) {
                    /* the exception belongs to the caller */
                    if (depth == base) return -1;
                    goto handle_exception_reload;
                }
            }
            if (depth == base) { retval = v; return 0; }
            fr = &frames[depth - 1];
            m = fr->m;
            cls = m->cls;
            pc = fr->pc;
            locals = fr->locals;
            sp = dst;
            if (n >= 1) *sp++ = v;
            if (n == 2) (sp++)->u = 0;
            NEXT;
        }

        /* fields */
        case 0xb2: case 0xb3: op_b2: op_b3: {
            int idx = U2(pc + 1);
            Field *f = cls->cp_res[idx];
            if (!f) { SYNC(); f = resolve_field_ref(cls, idx); if (!f) THROW(); }
            Class *fc = f->cls;
            if (fc->state != CS_INITED) { SYNC(); if (init_class(fc)) THROW(); }
            Slot *s = &fc->statics[f->slot];
            char d = f->desc[0];
            if (op == 0xb2) {
                *sp++ = *s;
                if (d == 'J' || d == 'D') (sp++)->u = 0;
            } else {
                if (d == 'J' || d == 'D') { sp -= 2; *s = sp[0]; }
                else { *s = *--sp; }
            }
            pc += 3;
            NEXT;
        }
        case 0xb4: op_b4: {
            int idx = U2(pc + 1);
            Field *f = cls->cp_res[idx];
            if (!f) { SYNC(); f = resolve_field_ref(cls, idx); if (!f) THROW(); }
            Object *o = sp[-1].l;
            if (!o) { SYNC(); npe_msg("read", cls, idx); THROW(); }
            char d = f->desc[0];
            sp[-1] = FIELDS(o)[f->slot];
            if (d == 'J' || d == 'D') (sp++)->u = 0;
            pc += 3;
            NEXT;
        }
        case 0xb5: op_b5: {
            int idx = U2(pc + 1);
            Field *f = cls->cp_res[idx];
            if (!f) { SYNC(); f = resolve_field_ref(cls, idx); if (!f) THROW(); }
            char d = f->desc[0];
            int w = (d == 'J' || d == 'D') ? 2 : 1;
            Object *o = sp[-1 - w].l;
            if (!o) { SYNC(); npe_msg("assign", cls, idx); THROW(); }
            Slot v = sp[-w];
            if (d == 'Z') v.i &= 1;
            else if (d == 'B') v.i = (jbyte)v.i;
            else if (d == 'C') v.i = (jchar)v.i;
            else if (d == 'S') v.i = (jshort)v.i;
            FIELDS(o)[f->slot] = v;
            sp -= 1 + w;
            pc += 3;
            NEXT;
        }

        /* calls */
        case 0xb6: case 0xb7: case 0xb8: case 0xb9: op_b6: op_b7: op_b8: op_b9: {
            int idx = U2(pc + 1);
            len = op == 0xb9 ? 5 : 3;
            MethodRef *r = cls->cp_res[idx];
            if (!r) { SYNC(); r = resolve_method_ref(cls, idx); if (!r) THROW(); }
            t = r->m;
            if (op == 0xb8) {
                if (t->cls->state != CS_INITED) { SYNC(); if (init_class(t->cls)) THROW(); }
                args = sp - t->arg_slots;
            } else {
                args = sp - t->arg_slots;
                Object *recv = args[0].l;
                if (!recv) { SYNC(); npe_msg("invoke", cls, idx); THROW(); }
                if (op != 0xb7 && !(t->access & (ACC_PRIVATE | ACC_FINAL))) {
                    Class *rc = recv->cls;
                    if (r->ic_cls == rc) t = r->ic_m;
                    else {
                        Method *v = find_virtual(rc, t->name, t->desc);
                        if (!v || (v->access & ACC_ABSTRACT)) {
                            char a[200];
                            java_name(rc->name, a, sizeof a);
                            SYNC();
                            throw_new("java/lang/AbstractMethodError", "%s.%s%s", a, t->name, t->desc);
                            THROW();
                        }
                        r->ic_cls = rc;
                        r->ic_m = v;
                        t = v;
                    }
                }
            }
            goto do_invoke;
        }
        case 0xba: op_ba: {
            int idx = U2(pc + 1);
            CallSite *cs = cls->cp_res[idx];
            SYNC();
            if (!cs && !(cs = link_callsite(cls, idx))) THROW();
            len = 5;
            args = sp - cs->arg_slots;
            switch (cs->kind) {
                case IND_CONCAT: {
                    Object *arr = alloc_array(array_class_of(cls_object), cs->n);
                    if (!arr) THROW();
                    for (int i = 0; i < cs->n; i++) {
                        Piece *p = &cs->pieces[i];
                        Object *v;
                        if (p->type == '"') v = p->lit;
                        else if (p->type == 'L') v = args[p->off].l;
                        else if (!(v = prim_string(p->type, args[p->off]))) THROW();
                        ADATA(arr, Object *)[i] = v;
                    }
                    sp = args;
                    (sp++)->l = arr;
                    t = concat_helper;
                    args = sp - 1;
                    goto do_invoke;
                }
                case IND_LAMBDA: {
                    Class *lc = cs->lcls;
                    Object *o;
                    if (cs->arg_slots == 0 && lc->lambda_single) o = lc->lambda_single;
                    else {
                        o = alloc_object(lc);
                        if (!o) THROW();
                        Slot *a = args;
                        LambdaInfo *L = lc->methods[0].lambda;
                        for (int i = 0; i < L->n_cap; i++) {
                            FIELDS(o)[i] = *a;
                            a += (L->cap_types[i] == 'J' || L->cap_types[i] == 'D') ? 2 : 1;
                        }
                        if (cs->arg_slots == 0) lc->lambda_single = o;
                    }
                    sp = args;
                    (sp++)->l = o;
                    pc += 5;
                    break;
                }
                case IND_RECORD: {
                    Object *rec = args[0].l;
                    if (!rec) { npe_msg("invoke", NULL, 0); THROW(); }
                    int nv = cs->record_op == 2 ? 2 : 1;
                    Object *arrs[2] = { NULL, NULL };
                    for (int k = 0; k < nv; k++) {
                        Object *src = args[k].l;
                        if (k == 1 && (!src || src->cls != rec->cls)) {
                            sp = args;
                            (sp++)->i = 0;
                            pc += 5;
                            goto next_insn;
                        }
                        arrs[k] = alloc_array(array_class_of(cls_object), cs->n);
                        if (!arrs[k]) THROW();
                        for (int i = 0; i < cs->n; i++) {
                            Field *f = cs->rfields[i];
                            Slot v = FIELDS(src)[f->slot];
                            Object *bv;
                            char d = f->desc[0];
                            if (d == 'L' || d == '[') bv = v.l;
                            else if (!(bv = box_value(d, v))) THROW();
                            ADATA(arrs[k], Object *)[i] = bv;
                        }
                    }
                    sp = args;
                    if (cs->record_op == 0) {
                        (sp++)->l = cs->rname;
                        (sp++)->l = cs->rnames;
                        (sp++)->l = arrs[0];
                        args = sp - 3;
                    } else if (cs->record_op == 1) {
                        (sp++)->l = arrs[0];
                        args = sp - 1;
                    } else {
                        (sp++)->l = arrs[0];
                        (sp++)->l = arrs[1];
                        args = sp - 2;
                    }
                    t = record_helper[cs->record_op];
                    goto do_invoke;
                }
                case IND_TYPESWITCH: case IND_ENUMSWITCH: {
                    jint r = switch_match(cs, args[0].l, args[1].i);
                    sp = args;
                    (sp++)->i = r;
                    pc += 5;
                    break;
                }
            }
            break;
        }

        case 0xbb: op_bb: {
            int idx = U2(pc + 1);
            Class *k = cls->cp_res[idx];
            SYNC();
            if (!k && !(k = resolve_class_ref(cls, idx))) THROW();
            if (k->state != CS_INITED && init_class(k)) THROW();
            if (k->access & (ACC_INTERFACE | ACC_ABSTRACT)) {
                throw_new("java/lang/InstantiationError", "%s", k->name);
                THROW();
            }
            Object *o = alloc_object(k);
            if (!o) THROW();
            (sp++)->l = o;
            pc += 3;
            break;
        }
        case 0xbc: op_bc: {
            static const char types[] = "????ZCFDBSIJ";
            int at = pc[1];
            SYNC();
            Object *a = alloc_prim_array(at < 12 ? types[at] : 'I', sp[-1].i);
            if (!a) THROW();
            sp[-1].l = a;
            pc += 2;
            break;
        }
        case 0xbd: op_bd: {
            int idx = U2(pc + 1);
            SYNC();
            Class *k = resolve_class_ref(cls, idx);
            if (!k) THROW();
            Object *a = alloc_array(array_class_of(k), sp[-1].i);
            if (!a) THROW();
            sp[-1].l = a;
            pc += 3;
            break;
        }
        case 0xbe: op_be: {
            Object *a = sp[-1].l;
            if (!a) { SYNC(); npe_msg("Cannot read the array length", NULL, 0); THROW(); }
            sp[-1].i = a->len;
            pc++;
            break;
        }
        case 0xbf: op_bf: {
            Object *e = sp[-1].l;
            SYNC();
            if (!e) { npe_msg("Cannot throw exception", NULL, 0); THROW(); }
            exception = e;
            THROW();
        }
        case 0xc0: case 0xc1: op_c0: op_c1: {
            int idx = U2(pc + 1);
            Object *o = sp[-1].l;
            if (!o) {
                if (op == 0xc1) sp[-1].i = 0;
                pc += 3;
                break;
            }
            Class *k = cls->cp_res[idx];
            if (!k) { SYNC(); if (!(k = resolve_class_ref(cls, idx))) THROW(); }
            int ok = o->cls == k || is_subclass(o->cls, k);
            if (op == 0xc1) sp[-1].i = ok;
            else if (!ok) { SYNC(); class_cast(o->cls, k); THROW(); }
            pc += 3;
            break;
        }
        case 0xc2: op_c2: {
            Object *o = (--sp)->l;
            if (!o) { sp++; SYNC(); npe_msg("Cannot enter synchronized block", NULL, 0); THROW(); }
            pc++;
            break;
        }
        case 0xc3: op_c3: sp--; pc++; NEXT;
        case 0xc4: op_c4: {
            int wop = pc[1], idx = U2(pc + 2);
            switch (wop) {
                case 0x15: case 0x17: case 0x19: *sp++ = locals[idx]; break;
                case 0x16: case 0x18: sp[0] = locals[idx]; sp[1].u = 0; sp += 2; break;
                case 0x36: case 0x38: case 0x3a: locals[idx] = *--sp; break;
                case 0x37: case 0x39: sp -= 2; locals[idx] = sp[0]; break;
                case 0xa9: pc = m->code + locals[idx].i; goto next_insn;
                case 0x84: locals[idx].i = (jint)((uint32_t)locals[idx].i + (uint32_t)(jint)S2(pc + 4)); pc += 6; goto next_insn;
            }
            pc += 4;
            break;
        }
        case 0xc5: op_c5: {
            int idx = U2(pc + 1), dims = pc[3];
            SYNC();
            Class *k = resolve_class_ref(cls, idx);
            if (!k) THROW();
            jint counts[256];
            for (int i = 0; i < dims; i++) {
                counts[i] = sp[i - dims].i;
                if (counts[i] < 0) { throw_new("java/lang/NegativeArraySizeException", "%d", counts[i]); THROW(); }
            }
            Object *a = multi_new(k, counts, dims);
            if (!a) THROW();
            sp -= dims;
            (sp++)->l = a;
            pc += 4;
            break;
        }
        case 0xc6: op_c6: pc += (--sp)->l == NULL ? S2(pc + 1) : 3; NEXT;
        case 0xc7: op_c7: pc += (--sp)->l != NULL ? S2(pc + 1) : 3; NEXT;
        case 0xc8: op_c8: pc += S4(pc + 1); NEXT;
        case 0xc9: op_c9: (sp++)->i = (jint)(pc + 5 - m->code); pc += S4(pc + 1); NEXT;
        default: op_default:
            SYNC();
            throw_new("java/lang/VerifyError", "bad bytecode 0x%02x in %s.%s", op, cls->name, m->name);
            THROW();
        }
        continue;
    next_insn:
        continue;

    do_invoke:
        /* t = method, args = its arguments on our stack, len = instruction length */
        fr->pc = pc + len;
        fr->sp = sp;
        {
            char conv[3] = { 0, 0, 0 };
            Slot *a = args;
            for (int hops = 0; t->lambda && hops < 8; hops++) {
                Slot *nb;
                char c2[3];
                Method *nt = lambda_rewrite(t, a, &nb, c2);
                if (!nt) THROW();
                if (c2[0]) memcpy(conv, c2, 3);
                t = nt;
                a = nb;
            }
            if (t->access & ACC_NATIVE) {
                if (!t->native) {
                    char cn[200];
                    java_name(t->cls->name, cn, sizeof cn);
                    throw_new("java/lang/UnsatisfiedLinkError", "%s.%s%s is not available in MyOS Java", cn, t->name, t->desc);
                    THROW();
                }
                Slot rv;
                rv.u = 0;
                t->native(a, &rv);
                if (exception) THROW();
                int n = t->ret == 'V' ? 0 : (t->ret == 'J' || t->ret == 'D') ? 2 : 1;
                if (conv[0] && apply_conv(conv[0], conv[1], conv[2], &rv, &n)) THROW();
                sp = args;
                if (a != args && conv[0] == 0 && t->ret == 'V' && a > args) sp = args + (a - args);
                if (n >= 1) *sp++ = rv;
                if (n == 2) (sp++)->u = 0;
                pc += len;
                continue;     /* (outside the switch) */
            }
            if (t->access & ACC_ABSTRACT || !t->code) {
                char cn[200];
                java_name(t->cls->name, cn, sizeof cn);
                throw_new("java/lang/AbstractMethodError", "%s.%s%s", cn, t->name, t->desc);
                THROW();
            }
            if (push_frame(t, a)) THROW();
            if (conv[0]) {
                Frame *nf = &frames[depth - 1];
                nf->ret_conv = conv[0];
                nf->ret_box = conv[1];
                nf->ret_box2 = conv[2];
            }
            goto reload;
        }

    handle_exception_reload:
    handle_exception:
        for (;;) {
            fr = &frames[depth - 1];
            m = fr->m;
            cls = m->cls;
            uint32_t off = (uint32_t)(fr->pc - 1 - m->code);
            Object *ex = exception;
            int found = -1;
            for (int i = 0; i < m->n_ex && found < 0; i++) {
                ExEntry *e = &m->ex[i];
                if (off < e->start || off >= e->end) continue;
                if (!e->type) { found = i; break; }
                Class *k = cls->cp_res[e->type];
                if (!k) {
                    exception = NULL;
                    k = resolve_class_ref(cls, e->type);
                    exception = ex;
                }
                if (k && is_subclass(ex->cls, k)) found = i;
            }
            if (found >= 0) {
                exception = NULL;
                fr->sp = fr->locals + m->max_locals;
                fr->sp->l = ex;
                fr->sp++;
                fr->pc = m->code + m->ex[found].handler;
                goto reload;
            }
            depth--;
            if (depth <= base) return -1;
        }
    }
}

int run_method(Method *m, Slot *args, int n) {
    if (native_nesting > 400) {
        throw_new("java/lang/StackOverflowError", NULL);
        return -1;
    }
    Slot *top = depth ? frames[depth - 1].sp : stack_base;
    if (top + n + 16 > stack_end) {
        throw_new("java/lang/StackOverflowError", NULL);
        return -1;
    }
    if (n) memmove(top, args, (size_t)n * sizeof(Slot));
    char conv[3] = { 0, 0, 0 };
    Slot *a = top;
    int is_new = 0;
    for (int hops = 0; m->lambda && hops < 8; hops++) {
        Slot *nb;
        char c2[3];
        if (m->lambda->kind == 8) is_new = 1;
        Method *nt = lambda_rewrite(m, a, &nb, c2);
        if (!nt) return -1;
        if (c2[0]) memcpy(conv, c2, 3);
        m = nt;
        a = nb;
    }
    if (m->access & ACC_NATIVE) {
        if (!m->native) {
            char cn[200];
            java_name(m->cls->name, cn, sizeof cn);
            throw_new("java/lang/UnsatisfiedLinkError", "%s.%s%s is not available in MyOS Java", cn, m->name, m->desc);
            return -1;
        }
        Slot r;
        r.u = 0;
        m->native(a, &r);
        if (exception) return -1;
        int ns = 1;
        if (conv[0] && apply_conv(conv[0], conv[1], conv[2], &r, &ns)) return -1;
        retval = is_new ? top[0] : r;
        return 0;
    }
    if (!m->code) {
        char cn[200];
        java_name(m->cls->name, cn, sizeof cn);
        throw_new("java/lang/AbstractMethodError", "%s.%s%s", cn, m->name, m->desc);
        return -1;
    }
    if (push_frame(m, a)) return -1;
    if (conv[0]) {
        Frame *nf = &frames[depth - 1];
        nf->ret_conv = conv[0];
        nf->ret_box = conv[1];
        nf->ret_box2 = conv[2];
    }
    native_nesting++;
    int r = interp(depth - 1);
    native_nesting--;
    if (!r && is_new) retval = top[0];
    return r;
}
