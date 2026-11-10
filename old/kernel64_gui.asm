; ============================================================
;  MyOS v0.5 - 64-bit kernel with a graphical desktop
;
;  Boot flow:  16-bit real mode   (font from BIOS, VBE graphics mode)
;          ->  32-bit protected   (build page tables)
;          ->  64-bit long mode   (desktop, mouse, terminal)
;
;  Drivers (all written here, no BIOS after boot):
;    - linear framebuffer graphics + double buffering
;    - 8x16 bitmap font rendering
;    - PS/2 keyboard and PS/2 mouse (polling)
;    - CMOS real-time clock
; ============================================================
[org 0x7E00]
default abs

KERNEL_SECTORS equ 64
MAX_INPUT      equ 64

; ---------------- memory layout ----------------
VBE_INFO       equ 0x0500       ; VBE controller info (512 B)
VBE_MODE       equ 0x0700       ; VBE mode info (256 B)
PML4_ADDR      equ 0x1000       ; page tables
PDPT_ADDR      equ 0x2000
PD_ADDR        equ 0x3000       ; 4 page directories: 0x3000-0x6FFF
MEMMAP_ADDR    equ 0x7000       ; BIOS E820 memory map
MEMMAP_MAX     equ 32
FONT_SEG       equ 0x6000       ; 8x16 font copied to 0x60000
FONT_ADDR      equ 0x60000
IDT_ADDR       equ 0x70000      ; interrupt descriptor table (4 KB)
DISK_BUF       equ 0x71000      ; one disk sector (512 B)
IDENT_BUF      equ 0x71200      ; ATA IDENTIFY data (512 B)
NOTE_SECTOR    equ 1            ; where 'save' keeps its text

; ATA (IDE) primary channel ports
ATA_DATA       equ 0x1F0
ATA_COUNT      equ 0x1F2
ATA_LBA0       equ 0x1F3
ATA_LBA1       equ 0x1F4
ATA_LBA2       equ 0x1F5
ATA_DRIVE      equ 0x1F6
ATA_STATUS     equ 0x1F7        ; read
ATA_CMD        equ 0x1F7        ; write
ATA_CTRL       equ 0x3F6
ST_ERR         equ 0x01
ST_DRQ         equ 0x08
ST_DF          equ 0x20
ST_BSY         equ 0x80
STACK_TOP      equ 0x90000
BACKBUF        equ 0x400000     ; off-screen buffer (up to 3 MB)

CODE32_SEL     equ 0x08
DATA_SEL       equ 0x10
CODE64_SEL     equ 0x18

; ---------------- GUI layout ----------------
TERM_COLS      equ 80
TERM_ROWS      equ 24
BORDER         equ 2
PAD            equ 4
TITLE_H        equ 24
SHADOW         equ 6
WIN_W          equ TERM_COLS * 8 + 2 * PAD + 2 * BORDER
WIN_H          equ BORDER + TITLE_H + PAD + TERM_ROWS * 16 + PAD + BORDER
CLOSE_SIZE     equ 16
TASKBAR_H      equ 32
TASK_X         equ 64
TASK_W         equ 112
CUR_W          equ 12
CUR_H          equ 19
TIMER_HZ       equ 100          ; timer interrupts per second
PIT_DIVISOR    equ 1193182 / TIMER_HZ
CLOCK_EVERY    equ TIMER_HZ / 4 ; check the clock 4 times a second
PANIC_W        equ 640
PANIC_H        equ 272

; ---------------- colors (0xRRGGBB) ----------------
COLOR_WHITE        equ 0xFFFFFF
COLOR_ACCENT       equ 0x8AB4FF
COLOR_TASKBAR      equ 0x15151F
COLOR_TASKBAR_LINE equ 0x34344A
COLOR_TASK_ACTIVE  equ 0x3B3B55
COLOR_TASK_IDLE    equ 0x24242F
COLOR_FRAME        equ 0x2A2A3A
COLOR_TITLE        equ 0x2D6CDF
COLOR_CLOSE        equ 0xE0484F
COLOR_PANIC        equ 0x9E1B1B
GRAD_TOP_R equ 0x10
GRAD_TOP_G equ 0x2A
GRAD_TOP_B equ 0x5C
GRAD_BOT_R equ 0x5B
GRAD_BOT_G equ 0x2C
GRAD_BOT_B equ 0x83
TRANSPARENT        equ -1

%macro PUSHALL 0
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
%endmacro

%macro POPALL 0
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
%endmacro

; ============================================================
;  STAGE 1: 16-bit real mode (BIOS still available)
; ============================================================
[bits 16]
entry16:
    cld
    mov [boot_drive], dl

    ; can this CPU run 64-bit code?
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb no_long_mode
    mov eax, 0x80000001
    cpuid
    test edx, 1 << 29
    jz no_long_mode

    call load_font              ; borrow the BIOS 8x16 font
    call detect_memory          ; ask BIOS for the RAM map
    call setup_video            ; switch to a 32-bit color graphics mode
    jc no_video

    in al, 0x92                 ; enable A20 line
    or al, 2
    and al, 0xFE
    out 0x92, al

    cli
    mov al, 0xFF                ; mask all PIC interrupts (we poll)
    out 0x21, al
    out 0xA1, al

    lgdt [gdt_ptr]
    mov eax, cr0
    or eax, 1                   ; CR0.PE -> protected mode
    mov cr0, eax
    jmp CODE32_SEL:entry32

no_long_mode:
    mov si, msg_no_lm
    jmp fatal16
no_video:
    mov si, msg_no_video
fatal16:
    lodsb
    test al, al
    jz .halt
    mov ah, 0x0E
    xor bh, bh
    int 0x10
    jmp fatal16
.halt:
    cli
    hlt
    jmp .halt

; copy the BIOS 8x16 font (256 chars x 16 bytes) to FONT_ADDR
load_font:
    push ds
    push es
    push bp
    mov ax, 0x1130
    mov bh, 6                   ; 6 = 8x16 font
    int 0x10                    ; ES:BP -> font data
    push es
    pop ds
    mov si, bp
    mov ax, FONT_SEG
    mov es, ax
    xor di, di
    mov cx, 4096 / 2
    rep movsw
    pop bp
    pop es
    pop ds
    ret

; BIOS INT 15h / E820 memory map -> MEMMAP_ADDR
detect_memory:
    mov di, MEMMAP_ADDR
    xor ebx, ebx
    xor bp, bp
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

; find and set 1024x768x32 (or 800x600x32) with a linear framebuffer
setup_video:
    mov dword [VBE_INFO], 'VBE2'
    mov ax, 0x4F00              ; VBE: get controller info
    mov di, VBE_INFO
    int 0x10
    cmp ax, 0x004F
    jne .fail
    mov word [want_w], 1024
    mov word [want_h], 768
    call find_mode
    jnc .found
    mov word [want_w], 800
    mov word [want_h], 600
    call find_mode
    jnc .found
.fail:
    stc
    ret
.found:
    mov bx, [vbe_mode_num]
    or bx, 0x4000               ; use linear framebuffer
    mov ax, 0x4F02              ; VBE: set mode
    int 0x10
    cmp ax, 0x004F
    jne .fail
    mov eax, [VBE_MODE + 40]    ; physical framebuffer address
    mov [fb_addr], eax
    movzx eax, word [VBE_MODE + 16]
    mov [fb_pitch], eax         ; bytes per screen line
    movzx eax, word [VBE_MODE + 18]
    mov [scr_w], eax
    movzx eax, word [VBE_MODE + 20]
    mov [scr_h], eax
    clc
    ret

; walk the VBE mode list looking for want_w x want_h x 32bpp
find_mode:
    push fs
    mov si, [VBE_INFO + 14]     ; far pointer to mode list
    mov ax, [VBE_INFO + 16]
    mov fs, ax
.next:
    mov cx, [fs:si]
    cmp cx, 0xFFFF
    je .not_found
    add si, 2
    mov [vbe_mode_num], cx
    push si
    mov ax, 0x4F01              ; VBE: get mode info
    mov di, VBE_MODE
    int 0x10
    pop si
    cmp ax, 0x004F
    jne .next
    test word [VBE_MODE], 0x80  ; linear framebuffer supported?
    jz .next
    cmp byte [VBE_MODE + 25], 32
    jne .next
    mov ax, [VBE_MODE + 18]
    cmp ax, [want_w]
    jne .next
    mov ax, [VBE_MODE + 20]
    cmp ax, [want_h]
    jne .next
    pop fs
    clc
    ret
.not_found:
    pop fs
    stc
    ret

