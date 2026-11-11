// linux.c - running Linux programs on MyOS-ARM.
//
// A Linux program here is a static AArch64 ELF file (ET_EXEC, or a static
// PIE) in MyFS, e.g. BusyBox. It gets the same memory layout and start-up
// stack Linux would give it, and its 'svc #0' system calls are answered
// with Linux numbers and Linux structures (this file) instead of MyOS ones.
//
// Memory: the MMU maps programs in 2 MB blocks (proc.c), so a Linux process
// keeps a list of address ranges it owns (from the ELF file, brk, mmap and
// the stack) and gets a 2 MB block of RAM the first time it touches one.
//   0x00400000-0x03FFFFFF  the program (ELF segments) and its brk heap
//   0x40000000-0x7FFFFFFF  mmap() memory, the stack (8 MB at the top)
//
// Files: MyFS paths (v2 has folders; the current folder is proc->cwd).
// "/bin/X" (also /usr/bin, /sbin ...) that doesn't exist means "/X" (where
// pkg installs), or busybox if X is one of its applets. /dev/null, /dev/tty
// ... and a small /proc are made up here. Files opened for writing are kept
// in RAM and written to MyFS when the last descriptor closes.
// Processes: fork (a full copy of the memory), execve, wait4, pipes; enough
// for "busybox sh". Signals: handlers run on a Linux signal frame (rt_sigreturn
// comes back), delivered when the program returns from a system call or an
// interrupt; Ctrl+C sends SIGINT to the foreground job.
#include <stdint.h>
#include <string.h>
#include "broadcom/caches.h"
#include "console.h"
#include "linux.h"
#include "myfs.h"
#include "print.h"
#include "proc.h"
#include "timer.h"
#include "user.h"
#ifndef NO_USB
#include "rng.h"
#endif

extern uint64_t enter_user(uint64_t entry, uint64_t sp, uint64_t arg, uint64_t *ctx);
extern void user_resume(trapframe_t *f) __attribute__((noreturn));
extern void fp_save(uint64_t *state);
extern void fp_restore(uint64_t *state);
static void die_by_signal(proc_t *p, int sig) __attribute__((noreturn));
static int interrupted(proc_t *p);

#define PAGE        4096UL
#define BLOCK       PROC_BLOCK
#define ADDR_MASK   0x0000FFFFFFE00000ull
#define STACK_TOP   LX_HIGH_END
#define STACK_SIZE  (8UL << 20)
#define MMAP_TOP    (STACK_TOP - STACK_SIZE - BLOCK)
#define NVMA        64
#define NFD         32
#define NFILE       64
#define NPIPE       8
#define PIPE_SIZE   4096
#define MAX_ARGS    (64 * 1024)     // argv + envp strings for execve
#define MAX_FILE    (32UL << 20)    // largest file a program can write

#define PAGE_UP(a)   (((a) + PAGE - 1) & ~(PAGE - 1))
#define PAGE_DOWN(a) ((a) & ~(PAGE - 1))

// Linux error numbers
enum {
    EPERM = 1, ENOENT = 2, ESRCH = 3, EINTR = 4, EIO = 5, E2BIG = 7, ENOEXEC = 8, EBADF = 9,
    ECHILD = 10, EAGAIN = 11, ENOMEM = 12, EACCES = 13, EFAULT = 14, EBUSY = 16, EEXIST = 17,
    ENODEV = 19, ENOTDIR = 20, EISDIR = 21, EINVAL = 22, ENFILE = 23, EMFILE = 24, ENOTTY = 25, EFBIG = 27,
    ENOSPC = 28, ESPIPE = 29, EPIPE = 32, ERANGE = 34, ENAMETOOLONG = 36, ENOSYS = 38,
    ENOTEMPTY = 39,
};

// open() flags
#define O_ACCMODE   3
#define O_WRONLY    1
#define O_RDWR      2
#define O_CREAT     0100
#define O_EXCL      0200
#define O_TRUNC     01000
#define O_APPEND    02000
#define O_NONBLOCK  04000
#define O_DIRECTORY 040000
#define O_CLOEXEC   02000000
#define AT_FDCWD    (-100)
#define AT_EMPTY_PATH 0x1000
#define AT_REMOVEDIR  0x200

// signals
#define SIGINT  2
#define SIGKILL 9
#define SIGSEGV 11
#define SIGPIPE 13
#define SIGTERM 15
#define SIGCHLD 17
#define SIGSTOP 19
#define SIG_BIT(s) (1ull << ((s) - 1))
#define SA_NODEFER   0x40000000ull
#define SA_RESETHAND 0x80000000ull
#define SA_RESTART   0x10000000ull
#define SA_RESTORER  0x04000000ull
#define NSIG    65
#define SIG_DFL 0
#define SIG_IGN 1

// ---------------------------------------------------------------
//  per-process state
// ---------------------------------------------------------------
typedef struct { uint64_t start, end; } vma_t;
typedef struct { uint64_t handler, flags, restorer, mask; } sigact_t;

typedef struct {
    vma_t vma[NVMA];            // memory it owns (sorted, page aligned)
    int nvma;
    uint64_t brk_start, brk;
    uint64_t code_lo, code_hi;  // executable segments (caches to clean after fork)
    int8_t fd[NFD];             // file descriptor -> files[] (-1 = closed)
    uint32_t cloexec;           // one bit per descriptor
    sigact_t sig[NSIG];
    uint64_t sigmask;           // blocked signals (bit sig-1)
    uint64_t pending;           // signals waiting for their handler
    uint64_t saved_mask;        // rt_sigsuspend: the mask to go back to after the handler
    int restore_mask;
    char exe[MYFS_PATH_MAX];    // its MyFS file (for /proc/self/exe)
    uint64_t entry, sp;         // where it starts
    trapframe_t frame;          // fork: the child's first registers
    int term_sig;               // stopped by this signal (wait4 tells the parent)
    int execed;                 // vfork: the parent waits until the child calls execve
    int trace;                  // strace: print every system call
} lx_t;

static lx_t lxs[NPROC];
static uint64_t high_l2[NPROC][512] __attribute__((aligned(4096)));
static uint64_t start_ctx[13];

#define LX(p) (&lxs[(p)->slot])

// ---------------------------------------------------------------
//  open files, pipes, terminals (shared by all Linux processes)
// ---------------------------------------------------------------
enum { FT_TTY = 1, FT_FILE, FT_DIR, FT_NULL, FT_ZERO, FT_RANDOM, FT_PIPE_R, FT_PIPE_W,
       FT_PROC, FT_PROCDIR };      // /proc: a text made when opened, a /proc folder (pid 0 = /proc)

typedef struct {
    int refs, type, flags;
    char name[MYFS_PATH_MAX];   // FT_FILE, FT_DIR: its MyFS path
    uint64_t off;               // file position (FT_DIR: entry number)
    uint64_t size;              // FT_FILE: its size
    uint8_t *buf;               // FT_FILE opened for writing: the whole file in RAM
    int nblocks;
    int dirty;
    int pipe;
    int pid;                    // FT_PROCDIR: whose folder (0 = /proc itself)
    int pbuf;                   // FT_PROC: its text in procbufs[] (-1 = none)
} file_t;

typedef struct {
    int used, readers, writers;
    uint32_t r, w;              // read / write counters (bytes in the pipe = w - r)
    uint8_t data[PIPE_SIZE];
} pipe_t;

static file_t files[NFILE];
static pipe_t pipes[NPIPE];
#define NPROCBUF 8
static char procbufs[NPROCBUF][4096];
static uint8_t procbuf_used[NPROCBUF];

// terminal settings (struct termios, Linux kernel layout) per terminal
typedef struct {
    uint32_t iflag, oflag, cflag, lflag;
    uint8_t line, cc[19];
} ktermios_t;

#define ICRNL  0x100
#define ISIG   0x1
#define ICANON 0x2
#define ECHO   0x8
#define ECHOE  0x10
#define VINTR 0
#define VERASE 2
#define VKILL 3
#define VEOF 4
#define VTIME 5
#define VMIN 6

static ktermios_t tios[NTTY];
static uint16_t winsize[NTTY][4];

typedef struct {                // keys that arrived for Linux programs
    uint8_t q[64];
    unsigned head, tail;
    char line[256];             // canonical mode: the line being typed
    int len, pos, ready;
} ttyin_t;
static ttyin_t ttyin[NTTY];

static const ktermios_t default_tios = {
    .iflag = 0x500,             // ICRNL IXON
    .oflag = 0x5,               // OPOST ONLCR
    .cflag = 0xBF,              // B38400 CS8 CREAD
    .lflag = 0x8A3B,            // ISIG ICANON ECHO ECHOE ECHOK IEXTEN ECHOCTL ECHOKE
    .cc = { 3, 28, 127, 21, 4, 0, 1, 0, 17, 19, 26, 0, 18, 15, 23, 22, 0 },
};

static void tty_reset(int t) {
    tios[t] = default_tios;
    winsize[t][0] = 24;
    winsize[t][1] = 80;
    winsize[t][2] = winsize[t][3] = 0;
    ttyin[t].head = ttyin[t].tail = 0;
    ttyin[t].len = ttyin[t].pos = ttyin[t].ready = 0;
}

static uint64_t rt_offset;      // seconds to add to the uptime for the wall clock (settimeofday)

// ---------------------------------------------------------------
//  small helpers
// ---------------------------------------------------------------
static void put_hex(uint64_t v) {
    int d = 1;
    while (d < 16 && (v >> (d * 4))) d++;
    console_puts("0x");
    print_hex(v, d);
}

static uint64_t mrs_tpidr(void) {
    uint64_t v;
    __asm__ volatile ("mrs %0, tpidr_el0" : "=r"(v));
    return v;
}

static void random_bytes(void *out, size_t n) {
#ifndef NO_USB
    rng_bytes(out, n);
#else
    static uint64_t s = 0x9E3779B97F4A7C15ull;
    uint8_t *o = out;
    for (size_t i = 0; i < n; i++) {
        s ^= timer_count();
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        o[i] = (uint8_t)(s >> 24);
    }
#endif
}

static uint64_t now_ns(void) {
    uint64_t c = timer_count(), f = timer_freq();
    return (c / f) * 1000000000ull + (c % f) * 1000000000ull / f;
}

// ---------------------------------------------------------------
//  memory
// ---------------------------------------------------------------
static uint64_t *pte(proc_t *p, uint64_t va) {
    if (va >= LX_LOW_START && va < LX_LOW_END) return &p->l2[va / BLOCK];
    if (va >= LX_HIGH_START && va < LX_HIGH_END) return &high_l2[p->slot][(va - LX_HIGH_START) / BLOCK];
    return 0;
}

static int in_window(uint64_t s, uint64_t e) {
    return s < e && ((s >= LX_LOW_START && e <= LX_LOW_END) || (s >= LX_HIGH_START && e <= LX_HIGH_END));
}

// physical address behind va (0 = nothing there yet)
static uint8_t *phys(proc_t *p, uint64_t va) {
    uint64_t *e = pte(p, va);
    if (!e || !(*e & 1)) return 0;
    return (uint8_t *)((*e & ADDR_MASK) + (va & (BLOCK - 1)));
}

// ---------------------------------------------------------------
//  a copy of the last program file in RAM: MyFS reads 512 bytes at a time,
//  so loading BusyBox (1.1 MB) for every command of a shell pipeline would
//  be slow on the Pi. The copy is checked against the file (size + 8
//  sampled pieces) every time, and dropped when memory runs out.
// ---------------------------------------------------------------
#define ECACHE_MAX (8UL << 20)
#define ECACHE_PROBES 8
static struct {
    char path[MYFS_PATH_MAX];
    int32_t size;
    uint8_t *mem;
    int nblocks;
    uint8_t probe[ECACHE_PROBES][32];
} ecache;

static void ecache_drop(void) {
    if (ecache.mem) proc_mem_free((uint64_t)ecache.mem, ecache.nblocks);
    ecache.mem = 0;
    ecache.path[0] = 0;
}

static uint64_t lx_mem_alloc(int n) {              // pool blocks; the cache goes first if short
    uint64_t m = proc_mem_alloc(n);
    if (!m && ecache.mem) {
        ecache_drop();
        m = proc_mem_alloc(n);
    }
    return m;
}

static uint32_t probe_off(int i, int32_t size) {
    uint64_t o = (uint64_t)(uint32_t)size * (uint64_t)i / ECACHE_PROBES;
    return (uint32_t)(o + 32 > (uint64_t)(uint32_t)size ? (size > 32 ? size - 32 : 0) : o);
}

// 1 if ecache holds file now (loading it if it fits)
static int ecache_get(const char *file) {
    int32_t size = myfs_size(file);
    if (size <= 0 || (uint64_t)size > ECACHE_MAX) return 0;
    uint32_t n = size < 32 ? (uint32_t)size : 32;
    if (ecache.mem && ecache.size == size && !strcmp(ecache.path, file)) {
        uint8_t b[32];
        int same = 1;
        for (int i = 0; i < ECACHE_PROBES && same; i++)
            same = myfs_read(file, probe_off(i, size), b, n) == (int)n && !memcmp(b, ecache.probe[i], n);
        if (same) return 1;
    }
    ecache_drop();
    int nb = (int)(((uint64_t)size + BLOCK - 1) / BLOCK);
    uint64_t m = proc_mem_alloc(nb);
    if (!m) return 0;
    if (myfs_read(file, 0, (void *)m, (uint32_t)size) != size) {
        proc_mem_free(m, nb);
        return 0;
    }
    ecache.mem = (uint8_t *)m;
    ecache.nblocks = nb;
    ecache.size = size;
    strcpy(ecache.path, file);
    for (int i = 0; i < ECACHE_PROBES; i++) memcpy(ecache.probe[i], ecache.mem + probe_off(i, size), n);
    return 1;
}

static int map_block(proc_t *p, uint64_t va) {
    uint64_t *e = pte(p, va);
    if (!e) return 0;
    if (*e & 1) return 1;
    uint64_t m = lx_mem_alloc(1);
    if (!m) return 0;
    *e = proc_user_desc(m);
    data_clean_and_invalidate(e, 8);
    return 1;
}

static void unmap_block(proc_t *p, uint64_t va) {
    uint64_t *e = pte(p, va);
    if (!e || !(*e & 1)) return;
    uint64_t m = *e & ADDR_MASK;
    *e = 0;
    data_clean_and_invalidate(e, 8);
    if (proc_is_pool(m)) proc_mem_free(m, 1);
}

static uint64_t brk_end(lx_t *x) { return x->brk > x->brk_start ? PAGE_UP(x->brk) : x->brk_start; }

// end of the owned range that contains a (0 = a isn't the program's)
static uint64_t owned_until(lx_t *x, uint64_t a) {
    for (int i = 0; i < x->nvma; i++)
        if (a >= x->vma[i].start && a < x->vma[i].end) return x->vma[i].end;
    if (a >= x->brk_start && a < brk_end(x)) return brk_end(x);
    return 0;
}

static int block_in_use(lx_t *x, uint64_t b) {
    for (int i = 0; i < x->nvma; i++)
        if (x->vma[i].start < b + BLOCK && x->vma[i].end > b) return 1;
    return x->brk_start < b + BLOCK && brk_end(x) > b;
}

// blocks in [s, e) that nothing owns any more go back to the pool
static void release_blocks(proc_t *p, uint64_t s, uint64_t e) {
    lx_t *x = LX(p);
    int any = 0;
    for (uint64_t b = s & ~(BLOCK - 1); b < e; b += BLOCK) {
        if (!block_in_use(x, b) && phys(p, b)) {
            unmap_block(p, b);
            any = 1;
        }
    }
    if (any) proc_flush_tlb();
}

static void zero_range(proc_t *p, uint64_t s, uint64_t e) {
    while (s < e) {
        uint64_t n = BLOCK - (s & (BLOCK - 1));
        if (n > e - s) n = e - s;
        uint8_t *m = phys(p, s);
        if (m) memset(m, 0, n);
        s += n;
    }
}

static void vma_cut(lx_t *x, uint64_t s, uint64_t e) {
    vma_t out[NVMA];
    int n = 0;
    for (int i = 0; i < x->nvma; i++) {
        vma_t v = x->vma[i];
        if (v.end <= s || v.start >= e) {
            out[n++] = v;
            continue;
        }
        if (v.start < s && n < NVMA) out[n++] = (vma_t){ v.start, s };
        if (v.end > e && n < NVMA) out[n++] = (vma_t){ e, v.end };
    }
    memcpy(x->vma, out, sizeof(vma_t) * (size_t)n);
    x->nvma = n;
}

