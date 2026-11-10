; ============================================================
;  MyOS v1.0 - 64-bit kernel (text mode)
;
;  Boot flow:  16-bit real mode  ->  32-bit protected mode
;                                ->  64-bit long mode
;
;  Drivers (all written here, no BIOS after boot):
;    - VGA text screen          - PS/2 keyboard (IRQ1)
;    - PIT timer (IRQ0)         - CMOS real-time clock
;    - ATA hard disk (PIO)
; ============================================================
[org 0x7E00]
default abs

KERNEL_SECTORS equ 64
MAX_INPUT      equ 74

; ---------------- memory layout ----------------
PML4_ADDR      equ 0x1000       ; page tables (3 x 4 KB)
PDPT_ADDR      equ 0x2000
PD_ADDR        equ 0x3000
MEMMAP_ADDR    equ 0x5000       ; BIOS E820 memory map
MEMMAP_MAX     equ 32
IDT_ADDR       equ 0x70000      ; interrupt descriptor table (4 KB)
DISK_BUF       equ 0x71000      ; one disk sector (512 B)
IDENT_BUF      equ 0x71200      ; ATA IDENTIFY data (512 B)
SB_BUF         equ 0x71400      ; MyFS superblock (512 B)
DIR_BUF        equ 0x72000      ; MyFS directory (4 KB)
PKG_BUF        equ 0x200000     ; a whole package while installing (kernel only)

; ---------------- package (.pkg) header, 128 bytes ----------------
PKG_HDR_SIZE   equ 128          ; "MYPK", u32 format, name[16], version[16],
P_NAME         equ 8            ;   description[64], u32 program size,
P_VERSION      equ 24           ;   then the program itself ("MYEX"...)
P_DESC         equ 40
P_SIZE         equ 104
STACK_TOP      equ 0x90000

; ---------------- MyFS layout ----------------
;  sector 0      superblock: "MYFS0001", total sectors, next free sector
;  sector 1-8    directory: 64 entries x 64 bytes
;  sector 16..   file data (each file = one run of sectors)
DIR_START      equ 1
DIR_SECTORS    equ 8
DATA_START     equ 16
MAX_FILES      equ 64
NAME_MAX       equ 47
SB_TOTAL       equ 8            ; superblock fields
SB_NEXT_FREE   equ 12
E_START        equ 48           ; directory entry fields (name = 0..47)
E_SIZE         equ 52
E_ALLOC        equ 56
E_FLAGS        equ 60           ; 1 = in use

VGA            equ 0xB8000      ; text-mode video memory
COLS           equ 80
ROWS           equ 25
ATTR_PANIC     equ 0x4F         ; white on red

; GDT selectors (this order is required by syscall/sysret)
CODE64_SEL     equ 0x08         ; kernel code
DATA_SEL       equ 0x10         ; kernel data
USER_DS        equ 0x18 | 3     ; user data, ring 3
USER_CS        equ 0x20 | 3     ; user code, ring 3
CODE32_SEL     equ 0x28         ; only used while booting
TSS_SEL        equ 0x30

; ---------------- user programs ----------------
USER_BASE      equ 0x400000     ; programs load here; ring 3 may touch
USER_END       equ 0x600000     ;   only this one 2 MB page
USER_MAX_SIZE  equ 0x100000     ; largest program file: 1 MB
USER_ARGS      equ 0x5FFF00     ; command-line text for the program
USER_STACK     equ 0x5FFF00     ; user stack grows down from here

TIMER_HZ       equ 100
PIT_DIVISOR    equ 1193182 / TIMER_HZ

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
    mov ax, 0x0003              ; 80x25 color text mode
    int 0x10

    mov eax, 0x80000000         ; can this CPU run 64-bit code?
    cpuid
    cmp eax, 0x80000001
    jb no_long_mode
    mov eax, 0x80000001
    cpuid
    test edx, 1 << 29
    jz no_long_mode

    call detect_memory          ; ask BIOS for the RAM map

    in al, 0x92                 ; enable A20 line
    or al, 2
    and al, 0xFE
    out 0x92, al

    cli
    mov al, 0xFF                ; mask PIC until our IDT is ready
    out 0x21, al
    out 0xA1, al

    lgdt [gdt_ptr]
    mov eax, cr0
    or eax, 1                   ; CR0.PE -> protected mode
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

    ; identity-map the first 1 GB with 2 MB pages
    mov edi, PML4_ADDR
    xor eax, eax
    mov ecx, 3 * 4096 / 4
    rep stosd
    mov dword [PML4_ADDR], PDPT_ADDR | 7    ; present + writable + user
    mov dword [PDPT_ADDR], PD_ADDR | 7
    mov edi, PD_ADDR
    mov eax, 0x83               ; present + writable + 2 MB page (kernel only)
    mov ecx, 512
.map:
    mov [edi], eax
    add eax, 0x200000
    add edi, 8
    loop .map
    or dword [PD_ADDR + (USER_BASE >> 21) * 8], 4   ; ring 3 may use this page

    mov eax, PML4_ADDR
    mov cr3, eax
    mov eax, cr4
    or eax, 1 << 5              ; CR4.PAE
    mov cr4, eax
    mov ecx, 0xC0000080         ; EFER
    rdmsr
    or eax, (1 << 8) | 1        ; EFER.LME (long mode) + EFER.SCE (syscall)
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

    mov rax, cr0                ; allow floating point / SSE instructions
    and rax, ~(1 << 2)          ; CR0.EM = 0: real FPU, no emulation
    or rax, 1 << 1              ; CR0.MP = 1
    mov cr0, rax
    mov rax, cr4
    or rax, (1 << 9) | (1 << 10) ; CR4.OSFXSR + OSXMMEXCPT: SSE on
    mov cr4, rax
    fninit

    call clear_screen
    call ata_init               ; look for a hard disk
    call idt_init               ; exception + hardware interrupt handlers
    call tss_init               ; kernel stack for interrupts from ring 3
    call syscall_init           ; where the 'syscall' instruction goes
    call pic_init               ; route IRQs to vectors 32-47
    call pit_init               ; timer: TIMER_HZ interrupts per second
    call kbd_flush
    sti                         ; interrupts ON
    mov rsi, banner
    call print_str
    call fs_mount               ; read the file system, if there is one
    call print_fs_status
    mov rsi, prompt
    call print_str

; main loop: sleep until an interrupt, then handle queued keys
event_loop:
    cli
    mov al, [kbq_head]
    cmp al, [kbq_tail]
    jne .work
    sti                         ; sti + hlt: no wake-up can be lost
    hlt
    jmp event_loop
.work:
    sti