; ============================================================
;  STAGE 2: 32-bit protected mode -> paging -> long mode
; ============================================================
[bits 32]
entry32:
    mov ax, DATA_SEL
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, STACK_TOP

    ; identity-map the first 4 GB with 2 MB pages
    ; (the framebuffer usually lives just below 4 GB)
    mov edi, PML4_ADDR
    xor eax, eax
    mov ecx, 6 * 4096 / 4
    rep stosd
    mov dword [PML4_ADDR], PDPT_ADDR | 3
    mov dword [PDPT_ADDR],      (PD_ADDR + 0x0000) | 3
    mov dword [PDPT_ADDR + 8],  (PD_ADDR + 0x1000) | 3
    mov dword [PDPT_ADDR + 16], (PD_ADDR + 0x2000) | 3
    mov dword [PDPT_ADDR + 24], (PD_ADDR + 0x3000) | 3
    mov edi, PD_ADDR
    mov eax, 0x83               ; present + writable + 2 MB page
    mov ecx, 2048
.map:
    mov [edi], eax
    add eax, 0x200000
    add edi, 8
    loop .map

    mov eax, PML4_ADDR
    mov cr3, eax
    mov eax, cr4
    or eax, 1 << 5              ; CR4.PAE
    mov cr4, eax
    mov ecx, 0xC0000080         ; EFER
    rdmsr
    or eax, 1 << 8              ; EFER.LME
    wrmsr
    mov eax, cr0
    or eax, 0x80000000          ; CR0.PG -> long mode active
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

    call gui_init
    call mouse_init
    call ata_init               ; look for a hard disk
    call idt_init              ; exception + hardware interrupt handlers
    call pic_init               ; route IRQs to vectors 32-47
    call pit_init               ; timer: TIMER_HZ interrupts per second
    call ps2_flush
    sti                         ; interrupts ON
    call redraw_screen
    call clock_tick
    mov rsi, banner
    call print_str
    mov rsi, prompt
    call print_str

; main event loop: sleep until an interrupt arrives, then handle
; whatever the interrupt handlers queued up
event_loop:
    cli
    mov al, [kbq_head]
    cmp al, [kbq_tail]
    jne .work
    mov al, [msq_head]
    cmp al, [msq_tail]
    jne .work
    mov rax, [ticks]
    cmp rax, [next_clock]
    jae .work
    sti                         ; sti + hlt: no wake-up can be lost
    hlt                         ; CPU sleeps until the next interrupt
    jmp event_loop
.work:
    sti
.keys:
    movzx ebx, byte [kbq_tail]
    cmp bl, [kbq_head]
    je .mouse
    mov al, [kbq + rbx]
    inc byte [kbq_tail]
    call kb_decode
    test al, al
    jz .keys
    call on_key
    jmp .keys
.mouse:
    movzx ebx, byte [msq_tail]
    cmp bl, [msq_head]
    je .clock
    mov al, [msq + rbx]
    inc byte [msq_tail]
    call mouse_byte
    jmp .mouse
.clock:
    mov rax, [ticks]
    cmp rax, [next_clock]
    jb event_loop
    add rax, CLOCK_EVERY
    mov [next_clock], rax
    call clock_tick
    jmp event_loop

; ------------------------------------------------------------
;  Interrupts
; ------------------------------------------------------------

; fill the IDT: vectors 0-31 = CPU exceptions, 32-47 = hardware IRQs
idt_init:
    PUSHALL
    mov rdi, IDT_ADDR
    xor eax, eax
    mov ecx, 4096 / 8
    rep stosq
    xor ecx, ecx
.entry:
    mov rax, [isr_table + rcx * 8]
    mov rdi, rcx
    shl rdi, 4                  ; 16 bytes per entry
    add rdi, IDT_ADDR
    mov [rdi], ax               ; handler address bits 0-15
    mov word [rdi + 2], CODE64_SEL
    mov byte [rdi + 4], 0
    mov byte [rdi + 5], 0x8E    ; present, ring 0, interrupt gate
    shr rax, 16
    mov [rdi + 6], ax           ; bits 16-31
    shr rax, 16
    mov [rdi + 8], eax          ; bits 32-63
    mov dword [rdi + 12], 0
    inc ecx
    cmp ecx, 48
    jb .entry
    lidt [idt_ptr]
    POPALL
    ret

; remap the two 8259 PICs: IRQ 0-7 -> 32-39, IRQ 8-15 -> 40-47
pic_init:
    push rax
    mov al, 0x11                ; start init sequence
    out 0x20, al
    call io_wait
    out 0xA0, al
    call io_wait
    mov al, 32                  ; master offset
    out 0x21, al
    call io_wait
    mov al, 40                  ; slave offset
    out 0xA1, al
    call io_wait
    mov al, 4                   ; slave sits on IRQ2
    out 0x21, al
    call io_wait
    mov al, 2
    out 0xA1, al
    call io_wait
    mov al, 1                   ; 8086 mode
    out 0x21, al
    call io_wait
    out 0xA1, al
    call io_wait
    mov al, 0xF8                ; unmask IRQ0 timer, IRQ1 keyboard, IRQ2 cascade
    out 0x21, al
    mov al, 0xEF                ; unmask IRQ12 mouse
    out 0xA1, al
    pop rax
    ret

io_wait:
    push rax
    xor al, al
    out 0x80, al
    pop rax
    ret

; program the PIT (channel 0) to fire TIMER_HZ times per second
pit_init:
    push rax
    mov al, 0x36                ; channel 0, lo/hi byte, square wave
    out 0x43, al
    mov ax, PIT_DIVISOR
    out 0x40, al
    mov al, ah
    out 0x40, al
    pop rax
    ret

; throw away anything waiting in the PS/2 controller
ps2_flush:
    push rax
.loop:
    in al, 0x64
    test al, 1
    jz .done
    in al, 0x60
    jmp .loop
.done:
    mov byte [mouse_idx], 0
    pop rax
    ret

; interrupt entry stubs: push (error code), vector number
%assign i 0
%rep 48
isr_%+i:
  %if (i == 8) || (i == 10) || (i == 11) || (i == 12) || (i == 13) || (i == 14) || (i == 17) || (i == 21) || (i == 29) || (i == 30)
    ; CPU already pushed an error code
  %else
    push 0                      ; dummy error code
  %endif
    push i
    jmp isr_common
%assign i i + 1
%endrep

; stack here: [regs x15] [vector] [error] [rip] [cs] [rflags] [rsp] [ss]
isr_common:
    PUSHALL
    mov rax, [rsp + 15 * 8]     ; vector
    cmp rax, 32
    jb exception_handler        ; CPU exception -> panic screen
    sub rax, 32
    mov rbx, [irq_handlers + rax * 8]
    test rbx, rbx
    jz .eoi
    call rbx
.eoi:
    mov al, 0x20                ; "end of interrupt" to the PIC(s)
    cmp qword [rsp + 15 * 8], 40
    jb .master
    out 0xA0, al
.master:
    out 0x20, al
    POPALL
    add rsp, 16                 ; drop vector + error code
    iretq

; IRQ0: timer tick
irq_timer:
    inc qword [ticks]
    ret

; IRQ1 / IRQ12: a byte from the keyboard or the mouse -> queue it
irq_ps2:
    in al, 0x64
    test al, 1
    jz .done
    mov bl, al
    in al, 0x60
    test bl, 0x20
    jnz .mouse
    movzx ecx, byte [kbq_head]
    mov [kbq + rcx], al
    inc byte [kbq_head]
    ret
.mouse:
    movzx ecx, byte [msq_head]
    mov [msq + rcx], al
    inc byte [msq_head]
.done:
    ret

; CPU exception: draw a "kernel panic" box with the register dump, halt
exception_handler:
    cli
    mov rbp, rsp                ; RBP -> saved registers
    mov r12d, [scr_w]
    sub r12d, PANIC_W
    shr r12d, 1                 ; R12 = box x
    mov r13d, [scr_h]
    sub r13d, PANIC_H
    shr r13d, 1                 ; R13 = box y
    mov edi, r12d
    mov esi, r13d
    mov edx, PANIC_W
    mov ecx, PANIC_H
    mov r8d, COLOR_WHITE
    call fill_rect
    add edi, 2
    add esi, 2
    sub edx, 4
    sub ecx, 4
    mov r8d, COLOR_PANIC
    call fill_rect
    lea r14d, [r13 + 16]        ; R14 = current text line y

    mov rsi, str_panic_title
    call panic_print
    add r14d, 18

    mov rdi, line_buf           ; "Exception 0: Divide Error"
    mov rsi, str_exception
    call str_copy
    mov rax, [rbp + 15 * 8]
    call str_dec
    mov rsi, str_colon
    call str_copy
    mov rax, [rbp + 15 * 8]
    mov rsi, [exc_names + rax * 8]
    call str_copy
    mov rsi, line_buf
    call panic_print

    mov rdi, line_buf           ; "Error code: ...   CR2: ..."
    mov rsi, str_errcode
    call str_copy
    mov rax, [rbp + 16 * 8]
    call str_hex64
    mov rsi, str_cr2
    call str_copy
    mov rax, cr2                ; address that caused a page fault
    call str_hex64
    mov rsi, line_buf
    call panic_print
    add r14d, 18

    mov r15, reg_table          ; registers, 3 per line
