// user.c - programs in user mode (EL0) on MyOS-ARM.
//
// A program is a MyFS file starting with "MYEX", u32 arch (2 = ARM64),
// u64 entry point, linked to run at 0x400000 (csdk/link.ld). It runs in
// EL0 and may only touch the 2 MB block 0x400000-0x5FFFFF; everything
// else it wants, it asks the kernel for with 'svc #0':
//   x8 = service number (same numbers as MyOS PC), x0-x2 = arguments,
//   result in x0.
#include <stdint.h>
#include <string.h>
#include "broadcom/caches.h"
#include "console.h"
#include "led.h"
#include "myfs.h"
#include "print.h"
#include "proc.h"
#include "../csdk/myos_info.h"
#ifndef NO_USB
#include "broadcom/vcmailbox.h"
#include "net.h"
#endif
#include "timer.h"
#include "user.h"

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

// ---------------------------------------------------------------
//  memory: every process has its own MMU tables (proc.c)
// ---------------------------------------------------------------
void user_init(void) {
    // let programs use floating point / SIMD
    __asm__ volatile ("msr cpacr_el1, %0; isb" :: "r"(3ull << 20));
    proc_init();
}

// is [addr, addr+len) the program's own memory? (heap blocks get mapped
// now, so the kernel can touch them without a fault)
static int user_range(uint64_t addr, uint64_t len) {
    if (addr >= USER_BASE && len <= USER_END - USER_BASE && addr + len <= USER_END) return 1;
    if (addr >= USER_HEAP && len <= USER_HEAP_END - USER_HEAP && addr + len <= USER_HEAP_END)
        return proc_user_ensure(addr, len);
    return 0;
}

// copy a path from program memory and make it absolute (from the program's
// current folder); 0 if it's not a sensible path
static int user_path(uint64_t addr, char *out) {
    char raw[MYFS_PATH_MAX];
    for (int i = 0; i < MYFS_PATH_MAX; i++) {
        if (!user_range(addr + i, 1)) return 0;
        char c = *(const char *)(addr + i);
        raw[i] = c;
        if (c == 0) return i > 0 && myfs_normalize(proc_cwd(), raw, out) == 0;
        if (c <= ' ' || c > '~') return 0;
    }
    return 0;
}

int program_header_arch(const uint8_t *data, uint32_t size) {
    if (size < 16 || memcmp(data, "MYEX", 4) != 0) return 0;
    return (int)rd32(data + 4);
}

// ---------------------------------------------------------------
//  running a program
// ---------------------------------------------------------------
int run_program(const char *name, const char *args, int background) {
    int pid = proc_spawn(name, args, background);
    if (pid == -1) return -1;                       // no such file
    if (pid < 0) return 0;                          // (proc_spawn said why)
    if (background) {
        const char *base = name;                    // "/apps/x" -> "x"
        for (const char *s = name; *s; s++) if (*s == '/') base = s + 1;
        console_puts("[");
        print_dec((uint64_t)pid);
        console_puts("] ");
        console_puts(base);
        console_puts(" is running in the background ('ps' lists, 'fg ");
        print_dec((uint64_t)pid);
        console_puts("' brings it back)\n");
        return 0;
    }
    int64_t code = 0;
    if (proc_wait(pid, &code)) {
        if (!console_at_line_start()) console_putc('\n');
        if (code == EXIT_KILLED) console_puts("^C - stopped.\n");
        else if (code > 0) {
            console_puts("Program exited with code ");
            print_dec((uint64_t)code);
            console_putc('\n');
        }
    }
    return 0;
}

// ---------------------------------------------------------------
//  system calls
// ---------------------------------------------------------------
enum {
    SYS_EXIT, SYS_PRINT, SYS_PUTC, SYS_GETKEY, SYS_READLINE, SYS_PRINTNUM,
    SYS_TICKS, SYS_SLEEP, SYS_FILESIZE, SYS_READFILE, SYS_WRITEFILE, SYS_DELETE,
    SYS_LISTFILES, SYS_KEYNOW, SYS_PROCINFO, SYS_SYSINFO, SYS_KILL
};
enum {                          // MyFS v2 folders (csdk/myos.h): from 32 on
    SYS_MKDIR = 32, SYS_RMDIR, SYS_RENAME, SYS_STAT, SYS_READDIR, SYS_CHDIR, SYS_GETCWD
};