// own [s, e) (merged with what it touches); 0 = too many ranges
static int vma_add(lx_t *x, uint64_t s, uint64_t e) {
    int i = 0;
    while (i < x->nvma) {                   // swallow every range that overlaps or touches
        vma_t v = x->vma[i];
        if (v.end >= s && v.start <= e) {
            if (v.start < s) s = v.start;
            if (v.end > e) e = v.end;
            memmove(&x->vma[i], &x->vma[i + 1], sizeof(vma_t) * (size_t)(x->nvma - i - 1));
            x->nvma--;
        } else {
            i++;
        }
    }
    if (x->nvma == NVMA) return 0;
    i = 0;
    while (i < x->nvma && x->vma[i].start < s) i++;
    memmove(&x->vma[i + 1], &x->vma[i], sizeof(vma_t) * (size_t)(x->nvma - i));
    x->vma[i] = (vma_t){ s, e };
    x->nvma++;
    return 1;
}

static int range_free(lx_t *x, uint64_t s, uint64_t e) {
    for (int i = 0; i < x->nvma; i++)
        if (x->vma[i].start < e && x->vma[i].end > s) return 0;
    return !(x->brk_start < e && brk_end(x) > s);
}

// a free hole of len bytes for mmap, searched from the top down
static uint64_t find_gap(lx_t *x, uint64_t len) {
    uint64_t top = MMAP_TOP;
    for (int i = x->nvma - 1; i >= 0; i--) {
        vma_t v = x->vma[i];
        if (v.start >= top) continue;
        uint64_t lo = v.end < LX_HIGH_START ? LX_HIGH_START : v.end;
        if (v.end <= top && top >= lo && top - lo >= len) return top - len;
        top = v.start;
        if (top <= LX_HIGH_START) return 0;
    }
    if (top >= LX_HIGH_START + len) return top - len;
    return 0;
}

// is [a, a+len) the program's memory? (blocks get mapped now, so the kernel can touch it)
static int uok(proc_t *p, uint64_t a, uint64_t len) {
    if (len == 0) return 1;
    lx_t *x = LX(p);
    uint64_t e = a + len;
    if (e < a) return 0;
    for (uint64_t c = a; c < e;) {
        uint64_t u = owned_until(x, c);
        if (!u) return 0;
        c = u;
    }
    for (uint64_t b = a & ~(BLOCK - 1); b < e; b += BLOCK)
        if (!map_block(p, b)) return 0;
    return 1;
}

// copy a string from the program; its length, or -EFAULT / -ENAMETOOLONG
static int ustr(proc_t *p, uint64_t a, char *out, int max) {
    for (int i = 0; i < max; i++) {
        if ((i == 0 || ((a + i) & (PAGE - 1)) == 0) && !uok(p, a + i, 1)) return -EFAULT;
        out[i] = *(const char *)(a + i);
        if (!out[i]) return i;
    }
    return -ENAMETOOLONG;
}

static void unmap_all(proc_t *p) {
    for (uint64_t va = LX_LOW_START; va < LX_LOW_END; va += BLOCK) unmap_block(p, va);
    for (uint64_t va = LX_HIGH_START; va < LX_HIGH_END; va += BLOCK) unmap_block(p, va);
    proc_flush_tlb();
}

uint32_t linux_used_blocks(const proc_t *p) {
    uint32_t n = 0;
    for (uint64_t i = LX_LOW_START / BLOCK; i < LX_LOW_END / BLOCK; i++) n += p->l2[i] & 1;
    for (int i = 0; i < 512; i++) n += high_l2[p->slot][i] & 1;
    return n;
}

int linux_fault(uint64_t addr, uint64_t esr) {
    uint32_t fsc = (uint32_t)(esr & 0x3F);
    if (fsc < 0x04 || fsc > 0x07) return 0;     // only "nothing mapped here"
    proc_t *p = proc_current();
    if (!owned_until(LX(p), addr)) return 0;
    if (!map_block(p, addr & ~(BLOCK - 1))) {
        console_puts("\n  (out of memory)");
        return 0;
    }
    proc_flush_tlb();
    return 1;
}

// a fresh process: the kernel map, nothing in the program windows
static void lx_setup(proc_t *p) {
    memset(LX(p), 0, sizeof(lx_t));
    memset(high_l2[p->slot], 0, sizeof high_l2[0]);
    for (uint64_t i = LX_LOW_START / BLOCK; i < LX_LOW_END / BLOCK; i++) p->l2[i] = 0;
    p->l1[1] = (uint64_t)high_l2[p->slot] | 3;  // table descriptor for the second GB
    data_clean_and_invalidate(high_l2[p->slot], 4096);
    p->linux = 1;
    for (int i = 0; i < NFD; i++) LX(p)->fd[i] = -1;
}

void linux_free(proc_t *p) {
    for (uint64_t va = LX_LOW_START; va < LX_LOW_END; va += BLOCK) unmap_block(p, va);
    for (uint64_t va = LX_HIGH_START; va < LX_HIGH_END; va += BLOCK) unmap_block(p, va);
}

// ---------------------------------------------------------------
//  files
// ---------------------------------------------------------------
static int file_new(int type, int flags) {
    for (int i = 0; i < NFILE; i++) {
        if (files[i].refs == 0) {
            memset(&files[i], 0, sizeof files[i]);
            files[i].refs = 1;
            files[i].type = type;
            files[i].flags = flags;
            files[i].pbuf = -1;
            return i;
        }
    }
    return -ENFILE;
}

// writable file: room for need bytes in its RAM copy
static int file_reserve(file_t *f, uint64_t need) {
    if (need <= (uint64_t)f->nblocks * BLOCK) return 0;
    if (need > MAX_FILE) return -EFBIG;
    int n = (int)((need + BLOCK - 1) / BLOCK);
    if (n < f->nblocks * 2) n = f->nblocks * 2;
    if ((uint64_t)n * BLOCK > MAX_FILE) n = (int)(MAX_FILE / BLOCK);
    uint64_t m = lx_mem_alloc(n);
    if (!m) return -ENOSPC;
    if (f->buf) {
        memcpy((void *)m, f->buf, f->size);
        proc_mem_free((uint64_t)f->buf, f->nblocks);
    }
    f->buf = (uint8_t *)m;
    f->nblocks = n;
    return 0;
}

static int pipe_close_end(int i, int writer);

static int file_put(int i) {
    file_t *f = &files[i];
    if (--f->refs > 0) return 0;
    int r = 0;
    if (f->type == FT_FILE && f->buf) {
        if (f->dirty && !strcmp(f->name, ecache.path)) ecache_drop();
        if (f->dirty && myfs_write(f->name, f->buf, (uint32_t)f->size)) r = -ENOSPC;
        proc_mem_free((uint64_t)f->buf, f->nblocks);
        f->buf = 0;
    } else if (f->type == FT_PIPE_R || f->type == FT_PIPE_W) {
        pipe_close_end(f->pipe, f->type == FT_PIPE_W);
    } else if (f->pbuf >= 0) {
        procbuf_used[f->pbuf] = 0;
        f->pbuf = -1;
    }
    f->type = 0;
    return r;
}

static file_t *fget(lx_t *x, int64_t fd) {
    if (fd < 0 || fd >= NFD || x->fd[fd] < 0) return 0;
    return &files[(int)x->fd[fd]];
}

static int fd_alloc(lx_t *x, int min) {
    for (int i = min; i < NFD; i++)
        if (x->fd[i] < 0) return i;
    return -EMFILE;
}

static int fd_close(lx_t *x, int fd) {
    if (fd < 0 || fd >= NFD || x->fd[fd] < 0) return -EBADF;
    int f = x->fd[fd];
    x->fd[fd] = -1;
    x->cloexec &= ~(1u << fd);
    return file_put(f);
}

// --- paths: MyFS is flat, "/" is the only directory
enum { R_FILE = 1, R_DIR, R_NULL, R_ZERO, R_TTY, R_RANDOM, R_FD, R_PROCDIR, R_PROCFILE };

static int resolve_proc(const char *rest, char *name, int *arg);
static int strchr_(const char *s, char c);
static int applet_lookup(const char *name);

static int fs_err(int e) {                          // MyFS error -> Linux errno
    switch (e) {
        case MYFS_ERR_NOTFOUND: return -ENOENT;
        case MYFS_ERR_FULL: return -ENOSPC;
        case MYFS_ERR_NAME: return -EINVAL;
        case MYFS_ERR_EXISTS: return -EEXIST;
        case MYFS_ERR_NOTDIR: return -ENOTDIR;
        case MYFS_ERR_ISDIR: return -EISDIR;
        case MYFS_ERR_NOTEMPTY: return -ENOTEMPTY;
        case MYFS_ERR_V1: return -EPERM;
    }
    return -EIO;
}

static const char *const bin_dirs[] = { "/bin/", "/usr/bin/", "/sbin/", "/usr/sbin/", "/usr/local/bin/" };

// path -> what it means: name gets the MyFS path (R_FILE, R_DIR) or *fdno the
// descriptor (R_FD) / the /proc entry (R_PROCDIR, R_PROCFILE)
static int resolve(const char *path, char *name, int *fdno) {
    if (!*path) return -ENOENT;
    char abs[MYFS_PATH_MAX];
    if (myfs_normalize(proc_cwd(), path, abs)) return -ENAMETOOLONG;
    if (!strcmp(abs, "/dev/null")) return R_NULL;
    if (!strcmp(abs, "/dev/zero")) return R_ZERO;
    if (!strcmp(abs, "/dev/tty") || !strcmp(abs, "/dev/console")) return R_TTY;
    if (!strcmp(abs, "/dev/random") || !strcmp(abs, "/dev/urandom")) return R_RANDOM;
    if (!strcmp(abs, "/dev/stdin") || !strcmp(abs, "/proc/self/fd/0")) { *fdno = 0; return R_FD; }
    if (!strcmp(abs, "/dev/stdout") || !strcmp(abs, "/proc/self/fd/1")) { *fdno = 1; return R_FD; }
    if (!strcmp(abs, "/dev/stderr") || !strcmp(abs, "/proc/self/fd/2")) { *fdno = 2; return R_FD; }
    if (!strcmp(abs, "/proc/self/exe")) {
        strcpy(name, LX(proc_current())->exe);
        return R_FILE;
    }
    if (!memcmp(abs, "/proc", 5) && (!abs[5] || abs[5] == '/')) return resolve_proc(abs + 5, name, fdno);
    strcpy(name, abs);
    int t = myfs_stat(abs, 0);
    if (t == MYFS_DIR) return R_DIR;
    if (t == MYFS_FILE) return R_FILE;
    for (unsigned i = 0; i < sizeof bin_dirs / sizeof bin_dirs[0]; i++) {   // "/bin/ls" -> "/ls", busybox
        size_t k = strlen(bin_dirs[i]);
        if (strncmp(abs, bin_dirs[i], k) || strchr_(abs + k, '/')) continue;
        char top[MYFS_PATH_MAX];
        top[0] = '/';
        strcpy(top + 1, abs + k);
        if (myfs_stat(top, 0) == MYFS_FILE) strcpy(name, top);
        else if (applet_lookup(abs + k)) strcpy(name, "/busybox");
        return R_FILE;
    }
    if (!strcmp(abs, "/tmp") || !strncmp(abs, "/tmp/", 5)) {    // no /tmp yet: make it (or use "/" on v1)
        if (myfs_version() == 2) myfs_mkdir("/tmp");
        else if (abs[4]) memmove(name, abs + 4, strlen(abs + 4) + 1);
        else strcpy(name, "/");
        if (myfs_stat(name, 0) == MYFS_DIR) return R_DIR;
    }
    if (!myfs_valid_name(name)) return t == MYFS_ERR_NOTDIR ? -ENOTDIR : -ENOENT;
    return R_FILE;                                  // (may not exist yet: open with O_CREAT)
}

static int strchr_(const char *s, char c) {
    for (; *s; s++)
        if (*s == c) return 1;
    return 0;
}

static int upath(proc_t *p, uint64_t a, char *name, int *fdno) {
    char path[256];
    int n = ustr(p, a, path, sizeof path);
    if (n < 0) return n;
    return resolve(path, name, fdno);
}

static uint64_t path_ino(const char *path) {      // an inode number: a hash of the path
    uint64_t h = 1469598103934665603ull;
    for (; *path; path++) h = (h ^ (uint8_t)*path) * 1099511628211ull;
    return (h >> 1) | 16;
}

static int is_program(const char *name) {
    uint8_t h[4];
    if (myfs_read(name, 0, h, 4) != 4) return 0;
    return !memcmp(h, "\177ELF", 4) || !memcmp(h, "MYEX", 4);
}

int linux_is_elf(const char *name) {
    uint8_t h[4];
    return myfs_read(name, 0, h, 4) == 4 && !memcmp(h, "\177ELF", 4);
}

// struct stat (AArch64 Linux)
typedef struct {
    uint64_t dev, ino;
    uint32_t mode, nlink, uid, gid;
    uint64_t rdev, pad1;
    int64_t size;
    int32_t blksize, pad2;
    int64_t blocks;
    int64_t atime, atime_ns, mtime, mtime_ns, ctime, ctime_ns;
    uint32_t unused[2];
} kstat_t;

#define S_IFCHR 0020000
#define S_IFDIR 0040000
#define S_IFREG 0100000
#define S_IFIFO 0010000

static void stat_fill(kstat_t *st, uint32_t mode, uint64_t size, uint64_t ino, uint64_t rdev) {
    memset(st, 0, sizeof *st);
    st->dev = 0x801;
    st->ino = ino;
    st->mode = mode;
    st->nlink = (mode & S_IFDIR) ? 2 : 1;
    st->rdev = rdev;
    st->size = (int64_t)size;
    st->blksize = 4096;
    st->blocks = (int64_t)((size + 511) / 512);
    st->atime = st->mtime = st->ctime = (int64_t)rt_offset;
}

static int stat_kind(int kind, const char *name, kstat_t *st) {
    switch (kind) {
        case R_DIR:
            if (myfs_stat(name, 0) != MYFS_DIR) return -ENOENT;
            stat_fill(st, S_IFDIR | 0755, 4096, strcmp(name, "/") ? path_ino(name) : 2, 0);
            return 0;
        case R_FILE: {
            int32_t size = myfs_size(name);
            if (size < 0) return -ENOENT;
            stat_fill(st, S_IFREG | (is_program(name) ? 0755 : 0644), (uint64_t)size,
                      path_ino(name), 0);
            return 0;
        }
        case R_NULL:   stat_fill(st, S_IFCHR | 0666, 0, 100, 0x103); return 0;
        case R_ZERO:   stat_fill(st, S_IFCHR | 0666, 0, 101, 0x105); return 0;
        case R_RANDOM: stat_fill(st, S_IFCHR | 0666, 0, 102, 0x109); return 0;
        case R_TTY:    stat_fill(st, S_IFCHR | 0620, 0, 103, 0x8800); return 0;
        case R_PROCDIR:  stat_fill(st, S_IFDIR | 0555, 0, 1000, 0); st->dev = 4; return 0;
        case R_PROCFILE: stat_fill(st, S_IFREG | 0444, 0, 1001, 0); st->dev = 4; return 0;
    }
    return -ENOENT;
}

static int stat_file(file_t *f, kstat_t *st) {
    switch (f->type) {
        case FT_TTY: return stat_kind(R_TTY, 0, st);
        case FT_NULL: return stat_kind(R_NULL, 0, st);
        case FT_ZERO: return stat_kind(R_ZERO, 0, st);
        case FT_RANDOM: return stat_kind(R_RANDOM, 0, st);
        case FT_DIR: return stat_kind(R_DIR, f->name, st);
        case FT_PROCDIR: return stat_kind(R_PROCDIR, 0, st);
        case FT_PROC: return stat_kind(R_PROCFILE, 0, st);
        case FT_PIPE_R: case FT_PIPE_W:
            stat_fill(st, S_IFIFO | 0600, 0, 200 + (uint64_t)f->pipe, 0);
            return 0;
        case FT_FILE:
            stat_fill(st, S_IFREG | (is_program(f->name) ? 0755 : 0644), f->size,
                      path_ino(f->name), 0);
            return 0;
    }
    return -EBADF;
}

// ---------------------------------------------------------------
//  /proc: just enough for ps, top, free, df, kill, pidof
// ---------------------------------------------------------------
enum { PF_MEMINFO = 1, PF_UPTIME, PF_LOADAVG, PF_STAT, PF_CPUINFO, PF_VERSION, PF_MOUNTS,
       PF_PSTAT, PF_PSTATUS, PF_PCMDLINE, PF_PCOMM, PF_COUNT };
static const char *const proc_names[] = { 0, "meminfo", "uptime", "loadavg", "stat", "cpuinfo",
                                          "version", "mounts", "stat", "status", "cmdline", "comm" };

static proc_t *proc_visible(int pid) {             // a process /proc shows (not the serial shell, 0)
    proc_t *q = pid > 0 ? proc_by_pid(pid) : 0;
    return q && q->state != P_FREE ? q : 0;
}