.reg_line:
    mov rdi, line_buf
    mov byte [rdi], 0
    mov ecx, 3
.reg:
    mov rsi, [r15]
    test rsi, rsi
    jz .flush
    call str_copy
    mov rax, [r15 + 8]
    mov rax, [rbp + rax]
    call str_hex64
    mov rsi, str_gap2
    call str_copy
    add r15, 16
    dec ecx
    jnz .reg
.flush:
    mov rsi, line_buf
    call panic_print
    cmp qword [r15], 0
    jne .reg_line

    add r14d, 18
    mov rsi, str_halted
    call panic_print
    mov edi, r12d
    mov esi, r13d
    mov edx, PANIC_W
    mov ecx, PANIC_H
    call blit_raw
.halt:
    cli
    hlt
    jmp .halt

; draw RSI at the next line of the panic box
panic_print:
    PUSHALL
    mov rdx, rsi
    lea edi, [r12 + 16]
    mov esi, r14d
    mov r8d, COLOR_WHITE
    mov r9d, TRANSPARENT
    call draw_text
    POPALL
    add r14d, 18
    ret

; ------------------------------------------------------------
;  Input handling
; ------------------------------------------------------------

; AL = ASCII key
on_key:
    PUSHALL
    cmp byte [win_visible], 0
    je .done
    cmp al, 10
    je .enter
    cmp al, 8
    je .back
    cmp al, ' '
    jb .done
    cmp al, '~'
    ja .done
    mov ecx, [input_len]
    cmp ecx, MAX_INPUT
    jae .done
    mov [input_buf + rcx], al
    inc dword [input_len]
    call putc
    jmp .done
.back:
    cmp dword [input_len], 0
    je .done
    dec dword [input_len]
    call putc                   ; AL = 8 -> erase
    jmp .done
.enter:
    call newline
    mov ecx, [input_len]
    mov byte [input_buf + rcx], 0
    mov dword [input_len], 0
    call execute_command
    mov rsi, prompt
    call print_str
.done:
    POPALL
    ret

; left mouse button pressed at (mouse_x, mouse_y)
on_click:
    PUSHALL
    mov eax, [mouse_x]
    mov ebx, [mouse_y]
    cmp byte [win_visible], 0
    je .task_button
    mov ecx, [close_x]          ; close button?
    cmp eax, ecx
    jl .task_button
    add ecx, CLOSE_SIZE
    cmp eax, ecx
    jge .task_button
    mov ecx, [close_y]
    cmp ebx, ecx
    jl .task_button
    add ecx, CLOSE_SIZE
    cmp ebx, ecx
    jge .task_button
    mov byte [win_visible], 0
    call redraw_screen
    jmp .done
.task_button:                   ; "Terminal" button in the taskbar?
    cmp eax, TASK_X
    jl .done
    cmp eax, TASK_X + TASK_W
    jge .done
    mov ecx, [scr_h]
    sub ecx, TASKBAR_H - 4
    cmp ebx, ecx
    jl .done
    add ecx, 24
    cmp ebx, ecx
    jge .done
    xor byte [win_visible], 1
    call redraw_screen
.done:
    POPALL
    ret

; ------------------------------------------------------------
;  Shell
; ------------------------------------------------------------
execute_command:
    mov rsi, input_buf
    call skip_spaces
    cmp byte [rsi], 0
    je .done
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
    ret
.unknown:
    mov rsi, msg_unknown
    call print_str
.done:
    ret

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
;  Commands (RSI = argument text)
; ------------------------------------------------------------
cmd_help:
    mov rsi, msg_help
    jmp print_str

cmd_clear:
    jmp term_clear

cmd_echo:
    call print_str
    jmp newline

cmd_about:
    mov rsi, msg_about
    call print_str
    mov rsi, str_screen
    call print_str
    mov eax, [scr_w]
    call print_dec
    mov rsi, str_x
    call print_str
    mov eax, [scr_h]
    call print_dec
    mov rsi, msg_bpp
    jmp print_str

cmd_time:
    mov rsi, msg_date
    call print_str
    mov al, '2'
    call putc
    mov al, '0'
    call putc
    mov al, 0x09
    call rtc_read
    call print_bcd
    mov al, '-'
    call putc
    mov al, 0x08
    call rtc_read
    call print_bcd
    mov al, '-'
    call putc
    mov al, 0x07
    call rtc_read
    call print_bcd
    call newline
    mov rsi, msg_time
    call print_str
    mov al, 0x04
    call rtc_read
    call print_bcd
    mov al, ':'
    call putc
    mov al, 0x02
    call rtc_read
    call print_bcd
    mov al, ':'
    call putc
    mov al, 0x00
    call rtc_read
    call print_bcd
    jmp newline

cmd_mem:
    mov rsi, msg_mem_head
    call print_str
    movzx rcx, word [mem_count]
    mov rbx, MEMMAP_ADDR
    xor r8, r8
.entry:
    test rcx, rcx
    jz .total
    mov rsi, msg_gap
    call print_str
    mov rax, [rbx]
    call print_hex64
    mov rsi, msg_gap
    call print_str
    mov rax, [rbx + 8]
    call print_hex64
    mov rsi, msg_gap
    call print_str
    mov eax, [rbx + 16]
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
    shr rax, 20
    call print_dec
    mov rsi, msg_mb
    jmp print_str

cmd_cpu:
    xor eax, eax
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
    mov eax, 0x80000000
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
    mov ecx, 0xC0000080
    rdmsr
    mov rsi, msg_mode_lm
    test eax, 1 << 10
    jnz .print_mode
    mov rsi, msg_mode_other
.print_mode:
    jmp print_str

cmd_color:
    movzx ebx, byte [theme_index]
    inc ebx
    cmp ebx, THEME_COUNT
    jb .set
    xor ebx, ebx
.set:
    mov [theme_index], bl
    mov eax, [themes + rbx * 8]
    mov [term_fg], eax
    mov eax, [themes + rbx * 8 + 4]
    mov [term_bg], eax
    call term_refresh
    mov rsi, msg_color
    jmp print_str

cmd_uptime:
    mov rsi, msg_uptime
    call print_str
    mov rax, [ticks]
    xor edx, edx
    mov ebx, TIMER_HZ
    div rbx                     ; RAX = seconds since boot
    mov r9, rax
    xor edx, edx
    mov ebx, 3600
    div rbx
    call print_dec              ; hours
    mov rsi, msg_h
    call print_str
    mov rax, r9
    xor edx, edx
    mov ebx, 60
    div rbx
    xor edx, edx
    div rbx                     ; RDX = minutes % 60
    mov rax, rdx
    call print_dec
    mov rsi, msg_m
    call print_str
    mov rax, r9
    xor edx, edx
    div rbx                     ; RDX = seconds % 60
    mov rax, rdx
    call print_dec
    mov rsi, msg_s
    call print_str
    mov rsi, msg_ticks
    call print_str
    mov rax, [ticks]
    call print_dec
    mov rsi, msg_ticks_tail
    jmp print_str

cmd_disk:
    cmp byte [disk_present], 0
    jne .found
    mov rsi, msg_no_disk
    jmp print_str
.found:
    mov rsi, msg_disk_model
    call print_str
    mov rsi, disk_model
    call print_str
    call newline
    mov rsi, msg_disk_size
    call print_str
    mov eax, [disk_sectors]
    shr eax, 11                 ; sectors / 2048 = MB
    call print_dec
    mov rsi, msg_disk_mb
    call print_str
    mov eax, [disk_sectors]
    call print_dec
    mov rsi, msg_disk_sectors
    jmp print_str

cmd_save:
    cmp byte [rsi], 0
    jne .has_text
    mov rsi, msg_save_usage
    jmp print_str
.has_text:
    mov r12, rsi
    mov rdi, DISK_BUF           ; build the sector: "MYOSNOTE" + text
    xor eax, eax
    mov ecx, 512 / 8
    rep stosq
    mov rdi, DISK_BUF
    mov rsi, note_sig
    call str_copy
    mov rsi, r12
    call str_copy
    mov eax, NOTE_SECTOR
    mov rsi, DISK_BUF
    call ata_write
    jc disk_error
    mov rsi, msg_saved
    jmp print_str

cmd_load:
    mov eax, NOTE_SECTOR
    mov rdi, DISK_BUF
    call ata_read
    jc disk_error
    mov rax, [DISK_BUF]
    cmp rax, [note_sig]
    jne .empty
    mov rsi, msg_note
    call print_str
    mov rsi, DISK_BUF + 8
    call print_str
    jmp newline
