; ============================================================
;  MyOS v0.2 - 64-bit kernel
;
;  Boot flow:  16-bit real mode  ->  32-bit protected mode
;                                ->  64-bit long mode
;
;  In 64-bit mode the BIOS is gone, so this kernel has its own
;  drivers: VGA text screen, PS/2 keyboard, CMOS real-time clock.
; ============================================================
[org 0x7E00]
default abs                     ; use absolute addresses in 64-bit code

KERNEL_SECTORS equ 16
MAX_INPUT      equ 64

PML4_ADDR      equ 0x1000       ; page tables (3 x 4 KB)
PDPT_ADDR      equ 0x2000
PD_ADDR        equ 0x3000
MEMMAP_ADDR    equ 0x5000       ; BIOS E820 memory map goes here
MEMMAP_MAX     equ 32
STACK_TOP      equ 0x90000

VGA            equ 0xB8000      ; text-mode video memory
COLS           equ 80
ROWS           equ 25

CODE32_SEL     equ 0x08         ; GDT selectors
DATA_SEL       equ 0x10
CODE64_SEL     equ 0x18

; ============================================================
;  STAGE 1: 16-bit real mode (BIOS still available)
; ============================================================
[bits 16]
entry16:
    cld
    mov [boot_drive], dl
    mov ax, 0x0003              ; 80x25 color text mode
    int 0x10

    ; can this CPU run 64-bit code?
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb no_long_mode
    mov eax, 0x80000001
    cpuid
    test edx, 1 << 29           ; LM bit
    jz no_long_mode

    call detect_memory          ; last chance to ask the BIOS

    in al, 0x92                 ; enable A20 line (fast A20 gate)
    or al, 2
    and al, 0xFE
    out 0x92, al

    cli
    mov al, 0xFF                ; mask every PIC interrupt;
    out 0x21, al                ; we poll the keyboard instead
    out 0xA1, al

    lgdt [gdt_ptr]
    mov eax, cr0
    or eax, 1                   ; CR0.PE = 1 -> protected mode
    mov cr0, eax
    jmp CODE32_SEL:entry32

no_long_mode:
    mov si, msg_no_lm
.print:
    lodsb
    test al, al
    jz .halt
    mov ah, 0x0E
    xor bh, bh
    int 0x10
    jmp .print
.halt:
    cli
    hlt
    jmp .halt

; BIOS INT 15h / E820: list of RAM regions -> MEMMAP_ADDR
detect_memory:
    mov di, MEMMAP_ADDR
    xor ebx, ebx
    xor bp, bp                  ; entry count
.next:
    mov eax, 0xE820
    mov edx, 0x534D4150         ; 'SMAP'
    mov ecx, 24
    mov dword [es:di + 20], 1
    int 0x15
    jc .done
    cmp eax, 0x534D4150
    jne .done
    jcxz .skip
    inc bp
    add di, 24
.skip:
    test ebx, ebx
    jz .done
    cmp bp, MEMMAP_MAX
    jb .next
.done:
    mov [mem_count], bp
    ret

; ============================================================
;  STAGE 2: 32-bit protected mode -> set up paging -> long mode
; ============================================================
[bits 32]
entry32:
    mov ax, DATA_SEL
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, STACK_TOP

    ; identity-map the first 1 GB using 2 MB pages
    mov edi, PML4_ADDR          ; zero the three tables
    xor eax, eax
    mov ecx, 3 * 4096 / 4
    rep stosd
    mov dword [PML4_ADDR], PDPT_ADDR | 3    ; present + writable
    mov dword [PDPT_ADDR], PD_ADDR | 3
    mov edi, PD_ADDR
    mov eax, 0x83               ; present + writable + huge (2 MB)
    mov ecx, 512
.map:
    mov [edi], eax
    add eax, 0x200000
    add edi, 8
    loop .map

    mov eax, PML4_ADDR
    mov cr3, eax                ; tell the CPU where the tables are
    mov eax, cr4
    or eax, 1 << 5              ; CR4.PAE
    mov cr4, eax
    mov ecx, 0xC0000080         ; EFER MSR
    rdmsr
    or eax, 1 << 8              ; EFER.LME (long mode enable)
    wrmsr
    mov eax, cr0
    or eax, 0x80000000          ; CR0.PG -> paging on = long mode active
    mov cr0, eax
    jmp CODE64_SEL:entry64

; ============================================================
;  STAGE 3: 64-bit long mode
; ============================================================
[bits 64]
entry64:
    mov ax, DATA_SEL
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov rsp, STACK_TOP

    call clear_screen
    mov rsi, banner
    call print_str

