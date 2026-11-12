// lxtest - a tiny static Linux AArch64 program that talks to the kernel with
// raw system calls (no C library), to test MyOS's Linux layer (arm/linux.c).
// Built by arm/build.ps1 with clang --target=aarch64-linux-gnu -nostdlib -static.
//
//   lxtest            prints its arguments, environment and auxv facts, then
//                     tests brk, mmap, files, fork/wait, pipes, uname, time
//   lxtest exit N     exits with code N
//   lxtest crash      touches unmapped memory
//   lxtest echo X..   prints X.. (used as the program run by execve)
typedef unsigned long u64;
typedef long i64;

static i64 sc(i64 n, i64 a, i64 b, i64 c, i64 d, i64 e, i64 f) {
    register i64 x8 __asm__("x8") = n;
    register i64 x0 __asm__("x0") = a;
    register i64 x1 __asm__("x1") = b;
    register i64 x2 __asm__("x2") = c;
    register i64 x3 __asm__("x3") = d;
    register i64 x4 __asm__("x4") = e;
    register i64 x5 __asm__("x5") = f;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5) : "memory");
    return x0;
}
#define S0(n) sc(n, 0, 0, 0, 0, 0, 0)
#define S1(n, a) sc(n, (i64)(a), 0, 0, 0, 0, 0)
#define S2(n, a, b) sc(n, (i64)(a), (i64)(b), 0, 0, 0, 0)
#define S3(n, a, b, c) sc(n, (i64)(a), (i64)(b), (i64)(c), 0, 0, 0)
#define S4(n, a, b, c, d) sc(n, (i64)(a), (i64)(b), (i64)(c), (i64)(d), 0, 0)
#define S6(n, a, b, c, d, e, f) sc(n, (i64)(a), (i64)(b), (i64)(c), (i64)(d), (i64)(e), (i64)(f))

static u64 slen(const char *s) { u64 n = 0; while (s[n]) n++; return n; }
static void out(const char *s) { S3(64, 1, s, slen(s)); }
static int seq(const char *a, const char *b) { while (*a && *a == *b) a++, b++; return *a == *b; }

static void num(i64 v) {
    char b[24];
    int i = 23;
    int neg = v < 0;
    u64 u = neg ? (u64)-v : (u64)v;
    b[i] = 0;
    do { b[--i] = (char)('0' + u % 10); u /= 10; } while (u);
    if (neg) b[--i] = '-';
    out(b + i);
}

static void check(const char *what, int ok) {
    out(ok ? "ok " : "FAIL ");
    out(what);
    out("\n");
}

static void exit_(int c) { S1(94, c); for (;;) {} }

static int tls_var_check(void) {           // the thread pointer belongs to this process
    u64 v = 0x1234567;
    __asm__ volatile("msr tpidr_el0, %0" :: "r"(v));
    S1(124, 0);                             // sched_yield: others run meanwhile
    u64 r;
    __asm__ volatile("mrs %0, tpidr_el0" : "=r"(r));
    return r == v;
}

static char bss_buf[100000];                // zeroed by the loader
static char data_str[] = "data segment";

static char *self;                  // argv[0]: where this program is (execve runs it again)