.keys:
    movzx ebx, byte [kbq_tail]
    cmp bl, [kbq_head]
    je event_loop
    mov al, [kbq + rbx]
    inc byte [kbq_tail]
    call kb_decode
    test al, al
    jz .keys
    call on_key
    jmp .keys

; ------------------------------------------------------------
;  Shell
; ------------------------------------------------------------

; AL = ASCII key
on_key:
    PUSHALL
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
    cmp byte [fs_ready], 0      ; not built in: a program on the disk?
    je .not_found
    call get_name               ; name_buf = program name, RSI -> arguments
    jc .not_found
    call find_file
    jc .not_found
    jmp run_program
.not_found:
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

; next color theme, recolor the whole screen without clearing it
cmd_color:
    movzx ebx, byte [theme_index]
    inc ebx
    cmp ebx, THEME_COUNT
    jb .set
    xor ebx, ebx
.set:
    mov [theme_index], bl
    mov al, [themes + rbx]
    mov [text_attr], al
    mov rdi, VGA + 1            ; attribute bytes are the odd ones
    mov ecx, COLS * ROWS
.recolor:
    mov [rdi], al
    add rdi, 2
    loop .recolor
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

; ------------------------------------------------------------
;  File commands (MyFS)
; ------------------------------------------------------------
cmd_ls:
    call fs_check
    jc .done
    mov rbx, DIR_BUF
    mov ecx, MAX_FILES
    xor r12d, r12d              ; file count
.entry:
    test dword [rbx + E_FLAGS], 1
    jz .next
    inc r12d
    mov rsi, msg_gap
    call print_str
    mov rsi, rbx                ; name
    call print_str
.pad:
    cmp byte [cur_x], 36
    jae .size
    mov al, ' '
    call putc
    jmp .pad
.size:
    mov eax, [rbx + E_SIZE]
    call print_dec
    mov rsi, msg_bytes
    call print_str
.next:
    add rbx, 64
    dec ecx
    jnz .entry
    test r12d, r12d
    jnz .footer
    mov rsi, msg_no_files
    call print_str
.footer:
    mov eax, r12d
    call print_dec
    mov rsi, msg_files_free
    call print_str
    call free_kb
    call print_dec
    mov rsi, msg_kb_free
    call print_str
.done:
    ret

cmd_cat:
    call fs_check
    jc .done
    call get_name
    jc .usage
    call find_file
    jc file_not_found
    mov r12d, [rbx + E_START]   ; current sector
    mov r13d, [rbx + E_SIZE]    ; bytes left
.sector:
    test r13d, r13d
    jz .end
    mov eax, r12d
    mov rdi, DISK_BUF
    call ata_read
    jc disk_error
    mov ecx, 512
    cmp r13d, ecx
    jae .count_ok
    mov ecx, r13d
.count_ok:
    sub r13d, ecx
    mov rsi, DISK_BUF
.byte:
    lodsb
    cmp al, 13                  ; skip CR (Windows line endings)
    je .skip
    cmp al, 10
    je .put
    cmp al, 9                   ; tab -> space
    jne .check
    mov al, ' '
.check:
    cmp al, ' '
    jb .dot
    cmp al, '~'
    jbe .put
.dot:
    mov al, '.'
.put:
    call putc
.skip:
    dec ecx
    jnz .byte
    inc r12d
    jmp .sector
.end:
    cmp byte [cur_x], 0         ; finish the line if the file didn't
    je .done
    call newline
.done:
    ret
.usage:
    mov rsi, msg_cat_usage
    jmp print_str

cmd_write:
    call fs_check
    jc .done
    call get_name
    jc .usage
    mov r12, rsi                ; the text
    mov rdi, DISK_BUF           ; file content = text + newline
    xor eax, eax
    mov ecx, 512 / 8
    rep stosq
    mov rdi, DISK_BUF
    mov rsi, r12
    call str_copy
    mov byte [rdi], 10
    inc rdi
    mov rcx, rdi
    sub rcx, DISK_BUF           ; ECX = size
    mov r13d, ecx
    mov rsi, DISK_BUF
    call fs_write_file
    jc .done
    mov rsi, msg_wrote
    call print_str
    mov eax, r13d
    call print_dec
    mov rsi, msg_bytes_to
    call print_str
    mov rsi, name_buf
    call print_str
    call newline
.done:
    ret
.usage:
    mov rsi, msg_write_usage
    jmp print_str

cmd_rm:
    call fs_check
    jc .done
    call get_name
    jc .usage
    call find_file
    jc file_not_found
    mov dword [rbx + E_FLAGS], 0
    call fs_flush
    jc disk_error
    mov rsi, msg_removed
    call print_str
    mov rsi, name_buf
    call print_str
    call newline
.done:
    ret
.usage:
    mov rsi, msg_rm_usage
    jmp print_str

cmd_format:
    cmp byte [disk_present], 0
    jne .have_disk
    mov rsi, msg_no_disk
    jmp print_str
.have_disk:
    mov rdi, s_yes              ; must type "format yes"
    call str_eq
    je .go
    mov rsi, msg_format_warn
    jmp print_str
.go:
    mov rdi, SB_BUF             ; empty superblock + directory
    xor eax, eax
    mov ecx, 512 / 8
    rep stosq
    mov rdi, DIR_BUF
    mov ecx, DIR_SECTORS * 512 / 8
    rep stosq
    mov rax, [fs_magic]
    mov [SB_BUF], rax
    mov eax, [disk_sectors]
    mov [SB_BUF + SB_TOTAL], eax
    mov dword [SB_BUF + SB_NEXT_FREE], DATA_START
    call fs_flush
    jc disk_error
    mov byte [fs_ready], 1
    mov rsi, msg_formatted
    call print_str
    call free_kb
    call print_dec
    mov rsi, msg_kb_free
    jmp print_str

file_not_found:
    mov rsi, msg_not_found
    call print_str
    mov rsi, name_buf
    call print_str
    jmp newline

; ------------------------------------------------------------
;  MyFS internals
; ------------------------------------------------------------

; read superblock + directory into memory, set fs_ready
fs_mount:
    PUSHALL
    mov byte [fs_ready], 0
    cmp byte [disk_present], 0
    je .done
    xor eax, eax
    mov rdi, SB_BUF
    call ata_read
    jc .done
    mov rax, [SB_BUF]
    cmp rax, [fs_magic]
    jne .done
    mov eax, DIR_START
    mov rdi, DIR_BUF
    mov ecx, DIR_SECTORS
.read:
    call ata_read
    jc .done
    inc eax
    add rdi, 512
    loop .read
    mov byte [fs_ready], 1
