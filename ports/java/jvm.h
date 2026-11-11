/* jvm.h - a small Java Virtual Machine for MyOS (written from scratch, MIT).
 *
 * Runs .class / .jar files compiled on the PC with javac. One thread, an
 * interpreter with its own frame stack (Java calls don't use the C stack),
 * a mark-sweep garbage collector and a runtime library of our own (Java
 * sources in ports/java/lib, embedded in the binary as a jar).
 */
#ifndef JVM_H
#define JVM_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

typedef int32_t jint;
typedef int64_t jlong;
typedef uint16_t jchar;
typedef int8_t jbyte;
typedef int16_t jshort;
typedef float jfloat;
typedef double jdouble;

typedef struct Object Object;
typedef struct Class Class;
typedef struct Method Method;
typedef struct Field Field;

/* one operand-stack / local-variable / field slot. long and double take
 * two slots on the stack and in locals (value in the first), one in fields */
typedef union Slot {
    jint i; jlong j; jfloat f; jdouble d; Object *l; void *p; uint64_t u;
} Slot;

/* every heap object: 16-byte header, then fields (Slot each) or array data */
struct Object {
    Class *cls;         /* NULL = free chunk */
    uint32_t gc;        /* chunk size in bytes | GC_MARK */
    jint len;           /* arrays: length */
};
#define GC_MARK 0x80000000u
#define FIELDS(o) ((Slot *)((Object *)(o) + 1))
#define ADATA(o, T) ((T *)((Object *)(o) + 1))

/* constant pool tags */
enum { CP_UTF8 = 1, CP_INT = 3, CP_FLOAT = 4, CP_LONG = 5, CP_DOUBLE = 6, CP_CLASS = 7,
       CP_STRING = 8, CP_FIELD = 9, CP_METHOD = 10, CP_IMETHOD = 11, CP_NAT = 12,
       CP_MHANDLE = 15, CP_MTYPE = 16, CP_DYNAMIC = 17, CP_INDY = 18, CP_MODULE = 19, CP_PACKAGE = 20 };

typedef union CpEntry {
    jint i; jfloat f; jlong j; jdouble d;
    const char *utf8;
    struct { uint16_t a, b; } ref;
} CpEntry;

/* access flags */
enum { ACC_PUBLIC = 1, ACC_PRIVATE = 2, ACC_PROTECTED = 4, ACC_STATIC = 8, ACC_FINAL = 0x10,
       ACC_SYNCHRONIZED = 0x20, ACC_VOLATILE = 0x40, ACC_NATIVE = 0x100, ACC_INTERFACE = 0x200,
       ACC_ABSTRACT = 0x400, ACC_ENUM = 0x4000 };

enum { CS_LOADED, CS_LINKED, CS_INITING, CS_INITED, CS_ERROR };

typedef struct MethodRef {      /* a resolved Methodref: the method + an inline cache */
    Method *m;
    Class *ic_cls;
    Method *ic_m;
} MethodRef;

struct Class {
    Object *mirror;             /* java.lang.Class object (made on first use) */
    const char *name;           /* internal name "java/lang/String", "[I" (interned) */
    Class *super;
    Class **ifaces;
    uint16_t n_ifaces;
    uint16_t access;
    uint8_t state;
    uint8_t is_array;           /* arrays: elem = 'I','Z',... or 'L' for references */
    char elem;
    uint8_t elem_size;
    char prim;                  /* primitive classes (int.class): 'I' ... */
    Class *component;
    Class *array_class;         /* cached [this */
    uint16_t cp_n;
    uint8_t *cp_tag;
    CpEntry *cp;
    void **cp_res;              /* resolved entries (Class*, Field*, MethodRef*, Object*, CallSite*) */
    Field *fields;
    uint16_t n_fields;
    Method *methods;
    uint16_t n_methods;
    uint32_t n_inst;            /* instance slots, super's included */
    uint8_t *refmap;            /* per instance slot: 1 = reference */
    Slot *statics;
    uint8_t *static_refmap;
    uint16_t n_statics;
    const char *source;         /* SourceFile attribute */
    uint16_t n_bsm;
    uint16_t **bsm;             /* BootstrapMethods: [method handle, n args, args...] */
    Class *hash_next;
    Class *all_next;
    Object *lambda_single;      /* lambda classes with no captures: the one instance */
};

struct Field {
    const char *name, *desc;
    Class *cls;
    uint16_t access;
    uint16_t constval;          /* ConstantValue cp index, 0 = none */
    uint32_t slot;
};

typedef void (*NativeFn)(Slot *args, Slot *ret);

typedef struct ExEntry { uint16_t start, end, handler, type; } ExEntry;
typedef struct LineEntry { uint16_t pc, line; } LineEntry;
typedef struct LambdaInfo LambdaInfo;

