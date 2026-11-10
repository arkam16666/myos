; ============================================================
;  MyOS - Kernel with a simple command shell (16-bit real mode)
; ============================================================
[org 0x7E00]
[bits 16]

KERNEL_SECTORS equ 16
MAX_INPUT      equ 64

kernel_start:
    mov [boot_drive], dl
    call clear_screen
    mov si, banner
    call print_str

main_loop:
    mov si, prompt
    call print_str
    mov di, input_buf
    call read_line

    mov si, input_buf
    call skip_spaces
    cmp byte [si], 0
    je main_loop
    call lowercase_word

    ; look the command up in the table
    mov bx, cmd_table
.next:
    mov di, [bx]
    test di, di
    jz .unknown
    call match_cmd              ; CF clear = match, SI -> arguments
    jnc .found
    add bx, 4
    jmp .next
.found:
    call [bx + 2]
    jmp main_loop
.unknown:
    mov si, msg_unknown
    call print_str
    jmp main_loop

; ------------------------------------------------------------
;  Commands  (on entry SI points to the argument text)
; ------------------------------------------------------------
cmd_help:
    mov si, msg_help
    call print_str
    ret

cmd_clear:
    call clear_screen
    ret

cmd_echo:
    call print_str
    call newline
    ret

cmd_about:
    mov si, msg_about
    call print_str
    ret

cmd_time:
    mov ah, 0x04                ; RTC date: CH=century CL=year DH=month DL=day
    int 0x1A
    mov si, msg_date
    call print_str
    mov al, ch
    call print_bcd
    mov al, cl
    call print_bcd
    mov al, '-'
    call putc
    mov al, dh
    call print_bcd
    mov al, '-'
    call putc
    mov al, dl
    call print_bcd
    call newline

    mov ah, 0x02                ; RTC time: CH=hour CL=min DH=sec
    int 0x1A
    mov si, msg_time
    call print_str
    mov al, ch
    call print_bcd
    mov al, ':'
    call putc
    mov al, cl
    call print_bcd
    mov al, ':'
    call putc
    mov al, dh
    call print_bcd
    call newline
    ret

cmd_mem:
    int 0x12                    ; AX = conventional memory in KB
    mov si, msg_mem
    call print_str
    call print_dec
    mov si, msg_kb
    call print_str
    ret

cmd_color:
    mov bl, [theme_index]
    inc bl
    cmp bl, THEME_COUNT
    jb .set
    xor bl, bl
.set:
    mov [theme_index], bl
    xor bh, bh
    mov al, [themes + bx]
    mov [text_attr], al
    call clear_screen
    mov si, msg_color
    call print_str
    ret

cmd_reboot:
    mov si, msg_reboot
    call print_str
    mov al, 0xFE                ; pulse CPU reset via keyboard controller
    out 0x64, al
    jmp 0xFFFF:0x0000           ; fallback: jump to BIOS reset vector

cmd_shutdown:
    mov si, msg_shutdown
    call print_str
    mov ax, 0x5301              ; APM: connect real-mode interface
    xor bx, bx
    int 0x15
    mov ax, 0x530E              ; APM: driver version 1.2
    xor bx, bx
    mov cx, 0x0102
    int 0x15
    mov ax, 0x5307              ; APM: set power state = off
    mov bx, 0x0001
    mov cx, 0x0003
    int 0x15
    mov dx, 0x604               ; fallback: QEMU ACPI power-off port
    mov ax, 0x2000
    out dx, ax
    mov si, msg_safe_off        ; still alive: just halt
    call print_str
.halt:
    cli
    hlt
    jmp .halt

; ------------------------------------------------------------
;  Shell helpers
; ------------------------------------------------------------

; read a line into DI (zero-terminated), with backspace support
read_line:
    xor cx, cx
.key:
    xor ah, ah
    int 0x16                    ; wait for key: AL = ASCII
    cmp al, 13
    je .done
    cmp al, 8
    je .back
    cmp al, ' '
    jb .key
    cmp al, '~'
    ja .key
    cmp cx, MAX_INPUT
    jae .key
    stosb
    inc cx
    call putc
    jmp .key
.back:
    jcxz .key
    dec di
    dec cx
    mov al, 8
    call putc
    mov al, ' '
    call putc
    mov al, 8
    call putc
    jmp .key
.done:
    mov byte [di], 0
    call newline
    ret

; advance SI past spaces
skip_spaces:
    cmp byte [si], ' '
    jne .done
    inc si
    jmp skip_spaces
.done:
    ret

; lowercase the first word at SI (in place)
lowercase_word:
    push di
    mov di, si
.loop:
    mov al, [di]
    test al, al
    jz .done
    cmp al, ' '
    je .done
    cmp al, 'A'
    jb .skip
    cmp al, 'Z'
    ja .skip
    add byte [di], 32
.skip:
    inc di
    jmp .loop
.done:
    pop di
    ret