.empty:
    mov rsi, msg_no_note
    jmp print_str

cmd_hexdump:
    call parse_dec
    mov r12d, eax
    cmp eax, [disk_sectors]
    jb .read
    mov rsi, msg_bad_sector
    jmp print_str
.read:
    mov rdi, DISK_BUF
    call ata_read
    jc disk_error
    mov rsi, msg_sector
    call print_str
    mov eax, r12d
    call print_dec
    mov rsi, msg_first_half
    call print_str
    mov rbx, DISK_BUF
    xor r13d, r13d              ; offset in sector
.line:
    mov eax, r13d
    call print_hex16
    mov al, ':'
    call putc
    mov al, ' '
    call putc
    xor ecx, ecx
.hex:
    mov al, [rbx + rcx]
    call print_hex8
    mov al, ' '
    call putc
    inc ecx
    cmp ecx, 16
    jb .hex
    mov al, '|'
    call putc
    xor ecx, ecx
.ascii:
    mov al, [rbx + rcx]
    cmp al, ' '
    jb .dot
    cmp al, '~'
    jbe .show
.dot:
    mov al, '.'
.show:
    call putc
    inc ecx
    cmp ecx, 16
    jb .ascii
    mov al, '|'
    call putc
    call newline
    add rbx, 16
    add r13d, 16
    cmp r13d, 256
    jb .line
    ret

disk_error:
    mov rsi, msg_disk_err
    jmp print_str

cmd_crash:
    cmp byte [rsi], 'p'
    je .page_fault
    xor ecx, ecx
    div ecx                     ; divide by zero -> exception 0
.page_fault:
    mov rax, 0x0000100000000000 ; not mapped -> exception 14
    mov rax, [rax]
    ret

cmd_reboot:
    mov rsi, msg_reboot
    call print_str
.wait:
    in al, 0x64
    test al, 2
    jnz .wait
    mov al, 0xFE
    out 0x64, al
    lidt [null_idt]
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
;  Terminal window (text grid drawn with the bitmap font)
; ------------------------------------------------------------

; print character AL in the terminal (10 = newline, 8 = backspace)
putc:
    PUSHALL
    mov r12d, [term_cx]         ; remember old cursor cell
    mov r13d, [term_cy]
    cmp al, 10
    je .newline
    cmp al, 8
    je .back
    mov ebx, r13d
    imul ebx, TERM_COLS
    add ebx, r12d
    mov [term_buf + rbx], al
    inc dword [term_cx]
    cmp dword [term_cx], TERM_COLS
    jb .redraw
.newline:
    mov dword [term_cx], 0
    inc dword [term_cy]
    cmp dword [term_cy], TERM_ROWS
    jb .redraw
    mov dword [term_cy], TERM_ROWS - 1
    call term_scroll
    jmp .done
.back:
    test r12d, r12d
    jz .done
    dec dword [term_cx]
    mov ebx, r13d
    imul ebx, TERM_COLS
    add ebx, r12d
    dec ebx
    mov byte [term_buf + rbx], ' '
.redraw:
    mov ecx, r12d               ; old cell (removes cursor)
    mov edx, r13d
    call term_draw_cell
    mov ecx, [term_cx]          ; new cell (draws cursor)
    mov edx, [term_cy]
    call term_draw_cell
.done:
    POPALL
    ret

term_scroll:
    PUSHALL
    mov rdi, term_buf
    mov rsi, term_buf + TERM_COLS
    mov ecx, TERM_COLS * (TERM_ROWS - 1)
    rep movsb
    mov al, ' '
    mov ecx, TERM_COLS
    rep stosb
    call term_refresh
    POPALL
    ret

term_clear:
    PUSHALL
    mov rdi, term_buf
    mov al, ' '
    mov ecx, TERM_COLS * TERM_ROWS
    rep stosb
    mov dword [term_cx], 0
    mov dword [term_cy], 0
    call term_refresh
    POPALL
    ret

; draw one cell (ECX = col, EDX = row) into the back buffer
term_paint_cell:
    PUSHALL
    mov r14d, ecx
    mov r15d, edx
    mov eax, edx
    imul eax, TERM_COLS
    add eax, ecx
    movzx r10d, byte [term_buf + rax]
    mov edi, ecx
    shl edi, 3
    add edi, [content_x]
    mov esi, edx
    shl esi, 4
    add esi, [content_y]
    mov edx, r10d
    mov r8d, [term_fg]
    mov r9d, [term_bg]
    call draw_char
    cmp r14d, [term_cx]         ; text cursor here? draw underline
    jne .done
    cmp r15d, [term_cy]
    jne .done
    add esi, 13
    mov edx, 8
    mov ecx, 2
    call fill_rect
.done:
    POPALL
    ret

; draw one cell and show it on screen
term_draw_cell:
    cmp byte [win_visible], 0
    je .ret
    call term_paint_cell
    PUSHALL
    mov edi, ecx
    shl edi, 3
    add edi, [content_x]
    mov esi, edx
    shl esi, 4
    add esi, [content_y]
    mov edx, 8
    mov ecx, 16
    call blit_rect
    POPALL
.ret:
    ret

; draw the whole text area into the back buffer
term_render:
    PUSHALL
    mov edi, [content_x]
    sub edi, PAD
    mov esi, [content_y]
    sub esi, PAD
    mov edx, TERM_COLS * 8 + 2 * PAD
    mov ecx, TERM_ROWS * 16 + 2 * PAD
    mov r8d, [term_bg]
    call fill_rect
    xor r13d, r13d
.row:
    xor r12d, r12d
.col:
    mov ecx, r12d
    mov edx, r13d
    call term_paint_cell
    inc r12d
    cmp r12d, TERM_COLS
    jb .col
    inc r13d
    cmp r13d, TERM_ROWS
    jb .row
    POPALL
    ret

; redraw the text area and show it on screen
term_refresh:
    cmp byte [win_visible], 0
    je .ret
    call term_render
    PUSHALL
    mov edi, [content_x]
    sub edi, PAD
    mov esi, [content_y]
    sub esi, PAD
    mov edx, TERM_COLS * 8 + 2 * PAD
    mov ecx, TERM_ROWS * 16 + 2 * PAD
    call blit_rect
    POPALL
.ret:
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

print_dec:                      ; RAX unsigned decimal
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

print_hex64:                    ; RAX as 0x + 16 hex digits
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

print_nibble:                   ; low 4 bits of AL as one hex digit
    push rax
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .print
    add al, 7
.print:
    call putc
    pop rax
    ret

print_hex8:                     ; AL as 2 hex digits
    push rax
    push rax
    shr al, 4
    call print_nibble
    pop rax
    call print_nibble
    pop rax
    ret

print_hex16:                    ; AX as 4 hex digits
    push rax
    push rax
    mov al, ah
    call print_hex8
    pop rax
    call print_hex8
    pop rax
    ret

; RSI -> RAX (decimal digits until the first non-digit)
parse_dec:
    push rbx
    xor eax, eax
.loop:
    movzx ebx, byte [rsi]
    sub ebx, '0'
    cmp ebx, 9
    ja .done
    imul rax, rax, 10
    add rax, rbx
    inc rsi
    jmp .loop
.done:
    pop rbx
    ret

print_bcd:                      ; AL (BCD) as two digits
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

; ------------------------------------------------------------
;  Desktop
; ------------------------------------------------------------
gui_init:
    PUSHALL
    mov eax, [scr_w]            ; center the window above the taskbar
    sub eax, WIN_W
    shr eax, 1
    mov [win_x], eax
    mov eax, [scr_h]
    sub eax, TASKBAR_H + WIN_H
    shr eax, 1
    mov [win_y], eax
    mov eax, [win_x]
    add eax, BORDER + PAD
    mov [content_x], eax
    mov eax, [win_y]
    add eax, BORDER + TITLE_H + PAD
    mov [content_y], eax
    mov eax, [win_x]
    add eax, WIN_W - BORDER - 4 - CLOSE_SIZE
    mov [close_x], eax
    mov eax, [win_y]
    add eax, BORDER + 4
    mov [close_y], eax

    mov eax, [scr_w]            ; mouse starts in the middle
    shr eax, 1
    mov [mouse_x], eax
    mov eax, [scr_h]
    shr eax, 1
    mov [mouse_y], eax

    mov rdi, term_buf
    mov al, ' '
    mov ecx, TERM_COLS * TERM_ROWS
    rep stosb

    mov rdi, desk_info          ; "Screen: 1024 x 768 ..."
    mov rsi, str_screen
    call str_copy
    mov eax, [scr_w]
    call str_dec
    mov rsi, str_x
    call str_copy
    mov eax, [scr_h]
    call str_dec
    mov rsi, str_desk_tail
    call str_copy
    POPALL
    ret