struct Method {
    Class *cls;
    const char *name, *desc;
    uint16_t access;
    uint16_t max_stack, max_locals;
    uint16_t arg_slots;         /* 'this' included */
    char ret;                   /* 'V', 'I' (also Z B C S), 'J', 'F', 'D', 'L' */
    uint32_t code_len;
    uint8_t *code;
    uint16_t n_ex, n_lines;
    ExEntry *ex;
    LineEntry *lines;
    NativeFn native;
    LambdaInfo *lambda;
};

typedef struct Frame {
    Method *m;
    uint8_t *pc;                /* return address (always past the start of the current instruction) */
    Slot *locals;
    Slot *sp;
    char ret_conv;              /* lambda adapters: convert the return value ('V' drop, 'L' box, prim unbox) */
    char ret_box, ret_box2;     /* types for the conversion */
} Frame;

/* ---- globals (one thread) */
extern Slot *stack_base, *stack_end;
extern Frame *frames;
extern int depth, max_depth;
extern Object *exception;       /* pending exception */
extern Slot retval;             /* value of the last method run by run_method */
extern int native_nesting;
extern Class *class_list;

/* well-known classes and field slots */
extern Class *cls_object, *cls_string, *cls_class, *cls_throwable, *cls_chararray;
extern int slot_str_value, slot_str_hash, slot_thr_message, slot_thr_backtrace, slot_thr_cause;
extern int slot_class_vm;

/* ---- class.c */
const char *intern(const char *s);
const char *intern_n(const char *s, size_t n);
Class *load_class(const char *name);            /* NULL + exception if it can't */
Class *find_loaded(const char *name);
Class *array_class_of(Class *elem);
Class *prim_class(char t);
int init_class(Class *c);                       /* 0 ok, -1 exception */
Method *find_method(Class *c, const char *name, const char *desc);   /* in c only */
Method *resolve_method(Class *c, const char *name, const char *desc);
Method *find_virtual(Class *c, const char *name, const char *desc);
Field *resolve_field(Class *c, const char *name, const char *desc);
Field *find_field_by_name(Class *c, const char *name);
int is_subclass(Class *s, Class *t);           /* s assignable to t */
Class *resolve_class_ref(Class *c, int idx);
Field *resolve_field_ref(Class *c, int idx);
MethodRef *resolve_method_ref(Class *c, int idx);
Object *resolve_string_ref(Class *c, int idx);
Object *class_mirror(Class *c);
Class *mirror_class(Object *mirror);
int desc_arg_slots(const char *desc);
char desc_ret(const char *desc);
void add_classpath(const char *entry);
int classpath_has_jar(const char *path, char *main_class, size_t n);
void set_runtime_jar(const unsigned char *data, size_t len);
Class *define_lambda_class(Class *host, const char *iface_name);
int class_resource(const char *name, unsigned char **data, size_t *len);
void java_name(const char *internal, char *out, size_t n);

/* ---- interp.c */
int interp(int base);
int run_method(Method *m, Slot *args, int nslots);    /* 0 ok (result in retval), -1 exception */
int push_frame(Method *m, Slot *args);
int frame_line(Method *m, uint8_t *pc);
Object *box_value(char type, Slot v);
int unbox_value(Object *o, char type, Slot *out);

/* ---- gc.c */
void heap_init(size_t bytes);
Object *alloc_object(Class *c);
Object *alloc_array(Class *arr_cls, jint len);
Object *alloc_prim_array(char t, jint len);
void gc_collect(void);
void gc_add_root(Object **p);
int is_object(void *p);
size_t heap_free_bytes(void), heap_total_bytes(void);
extern void *c_stack_top;

/* ---- native.c */
NativeFn find_native(const char *cls, const char *name, const char *desc);
void throw_new(const char *cls, const char *fmt, ...);
void throw_npe(void);
void fill_stack_trace(Object *thr, int skip_inits);
Object *new_string_utf8(const char *s);
Object *new_string_chars(const jchar *c, jint n);
char *string_utf8(Object *s);                   /* malloc'd */
Object *intern_string(Object *s);
void print_exception_native(Object *ex);
void jvm_exit(int code);
int jstrlen(Object *s);
jchar *jstrchars(Object *s);

/* ---- util.c */
int inflate_raw(const unsigned char *src, size_t srclen, unsigned char *dst, size_t dstlen);
int double_to_digits(double v, char *digits, int *exp10);    /* shortest; returns digit count */
int float_to_digits(float v, char *digits, int *exp10);
void java_double_string(double v, char *out);
void java_float_string(float v, char *out);
double parse_double(const char *s, int *ok);
void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t m);
void fatal(const char *fmt, ...);
jlong now_millis(void);
jlong now_nanos(void);
void sleep_millis(jlong ms);

#endif
