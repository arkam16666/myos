// linux.h - running Linux programs (static AArch64 ELF files) on MyOS-ARM
#ifndef LINUX_H
#define LINUX_H
#include <stdint.h>
#include "proc.h"
#include "user.h"

// Memory map of a Linux program (each process has its own MMU tables):
#define LX_LOW_START  0x00400000UL  // ELF programs are linked here (like MYEX programs),
#define LX_LOW_END    0x04000000UL  //   the brk heap follows them
#define LX_HIGH_START 0x40000000UL  // the second GB has its own level-2 table:
#define LX_HIGH_END   0x80000000UL  //   mmap() memory, and the stack at the top

int linux_is_elf(const char *name);         // is MyFS file NAME an ELF file?
// run MyFS file FILE as a Linux program: argv[0] = ARGV0, the rest split from ARGS
// (quotes work). Waits for it unless BACKGROUND. -1 = no such file.
int linux_run(const char *file, const char *argv0, const char *args, int background);
int linux_applet(const char *name);         // is NAME a BusyBox applet (busybox installed)?
int linux_command(const char *line, int background);    // shell: "strace ..." (1 = handled)

// hooks for proc.c / user.c
void linux_syscall(trapframe_t *f);
int linux_fault(uint64_t addr, uint64_t esr);   // 1 = memory mapped, carry on
void linux_exit(proc_t *p);                 // the process ends (still on its own map)
void linux_free(proc_t *p);                 // give its memory back
uint32_t linux_used_blocks(const proc_t *p);
int linux_interrupt(int tty, proc_t *fg);   // Ctrl+C: 1 = handled, 0 = pass the key on
void linux_irq_return(trapframe_t *f);      // vectors.S: after an interrupt (signal handlers)

#endif
