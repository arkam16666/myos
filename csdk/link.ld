/* link.ld - lay a C program out as a MyOS program file:
 *   0x400000: "MYEX", version, entry point, then code and data
 */
ENTRY(_start)

SECTIONS
{
    . = 0x400000;
    .header : {
        LONG(0x5845594D)        /* "MYEX" */
        LONG(1)
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
