// proc.c - processes: several programs at the same time on MyOS-ARM.
//
// Every program believes it owns 0x400000-0x5FFFFF (code, data, stack) and
// 0x02000000-0x03FFFFFF (heap), like before. Each process has its own MMU
// tables that point those addresses at its own 2 MB blocks of RAM, taken
// from a pool that starts at 0x04000000 and runs to the end of the RAM the
// firmware gives the ARM (asked at boot; 448 MB on a Pi Zero 2 W with the
// default 64 MB for the GPU). Heap blocks are handed out only when the
// program first touches them (a "page fault").
//
// Every process has a kernel stack; switching between processes means
// switching kernel stacks (proc_switch in vectors.S) and MMU tables.
// The timer interrupt switches when a program has run for a while; the
// kernel itself only switches at proc_yield() (so kernel code never has
// to worry about being interrupted halfway).
#include <string.h>
#include "broadcom/caches.h"
#include "broadcom/vcmailbox.h"
#include "console.h"
#include "myfs.h"
#include "proc.h"
#include "timer.h"
#include "user.h"

extern uint64_t level_1_table[512];                          // broadcom/mmu.c
extern uint64_t level_2_0x0_0000_0000_to_0x0_4000_0000[512];
extern void proc_switch(uint64_t *old_ctx, uint64_t *new_ctx);
extern void proc_trampoline(void);
extern void fp_save(uint64_t *state);
extern void fp_restore(uint64_t *state);
extern uint64_t enter_user(uint64_t entry, uint64_t sp, uint64_t arg, uint64_t *ctx);

#define POOL_START   0x04000000UL
#define POOL_END_DEFAULT 0x14000000UL   // if the firmware can't tell us: 256 MB, fits every Pi
#define POOL_END_MAX 0x3F000000UL       // the kernel's map has normal RAM only below this
#define BLOCK        0x200000UL
#define POOL_MAX_BLOCKS ((POOL_END_MAX - POOL_START) / BLOCK)   // 472 x 2 MB
#define KSTACK_SIZE  (32 * 1024)

#define DESC_AP_EL0_RW  (1ull << 6)
#define DESC_PXN        (1ull << 53)
#define DESC_UXN        (1ull << 54)
#define DESC_ADDR_MASK  0x0000FFFFFFE00000ull

static proc_t procs[NPROC];
static proc_t *cur;
static int next_pid = 1;
static int tty_fg[NTTY];                // foreground program of each terminal (0 = its shell)
static int tty_alive[NTTY] = { 1 };
static uint8_t pool_used[POOL_MAX_BLOCKS];
static uint32_t pool_blocks = (POOL_END_DEFAULT - POOL_START) / BLOCK;
static uint64_t l1_tables[NPROC][512] __attribute__((aligned(4096)));
static uint64_t l2_tables[NPROC][512] __attribute__((aligned(4096)));
static uint8_t kstacks[NPROC][KSTACK_SIZE] __attribute__((aligned(16)));
static uint64_t kernel_ttbr0, mem_attrs;
static uint64_t dummy_ctx[13];
volatile int proc_need_resched;

static uint64_t block_addr(int b) { return POOL_START + (uint64_t)b * BLOCK; }

static int block_alloc(void) {
    for (uint32_t i = 0; i < pool_blocks; i++) {
        if (!pool_used[i]) {
            pool_used[i] = 1;
            memset((void *)block_addr((int)i), 0, BLOCK);   // no leftovers from others
            return (int)i;
        }
    }
    return -1;
}

uint32_t proc_free_blocks(void) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < pool_blocks; i++) n += !pool_used[i];
    return n;
}

uint32_t proc_total_blocks(void) { return pool_blocks; }

// Ask the firmware where the ARM's RAM ends (the rest belongs to the GPU)
// and make the pool reach up to there.
extern bool vcmailbox_request(volatile vcmailbox_buffer_t *buffer);   // broadcom/vcmailbox.c

static void pool_size_from_firmware(void) {
    struct {
        uint32_t buffer_size, code;             // = vcmailbox_buffer_t
        vcmailbox_get_arm_memory_t tag;
        uint32_t end_tag;
    } __attribute__((aligned(16))) msg;
    memset(&msg, 0, sizeof msg);
    msg.buffer_size = sizeof msg;
    msg.tag = VCMAILBOX_GET_ARM_MEMORY_DEFAULTS;
    if (!vcmailbox_request((volatile vcmailbox_buffer_t *)&msg)) return;
    uint64_t end = (uint64_t)msg.tag.response.base_address + msg.tag.response.size;
    if (end > POOL_END_MAX) end = POOL_END_MAX;
    if (end < POOL_START + BLOCK) return;       // nonsense answer: keep the default
    pool_blocks = (uint32_t)((end - POOL_START) / BLOCK);
}