; draw everything into the back buffer and show it
redraw_screen:
    PUSHALL
    call draw_desktop
    xor edi, edi
    xor esi, esi
    mov edx, [scr_w]
    mov ecx, [scr_h]
    call blit_rect
    POPALL
    ret

draw_desktop:
    PUSHALL
    xor edi, edi
    xor esi, esi
    mov edx, [scr_w]
    mov ecx, [scr_h]
    sub ecx, TASKBAR_H
    call draw_bg_rect
    mov edi, 16
    mov esi, 14
    mov rdx, str_desk_title
    mov r8d, COLOR_WHITE
    mov r9d, TRANSPARENT
    call draw_text
    mov esi, 34
    mov rdx, desk_info
    mov r8d, COLOR_ACCENT
    call draw_text
    call draw_taskbar
    cmp byte [win_visible], 0
    je .done
    call draw_window
.done:
    POPALL
    ret

draw_taskbar:
    PUSHALL
    mov r15d, [scr_h]
    sub r15d, TASKBAR_H         ; R15 = taskbar top
    xor edi, edi
    mov esi, r15d
    mov edx, [scr_w]
    mov ecx, TASKBAR_H
    mov r8d, COLOR_TASKBAR
    call fill_rect
    mov ecx, 1
    mov r8d, COLOR_TASKBAR_LINE
    call fill_rect
    mov edi, 14                 ; "MyOS" logo text
    lea esi, [r15 + 8]
    mov rdx, str_logo
    mov r8d, COLOR_ACCENT
    mov r9d, TRANSPARENT
    call draw_text
    mov edi, TASK_X             ; "Terminal" button
    lea esi, [r15 + 4]
    mov edx, TASK_W
    mov ecx, 24
    mov r8d, COLOR_TASK_IDLE
    cmp byte [win_visible], 0
    je .btn
    mov r8d, COLOR_TASK_ACTIVE
.btn:
    call fill_rect
    cmp byte [win_visible], 0
    je .label
    lea esi, [r15 + 26]         ; active: accent underline
    mov ecx, 2
    mov r8d, COLOR_ACCENT
    call fill_rect
.label:
    mov edi, TASK_X + (TASK_W - 64) / 2
    lea esi, [r15 + 8]
    mov rdx, str_term_title
    mov r8d, COLOR_WHITE
    mov r9d, TRANSPARENT
    call draw_text
    call draw_clock
    POPALL
    ret

draw_window:
    PUSHALL
    mov edi, [win_x]            ; soft shadow
    add edi, SHADOW
    mov esi, [win_y]
    add esi, SHADOW
    mov edx, WIN_W
    mov ecx, WIN_H
    call darken_rect
    mov edi, [win_x]            ; frame
    mov esi, [win_y]
    mov r8d, COLOR_FRAME
    call fill_rect
    add edi, BORDER             ; title bar
    add esi, BORDER
    mov edx, WIN_W - 2 * BORDER
    mov ecx, TITLE_H
    mov r8d, COLOR_TITLE
    call fill_rect
    add edi, 8
    add esi, 4
    mov rdx, str_term_title
    mov r8d, COLOR_WHITE
    mov r9d, TRANSPARENT
    call draw_text
    mov edi, [close_x]          ; close button
    mov esi, [close_y]
    mov edx, CLOSE_SIZE
    mov ecx, CLOSE_SIZE
    mov r8d, COLOR_CLOSE
    call fill_rect
    add edi, 4
    mov edx, 'x'
    mov r8d, COLOR_WHITE
    call draw_char
    call term_render
    POPALL
    ret

; ------------------------------------------------------------
;  Taskbar clock
; ------------------------------------------------------------
clock_tick:
    PUSHALL
    xor eax, eax                ; seconds changed?
    call rtc_read
    cmp al, [last_sec]
    je .done
    mov [last_sec], al
    mov rdi, clock_str
    mov al, 0x04
    call rtc_read
    call store_bcd
    inc rdi
    mov al, 0x02
    call rtc_read
    call store_bcd
    inc rdi
    mov al, 0x00
    call rtc_read
    call store_bcd
    call draw_clock
    call clock_pos
    mov edx, 8 * 8
    mov ecx, 16
    call blit_rect
.done:
    POPALL
    ret

draw_clock:
    PUSHALL
    call clock_pos
    mov rdx, clock_str
    mov r8d, COLOR_WHITE
    mov r9d, COLOR_TASKBAR
    call draw_text
    POPALL
    ret

clock_pos:                      ; -> EDI = x, ESI = y
    mov edi, [scr_w]
    sub edi, 8 * 8 + 16
    mov esi, [scr_h]
    sub esi, TASKBAR_H - 8
    ret

store_bcd:                      ; AL (BCD) -> 2 ASCII digits at RDI, RDI += 2
    push rax
    mov ah, al
    shr al, 4
    add al, '0'
    mov [rdi], al
    mov al, ah
    and al, 0x0F
    add al, '0'
    mov [rdi + 1], al
    add rdi, 2
    pop rax
    ret

; ------------------------------------------------------------
;  Driver: graphics (all drawing goes to BACKBUF first)
;  Common args: EDI = x, ESI = y, EDX = width, ECX = height
; ------------------------------------------------------------

; fill a rectangle with color R8D
fill_rect:
    PUSHALL
    mov eax, esi
    imul eax, [scr_w]
    add eax, edi
    lea rbx, [BACKBUF + rax * 4]
    mov r9d, [scr_w]
    shl r9, 2
    mov r10d, ecx
    mov eax, r8d
.row:
    test r10d, r10d
    jz .done
    mov rdi, rbx
    mov ecx, edx
    rep stosd
    add rbx, r9
    dec r10d
    jmp .row
.done:
    POPALL
    ret

; halve the brightness of a rectangle (used for shadows)
darken_rect:
    PUSHALL
    mov eax, esi
    imul eax, [scr_w]
    add eax, edi
    lea rbx, [BACKBUF + rax * 4]
    mov r9d, [scr_w]
    shl r9, 2
    mov r10d, ecx
.row:
    test r10d, r10d
    jz .done
    mov rdi, rbx
    mov ecx, edx
.px:
    mov eax, [rdi]
    shr eax, 1
    and eax, 0x7F7F7F
    mov [rdi], eax
    add rdi, 4
    dec ecx
    jnz .px
    add rbx, r9
    dec r10d
    jmp .row
.done:
    POPALL
    ret

; background gradient for a rectangle
draw_bg_rect:
    PUSHALL
    mov r12d, ecx
.row:
    test r12d, r12d
    jz .done
    call bg_color
    mov ecx, 1
    call fill_rect
    inc esi
    dec r12d
    jmp .row
.done:
    POPALL
    ret

%macro GRAD_CHANNEL 3           ; top, bottom, bit shift
    mov eax, %2 - %1
    imul eax, ebx
    sar eax, 8
    add eax, %1
    shl eax, %3
    or r8d, eax
%endmacro

; ESI = y -> R8D = gradient color for that row
bg_color:
    push rax
    push rbx
    push rcx
    push rdx
    mov eax, esi
    shl eax, 8
    xor edx, edx
    mov ecx, [scr_h]
    sub ecx, TASKBAR_H
    div ecx
    mov ebx, eax                ; EBX = 0..255 position
    xor r8d, r8d
    GRAD_CHANNEL GRAD_TOP_R, GRAD_BOT_R, 16
    GRAD_CHANNEL GRAD_TOP_G, GRAD_BOT_G, 8
    GRAD_CHANNEL GRAD_TOP_B, GRAD_BOT_B, 0
    pop rdx
    pop rcx
    pop rbx
    pop rax
    ret

; draw character EDX at (EDI, ESI), fg R8D, bg R9D (TRANSPARENT = none)
draw_char:
    PUSHALL
    movzx edx, dl
    shl edx, 4
    lea r10, [FONT_ADDR + rdx]  ; glyph: 16 rows, 1 byte each
    mov eax, esi
    imul eax, [scr_w]
    add eax, edi
    lea rbx, [BACKBUF + rax * 4]
    mov r11d, [scr_w]
    shl r11, 2
    mov ecx, 16
.row:
    movzx r12d, byte [r10]
    mov r13d, 8
    mov rdi, rbx
.px:
    test r12d, 0x80
    jz .bg
    mov [rdi], r8d
    jmp .next
.bg:
    cmp r9d, TRANSPARENT
    je .next
    mov [rdi], r9d
.next:
    add rdi, 4
    shl r12d, 1
    dec r13d
    jnz .px
    inc r10
    add rbx, r11
    dec ecx
    jnz .row
    POPALL
    ret

