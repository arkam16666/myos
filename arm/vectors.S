// vectors.S - MyOS-ARM exception vectors and the switch into user mode.
//
// Every exception saves all registers in a "trap frame" on the kernel
// stack (layout = trapframe_t in user.h) and calls C:
//   kernel crash  -> kernel_fault(frame)     (never returns)
//   interrupt     -> handle_irq()
//   from EL0      -> user_sync(frame)        (system call or program crash)

.equ FRAME_SIZE, 288

.macro SAVE_FRAME
    sub  sp, sp, #FRAME_SIZE
    stp  x0,  x1,  [sp, #16 * 0]
    stp  x2,  x3,  [sp, #16 * 1]
    stp  x4,  x5,  [sp, #16 * 2]
    stp  x6,  x7,  [sp, #16 * 3]
    stp  x8,  x9,  [sp, #16 * 4]
    stp  x10, x11, [sp, #16 * 5]
    stp  x12, x13, [sp, #16 * 6]
    stp  x14, x15, [sp, #16 * 7]
    stp  x16, x17, [sp, #16 * 8]
    stp  x18, x19, [sp, #16 * 9]
    stp  x20, x21, [sp, #16 * 10]
    stp  x22, x23, [sp, #16 * 11]
    stp  x24, x25, [sp, #16 * 12]
    stp  x26, x27, [sp, #16 * 13]
    stp  x28, x29, [sp, #16 * 14]
    mrs  x9, elr_el1
    stp  x30, x9,  [sp, #16 * 15]      // x30, return address
    mrs  x10, spsr_el1
    mrs  x11, sp_el0
    stp  x10, x11, [sp, #16 * 16]      // saved PSTATE, user stack pointer
.endm

.macro RESTORE_FRAME
    ldp  x10, x11, [sp, #16 * 16]
    msr  spsr_el1, x10
    msr  sp_el0, x11
    ldp  x30, x9,  [sp, #16 * 15]
    msr  elr_el1, x9
    ldp  x0,  x1,  [sp, #16 * 0]
    ldp  x2,  x3,  [sp, #16 * 1]
    ldp  x4,  x5,  [sp, #16 * 2]
    ldp  x6,  x7,  [sp, #16 * 3]
    ldp  x8,  x9,  [sp, #16 * 4]
    ldp  x10, x11, [sp, #16 * 5]
    ldp  x12, x13, [sp, #16 * 6]
    ldp  x14, x15, [sp, #16 * 7]
    ldp  x16, x17, [sp, #16 * 8]
    ldp  x18, x19, [sp, #16 * 9]
    ldp  x20, x21, [sp, #16 * 10]
    ldp  x22, x23, [sp, #16 * 11]
    ldp  x24, x25, [sp, #16 * 12]
    ldp  x26, x27, [sp, #16 * 13]
    ldp  x28, x29, [sp, #16 * 14]
    add  sp, sp, #FRAME_SIZE
    eret
.endm

.macro VENTRY label
    .balign 128
    b    \label
.endm

.section .text
.balign 2048
.global myos_vectors
myos_vectors:
    VENTRY bad_exception        // current EL, SP0: sync / IRQ / FIQ / SError
    VENTRY bad_exception
    VENTRY bad_exception
    VENTRY bad_exception
    VENTRY el1_sync             // current EL (kernel): sync
    VENTRY el1_irq              //                      IRQ
    VENTRY bad_exception        //                      FIQ
    VENTRY bad_exception        //                      SError
    VENTRY el0_sync             // from EL0, 64-bit (programs): sync
    VENTRY el0_irq              //                              IRQ
    VENTRY bad_exception
    VENTRY bad_exception
    VENTRY bad_exception        // from EL0, 32-bit: not supported
    VENTRY bad_exception
    VENTRY bad_exception
    VENTRY bad_exception

el1_sync:
bad_exception:
    SAVE_FRAME
    mov  x0, sp
    bl   kernel_fault           // prints the panic screen, never returns
1:  b    1b

el1_irq:
el0_irq:
    SAVE_FRAME
    bl   handle_irq
    RESTORE_FRAME

el0_sync:
    SAVE_FRAME
    mov  x0, sp
    bl   user_sync              // system call (svc) or a crashed program
    RESTORE_FRAME

// uint64_t enter_user(uint64_t entry, uint64_t user_sp, uint64_t arg, uint64_t ctx[13])
// Runs a program in EL0. "Returns" when return_to_kernel(ctx, code) is called.
.global enter_user
enter_user:
    stp  x19, x20, [x3, #0]     // remember where the kernel was
    stp  x21, x22, [x3, #16]
    stp  x23, x24, [x3, #32]
    stp  x25, x26, [x3, #48]
    stp  x27, x28, [x3, #64]
    stp  x29, x30, [x3, #80]
    mov  x9, sp
    str  x9, [x3, #96]
    msr  daifset, #2            // no interrupts while switching
    msr  elr_el1, x0            // where the program starts
    msr  sp_el0, x1             // its stack
    msr  spsr_el1, xzr          // EL0, interrupts on
    mov  x0, x2                 // first argument: the command line
    mov  x1, xzr                // don't leak kernel values to the program
    mov  x2, xzr
    mov  x3, xzr
    mov  x4, xzr
    mov  x5, xzr
    mov  x6, xzr
    mov  x7, xzr
    mov  x8, xzr
    mov  x9, xzr
    mov  x10, xzr
    mov  x11, xzr
    mov  x12, xzr
    mov  x13, xzr
    mov  x14, xzr
    mov  x15, xzr
    mov  x16, xzr
    mov  x17, xzr
    mov  x18, xzr
    mov  x19, xzr
    mov  x20, xzr
    mov  x21, xzr
    mov  x22, xzr
    mov  x23, xzr
    mov  x24, xzr
    mov  x25, xzr
    mov  x26, xzr
    mov  x27, xzr
    mov  x28, xzr
    mov  x29, xzr
    mov  x30, xzr
    eret

// void return_to_kernel(uint64_t ctx[13], uint64_t code)
// Back to the caller of enter_user, which then returns code.
.global return_to_kernel
return_to_kernel:
    ldp  x19, x20, [x0, #0]
    ldp  x21, x22, [x0, #16]
    ldp  x23, x24, [x0, #32]
    ldp  x25, x26, [x0, #48]
    ldp  x27, x28, [x0, #64]
    ldp  x29, x30, [x0, #80]
    ldr  x9, [x0, #96]
    mov  sp, x9
    mov  x0, x1
    msr  daifclr, #2            // interrupts on again
    ret