// rest = what follows "/proc"; *arg = pid * 16 + file (R_PROCFILE) or pid (R_PROCDIR)
static int resolve_proc(const char *rest, char *name, int *arg) {
    if (!*rest) {
        *arg = 0;
        return R_PROCDIR;
    }
    rest++;                                         // the '/'
    int pid = -1;
    if (!memcmp(rest, "self", 4) && (!rest[4] || rest[4] == '/')) {
        pid = proc_current()->pid;
        rest += 4;
    } else if (*rest >= '0' && *rest <= '9') {
        pid = 0;
        while (*rest >= '0' && *rest <= '9') pid = pid * 10 + (*rest++ - '0');
        if (*rest && *rest != '/') return -ENOENT;
    }
    if (pid < 0) {                                  // a file in /proc itself
        for (int i = PF_MEMINFO; i <= PF_MOUNTS; i++) {
            if (!strcmp(rest, proc_names[i])) {
                *arg = i;
                return R_PROCFILE;
            }
        }
        return -ENOENT;
    }
    proc_t *q = proc_visible(pid);
    if (!q) return -ENOENT;
    if (!*rest) {
        *arg = pid;
        return R_PROCDIR;
    }
    rest++;
    if (!strcmp(rest, "exe") && q->linux) {
        strcpy(name, LX(q)->exe);
        return R_FILE;
    }
    for (int i = PF_PSTAT; i < PF_COUNT; i++) {
        if (!strcmp(rest, proc_names[i])) {
            *arg = pid * 16 + i;
            return R_PROCFILE;
        }
    }
    return -ENOENT;
}

// the index-th entry of a /proc folder; 0 = no more
static int proc_entry(int pid, int index, char *name, int *type) {
    if (pid) {                                      // /proc/PID
        if (index >= PF_COUNT - PF_PSTAT) return 0;
        strcpy(name, proc_names[PF_PSTAT + index]);
        *type = 8;
        return 1;
    }
    if (index == 0) {
        strcpy(name, "self");
        *type = 10;                                 // DT_LNK
        return 1;
    }
    if (index <= PF_MOUNTS) {
        strcpy(name, proc_names[index]);
        *type = 8;
        return 1;
    }
    for (int i = 1, k = index - PF_MOUNTS - 1; i < NPROC; i++) {
        proc_t *q = proc_slot(i);
        if (q->state == P_FREE || k-- > 0) continue;
        char d[12];
        int n = 0, v = q->pid;
        do { d[n++] = (char)('0' + v % 10); v /= 10; } while (v);
        for (int j = 0; j < n; j++) name[j] = d[n - 1 - j];
        name[n] = 0;
        *type = 4;
        return 1;
    }
    return 0;
}

typedef struct { char *b; int n, cap; } tbuf_t;

static void tb_s(tbuf_t *t, const char *s) {
    while (*s && t->n < t->cap - 1) t->b[t->n++] = *s++;
}