main_loop:
    mov rsi, prompt
    call print_str
    mov rdi, input_buf
    call read_line

    mov rsi, input_buf
    call skip_spaces
    cmp byte [rsi], 0
    je main_loop
    call lowercase_word

    mov rbx, cmd_table
.next:
    mov rdi, [rbx]
    test rdi, rdi
    jz .unknown
    call match_cmd              ; CF=0 -> match, RSI -> arguments
    jnc .found
    add rbx, 16
    jmp .next
.found:
    call [rbx + 8]
    jmp main_loop
.unknown:
    mov rsi, msg_unknown
    call print_str
    jmp main_loop

; ------------------------------------------------------------
;  Commands (RSI = argument text)
; ------------------------------------------------------------
cmd_help:
    mov rsi, msg_help
    jmp print_str

cmd_clear:
    jmp clear_screen

cmd_echo:
    call print_str
    jmp newline

cmd_about:
    mov rsi, msg_about
    jmp print_str

cmd_time:
    mov rsi, msg_date
    call print_str
    mov al, '2'
    call putc
    mov al, '0'
    call putc
    mov al, 0x09                ; year
    call rtc_read
    call print_bcd
    mov al, '-'
    call putc
    mov al, 0x08                ; month
    call rtc_read
    call print_bcd
    mov al, '-'
    call putc
    mov al, 0x07                ; day
    call rtc_read
    call print_bcd
    call newline

    mov rsi, msg_time
    call print_str
    mov al, 0x04                ; hour
    call rtc_read
    call print_bcd
    mov al, ':'
    call putc
    mov al, 0x02                ; minute
    call rtc_read
    call print_bcd
    mov al, ':'
    call putc
    mov al, 0x00                ; second
    call rtc_read
    call print_bcd
    jmp newline

cmd_mem:
    mov rsi, msg_mem_head
    call print_str
    movzx rcx, word [mem_count]
    mov rbx, MEMMAP_ADDR
    xor r8, r8                  ; total usable bytes
.entry:
    test rcx, rcx
    jz .total
    mov rsi, msg_indent
    call print_str
    mov rax, [rbx]              ; base address
    call print_hex64
    mov rsi, msg_gap
    call print_str
    mov rax, [rbx + 8]          ; length
    call print_hex64
    mov rsi, msg_gap
    call print_str
    mov eax, [rbx + 16]         ; type
    mov rsi, msg_reserved
    cmp eax, 3
    jne .not_acpi
    mov rsi, msg_acpi
.not_acpi:
    cmp eax, 1
    jne .print_type
    mov rsi, msg_usable
    add r8, [rbx + 8]
.print_type:
    call print_str
    add rbx, 24
    dec rcx
    jmp .entry
.total:
    mov rsi, msg_mem_total
    call print_str
    mov rax, r8
    shr rax, 20                 ; bytes -> MB
    call print_dec
    mov rsi, msg_mb
    jmp print_str

cmd_cpu:
    xor eax, eax                ; CPUID 0: vendor string
    cpuid
    mov [cpu_buf], ebx
    mov [cpu_buf + 4], edx
    mov [cpu_buf + 8], ecx
    mov byte [cpu_buf + 12], 0
    mov rsi, msg_vendor
    call print_str
    mov rsi, cpu_buf
    call print_str
    call newline

    mov eax, 0x80000000         ; brand string available?
    cpuid
    cmp eax, 0x80000004
    jb .mode
    mov rdi, cpu_buf
    mov r8d, 0x80000002
.brand:
    mov eax, r8d
    cpuid
    mov [rdi], eax
    mov [rdi + 4], ebx
    mov [rdi + 8], ecx
    mov [rdi + 12], edx
    add rdi, 16
    inc r8d
    cmp r8d, 0x80000004
    jbe .brand
    mov byte [cpu_buf + 48], 0
    mov rsi, msg_model
    call print_str
    mov rsi, cpu_buf
    call skip_spaces
    call print_str
    call newline
.mode:
    mov ecx, 0xC0000080         ; read EFER: LMA bit = long mode active
    rdmsr
    mov rsi, msg_mode_lm
    test eax, 1 << 10
    jnz .print_mode
    mov rsi, msg_mode_other
.print_mode:
    jmp print_str

cmd_color:
    movzx rbx, byte [theme_index]
    inc rbx
    cmp rbx, THEME_COUNT
    jb .set
    xor rbx, rbx
