/* gc.c - the Java heap: allocation and a mark-sweep garbage collector.
 *
 * Objects never move. Fields are marked precisely (each class has a map
 * of its reference slots); the Java stack and the C stack are scanned
 * conservatively: any word that points at an object start keeps it alive
 * (C stack words may also point inside an object).
 *
 * Small objects grow up from the bottom of the heap, bigger ones (over
 * SMALL_MAX bytes) down from the top, so long-lived small objects don't
 * cut the free space for bigger ones into pieces. Arrays of EXT_MIN bytes
 * or more live outside the heap, in blocks from malloc (whose free list
 * joins neighbours again), so a big array can always use any free memory.
 */
#include <setjmp.h>
#include "jvm.h"

#define SMALL_MAX 512
static uint8_t *heap, *heap_end;
static uint8_t *bump;                       /* small objects: [heap, bump) */
static uint8_t *top;                        /* big objects: [top, heap_end) */
static uint64_t *startbits;                 /* one bit per 8 bytes: a live object starts here */
static Object *small[SMALL_MAX / 8 + 1];    /* free chunks by exact size */
static Object *large;                       /* bigger free chunks */
void *c_stack_top;
static size_t heap_size;

#define NEXT_FREE(o) ((o)->len ? (Object *)(heap + (size_t)(uint32_t)(o)->len * 8) : NULL)

static void set_next(Object *o, Object *next) {
    o->len = next ? (jint)(((uint8_t *)next - heap) / 8) : 0;
}

void heap_init(size_t bytes) {
    while (bytes >= (1u << 20)) {
        heap = malloc(bytes + 16);
        if (heap) break;
        bytes -= 2u << 20;
    }
    if (!heap) fatal("not enough memory for the Java heap");
    heap = (uint8_t *)(((uintptr_t)heap + 15) & ~(uintptr_t)15);
    heap_size = bytes;
    heap_end = heap + bytes;
    bump = heap + 8;        /* offset 0 would look like "no next" in the free lists */
    top = heap_end;
    startbits = xcalloc(bytes / 64 + 1, 8);
}

static size_t ext_bytes;
size_t heap_total_bytes(void) { return heap_size + ext_bytes; }

size_t heap_free_bytes(void) {
    size_t n = (size_t)(top - bump);
    for (int i = 0; i <= SMALL_MAX / 8; i++)
        for (Object *o = small[i]; o; o = NEXT_FREE(o)) n += o->gc;
    for (Object *o = large; o; o = NEXT_FREE(o)) n += o->gc;
    return n;
}

static inline void set_bit(void *p) {
    size_t i = (size_t)((uint8_t *)p - heap) / 8;
    startbits[i / 64] |= 1ull << (i % 64);
}
static inline void clear_bit(void *p) {
    size_t i = (size_t)((uint8_t *)p - heap) / 8;
    startbits[i / 64] &= ~(1ull << (i % 64));
}
static inline int get_bit(void *p) {
    size_t i = (size_t)((uint8_t *)p - heap) / 8;
    return (int)((startbits[i / 64] >> (i % 64)) & 1);
}

/* ------------------------------------------------------------ big arrays from malloc */
#define EXT_MIN (64 * 1024)
#define C_HEAP_RESERVE (2u << 20)        /* keep this much malloc space for the VM itself */
typedef struct Ext { struct Ext *next; uint64_t pad; } Ext;     /* header in front of the object */
static Ext *ext_list;
static uint8_t *ext_lo = (uint8_t *)~(uintptr_t)0, *ext_hi;
static size_t ext_since_gc;
#define EXT_OBJ(e) ((Object *)((Ext *)(e) + 1))

#ifdef MYOS_LIBC
extern size_t myos_heap_free(void);
static size_t c_heap_free(void) { return myos_heap_free(); }
#else
static size_t c_heap_free(void) { return (size_t)256 << 20; }
#endif

static Object *ext_find(uint8_t *p, int interior) {
    if (p < ext_lo || p >= ext_hi) return NULL;
    for (Ext *e = ext_list; e; e = e->next) {
        Object *o = EXT_OBJ(e);
        if ((uint8_t *)o == p || (interior && p > (uint8_t *)o && p < (uint8_t *)o + (o->gc & ~GC_MARK))) return o;
    }
    return NULL;
}

static Object *ext_alloc(size_t size) {
    if (c_heap_free() < size + C_HEAP_RESERVE) return NULL;
    Ext *e = malloc(sizeof(Ext) + size);
    if (!e) return NULL;
    e->next = ext_list;
    ext_list = e;
    Object *o = EXT_OBJ(e);
    if ((uint8_t *)o < ext_lo) ext_lo = (uint8_t *)o;
    if ((uint8_t *)o + size > ext_hi) ext_hi = (uint8_t *)o + size;
    ext_bytes += size;
    ext_since_gc += size;
    o->gc = (uint32_t)size;
    return o;
}

