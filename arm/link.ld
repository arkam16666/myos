/* link.ld - the Raspberry Pi firmware loads kernel8.img at 0x80000 */
ENTRY(_start)

SECTIONS
{
    . = 0x80000;
    .text   : { KEEP(*(.text.boot)) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data   : { *(.data .data.*) }
    .bss (NOLOAD) : ALIGN(16) {
        __bss_start = .;
        *(.bss .bss.*) *(COMMON)
        . = ALIGN(16);
        __bss_end = .;
    }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
__bss_size = (__bss_end - __bss_start) >> 3;    /* in 8-byte words, for boot8.s */
