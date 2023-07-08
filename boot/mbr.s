%include "config.s"

SECTION MBR vstart=0x7c00
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov sp, 0x7c00
    
    ;读取loader模块
    ;直接读取四个扇区
    mov dx, 0x1f2
    mov al, LOADER_SECT_NR
    out dx, al

    ;loader位于第一个扇区中
    mov dx, 0x1f3
    mov al, LOADER_START_SECT
    out dx, al

    xor al, al
    mov dx, 0x1f4
    out dx, al
    
    mov dx, 0x1f5
    out dx, al

    mov dx, 0x1f6
    or al, 0xe0
    out dx, al

    mov dx, 0x1f7
    mov al, 0x20
    out dx, al
    
.wait:
    nop
    in al, dx
    and al, 0x88
    cmp al, 0x08
    jne .wait

    mov dx, 0x1f0
    mov ax, LOADER_BASE_ADDR / 0x10
    mov es, ax
    xor di, di
    mov cx, LOADER_SECT_NR * 512 / 2
.read:
    in ax, dx
    mov [es:di], ax
    add di, 2
    loop .read


    jmp  0x0000:LOADER_START

times (510 - ($ - $$)) db 0

db 0x55, 0xaa