; draw zero-terminated string RDX at (EDI, ESI)
draw_text:
    PUSHALL
    mov r10, rdx
.loop:
    movzx edx, byte [r10]
    test edx, edx
    jz .done
    call draw_char
    add edi, 8
    inc r10
    jmp .loop
.done:
    POPALL
    ret

; copy a rectangle from the back buffer to the real screen
blit_raw:
    PUSHALL
    mov eax, esi
    imul eax, [scr_w]
    add eax, edi
    lea r10, [BACKBUF + rax * 4]    ; source
    mov eax, esi
    imul eax, [fb_pitch]
    mov r11, [fb_addr]
    add r11, rax
    mov eax, edi
    lea r11, [r11 + rax * 4]        ; destination
    mov r12d, [scr_w]
    shl r12, 2
    mov r13d, [fb_pitch]
    mov r14d, ecx
.row:
    test r14d, r14d
    jz .done
    mov rsi, r10
    mov rdi, r11
    mov ecx, edx
    rep movsd
    add r10, r12
    add r11, r13
    dec r14d
    jmp .row
.done:
    POPALL
    ret

; blit, then redraw the mouse pointer if it overlaps
blit_rect:
    call blit_raw
    PUSHALL
    mov eax, [mouse_x]
    add eax, CUR_W
    cmp edi, eax
    jge .done
    lea eax, [rdi + rdx]
    cmp eax, [mouse_x]
    jle .done
    mov eax, [mouse_y]
    add eax, CUR_H
    cmp esi, eax
    jge .done
    lea eax, [rsi + rcx]
    cmp eax, [mouse_y]
    jle .done
    call draw_cursor
.done:
    POPALL
    ret

; ------------------------------------------------------------
;  Mouse pointer (drawn straight onto the screen)
; ------------------------------------------------------------
draw_cursor:
    PUSHALL
    mov r10, cursor_sprite
    xor esi, esi                ; sprite row
.row:
    cmp esi, CUR_H
    jae .done
    mov eax, [mouse_y]
    add eax, esi
    cmp eax, [scr_h]
    jae .done
    imul eax, [fb_pitch]
    mov rbx, [fb_addr]
    add rbx, rax
    xor edi, edi                ; sprite column
.col:
    cmp edi, CUR_W
    jae .next_row
    mov eax, [mouse_x]
    add eax, edi
    cmp eax, [scr_w]
    jae .next_row
    movzx edx, byte [r10 + rdi]
    cmp dl, 'X'
    jne .not_edge
    mov dword [rbx + rax * 4], 0x000000
    jmp .next_col
.not_edge:
    cmp dl, '.'
    jne .next_col
    mov dword [rbx + rax * 4], 0xFFFFFF
.next_col:
    inc edi
    jmp .col
.next_row:
    add r10, CUR_W
    inc esi
    jmp .row
.done:
    POPALL
    ret

; restore what was under the pointer from the back buffer
erase_cursor:
    PUSHALL
    mov edi, [mouse_x]
    mov esi, [mouse_y]
    mov edx, [scr_w]
    sub edx, edi
    cmp edx, CUR_W
    jbe .w_ok
    mov edx, CUR_W
.w_ok:
    mov ecx, [scr_h]
    sub ecx, esi
    cmp ecx, CUR_H
    jbe .h_ok
    mov ecx, CUR_H
.h_ok:
    call blit_raw
    POPALL
    ret

; ------------------------------------------------------------
;  Driver: PS/2 controller, keyboard and mouse
; ------------------------------------------------------------
kbc_wait_write:                 ; wait until we may send a byte
    push rax
    push rcx
    mov ecx, 100000
.loop:
    in al, 0x64
    test al, 2
    jz .ok
    loop .loop
.ok:
    pop rcx
    pop rax
    ret

kbc_wait_read:                  ; wait until a byte is available
    push rax
    push rcx
    mov ecx, 100000
.loop:
    in al, 0x64
    test al, 1
    jnz .ok
    loop .loop
.ok:
    pop rcx
    pop rax
    ret

mouse_cmd:                      ; send AL to the mouse, eat the ACK
    push rax
    mov ah, al
    call kbc_wait_write
    mov al, 0xD4                ; "next byte is for the mouse"
    out 0x64, al
    call kbc_wait_write
    mov al, ah
    out 0x60, al
    call kbc_wait_read
    in al, 0x60
    pop rax
    ret

mouse_init:
    PUSHALL
    call kbc_wait_write
    mov al, 0xA8                ; enable the mouse port
    out 0x64, al
    call kbc_wait_write
    mov al, 0x20                ; read controller config
    out 0x64, al
    call kbc_wait_read
    in al, 0x60
    and al, 0xDF                ; clear "mouse clock disabled"
    or al, 0x03                 ; enable keyboard + mouse interrupts
    mov bl, al
    call kbc_wait_write
    mov al, 0x60                ; write controller config
    out 0x64, al
    call kbc_wait_write
    mov al, bl
    out 0x60, al
    mov al, 0xF6                ; mouse: default settings
    call mouse_cmd
    mov al, 0xF4                ; mouse: start sending packets
    call mouse_cmd
    POPALL
    ret

; collect 3-byte mouse packets (byte in AL)
mouse_byte:
    PUSHALL
    movzx ecx, byte [mouse_idx]
    test ecx, ecx
    jnz .store
    test al, 0x08               ; first byte always has bit 3 set
    jz .done
.store:
    mov [mouse_pkt + rcx], al
    inc ecx
    cmp ecx, 3
    jb .save
    xor ecx, ecx
    mov [mouse_idx], cl
    call mouse_packet
    jmp .done
.save:
    mov [mouse_idx], cl
.done:
    POPALL
    ret

mouse_packet:
    PUSHALL
    movzx eax, byte [mouse_pkt]
    test al, 0xC0               ; overflow -> ignore movement
    jnz .buttons
    movzx edx, byte [mouse_pkt + 1]
    test al, 0x10               ; X sign
    jz .dx_ok
    or edx, 0xFFFFFF00
.dx_ok:
    movzx ecx, byte [mouse_pkt + 2]
    test al, 0x20               ; Y sign
    jz .dy_ok
    or ecx, 0xFFFFFF00
.dy_ok:
    mov ebx, edx
    or ebx, ecx
    jz .buttons
    call erase_cursor
    mov eax, [mouse_x]
    add eax, edx
    cmp eax, 0
    jge .x_low_ok
    xor eax, eax
.x_low_ok:
    cmp eax, [scr_w]
    jl .x_high_ok
    mov eax, [scr_w]
    dec eax
.x_high_ok:
    mov [mouse_x], eax
    mov eax, [mouse_y]
    sub eax, ecx                ; PS/2 Y goes up, screen Y goes down
    cmp eax, 0
    jge .y_low_ok
    xor eax, eax
.y_low_ok:
    cmp eax, [scr_h]
    jl .y_high_ok
    mov eax, [scr_h]
    dec eax
.y_high_ok:
    mov [mouse_y], eax
    call draw_cursor
.buttons:
    mov al, [mouse_pkt]
    and al, 1                   ; left button
    mov bl, [mouse_btn]
    mov [mouse_btn], al
    test al, al
    jz .done
    test bl, bl
    jnz .done
    call on_click               ; just pressed
.done:
    POPALL
    ret

; AL = scancode (set 1) -> AL = ASCII, or 0 if nothing to type
kb_decode:
    push rbx
    cmp al, 0xE0
    jne .not_ext
    mov byte [kb_ext], 1
    jmp .none
.not_ext:
    cmp byte [kb_ext], 0
    je .normal
    mov byte [kb_ext], 0
    jmp .none
.normal:
    cmp al, 0x2A
    je .shift_down
    cmp al, 0x36
    je .shift_down
    cmp al, 0xAA
    je .shift_up
    cmp al, 0xB6
    je .shift_up
    test al, 0x80
    jnz .none
    cmp al, KEYMAP_SIZE
    jae .none
    movzx rbx, al
    cmp byte [kb_shift], 0
    jne .shifted
    mov al, [keymap + rbx]
    jmp .out
.shifted:
    mov al, [keymap_shift + rbx]
    jmp .out
.shift_down:
    mov byte [kb_shift], 1
    jmp .none
.shift_up:
    mov byte [kb_shift], 0
.none:
    xor eax, eax
.out:
    pop rbx
    ret

; ------------------------------------------------------------
;  Driver: ATA hard disk (PIO mode, primary master, LBA28)
; ------------------------------------------------------------