static int64_t sys_procinfo(uint64_t index, uint64_t addr) {
    if (!user_range(addr, sizeof(struct myos_procinfo))) return -1;
    proc_t *p = proc_slot((int)index);
    if (!p) return -1;                              // past the last slot
    struct myos_procinfo *out = (struct myos_procinfo *)addr;
    memset(out, 0, sizeof *out);
    if (p->state == P_FREE) return 0;               // an empty slot (state 0)
    out->pid = p->pid;
    out->tty = p->tty;
    out->state = p->state == P_READY ? 1 : 2;
    out->kind = p->kernel ? 1 : 0;
    out->foreground = !p->kernel && !p->background && p->state == P_READY;
    out->mem_kb = proc_used_blocks(p) * 2048;
    out->cpu_ticks = p->cpu_ticks;
    out->start_ticks = p->started;
    memcpy(out->name, p->name, sizeof out->name);
    return 0;
}

static int64_t sys_sysinfo(uint64_t addr) {
    if (!user_range(addr, sizeof(struct myos_sysinfo))) return -1;
    struct myos_sysinfo *out = (struct myos_sysinfo *)addr;
    memset(out, 0, sizeof *out);
    out->ticks = ticks;
    out->idle_ticks = proc_idle_ticks();
    out->mem_total_kb = proc_total_blocks() * 2048;
    out->mem_free_kb = proc_free_blocks() * 2048;
#ifndef NO_USB                                      // (the firmware mailbox: not in QEMU builds)
    out->temp_millic = (int32_t)vcmailbox_get_temperature();
    out->arm_hz = vcmailbox_get_clock_rate_measured(VCMAILBOX_CLOCK_ARM);
    out->net_in = net_packets_in();
    out->net_out = net_packets_out();
#endif
    if (myfs_mounted()) {
        out->files = (uint32_t)myfs_count();
        out->disk_free_kb = myfs_free_kb();
    }
    out->proc_slots = NPROC;
    for (int t = 0; t < NTTY; t++) out->terminals_open += console_tty_open(t);
    return 0;
}

static int64_t sys_stat(uint64_t path, uint64_t addr) {
    char p[MYFS_PATH_MAX];
    if (!user_path(path, p) || !user_range(addr, sizeof(struct myos_stat))) return MYFS_ERR_NAME;
    uint32_t size;
    int t = myfs_stat(p, &size);
    if (t < 0) return t;
    struct myos_stat *out = (struct myos_stat *)addr;
    out->type = (uint32_t)t;
    out->size = size;
    return 0;
}

static int64_t sys_readdir(uint64_t path, uint64_t index, uint64_t addr) {
    char p[MYFS_PATH_MAX];
    if (!user_path(path, p) || !user_range(addr, sizeof(struct myos_dirent))) return MYFS_ERR_NAME;
    struct myos_dirent *out = (struct myos_dirent *)addr;
    uint32_t size = 0;
    int t = myfs_readdir(p, (int)index, out->name, &size);
    if (t <= 0) return t;
    out->type = (uint32_t)t;
    out->size = size;
    return 1;
}

static int64_t sys_chdir(uint64_t path) {
    char p[MYFS_PATH_MAX];
    if (!user_path(path, p)) return MYFS_ERR_NAME;
    int t = myfs_stat(p, 0);
    if (t < 0) return t;
    if (t != MYFS_DIR) return MYFS_ERR_NOTDIR;
    strcpy(proc_current()->cwd, p);
    return 0;
}

static int64_t sys_getcwd(uint64_t addr, uint64_t size) {
    const char *cwd = proc_cwd();
    uint64_t n = strlen(cwd);
    if (n >= size || !user_range(addr, n + 1)) return -1;
    memcpy((void *)addr, cwd, n + 1);
    return (int64_t)n;
}