static uint64_t idle_ticks;

void proc_tick(void) {
    if (!cur) return;
    if (cur->kernel || cur->waiting) idle_ticks++;  // a shell, or a program waiting for keys/time
    else cur->cpu_ticks++;
}

uint64_t proc_idle_ticks(void) { return idle_ticks; }

uint32_t proc_used_blocks(const proc_t *p) {
    uint32_t n = p->code_block >= 0;
    for (int i = 0; i < 16; i++) n += p->heap[i] >= 0;
    return n;
}

static void flush_tlb(void) {
    __asm__ volatile ("dsb ishst; tlbi vmalle1; dsb ish; isb" ::: "memory");
}

void proc_init(void) {
    __asm__ volatile ("mrs %0, ttbr0_el1" : "=r"(kernel_ttbr0));
    // memory attributes of normal RAM, from the kernel's own map
    mem_attrs = level_2_0x0_0000_0000_to_0x0_4000_0000[POOL_START / BLOCK] & ~DESC_ADDR_MASK;
    pool_size_from_firmware();
    procs[0].state = P_READY;
    procs[0].pid = 0;
    procs[0].slot = 0;
    procs[0].code_block = -1;
    for (int i = 0; i < 16; i++) procs[0].heap[i] = -1;
    strcpy(procs[0].name, "shell");
    procs[0].kernel = 1;
    procs[0].tty = 0;
    cur = &procs[0];
}

proc_t *proc_current(void) { return cur; }
proc_t *proc_slot(int i) { return (i >= 0 && i < NPROC) ? &procs[i] : 0; }

proc_t *proc_by_pid(int pid) {
    for (int i = 0; i < NPROC; i++)
        if (procs[i].state != P_FREE && procs[i].pid == pid) return &procs[i];
    return 0;
}

int proc_foreground(void) { return tty_fg[cur->tty]; }
void proc_set_foreground(int pid) { tty_fg[cur->tty] = pid; }

int proc_interrupt(int tty) {
    return tty_fg[tty] && proc_kill(tty_fg[tty]) == 0;
}

void proc_tty_open(int tty) {
    tty_alive[tty] = 1;
    tty_fg[tty] = 0;
}

void proc_tty_close(int tty) {
    if (tty == 0) return;
    tty_alive[tty] = 0;
    tty_fg[tty] = 0;
    for (int i = 1; i < NPROC; i++) {
        proc_t *p = &procs[i];
        if (p->tty != tty) continue;
        if (p->state == P_READY) p->killed = 1;         // it stops at its next chance
        else if (p->state == P_ZOMBIE) p->state = P_FREE;
    }
}

static void switch_to(proc_t *next) {
    proc_t *prev = cur;
    fp_save(prev->fp);                  // each program has its own floating-point registers
    fp_restore(next->fp);
    cur = next;
    uint64_t ttbr = next->kernel ? kernel_ttbr0
                                    : (uint64_t)next->l1 | (kernel_ttbr0 & 0xFFF);
    __asm__ volatile ("dsb ish; msr ttbr0_el1, %0; isb" :: "r"(ttbr) : "memory");
    flush_tlb();
    proc_switch(prev->ctx, next->ctx);
}

void proc_yield(void) {
    if (!cur) return;
    for (int i = 1; i <= NPROC; i++) {
        proc_t *p = &procs[(cur->slot + i) % NPROC];
        if (p->state == P_READY) {
            if (p != cur) switch_to(p);
            break;
        }
    }
    if (cur->slot && cur->killed) proc_exit(EXIT_KILLED);
}

// called by vectors.S after an interrupt that came from a program
void proc_irq_return(void) {
    if (proc_need_resched) {
        proc_need_resched = 0;
        proc_yield();
    }
    if (cur->slot && cur->killed) proc_exit(EXIT_KILLED);
}

// called by vectors.S after a system call or fault from a program
void proc_user_return(void) {
    if (cur->slot && cur->killed) proc_exit(EXIT_KILLED);
}