.done:
    POPALL
    ret

; write superblock + directory back to the disk. CF=1 on error
fs_flush:
    push rax
    push rcx
    push rsi
    xor eax, eax
    mov rsi, SB_BUF
    call ata_write
    jc .out
    mov eax, DIR_START
    mov rsi, DIR_BUF
    mov ecx, DIR_SECTORS
.write:
    call ata_write
    jc .out
    inc eax
    add rsi, 512
    loop .write
    clc
.out:
    pop rsi
    pop rcx
    pop rax
    ret

; CF=1 (and a message) if there is no file system
fs_check:
    cmp byte [fs_ready], 0
    jne .ok
    push rsi
    mov rsi, msg_no_fs
    call print_str
    pop rsi
    stc
    ret
.ok:
    clc
    ret

; copy the next word at RSI into name_buf, RSI -> rest of the line
; CF=1 if there is no name or it is too long
get_name:
    push rax
    push rcx
    xor ecx, ecx
.loop:
    mov al, [rsi]
    test al, al
    jz .end
    cmp al, ' '
    je .end
    cmp ecx, NAME_MAX
    jae .fail
    mov [name_buf + rcx], al
    inc ecx
    inc rsi
    jmp .loop
.end:
    mov byte [name_buf + rcx], 0
    test ecx, ecx
    jz .fail
    call skip_spaces
    pop rcx
    pop rax
    clc
    ret
.fail:
    pop rcx
    pop rax
    stc
    ret

; look up name_buf -> RBX = directory entry. CF=1 if not found
find_file:
    push rcx
    push rsi
    push rdi
    mov rbx, DIR_BUF
    mov ecx, MAX_FILES
.loop:
    test dword [rbx + E_FLAGS], 1
    jz .next
    mov rsi, name_buf
    mov rdi, rbx
    call str_eq
    je .found
.next:
    add rbx, 64
    dec ecx
    jnz .loop
    pop rdi
    pop rsi
    pop rcx
    stc
    ret
.found:
    pop rdi
    pop rsi
    pop rcx
    clc
    ret

; RBX = first unused directory entry. CF=1 if the directory is full
find_free_slot:
    push rcx
    mov rbx, DIR_BUF
    mov ecx, MAX_FILES
.loop:
    test dword [rbx + E_FLAGS], 1
    jz .found
    add rbx, 64
    dec ecx
    jnz .loop
    pop rcx
    stc
    ret
.found:
    pop rcx
    clc
    ret

; create or replace file name_buf with ECX bytes from RSI
; CF=1 on error (message already printed)
fs_write_file:
    mov r8, rsi                 ; data
    mov r9d, ecx                ; size in bytes
    lea r10d, [rcx + 511]
    shr r10d, 9                 ; sectors needed
    call find_file
    jnc .existing
    call find_free_slot
    jc .dir_full
    mov rdi, rbx                ; fresh entry with the name
    xor eax, eax
    mov ecx, 64 / 8
    rep stosq
    mov rsi, name_buf
    mov rdi, rbx
    call str_copy
    jmp .allocate
.existing:
    cmp r10d, [rbx + E_ALLOC]   ; still fits in its old space?
    jbe .write
.allocate:
    mov eax, [SB_BUF + SB_NEXT_FREE]
    mov edx, eax
    add edx, r10d
    cmp edx, [SB_BUF + SB_TOTAL]
    ja .disk_full
    mov [SB_BUF + SB_NEXT_FREE], edx
    mov [rbx + E_START], eax
    mov [rbx + E_ALLOC], r10d
.write:
    mov [rbx + E_SIZE], r9d
    mov dword [rbx + E_FLAGS], 1
    mov eax, [rbx + E_START]
    mov rsi, r8
    mov ecx, r10d
.sector:
    test ecx, ecx
    jz .flush
    call ata_write
    jc .io_error
    inc eax
    add rsi, 512
    dec ecx
    jmp .sector
.flush:
    call fs_flush
    jc .io_error
    clc
    ret
.dir_full:
    mov rsi, msg_dir_full
    jmp .fail
.disk_full:
    mov rsi, msg_disk_full
    jmp .fail
.io_error:
    mov rsi, msg_disk_err
.fail:
    call print_str
    stc
    ret

; RAX = free space in KB
free_kb:
    mov eax, [SB_BUF + SB_TOTAL]
    sub eax, [SB_BUF + SB_NEXT_FREE]
    shr eax, 1                  ; 2 sectors = 1 KB
    ret

; EAX = number of files
count_files:
    push rbx
    push rcx
    xor eax, eax
    mov rbx, DIR_BUF
    mov ecx, MAX_FILES
.loop:
    test dword [rbx + E_FLAGS], 1
    jz .next
    inc eax
.next:
    add rbx, 64
    dec ecx
    jnz .loop
    pop rcx
    pop rbx
    ret

; one line at boot telling what is on the disk
print_fs_status:
    cmp byte [disk_present], 0
    jne .disk
    mov rsi, msg_no_disk
    jmp print_str
.disk:
    cmp byte [fs_ready], 0
    jne .mounted
    mov rsi, msg_boot_nofs
    jmp print_str
.mounted:
    mov rsi, msg_boot_fs
    call print_str
    call count_files
    call print_dec
    mov rsi, msg_boot_fs_tail
    jmp print_str

; ------------------------------------------------------------
;  Package manager
;    package  = file NAME.pkg on the disk (the "repository")
;    installed = a program file NAME exists
; ------------------------------------------------------------
cmd_pkg:
    call fs_check
    jc .done
    call lowercase_word
    mov rbx, pkg_table
.next:
    mov rdi, [rbx]
    test rdi, rdi
    jz .usage
    call match_cmd
    jnc .found
    add rbx, 16
    jmp .next
.found:
    jmp [rbx + 8]
.usage:
    mov rsi, msg_pkg_usage
    jmp print_str
.done:
    ret

; pkg list: every NAME.pkg on the disk, and whether it is installed
pkg_list:
    mov rsi, msg_pkg_head
    call print_str
    mov r12, DIR_BUF
    mov r13d, MAX_FILES
    xor r14d, r14d              ; packages found
.entry:
    test dword [r12 + E_FLAGS], 1
    jz .next
    mov rsi, r12
    call ends_with_pkg
    jne .next
    mov rbx, r12
    call read_pkg_header
    jc .next
    inc r14d
    mov rsi, msg_gap
    call print_str
    mov rsi, DISK_BUF + P_NAME
    call print_str
    mov al, 16
    call pad_to
    mov rsi, DISK_BUF + P_VERSION
    call print_str
    mov al, 25
    call pad_to
    mov rsi, msg_available
    call pkg_installed
    jc .status
    mov rsi, msg_installed