static void tb_u(tbuf_t *t, uint64_t v) {
    char d[21];
    int n = 0;
    do { d[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n && t->n < t->cap - 1) t->b[t->n++] = d[--n];
}

static void tb_frac(tbuf_t *t, uint64_t hundredths) {      // 12.34
    tb_u(t, hundredths / 100);
    tb_s(t, hundredths % 100 < 10 ? ".0" : ".");
    tb_u(t, hundredths % 100);
}

static void tb_kv(tbuf_t *t, const char *key, uint64_t kb) {
    tb_s(t, key);
    tb_u(t, kb);
    tb_s(t, " kB\n");
}

static int proc_parent(proc_t *q) {
    return q->linux && q->parent > 0 ? q->parent : 0;
}

static int proc_text(int arg, char *out, int cap) {
    tbuf_t tb = { out, 0, cap }, *t = &tb;
    int file = arg & 15, pid = arg >> 4;
    uint64_t total_kb = (uint64_t)proc_total_blocks() * 2048, free_kb = (uint64_t)proc_free_blocks() * 2048;
    uint64_t idle = proc_idle_ticks(), busy = ticks > idle ? ticks - idle : 0;
    int nprocs = 0;
    for (int i = 0; i < NPROC; i++) nprocs += proc_slot(i)->state != P_FREE;
    proc_t *q = proc_visible(pid);
    switch (file) {
        case PF_MEMINFO:
            tb_kv(t, "MemTotal:       ", total_kb);
            tb_kv(t, "MemFree:        ", free_kb);
            tb_kv(t, "MemAvailable:   ", free_kb);
            tb_kv(t, "Buffers:        ", 0);
            tb_kv(t, "Cached:         ", 0);
            tb_kv(t, "SwapCached:     ", 0);
            tb_kv(t, "SwapTotal:      ", 0);
            tb_kv(t, "SwapFree:       ", 0);
            tb_kv(t, "Shmem:          ", 0);
            tb_kv(t, "SReclaimable:   ", 0);
            break;
        case PF_UPTIME:
            tb_frac(t, ticks * 100 / TICKS_PER_SECOND);
            tb_s(t, " ");
            tb_frac(t, idle * 100 / TICKS_PER_SECOND);
            tb_s(t, "\n");
            break;
        case PF_LOADAVG:
            tb_s(t, "0.00 0.00 0.00 1/");
            tb_u(t, (uint64_t)nprocs);
            tb_s(t, " ");
            tb_u(t, (uint64_t)nprocs);
            tb_s(t, "\n");
            break;
        case PF_STAT:
            for (int c = 0; c < 2; c++) {
                tb_s(t, c ? "cpu0 " : "cpu  ");
                tb_u(t, busy);
                tb_s(t, " 0 0 ");
                tb_u(t, idle);
                tb_s(t, " 0 0 0 0 0 0\n");
            }
            tb_s(t, "intr 0\nctxt 0\nbtime ");
            tb_u(t, rt_offset);
            tb_s(t, "\nprocesses ");
            tb_u(t, (uint64_t)nprocs);
            tb_s(t, "\nprocs_running 1\nprocs_blocked 0\n");
            break;
        case PF_CPUINFO:
            tb_s(t, "processor\t: 0\nBogoMIPS\t: 38.40\nFeatures\t: fp asimd evtstrm crc32 cpuid\n"
                    "CPU implementer\t: 0x41\nCPU architecture: 8\nCPU variant\t: 0x0\nCPU part\t: 0xd03\n"
                    "CPU revision\t: 4\n\nHardware\t: BCM2835\nModel\t\t: Raspberry Pi (MyOS)\n");
            break;
        case PF_VERSION:
            tb_s(t, "Linux version 6.1.0-myos (MyOS for Raspberry Pi) #1\n");
            break;
        case PF_MOUNTS:
            tb_s(t, "myfs / myfs rw 0 0\nproc /proc proc rw 0 0\n");
            break;
        case PF_PSTAT: {                            // pid (comm) state ppid pgrp session tty ...
            if (!q) break;
            uint64_t blocks = proc_used_blocks(q);
            tb_u(t, (uint64_t)q->pid);
            tb_s(t, " (");
            tb_s(t, q->name);
            tb_s(t, q->state == P_ZOMBIE ? ") Z " : q->waiting || q->kernel ? ") S " : ") R ");
            tb_u(t, (uint64_t)proc_parent(q));
            tb_s(t, " ");
            tb_u(t, (uint64_t)q->pid);
            tb_s(t, " ");
            tb_u(t, (uint64_t)q->pid);
            tb_s(t, " ");
            tb_u(t, 0x8800 + (uint64_t)q->tty);     // tty_nr
            tb_s(t, " 0 0 0 0 0 0 ");               // tpgid flags minflt cminflt majflt cmajflt
            tb_u(t, q->cpu_ticks);                  // utime
            tb_s(t, " 0 0 0 20 0 1 0 ");            // stime cutime cstime priority nice threads 0
            tb_u(t, q->started);                    // starttime
            tb_s(t, " ");
            tb_u(t, blocks * BLOCK);                // vsize
            tb_s(t, " ");
            tb_u(t, blocks * (BLOCK / PAGE));       // rss (pages)
            tb_s(t, " 18446744073709551615 0 0 0 0 0 0 0 0 0 0 0 0 17 0 0 0 0 0 0\n");
            break;
        }
        case PF_PSTATUS:
            if (!q) break;
            tb_s(t, "Name:\t");
            tb_s(t, q->name);
            tb_s(t, q->state == P_ZOMBIE ? "\nState:\tZ (zombie)\nTgid:\t" : q->waiting || q->kernel
                    ? "\nState:\tS (sleeping)\nTgid:\t" : "\nState:\tR (running)\nTgid:\t");
            tb_u(t, (uint64_t)q->pid);
            tb_s(t, "\nPid:\t");
            tb_u(t, (uint64_t)q->pid);
            tb_s(t, "\nPPid:\t");
            tb_u(t, (uint64_t)proc_parent(q));
            tb_s(t, "\nUid:\t0\t0\t0\t0\nGid:\t0\t0\t0\t0\n");
            tb_kv(t, "VmSize:\t", proc_used_blocks(q) * 2048);
            tb_kv(t, "VmRSS:\t", proc_used_blocks(q) * 2048);
            tb_s(t, "Threads:\t1\n");
            break;
        case PF_PCMDLINE:
            if (!q || q->kernel) break;
            tb_s(t, q->name);
            if (t->n < t->cap) t->b[t->n++] = 0;
            break;
        case PF_PCOMM:
            if (!q) break;
            tb_s(t, q->name);
            tb_s(t, "\n");
            break;
    }
    return t->n;
}

static void set_cwd_proc(proc_t *p, int pid) {    // the current folder is /proc or /proc/PID
    strcpy(p->cwd, "/proc");
    if (!pid) return;
    char d[12];
    int n = 0;
    do { d[n++] = (char)('0' + pid % 10); pid /= 10; } while (pid);
    int k = 5;
    p->cwd[k++] = '/';
    while (n) p->cwd[k++] = d[--n];
    p->cwd[k] = 0;
}

static int64_t sys_statfs(proc_t *p, int kind, uint64_t a) {
    if (!uok(p, a, 120)) return -EFAULT;
    uint64_t *o = (uint64_t *)a;
    memset(o, 0, 120);
    if (kind == R_PROCDIR || kind == R_PROCFILE) {
        o[0] = 0x9FA0;                              // PROC_SUPER_MAGIC
        o[1] = 4096;
        o[8] = 255;
        return 0;
    }
    uint64_t free_kb = myfs_free_kb(), files_n = (uint64_t)myfs_count() + (uint64_t)myfs_dir_count();
    o[0] = 0x4D59;                                  // f_type ("MY")
    o[1] = 1024;                                    // f_bsize
    o[2] = myfs_total_kb();                         // f_blocks
    o[3] = o[4] = free_kb;                          // f_bfree, f_bavail
    o[5] = myfs_version() == 2 ? files_n + free_kb : MYFS_MAX_FILES;     // f_files
    o[6] = o[5] - files_n;                          // f_ffree
    o[8] = myfs_version() == 2 ? MYFS_NAME_MAX : MYFS_V1_NAME_MAX;      // f_namelen
    o[9] = 1024;                                    // f_frsize
    return 0;
}

static int64_t do_open(proc_t *p, uint64_t upathname, int flags) {
    lx_t *x = LX(p);
    char name[MYFS_PATH_MAX];
    int fdno = 0;
    int kind = upath(p, upathname, name, &fdno);
    if (kind < 0) return kind;
    int fd = fd_alloc(x, 0);
    if (fd < 0) return fd;
    int acc = flags & O_ACCMODE;
    int fi;
    if (kind == R_FD) {
        if (!fget(x, fdno)) return -ENOENT;
        fi = x->fd[fdno];
        files[fi].refs++;
    } else if (kind == R_DIR) {
        if (acc != 0) return -EISDIR;
        fi = file_new(FT_DIR, flags);
        if (fi >= 0) strcpy(files[fi].name, name);
    } else if (kind == R_PROCDIR) {
        if (acc != 0) return -EISDIR;
        fi = file_new(FT_PROCDIR, flags);
        if (fi >= 0) files[fi].pid = fdno;
    } else if (kind == R_PROCFILE) {
        if (acc != 0) return -EACCES;
        int b = -1;
        for (int i = 0; i < NPROCBUF && b < 0; i++)
            if (!procbuf_used[i]) b = i;
        if (b < 0) return -ENFILE;
        fi = file_new(FT_PROC, flags);
        if (fi >= 0) {
            procbuf_used[b] = 1;
            files[fi].pbuf = b;
            files[fi].size = (uint64_t)proc_text(fdno, procbufs[b], sizeof procbufs[b]);
        }
    } else if (kind != R_FILE) {
        static const int types[] = { [R_NULL] = FT_NULL, [R_ZERO] = FT_ZERO, [R_TTY] = FT_TTY,
                                     [R_RANDOM] = FT_RANDOM };
        fi = file_new(types[kind], flags);
    } else {
        int32_t size = myfs_size(name);
        if (size < 0 && !(flags & O_CREAT)) return -ENOENT;
        if (size >= 0 && (flags & O_CREAT) && (flags & O_EXCL)) return -EEXIST;
        if (size >= 0 && (flags & O_DIRECTORY)) return -ENOTDIR;
        if (size < 0 && (flags & O_DIRECTORY)) return -ENOENT;
        fi = file_new(FT_FILE, flags);
        if (fi < 0) return fi;
        file_t *f = &files[fi];
        strcpy(f->name, name);
        f->size = size < 0 ? 0 : (uint64_t)size;
        if (acc != 0) {                             // writing: keep the file in RAM
            if (size < 0 || (flags & O_TRUNC)) {
                f->size = 0;
                f->dirty = 1;                       // (an empty file appears even if nothing is written)
            }
            int r = file_reserve(f, f->size ? f->size : 1);
            if (r < 0) {
                f->refs = 0;
                return r;
            }
            if (f->size && myfs_read(name, 0, f->buf, (uint32_t)f->size) != (int)f->size) {
                file_put(fi);
                return -EIO;
            }
        }
    }
    if (fi < 0) return fi;
    x->fd[fd] = (int8_t)fi;
    if (flags & O_CLOEXEC) x->cloexec |= 1u << fd;
    else x->cloexec &= ~(1u << fd);
    return fd;
}

// ---------------------------------------------------------------
//  terminal input
// ---------------------------------------------------------------
static int in_fg_group(proc_t *q, int fgpid) {
    for (int depth = 0; q && depth < NPROC; depth++) {
        if (q->pid == fgpid) return 1;
        if (q->parent <= 0) return 0;
        q = proc_by_pid(q->parent);
    }
    return 0;
}

static int is_fg(proc_t *p) {
    int fg = proc_foreground();
    return fg && in_fg_group(p, fg);
}

static void tty_pump(proc_t *p) {                   // take keys from the terminal (yields)
    ttyin_t *t = &ttyin[p->tty];
    if (!is_fg(p)) {
        console_poll();
        return;
    }
    int c = console_getc_nowait();
    if (c >= 0 && ((t->head + 1) % sizeof t->q) != t->tail) {
        t->q[t->head] = (uint8_t)c;
        t->head = (t->head + 1) % sizeof t->q;
    }
}

static int tty_avail(proc_t *p) {
    ttyin_t *t = &ttyin[p->tty];
    if (t->ready) return 1;
    return t->head != t->tail;
}

static int tty_pop(proc_t *p) {
    ttyin_t *t = &ttyin[p->tty];
    if (t->head == t->tail) return -1;
    int c = t->q[t->tail];
    t->tail = (t->tail + 1) % sizeof t->q;
    if (c == '\r' && (tios[p->tty].iflag & ICRNL)) c = '\n';
    return c;
}

static int tty_getc_wait(proc_t *p) {               // next key, waiting for it (-1: a signal came)
    int c;
    p->waiting = 1;
    while ((c = tty_pop(p)) < 0 && !interrupted(p)) tty_pump(p);
    p->waiting = 0;
    return c;
}

static void echo_char(int c) {
    if (c < ' ' && c != '\n' && c != '\t') {
        console_putc('^');
        console_putc((char)(c + 64));
    } else {
        console_putc((char)c);
    }
}

static int64_t tty_read(proc_t *p, file_t *f, uint8_t *buf, uint64_t len) {
    int t = p->tty;
    ktermios_t *tio = &tios[t];
    ttyin_t *in = &ttyin[t];
    if (len == 0) return 0;
    if (!(tio->lflag & ICANON)) {                   // raw: what's there (at least VMIN keys)
        uint64_t n = 0;
        int vmin = tio->cc[VMIN], vtime = tio->cc[VTIME];
        uint64_t until = ticks + (uint64_t)vtime * 10;
        p->waiting = 1;
        for (;;) {
            int c;
            while (n < len && (c = tty_pop(p)) >= 0) buf[n++] = (uint8_t)c;
            if (n >= len || (vmin && n >= (uint64_t)vmin) || (!vmin && n)) break;
            if (f->flags & O_NONBLOCK) {
                if (n) break;
                p->waiting = 0;
                return -EAGAIN;
            }
            if (!vmin && (!vtime || ticks >= until)) break;
            if (interrupted(p)) {
                p->waiting = 0;
                return n ? (int64_t)n : -EINTR;
            }
            tty_pump(p);
        }
        p->waiting = 0;
        return (int64_t)n;
    }
    if (!in->ready && (f->flags & O_NONBLOCK) && in->head == in->tail) return -EAGAIN;
    while (!in->ready) {                            // canonical: a whole line, edited
        int c = tty_getc_wait(p);
        if (c < 0) return -EINTR;
        int echo = tio->lflag & ECHO;
        if (c == tio->cc[VERASE] || c == 8) {
            if (in->len > 0) {
                in->len--;
                if (echo) console_puts("\b \b");
            }
        } else if (c == tio->cc[VKILL]) {
            while (in->len > 0) {
                in->len--;
                if (echo) console_puts("\b \b");
            }
        } else if (c == tio->cc[VEOF]) {
            if (in->len == 0) return 0;             // Ctrl+D on an empty line: end of input
            in->ready = 1;
        } else if (c == '\n') {
            in->line[in->len++] = '\n';
            if (echo || (tio->lflag & 0x40)) console_putc('\n');
            in->ready = 1;
        } else if (in->len < (int)sizeof in->line - 2) {
            in->line[in->len++] = (char)c;
            if (echo) echo_char(c);
        }
        if (echo) console_puts("");
    }
    uint64_t n = (uint64_t)(in->len - in->pos);
    if (n > len) n = len;
    memcpy(buf, in->line + in->pos, n);
    in->pos += (int)n;
    if (in->pos >= in->len) in->len = in->pos = in->ready = 0;
    return (int64_t)n;
}

// ---------------------------------------------------------------
//  signals
// ---------------------------------------------------------------
static int default_ignored(int sig) {               // CHLD WINCH URG, and no job control:
    return sig == SIGCHLD || sig == 28 || sig == 23 || (sig >= 18 && sig <= 22);   // CONT STOP TSTP TTIN TTOU
}

// send sig to q: run its handler later, ignore it, or stop q
static void lx_signal(proc_t *q, int sig) {
    if (!q->linux) {                                // a MyOS program: only stopping
        if (!default_ignored(sig)) q->killed = 1;
        return;
    }
    lx_t *x = LX(q);
    uint64_t h = sig == SIGKILL ? SIG_DFL : x->sig[sig].handler;
    if (h == SIG_IGN) return;
    if (h == SIG_DFL) {
        if (default_ignored(sig)) return;
        x->term_sig = sig;
        q->killed = 1;                              // it stops at its next chance
        return;
    }
    x->pending |= SIG_BIT(sig);
}

static uint64_t deliverable(lx_t *x) {
    return x->pending & ~(x->sigmask & ~(SIG_BIT(SIGKILL) | SIG_BIT(SIGSTOP)));
}

// a waiting system call stops early (-EINTR) when this is true
static int interrupted(proc_t *p) {
    return deliverable(LX(p)) != 0;
}

// Linux AArch64 signal frame: siginfo (128 bytes), then ucontext
#define UC_OFF      128
#define UC_SIGMASK  (UC_OFF + 40)
#define MC_OFF      (UC_OFF + 176)          // struct sigcontext (16-byte aligned)
#define MC_REGS     (MC_OFF + 8)
#define MC_SP       (MC_OFF + 256)
#define MC_PC       (MC_OFF + 264)
#define MC_PSTATE   (MC_OFF + 272)
#define MC_RESERVED (MC_OFF + 288)          // fpsimd_context, then an empty record
#define FRAME_SIZE  (MC_RESERVED + 4096)
#define TRAMP_OFF   FRAME_SIZE              // "mov x8, #139; svc #0" if there's no sa_restorer

static uint64_t fpbuf[66] __attribute__((aligned(16)));

static void put64(uint64_t a, uint64_t v) { memcpy((void *)a, &v, 8); }
static uint64_t get64(uint64_t a) { uint64_t v; memcpy(&v, (void *)a, 8); return v; }
static void put32(uint64_t a, uint32_t v) { memcpy((void *)a, &v, 4); }

// run the handler of one deliverable signal; returns 1 if a handler was set up
static int deliver_one(proc_t *p, trapframe_t *f) {
    lx_t *x = LX(p);
    uint64_t d = deliverable(x);
    if (!d) return 0;
    int sig = 1;
    while (!(d & SIG_BIT(sig))) sig++;
    x->pending &= ~SIG_BIT(sig);
    sigact_t *a = &x->sig[sig];
    if (a->handler == SIG_IGN) return 0;
    if (a->handler == SIG_DFL) {                    // (changed back since it was sent)
        if (!default_ignored(sig)) die_by_signal(p, sig);
        return 0;
    }
    uint64_t sp = (f->sp_el0 - (FRAME_SIZE + 16)) & ~15ull;
    if (!uok(p, sp, FRAME_SIZE + 16)) die_by_signal(p, SIGSEGV);
    memset((void *)sp, 0, MC_RESERVED);
    put32(sp, (uint32_t)sig);                       // siginfo: signo, errno, code
    put64(UC_SIGMASK + sp, x->restore_mask ? x->saved_mask : x->sigmask);
    for (int i = 0; i < 31; i++) put64(sp + MC_REGS + 8 * (uint64_t)i, f->x[i]);
    put64(sp + MC_SP, f->sp_el0);
    put64(sp + MC_PC, f->elr);
    put64(sp + MC_PSTATE, f->spsr);
    fp_save(fpbuf);                                 // its FP/SIMD registers (still loaded)
    uint64_t fc = sp + MC_RESERVED;
    put32(fc, 0x46508001);                          // FPSIMD_MAGIC
    put32(fc + 4, 528);
    put32(fc + 8, (uint32_t)fpbuf[65]);             // fpsr
    put32(fc + 12, (uint32_t)fpbuf[64]);            // fpcr
    memcpy((void *)(fc + 16), fpbuf, 512);          // v0-v31
    memset((void *)(fc + 528), 0, 16);              // end of the records
    uint64_t ret = a->restorer;
    if (!(a->flags & SA_RESTORER) || !ret) {        // no restorer: a little trampoline on the stack
        put32(sp + TRAMP_OFF, 0xD2801168);          // mov x8, #139 (rt_sigreturn)
        put32(sp + TRAMP_OFF + 4, 0xD4000001);      // svc #0
        data_clean_and_invalidate((void *)(sp + TRAMP_OFF), 8);
        __asm__ volatile ("ic iallu; dsb ish; isb" ::: "memory");
        ret = sp + TRAMP_OFF;
    }
    uint64_t base = x->restore_mask ? x->saved_mask : x->sigmask;
    x->restore_mask = 0;
    x->sigmask = base | a->mask | ((a->flags & SA_NODEFER) ? 0 : SIG_BIT(sig));
    uint64_t handler = a->handler;
    if (a->flags & SA_RESETHAND) a->handler = SIG_DFL;
    f->x[0] = (uint64_t)sig;
    f->x[1] = sp;                                   // siginfo
    f->x[2] = sp + UC_OFF;                          // ucontext
    f->x[30] = ret;
    f->sp_el0 = sp;
    f->elr = handler;
    return 1;
}

// before going back to the program: handlers for pending signals. A system
// call that was cut short by one (-EINTR) starts again if SA_RESTART says so.
static void deliver_signals(proc_t *p, trapframe_t *f, int64_t r, uint64_t orig_x0) {
    lx_t *x = LX(p);
    if (!deliverable(x)) {
        if (x->restore_mask) {                      // (rt_sigsuspend woke up without a handler)
            x->sigmask = x->saved_mask;
            x->restore_mask = 0;
        }
        return;
    }
    int sig = 1;
    uint64_t d = deliverable(x);
    while (!(d & SIG_BIT(sig))) sig++;
    int restart = r == -EINTR && (x->sig[sig].flags & SA_RESTART);
    uint64_t pc = f->elr;
    if (deliver_one(p, f) && restart) {             // the handler returns to the svc again
        put64(f->sp_el0 + MC_REGS, orig_x0);
        put64(f->sp_el0 + MC_PC, pc - 4);
    }
}

void linux_irq_return(trapframe_t *f) {             // vectors.S: after an interrupt from a program
    proc_t *p = proc_current();
    if (p && p->linux && deliverable(LX(p))) deliver_signals(p, f, 0, 0);
}

static int64_t sys_rt_sigreturn(proc_t *p, trapframe_t *f) {
    lx_t *x = LX(p);
    uint64_t sp = f->sp_el0;
    if (!uok(p, sp, FRAME_SIZE)) die_by_signal(p, SIGSEGV);
    for (int i = 0; i < 31; i++) f->x[i] = get64(sp + MC_REGS + 8 * (uint64_t)i);
    f->sp_el0 = get64(sp + MC_SP);
    f->elr = get64(sp + MC_PC);
    f->spsr = get64(sp + MC_PSTATE) & 0xF0000000ull;    // only the flags: EL0, interrupts on
    x->sigmask = get64(sp + UC_SIGMASK) & ~(SIG_BIT(SIGKILL) | SIG_BIT(SIGSTOP));
    uint64_t fc = sp + MC_RESERVED;
    uint32_t magic;
    memcpy(&magic, (void *)fc, 4);
    if (magic == 0x46508001) {
        memcpy(fpbuf, (void *)(fc + 16), 512);
        uint32_t fpsr, fpcr;
        memcpy(&fpsr, (void *)(fc + 8), 4);
        memcpy(&fpcr, (void *)(fc + 12), 4);
        fpbuf[64] = fpcr;
        fpbuf[65] = fpsr;
        fp_restore(fpbuf);
    }
    return (int64_t)f->x[0];
}

int linux_interrupt(int tty, proc_t *fg) {
    if (!(tios[tty].lflag & ISIG)) return 0;        // raw mode: the program reads ^C as a key
    for (int i = 1; i < NPROC; i++) {
        proc_t *q = proc_slot(i);
        if (q->state != P_READY || !q->linux || q->tty != tty || !in_fg_group(q, fg->pid)) continue;
        lx_signal(q, SIGINT);
    }
    ttyin[tty].len = ttyin[tty].pos = ttyin[tty].ready = 0;
    ttyin[tty].head = ttyin[tty].tail = 0;
    return 1;
}

// ---------------------------------------------------------------
//  pipes
// ---------------------------------------------------------------
static int pipe_close_end(int i, int writer) {
    pipe_t *pp = &pipes[i];
    if (writer) pp->writers--;
    else pp->readers--;
    if (pp->readers <= 0 && pp->writers <= 0) pp->used = 0;
    return 0;
}

static void die_by_signal(proc_t *p, int sig) {
    LX(p)->term_sig = sig;
    proc_exit(EXIT_KILLED);
}

static int64_t pipe_read(proc_t *p, file_t *f, uint8_t *buf, uint64_t len) {
    pipe_t *pp = &pipes[f->pipe];
    p->waiting = 1;
    while (pp->w == pp->r) {
        if (pp->writers <= 0) break;
        if (f->flags & O_NONBLOCK) {
            p->waiting = 0;
            return -EAGAIN;
        }
        if (interrupted(p)) {
            p->waiting = 0;
            return -EINTR;
        }
        console_poll();
    }
    p->waiting = 0;
    uint64_t n = 0;
    while (n < len && pp->r != pp->w) buf[n++] = pp->data[pp->r++ % PIPE_SIZE];
    return (int64_t)n;
}

static int64_t pipe_write(proc_t *p, file_t *f, const uint8_t *buf, uint64_t len) {
    pipe_t *pp = &pipes[f->pipe];
    uint64_t n = 0;
    while (n < len) {
        if (pp->readers <= 0) {
            lx_signal(p, SIGPIPE);
            if (p->killed) proc_exit(EXIT_KILLED);
            return n ? (int64_t)n : -EPIPE;
        }
        if (pp->w - pp->r == PIPE_SIZE) {
            if (f->flags & O_NONBLOCK) return n ? (int64_t)n : -EAGAIN;
            if (interrupted(p)) return n ? (int64_t)n : -EINTR;
            p->waiting = 1;
            console_poll();
            p->waiting = 0;
            continue;
        }
        while (n < len && pp->w - pp->r < PIPE_SIZE) pp->data[pp->w++ % PIPE_SIZE] = buf[n++];
    }
    return (int64_t)n;
}

static int64_t sys_pipe2(proc_t *p, uint64_t ufds, int flags) {
    lx_t *x = LX(p);
    if (!uok(p, ufds, 8)) return -EFAULT;
    int pi = -1;
    for (int i = 0; i < NPIPE; i++) {
        if (!pipes[i].used) {
            pi = i;
            break;
        }
    }
    if (pi < 0) return -ENFILE;
    int a = fd_alloc(x, 0);
    if (a < 0) return a;
    x->fd[a] = 0;                                   // (reserve it while looking for a second)
    int b = fd_alloc(x, 0);
    x->fd[a] = -1;
    if (b < 0) return b;
    int fr = file_new(FT_PIPE_R, flags & O_NONBLOCK);
    if (fr < 0) return fr;
    int fw = file_new(FT_PIPE_W, O_WRONLY | (flags & O_NONBLOCK));
    if (fw < 0) {
        files[fr].refs = 0;
        return fw;
    }
    memset(&pipes[pi], 0, sizeof pipes[pi]);
    pipes[pi].used = 1;
    pipes[pi].readers = pipes[pi].writers = 1;
    files[fr].pipe = files[fw].pipe = pi;
    x->fd[a] = (int8_t)fr;
    x->fd[b] = (int8_t)fw;
    if (flags & O_CLOEXEC) x->cloexec |= (1u << a) | (1u << b);
    ((int32_t *)ufds)[0] = a;
    ((int32_t *)ufds)[1] = b;
    return 0;
}

// ---------------------------------------------------------------
//  read / write
// ---------------------------------------------------------------
static int64_t do_read(proc_t *p, file_t *f, uint64_t ubuf, uint64_t len, int64_t pos) {
    if ((f->flags & O_ACCMODE) == O_WRONLY && f->type != FT_TTY) return -EBADF;
    if (!uok(p, ubuf, len)) return -EFAULT;
    uint8_t *buf = (uint8_t *)ubuf;
    switch (f->type) {
        case FT_TTY: return tty_read(p, f, buf, len);
        case FT_NULL: return 0;
        case FT_ZERO: memset(buf, 0, len); return (int64_t)len;
        case FT_RANDOM: random_bytes(buf, len); return (int64_t)len;
        case FT_DIR: case FT_PROCDIR: return -EISDIR;
        case FT_PROC: {
            uint64_t off = pos >= 0 ? (uint64_t)pos : f->off;
            if (off >= f->size) return 0;
            if (len > f->size - off) len = f->size - off;
            memcpy(buf, procbufs[f->pbuf] + off, len);
            if (pos < 0) f->off += len;
            return (int64_t)len;
        }
        case FT_PIPE_R: return pipe_read(p, f, buf, len);
        case FT_PIPE_W: return -EBADF;
        case FT_FILE: {
            uint64_t off = pos >= 0 ? (uint64_t)pos : f->off;
            if (off >= f->size) return 0;
            if (len > f->size - off) len = f->size - off;
            int64_t n;
            if (f->buf) {
                memcpy(buf, f->buf + off, len);
                n = (int64_t)len;
            } else {
                n = myfs_read(f->name, (uint32_t)off, buf, (uint32_t)len);
                if (n < 0) return -EIO;
            }
            if (pos < 0) f->off += (uint64_t)n;
            return n;
        }
    }
    return -EBADF;
}

static int64_t do_write(proc_t *p, file_t *f, uint64_t ubuf, uint64_t len, int64_t pos) {
    if ((f->flags & O_ACCMODE) == 0 && f->type == FT_FILE) return -EBADF;
    if (!uok(p, ubuf, len)) return -EFAULT;
    const uint8_t *buf = (const uint8_t *)ubuf;
    switch (f->type) {
        case FT_TTY:
            for (uint64_t i = 0; i < len; i++) console_putc((char)buf[i]);
            console_puts("");
            return (int64_t)len;
        case FT_NULL: case FT_ZERO: case FT_RANDOM: return (int64_t)len;
        case FT_DIR: case FT_PROCDIR: return -EISDIR;
        case FT_PROC: return -EBADF;
        case FT_PIPE_W: return pipe_write(p, f, buf, len);
        case FT_PIPE_R: return -EBADF;
        case FT_FILE: {
            uint64_t off = pos >= 0 ? (uint64_t)pos : (f->flags & O_APPEND) ? f->size : f->off;
            int r = file_reserve(f, off + len);
            if (r < 0) return r;
            if (off > f->size) memset(f->buf + f->size, 0, off - f->size);
            memcpy(f->buf + off, buf, len);
            if (off + len > f->size) f->size = off + len;
            f->dirty = 1;
            if (pos < 0) f->off = off + len;
            return (int64_t)len;
        }
    }
    return -EBADF;
}

static int64_t do_iov(proc_t *p, file_t *f, uint64_t uiov, int64_t cnt, int writing, int64_t pos) {
    if (cnt < 0 || cnt > 1024 || !uok(p, uiov, (uint64_t)cnt * 16)) return -EINVAL;
    int64_t total = 0;
    for (int64_t i = 0; i < cnt; i++) {
        uint64_t base = ((uint64_t *)uiov)[i * 2], len = ((uint64_t *)uiov)[i * 2 + 1];
        if (!len) continue;
        int64_t r = writing ? do_write(p, f, base, len, pos) : do_read(p, f, base, len, pos);
        if (r < 0) return total ? total : r;
        total += r;
        if (pos >= 0) pos += r;
        if ((uint64_t)r < len) break;
    }
    return total;
}

static int64_t sys_getdents64(proc_t *p, file_t *f, uint64_t ubuf, uint64_t len) {
    if (f->type != FT_DIR && f->type != FT_PROCDIR) return -ENOTDIR;
    if (!uok(p, ubuf, len)) return -EFAULT;
    uint8_t *out = (uint8_t *)ubuf;
    uint64_t used = 0;
    for (;;) {
        char name[MYFS_NAME_MAX + 1];
        uint32_t size;
        int type = 8;                               // DT_REG
        int t;
        uint64_t ino;
        if (f->off == 0) { strcpy(name, "."); type = 4; ino = 2; }
        else if (f->off == 1) { strcpy(name, ".."); type = 4; ino = 2; }
        else if (f->type == FT_PROCDIR) {
            if (!proc_entry(f->pid, (int)f->off - 2, name, &type)) break;
            ino = 1000 + f->off;
        }
        else if ((t = myfs_readdir(f->name, (int)f->off - 2, name, &size)) > 0) {
            ino = f->off + 1;
            if (t == MYFS_DIR) type = 4;
        }
        else break;
        uint64_t nl = strlen(name);
        uint64_t reclen = (19 + nl + 1 + 7) & ~7ull;
        if (used + reclen > len) {
            if (used == 0) return -EINVAL;
            break;
        }
        uint8_t *d = out + used;
        memset(d, 0, reclen);
        memcpy(d, &ino, 8);
        uint64_t next = f->off + 1;
        memcpy(d + 8, &next, 8);
        uint16_t rl = (uint16_t)reclen;
        memcpy(d + 16, &rl, 2);
        d[18] = (uint8_t)type;
        memcpy(d + 19, name, nl + 1);
        used += reclen;
        f->off++;
    }
    return (int64_t)used;
}

static int64_t sys_lseek(file_t *f, int64_t off, int whence) {
    if (f->type == FT_TTY || f->type == FT_PIPE_R || f->type == FT_PIPE_W) return -ESPIPE;
    if (f->type != FT_FILE && f->type != FT_DIR && f->type != FT_PROC && f->type != FT_PROCDIR) return 0;
    int64_t base = whence == 0 ? 0 : whence == 1 ? (int64_t)f->off : whence == 2 ? (int64_t)f->size : -1;
    if (base < 0 || ((f->type == FT_DIR || f->type == FT_PROCDIR) && whence == 2)) return -EINVAL;
    if (base + off < 0) return -EINVAL;
    f->off = (uint64_t)(base + off);
    return (int64_t)f->off;
}

// ---------------------------------------------------------------
//  poll / select
// ---------------------------------------------------------------
#define POLLIN   1
#define POLLOUT  4
#define POLLERR  8
#define POLLHUP  16
#define POLLNVAL 32

static int ready_events(proc_t *p, file_t *f) {
    if (!f) return POLLNVAL;
    switch (f->type) {
        case FT_TTY:
            if (!tty_avail(p)) tty_pump(p);
            return (tty_avail(p) ? POLLIN : 0) | POLLOUT;
        case FT_PIPE_R: {
            pipe_t *pp = &pipes[f->pipe];
            return (pp->w != pp->r ? POLLIN : 0) | (pp->writers <= 0 ? POLLHUP : 0);
        }
        case FT_PIPE_W: {
            pipe_t *pp = &pipes[f->pipe];
            return pp->readers <= 0 ? POLLERR : (pp->w - pp->r < PIPE_SIZE ? POLLOUT : 0);
        }
    }
    return POLLIN | POLLOUT;
}

static int64_t timeout_from_ts(proc_t *p, uint64_t uts, int *ok) {     // ns, -1 = forever
    *ok = 1;
    if (!uts) return -1;
    if (!uok(p, uts, 16)) {
        *ok = 0;
        return 0;
    }
    int64_t sec = ((int64_t *)uts)[0], ns = ((int64_t *)uts)[1];
    if (sec < 0 || ns < 0) return 0;
    if (sec > 100000000) return -1;
    return sec * 1000000000ll + ns;
}

static int64_t sys_ppoll(proc_t *p, uint64_t ufds, uint64_t nfds, uint64_t uts) {
    lx_t *x = LX(p);
    if (nfds > 64 || !uok(p, ufds, nfds * 8)) return -EINVAL;
    int ok;
    int64_t tmo = timeout_from_ts(p, uts, &ok);
    if (!ok) return -EFAULT;
    uint64_t until = now_ns() + (uint64_t)(tmo < 0 ? 0 : tmo);
    for (;;) {
        int n = 0;
        for (uint64_t i = 0; i < nfds; i++) {
            uint8_t *e = (uint8_t *)ufds + i * 8;
            int32_t fd;
            int16_t events, rev = 0;
            memcpy(&fd, e, 4);
            memcpy(&events, e + 4, 2);
            if (fd >= 0) {
                int r = ready_events(p, fget(x, fd));
                rev = (int16_t)(r & (events | POLLERR | POLLHUP | POLLNVAL));
            }
            memcpy(e + 6, &rev, 2);
            if (rev) n++;
        }
        if (n || tmo == 0 || (tmo > 0 && now_ns() >= until)) return n;
        if (interrupted(p)) return -EINTR;
        p->waiting = 1;
        tty_pump(p);
        p->waiting = 0;
    }
}

static int64_t sys_pselect6(proc_t *p, int64_t nfds, uint64_t urd, uint64_t uwr, uint64_t uex, uint64_t uts) {
    lx_t *x = LX(p);
    if (nfds < 0 || nfds > NFD) nfds = NFD;
    uint64_t rd = 0, wr = 0;
    if ((urd && !uok(p, urd, 8)) || (uwr && !uok(p, uwr, 8)) || (uex && !uok(p, uex, 8))) return -EFAULT;
    if (urd) rd = *(uint64_t *)urd;
    if (uwr) wr = *(uint64_t *)uwr;
    int ok;
    int64_t tmo = timeout_from_ts(p, uts, &ok);
    if (!ok) return -EFAULT;
    uint64_t until = now_ns() + (uint64_t)(tmo < 0 ? 0 : tmo);
    for (;;) {
        uint64_t ord = 0, owr = 0;
        int n = 0;
        for (int fd = 0; fd < nfds; fd++) {
            uint64_t bit = 1ull << fd;
            if (!((rd | wr) & bit)) continue;
            file_t *f = fget(x, fd);
            if (!f) return -EBADF;
            int r = ready_events(p, f);
            if ((rd & bit) && (r & (POLLIN | POLLHUP | POLLERR))) { ord |= bit; n++; }
            if ((wr & bit) && (r & (POLLOUT | POLLERR))) { owr |= bit; n++; }
        }
        if (n || tmo == 0 || (tmo > 0 && now_ns() >= until)) {
            if (urd) *(uint64_t *)urd = ord;
            if (uwr) *(uint64_t *)uwr = owr;
            if (uex) *(uint64_t *)uex = 0;
            return n;
        }
        if (interrupted(p)) return -EINTR;
        p->waiting = 1;
        tty_pump(p);
        p->waiting = 0;
    }
}

// ---------------------------------------------------------------
//  ioctl (terminals)
// ---------------------------------------------------------------
static int64_t sys_ioctl(proc_t *p, file_t *f, uint64_t req, uint64_t arg) {
    if (f->type != FT_TTY) return -ENOTTY;
    int t = p->tty;
    switch (req) {
        case 0x5401:                                // TCGETS
            if (!uok(p, arg, sizeof(ktermios_t))) return -EFAULT;
            memcpy((void *)arg, &tios[t], sizeof(ktermios_t));
            return 0;
        case 0x5402: case 0x5403: case 0x5404:      // TCSETS, TCSETSW, TCSETSF
            if (!uok(p, arg, sizeof(ktermios_t))) return -EFAULT;
            memcpy(&tios[t], (void *)arg, sizeof(ktermios_t));
            if (req == 0x5404) ttyin[t].head = ttyin[t].tail = 0;
            return 0;
        case 0x5413:                                // TIOCGWINSZ
            if (!uok(p, arg, 8)) return -EFAULT;
            memcpy((void *)arg, winsize[t], 8);
            return 0;
        case 0x5414:                                // TIOCSWINSZ
            if (!uok(p, arg, 8)) return -EFAULT;
            memcpy(winsize[t], (void *)arg, 8);
            return 0;
        case 0x540F: {                              // TIOCGPGRP: the foreground job
            if (!uok(p, arg, 4)) return -EFAULT;
            int fg = proc_foreground();
            *(int32_t *)arg = fg ? fg : p->pid;
            return 0;
        }
        case 0x5429:                                // TIOCGSID
            if (!uok(p, arg, 4)) return -EFAULT;
            *(int32_t *)arg = p->pid;
            return 0;
        case 0x5410: case 0x540E: case 0x5422:      // TIOCSPGRP, TIOCSCTTY, TIOCNOTTY
        case 0x540A: case 0x540B:                   // TCXONC, TCFLSH
        case 0x5409:                                // TCSBRK (tcdrain)
            return 0;
        case 0x541B:                                // FIONREAD
            if (!uok(p, arg, 4)) return -EFAULT;
            *(int32_t *)arg = (int32_t)((ttyin[t].head - ttyin[t].tail) % sizeof ttyin[t].q);
            return 0;
    }
    return -EINVAL;
}

// ---------------------------------------------------------------
//  memory system calls
// ---------------------------------------------------------------
static int64_t sys_brk(proc_t *p, uint64_t want) {
    lx_t *x = LX(p);
    if (want < x->brk_start) return (int64_t)x->brk;
    uint64_t limit = x->brk_start < LX_LOW_END ? LX_LOW_END : MMAP_TOP;
    if (want > limit) return (int64_t)x->brk;
    uint64_t old_end = brk_end(x), new_end = PAGE_UP(want);
    if (new_end > old_end) {
        for (int i = 0; i < x->nvma; i++)
            if (x->vma[i].start < new_end && x->vma[i].end > old_end) return (int64_t)x->brk;
        zero_range(p, old_end, new_end);
    }
    x->brk = want;
    if (new_end < old_end) release_blocks(p, new_end, old_end);
    return (int64_t)want;
}

#define MAP_SHARED    0x01
#define MAP_FIXED     0x10
#define MAP_ANONYMOUS 0x20
#define MAP_FIXED_NOREPLACE 0x100000

static int64_t sys_munmap(proc_t *p, uint64_t addr, uint64_t len) {
    if ((addr & (PAGE - 1)) || !len) return -EINVAL;
    uint64_t e = PAGE_UP(addr + len);
    if (!in_window(addr, e)) return 0;
    vma_cut(LX(p), addr, e);
    release_blocks(p, addr, e);
    return 0;
}

static int64_t sys_mmap(proc_t *p, uint64_t addr, uint64_t len, int prot, int flags, int64_t fd, uint64_t off) {
    lx_t *x = LX(p);
    (void)prot;
    if (!len || (off & (PAGE - 1))) return -EINVAL;
    len = PAGE_UP(len);
    if (len > LX_HIGH_END - LX_HIGH_START) return -ENOMEM;
    file_t *f = 0;
    if (!(flags & MAP_ANONYMOUS)) {
        f = fget(x, fd);
        if (!f) return -EBADF;
        if (f->type != FT_FILE && f->type != FT_ZERO) return -ENODEV;
    }
    if (flags & (MAP_FIXED | MAP_FIXED_NOREPLACE)) {
        if ((addr & (PAGE - 1)) || !in_window(addr, addr + len)) return -ENOMEM;
        if ((flags & MAP_FIXED_NOREPLACE) && !range_free(x, addr, addr + len)) return -EEXIST;
        vma_cut(x, addr, addr + len);
    } else if (addr && !(addr & (PAGE - 1)) && in_window(addr, addr + len) && range_free(x, addr, addr + len)) {
        // the hint is free: take it
    } else {
        addr = find_gap(x, len);
        if (!addr) return -ENOMEM;
    }
    if (!vma_add(x, addr, addr + len)) return -ENOMEM;
    zero_range(p, addr, addr + len);
    if (f && f->type == FT_FILE && off < f->size) {
        uint64_t n = f->size - off;
        if (n > len) n = len;
        if (!uok(p, addr, n)) return -ENOMEM;
        if (f->buf) memcpy((void *)addr, f->buf + off, n);
        else if (myfs_read(f->name, (uint32_t)off, (void *)addr, (uint32_t)n) < 0) return -EIO;
    }
    return (int64_t)addr;
}

static int64_t sys_mremap(proc_t *p, uint64_t old, uint64_t oldlen, uint64_t newlen, int flags) {
    lx_t *x = LX(p);
    if ((old & (PAGE - 1)) || !newlen) return -EINVAL;
    oldlen = PAGE_UP(oldlen);
    newlen = PAGE_UP(newlen);
    if (!in_window(old, old + oldlen)) return -EFAULT;
    if (newlen <= oldlen) {
        if (newlen < oldlen) sys_munmap(p, old + newlen, oldlen - newlen);
        return (int64_t)old;
    }
    if (in_window(old, old + newlen) && range_free(x, old + oldlen, old + newlen)) {
        if (!vma_add(x, old + oldlen, old + newlen)) return -ENOMEM;
        zero_range(p, old + oldlen, old + newlen);
        return (int64_t)old;
    }
    if (!(flags & 1)) return -ENOMEM;               // MREMAP_MAYMOVE
    uint64_t n = find_gap(x, newlen);
    if (!n || !vma_add(x, n, n + newlen)) return -ENOMEM;
    zero_range(p, n, n + newlen);
    if (!uok(p, n, newlen) || !uok(p, old, oldlen)) return -ENOMEM;
    memcpy((void *)n, (void *)old, oldlen);
    sys_munmap(p, old, oldlen);
    return (int64_t)n;
}

// ---------------------------------------------------------------
//  loading an ELF file, the start-up stack
// ---------------------------------------------------------------
typedef struct {
    uint8_t ident[16];
    uint16_t type, machine;
    uint32_t version;
    uint64_t entry, phoff, shoff;
    uint32_t flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
} ehdr_t;

typedef struct {
    uint32_t type, flags;
    uint64_t offset, vaddr, paddr, filesz, memsz, align;
} phdr_t;

#define PT_LOAD 1
#define PT_INTERP 3
#define PT_PHDR 6
#define MAX_PH 16

typedef struct {
    ehdr_t eh;
    phdr_t ph[MAX_PH];
    uint64_t bias;
} elf_t;

// read and check the headers; 0 or -errno (with a message for the user if loud)
static int elf_check(const char *file, elf_t *e, int loud) {
    if (myfs_read(file, 0, &e->eh, sizeof e->eh) != (int)sizeof e->eh ||
        memcmp(e->eh.ident, "\177ELF", 4) != 0)
        return -ENOEXEC;
    const char *why = 0;
    if (e->eh.ident[4] != 2 || e->eh.ident[5] != 1 || e->eh.machine != 183)
        why = " is not a 64-bit ARM (AArch64) Linux program.\n";
    else if ((e->eh.type != 2 && e->eh.type != 3) || e->eh.phentsize != sizeof(phdr_t) ||
             e->eh.phnum == 0 || e->eh.phnum > MAX_PH)
        why = ": unsupported ELF file.\n";
    else if (myfs_read(file, (uint32_t)e->eh.phoff, e->ph, e->eh.phnum * (uint32_t)sizeof(phdr_t)) !=
             (int)(e->eh.phnum * sizeof(phdr_t)))
        why = ": broken ELF file.\n";
    if (!why) {
        uint64_t lo = ~0ull, hi = 0;
        for (int i = 0; i < e->eh.phnum; i++) {
            phdr_t *ph = &e->ph[i];
            if (ph->type == PT_INTERP) {
                why = " needs shared libraries (dynamically linked): only static programs run on MyOS.\n";
                break;
            }
            if (ph->type != PT_LOAD) continue;
            if (ph->vaddr < lo) lo = ph->vaddr;
            if (ph->vaddr + ph->memsz > hi) hi = ph->vaddr + ph->memsz;
            if (ph->filesz > ph->memsz) why = ": broken ELF file.\n";
        }
        if (!why && hi <= lo) why = ": broken ELF file.\n";
        e->bias = 0;
        if (!why && e->eh.type == 3) e->bias = LX_LOW_START - PAGE_DOWN(lo);    // static PIE
        if (!why && !in_window(PAGE_DOWN(lo + e->bias), PAGE_UP(hi + e->bias)))
            why = ": its addresses don't fit MyOS (it must load at 0x400000-0x3FFFFFF).\n";
    }
    if (why) {
        if (loud) {
            console_puts(file);
            console_puts(why);
        }
        return -ENOEXEC;
    }
    return 0;
}

// map the segments into p (whose map is empty); fills entry/brk, AT_PHDR
static int elf_load(proc_t *p, const char *file, elf_t *e, uint64_t *phdr_va) {
    lx_t *x = LX(p);
    uint64_t hi = 0;
    int cached = ecache_get(file);
    *phdr_va = 0;
    x->code_lo = ~0ull;
    x->code_hi = 0;
    for (int i = 0; i < e->eh.phnum; i++) {
        phdr_t *ph = &e->ph[i];
        if (ph->type == PT_PHDR) *phdr_va = ph->vaddr + e->bias;
        if (ph->type != PT_LOAD) continue;
        uint64_t va = ph->vaddr + e->bias;
        uint64_t s = PAGE_DOWN(va), end = PAGE_UP(va + ph->memsz);
        if (!vma_add(x, s, end)) return -ENOMEM;
        for (uint64_t b = s & ~(BLOCK - 1); b < end; b += BLOCK)
            if (!map_block(p, b)) return -ENOMEM;
        uint64_t off = ph->offset, left = ph->filesz, at = va;
        while (left) {                              // the file's bytes, block by block
            uint64_t n = BLOCK - (at & (BLOCK - 1));
            if (n > left) n = left;
            uint8_t *m = phys(p, at);
            if (cached && off + n <= (uint64_t)ecache.size) memcpy(m, ecache.mem + off, n);
            else if (myfs_read(file, (uint32_t)off, m, (uint32_t)n) != (int)n) return -EIO;
            data_clean_and_invalidate(m, n);
            off += n;
            at += n;
            left -= n;
        }
        if (ph->flags & 1) {                        // PF_X
            if (s < x->code_lo) x->code_lo = s;
            if (end > x->code_hi) x->code_hi = end;
        }
        if (!*phdr_va && e->eh.phoff >= ph->offset && e->eh.phoff < ph->offset + ph->filesz)
            *phdr_va = va + (e->eh.phoff - ph->offset);
        if (end > hi) hi = end;
    }
    x->brk_start = x->brk = hi;
    x->entry = e->eh.entry + e->bias;
    __asm__ volatile ("ic iallu; dsb ish; isb" ::: "memory");
    return 0;
}

// argv/envp strings, packed one after the other
typedef struct {
    char *buf;
    uint64_t used, cap;
    int argc, envc;
} strs_t;

static int strs_add(strs_t *s, const char *str, uint64_t n) {
    if (s->used + n + 1 > s->cap) return -E2BIG;
    memcpy(s->buf + s->used, str, n);
    s->buf[s->used + n] = 0;
    s->used += n + 1;
    return 0;
}

#define AT_NULL 0
#define AT_PHDR 3
#define AT_PHENT 4
#define AT_PHNUM 5
#define AT_PAGESZ 6
#define AT_BASE 7
#define AT_FLAGS 8
#define AT_ENTRY 9
#define AT_UID 11
#define AT_EUID 12
#define AT_GID 13
#define AT_EGID 14
#define AT_PLATFORM 15
#define AT_HWCAP 16
#define AT_CLKTCK 17
#define AT_SECURE 23
#define AT_RANDOM 25
#define AT_HWCAP2 26
#define AT_EXECFN 31

// the stack Linux programs start with: argc, argv[], 0, envp[], 0, auxv[]
static int build_stack(proc_t *p, strs_t *s, uint64_t phdr_va, int phnum, const char *execfn) {
    lx_t *x = LX(p);
    if (!vma_add(x, STACK_TOP - STACK_SIZE, STACK_TOP)) return -ENOMEM;
    uint64_t blk = STACK_TOP - BLOCK;
    if (!map_block(p, blk)) return -ENOMEM;
    uint8_t *m = phys(p, blk);
#define VA2M(va) (m + ((va) - blk))
    uint64_t sp = STACK_TOP - 16;
    uint64_t nl = strlen(execfn) + 1;
    sp -= nl;
    uint64_t fn_va = sp;
    memcpy(VA2M(sp), execfn, nl);
    sp -= 8;
    uint64_t plat_va = sp;
    memcpy(VA2M(sp), "aarch64", 8);
    sp = (sp - 16) & ~15ull;
    uint64_t rand_va = sp;
    random_bytes(VA2M(sp), 16);
    sp -= s->used;
    uint64_t str_va = sp;
    memcpy(VA2M(sp), s->buf, s->used);
    int naux = 18;
    uint64_t words = 1 + (uint64_t)s->argc + 1 + (uint64_t)s->envc + 1 + (uint64_t)naux * 2;
    sp = (sp - words * 8) & ~15ull;
    if (sp < blk + 4096) return -E2BIG;
    uint64_t *w = (uint64_t *)VA2M(sp);
    int k = 0;
    w[k++] = (uint64_t)s->argc;
    uint64_t at = str_va;
    for (int i = 0; i < s->argc; i++) {
        w[k++] = at;
        at += strlen((char *)VA2M(at)) + 1;
    }
    w[k++] = 0;
    for (int i = 0; i < s->envc; i++) {
        w[k++] = at;
        at += strlen((char *)VA2M(at)) + 1;
    }
    w[k++] = 0;
    uint64_t aux[][2] = {
        { AT_PHDR, phdr_va }, { AT_PHENT, sizeof(phdr_t) }, { AT_PHNUM, (uint64_t)phnum },
        { AT_PAGESZ, PAGE }, { AT_BASE, 0 }, { AT_FLAGS, 0 }, { AT_ENTRY, x->entry },
        { AT_UID, 0 }, { AT_EUID, 0 }, { AT_GID, 0 }, { AT_EGID, 0 },
        { AT_PLATFORM, plat_va }, { AT_HWCAP, 0x3 /* FP ASIMD */ }, { AT_CLKTCK, TICKS_PER_SECOND },
        { AT_SECURE, 0 }, { AT_RANDOM, rand_va }, { AT_EXECFN, fn_va }, { AT_NULL, 0 },
    };
    for (int i = 0; i < naux; i++) {
        w[k++] = aux[i][0];
        w[k++] = aux[i][1];
    }
#undef VA2M
    data_clean_and_invalidate(m + (sp - blk), STACK_TOP - sp);
    x->sp = sp;
    return 0;
}

static const char *const default_env[] = {
    "PATH=/bin:/usr/bin:/sbin:/usr/sbin", "HOME=/", "TERM=xterm", "USER=root", "LOGNAME=root",
    "SHELL=/bin/sh", "PWD=/",
};

static void set_name(proc_t *p, const char *argv0) {
    const char *b = argv0;
    for (const char *q = argv0; *q; q++)
        if (*q == '/' && q[1]) b = q + 1;
    int k = 0;
    while (b[k] && k < 15) {
        p->name[k] = b[k];
        k++;
    }
    p->name[k] = 0;
}

// split a command line into words: spaces separate, '...' "..." and \ quote
static int split_args(strs_t *s, const char *args) {
    const char *c = args;
    char word[256];
    for (;;) {
        while (*c == ' ' || *c == '\t') c++;
        if (!*c) return 0;
        int n = 0;
        char q = 0;
        while (*c && (q || (*c != ' ' && *c != '\t'))) {
            if (q) {
                if (*c == q) { q = 0; c++; continue; }
                if (q == '"' && *c == '\\' && (c[1] == '"' || c[1] == '\\')) c++;
            } else if (*c == '\'' || *c == '"') {
                q = *c++;
                continue;
            } else if (*c == '\\' && c[1]) {
                c++;
            }
            if (n < (int)sizeof word - 1) word[n++] = *c;
            c++;
        }
        if (strs_add(s, word, (uint64_t)n) < 0) return -E2BIG;
        s->argc++;
    }
}

// first thing a new Linux process runs (on its own kernel stack)
static void lx_start(void) {
    proc_t *p = proc_current();
    enter_user(LX(p)->entry, LX(p)->sp, 0, start_ctx);     // never comes back here
}

static void tty_files(lx_t *x) {
    int fi = file_new(FT_TTY, O_RDWR);
    if (fi < 0) return;
    files[fi].refs = 3;
    x->fd[0] = x->fd[1] = x->fd[2] = (int8_t)fi;
}

static int trace_next;              // linux_command("strace ..."): trace the next program

int linux_run(const char *file, const char *argv0, const char *args, int background) {
    if (myfs_size(file) < 0) return -1;
    elf_t e;
    memset(&e, 0, sizeof e);
    if (elf_check(file, &e, 1) < 0) {
        if (memcmp(e.eh.ident, "\177ELF", 4)) {
            console_puts(file);
            console_puts(" is not a Linux program.\n");
        }
        trace_next = 0;
        return 0;
    }
    char sbuf[1024];
    strs_t s = { sbuf, 0, sizeof sbuf, 0, 0 };
    if (strs_add(&s, argv0, strlen(argv0)) == 0) s.argc = 1;
    if (split_args(&s, args) < 0) {
        console_puts("Too many arguments.\n");
        trace_next = 0;
        return 0;
    }
    for (unsigned i = 0; i < sizeof default_env / sizeof default_env[0]; i++)
        if (strs_add(&s, default_env[i], strlen(default_env[i])) == 0) s.envc++;
    proc_t *p = proc_new();
    if (!p) {
        console_puts("Too many programs running (try 'ps' and 'kill').\n");
        trace_next = 0;
        return 0;
    }
    lx_setup(p);
    lx_t *x = LX(p);
    x->trace = trace_next;
    trace_next = 0;
    strcpy(x->exe, file);
    uint64_t phdr_va;
    int r = elf_load(p, file, &e, &phdr_va);
    if (r == 0) r = build_stack(p, &s, phdr_va, e.eh.phnum, argv0);
    if (r < 0) {
        console_puts(r == -ENOMEM ? "Out of memory.\n" : r == -EIO ? "Disk error!\n" : "Can't start it.\n");
        linux_free(p);
        p->linux = 0;
        return 0;
    }
    tty_files(x);
    set_name(p, argv0);
    p->parent = 0;
    p->background = background;
    if (!background) tty_reset(p->tty);
    proc_start(p, lx_start, !background);
    int pid = p->pid;
    if (background) {
        console_puts("[");
        print_dec((uint64_t)pid);
        console_puts("] ");
        console_puts(p->name);
        console_puts(" is running in the background ('ps' lists, 'fg ");
        print_dec((uint64_t)pid);
        console_puts("' brings it back)\n");
        return 0;
    }
    int64_t code = 0;
    if (proc_wait(pid, &code)) {
        if (!console_at_line_start()) console_putc('\n');
        if (code == EXIT_KILLED) console_puts("^C - stopped.\n");
    }
    return 0;
}

// ---------------------------------------------------------------
//  BusyBox applets: "ls" can mean "busybox ls"
// ---------------------------------------------------------------
static char applet_names[4096];     // the applet table from the busybox file ("[\0[[\0acpid\0...")
static int32_t applets_len = -1, applets_of_size = -1;

static void load_applets(void) {
    int32_t size = myfs_size("/busybox");
    if (size == applets_of_size) return;
    applets_of_size = size;
    applets_len = 0;
    if (size < 0) return;
    static uint8_t chunk[4096 + 16];
    static const char marker[] = "\0[\0[[\0";
    for (int32_t off = 0; off < size; off += 4096) {
        int n = myfs_read("/busybox", (uint32_t)off, chunk, sizeof chunk);
        if (n <= 6) break;
        for (int i = 0; i + 6 <= n && i < 4096; i++) {
            if (memcmp(chunk + i, marker, 6) != 0) continue;
            int got = myfs_read("/busybox", (uint32_t)(off + i + 1), applet_names, sizeof applet_names - 2);
            if (got <= 0) return;
            int k = 0;
            while (k + 1 < got && !(applet_names[k] == 0 && applet_names[k + 1] == 0)) k++;
            applet_names[k] = applet_names[k + 1] = 0;
            applets_len = k + 1;
            return;
        }
    }
}

static int applet_lookup(const char *name) {
    load_applets();
    for (int i = 0; i < applets_len;) {
        if (!strcmp(applet_names + i, name)) return 1;
        i += (int)strlen(applet_names + i) + 1;
    }
    return 0;
}

int linux_applet(const char *name) {
    return myfs_size(name) < 0 && applet_lookup(name);
}

int linux_command(const char *line, int background) {
    if (memcmp(line, "strace ", 7) != 0) return 0;
    const char *c = line + 7;
    while (*c == ' ') c++;
    char name[MYFS_PATH_MAX];
    int n = 0;
    while (*c && *c != ' ' && n < MYFS_PATH_MAX - 1) name[n++] = *c++;
    name[n] = 0;
    while (*c == ' ') c++;
    trace_next = 1;
    if (linux_is_elf(name)) linux_run(name, name, c, background);
    else if (linux_applet(name)) linux_run("/busybox", name, c, background);
    else console_puts("strace: only Linux programs can be traced.\n");
    trace_next = 0;
    return 1;
}

// ---------------------------------------------------------------
//  processes: fork, execve, wait4, exit
// ---------------------------------------------------------------
static void lx_fork_start(void) {
    proc_t *p = proc_current();
    trapframe_t *f = (trapframe_t *)(proc_kstack_top(p) - 2048);
    memcpy(f, &LX(p)->frame, sizeof *f);
    user_resume(f);
}

#define CLONE_VM             0x00000100
#define CLONE_VFORK          0x00004000
#define CLONE_THREAD         0x00010000
#define CLONE_SETTLS         0x00080000
#define CLONE_PARENT_SETTID  0x00100000
#define CLONE_CHILD_SETTID   0x01000000

static int64_t sys_clone(proc_t *p, trapframe_t *f, uint64_t flags, uint64_t stack, uint64_t ptid,
                         uint64_t tls, uint64_t ctid) {
    if ((flags & CLONE_THREAD) || ((flags & CLONE_VM) && !(flags & CLONE_VFORK))) return -ENOSYS;
    lx_t *x = LX(p);
    proc_t *c = proc_new();
    if (!c) return -EAGAIN;
    lx_setup(c);
    lx_t *cx = LX(c);
    // a copy of every block it has (2 MB each)
    for (int w = 0; w < 2; w++) {
        uint64_t s = w ? LX_HIGH_START : LX_LOW_START, e = w ? LX_HIGH_END : LX_LOW_END;
        for (uint64_t va = s; va < e; va += BLOCK) {
            uint8_t *src = phys(p, va);
            if (!src) continue;
            uint64_t m = lx_mem_alloc(1);
            if (!m) {
                linux_free(c);
                c->linux = 0;
                return -ENOMEM;
            }
            memcpy((void *)m, src, BLOCK);
            if (va < x->code_hi && va + BLOCK > x->code_lo) data_clean_and_invalidate((void *)m, BLOCK);
            *pte(c, va) = proc_user_desc(m);
        }
    }
    data_clean_and_invalidate(c->l2, 4096);
    data_clean_and_invalidate(high_l2[c->slot], 4096);
    __asm__ volatile ("ic iallu; dsb ish; isb" ::: "memory");
    memcpy(cx, x, sizeof *cx);
    cx->trace = x->trace;
    cx->term_sig = 0;
    cx->execed = 0;
    cx->pending = 0;                                // (signals sent to the parent stay there)
    cx->restore_mask = 0;
    for (int i = 0; i < NFD; i++)
        if (cx->fd[i] >= 0) files[(int)cx->fd[i]].refs++;
    cx->frame = *f;
    cx->frame.x[0] = 0;                             // fork() returns 0 in the child
    if (stack) cx->frame.sp_el0 = stack;
    c->tpidr = (flags & CLONE_SETTLS) ? tls : mrs_tpidr();
    fp_save(c->fp);
    c->parent = p->pid;
    c->background = p->background;
    memcpy(c->name, p->name, sizeof c->name);
    proc_start(c, lx_fork_start, 0);
    int pid = c->pid;
    if ((flags & CLONE_PARENT_SETTID) && uok(p, ptid, 4)) *(int32_t *)ptid = pid;
    if (flags & CLONE_CHILD_SETTID) {
        uint8_t *m = phys(c, ctid);
        if (m) *(int32_t *)m = pid;
    }
    if (flags & CLONE_VFORK) {                      // the parent waits until the child execs or exits
        p->waiting = 1;
        for (;;) {
            proc_t *q = proc_by_pid(pid);
            if (!q || q->state != P_READY || LX(q)->execed) break;
            console_poll();
        }
        p->waiting = 0;
    }
    return pid;
}

static int64_t sys_execve(proc_t *p, trapframe_t *f, uint64_t upath, uint64_t uargv, uint64_t uenvp) {
    lx_t *x = LX(p);
    char path[256], file[MYFS_PATH_MAX];
    int n = ustr(p, upath, path, sizeof path);
    if (n < 0) return n;
    int fdno;
    int kind = resolve(path, file, &fdno);
    if (kind < 0) return kind;
    if (kind != R_FILE) return -EACCES;
    if (myfs_size(file) < 0) return -ENOENT;
    elf_t e;
    int r = elf_check(file, &e, 0);
    if (r < 0) return r;
    // argv and envp: copy them out before the old memory goes
    uint64_t scratch = lx_mem_alloc(1);
    if (!scratch) return -ENOMEM;
    strs_t s = { (char *)scratch, 0, MAX_ARGS, 0, 0 };
    r = 0;
    for (int pass = 0; pass < 2 && r == 0; pass++) {
        uint64_t vec = pass ? uenvp : uargv;
        for (int i = 0; vec && r == 0; i++) {
            if (!uok(p, vec + (uint64_t)i * 8, 8)) { r = -EFAULT; break; }
            uint64_t a = ((uint64_t *)vec)[i];
            if (!a) break;
            char *dst = s.buf + s.used;
            int len = ustr(p, a, dst, (int)(s.cap - s.used));
            if (len < 0) { r = len == -ENAMETOOLONG ? -E2BIG : len; break; }
            s.used += (uint64_t)len + 1;
            if (pass) s.envc++;
            else s.argc++;
        }
    }
    if (r == 0 && s.argc == 0) r = strs_add(&s, file, strlen(file)) == 0 ? (s.argc = 1, 0) : -E2BIG;
    if (r < 0) {
        proc_mem_free(scratch, 1);
        return r;
    }
    char argv0[64];
    size_t a0len = strlen(s.buf);
    if (a0len > sizeof argv0 - 1) a0len = sizeof argv0 - 1;
    memcpy(argv0, s.buf, a0len);
    argv0[a0len] = 0;
    // point of no return: the old program is gone
    for (int i = 0; i < NFD; i++)
        if (x->cloexec & (1u << i)) fd_close(x, i);
    unmap_all(p);
    x->nvma = 0;
    x->brk_start = x->brk = 0;
    for (int i = 0; i < NSIG; i++)
        if (x->sig[i].handler != SIG_IGN) memset(&x->sig[i], 0, sizeof x->sig[i]);
    x->pending = 0;
    strcpy(x->exe, file);
    uint64_t phdr_va;
    r = elf_load(p, file, &e, &phdr_va);
    if (r == 0) r = build_stack(p, &s, phdr_va, e.eh.phnum, path);
    proc_mem_free(scratch, 1);
    proc_flush_tlb();
    if (r < 0) {
        console_puts("execve: out of memory\n");
        die_by_signal(p, SIGSEGV);
    }
    memset(f, 0, sizeof *f);
    f->elr = x->entry;
    f->sp_el0 = x->sp;
    f->spsr = 0;
    p->tpidr = 0;
    __asm__ volatile ("msr tpidr_el0, xzr");
    set_name(p, argv0);
    x->execed = 1;
    return 0;
}

static int wait_status(proc_t *q) {
    int sig = LX(q)->term_sig;
    if (!sig && q->exit_code == EXIT_KILLED) sig = SIGKILL;
    if (!sig && q->exit_code == EXIT_CRASHED) sig = SIGSEGV;
    if (sig) return sig & 0x7F;
    return (int)((q->exit_code & 0xFF) << 8);
}

static int64_t sys_wait4(proc_t *p, int64_t pid, uint64_t ustatus, int options, uint64_t urusage) {
    if (ustatus && !uok(p, ustatus, 4)) return -EFAULT;
    if (urusage && !uok(p, urusage, 144)) return -EFAULT;
    for (;;) {
        int have = 0;
        for (int i = 1; i < NPROC; i++) {
            proc_t *q = proc_slot(i);
            if (q->state == P_FREE || !q->linux || q->parent != p->pid) continue;
            if (pid > 0 && q->pid != pid) continue;
            have = 1;
            if (q->state == P_ZOMBIE) {
                if (ustatus) *(int32_t *)ustatus = wait_status(q);
                if (urusage) {                      // struct rusage: only the user time is known
                    memset((void *)urusage, 0, 144);
                    ((int64_t *)urusage)[0] = (int64_t)(q->cpu_ticks / TICKS_PER_SECOND);
                    ((int64_t *)urusage)[1] = (int64_t)(q->cpu_ticks % TICKS_PER_SECOND * 10000);
                }
                int qp = q->pid;
                proc_reap(q);
                return qp;
            }
        }
        if (!have) return -ECHILD;
        if (options & 1) return 0;                  // WNOHANG
        if (interrupted(p)) return -EINTR;
        p->waiting = 1;
        console_poll();
        p->waiting = 0;
    }
}

void linux_exit(proc_t *p) {
    lx_t *x = LX(p);
    proc_t *parent = p->parent > 0 ? proc_by_pid(p->parent) : 0;
    if (parent && parent->linux && parent->state == P_READY) lx_signal(parent, SIGCHLD);
    for (int i = 0; i < NFD; i++)
        if (x->fd[i] >= 0) fd_close(x, i);
    for (int i = 1; i < NPROC; i++) {               // its children: nobody waits for them now
        proc_t *q = proc_slot(i);
        if (q->state == P_FREE || !q->linux || q->parent != p->pid) continue;
        if (q->state == P_ZOMBIE) q->state = P_FREE;
        else q->parent = -1;
    }
}

static int64_t sys_kill(proc_t *p, int64_t pid, int sig) {
    if (sig < 0 || sig >= NSIG) return -EINVAL;
    int found = 0;
    for (int i = 1; i < NPROC; i++) {
        proc_t *q = proc_slot(i);
        if (q->state != P_READY || q->kernel) continue;
        if (pid > 0 && q->pid != pid) continue;
        if (pid == 0 || pid < -1) {                 // a process group: the tree it heads
            int g = pid == 0 ? p->pid : (int)-pid;
            if (!q->linux || !in_fg_group(q, g)) continue;
        }
        if (pid == -1 && (q == p || !q->linux)) continue;
        found = 1;
        if (sig) lx_signal(q, sig);
    }
    if (!found) return -ESRCH;
    if (p->killed) proc_exit(EXIT_KILLED);
    return 0;
}

// ---------------------------------------------------------------
//  everything else
// ---------------------------------------------------------------
static int64_t sys_uname(proc_t *p, uint64_t a) {
    if (!uok(p, a, 65 * 6)) return -EFAULT;
    static const char *const f[6] = { "Linux", "myos", "6.1.0-myos", "#1 MyOS", "aarch64", "(none)" };
    char *o = (char *)a;
    memset(o, 0, 65 * 6);
    for (int i = 0; i < 6; i++) strcpy(o + 65 * i, f[i]);
    return 0;
}

static int64_t sys_sysinfo(proc_t *p, uint64_t a) {
    if (!uok(p, a, 112)) return -EFAULT;
    uint8_t *o = (uint8_t *)a;
    memset(o, 0, 112);
    uint64_t v = ticks / TICKS_PER_SECOND;
    memcpy(o, &v, 8);                               // uptime
    v = (uint64_t)proc_total_blocks() * BLOCK;
    memcpy(o + 32, &v, 8);                          // totalram
    v = (uint64_t)proc_free_blocks() * BLOCK;
    memcpy(o + 40, &v, 8);                          // freeram
    uint16_t n = 0;
    for (int i = 0; i < NPROC; i++) n += proc_slot(i)->state != P_FREE;
    memcpy(o + 80, &n, 2);                          // procs
    uint32_t unit = 1;
    memcpy(o + 104, &unit, 4);                      // mem_unit
    return 0;
}

static int64_t sys_clock_gettime(proc_t *p, int clk, uint64_t a) {
    if (!uok(p, a, 16)) return -EFAULT;
    uint64_t ns = now_ns();
    if (clk == 0 || clk == 5 || clk == 8) ns += rt_offset * 1000000000ull;     // REALTIME (COARSE, ALARM)
    else if (clk == 2 || clk == 3) ns = p->cpu_ticks * (1000000000ull / TICKS_PER_SECOND);
    ((int64_t *)a)[0] = (int64_t)(ns / 1000000000ull);
    ((int64_t *)a)[1] = (int64_t)(ns % 1000000000ull);
    return 0;
}

static int64_t sleep_until(proc_t *p, uint64_t until_ns) {
    p->waiting = 1;
    while (now_ns() < until_ns) {
        if (interrupted(p)) {
            p->waiting = 0;
            return -EINTR;
        }
        console_poll();
    }
    p->waiting = 0;
    return 0;
}

static int64_t sys_nanosleep(proc_t *p, uint64_t req, int abs, int clk) {
    if (!uok(p, req, 16)) return -EFAULT;
    int64_t sec = ((int64_t *)req)[0], ns = ((int64_t *)req)[1];
    if (sec < 0 || ns < 0 || ns >= 1000000000) return -EINVAL;
    uint64_t t = (uint64_t)sec * 1000000000ull + (uint64_t)ns;
    if (abs) {
        if (clk == 0) t = t > rt_offset * 1000000000ull ? t - rt_offset * 1000000000ull : 0;
        return sleep_until(p, t);
    }
    return sleep_until(p, now_ns() + t);
}

static int64_t sys_prlimit(proc_t *p, int res, uint64_t newl, uint64_t oldl) {
    (void)newl;
    if (!oldl) return 0;
    if (!uok(p, oldl, 16)) return -EFAULT;
    uint64_t v = ~0ull;
    if (res == 3) v = STACK_SIZE;                   // RLIMIT_STACK
    if (res == 7) v = NFD;                          // RLIMIT_NOFILE
    ((uint64_t *)oldl)[0] = ((uint64_t *)oldl)[1] = v;
    return 0;
}

static int64_t sys_rt_sigaction(proc_t *p, int sig, uint64_t act, uint64_t old) {
    lx_t *x = LX(p);
    if (sig < 1 || sig >= NSIG) return -EINVAL;
    if (old) {
        if (!uok(p, old, 32)) return -EFAULT;
        memcpy((void *)old, &x->sig[sig], 32);
    }
    if (act) {
        if (sig == SIGKILL || sig == 19) return -EINVAL;
        if (!uok(p, act, 32)) return -EFAULT;
        memcpy(&x->sig[sig], (void *)act, 32);
    }
    return 0;
}

static int64_t sys_rt_sigprocmask(proc_t *p, int how, uint64_t set, uint64_t old) {
    lx_t *x = LX(p);
    if (old) {
        if (!uok(p, old, 8)) return -EFAULT;
        *(uint64_t *)old = x->sigmask;
    }
    if (set) {
        if (!uok(p, set, 8)) return -EFAULT;
        uint64_t s = *(uint64_t *)set;
        if (how == 0) x->sigmask |= s;
        else if (how == 1) x->sigmask &= ~s;
        else if (how == 2) x->sigmask = s;
        else return -EINVAL;
        x->sigmask &= ~(SIG_BIT(SIGKILL) | SIG_BIT(SIGSTOP));
    }
    return 0;
}

static int64_t sys_sendfile(proc_t *p, file_t *out, file_t *in, uint64_t uoff, uint64_t count) {
    // through a bounce buffer on the program's stack is not possible: use a kernel one
    static uint8_t bounce[4096];
    (void)p;
    if (uoff) return -EINVAL;
    if (in->type != FT_FILE || in->buf) return -EINVAL;
    if (out->type != FT_TTY && out->type != FT_NULL) return -EINVAL;   // (the program falls back
                                                                        //  to read + write)
    int64_t total = 0;
    while ((uint64_t)total < count) {
        uint64_t n = count - (uint64_t)total;
        if (n > sizeof bounce) n = sizeof bounce;
        if (in->off >= in->size) break;
        if (n > in->size - in->off) n = in->size - in->off;
        int got = myfs_read(in->name, (uint32_t)in->off, bounce, (uint32_t)n);
        if (got <= 0) break;
        in->off += (uint64_t)got;
        int64_t w;
        if (out->type == FT_TTY) {
            for (int i = 0; i < got; i++) console_putc((char)bounce[i]);
            console_puts("");
            w = got;
        } else if (out->type == FT_NULL) {
            w = got;
        } else {
            return total ? total : -EINVAL;
        }
        total += w;
    }
    return total;
}

// names for strace
static const char *sys_name(uint64_t n) {
    switch (n) {
        case 17: return "getcwd"; case 23: return "dup"; case 24: return "dup3"; case 25: return "fcntl";
        case 29: return "ioctl"; case 34: return "mkdirat"; case 35: return "unlinkat";
        case 38: return "renameat"; case 48: return "faccessat"; case 49: return "chdir";
        case 56: return "openat"; case 57: return "close"; case 59: return "pipe2";
        case 61: return "getdents64"; case 62: return "lseek"; case 63: return "read";
        case 64: return "write"; case 65: return "readv"; case 66: return "writev";
        case 67: return "pread64"; case 68: return "pwrite64"; case 71: return "sendfile";
        case 72: return "pselect6"; case 73: return "ppoll"; case 78: return "readlinkat";
        case 79: return "newfstatat"; case 80: return "fstat"; case 93: return "exit";
        case 94: return "exit_group"; case 96: return "set_tid_address"; case 98: return "futex";
        case 99: return "set_robust_list"; case 101: return "nanosleep"; case 113: return "clock_gettime";
        case 115: return "clock_nanosleep"; case 129: return "kill"; case 134: return "rt_sigaction";
        case 135: return "rt_sigprocmask"; case 160: return "uname"; case 172: return "getpid";
        case 179: return "sysinfo"; case 214: return "brk"; case 215: return "munmap";
        case 216: return "mremap"; case 220: return "clone"; case 221: return "execve";
        case 222: return "mmap"; case 226: return "mprotect"; case 260: return "wait4";
        case 261: return "prlimit64"; case 278: return "getrandom";
    }
    return 0;
}

static int64_t dispatch(proc_t *p, trapframe_t *f, uint64_t n, uint64_t a0, uint64_t a1, uint64_t a2,
                        uint64_t a3, uint64_t a4, uint64_t a5, int *known);

static void linux_trace(proc_t *p, uint64_t n, uint64_t a0, uint64_t a1, uint64_t a2, int64_t r, int known) {
    if (!console_at_line_start()) console_putc('\n');
    console_puts("[");
    print_dec((uint64_t)p->pid);
    console_puts("] ");
    const char *nm = sys_name(n);
    if (nm) {
        console_puts(nm);
    } else {
        console_puts(known ? "syscall " : "UNKNOWN syscall ");
        print_dec(n);
    }
    console_putc('(');
    put_hex(a0);
    console_puts(", ");
    put_hex(a1);
    console_puts(", ");
    put_hex(a2);
    console_puts(") = ");
    print_signed(r);
    console_puts("\n");
}

void linux_syscall(trapframe_t *f) {
    proc_t *p = proc_current();
    uint64_t n = f->x[8];
    int known = 1;
    uint64_t a0 = f->x[0], a1 = f->x[1], a2 = f->x[2];
    if (LX(p)->trace && (n == 93 || n == 94 || n == 221)) linux_trace(p, n, a0, a1, a2, 0, 1);
    int64_t r = dispatch(p, f, n, a0, a1, a2, f->x[3], f->x[4], f->x[5], &known);
    // unknown calls are only shown under strace: programs probe for new ones
    if (LX(p)->trace) linux_trace(p, n, a0, a1, a2, r, known);
    if (known != 2 && (n != 221 || r < 0)) f->x[0] = (uint64_t)r;   // (execve, sigreturn set them)
    deliver_signals(p, f, known == 2 ? 0 : r, a0);
}

static int64_t dispatch(proc_t *p, trapframe_t *f, uint64_t n, uint64_t a0, uint64_t a1, uint64_t a2,
                        uint64_t a3, uint64_t a4, uint64_t a5, int *known) {
    lx_t *x = LX(p);
    file_t *fl;
    char name[MYFS_PATH_MAX], name2[MYFS_PATH_MAX];
    int fdno, kind;
    kstat_t st;
    switch (n) {
        // --- files
        case 56:                                    // openat(dirfd, path, flags, mode)
            return do_open(p, a1, (int)a2);
        case 57:                                    // close
            return fd_close(x, (int)a0);
        case 63:                                    // read
            if (!(fl = fget(x, (int64_t)a0))) return -EBADF;
            return do_read(p, fl, a1, a2, -1);
        case 64:                                    // write
            if (!(fl = fget(x, (int64_t)a0))) return -EBADF;
            return do_write(p, fl, a1, a2, -1);
        case 65: case 66:                           // readv, writev
            if (!(fl = fget(x, (int64_t)a0))) return -EBADF;
            return do_iov(p, fl, a1, (int64_t)a2, n == 66, -1);
        case 67: case 68:                           // pread64, pwrite64
            if (!(fl = fget(x, (int64_t)a0))) return -EBADF;
            if (fl->type != FT_FILE) return -ESPIPE;
            return n == 67 ? do_read(p, fl, a1, a2, (int64_t)a3) : do_write(p, fl, a1, a2, (int64_t)a3);
        case 69: case 70:                           // preadv, pwritev
            if (!(fl = fget(x, (int64_t)a0))) return -EBADF;
            if (fl->type != FT_FILE) return -ESPIPE;
            return do_iov(p, fl, a1, (int64_t)a2, n == 70, (int64_t)a3);
        case 62:                                    // lseek
            if (!(fl = fget(x, (int64_t)a0))) return -EBADF;
            return sys_lseek(fl, (int64_t)a1, (int)a2);
        case 61:                                    // getdents64
            if (!(fl = fget(x, (int64_t)a0))) return -EBADF;
            return sys_getdents64(p, fl, a1, a2);
        case 71:                                    // sendfile(out, in, offset, count)
            if (!(fl = fget(x, (int64_t)a0)) || !fget(x, (int64_t)a1)) return -EBADF;
            return sys_sendfile(p, fl, fget(x, (int64_t)a1), a2, a3);
        case 80:                                    // fstat
            if (!(fl = fget(x, (int64_t)a0))) return -EBADF;
            if (!uok(p, a1, sizeof st)) return -EFAULT;
            stat_file(fl, &st);
            memcpy((void *)a1, &st, sizeof st);
            return 0;
        case 79: {                                  // newfstatat(dirfd, path, buf, flags)
            if (!uok(p, a2, sizeof st)) return -EFAULT;
            char path[256];
            int len = ustr(p, a1, path, sizeof path);
            if (len < 0) return len;
            int r;
            if (len == 0) {
                if (!(a3 & AT_EMPTY_PATH)) return -ENOENT;
                if (!(fl = fget(x, (int64_t)a0))) return -EBADF;
                r = stat_file(fl, &st);
            } else {
                kind = resolve(path, name, &fdno);
                if (kind < 0) return kind;
                if (kind == R_FD) {
                    if (!(fl = fget(x, fdno))) return -ENOENT;
                    r = stat_file(fl, &st);
                } else {
                    r = stat_kind(kind, name, &st);
                }
            }
            if (r == 0) memcpy((void *)a2, &st, sizeof st);
            return r;
        }
        case 48: case 439:                          // faccessat, faccessat2
            kind = upath(p, a1, name, &fdno);
            if (kind < 0) return kind;
            if (kind == R_FILE && myfs_size(name) < 0) return -ENOENT;
            if (kind == R_FILE && (a2 & 1) && !is_program(name)) return -EACCES;
            return 0;
        case 35:                                    // unlinkat(dirfd, path, flags)
            kind = upath(p, a1, name, &fdno);
            if (kind < 0) return kind;
            if (kind == R_DIR) {
                if (!(a2 & AT_REMOVEDIR)) return -EISDIR;
                if (!strcmp(name, "/")) return -EBUSY;
                return (kind = myfs_rmdir(name)) ? fs_err(kind) : 0;
            }
            if (kind != R_FILE) return -EPERM;
            if (a2 & AT_REMOVEDIR) return myfs_size(name) < 0 ? -ENOENT : -ENOTDIR;
            if (!strcmp(name, ecache.path)) ecache_drop();
            return (kind = myfs_remove(name)) ? fs_err(kind) : 0;
        case 38: case 276:                          // renameat, renameat2(olddir, old, newdir, new)
            if ((kind = upath(p, a1, name, &fdno)) < 0) return kind;
            if (kind != R_FILE && kind != R_DIR) return -EPERM;
            if ((kind = upath(p, a3, name2, &fdno)) < 0) return kind;
            if (kind != R_FILE && kind != R_DIR) return -EPERM;
            if (!strcmp(name, name2)) return 0;
            if (!strcmp(name, ecache.path) || !strcmp(name2, ecache.path)) ecache_drop();
            if (myfs_stat(name2, 0) == MYFS_FILE && myfs_stat(name, 0) == MYFS_FILE) myfs_remove(name2);
            return (kind = myfs_rename(name, name2)) ? fs_err(kind) : 0;
        case 34:                                    // mkdirat(dirfd, path, mode)
            kind = upath(p, a1, name, &fdno);
            if (kind < 0) return kind;
            if (kind != R_FILE) return -EEXIST;
            if (myfs_size(name) >= 0) return -EEXIST;
            return (kind = myfs_mkdir(name)) ? fs_err(kind) : 0;
        case 37: case 36:                           // linkat, symlinkat
            return -EPERM;
        case 43:                                    // statfs(path, buf)
            kind = upath(p, a0, name, &fdno);
            if (kind < 0) return kind;
            return sys_statfs(p, kind, a1);
        case 44:                                    // fstatfs(fd, buf)
            if (!(fl = fget(x, (int64_t)a0))) return -EBADF;
            return sys_statfs(p, fl->type == FT_PROC || fl->type == FT_PROCDIR ? R_PROCDIR : R_DIR, a1);
        case 78: {                                  // readlinkat(dirfd, path, buf, size)
            kind = upath(p, a1, name, &fdno);
            char path[256];
            if (ustr(p, a1, path, sizeof path) < 0) return -EFAULT;
            if (!strcmp(path, "/proc/self")) {
                char d[12];
                int k = 0, v = p->pid;
                do { d[k++] = (char)('0' + v % 10); v /= 10; } while (v);
                if ((uint64_t)k > a3) k = (int)a3;
                if (!uok(p, a2, (uint64_t)k)) return -EFAULT;
                for (int j = 0; j < k; j++) ((char *)a2)[j] = d[k - 1 - j];
                return k;
            }
            if (strcmp(path, "/proc/self/exe")) return kind < 0 ? kind : -EINVAL;
            uint64_t len = strlen(x->exe);
            if (len > a3) len = a3;
            if (!uok(p, a2, len)) return -EFAULT;
            memcpy((char *)a2, x->exe, len);
            return (int64_t)len;
        }
        case 17: {                                  // getcwd
            const char *cwd = proc_cwd();
            uint64_t len = strlen(cwd) + 1;
            if (a1 < len) return -ERANGE;
            if (!uok(p, a0, len)) return -EFAULT;
            memcpy((void *)a0, cwd, len);
            return (int64_t)len;
        }
        case 49:                                    // chdir: a MyFS folder, /proc or /proc/PID
            kind = upath(p, a0, name, &fdno);
            if (kind < 0) return kind;
            if (kind == R_DIR) {
                strcpy(p->cwd, name);
                return 0;
            }
            if (kind == R_PROCDIR) {
                set_cwd_proc(p, fdno);
                return 0;
            }
            return kind == R_FILE && myfs_size(name) < 0 ? -ENOENT : -ENOTDIR;
        case 50:                                    // fchdir
            if (!(fl = fget(x, (int64_t)a0))) return -EBADF;
            if (fl->type == FT_DIR) strcpy(p->cwd, fl->name);
            else if (fl->type == FT_PROCDIR) set_cwd_proc(p, fl->pid);
            else return -ENOTDIR;
            return 0;
        case 23:                                    // dup
            if (!(fl = fget(x, (int64_t)a0))) return -EBADF;
            if ((kind = fd_alloc(x, 0)) < 0) return kind;
            x->fd[kind] = x->fd[a0];
            fl->refs++;
            x->cloexec &= ~(1u << kind);
            return kind;
        case 24:                                    // dup3(old, new, flags)
            if (!(fl = fget(x, (int64_t)a0))) return -EBADF;
            if (a0 == a1) return -EINVAL;
            if (a1 >= NFD) return -EBADF;
            fl->refs++;
            if (x->fd[a1] >= 0) fd_close(x, (int)a1);
            x->fd[a1] = x->fd[a0];
            if (a2 & O_CLOEXEC) x->cloexec |= 1u << a1;
            else x->cloexec &= ~(1u << a1);
            return (int64_t)a1;
        case 25:                                    // fcntl
            if (!(fl = fget(x, (int64_t)a0))) return -EBADF;
            switch (a1) {
                case 0: case 1030:                  // F_DUPFD, F_DUPFD_CLOEXEC
                    if (a2 >= NFD) return -EINVAL;
                    if ((kind = fd_alloc(x, (int)a2)) < 0) return kind;
                    x->fd[kind] = x->fd[a0];
                    fl->refs++;
                    if (a1 == 1030) x->cloexec |= 1u << kind;
                    else x->cloexec &= ~(1u << kind);
                    return kind;
                case 1: return (x->cloexec >> a0) & 1;          // F_GETFD
                case 2:                                         // F_SETFD
                    if (a2 & 1) x->cloexec |= 1u << a0;
                    else x->cloexec &= ~(1u << a0);
                    return 0;
                case 3: return fl->flags;                       // F_GETFL
                case 4:                                         // F_SETFL
                    fl->flags = (fl->flags & ~(O_APPEND | O_NONBLOCK)) | ((int)a2 & (O_APPEND | O_NONBLOCK));
                    return 0;
                case 5: case 6: case 7: return 0;               // locks: always granted
            }
            return -EINVAL;
        case 29:                                    // ioctl
            if (!(fl = fget(x, (int64_t)a0))) return -EBADF;
            return sys_ioctl(p, fl, a1, a2);
        case 59:                                    // pipe2
            return sys_pipe2(p, a0, (int)a1);
        case 72:                                    // pselect6
            return sys_pselect6(p, (int64_t)a0, a1, a2, a3, a4);
        case 73:                                    // ppoll
            return sys_ppoll(p, a0, a1, a2);
        case 46:                                    // ftruncate
            if (!(fl = fget(x, (int64_t)a0))) return -EBADF;
            if (fl->type != FT_FILE || !fl->buf) return -EINVAL;
            if (file_reserve(fl, a1 ? a1 : 1) < 0) return -EFBIG;
            if (a1 > fl->size) memset(fl->buf + fl->size, 0, a1 - fl->size);
            fl->size = a1;
            fl->dirty = 1;
            return 0;
        case 82: case 83: case 81:                  // fsync, fdatasync, sync
        case 88: case 52: case 53: case 54: case 55:    // utimensat, fchmod, fchmodat, fchownat, fchown
            return 0;
        // --- memory
        case 214: return sys_brk(p, a0);
        case 222: return sys_mmap(p, a0, a1, (int)a2, (int)a3, (int64_t)a4, a5);
        case 215: return sys_munmap(p, a0, a1);
        case 216: return sys_mremap(p, a0, a1, a2, (int)a3);
        case 226: case 233: case 227: case 228: case 229:   // mprotect, madvise, msync, mlock, munlock
            return 0;
        // --- processes
        case 93: case 94:                           // exit, exit_group
            proc_exit((int64_t)(a0 & 0xFF));
        case 220:                                   // clone(flags, stack, ptid, tls, ctid)
            return sys_clone(p, f, a0, a1, a2, a3, a4);
        case 221:                                   // execve
            return sys_execve(p, f, a0, a1, a2);
        case 260:                                   // wait4
            return sys_wait4(p, (int64_t)a0, a1, (int)a2, a3);
        case 95: {                                  // waitid(P_ALL/P_PID, id, info, options)
            return -ENOSYS;
        }
        case 129:                                   // kill
            return sys_kill(p, (int64_t)a0, (int)a1);
        case 130: case 131:                         // tkill, tgkill: abort() etc.
            if ((int)(n == 131 ? a1 : a0) != p->pid) return -ESRCH;
            if ((n == 131 ? a2 : a1) >= NSIG) return -EINVAL;
            lx_signal(p, (int)(n == 131 ? a2 : a1));
            if (p->killed) proc_exit(EXIT_KILLED);
            return 0;
        case 172: case 178: return p->pid;          // getpid, gettid
        case 173: return p->parent > 0 ? p->parent : 1;     // getppid
        case 174: case 175: case 176: case 177: return 0;   // getuid, geteuid, getgid, getegid
        case 155: case 156: {                       // getpgid, getsid: the job's first process
            proc_t *q = a0 ? proc_by_pid((int)a0) : p;
            if (!q) return -ESRCH;
            for (int d = 0; d < NPROC && q->parent > 0; d++) {
                proc_t *up = proc_by_pid(q->parent);
                if (!up) break;
                q = up;
            }
            return q->pid;
        }
        case 154: case 144: case 146: case 147: case 149: case 143: case 145: case 159:
            return 0;                               // setpgid, setgid, setuid, setresuid, setresgid...
        case 157: return p->pid;                    // setsid
        case 158: return 0;                         // getgroups: none
        case 148: case 150:                         // getresuid, getresgid
            if (!uok(p, a0, 4) || !uok(p, a1, 4) || !uok(p, a2, 4)) return -EFAULT;
            *(uint32_t *)a0 = *(uint32_t *)a1 = *(uint32_t *)a2 = 0;
            return 0;
        case 96: return p->pid;                     // set_tid_address
        case 99: return 0;                          // set_robust_list
        case 98:                                    // futex: one thread, so nobody to wait for
            return (a1 & 127) == 1 ? 0 : -EAGAIN;
        case 124:                                   // sched_yield
            proc_yield();
            return 0;
        case 123:                                   // sched_getaffinity: one CPU
            if (a1 < 8 || !uok(p, a2, 8)) return -EINVAL;
            *(uint64_t *)a2 = 1;
            return 8;
        case 167:                                   // prctl
            if (a0 == 15 && ustr(p, a1, name, 16) >= 0) set_name(p, name);    // PR_SET_NAME
            return 0;
        case 134: return sys_rt_sigaction(p, (int)a0, a1, a2);
        case 135: return sys_rt_sigprocmask(p, (int)a0, a1, a2);
        case 132:                                   // sigaltstack: none (handlers use the stack)
            if (a1 && uok(p, a1, 24)) {
                memset((void *)a1, 0, 24);
                ((uint32_t *)a1)[2] = 2;            // SS_DISABLE
            }
            return 0;
        case 136:                                   // rt_sigpending
            if (!uok(p, a0, 8)) return -EFAULT;
            *(uint64_t *)a0 = x->pending & x->sigmask;
            return 0;
        case 133:                                   // rt_sigsuspend(mask): wait for a signal
            if (!uok(p, a0, 8)) return -EFAULT;
            x->saved_mask = x->sigmask;
            x->restore_mask = 1;
            x->sigmask = *(uint64_t *)a0 & ~(SIG_BIT(SIGKILL) | SIG_BIT(SIGSTOP));
            p->waiting = 1;
            while (!interrupted(p)) console_poll();
            p->waiting = 0;
            return -EINTR;
        case 139:                                   // rt_sigreturn: back from a handler
            *known = 2;
            return sys_rt_sigreturn(p, f);
        // --- time
        case 113: return sys_clock_gettime(p, (int)a0, a1);
        case 114:                                   // clock_getres
            if (a1) {
                if (!uok(p, a1, 16)) return -EFAULT;
                ((int64_t *)a1)[0] = 0;
                ((int64_t *)a1)[1] = 1;
            }
            return 0;
        case 169: {                                 // gettimeofday
            if (a0) {
                if (!uok(p, a0, 16)) return -EFAULT;
                uint64_t ns = now_ns() + rt_offset * 1000000000ull;
                ((int64_t *)a0)[0] = (int64_t)(ns / 1000000000ull);
                ((int64_t *)a0)[1] = (int64_t)(ns % 1000000000ull / 1000);
            }
            return 0;
        }
        case 112: case 170: {                       // clock_settime, settimeofday
            uint64_t a = n == 112 ? a1 : a0;
            if (n == 112 && a0 != 0) return -EINVAL;
            if (!uok(p, a, 16)) return -EFAULT;
            int64_t sec = ((int64_t *)a)[0];
            uint64_t up = now_ns() / 1000000000ull;
            rt_offset = sec > (int64_t)up ? (uint64_t)sec - up : 0;
            return 0;
        }
        case 101: return sys_nanosleep(p, a0, 0, 1);
        case 115: return sys_nanosleep(p, a2, (int)(a1 & 1), (int)a0);
        case 153: {                                 // times
            if (a0) {
                if (!uok(p, a0, 32)) return -EFAULT;
                memset((void *)a0, 0, 32);
                ((uint64_t *)a0)[0] = p->cpu_ticks;
            }
            return (int64_t)ticks;
        }
        case 165:                                   // getrusage
            if (!uok(p, a1, 144)) return -EFAULT;
            memset((void *)a1, 0, 144);
            ((int64_t *)a1)[0] = (int64_t)(p->cpu_ticks / TICKS_PER_SECOND);
            ((int64_t *)a1)[1] = (int64_t)(p->cpu_ticks % TICKS_PER_SECOND * 10000);
            return 0;
        // --- system
        case 160: return sys_uname(p, a0);
        case 179: return sys_sysinfo(p, a0);
        case 163: return sys_prlimit(p, (int)a0, 0, a1);    // getrlimit
        case 164: return 0;                                  // setrlimit
        case 261: return sys_prlimit(p, (int)a1, a2, a3);   // prlimit64
        case 166: return 022;                       // umask
        case 278:                                   // getrandom
            if (!uok(p, a0, a1)) return -EFAULT;
            random_bytes((void *)a0, a1);
            return (int64_t)a1;
        case 293: case 283: case 291:               // rseq, membarrier, statx: not here (programs fall back)
            return -ENOSYS;
    }
    *known = 0;
    return -ENOSYS;
}
