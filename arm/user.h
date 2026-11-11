// user.h - programs in user mode (EL0), system calls, crash reports
#ifndef USER_H
#define USER_H
#include <stdint.h>

#define USER_BASE     0x400000UL    // programs load here (same as MyOS PC)
#define USER_END      0x600000UL    // the one 2 MB block EL0 may touch
#define USER_ARGS     0x5FFF00UL    // command line; the user stack starts below it
#define USER_MAX_SIZE 0x100000UL    // largest program file
#define USER_HEAP     0x02000000UL  // extra memory for programs (malloc): 32 MB,
#define USER_HEAP_END 0x04000000UL  //   read/write but never executable
#define MYEX_ARCH_X86   1           // program header: "MYEX", u32 arch, u64 entry
#define MYEX_ARCH_ARM64 2

// registers saved by vectors.S on every exception
typedef struct {
    uint64_t x[31];
    uint64_t elr;       // where the exception happened
    uint64_t spsr;      // saved PSTATE
    uint64_t sp_el0;    // the program's stack pointer
    uint64_t pad[2];
} trapframe_t;

void user_init(void);                               // open the program area to EL0
int run_program(const char *name, const char *args); // -1 = no such file
int program_header_arch(const uint8_t *data, uint32_t size);   // MYEX_ARCH_* or 0

void kernel_fault(trapframe_t *f) __attribute__((noreturn));
void user_sync(trapframe_t *f);

#endif
