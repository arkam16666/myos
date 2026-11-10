; ============================================================
;  myos.inc - everything a MyOS program needs
;
;  A program starts with PROGRAM, has a _start label, and talks
;  to the kernel only through 'syscall':
;      RAX = service number, RDI / RSI = arguments, result in RAX.
;  All registers except RAX, RCX and R11 survive a syscall.
;  At _start, RDI points to the command-line arguments.
; ============================================================

SYS_EXIT     equ 0      ; RDI = exit code (never returns)
SYS_PRINT    equ 1      ; RDI = zero-terminated string
SYS_PUTC     equ 2      ; RDI = character
SYS_GETKEY   equ 3      ; waits for a key, RAX = ASCII
SYS_READLINE equ 4      ; RDI = buffer, RSI = size, RAX = length
SYS_PRINTNUM equ 5      ; RDI = unsigned number
SYS_TICKS    equ 6      ; RAX = timer ticks since boot (100 per second)
SYS_SLEEP    equ 7      ; RDI = ticks to wait (100 = 1 second)
; files (all return -1 on error)
SYS_FILESIZE equ 8      ; RDI = name, RAX = size
SYS_READFILE equ 9      ; RDI = name, RSI = buffer, RDX = max, RAX = bytes read
SYS_WRITEFILE equ 10    ; RDI = name, RSI = data, RDX = size (create/replace)
SYS_DELETE   equ 11     ; RDI = name
SYS_LISTFILES equ 12    ; RDI = index, RSI = 48-byte name buffer, RAX = size

; program header: magic "MYEX", version, entry point
%macro PROGRAM 0
    [org 0x400000]
    [bits 64]
    default abs
    db "MYEX"
    dd 1
    dq _start
%endmacro

%macro PRINT 1
    mov rdi, %1
    mov eax, SYS_PRINT
    syscall
%endmacro

%macro PUTC 1
    mov edi, %1
    mov eax, SYS_PUTC
    syscall
%endmacro

%macro PRINTNUM 1
    mov rdi, %1
    mov eax, SYS_PRINTNUM
    syscall
%endmacro

%macro SLEEP 1
    mov edi, %1
    mov eax, SYS_SLEEP
    syscall
%endmacro

%macro EXIT 1
    mov edi, %1
    mov eax, SYS_EXIT
    syscall
%endmacro