; ask the disk who it is (IDENTIFY), fill disk_model / disk_sectors
ata_init:
    PUSHALL
    mov byte [disk_present], 0
    mov dx, ATA_CTRL
    mov al, 0x02                ; nIEN: the disk must not send IRQs
    out dx, al
    mov dx, ATA_STATUS
    in al, dx
    cmp al, 0xFF                ; floating bus = no controller
    je .done
    mov dx, ATA_DRIVE
    mov al, 0xA0                ; select master
    out dx, al
    call ata_delay
    xor al, al
    mov dx, ATA_COUNT
    out dx, al
    mov dx, ATA_LBA0
    out dx, al
    mov dx, ATA_LBA1
    out dx, al
    mov dx, ATA_LBA2
    out dx, al
    mov dx, ATA_CMD
    mov al, 0xEC                ; IDENTIFY
    out dx, al
    call ata_delay
    mov dx, ATA_STATUS
    in al, dx
    test al, al                 ; 0 = no drive
    jz .done
    call ata_wait_ready
    jc .done
    mov dx, ATA_LBA1            ; non-zero here = not an ATA disk
    in al, dx
    test al, al
    jnz .done
    mov dx, ATA_LBA2
    in al, dx
    test al, al
    jnz .done
    call ata_wait_drq
    jc .done
    mov rdi, IDENT_BUF
    mov dx, ATA_DATA
    mov ecx, 256
    rep insw
    mov eax, [IDENT_BUF + 120]  ; words 60-61: total LBA28 sectors
    mov [disk_sectors], eax
    mov rsi, IDENT_BUF + 54     ; words 27-46: model name, bytes swapped
    mov rdi, disk_model
    mov ecx, 20
.swap:
    lodsw
    xchg al, ah
    stosw
    loop .swap
    mov byte [rdi], 0
.trim:                          ; drop trailing spaces
    dec rdi
    cmp rdi, disk_model
    jb .trimmed
    cmp byte [rdi], ' '
    jne .trimmed
    mov byte [rdi], 0
    jmp .trim
.trimmed:
    mov byte [disk_present], 1
.done:
    POPALL
    ret

; read sector EAX into RDI (512 bytes). CF=1 on error
ata_read:
    PUSHALL
    cmp byte [disk_present], 0
    je .fail
    call ata_select
    jc .fail
    mov dx, ATA_CMD
    mov al, 0x20                ; READ SECTORS
    out dx, al
    call ata_delay
    call ata_wait_drq
    jc .fail
    mov dx, ATA_DATA
    mov ecx, 256                ; 256 words = 512 bytes
    rep insw
    POPALL
    clc
    ret
.fail:
    POPALL
    stc
    ret

; write 512 bytes from RSI to sector EAX. CF=1 on error
ata_write:
    PUSHALL
    cmp byte [disk_present], 0
    je .fail
    call ata_select
    jc .fail
    mov dx, ATA_CMD
    mov al, 0x30                ; WRITE SECTORS
    out dx, al
    call ata_delay
    call ata_wait_drq
    jc .fail
    mov dx, ATA_DATA
    mov ecx, 256
    rep outsw
    call ata_wait_ready
    jc .fail
    mov dx, ATA_CMD
    mov al, 0xE7                ; CACHE FLUSH: make sure it hits the disk
    out dx, al
    call ata_delay
    call ata_wait_ready
    jc .fail
    POPALL
    clc
    ret
.fail:
    POPALL
    stc
    ret

; select master, LBA = EAX, sector count = 1
ata_select:
    push rax
    push rcx
    push rdx
    mov ecx, eax
    call ata_wait_ready
    jc .out
    mov dx, ATA_DRIVE
    mov eax, ecx
    shr eax, 24
    and al, 0x0F
    or al, 0xE0                 ; master + LBA mode + LBA bits 24-27
    out dx, al
    call ata_delay
    mov dx, ATA_COUNT
    mov al, 1
    out dx, al
    mov eax, ecx
    mov dx, ATA_LBA0
    out dx, al
    shr eax, 8
    mov dx, ATA_LBA1
    out dx, al
    shr eax, 8
    mov dx, ATA_LBA2
    out dx, al
    clc
.out:
    pop rdx
    pop rcx
    pop rax
    ret

; wait while the disk is busy. CF=1 on timeout
ata_wait_ready:
    push rax
    push rcx
    push rdx
    mov dx, ATA_STATUS
    mov ecx, 1000000
.loop:
    in al, dx
    test al, ST_BSY
    jz .ok
    loop .loop
    stc
    jmp .out
.ok:
    clc
.out:
    pop rdx
    pop rcx
    pop rax
    ret

; wait until the disk has data for us / wants data. CF=1 on error
ata_wait_drq:
    push rax
    push rcx
    push rdx
    mov dx, ATA_STATUS
    mov ecx, 1000000
.loop:
    in al, dx
    test al, ST_BSY
    jnz .next
    test al, ST_ERR | ST_DF
    jnz .fail
    test al, ST_DRQ
    jnz .ok
.next:
    loop .loop
.fail:
    stc
    jmp .out
.ok:
    clc
.out:
    pop rdx
    pop rcx
    pop rax
    ret

; ~400 ns pause: read the alternate status register 4 times
ata_delay:
    push rax
    push rdx
    mov dx, ATA_CTRL
    in al, dx
    in al, dx
    in al, dx
    in al, dx
    pop rdx
    pop rax
    ret

; ------------------------------------------------------------
;  Driver: CMOS real-time clock
; ------------------------------------------------------------
rtc_read:                       ; AL = register -> AL = BCD value
    push rbx
    mov bl, al
.busy:
    mov al, 0x0A
    out 0x70, al
    in al, 0x71
    test al, 0x80
    jnz .busy
    mov al, 0x0B
    out 0x70, al
    in al, 0x71
    mov bh, al
    mov al, bl
    out 0x70, al
    in al, 0x71
    test bh, 4
    jz .done
    movzx eax, al
    mov bl, 10
    div bl
    shl al, 4
    or al, ah
.done:
    pop rbx
    ret

; ------------------------------------------------------------
;  String helpers
; ------------------------------------------------------------
str_copy:                       ; RSI -> RDI, RDI ends on the terminator
    push rax
.loop:
    lodsb
    mov [rdi], al
    test al, al
    jz .done
    inc rdi
    jmp .loop
.done:
    pop rax
    ret

str_hex64:                      ; RAX -> "0x" + 16 hex digits at RDI
    push rax
    push rbx
    push rcx
    mov rbx, rax
    mov word [rdi], '0x'
    add rdi, 2
    mov ecx, 16
.digit:
    rol rbx, 4
    mov al, bl
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .store
    add al, 7
.store:
    mov [rdi], al
    inc rdi
    loop .digit
    mov byte [rdi], 0
    pop rcx
    pop rbx
    pop rax
    ret

str_dec:                        ; EAX -> decimal at RDI
    push rax
    push rbx
    push rcx
    push rdx
    mov ebx, 10
    xor ecx, ecx
.div:
    xor edx, edx
    div ebx
    push rdx
    inc ecx
    test eax, eax
    jnz .div
.out:
    pop rax
    add al, '0'
    mov [rdi], al
    inc rdi
    loop .out
    mov byte [rdi], 0
    pop rdx
    pop rcx
    pop rbx
    pop rax
    ret

; ============================================================
;  Data
; ============================================================
align 8
gdt:
    dq 0
    dq 0x00CF9A000000FFFF       ; 0x08: 32-bit code
    dq 0x00CF92000000FFFF       ; 0x10: data
    dq 0x00AF9A000000FFFF       ; 0x18: 64-bit code
gdt_end:
gdt_ptr:
    dw gdt_end - gdt - 1
    dd gdt
null_idt:
    dw 0
    dq 0
idt_ptr:
    dw 4096 - 1
    dq IDT_ADDR

align 8
isr_table:
%assign i 0
%rep 48
    dq isr_%+i
%assign i i + 1
%endrep

irq_handlers:                   ; IRQ 0-15
    dq irq_timer, irq_ps2, 0, 0, 0, 0, 0, 0
    dq 0, 0, 0, 0, irq_ps2, 0, 0, 0

exc_names:
    dq e00, e01, e02, e03, e04, e05, e06, e07
    dq e08, e09, e10, e11, e12, e13, e14, e15
    dq e16, e17, e18, e19, e20, e21, e15, e15
    dq e15, e15, e15, e15, e15, e15, e15, e15
e00 db "Divide Error", 0
e01 db "Debug", 0
e02 db "Non-Maskable Interrupt", 0
e03 db "Breakpoint", 0
e04 db "Overflow", 0
e05 db "Bound Range Exceeded", 0
e06 db "Invalid Opcode", 0
e07 db "Device Not Available", 0
e08 db "Double Fault", 0
e09 db "Coprocessor Segment Overrun", 0
e10 db "Invalid TSS", 0
e11 db "Segment Not Present", 0
e12 db "Stack-Segment Fault", 0
e13 db "General Protection Fault", 0
e14 db "Page Fault", 0
e15 db "Reserved", 0
e16 db "x87 Floating-Point Error", 0
e17 db "Alignment Check", 0
e18 db "Machine Check", 0
e19 db "SIMD Floating-Point Error", 0
e20 db "Virtualization Exception", 0
e21 db "Control Protection Exception", 0