static void ext_sweep(void) {
    ext_lo = (uint8_t *)~(uintptr_t)0;
    ext_hi = NULL;
    for (Ext **pe = &ext_list; *pe;) {
        Ext *e = *pe;
        Object *o = EXT_OBJ(e);
        if (o->gc & GC_MARK) {
            o->gc &= ~GC_MARK;
            if ((uint8_t *)o < ext_lo) ext_lo = (uint8_t *)o;
            if ((uint8_t *)o + o->gc > ext_hi) ext_hi = (uint8_t *)o + o->gc;
            pe = &e->next;
        } else {
            *pe = e->next;
            ext_bytes -= o->gc;
            free(e);
        }
    }
    ext_since_gc = 0;
}

int is_object(void *p) {
    uint8_t *b = p;
    if (b >= heap && b < heap_end) return (((uintptr_t)b & 7) == 0) && get_bit(b);
    return ((uintptr_t)b & 7) == 0 && ext_find(b, 0) != NULL;
}

static Object *small_tail[SMALL_MAX / 8 + 1], *large_tail;
static int sweeping;            /* the sweep keeps the free lists in address order */
int gc_verbose = -1;

static void put_free(Object *o, size_t size) {
    o->cls = NULL;
    o->gc = (uint32_t)size;
    Object **head = size <= SMALL_MAX ? &small[size / 8] : &large;
    Object **tail = size <= SMALL_MAX ? &small_tail[size / 8] : &large_tail;
    if (sweeping) {
        set_next(o, NULL);
        if (*tail) set_next(*tail, o); else *head = o;
        *tail = o;
    } else {
        set_next(o, *head);
        *head = o;
    }
}

/* take size bytes from a free chunk c of cs bytes (already unlinked) */
static Object *carve(Object *c, size_t cs, size_t size) {
    if (cs - size >= 16) {
        Object *o = (Object *)((uint8_t *)c + (cs - size));
        put_free(c, cs - size);
        o->gc = (uint32_t)size;
        return o;
    }
    c->gc = (uint32_t)cs;
    return c;
}

/* first fit in the big-chunk list; where: 0 = small region only, 1 = any */
static Object *from_large(size_t size, int where) {
    Object *prev = NULL;
    for (Object *c = large; c; prev = c, c = NEXT_FREE(c)) {
        size_t cs = c->gc;
        if (cs < size || (where == 0 && (uint8_t *)c >= top)) continue;
        Object *next = NEXT_FREE(c);
        if (prev) set_next(prev, next); else large = next;
        return carve(c, cs, size);
    }
    return NULL;
}

static Object *raw_alloc(size_t size, int last_resort) {
    Object *o;
    if (size <= SMALL_MAX) {
        if ((o = small[size / 8])) {
            small[size / 8] = NEXT_FREE(o);
            return o;
        }
        for (size_t s = size + 16; s <= SMALL_MAX; s += 8) {     /* split a bigger small chunk */
            Object *c = small[s / 8];
            if (!c) continue;
            small[s / 8] = NEXT_FREE(c);
            return carve(c, s, size);
        }
        if ((o = from_large(size, 0))) return o;                /* fill holes before growing */
        if ((size_t)(top - bump) >= size) {
            o = (Object *)bump;
            bump += size;
            o->gc = (uint32_t)size;
            return o;
        }
        return last_resort ? from_large(size, 1) : NULL;
    }
    if ((o = from_large(size, 1))) return o;
    if ((size_t)(top - bump) >= size) {
        top -= size;
        o = (Object *)top;
        o->gc = (uint32_t)size;
        return o;
    }
    return NULL;
}

static Object *oom_error;

