/* main.c - the java command: options, class path, start-up, main() */
#include "jvm.h"
#include "rt_jar.h"             /* the runtime library jar, made by ports/java/mkrt.py */
#ifdef MYOS_LIBC
#include <unistd.h>
#endif

#define JVM_VERSION "1.0"
#define FRAMES 12000
#define SLOTS (192 * 1024)
#define HEAP_MB 16
#define C_STACK (1024 * 1024)

static size_t heap_bytes = (size_t)HEAP_MB << 20;
static char *props[64][2];
static int n_props;
static char classpath_str[512] = ".";

const char *jvm_property(const char *key) {
    for (int i = 0; i < n_props; i++) if (!strcmp(props[i][0], key)) return props[i][1];
    if (!strcmp(key, "java.version")) return "21";
    if (!strcmp(key, "java.specification.version")) return "21";
    if (!strcmp(key, "java.vendor")) return "MyOS";
    if (!strcmp(key, "java.vm.name")) return "MyOS Java " JVM_VERSION;
    if (!strcmp(key, "os.name")) return "MyOS";
    if (!strcmp(key, "os.arch")) return "aarch64";
    if (!strcmp(key, "line.separator")) return "\n";
    if (!strcmp(key, "file.separator")) return "/";
    if (!strcmp(key, "path.separator")) return ":";
    if (!strcmp(key, "user.name")) return "myos";
    if (!strcmp(key, "java.class.path")) return classpath_str;
    if (!strcmp(key, "file.encoding")) return "UTF-8";
    if (!strcmp(key, "user.dir") || !strcmp(key, "user.home")) {
        static char cwd[256];
#ifdef MYOS_LIBC
        if (!getcwd(cwd, sizeof cwd)) strcpy(cwd, "/");
#else
        strcpy(cwd, ".");
#endif
        return cwd;
    }
    return NULL;
}

static void usage(void) {
    printf("usage: java [options] CLASS [args...]       run CLASS.class (its main method)\n"
           "       java [options] -jar FILE.jar [args...]  run the Main-Class of a jar\n"
           "options:\n"
           "  -cp, -classpath PATH   folders and jars to find classes in (separated by : or ;)\n"
           "  -DNAME=VALUE           set a system property\n"
           "  -Xmx<size>             Java heap size, e.g. -Xmx12m (default %dm)\n"
           "  -version               show the version\n"
           "Compile on a PC with:  javac --release 8 Hello.java   (MyOS has no javac yet)\n", HEAP_MB);
}

static void split_classpath(const char *cp) {
    char buf[512];
    snprintf(buf, sizeof buf, "%s", cp);
    snprintf(classpath_str, sizeof classpath_str, "%s", cp);
    char *p = buf;
    while (*p) {
        char *e = p;
        while (*e && *e != ':' && *e != ';') e++;
        /* a Windows-style "C:\..." is not a separator, but MyOS has no drives */
        char save = *e;
        *e = 0;
        if (*p) add_classpath(p);
        if (!save) break;
        p = e + 1;
    }
}

static Object *make_args(int argc, char **argv) {
    Object *arr = alloc_array(array_class_of(cls_string), argc);
    if (!arr) return NULL;
    for (int i = 0; i < argc; i++) {
        Object *s = new_string_utf8(argv[i]);
        if (!s) return NULL;
        ADATA(arr, Object *)[i] = s;
    }
    return arr;
}

static void report_uncaught(void) {
    Object *ex = exception;
    exception = NULL;
    Class *t = load_class("java/lang/Thread");
    Method *m = t ? find_method(t, intern("dispatchUncaughtException"), intern("(Ljava/lang/Throwable;)V")) : NULL;
    Slot a;
    a.l = ex;
    if (!m || init_class(t) || run_method(m, &a, 1)) print_exception_native(ex);
}

extern void native_init(void);
extern void gc_prealloc_oom(void);