static int is_foreground(void) {
    return proc_current()->pid == proc_foreground();
}

static int getkey(void) {
    proc_current()->waiting = 1;
    for (;;) {
        if (!is_foreground()) {                     // only the foreground program gets keys
            console_poll();
            continue;
        }
        int c = console_getc_nowait();
        if (c < 0) continue;
        proc_current()->waiting = 0;
        if (c == '\r') return '\n';                 // Enter, as on MyOS PC
        if (c == 127) return 8;                     // Backspace
        return c;
    }
}

static int64_t sys_readline(uint64_t buf, uint64_t size) {
    if (size == 0 || !user_range(buf, size)) return -1;
    char *p = (char *)buf;
    uint64_t len = 0;
    for (;;) {
        int c = getkey();
        if (c == '\n') {
            p[len] = 0;
            console_putc('\n');
            return (int64_t)len;
        }
        if (c == 8) {
            if (len > 0) {
                len--;
                console_puts("\b \b");
            }
        } else if (c >= ' ' && c <= '~' && len < size - 1) {
            p[len++] = (char)c;
            console_putc((char)c);
            console_puts("");
        }
    }
}

static int64_t syscall(uint64_t n, uint64_t a, uint64_t b, uint64_t c) {
    char name[MYFS_PATH_MAX], other[MYFS_PATH_MAX];
    switch (n) {
        case SYS_EXIT:
            proc_exit((int64_t)a);
        case SYS_PRINT:
            for (; user_range(a, 1) && *(const char *)a; a++) console_putc(*(const char *)a);
            console_puts("");
            return 0;
        case SYS_PUTC:
            console_putc((char)a);
            if (a == '\n') console_puts("");
            return 0;
        case SYS_GETKEY:
            return getkey();
        case SYS_READLINE:
            return sys_readline(a, b);
        case SYS_PRINTNUM:
            print_dec(a);
            console_puts("");
            return 0;
        case SYS_TICKS:
            return (int64_t)ticks;
        case SYS_SLEEP: {
            uint64_t until = ticks + a;
            proc_current()->waiting = 1;            // (counted as idle, not as busy)
            while (ticks < until) console_poll();
            proc_current()->waiting = 0;
            return 0;
        }
        // file names may be paths, from the program's current folder
        case SYS_FILESIZE:
            if (!user_path(a, name)) return -1;
            return myfs_size(name);
        case SYS_READFILE:
            if (!user_path(a, name) || !user_range(b, c)) return -1;
            return myfs_read(name, 0, (void *)b, (uint32_t)c);
        case SYS_WRITEFILE:
            if (!user_path(a, name) || !user_range(b, c)) return -1;
            return myfs_write(name, (const void *)b, (uint32_t)c) ? -1 : 0;
        case SYS_DELETE:
            if (!user_path(a, name)) return -1;
            return myfs_remove(name) ? -1 : 0;
        case SYS_LISTFILES: {                       // files of the current folder (old call:
            uint32_t size;                          //  48-byte buffer, longer names are cut)
            if (!user_range(b, MYFS_V1_NAME_MAX + 1) ||
                myfs_file_entry(proc_cwd(), (int)a, name, &size)) return -1;
            name[MYFS_V1_NAME_MAX] = 0;
            memcpy((void *)b, name, strlen(name) + 1);
            return size;
        }
        case SYS_MKDIR:
            return user_path(a, name) ? myfs_mkdir(name) : MYFS_ERR_NAME;
        case SYS_RMDIR:
            return user_path(a, name) ? myfs_rmdir(name) : MYFS_ERR_NAME;
        case SYS_RENAME:
            return user_path(a, name) && user_path(b, other) ? myfs_rename(name, other) : MYFS_ERR_NAME;
        case SYS_STAT:
            return sys_stat(a, b);
        case SYS_READDIR:
            return sys_readdir(a, b, c);
        case SYS_CHDIR:
            return sys_chdir(a);
        case SYS_GETCWD:
            return sys_getcwd(a, b);
        case SYS_PROCINFO:
            return sys_procinfo(a, b);
        case SYS_SYSINFO:
            return sys_sysinfo(a);
        case SYS_KILL:
            return proc_kill((int)a);
        case SYS_KEYNOW: {                          // a key if one is waiting, else -1
            if (!is_foreground()) {
                console_poll();
                return -1;
            }
            int k = console_getc_nowait();
            if (k == '\r') return '\n';
            if (k == 127) return 8;
            return k;
        }
        default:
            return -1;
    }
}