.set:
    mov [theme_index], bl
    mov al, [themes + rbx]
    mov [text_attr], al
    call clear_screen
    mov rsi, msg_color
    jmp print_str

cmd_reboot:
    mov rsi, msg_reboot
    call print_str
.wait:
    in al, 0x64                 ; wait until keyboard controller is ready
    test al, 2
    jnz .wait
    mov al, 0xFE                ; pulse the CPU reset line
    out 0x64, al
    lidt [null_idt]             ; fallback: force a triple fault
    int3

cmd_shutdown:
    mov rsi, msg_shutdown
    call print_str
    mov dx, 0x604               ; QEMU
    mov ax, 0x2000
    out dx, ax
    mov dx, 0xB004              ; Bochs / older QEMU
    out dx, ax
    mov dx, 0x4004              ; VirtualBox
    mov ax, 0x3400
    out dx, ax
    mov rsi, msg_safe_off
    call print_str
.halt:
    cli
    hlt
    jmp .halt

; ------------------------------------------------------------
;  Shell helpers
; ------------------------------------------------------------

; read a line into RDI (zero-terminated)
read_line:
    xor rcx, rcx
.key:
    call getch
    cmp al, 10
    je .done
    cmp al, 8
    je .back
    cmp al, ' '
    jb .key
    cmp al, '~'
    ja .key
    cmp rcx, MAX_INPUT
    jae .key
    stosb
    inc rcx
    call putc
    jmp .key
.back:
    test rcx, rcx
    jz .key
    dec rdi
    dec rcx
    call putc                   ; AL = 8 -> erase on screen
    jmp .key
.done:
    mov byte [rdi], 0
    jmp newline

skip_spaces:
    cmp byte [rsi], ' '
    jne .done
    inc rsi
    jmp skip_spaces
.done:
    ret

lowercase_word:
    push rdi
    mov rdi, rsi
.loop:
    mov al, [rdi]
    test al, al
    jz .done
    cmp al, ' '
    je .done
    cmp al, 'A'
    jb .skip
    cmp al, 'Z'
    ja .skip
    add byte [rdi], 32
.skip:
    inc rdi
    jmp .loop
.done:
    pop rdi
    ret

; compare word at RSI with name at RDI
match_cmd:
    push rsi
.loop:
    mov al, [rdi]
    test al, al
    jz .end_name
    cmp al, [rsi]
    jne .fail
    inc rsi
    inc rdi
    jmp .loop
.end_name:
    mov al, [rsi]
    test al, al
    jz .ok
    cmp al, ' '
    je .ok
.fail:
    pop rsi
    stc
    ret
.ok:
    add rsp, 8
    call skip_spaces
    clc
    ret

; ------------------------------------------------------------
;  Driver: PS/2 keyboard (polling, scancode set 1)
; ------------------------------------------------------------

; wait for a key, return its ASCII code in AL
getch:
    push rbx
.wait:
    in al, 0x64                 ; status: bit0 = data ready
    test al, 1
    jz .wait
    mov bl, al
    in al, 0x60                 ; read scancode
    test bl, 0x20               ; came from the mouse? ignore
    jnz .wait
    cmp al, 0xE0                ; extended key prefix
    jne .not_ext
    mov byte [kb_ext], 1
    jmp .wait
.not_ext:
    cmp byte [kb_ext], 0
    je .normal
    mov byte [kb_ext], 0        ; ignore arrows, etc.
    jmp .wait
.normal:
    cmp al, 0x2A                ; left shift down
    je .shift_down
    cmp al, 0x36                ; right shift down
    je .shift_down
    cmp al, 0xAA                ; left shift up
    je .shift_up
    cmp al, 0xB6                ; right shift up
    je .shift_up
    test al, 0x80               ; other key released
    jnz .wait
    cmp al, KEYMAP_SIZE
    jae .wait
    movzx rbx, al
    cmp byte [kb_shift], 0
    jne .shifted
    mov al, [keymap + rbx]
    jmp .check
.shifted:
    mov al, [keymap_shift + rbx]
.check:
    test al, al
    jz .wait
    pop rbx
    ret
.shift_down:
    mov byte [kb_shift], 1
    jmp .wait
.shift_up:
    mov byte [kb_shift], 0
    jmp .wait

; ------------------------------------------------------------
;  Driver: CMOS real-time clock
; ------------------------------------------------------------

; AL = CMOS register -> AL = value as BCD
rtc_read:
    push rbx
    mov bl, al