static Object *alloc_bytes(size_t size, Class *c) {
    size = (size + 7) & ~(size_t)7;
    if (size < 16) size = 16;
    if (size >= 0x7fffffff) goto oom;
    if (size >= EXT_MIN && c->is_array) {
        /* collect now and then, so dead big arrays give their memory back */
        if (ext_since_gc > heap_size / 2) gc_collect();
        Object *o = ext_alloc(size);
        if (!o) {
            gc_collect();
            o = ext_alloc(size);
        }
        if (!o) o = raw_alloc(size, 1);     /* maybe it fits in the heap */
        if (!o) goto oom;
        uint32_t real = o->gc;
        memset(o, 0, real);
        o->gc = real;
        o->cls = c;
        if ((uint8_t *)o >= heap && (uint8_t *)o < heap_end) set_bit(o);
        return o;
    }
    Object *o = raw_alloc(size, 0);
    if (!o) {
        gc_collect();
        o = raw_alloc(size, 1);
    }
    if (!o) goto oom;
    uint32_t real = o->gc;
    memset(o, 0, real);
    o->gc = real;
    o->cls = c;
    set_bit(o);
    return o;
oom: {
        static int in_oom;
        if (!in_oom) {          /* a proper error with a stack trace, if there's room for one */
            in_oom = 1;
            throw_new("java/lang/OutOfMemoryError", "Java heap space (%lu bytes more needed)", (unsigned long)size);
            in_oom = 0;
            if (exception) return NULL;
        }
    }
    if (!oom_error) {
        Class *k = load_class("java/lang/OutOfMemoryError");
        if (k && (oom_error = raw_alloc(16 + k->n_inst * 8, 1))) {
            uint32_t real = oom_error->gc;
            memset(oom_error, 0, real);
            oom_error->gc = real;
            oom_error->cls = k;
            set_bit(oom_error);
            gc_add_root(&oom_error);
        }
    }
    exception = oom_error;
    if (!exception) fatal("out of memory");
    return NULL;
}

/* the OutOfMemoryError thrown when even a small exception object doesn't fit */
void gc_prealloc_oom(void) {
    Class *k = load_class("java/lang/OutOfMemoryError");
    if (k && !oom_error && (oom_error = raw_alloc(16 + k->n_inst * 8, 1))) {
        uint32_t real = oom_error->gc;
        memset(oom_error, 0, real);
        oom_error->gc = real;
        oom_error->cls = k;
        set_bit(oom_error);
        gc_add_root(&oom_error);
    }
}

Object *alloc_object(Class *c) {
    return alloc_bytes(16 + (size_t)c->n_inst * 8, c);
}

Object *alloc_array(Class *ac, jint len) {
    if (len < 0) {
        throw_new("java/lang/NegativeArraySizeException", "%d", len);
        return NULL;
    }
    Object *o = alloc_bytes(16 + (size_t)len * ac->elem_size, ac);
    if (o) o->len = len;
    return o;
}

Object *alloc_prim_array(char t, jint len) {
    return alloc_array(array_class_of(prim_class(t)), len);
}

/* ------------------------------------------------------------ marking */
static Object **mstack;
static size_t msp, mcap;
static int moverflow;

static Object ***roots;
static int n_roots, cap_roots;

void gc_add_root(Object **p) {
    if (n_roots == cap_roots) {
        cap_roots = cap_roots ? cap_roots * 2 : 32;
        roots = realloc(roots, cap_roots * sizeof(Object **));
    }
    roots[n_roots++] = p;
}

static inline void mark(Object *o) {
    if (!o || (o->gc & GC_MARK)) return;
    o->gc |= GC_MARK;
    if (msp == mcap) {
        size_t ncap = mcap ? mcap * 2 : 4096;
        Object **n = realloc(mstack, ncap * sizeof(Object *));
        if (!n) { moverflow = 1; return; }
        mstack = n;
        mcap = ncap;
    }
    mstack[msp++] = o;
}

static inline void mark_maybe(uint64_t v) {
    if (is_object((void *)(uintptr_t)v)) mark((Object *)(uintptr_t)v);
}

/* a word from the C stack: may point inside an object */
static void mark_interior(uint64_t v) {
    uint8_t *p = (uint8_t *)(uintptr_t)(v & ~(uint64_t)7);
    if (p < heap || p >= heap_end) {
        Object *o = ext_find((uint8_t *)(uintptr_t)v, 1);
        if (o) mark(o);
        return;
    }
    if (p >= bump && p < top) return;
    size_t i = (size_t)(p - heap) / 8;
    for (size_t limit = 0; limit < (1u << 22); limit++) {    /* find the start at or before p */
        uint64_t w = startbits[i / 64] & (~0ull >> (63 - i % 64));
        if (w) {
            size_t bit = 63 - (size_t)__builtin_clzll(w);
            Object *o = (Object *)(heap + ((i / 64) * 64 + bit) * 8);
            if ((uint8_t *)(uintptr_t)v < (uint8_t *)o + (o->gc & ~GC_MARK)) mark(o);
            return;
        }
        if (i < 64) return;
        i = (i / 64) * 64 - 1;
    }
}

static void scan(Object *o) {
    Class *c = o->cls;
    if (c->is_array) {
        if (c->elem == 'L') {
            Object **a = ADATA(o, Object *);
            for (jint i = 0; i < o->len; i++) mark(a[i]);
        }
        return;
    }
    Slot *f = FIELDS(o);
    for (uint32_t i = 0; i < c->n_inst; i++)
        if (c->refmap[i]) mark(f[i].l);
}