static void free_memory(proc_t *p) {
    if (p->code_block >= 0) pool_used[p->code_block] = 0;
    p->code_block = -1;
    for (int i = 0; i < 16; i++) {
        if (p->heap[i] >= 0) pool_used[p->heap[i]] = 0;
        p->heap[i] = -1;
    }
}

void proc_exit(int64_t code) {
    __asm__ volatile ("msr daifset, #2");
    cur->exit_code = code;
    // a shell thread, or a program whose terminal is gone: nobody will ask
    cur->state = (cur->kernel || !tty_alive[cur->tty]) ? P_FREE : P_ZOMBIE;
    // back on the kernel's tables before giving our blocks away
    __asm__ volatile ("dsb ish; msr ttbr0_el1, %0; isb" :: "r"(kernel_ttbr0) : "memory");
    flush_tlb();
    free_memory(cur);
    if (tty_fg[cur->tty] == cur->pid) tty_fg[cur->tty] = 0;
    proc_yield();                       // a zombie is never picked again
    for (;;) {
    }
}

void proc_reap(proc_t *p) {
    if (p && p->slot && p->state == P_ZOMBIE) p->state = P_FREE;
}

int proc_kill(int pid) {
    proc_t *p = proc_by_pid(pid);
    if (!p || p->kernel || p->state != P_READY) return -1;
    p->killed = 1;
    return 0;
}

int proc_wait(int pid, int64_t *code) {
    proc_t *p = proc_by_pid(pid);
    while (p && p->state == P_READY && tty_fg[cur->tty] == pid) console_poll();   // (it yields)
    if (p && p->state == P_ZOMBIE) {
        *code = p->exit_code;
        proc_reap(p);
        return 1;
    }
    return 0;                           // it went to the background (or is gone)
}

// first thing a new process runs (on its own kernel stack)
static void user_start(void) {
    enter_user(cur->entry, USER_ARGS, USER_ARGS, dummy_ctx);    // never comes back here
}

int proc_spawn(const char *name, const char *args, int background) {
    int32_t size = myfs_size(name);
    if (size < 0) return -1;
    if (size < 16 || (uint32_t)size > USER_MAX_SIZE) {
        console_puts(name);
        console_puts(" is not a program.\n");
        return -2;
    }
    proc_t *p = 0;
    for (int i = 1; i < NPROC; i++) {
        if (procs[i].state == P_FREE) {
            p = &procs[i];
            memset(p, 0, sizeof *p);
            p->slot = i;
            break;
        }
    }
    if (!p) {
        console_puts("Too many programs running (try 'ps' and 'kill').\n");
        return -3;
    }
    for (int i = 0; i < 16; i++) p->heap[i] = -1;
    p->code_block = block_alloc();
    if (p->code_block < 0) {
        console_puts("Out of memory.\n");
        return -3;
    }
    uint8_t *base = (uint8_t *)block_addr(p->code_block);     // where 0x400000 really is
    if (myfs_read(name, 0, base, (uint32_t)size) != size) {
        console_puts("Disk error!\n");
        free_memory(p);
        return -2;
    }
    int arch = program_header_arch(base, (uint32_t)size);
    uint64_t entry = (uint64_t)base[8] | (uint64_t)base[9] << 8 | (uint64_t)base[10] << 16 |
                     (uint64_t)base[11] << 24;
    if (arch != MYEX_ARCH_ARM64 || entry < USER_BASE + 16 || entry >= USER_BASE + (uint64_t)size) {
        console_puts(name);
        console_puts(arch == MYEX_ARCH_X86 ? " is a program for MyOS PC (x86), not for the Pi.\n"
                                           : " is not a program.\n");
        free_memory(p);
        return -2;
    }
    size_t n = strlen(args);
    if (n > 250) n = 250;
    memcpy(base + (USER_ARGS - USER_BASE), args, n);          // the stack starts below this
    // the code was written as data: push it to RAM, forget old instructions
    data_clean_and_invalidate(base, BLOCK);
    __asm__ volatile ("ic iallu; dsb ish; isb" ::: "memory");

    // page tables: the kernel's map, but 0x400000 -> our block, heap empty
    p->l1 = l1_tables[p->slot];
    p->l2 = l2_tables[p->slot];
    memcpy(p->l1, level_1_table, sizeof l1_tables[0]);
    memcpy(p->l2, level_2_0x0_0000_0000_to_0x0_4000_0000, sizeof l2_tables[0]);
    p->l1[0] = (level_1_table[0] & 0xFFF) | (uint64_t)p->l2;
    p->l2[USER_BASE / BLOCK] = ((mem_attrs | DESC_AP_EL0_RW | DESC_PXN) & ~DESC_UXN) |
                               (uint64_t)base;
    for (uint64_t a = USER_HEAP; a < USER_HEAP_END; a += BLOCK) p->l2[a / BLOCK] = 0;
    data_clean_and_invalidate(p->l1, 4096);
    data_clean_and_invalidate(p->l2, 4096);

    // its name in ps/htop: the program, or for "python app.py" the app
    const char *s = name;
    int k = 0;
    if (strcmp(name, "python") == 0) {
        const char *e = args;
        while (*e && *e != ' ') e++;
        if (e - args > 3 && memcmp(e - 3, ".py", 3) == 0) {
            for (const char *q = args; q < e - 3 && k < 15; q++) p->name[k++] = *q;
            s = "";
        }
    }
    while (*s && k < 15) p->name[k++] = *s++;
    p->name[k] = 0;
    p->pid = next_pid++;
    p->entry = entry;
    p->background = background;
    p->tty = cur->tty;
    p->kernel = 0;
    p->started = ticks;
    // a new kernel thread that starts in proc_trampoline -> user_start
    memset(p->ctx, 0, sizeof p->ctx);
    p->ctx[0] = (uint64_t)user_start;                       // x19
    p->ctx[11] = (uint64_t)proc_trampoline;                 // x30: where proc_switch "returns"
    p->ctx[12] = (uint64_t)(kstacks[p->slot] + KSTACK_SIZE);  // sp
    p->ctx[13] = 0x3C0;                                     // interrupts masked until started
    if (!background) tty_fg[p->tty] = p->pid;
    p->state = P_READY;
    return p->pid;
}