.busy:
    mov al, 0x0A                ; wait while an update is in progress
    out 0x70, al
    in al, 0x71
    test al, 0x80
    jnz .busy
    mov al, 0x0B                ; status B: bit2 = binary mode
    out 0x70, al
    in al, 0x71
    mov bh, al
    mov al, bl
    out 0x70, al
    in al, 0x71
    test bh, 4
    jz .done
    movzx eax, al               ; binary -> BCD
    mov bl, 10
    div bl
    shl al, 4
    or al, ah
.done:
    pop rbx
    ret

; ------------------------------------------------------------
;  Driver: VGA text screen (writes directly to 0xB8000)
; ------------------------------------------------------------

; print character in AL (handles 10 = newline, 8 = backspace)
putc:
    push rax
    push rbx
    push rcx
    cmp al, 10
    je .newline
    cmp al, 8
    je .backspace
    call cursor_offset
    mov ah, [text_attr]
    mov [VGA + rbx * 2], ax
    inc byte [cur_x]
    cmp byte [cur_x], COLS
    jb .done
.newline:
    mov byte [cur_x], 0
    inc byte [cur_y]
    cmp byte [cur_y], ROWS
    jb .done
    call scroll
    mov byte [cur_y], ROWS - 1
    jmp .done
.backspace:
    cmp byte [cur_x], 0
    je .done
    dec byte [cur_x]
    call cursor_offset
    mov al, ' '
    mov ah, [text_attr]
    mov [VGA + rbx * 2], ax
.done:
    call update_cursor
    pop rcx
    pop rbx
    pop rax
    ret

; RBX = cur_y * COLS + cur_x   (clobbers RCX)
cursor_offset:
    movzx rbx, byte [cur_y]
    imul rbx, rbx, COLS
    movzx rcx, byte [cur_x]
    add rbx, rcx
    ret

; move the blinking hardware cursor
update_cursor:
    push rax
    push rbx
    push rcx
    push rdx
    call cursor_offset
    mov dx, 0x3D4
    mov al, 0x0F                ; cursor position low byte
    out dx, al
    inc dx
    mov al, bl
    out dx, al
    dec dx
    mov al, 0x0E                ; cursor position high byte
    out dx, al
    inc dx
    mov al, bh
    out dx, al
    pop rdx
    pop rcx
    pop rbx
    pop rax
    ret

; move every row up by one, blank the last row
scroll:
    push rax
    push rcx
    push rsi
    push rdi
    mov rdi, VGA
    mov rsi, VGA + COLS * 2
    mov rcx, COLS * (ROWS - 1)
    rep movsw
    mov al, ' '
    mov ah, [text_attr]
    mov rcx, COLS
    rep stosw
    pop rdi
    pop rsi
    pop rcx
    pop rax
    ret

clear_screen:
    push rax
    push rcx
    push rdi
    mov rdi, VGA
    mov al, ' '
    mov ah, [text_attr]
    mov rcx, COLS * ROWS
    rep stosw
    mov byte [cur_x], 0
    mov byte [cur_y], 0
    call update_cursor
    pop rdi
    pop rcx
    pop rax
    ret

print_str:
    push rax
    push rsi
.loop:
    lodsb
    test al, al
    jz .done
    call putc
    jmp .loop
.done:
    pop rsi
    pop rax
    ret

newline:
    push rax
    mov al, 10
    call putc
    pop rax
    ret

; print RAX as unsigned decimal
print_dec:
    push rax
    push rbx
    push rcx
    push rdx
    mov rbx, 10
    xor rcx, rcx
.div:
    xor rdx, rdx
    div rbx
    push rdx
    inc rcx
    test rax, rax
    jnz .div
.out:
    pop rax
    add al, '0'
    call putc
    loop .out
    pop rdx
    pop rcx
    pop rbx
    pop rax
    ret

; print RAX as 0x + 16 hex digits
print_hex64:
    push rax
    push rbx
    push rcx
    mov rbx, rax
    mov al, '0'
    call putc
    mov al, 'x'
    call putc
    mov rcx, 16
.digit:
    rol rbx, 4
    mov al, bl
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .print
    add al, 7
.print:
    call putc
    loop .digit
    pop rcx
    pop rbx
    pop rax
    ret

; print AL (BCD) as two digits
print_bcd:
    push rax
    push rax
    shr al, 4
    add al, '0'
    call putc
    pop rax
    and al, 0x0F
    add al, '0'
    call putc
    pop rax
    ret