.status:
    call print_str
    mov al, 37
    call pad_to
    mov rsi, DISK_BUF + P_DESC
    call print_str
    call newline
.next:
    add r12, 64
    dec r13d
    jnz .entry
    test r14d, r14d
    jnz .done
    mov rsi, msg_no_pkgs
    call print_str
.done:
    ret

; pkg info NAME
pkg_info:
    call find_pkg
    jc .done
    call read_pkg_header
    jc bad_pkg
    mov rsi, msg_i_name
    call print_str
    mov rsi, DISK_BUF + P_NAME
    call print_str
    mov rsi, msg_i_version
    call print_str
    mov rsi, DISK_BUF + P_VERSION
    call print_str
    mov rsi, msg_i_desc
    call print_str
    mov rsi, DISK_BUF + P_DESC
    call print_str
    mov rsi, msg_i_size
    call print_str
    mov eax, [DISK_BUF + P_SIZE]
    call print_dec
    mov rsi, msg_i_installed
    call print_str
    mov rsi, msg_no
    call pkg_installed
    jc .answer
    mov rsi, msg_yes
.answer:
    call print_str
.done:
    ret

; pkg install NAME: unpack NAME.pkg into the program file NAME
pkg_install:
    call find_pkg
    jc .done
    mov r12d, [rbx + E_SIZE]    ; load the whole package
    cmp r12d, PKG_HDR_SIZE
    jb bad_pkg
    cmp r12d, USER_MAX_SIZE + PKG_HDR_SIZE
    ja bad_pkg
    mov eax, [rbx + E_START]
    mov rdi, PKG_BUF
    lea ecx, [r12 + 511]
    shr ecx, 9
.load:
    call ata_read
    jc disk_error
    inc eax
    add rdi, 512
    loop .load
    cmp dword [PKG_BUF], 'MYPK' ; check everything before writing
    jne bad_pkg
    mov r13d, [PKG_BUF + P_SIZE]
    cmp r13d, 16
    jb bad_pkg
    cmp r13d, USER_MAX_SIZE
    ja bad_pkg
    lea eax, [r13 + PKG_HDR_SIZE]
    cmp eax, r12d
    ja bad_pkg
    cmp dword [PKG_BUF + PKG_HDR_SIZE], 'MYEX'
    jne bad_pkg
    mov byte [PKG_BUF + P_NAME + 15], 0
    mov byte [PKG_BUF + P_VERSION + 15], 0
    mov rsi, PKG_BUF + P_NAME
    mov rdi, name_buf
    call str_copy
    call valid_prog_name        ; e.g. no "readme.txt" overwrites
    jc bad_pkg
    mov rsi, msg_installing
    call print_str
    mov rsi, name_buf
    call print_str
    mov al, ' '
    call putc
    mov rsi, PKG_BUF + P_VERSION
    call print_str
    mov rsi, msg_dots
    call print_str
    mov rsi, PKG_BUF + PKG_HDR_SIZE
    mov ecx, r13d
    call fs_write_file
    jc .done
    mov rsi, msg_installed_run
    call print_str
    mov rsi, name_buf
    call print_str
    mov rsi, msg_to_run
    call print_str
.done:
    ret

; pkg remove NAME: delete the installed program (the package stays)
pkg_remove:
    call get_name
    jc .usage
    call valid_prog_name
    jc .usage
    call find_file
    jc .missing
    mov dword [rbx + E_FLAGS], 0
    call fs_flush
    jc disk_error
    mov rsi, msg_removed
    call print_str
    mov rsi, name_buf
    call print_str
    jmp newline
.missing:
    mov rsi, name_buf
    call print_str
    mov rsi, msg_not_installed
    jmp print_str
.usage:
    mov rsi, msg_pkg_usage
    jmp print_str

bad_pkg:
    mov rsi, msg_bad_pkg
    jmp print_str

; RSI = argument -> RBX = directory entry of NAME.pkg
; CF=1 (message already printed) if there is no such package
find_pkg:
    call get_name
    jc .usage
    call valid_prog_name
    jc .usage
    mov rdi, name_buf           ; NAME -> NAME.pkg
.end:
    cmp byte [rdi], 0
    je .append
    inc rdi
    jmp .end
.append:
    mov dword [rdi], '.pkg'
    mov byte [rdi + 4], 0
    call find_file
    jc .missing
    ret
.missing:
    mov rsi, msg_no_pkg
    call print_str
    mov rsi, name_buf
    call print_str
    mov rsi, msg_try_list
    call print_str
    stc
    ret
.usage:
    mov rsi, msg_pkg_usage
    call print_str
    stc
    ret

; read the 128-byte header of package entry RBX into DISK_BUF
; CF=1 if it is not a valid package
read_pkg_header:
    push rax
    push rdi
    cmp dword [rbx + E_SIZE], PKG_HDR_SIZE
    jb .bad
    mov eax, [rbx + E_START]
    mov rdi, DISK_BUF
    call ata_read
    jc .bad
    cmp dword [DISK_BUF], 'MYPK'
    jne .bad
    mov byte [DISK_BUF + P_NAME + 15], 0      ; make sure strings end
    mov byte [DISK_BUF + P_VERSION + 15], 0
    mov byte [DISK_BUF + P_DESC + 63], 0
    pop rdi
    pop rax
    clc
    ret
.bad:
    pop rdi
    pop rax
    stc
    ret

; is the program named in DISK_BUF's header installed? CF=0 = yes
pkg_installed:
    push rbx
    push rsi
    push rdi
    mov rsi, DISK_BUF + P_NAME
    mov rdi, name_buf
    call str_copy
    call find_file
    pop rdi
    pop rsi
    pop rbx
    ret

; program names: 1-15 characters of a-z 0-9 - _   (CF=1 if not)
valid_prog_name:
    push rax
    push rcx
    xor ecx, ecx
.loop:
    mov al, [name_buf + rcx]
    test al, al
    jz .end
    cmp ecx, 15
    jae .bad
    cmp al, 'a'
    jb .not_lower
    cmp al, 'z'
    jbe .ok
.not_lower:
    cmp al, '0'
    jb .other
    cmp al, '9'
    jbe .ok
.other:
    cmp al, '-'
    je .ok
    cmp al, '_'
    jne .bad
.ok:
    inc ecx
    jmp .loop
.end:
    test ecx, ecx
    jz .bad
    pop rcx
    pop rax
    clc
    ret
.bad:
    pop rcx
    pop rax
    stc
    ret

; ZF=1 if the string at RSI ends with ".pkg"
ends_with_pkg:
    push rax
    push rcx
    mov rcx, rsi