static void drain(void) {
    while (msp) scan(mstack[--msp]);
}

extern void gc_mark_extra(void (*markfn)(Object *));

static void __attribute__((noinline)) mark_c_stack(void) {
    jmp_buf regs;
    setjmp(regs);
    volatile uint64_t here = 0;
    uint64_t *p = (uint64_t *)(((uintptr_t)&here) & ~(uintptr_t)7);
    for (uint64_t *r = (uint64_t *)&regs; r < (uint64_t *)((char *)&regs + sizeof regs); r++) mark_interior(*r);
    for (; (void *)p < c_stack_top; p++) mark_interior(*p);
}

static void rescan_region(uint8_t *from, uint8_t *to) {
    for (uint8_t *p = from; p < to;) {
        Object *o = (Object *)p;
        uint32_t sz = o->gc & ~GC_MARK;
        if (o->cls && (o->gc & GC_MARK)) { scan(o); drain(); }
        p += sz;
    }
}

/* free unmarked objects in [from, to), joining neighbouring free chunks;
 * returns the start of a free run that reaches 'to' (or NULL) */
static uint8_t *sweep_region(uint8_t *from, uint8_t *to, int skip_first_run, uint8_t **first_end) {
    uint8_t *run = NULL;
    int first = 1;
    if (first_end) *first_end = NULL;
    for (uint8_t *p = from; p < to;) {
        Object *o = (Object *)p;
        uint32_t sz = o->gc & ~GC_MARK;
        if (o->cls && (o->gc & GC_MARK)) {
            o->gc = sz;
            if (run) {
                if (first && skip_first_run && run == from) *first_end = p;    /* the caller drops it */
                else put_free((Object *)run, (size_t)(p - run));
                run = NULL;
            }
            first = 0;
        } else {
            if (o->cls) clear_bit(o);
            if (!run) run = p;
        }
        p += sz;
    }
    return run;
}

void gc_collect(void) {
    msp = 0;
    moverflow = 0;
    /* roots: Java stack */
    Slot *stop = stack_base;
    if (depth > 0) {
        Frame *f = &frames[depth - 1];
        stop = f->sp;
        Slot *lim = f->locals + f->m->max_locals + f->m->max_stack;
        if (lim > stop && lim <= stack_end) stop = lim;
    }
    for (Slot *s = stack_base; s < stop; s++) mark_maybe(s->u);
    mark_maybe(retval.u);
    mark(exception);
    for (Class *c = class_list; c; c = c->all_next) {
        mark(c->mirror);
        mark(c->lambda_single);
        for (int i = 0; i < c->n_statics; i++)
            if (c->static_refmap[i]) mark(c->statics[i].l);
    }
    for (int i = 0; i < n_roots; i++) mark(*roots[i]);
    gc_mark_extra(mark);
    mark_c_stack();
    drain();
    while (moverflow) {                 /* the mark stack ran out: rescan marked objects */
        moverflow = 0;
        rescan_region(heap + 8, bump);
        rescan_region(top, heap_end);
    }
    memset(small, 0, sizeof small);
    memset(small_tail, 0, sizeof small_tail);
    large = large_tail = NULL;
    sweeping = 1;
    uint8_t *run = sweep_region(heap + 8, bump, 0, NULL);
    if (run) bump = run;
    uint8_t *first_end;
    run = sweep_region(top, heap_end, 1, &first_end);
    if (run == top) top = heap_end;             /* everything up there is free */
    else {
        if (run) put_free((Object *)run, (size_t)(heap_end - run));
        if (first_end) top = first_end;         /* free space at the bottom of the big region */
    }
    sweeping = 0;
    ext_sweep();
    if (gc_verbose < 0) gc_verbose = getenv("JGC") != NULL;
    if (gc_verbose) {
        size_t biggest = (size_t)(top - bump);
        for (Object *o = large; o; o = NEXT_FREE(o)) if (o->gc > biggest) biggest = o->gc;
        fprintf(stderr, "[gc: %lu KB free of %lu KB; small objects %lu KB, big %lu KB; biggest free block %lu KB;"
                        " %lu KB in big arrays outside the heap]\n",
                (unsigned long)(heap_free_bytes() / 1024), (unsigned long)(heap_size / 1024),
                (unsigned long)((bump - heap) / 1024), (unsigned long)((heap_end - top) / 1024),
                (unsigned long)(biggest / 1024), (unsigned long)(ext_bytes / 1024));
    }
}
