/* link.ld - lay a C program out as a MyOS program file:
 *   0x400000: "MYEX", u32 architecture, u64 entry point, then code and data
 * Architecture: 1 = x86-64 (MyOS PC), 2 = ARM64 (MyOS Pi; link with
 * --defsym=MYOS_ARCH=2).
 */
ENTRY(_start)

SECTIONS
{
    . = 0x400000;
    .header : {
        LONG(0x5845594D)        /* "MYEX" */
        LONG(DEFINED(MYOS_ARCH) ? MYOS_ARCH : 1)
        QUAD(_start)
    }
    .text   : { *(.text.start) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data   : { *(.data .data.*) }
    .bss    : {
        __bss_start = .;
        *(.bss .bss.*) *(COMMON)
        __bss_end = .;
    }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