; ============================================================
;  Data
; ============================================================
align 8
gdt:
    dq 0                        ; null descriptor
    dq 0x00CF9A000000FFFF       ; 0x08: 32-bit code
    dq 0x00CF92000000FFFF       ; 0x10: data
    dq 0x00AF9A000000FFFF       ; 0x18: 64-bit code (L bit set)
gdt_end:
gdt_ptr:
    dw gdt_end - gdt - 1
    dd gdt
null_idt:
    dw 0
    dq 0

align 8
cmd_table:
    dq s_help,     cmd_help
    dq s_clear,    cmd_clear
    dq s_echo,     cmd_echo
    dq s_time,     cmd_time
    dq s_mem,      cmd_mem
    dq s_cpu,      cmd_cpu
    dq s_color,    cmd_color
    dq s_about,    cmd_about
    dq s_reboot,   cmd_reboot
    dq s_shutdown, cmd_shutdown
    dq 0

s_help     db "help", 0
s_clear    db "clear", 0
s_echo     db "echo", 0
s_time     db "time", 0
s_mem      db "mem", 0
s_cpu      db "cpu", 0
s_color    db "color", 0
s_about    db "about", 0
s_reboot   db "reboot", 0
s_shutdown db "shutdown", 0

; scancode set 1 -> ASCII
keymap:
    db 0, 27, "1234567890-=", 8, 9
    db "qwertyuiop[]", 10, 0
    db "asdfghjkl;'`", 0
    db "\zxcvbnm,./", 0, "*", 0, " "
KEYMAP_SIZE equ $ - keymap
keymap_shift:
    db 0, 27, "!@#$%^&*()_+", 8, 9
    db "QWERTYUIOP{}", 10, 0
    db "ASDFGHJKL:", 0x22, "~", 0
    db "|ZXCVBNM<>?", 0, "*", 0, " "

banner:
    db "  __  __        ___  ____  ", 10
    db " |  \/  |_   _ / _ \/ ___| ", 10
    db " | |\/| | | | | | | \___ \ ", 10
    db " | |  | | |_| | |_| |___) |", 10
    db " |_|  |_|\__, |\___/|____/ ", 10
    db "         |___/   v0.2  (64-bit)", 10, 10
    db "Type 'help' to see commands.", 10, 10, 0

prompt      db "myos> ", 0
msg_unknown db "Unknown command. Type 'help'.", 10, 0
msg_help:
    db "Commands:", 10
    db "  help      show this list", 10
    db "  clear     clear the screen", 10
    db "  echo X    print X", 10
    db "  time      show date and time", 10
    db "  mem       show the memory map", 10
    db "  cpu       show CPU info and mode", 10
    db "  color     change color theme", 10
    db "  about     about this OS", 10
    db "  reboot    restart the computer", 10
    db "  shutdown  power off", 10, 0
msg_about:
    db "MyOS v0.2 - a tiny 64-bit operating system", 10
    db "Written in x86-64 assembly. Own drivers for screen,", 10
    db "keyboard and clock - no BIOS after boot.", 10, 0
msg_date       db "Date: ", 0
msg_time       db "Time: ", 0
msg_mem_head   db "Memory map (from BIOS E820):", 10
               db "  Base                Length              Type", 10, 0
msg_indent     db "  ", 0
msg_gap        db "  ", 0
msg_usable     db "usable", 10, 0
msg_acpi       db "ACPI", 10, 0
msg_reserved   db "reserved", 10, 0
msg_mem_total  db "Usable RAM: ", 0
msg_mb         db " MB", 10, 0
msg_vendor     db "Vendor: ", 0
msg_model      db "Model:  ", 0
msg_mode_lm    db "Mode:   64-bit long mode (EFER.LMA = 1)", 10, 0
msg_mode_other db "Mode:   not in long mode?!", 10, 0
msg_color      db "Theme changed. Type 'color' again for the next one.", 10, 0
msg_reboot     db "Rebooting...", 10, 0
msg_shutdown   db "Shutting down...", 10, 0
msg_safe_off   db "It is now safe to turn off your computer.", 10, 0
msg_no_lm      db "This CPU does not support 64-bit mode.", 13, 10, 0

themes      db 0x07, 0x1F, 0x0A, 0x2F, 0x4F, 0x70
THEME_COUNT equ $ - themes
theme_index db 0
text_attr   db 0x07
cur_x       db 0
cur_y       db 0
kb_shift    db 0
kb_ext      db 0
boot_drive  db 0
mem_count   dw 0
cpu_buf     times 49 db 0
input_buf   times MAX_INPUT + 1 db 0

times KERNEL_SECTORS * 512 - ($ - $$) db 0
