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
#include "timer.h"
#include "user.h"

extern uint64_t enter_user(uint64_t entry, uint64_t sp, uint64_t arg, uint64_t *ctx);
extern void return_to_kernel(uint64_t *ctx, uint64_t code) __attribute__((noreturn));
extern uint64_t level_2_0x0_0000_0000_to_0x0_4000_0000[512];    // broadcom/mmu.c

static uint64_t kernel_ctx[13];
static int running;

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t rd64(const uint8_t *p) { return rd32(p) | (uint64_t)rd32(p + 4) << 32; }

// ---------------------------------------------------------------
//  memory: let EL0 use the 2 MB block at USER_BASE, nothing else
// ---------------------------------------------------------------
#define DESC_AP_EL0_RW  (1ull << 6)     // EL1 and EL0 may read and write
#define DESC_PXN        (1ull << 53)    // the kernel may not execute it
#define DESC_UXN        (1ull << 54)    // EL0 may not execute it

void user_init(void) {
    uint64_t *e = &level_2_0x0_0000_0000_to_0x0_4000_0000[USER_BASE >> 21];
    *e = (*e | DESC_AP_EL0_RW | DESC_PXN) & ~DESC_UXN;
    __asm__ volatile ("dsb ishst; tlbi vmalle1; dsb ish; isb" ::: "memory");
    // let programs use floating point / SIMD
    __asm__ volatile ("msr cpacr_el1, %0; isb" :: "r"(3ull << 20));
}

static int user_range(uint64_t addr, uint64_t len) {
    return addr >= USER_BASE && len <= USER_END - USER_BASE && addr + len <= USER_END;
}

// copy a file name from program memory; 0 if it's not a sensible name
static int user_name(uint64_t addr, char *out) {
    for (int i = 0; i <= MYFS_NAME_MAX; i++) {
        if (!user_range(addr + i, 1)) return 0;
        char c = *(const char *)(addr + i);
        out[i] = c;
        if (c == 0) return i > 0;
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
int run_program(const char *name, const char *args) {
    int32_t size = myfs_size(name);
    if (size < 0) return -1;
    if (size < 16 || (uint32_t)size > USER_MAX_SIZE) {
        console_puts(name);
        console_puts(" is not a program.\n");
        return 0;
    }
    uint8_t *base = (uint8_t *)USER_BASE;
    memset(base, 0, USER_END - USER_BASE);          // wipe the last program
    if (myfs_read(name, 0, base, (uint32_t)size) != size) {
        console_puts("Disk error!\n");
        return 0;
    }
    int arch = program_header_arch(base, (uint32_t)size);
    if (arch != MYEX_ARCH_ARM64) {
        console_puts(name);
        console_puts(arch == MYEX_ARCH_X86 ? " is a program for MyOS PC (x86), not for the Pi.\n"
                                           : " is not a program.\n");
        return 0;
    }
    uint64_t entry = rd64(base + 8);
    if (entry < USER_BASE + 16 || entry >= USER_BASE + (uint64_t)size) {
        console_puts(name);
        console_puts(" is not a program.\n");
        return 0;
    }
    size_t n = strlen(args);
    if (n > 250) n = 250;
    memcpy((void *)USER_ARGS, args, n);             // the stack starts below this

    // the code was written as data: push it out of the data cache and
    // make sure the CPU doesn't run stale instructions
    data_clean_and_invalidate((void *)USER_BASE, USER_END - USER_BASE);
    __asm__ volatile ("ic iallu; dsb ish; isb" ::: "memory");

    running = 1;
    enter_user(entry, USER_ARGS, USER_ARGS, kernel_ctx);
    running = 0;
    if (!console_at_line_start()) console_putc('\n');
    return 0;
}

// ---------------------------------------------------------------
//  system calls
// ---------------------------------------------------------------
enum {
    SYS_EXIT, SYS_PRINT, SYS_PUTC, SYS_GETKEY, SYS_READLINE, SYS_PRINTNUM,
    SYS_TICKS, SYS_SLEEP, SYS_FILESIZE, SYS_READFILE, SYS_WRITEFILE, SYS_DELETE,
    SYS_LISTFILES, SYS_KEYNOW
};

static int getkey(void) {
    for (;;) {
        int c = console_getc_nowait();
        if (c < 0) continue;
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
    char name[MYFS_NAME_MAX + 1];
    switch (n) {
        case SYS_EXIT:
            running = 0;
            if (a != 0) {
                if (!console_at_line_start()) console_putc('\n');
                console_puts("Program exited with code ");
                print_dec(a);
                console_putc('\n');
            }
            __asm__ volatile ("msr daifset, #2");
            return_to_kernel(kernel_ctx, a);
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
            while (ticks < until) console_poll();
            return 0;
        }
        case SYS_FILESIZE:
            if (!user_name(a, name)) return -1;
            return myfs_size(name);
        case SYS_READFILE:
            if (!user_name(a, name) || !user_range(b, c)) return -1;
            return myfs_read(name, 0, (void *)b, (uint32_t)c);
        case SYS_WRITEFILE:
            if (!user_name(a, name) || !user_range(b, c)) return -1;
            return myfs_write(name, (const void *)b, (uint32_t)c) ? -1 : 0;
        case SYS_DELETE:
            if (!user_name(a, name)) return -1;
            return myfs_remove(name) ? -1 : 0;
        case SYS_LISTFILES: {
            uint32_t size;
            if (!user_range(b, MYFS_NAME_MAX + 1) || myfs_entry((int)a, name, &size)) return -1;
            memcpy((void *)b, name, strlen(name) + 1);
            return size;
        }
        case SYS_KEYNOW: {                          // a key if one is waiting, else -1
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
    running = 0;
    return_to_kernel(kernel_ctx, (uint64_t)-1);
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