.len:
    cmp byte [rcx], 0
    je .got
    inc rcx
    jmp .len
.got:
    sub rcx, rsi
    cmp rcx, 5
    jb .no
    cmp dword [rsi + rcx - 4], '.pkg'
    jmp .out
.no:
    or eax, 1                   ; ZF=0
.out:
    pop rcx
    pop rax
    ret

; print spaces until the cursor reaches column AL
pad_to:
    push rax
    mov ah, al
.loop:
    cmp [cur_x], ah
    jae .done
    mov al, ' '
    call putc
    jmp .loop
.done:
    pop rax
    ret

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
    cli
.wait:
    in al, 0x64
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
    mov al, 0xFC                ; unmask IRQ0 timer + IRQ1 keyboard
    out 0x21, al
    mov al, 0xFF                ; slave: everything masked
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

; throw away anything waiting in the keyboard controller
kbd_flush:
    push rax
.loop:
    in al, 0x64
    test al, 1
    jz .done
    in al, 0x60
    jmp .loop
.done:
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

; IRQ1: a byte from the keyboard -> queue it
irq_keyboard:
    in al, 0x64
    test al, 1
    jz .done
    mov bl, al
    in al, 0x60
    test bl, 0x20               ; mouse byte? not ours
    jnz .done
    movzx ecx, byte [kbq_head]
    mov [kbq + rcx], al
    inc byte [kbq_head]
.done:
    ret

; CPU exception: red screen with the register dump, then halt
exception_handler:
    test byte [rsp + 18 * 8], 3 ; saved CS: did it happen in ring 3?
    jnz user_crash              ; yes -> only the program dies
    cli
    mov rbp, rsp                ; RBP -> saved registers
    mov byte [text_attr], ATTR_PANIC
    call clear_screen

    mov rsi, str_panic_title
    call panic_line
    call newline

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
    call panic_line

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
    call panic_line
    call newline

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
    call panic_line
    cmp qword [r15], 0
    jne .reg_line

    call newline
    mov rsi, str_halted
    call panic_line
.halt:
    cli
    hlt
    jmp .halt

panic_line:                     ; "  " + RSI + newline
    push rsi
    mov rsi, msg_gap
    call print_str
    pop rsi
    call print_str
    jmp newline

; ------------------------------------------------------------
;  User programs: loader, ring 3, system calls
; ------------------------------------------------------------

; put the TSS descriptor into the GDT and load it
; (the TSS tells the CPU which stack to use when ring 3 is interrupted)
tss_init:
    push rax
    mov rax, tss
    mov word [gdt_tss], TSS_SIZE - 1    ; limit
    mov [gdt_tss + 2], ax               ; base 0-15
    shr rax, 16
    mov [gdt_tss + 4], al               ; base 16-23
    mov byte [gdt_tss + 5], 0x89        ; present, 64-bit TSS
    mov byte [gdt_tss + 6], 0
    mov [gdt_tss + 7], ah               ; base 24-31
    shr rax, 16
    mov [gdt_tss + 8], eax              ; base 32-63
    mov dword [gdt_tss + 12], 0
    mov ax, TSS_SEL
    ltr ax
    pop rax
    ret

; tell the CPU where 'syscall' jumps and which segments to use
syscall_init:
    push rax
    push rcx
    push rdx
    mov ecx, 0xC0000081         ; STAR: syscall CS=0x08, sysret base=0x10
    xor eax, eax
    mov edx, (0x10 << 16) | CODE64_SEL
    wrmsr
    mov ecx, 0xC0000082         ; LSTAR: syscall entry point
    mov rax, syscall_entry
    mov rdx, rax
    shr rdx, 32
    wrmsr
    mov ecx, 0xC0000084         ; FMASK: clear IF + DF when entering
    mov eax, 0x600
    xor edx, edx
    wrmsr
    pop rdx
    pop rcx
    pop rax
    ret

; run the program in directory entry RBX (name in name_buf),
; RSI = its arguments. Returns when the program exits or crashes.
run_program:
    mov r13d, [rbx + E_SIZE]
    cmp r13d, 16
    jb .not_program
    cmp r13d, USER_MAX_SIZE
    ja .too_big
    mov r14, rsi
    mov rdi, USER_BASE          ; wipe the user area
    xor eax, eax
    mov ecx, (USER_END - USER_BASE) / 8
    rep stosq
    mov rsi, r14                ; arguments -> user memory
    mov rdi, USER_ARGS
    call str_copy
    mov eax, [rbx + E_START]    ; load the file from the disk
    mov rdi, USER_BASE
    lea ecx, [r13 + 511]
    shr ecx, 9
.load:
    call ata_read
    jc disk_error
    inc eax
    add rdi, 512
    loop .load
    cmp dword [USER_BASE], 'MYEX'   ; header check
    jne .not_program
    mov r15, [USER_BASE + 8]    ; entry point must be inside the file
    cmp r15, USER_BASE + 16
    jb .not_program
    mov eax, r13d
    add rax, USER_BASE
    cmp r15, rax
    jae .not_program

    ; drop to ring 3: build an interrupt-return frame and iretq into it
    cli
    mov [saved_rsp], rsp        ; [rsp] = our return address
    mov [tss + 4], rsp          ; TSS.RSP0 = kernel stack while in ring 3
    push USER_DS                ; SS
    push USER_STACK             ; RSP
    push 0x202                  ; RFLAGS: interrupts on
    push USER_CS                ; CS  (ring 3)
    push r15                    ; RIP (program entry)
    mov rdi, USER_ARGS          ; first argument: the command line
    xor eax, eax                ; don't leak kernel values
    xor ebx, ebx
    xor ecx, ecx
    xor edx, edx
    xor esi, esi
    xor ebp, ebp
    xor r8d, r8d
    xor r9d, r9d
    xor r10d, r10d
    xor r11d, r11d
    xor r12d, r12d
    xor r13d, r13d
    xor r14d, r14d
    xor r15d, r15d
    iretq
.not_program:
    mov rsi, name_buf
    call print_str
    mov rsi, msg_not_program
    jmp print_str
.too_big:
    mov rsi, msg_too_big
    jmp print_str

; back to the shell: drop the program's stack, return from run_program
program_exit:
    cli
    mov ax, DATA_SEL
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov rsp, [saved_rsp]
    sti
    cmp byte [cur_x], 0
    je .done
    call newline
.done:
    ret

; a ring-3 program caused a CPU exception: report it, kill the program
user_crash:
    mov rbp, rsp
    cmp byte [cur_x], 0
    je .report
    call newline
