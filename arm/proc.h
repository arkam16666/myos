// proc.h - processes: several programs running at the same time
#ifndef PROC_H
#define PROC_H
#include <stdint.h>
#include "console.h"

#define NPROC 16                // slot 0 is the shell of terminal 0

enum { P_FREE, P_READY, P_ZOMBIE };
#define EXIT_KILLED  (-2)       // stopped with kill or Ctrl+C
#define EXIT_CRASHED (-1)

typedef struct {
    int state, pid, slot;
    int tty;                    // its terminal (console.h)
    int kernel;                 // a kernel thread (a shell), not a program
    char name[16];
    uint64_t ctx[14];           // x19-x30, sp, daif (vectors.S proc_switch)
    uint64_t fp[66] __attribute__((aligned(16)));   // q0-q31, fpcr, fpsr (fp_save)
    uint64_t *l1, *l2;          // its page tables
    int code_block;             // its 2 MB at 0x400000 (pool block number)
    int16_t heap[16];           // its heap blocks at 0x02000000.. (-1 = not used yet)
    volatile int killed;
    int background;
    int64_t exit_code;
    uint64_t entry;
    uint64_t started;           // ticks
} proc_t;

void proc_init(void);
// load program NAME from MyFS into a new process; returns its pid, or
//  -1 no such file, -2 not a program (message printed), -3 no free slot/memory
int proc_spawn(const char *name, const char *args, int background);
void proc_yield(void);                  // let the others run a little
proc_t *proc_current(void);
proc_t *proc_by_pid(int pid);
proc_t *proc_slot(int i);
int proc_foreground(void);              // pid that gets this terminal's keys (0 = its shell)
void proc_set_foreground(int pid);
int proc_spawn_kernel(const char *name, void (*fn)(void), int tty);   // a shell thread
int proc_interrupt(int tty);            // Ctrl+C: 1 if a program was stopped
void proc_tty_open(int tty);
void proc_tty_close(int tty);           // stop everything on that terminal
int proc_kill(int pid);                 // 0 = done
void proc_exit(int64_t code) __attribute__((noreturn));
int proc_wait(int pid, int64_t *code);  // shell: wait for a foreground job; 1 = it ended
void proc_reap(proc_t *p);              // forget an ended process
int proc_handle_fault(uint64_t addr, uint64_t esr);    // heap page fault -> 1 if fixed
int proc_user_ensure(uint64_t addr, uint64_t len);     // map heap the kernel will touch
uint32_t proc_free_blocks(void);        // 2 MB blocks left in the pool
uint32_t proc_used_blocks(const proc_t *p);

#endif