; compare word at SI with command name at DI
; match:    CF=0, SI -> first argument character
; no match: CF=1, SI unchanged
match_cmd:
    push si
.loop:
    mov al, [di]
    test al, al
    jz .end_name
    cmp al, [si]
    jne .fail
    inc si
    inc di
    jmp .loop
.end_name:
    mov al, [si]
    test al, al
    jz .ok
    cmp al, ' '
    je .ok
.fail:
    pop si
    stc
    ret
.ok:
    add sp, 2                   ; drop saved SI
    call skip_spaces
    clc
    ret

; ------------------------------------------------------------
;  Screen output
; ------------------------------------------------------------

; print character in AL
putc:
    push ax
    push bx
    mov ah, 0x0E
    xor bh, bh
    int 0x10
    pop bx
    pop ax
    ret

; print zero-terminated string at SI (newlines are 13,10)
print_str:
    push ax
    push si
.loop:
    lodsb
    test al, al
    jz .done
    call putc
    jmp .loop
.done:
    pop si
    pop ax
    ret

newline:
    push ax
    mov al, 13
    call putc
    mov al, 10
    call putc
    pop ax
    ret

; print AX as unsigned decimal
print_dec:
    push ax
    push bx
    push cx
    push dx
    mov bx, 10
    xor cx, cx
.div:
    xor dx, dx
    div bx
    push dx
    inc cx
    test ax, ax
    jnz .div
.out:
    pop ax
    add al, '0'
    call putc
    loop .out
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; print AL (BCD) as two digits
print_bcd:
    push ax
    push ax
    shr al, 4
    add al, '0'
    call putc
    pop ax
    and al, 0x0F
    add al, '0'
    call putc
    pop ax
    ret

; clear screen using current text_attr, cursor to top-left
clear_screen:
    push ax
    push bx
    push cx
    push dx
    mov ax, 0x0600
    mov bh, [text_attr]
    xor cx, cx
    mov dx, 0x184F              ; row 24, col 79
    int 0x10
    mov ah, 0x02
    xor bh, bh
    xor dx, dx
    int 0x10
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; ------------------------------------------------------------
;  Data
; ------------------------------------------------------------
cmd_table:
    dw s_help,     cmd_help
    dw s_clear,    cmd_clear
    dw s_echo,     cmd_echo
    dw s_time,     cmd_time
    dw s_mem,      cmd_mem
    dw s_color,    cmd_color
    dw s_about,    cmd_about
    dw s_reboot,   cmd_reboot
    dw s_shutdown, cmd_shutdown
    dw 0

s_help     db "help", 0
s_clear    db "clear", 0
s_echo     db "echo", 0
s_time     db "time", 0
s_mem      db "mem", 0
s_color    db "color", 0
s_about    db "about", 0
s_reboot   db "reboot", 0
s_shutdown db "shutdown", 0

banner:
    db "  __  __        ___  ____  ", 13, 10
    db " |  \/  |_   _ / _ \/ ___| ", 13, 10
    db " | |\/| | | | | | | \___ \ ", 13, 10
    db " | |  | | |_| | |_| |___) |", 13, 10
    db " |_|  |_|\__, |\___/|____/ ", 13, 10
    db "         |___/   v0.1", 13, 10, 13, 10
    db "Type 'help' to see commands.", 13, 10, 13, 10, 0

prompt      db "myos> ", 0
msg_unknown db "Unknown command. Type 'help'.", 13, 10, 0
msg_help:
    db "Commands:", 13, 10
    db "  help      show this list", 13, 10
    db "  clear     clear the screen", 13, 10
    db "  echo X    print X", 13, 10
    db "  time      show date and time", 13, 10
    db "  mem       show base memory", 13, 10
    db "  color     change color theme", 13, 10
    db "  about     about this OS", 13, 10
    db "  reboot    restart the computer", 13, 10
    db "  shutdown  power off", 13, 10, 0
msg_about:
    db "MyOS v0.1 - a tiny 16-bit operating system", 13, 10
    db "Written in x86 assembly. Boots from a floppy image.", 13, 10, 0
msg_date     db "Date: ", 0
msg_time     db "Time: ", 0
msg_mem      db "Base memory: ", 0
msg_kb       db " KB", 13, 10, 0
msg_color    db "Theme changed. Type 'color' again for the next one.", 13, 10, 0
msg_reboot   db "Rebooting...", 13, 10, 0
msg_shutdown db "Shutting down...", 13, 10, 0
msg_safe_off db "It is now safe to turn off your computer.", 13, 10, 0

themes      db 0x07, 0x1F, 0x0A, 0x2F, 0x4F, 0x70
THEME_COUNT equ $ - themes
theme_index db 0
text_attr   db 0x07
boot_drive  db 0

input_buf   times MAX_INPUT + 1 db 0

times KERNEL_SECTORS * 512 - ($ - $$) db 0