static void run(int argc, char **argv, char **envp, u64 *auxv) {
    self = argv[0];
    if (argc >= 3 && seq(argv[1], "exit")) {
        int n = 0;
        for (char *c = argv[2]; *c; c++) n = n * 10 + (*c - '0');
        exit_(n);
    }
    if (argc >= 2 && seq(argv[1], "crash")) {
        *(volatile int *)0x30000000 = 1;
        exit_(0);
    }
    if (argc >= 2 && seq(argv[1], "echo")) {
        for (int i = 2; i < argc; i++) {
            out(argv[i]);
            out(i + 1 < argc ? " " : "\n");
        }
        exit_(0);
    }
    out("Hello from a Linux program!\n");
    out("argc=");
    num(argc);
    out("\n");
    for (int i = 0; i < argc; i++) {
        out("argv[");
        num(i);
        out("]=");
        out(argv[i]);
        out("\n");
    }
    int envs = 0, path = 0;
    for (char **e = envp; *e; e++) {
        envs++;
        if ((*e)[0] == 'P' && (*e)[1] == 'A' && (*e)[2] == 'T' && (*e)[3] == 'H') path = 1;
    }
    check("environment", envs > 0 && path);
    u64 pagesz = 0, random = 0, entry = 0;
    for (u64 *a = auxv; a[0]; a += 2) {
        if (a[0] == 6) pagesz = a[1];
        if (a[0] == 25) random = a[1];
        if (a[0] == 9) entry = a[1];
    }
    check("auxv", pagesz == 4096 && random && entry);
    int zero = 1;
    for (int i = 0; i < (int)sizeof bss_buf; i++) zero &= bss_buf[i] == 0;
    check("bss is zero", zero);
    check("data segment", seq(data_str, "data segment"));

    // brk
    u64 b0 = (u64)S1(214, 0);
    u64 b1 = (u64)S1(214, b0 + 3 * 1024 * 1024);
    char *h = (char *)b0;
    h[0] = 'x';
    h[3 * 1024 * 1024 - 1] = 'y';
    check("brk grows", b1 == b0 + 3 * 1024 * 1024 && h[0] == 'x');

    // mmap / munmap
    char *m = (char *)S6(222, 0, 5 * 1024 * 1024, 3, 0x22, -1, 0);   // PROT_RW, MAP_PRIVATE|ANON
    int mz = (i64)m > 0;
    if (mz) {
        for (int i = 0; i < 5 * 1024 * 1024; i += 4096) mz &= m[i] == 0;
        m[0] = 1;
        m[5 * 1024 * 1024 - 1] = 2;
    }
    check("mmap anonymous", mz);
    check("munmap", S2(215, m, 5 * 1024 * 1024) == 0);

    // files
    int fd = (int)S4(56, -100, "lxtest.txt", 01 | 0100 | 01000, 0644);   // O_WRONLY|O_CREAT|O_TRUNC
    check("open for writing", fd >= 3);
    check("write", S3(64, fd, "line one\nline two\n", 18) == 18);
    check("close", S1(57, fd) == 0);
    fd = (int)S4(56, -100, "./lxtest.txt", 0, 0);
    char buf[64];
    i64 n = S3(63, fd, buf, sizeof buf);
    check("read back", n == 18 && buf[5] == 'o' && buf[17] == '\n');
    check("lseek", S3(62, fd, 5, 0) == 5 && S3(63, fd, buf, 3) == 3 && buf[0] == 'o');
    u64 st[16];
    check("fstat size", S2(80, fd, st) == 0 && st[6] == 18);
    S1(57, fd);
    check("open missing file", S4(56, -100, "no-such-file", 0, 0) == -2);
    // getdents64 on the current folder
    fd = (int)S4(56, -100, ".", 040000, 0);
    int found = 0;
    for (;;) {
        char d[512];
        i64 got = S3(61, fd, d, sizeof d);
        if (got <= 0) break;
        for (i64 o = 0; o < got;) {
            unsigned short rl = *(unsigned short *)(d + o + 16);
            if (seq(d + o + 19, "lxtest.txt")) found = 1;
            o += rl;
        }
    }
    S1(57, fd);
    check("getdents64 lists the file", found);
    check("unlink", S3(35, -100, "lxtest.txt", 0) == 0 && S4(56, -100, "lxtest.txt", 0, 0) == -2);

    // uname, time
    char u[65 * 6];
    check("uname", S1(160, u) == 0 && seq(u, "Linux") && seq(u + 65 * 4, "aarch64"));
    i64 t1[2], t2[2];
    S2(113, 1, t1);
    i64 req[2] = { 0, 50 * 1000 * 1000 };
    S2(101, req, 0);
    S2(113, 1, t2);
    i64 ms = (t2[0] - t1[0]) * 1000 + (t2[1] - t1[1]) / 1000000;
    check("nanosleep 50 ms", ms >= 45 && ms < 1000);
    check("thread pointer kept", tls_var_check());

    // fork + wait4
    int pid = (int)S6(220, 17, 0, 0, 0, 0, 0);                         // clone(SIGCHLD) = fork
    if (pid == 0) {
        data_str[0] = 'D';                  // the parent must not see this
        exit_(42);
    }
    int status = 0;
    int w = (int)S4(260, pid, &status, 0, 0);
    check("fork + wait4", pid > 0 && w == pid && (status >> 8) == 42 && data_str[0] == 'd');

    // pipe between parent and child
    int pfd[2];
    check("pipe2", S2(59, pfd, 0) == 0);
    pid = (int)S6(220, 17, 0, 0, 0, 0, 0);
    if (pid == 0) {
        S1(57, pfd[0]);
        for (int i = 0; i < 100; i++) S3(64, pfd[1], "0123456789", 10);
        exit_(0);
    }
    S1(57, pfd[1]);
    i64 total = 0;
    for (;;) {
        char pb[300];
        i64 got = S3(63, pfd[0], pb, sizeof pb);
        if (got <= 0) break;
        total += got;
    }
    S4(260, pid, &status, 0, 0);
    check("pipe carries 1000 bytes", total == 1000);

    // execve in a child
    pid = (int)S6(220, 17, 0, 0, 0, 0, 0);
    if (pid == 0) {
        char *av[] = { "lxtest", "echo", "exec", "works", 0 };
        char *ev[] = { "A=1", 0 };
        S3(221, self, av, ev);
        exit_(99);
    }
    S4(260, pid, &status, 0, 0);
    check("execve", (status >> 8) == 0);
    out("lxtest done\n");
    exit_(0);
}

void start_c(u64 *sp) {
    int argc = (int)sp[0];
    char **argv = (char **)(sp + 1);
    char **envp = argv + argc + 1;
    char **e = envp;
    while (*e) e++;
    run(argc, argv, envp, (u64 *)(e + 1));
}

__asm__(".globl _start\n_start:\n mov x0, sp\n bl start_c\n b .\n");