static int jvm_main(int argc, char **argv) {
    const char *cp = NULL, *jar = NULL;
    int i = 1;
    for (; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] != '-') break;
        if (!strcmp(a, "-cp") || !strcmp(a, "-classpath") || !strcmp(a, "--class-path")) {
            if (++i >= argc) { usage(); return 1; }
            cp = argv[i];
        } else if (!strcmp(a, "-jar")) {
            if (++i >= argc) { usage(); return 1; }
            jar = argv[i++];
            break;
        } else if (!strncmp(a, "-D", 2)) {
            const char *eq = strchr(a + 2, '=');
            if (n_props < 64) {
                size_t kl = eq ? (size_t)(eq - a - 2) : strlen(a + 2);
                props[n_props][0] = xmalloc(kl + 1);
                memcpy(props[n_props][0], a + 2, kl);
                props[n_props][0][kl] = 0;
                props[n_props][1] = (char *)(eq ? eq + 1 : "");
                n_props++;
            }
        } else if (!strcmp(a, "-version") || !strcmp(a, "--version") || !strcmp(a, "-showversion")) {
            printf("java version \"21\" (MyOS Java %s: a small JVM written for MyOS)\n"
                   "runs class files from javac 8 to 25; runtime library: MyOS's own\n", JVM_VERSION);
            if (strcmp(a, "-showversion")) return 0;
        } else if (!strcmp(a, "-h") || !strcmp(a, "-help") || !strcmp(a, "--help") || !strcmp(a, "-?")) {
            usage();
            return 0;
        } else if (!strcmp(a, "-verbose:gc") || !strcmp(a, "-Xlog:gc")) {
            extern int gc_verbose;
            gc_verbose = 1;
        } else if (!strncmp(a, "-Xmx", 4)) {
            long v = atol(a + 4);
            char u = a[strlen(a) - 1] | 32;
            size_t b = (size_t)v * (u == 'g' ? (1u << 30) : u == 'k' ? 1024u : u == 'm' ? (1u << 20) : 1u);
            if (b >= (1u << 20)) heap_bytes = b;
        }
        /* other options (-ea, -Xss..., -XX:..., -server) are accepted and ignored */
    }
    if (!jar && i >= argc) { usage(); return 1; }

    char main_name[300];
    if (jar) {
        char mc[256];
        int r = classpath_has_jar(jar, mc, sizeof mc);
        if (r < 0) { fprintf(stderr, "Error: Unable to access jarfile %s\n", jar); return 1; }
        if (r == 0) { fprintf(stderr, "no main manifest attribute, in %s\n", jar); return 1; }
        split_classpath(jar);
        snprintf(main_name, sizeof main_name, "%s", mc);
    } else {
        const char *n = argv[i++];
        size_t l = strlen(n);
        if (l > 5 && !strcmp(n + l - 5, ".java")) {
            fprintf(stderr, "java: can't run %s: MyOS has no Java compiler yet.\n"
                            "Compile it on a PC (javac --release 8 %s) and copy the .class file here.\n", n, n);
            return 1;
        }
        snprintf(main_name, sizeof main_name, "%s", n);
        l = strlen(main_name);
        if (l > 6 && !strcmp(main_name + l - 6, ".class")) {
            main_name[l - 6] = 0;
            char *slash = strrchr(main_name, '/');
            if (slash && !cp) {             /* java dir/Hello.class */
                *slash = 0;
                static char dir[300];
                snprintf(dir, sizeof dir, "%s", main_name);
                memmove(main_name, slash + 1, strlen(slash + 1) + 1);
                cp = dir;
            }
        }
        split_classpath(cp ? cp : ".");
    }
    for (char *p = main_name; *p; p++) if (*p == '.') *p = '/';

    /* the VM */
    stack_base = xcalloc(SLOTS, sizeof(Slot));
    stack_end = stack_base + SLOTS;
    max_depth = FRAMES;
    frames = xcalloc(FRAMES, sizeof(Frame));
    heap_init(heap_bytes);
    set_runtime_jar(rt_jar, sizeof rt_jar);
    cls_object = load_class("java/lang/Object");
    if (!cls_object) fatal("runtime library: no java.lang.Object");
    cls_class = load_class("java/lang/Class");
    cls_string = load_class("java/lang/String");
    cls_throwable = load_class("java/lang/Throwable");
    if (!cls_class || !cls_string || !cls_throwable) fatal("runtime library is incomplete");
    cls_chararray = array_class_of(prim_class('C'));
    native_init();
    gc_prealloc_oom();
    Class *sys = load_class("java/lang/System");
    if (!sys || init_class(sys)) {
        if (exception) print_exception_native(exception);
        fatal("can't start the runtime library");
    }

    Class *mc = load_class(main_name);
    {
        extern int gc_verbose;
        if (gc_verbose > 0) {
            int nc = 0;
            for (Class *k = class_list; k; k = k->all_next) nc++;
#ifdef MYOS_LIBC
            extern size_t myos_heap_free(void);
            fprintf(stderr, "[java: %d classes loaded, Java heap %lu KB, C heap free %lu KB]\n", nc,
                    (unsigned long)(heap_total_bytes() / 1024), (unsigned long)(myos_heap_free() / 1024));
#else
            fprintf(stderr, "[java: %d classes loaded, Java heap %lu KB]\n", nc, (unsigned long)(heap_total_bytes() / 1024));
#endif
        }
    }
    if (!mc) {
        char dotted[300];
        java_name(main_name, dotted, sizeof dotted);
        fflush(stdout);
        fprintf(stderr, "Error: Could not find or load main class %s\n"
                        "Caused by: java.lang.ClassNotFoundException: %s\n", dotted, dotted);
        return 1;
    }
    /* main(String[]), then (Java 25) main(), also as instance methods */
    const char *mn = intern("main"), *d1 = intern("([Ljava/lang/String;)V"), *d0 = intern("()V");
    Method *m = NULL;
    for (Class *k = mc; k && !m; k = k->super) {
        Method *x = find_method(k, mn, d1);
        if (x && (x->access & ACC_STATIC)) m = x;
    }
    if (!m) m = resolve_method(mc, mn, d1);
    if (!m) m = resolve_method(mc, mn, d0);
    if (!m) {
        char dotted[300];
        java_name(main_name, dotted, sizeof dotted);
        fprintf(stderr, "Error: Main method not found in class %s, please define the main method as:\n"
                        "   public static void main(String[] args)\n", dotted);
        return 1;
    }
    Slot a[2];
    int n = 0;
    if (init_class(mc)) { report_uncaught(); jvm_exit(1); }
    if (!(m->access & ACC_STATIC)) {
        Method *ctor = find_method(mc, intern("<init>"), d0);
        if (!ctor) {
            fprintf(stderr, "Error: no constructor without arguments in %s for its main method\n", main_name);
            return 1;
        }
        Object *o = alloc_object(mc);
        if (!o) { report_uncaught(); jvm_exit(1); }
        a[0].l = o;
        if (run_method(ctor, a, 1)) { report_uncaught(); jvm_exit(1); }
        a[0].l = o;
        n = 1;
    }
    if (m->desc == d1) {
        Object *args = make_args(argc - i, argv + i);
        if (!args) { report_uncaught(); jvm_exit(1); }
        a[n++].l = args;
    }
    if (run_method(m, a, n)) {
        report_uncaught();
        jvm_exit(1);
    }
    jvm_exit(0);
    return 0;
}

#ifdef MYOS_LIBC
/* The normal stack is 256 KB; run on a bigger one from the heap. */
static int main_argc;
static char **main_argv;

static void __attribute__((noreturn, used)) on_big_stack(void) {
    exit(jvm_main(main_argc, main_argv));
}

int main(int argc, char **argv) {
    char *stack = malloc(C_STACK);
    main_argc = argc;
    main_argv = argv;
    if (!stack) {
        char here;
        c_stack_top = &here + 64;
        return jvm_main(argc, argv);
    }
    uintptr_t top = ((uintptr_t)stack + C_STACK) & ~(uintptr_t)15;
    c_stack_top = (void *)top;
    __asm__ volatile("mov sp, %0\n\tbr %1" : : "r"(top), "r"(on_big_stack) : "memory");
    __builtin_unreachable();
}
#else
int main(int argc, char **argv) {
    char here;
    c_stack_top = &here + 256;
    return jvm_main(argc, argv);
}
#endif