static uint64_t read_esr(void) { uint64_t v; __asm__ volatile ("mrs %0, esr_el1" : "=r"(v)); return v; }
static uint64_t read_far(void) { uint64_t v; __asm__ volatile ("mrs %0, far_el1" : "=r"(v)); return v; }

static const char *exception_name(uint32_t ec) {
    switch (ec) {
        case 0x00: return "Undefined Instruction";
        case 0x07: return "Floating Point Not Enabled";
        case 0x0E: return "Illegal Execution State";
        case 0x15: return "System Call";
        case 0x20: case 0x21: return "Instruction Abort";
        case 0x22: return "PC Alignment Fault";
        case 0x24: case 0x25: return "Data Abort";
        case 0x26: return "Stack Alignment Fault";
        case 0x2F: return "SError";
        case 0x3C: return "Breakpoint";
        default:   return "Exception";
    }
}

static int is_abort(uint32_t ec) {
    return ec == 0x20 || ec == 0x21 || ec == 0x24 || ec == 0x25;
}

// exception from EL0: a system call, or the program did something it may not
void user_sync(trapframe_t *f) {
    uint64_t esr = read_esr();
    uint32_t ec = (uint32_t)(esr >> 26);
    if (ec == 0x15) {                               // svc #0
        __asm__ volatile ("msr daifclr, #2");       // USB and the timer keep working
        f->x[0] = (uint64_t)syscall(f->x[8], f->x[0], f->x[1], f->x[2]);
        __asm__ volatile ("msr daifset, #2");       // vectors.S restores with IRQs off
        return;
    }
    uint64_t far = read_far();
    if (ec == 0x24 && proc_handle_fault(far, esr)) return;     // first touch of a heap block
    if (!console_at_line_start()) console_putc('\n');
    console_puts("Program crashed: ");
    console_puts(exception_name(ec));
    console_puts(" at PC=0x");
    print_hex(f->elr, 16);
    if (is_abort(ec)) {
        console_puts("\n  while touching address 0x");
        print_hex(far, 16);
    }
    console_putc('\n');
    if (ec == 0x24 && far >= USER_HEAP && far < USER_HEAP_END)
        console_puts("  (out of memory: no free RAM for its heap)\n");
    proc_exit(EXIT_CRASHED);
}

// the kernel itself crashed: show what happened and stop
void kernel_fault(trapframe_t *f) {
    console_panic_mode();
    uint64_t esr = read_esr();
    uint32_t ec = (uint32_t)(esr >> 26);
    console_puts("\n\n*** KERNEL PANIC ***\n\n  ");
    console_puts(exception_name(ec));
    console_puts("  (ESR 0x");
    print_hex(esr, 8);
    console_puts(")\n  PC=0x");
    print_hex(f->elr, 16);
    if (is_abort(ec)) {
        console_puts("  address 0x");
        print_hex(read_far(), 16);
    }
    console_puts("\n\n");
    for (int i = 0; i < 31; i++) {
        console_puts(i < 10 ? "  x" : "  x");
        print_dec((uint64_t)i);
        console_puts(i < 10 ? " =0x" : "=0x");
        print_hex(f->x[i], 16);
        if (i % 3 == 2 || i == 30) console_putc('\n');
    }
    console_puts("\n  The system has been halted. Please restart the Pi.\n");
    for (;;) {                                      // keep USB alive so the text arrives
        console_service();
        led_panic_step(millis());
    }
}