.report:
    mov rsi, msg_crashed
    call print_str
    mov rax, [rbp + 15 * 8]
    mov rsi, [exc_names + rax * 8]
    call print_str
    mov rsi, msg_at_rip
    call print_str
    mov rax, [rbp + 17 * 8]
    call print_hex64
    cmp qword [rbp + 15 * 8], 14
    jne .end
    mov rsi, msg_fault_addr     ; page fault: which address?
    call print_str
    mov rax, cr2
    call print_hex64
.end:
    call newline
    jmp program_exit

; 'syscall' from ring 3 lands here. RAX = service, RDI/RSI = arguments.
; The CPU put the user RIP in RCX and RFLAGS in R11; RSP is still the
; user's stack, so switch stacks first.
syscall_entry:
    mov [user_rsp], rsp
    mov rsp, [tss + 4]          ; kernel stack
    push qword [user_rsp]
    push rcx
    push r11
    push rbx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r12
    push r13
    push r14
    push r15
    sti
    cmp rax, SYSCALL_COUNT
    jae .bad
    call [syscall_table + rax * 8]
    jmp .back
.bad:
    mov rax, -1
.back:
    cli
    pop r15
    pop r14
    pop r13
    pop r12
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rbx
    pop r11
    pop rcx
    pop rsp                     ; user stack again
    o64 sysret                  ; back to ring 3

; ---- system call services ----

sys_exit:                       ; 0: RDI = exit code
    test rdi, rdi
    jz program_exit
    mov r12, rdi
    cmp byte [cur_x], 0
    je .msg
    call newline
.msg:
    mov rsi, msg_exit_code
    call print_str
    mov rax, r12
    call print_dec
    call newline
    jmp program_exit

sys_print:                      ; 1: RDI = zero-terminated string
    mov rsi, rdi
.loop:
    cmp rsi, USER_BASE          ; only read the program's own memory
    jb .done
    cmp rsi, USER_END
    jae .done
    mov al, [rsi]
    test al, al
    jz .done
    call putc
    inc rsi
    jmp .loop
.done:
    xor eax, eax
    ret

sys_putc:                       ; 2: RDI = character
    mov eax, edi
    call putc
    xor eax, eax
    ret

sys_getkey:                     ; 3: wait for a key -> RAX = ASCII
.wait:
    cli
    movzx ebx, byte [kbq_tail]
    cmp bl, [kbq_head]
    jne .have
    sti
    hlt                         ; sleep until an interrupt
    jmp .wait
.have:
    sti
    mov al, [kbq + rbx]
    inc byte [kbq_tail]
    call kb_decode
    test al, al
    jz .wait
    movzx eax, al
    ret

sys_readline:                   ; 4: RDI = buffer, RSI = size -> RAX = length
    cmp rdi, USER_BASE          ; buffer must be inside program memory
    jb .bad
    test rsi, rsi
    jz .bad
    cmp rsi, USER_END - USER_BASE
    ja .bad
    lea rax, [rdi + rsi]
    cmp rax, USER_END
    ja .bad
    mov r8, rdi
    lea r9, [rsi - 1]           ; leave room for the terminator
    xor ecx, ecx
.key:
    call sys_getkey
    cmp al, 10
    je .enter
    cmp al, 8
    je .back
    cmp al, ' '
    jb .key
    cmp al, '~'
    ja .key
    cmp rcx, r9
    jae .key
    mov [r8 + rcx], al
    inc rcx
    call putc
    jmp .key
.back:
    test rcx, rcx
    jz .key
    dec rcx
    call putc
    jmp .key
.enter:
    mov byte [r8 + rcx], 0
    call newline
    mov rax, rcx
    ret
.bad:
    mov rax, -1
    ret

sys_printnum:                   ; 5: RDI = unsigned number
    mov rax, rdi
    call print_dec
    xor eax, eax
    ret

sys_ticks:                      ; 6: -> RAX = timer ticks since boot (100/s)
    mov rax, [ticks]
    ret

sys_sleep:                      ; 7: RDI = ticks to wait (100 = 1 second)
    mov rbx, [ticks]
    add rbx, rdi
.wait:
    cmp [ticks], rbx
    jae .done
    hlt
    jmp .wait
.done:
    xor eax, eax
    ret

; ---- file system calls (MyFS). All return -1 on any error. ----

sys_file_size:                  ; 8: RDI = name -> RAX = size in bytes
    cmp byte [fs_ready], 0
    je sys_fail
    call user_name
    jc sys_fail
    call find_file
    jc sys_fail
    mov eax, [rbx + E_SIZE]
    ret

sys_read_file:                  ; 9: RDI = name, RSI = buffer, RDX = max bytes
    cmp byte [fs_ready], 0      ;    -> RAX = bytes read
    je sys_fail
    call user_name
    jc sys_fail
    call user_buffer
    jc sys_fail
    call find_file
    jc sys_fail
    mov r8d, [rbx + E_SIZE]     ; R8 = bytes to copy = min(size, max)
    cmp r8, rdx
    jbe .count_ok
    mov r8, rdx
.count_ok:
    mov r9, r8                  ; R9 = bytes left
    mov rdi, rsi                ; RDI = destination in program memory
    mov eax, [rbx + E_START]
.sector:
    test r9, r9
    jz .done
    push rdi
    mov rdi, DISK_BUF
    call ata_read
    pop rdi
    jc sys_fail
    mov ecx, 512
    cmp r9, rcx
    jae .copy
    mov rcx, r9
.copy:
    sub r9, rcx
    mov rsi, DISK_BUF
    rep movsb
    inc eax
    jmp .sector
.done:
    mov rax, r8
    ret

sys_write_file:                 ; 10: RDI = name, RSI = data, RDX = size
    cmp byte [fs_ready], 0      ;     creates or replaces the file -> RAX = 0
    je sys_fail
    call user_name
    jc sys_fail
    call user_buffer
    jc sys_fail
    cmp rdx, USER_MAX_SIZE
    ja sys_fail
    mov rdi, PKG_BUF            ; copy into a kernel buffer padded with
    mov rcx, rdx                ;   zeros to whole sectors, so nothing past
    add rcx, 511                ;   the program's data reaches the disk
    and rcx, ~511
    push rcx
    push rdi
    xor eax, eax
    rep stosb
    pop rdi
    pop rcx
    mov rcx, rdx
    rep movsb
    mov rsi, PKG_BUF
    mov ecx, edx
    call fs_write_file
    jc sys_fail
    xor eax, eax
    ret