int proc_spawn_kernel(const char *name, void (*fn)(void), int tty) {
    proc_t *p = 0;
    for (int i = 1; i < NPROC && !p; i++) {
        if (procs[i].state == P_FREE) {
            p = &procs[i];
            memset(p, 0, sizeof *p);
            p->slot = i;
        }
    }
    if (!p) return -1;
    p->code_block = -1;
    for (int i = 0; i < 16; i++) p->heap[i] = -1;
    int k = 0;
    while (name[k] && k < 15) {
        p->name[k] = name[k];
        k++;
    }
    p->pid = next_pid++;
    p->kernel = 1;
    p->tty = tty;
    p->started = ticks;
    p->ctx[0] = (uint64_t)fn;                               // x19: proc_trampoline calls it
    p->ctx[11] = (uint64_t)proc_trampoline;
    p->ctx[12] = (uint64_t)(kstacks[p->slot] + KSTACK_SIZE);
    p->ctx[13] = 0x3C0;
    p->state = P_READY;
    return p->pid;
}

// give the current process heap block i (0-15); 0 if there's no memory left
static int map_heap(int i) {
    if (cur->slot == 0 || i < 0 || i >= 16) return 0;
    if (cur->heap[i] >= 0) return 1;
    int b = block_alloc();
    if (b < 0) return 0;
    cur->heap[i] = (int16_t)b;
    cur->l2[(USER_HEAP + (uint64_t)i * BLOCK) / BLOCK] =
        mem_attrs | DESC_AP_EL0_RW | DESC_PXN | DESC_UXN | block_addr(b);
    data_clean_and_invalidate(&cur->l2[(USER_HEAP + (uint64_t)i * BLOCK) / BLOCK], 8);
    flush_tlb();
    return 1;
}

int proc_handle_fault(uint64_t addr, uint64_t esr) {
    uint32_t fsc = (uint32_t)(esr & 0x3F);
    if (fsc < 0x04 || fsc > 0x07) return 0;             // only "nothing mapped here"
    if (addr < USER_HEAP || addr >= USER_HEAP_END) return 0;
    return map_heap((int)((addr - USER_HEAP) / BLOCK));
}

int proc_user_ensure(uint64_t addr, uint64_t len) {
    if (addr >= USER_HEAP && addr < USER_HEAP_END && len) {
        uint64_t end = addr + len - 1;
        for (uint64_t a = addr & ~(BLOCK - 1); a <= end && a < USER_HEAP_END; a += BLOCK)
            if (!map_heap((int)((a - USER_HEAP) / BLOCK))) return 0;
    }
    return 1;
}
