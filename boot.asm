; ============================================================
;  MyOS - Boot sector (512 bytes)
;  BIOS loads this to 0x7C00, it loads the kernel to 0x7E00
; ============================================================
[org 0x7C00]
[bits 16]

KERNEL_SECTORS equ 64           ; kernel size = 64 * 512 = 32 KB
KERNEL_ADDR    equ 0x7E00
SECTORS_PER_TRACK equ 18        ; 1.44 MB floppy geometry
HEADS          equ 2

start:
    jmp 0x0000:init             ; normalize CS:IP

init:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00              ; stack grows down from below us
    sti
    mov [boot_drive], dl        ; BIOS passes boot drive in DL

    mov si, msg_loading
    call print

    ; read the kernel one sector at a time (works across tracks/heads)
    mov bx, KERNEL_ADDR
    mov word [lba], 1           ; sector 0 = this boot sector
.sector:
    mov byte [retries], 3
.try:
    mov ax, [lba]               ; LBA -> cylinder / head / sector
    xor dx, dx
    mov cx, SECTORS_PER_TRACK
    div cx                      ; AX = track, DX = sector - 1
    mov cl, dl
    inc cl
    xor dx, dx
    mov di, HEADS
    div di                      ; AX = cylinder, DX = head
    mov ch, al
    mov dh, dl
    mov dl, [boot_drive]
    mov ax, 0x0201              ; BIOS: read 1 sector to ES:BX
    int 0x13
    jnc .ok
    dec byte [retries]
    jz disk_error
    xor ah, ah                  ; reset disk and retry
    mov dl, [boot_drive]
    int 0x13
    jmp .try
.ok:
    add bx, 512
    inc word [lba]
    cmp word [lba], KERNEL_SECTORS
    jbe .sector

    mov dl, [boot_drive]
    jmp 0x0000:KERNEL_ADDR      ; hand over to the kernel

disk_error:
    mov si, msg_error
    call print
.halt:
    cli
    hlt
    jmp .halt

; print zero-terminated string at DS:SI
print:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    xor bh, bh
    int 0x10
    jmp print
.done:
    ret

msg_loading db "Loading kernel...", 13, 10, 0
msg_error   db "Disk read error!", 13, 10, 0
boot_drive  db 0
retries     db 0
lba         dw 0

times 510 - ($ - $$) db 0
dw 0xAA55                       ; boot signature