sys_delete_file:                ; 11: RDI = name -> RAX = 0
    cmp byte [fs_ready], 0
    je sys_fail
    call user_name
    jc sys_fail
    call find_file
    jc sys_fail
    mov dword [rbx + E_FLAGS], 0
    call fs_flush
    jc sys_fail
    xor eax, eax
    ret

sys_list_files:                 ; 12: RDI = index (0, 1, 2...), RSI = 48-byte
    cmp byte [fs_ready], 0      ;     buffer for the name -> RAX = file size,
    je sys_fail                 ;     or -1 when there are no more files
    mov edx, NAME_MAX + 1
    call user_buffer
    jc sys_fail
    mov rbx, DIR_BUF
    mov ecx, MAX_FILES
.entry:
    test dword [rbx + E_FLAGS], 1
    jz .next
    test rdi, rdi
    jz .found
    dec rdi
.next:
    add rbx, 64
    dec ecx
    jnz .entry
    jmp sys_fail
.found:
    mov rdi, rsi
    mov rsi, rbx
    call str_copy
    mov eax, [rbx + E_SIZE]
    ret

sys_fail:
    mov rax, -1
    ret

; copy the file name at RDI (program memory) into name_buf. CF=1 if bad
user_name:
    push rax
    push rcx
    push rsi
    mov rsi, rdi
    xor ecx, ecx
.loop:
    cmp rsi, USER_BASE
    jb .bad
    cmp rsi, USER_END
    jae .bad
    mov al, [rsi]
    test al, al
    jz .end
    cmp al, ' '                 ; no spaces or control characters
    jbe .bad
    cmp al, '~'
    ja .bad
    cmp ecx, NAME_MAX
    jae .bad
    mov [name_buf + rcx], al
    inc ecx
    inc rsi
    jmp .loop
.end:
    test ecx, ecx
    jz .bad
    mov byte [name_buf + rcx], 0
    pop rsi
    pop rcx
    pop rax
    clc
    ret
.bad:
    pop rsi
    pop rcx
    pop rax
    stc
    ret

; CF=1 unless RSI..RSI+RDX lies inside program memory
user_buffer:
    push rax
    cmp rsi, USER_BASE
    jb .bad
    mov rax, USER_END - USER_BASE
    cmp rdx, rax
    ja .bad
    lea rax, [rsi + rdx]
    cmp rax, USER_END
    ja .bad
    pop rax
    clc
    ret
.bad:
    pop rax
    stc
    ret

; ------------------------------------------------------------
;  Driver: VGA text screen (writes directly to 0xB8000)
; ------------------------------------------------------------

; print character in AL (10 = newline, 8 = backspace)
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
    mov ecx, COLS * (ROWS - 1)
    rep movsw
    mov al, ' '
    mov ah, [text_attr]
    mov ecx, COLS
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
    mov ecx, COLS * ROWS
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
    call print_nibble
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
;  Driver: PS/2 keyboard (scancode set 1)
; ------------------------------------------------------------

; AL = scancode -> AL = ASCII, or 0 if nothing to type
kb_decode:
    push rbx
    cmp al, 0xE0
    jne .not_ext
    mov byte [kb_ext], 1
    jmp .none
.not_ext:
    cmp byte [kb_ext], 0
    je .normal
    mov byte [kb_ext], 0        ; ignore arrows, etc.
    jmp .none
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

; compare zero-terminated strings RSI and RDI -> ZF=1 if equal
str_eq:
    push rax
    push rsi
    push rdi
.loop:
    mov al, [rsi]
    cmp al, [rdi]
    jne .done                   ; ZF=0
    test al, al
    jz .done                    ; ZF=1
    inc rsi
    inc rdi
    jmp .loop
.done:
    pop rdi
    pop rsi
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

; ============================================================
;  Data
; ============================================================
align 8
gdt:
    dq 0                        ; 0x00 null
    dq 0x00AF9A000000FFFF       ; 0x08 kernel code (64-bit, ring 0)
    dq 0x00CF92000000FFFF       ; 0x10 kernel data
    dq 0x00CFF2000000FFFF       ; 0x18 user data   (ring 3)
    dq 0x00AFFA000000FFFF       ; 0x20 user code   (64-bit, ring 3)
    dq 0x00CF9A000000FFFF       ; 0x28 32-bit code (boot only)
gdt_tss:
    dq 0, 0                     ; 0x30 TSS, filled in by tss_init
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

; Task State Segment: in 64-bit mode it mostly holds RSP0,
; the stack the CPU switches to when ring 3 is interrupted
align 16
tss:
    dd 0                        ; reserved
    dq 0                        ; RSP0 (offset 4)
    dq 0, 0                     ; RSP1, RSP2
    dq 0                        ; reserved
    dq 0, 0, 0, 0, 0, 0, 0      ; IST1-7
    dq 0                        ; reserved
    dw 0                        ; reserved
    dw 104                      ; I/O map base = end: no port access for ring 3
TSS_SIZE equ $ - tss

align 8
syscall_table:
    dq sys_exit, sys_print, sys_putc, sys_getkey
    dq sys_readline, sys_printnum, sys_ticks, sys_sleep
    dq sys_file_size, sys_read_file, sys_write_file, sys_delete_file
    dq sys_list_files
SYSCALL_COUNT equ ($ - syscall_table) / 8

align 8
isr_table:
%assign i 0
%rep 48
    dq isr_%+i
%assign i i + 1
%endrep

irq_handlers:                   ; IRQ 0-15
    dq irq_timer, irq_keyboard, 0, 0, 0, 0, 0, 0
    dq 0, 0, 0, 0, 0, 0, 0, 0

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
    dq s_ls,       cmd_ls
    dq s_cat,      cmd_cat
    dq s_write,    cmd_write
    dq s_rm,       cmd_rm
    dq s_format,   cmd_format
    dq s_pkg,      cmd_pkg
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
s_disk     db "disk", 0
s_ls       db "ls", 0
s_cat      db "cat", 0
s_write    db "write", 0
s_rm       db "rm", 0
s_format   db "format", 0
s_yes      db "yes", 0
s_pkg      db "pkg", 0
s_list     db "list", 0
s_info     db "info", 0
s_install  db "install", 0
s_remove   db "remove", 0

align 8
pkg_table:
    dq s_list,    pkg_list
    dq s_info,    pkg_info
    dq s_install, pkg_install
    dq s_remove,  pkg_remove
    dq 0
s_hexdump  db "hexdump", 0
s_crash    db "crash", 0
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

; text color themes (VGA attribute: background << 4 | foreground)
themes      db 0x07, 0x0A, 0x1F, 0x0E, 0x70
THEME_COUNT equ $ - themes