; register name, offset in the saved frame
align 8
reg_table:
    dq r_rax, 112, r_rbx, 104, r_rcx, 96
    dq r_rdx, 88,  r_rsi, 80,  r_rdi, 72
    dq r_rbp, 64,  r_rsp, 160, r_r8,  56
    dq r_r9,  48,  r_r10, 40,  r_r11, 32
    dq r_r12, 24,  r_r13, 16,  r_r14, 8
    dq r_r15, 0,   r_rip, 136, r_rfl, 152
    dq 0
r_rax db "RAX=", 0
r_rbx db "RBX=", 0
r_rcx db "RCX=", 0
r_rdx db "RDX=", 0
r_rsi db "RSI=", 0
r_rdi db "RDI=", 0
r_rbp db "RBP=", 0
r_rsp db "RSP=", 0
r_r8  db "R8 =", 0
r_r9  db "R9 =", 0
r_r10 db "R10=", 0
r_r11 db "R11=", 0
r_r12 db "R12=", 0
r_r13 db "R13=", 0
r_r14 db "R14=", 0
r_r15 db "R15=", 0
r_rip db "RIP=", 0
r_rfl db "FLG=", 0

str_panic_title db "*** KERNEL PANIC ***", 0
str_exception   db "Exception ", 0
str_colon       db ": ", 0
str_errcode     db "Error code: ", 0
str_cr2         db "   CR2: ", 0
str_gap2        db "  ", 0
str_halted      db "The system has been halted. Please restart the computer.", 0

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
    dq s_uptime,   cmd_uptime
    dq s_disk,     cmd_disk
    dq s_save,     cmd_save
    dq s_load,     cmd_load
    dq s_hexdump,  cmd_hexdump
    dq s_crash,    cmd_crash
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
s_uptime   db "uptime", 0
s_crash    db "crash", 0
s_disk     db "disk", 0
s_save     db "save", 0
s_load     db "load", 0
s_hexdump  db "hexdump", 0
s_reboot   db "reboot", 0
s_shutdown db "shutdown", 0

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

; mouse pointer: X = black edge, . = white fill, space = transparent
cursor_sprite:
    db "X           "
    db "XX          "
    db "X.X         "
    db "X..X        "
    db "X...X       "
    db "X....X      "
    db "X.....X     "
    db "X......X    "
    db "X.......X   "
    db "X........X  "
    db "X.........X "
    db "X..........X"
    db "X......XXXXX"
    db "X...X..X    "
    db "X..XX..X    "
    db "X.X  X..X   "
    db "XX   X..X   "
    db "X     X..X  "
    db "      XXXX  "
CURSOR_SIZE equ $ - cursor_sprite
    times CURSOR_SIZE - CUR_W * CUR_H db 0     ; assemble error if too short
    times CUR_W * CUR_H - CURSOR_SIZE db 0     ; assemble error if too long

; terminal color themes: foreground, background
themes:
    dd 0xD8D8D8, 0x14141C
    dd 0x33FF66, 0x000000
    dd 0xFFFFFF, 0x1E3A8A
    dd 0xFFB000, 0x1C1200
    dd 0x202020, 0xF2F2F2
THEME_COUNT equ ($ - themes) / 8

banner:
    db "  __  __        ___  ____  ", 10
    db " |  \/  |_   _ / _ \/ ___| ", 10
    db " | |\/| | | | | | | \___ \ ", 10
    db " | |  | | |_| | |_| |___) |", 10
    db " |_|  |_|\__, |\___/|____/ ", 10
    db "         |___/   v0.5  (64-bit, graphical)", 10, 10
    db "Type 'help' to see commands.", 10, 10, 0

prompt         db "myos> ", 0
msg_unknown    db "Unknown command. Type 'help'.", 10, 0
msg_help:
    db "Commands:", 10
    db "  help      show this list", 10
    db "  clear     clear the terminal", 10
    db "  echo X    print X", 10
    db "  time      show date and time", 10
    db "  mem       show the memory map", 10
    db "  cpu       show CPU info and mode", 10
    db "  color     change terminal colors", 10
    db "  about     about this OS", 10
    db "  uptime    time since boot", 10
    db "  disk      show hard disk info", 10
    db "  save X    save text X to the hard disk", 10
    db "  load      read the saved text back", 10
    db "  hexdump N show raw bytes of disk sector N", 10
    db "  crash     test the panic screen ('crash pf' = page fault)", 10
    db "  reboot    restart the computer", 10
    db "  shutdown  power off", 10
    db "Mouse: [x] hides this window, the taskbar button brings it back.", 10, 0
msg_about:
    db "MyOS v0.5 - a tiny 64-bit operating system with a GUI", 10
    db "Written in x86-64 assembly. Own drivers for graphics, font,", 10
    db "keyboard, mouse and clock - no BIOS after boot.", 10, 0
msg_bpp        db ", 32-bit color", 10, 0
msg_date       db "Date: ", 0
msg_time       db "Time: ", 0
msg_mem_head   db "Memory map (from BIOS E820):", 10
               db "  Base                Length              Type", 10, 0
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
msg_color      db "Colors changed. Type 'color' again for the next theme.", 10, 0
msg_no_disk    db "No hard disk found.", 10, 0
msg_disk_model db "Disk 0: ", 0
msg_disk_size  db "Size:   ", 0
msg_disk_mb    db " MB (", 0
msg_disk_sectors db " sectors x 512 bytes)", 10, 0
msg_save_usage db "Usage: save <text>", 10, 0
msg_saved      db "Saved to disk (sector 1). It will still be there after a reboot.", 10, 0
msg_note       db "Saved text: ", 0
msg_no_note    db "Nothing saved yet. Try: save hello", 10, 0
msg_disk_err   db "Disk error!", 10, 0
msg_bad_sector db "Sector number is past the end of the disk.", 10, 0
msg_sector     db "Sector ", 0
msg_first_half db " (first 256 of 512 bytes):", 10, 0
note_sig       db "MYOSNOTE", 0
msg_uptime     db "Uptime: ", 0
msg_h          db "h ", 0
msg_m          db "m ", 0
msg_s          db "s", 10, 0
msg_ticks      db "Timer interrupts so far: ", 0
msg_ticks_tail db " (100 per second)", 10, 0
msg_reboot     db "Rebooting...", 10, 0
msg_shutdown   db "Shutting down...", 10, 0
msg_safe_off   db "It is now safe to turn off your computer.", 10, 0
msg_no_lm      db "This CPU does not support 64-bit mode.", 13, 10, 0
msg_no_video   db "No 32-bit VBE graphics mode (1024x768 or 800x600) found.", 13, 10, 0

str_desk_title db "MyOS v0.5 - 64-bit graphical desktop", 0
str_screen     db "Screen: ", 0
str_x          db " x ", 0
str_desk_tail  db ", 32-bit color", 0
str_logo       db "MyOS", 0
str_term_title db "Terminal", 0

; ---------------- variables ----------------
align 8
fb_addr     dq 0
fb_pitch    dd 0
scr_w       dd 0
scr_h       dd 0
win_x       dd 0
win_y       dd 0
content_x   dd 0
content_y   dd 0
close_x     dd 0
close_y     dd 0
mouse_x     dd 0
mouse_y     dd 0
term_cx     dd 0
term_cy     dd 0
term_fg     dd 0xD8D8D8
term_bg     dd 0x14141C
input_len   dd 0
disk_sectors dd 0
disk_present db 0
disk_model  times 41 db 0
align 8
ticks       dq 0                ; incremented by the timer interrupt
next_clock  dq 0
want_w      dw 0
want_h      dw 0
vbe_mode_num dw 0
mem_count   dw 0
win_visible db 1
theme_index db 0
mouse_idx   db 0
mouse_btn   db 0
mouse_pkt   db 0, 0, 0
kb_shift    db 0
kb_ext      db 0
boot_drive  db 0
last_sec    db 0xFF
clock_str   db "00:00:00", 0
cpu_buf     times 49 db 0
desk_info   times 128 db 0
input_buf   times MAX_INPUT + 1 db 0
line_buf    times 128 db 0
kbq_head    db 0                ; keyboard queue (written by IRQ1)
kbq_tail    db 0
msq_head    db 0                ; mouse queue (written by IRQ12)
msq_tail    db 0
kbq         times 256 db 0
msq         times 256 db 0
term_buf    times TERM_COLS * TERM_ROWS db ' '

times KERNEL_SECTORS * 512 - ($ - $$) db 0