banner:
    db "  __  __        ___  ____  ", 10
    db " |  \/  |_   _ / _ \/ ___| ", 10
    db " | |\/| | | | | | | \___ \ ", 10
    db " | |  | | |_| | |_| |___) |", 10
    db " |_|  |_|\__, |\___/|____/ ", 10
    db "         |___/   v1.0  (64-bit)", 10, 10
    db "Type 'help' to see commands.", 10, 10, 0

prompt         db "myos> ", 0
msg_unknown    db "Unknown command. Type 'help'.", 10, 0
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
    db "  uptime    time since boot", 10
    db "  ls        list files", 10
    db "  cat F     show file F", 10
    db "  write F X write text X into file F", 10
    db "  rm F      delete file F", 10
    db "  format    erase the disk, create an empty file system", 10
    db "  pkg       packages: pkg list | info N | install N | remove N", 10
    db "  disk      show hard disk info", 10
    db "  hexdump N show raw bytes of disk sector N", 10
    db "  crash     test the panic screen ('crash pf' = page fault)", 10
    db "  reboot    restart the computer", 10
    db "  shutdown  power off", 10
    db "Programs run by typing their name. More in 'pkg list'.", 10, 0
msg_about:
    db "MyOS v1.0 - a tiny 64-bit operating system (text mode)", 10
    db "Written in x86-64 assembly. Own drivers for screen, keyboard,", 10
    db "timer, clock and hard disk - no BIOS after boot. Runs programs", 10
    db "from the disk in ring 3 (user mode) with system calls.", 10, 0
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
msg_color      db "Theme changed. Type 'color' again for the next one.", 10, 0
msg_uptime     db "Uptime: ", 0
msg_h          db "h ", 0
msg_m          db "m ", 0
msg_s          db "s", 10, 0
msg_ticks      db "Timer interrupts so far: ", 0
msg_ticks_tail db " (100 per second)", 10, 0
msg_no_disk    db "No hard disk found.", 10, 0
msg_disk_model db "Disk 0: ", 0
msg_disk_size  db "Size:   ", 0
msg_disk_mb    db " MB (", 0
msg_disk_sectors db " sectors x 512 bytes)", 10, 0
msg_disk_err   db "Disk error!", 10, 0
msg_no_fs      db "The disk has no file system yet. Type 'format' to create one.", 10, 0
msg_boot_nofs  db "Disk: not formatted. Type 'format' to create a file system.", 10, 10, 0
msg_boot_fs    db "Disk: MyFS mounted, ", 0
msg_boot_fs_tail db " file(s). Type 'ls' to list them.", 10, 10, 0
msg_bytes      db " bytes", 10, 0
msg_no_files   db "  (no files)", 10, 0
msg_files_free db " file(s), ", 0
msg_kb_free    db " KB free", 10, 0
msg_cat_usage  db "Usage: cat <file>", 10, 0
msg_write_usage db "Usage: write <file> <text>", 10, 0
msg_rm_usage   db "Usage: rm <file>", 10, 0
msg_not_found  db "No such file: ", 0
msg_wrote      db "Wrote ", 0
msg_bytes_to   db " bytes to ", 0
msg_removed    db "Removed ", 0
msg_dir_full   db "Directory is full (64 files max).", 10, 0
msg_disk_full  db "Disk is full.", 10, 0
msg_format_warn db "This ERASES everything on the disk. Type 'format yes' to continue.", 10, 0
msg_formatted  db "Disk formatted with MyFS. ", 0
fs_magic       db "MYFS0001", 0
msg_not_program db " is not a program.", 10, 0
msg_too_big    db "Program is too big (1 MB max).", 10, 0
msg_crashed    db "Program crashed: ", 0
msg_at_rip     db " at RIP=", 0
msg_fault_addr db 10, "  while touching address ", 0
msg_exit_code  db "Program exited with code ", 0
msg_pkg_usage  db "Usage: pkg list | pkg info NAME | pkg install NAME | pkg remove NAME", 10, 0
msg_pkg_head   db "  NAME          VERSION  STATUS      DESCRIPTION", 10, 0
msg_available  db "available", 0
msg_installed  db "installed", 0
msg_no_pkgs    db "  (no packages on the disk)", 10, 0
msg_no_pkg     db "No such package: ", 0
msg_try_list   db ". Try 'pkg list'.", 10, 0
msg_bad_pkg    db "That package file is damaged or not a package.", 10, 0
msg_installing db "Installing ", 0
msg_dots       db " ...", 10, 0
msg_installed_run db "Done. Type '", 0
msg_to_run     db "' to run it.", 10, 0
msg_not_installed db " is not installed.", 10, 0
msg_i_name     db "Package:     ", 0
msg_i_version  db 10, "Version:     ", 0
msg_i_desc     db 10, "Description: ", 0
msg_i_size     db 10, "Size:        ", 0
msg_i_installed db " bytes", 10, "Installed:   ", 0
msg_yes        db "yes", 10, 0
msg_no         db "no", 10, 0
msg_bad_sector db "Sector number is past the end of the disk.", 10, 0
msg_sector     db "Sector ", 0
msg_first_half db " (first 256 of 512 bytes):", 10, 0
msg_reboot     db "Rebooting...", 10, 0
msg_shutdown   db "Shutting down...", 10, 0
msg_safe_off   db "It is now safe to turn off your computer.", 10, 0
msg_no_lm      db "This CPU does not support 64-bit mode.", 13, 10, 0

str_panic_title db "*** KERNEL PANIC ***", 0
str_exception   db "Exception ", 0
str_colon       db ": ", 0
str_errcode     db "Error code: ", 0
str_cr2         db "   CR2: ", 0
str_gap2        db "  ", 0
str_halted      db "The system has been halted. Please restart the computer.", 0

; ---------------- variables ----------------
align 8
ticks        dq 0               ; incremented by the timer interrupt
saved_rsp    dq 0               ; shell's stack while a program runs
user_rsp     dq 0               ; program's stack during a syscall
disk_sectors dd 0
input_len    dd 0
mem_count    dw 0
text_attr    db 0x07
theme_index  db 0
cur_x        db 0
cur_y        db 0
kb_shift     db 0
kb_ext       db 0
boot_drive   db 0
disk_present db 0
fs_ready     db 0               ; 1 = MyFS found on the disk
kbq_head    db 0               ; keyboard queue (written by IRQ1)
kbq_tail     db 0
disk_model   times 41 db 0
cpu_buf      times 49 db 0
input_buf    times MAX_INPUT + 1 db 0
line_buf     times 128 db 0
name_buf     times NAME_MAX + 1 db 0
kbq          times 256 db 0

times KERNEL_SECTORS * 512 - ($ - $$) db 0
