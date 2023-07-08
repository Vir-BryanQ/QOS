[bits 32]
%define ERROR_CODE nop
%define NO_ERROR_CODE push dword 0

GDATA_SEL equ 0x0010

extern syscall_table
extern intr_handler_table
extern put_str

global intr_entry_table
global intr_exit
global syscall_handler

SECTION .data
    intr_entry_table:

%macro INTR_VECTOR 2
SECTION .text

intr_%1_entry:
    %2
    pushad
    push ds
    push es
    push fs
    push gs

    ;发送EOI
    mov al, 0x20
    out 0x20, al
    out 0xa0, al

    ; 中断进入内核后最重要的是把所有的段寄存器转移到内核全局段中
    ; 例如：put_char中会修改ds和es，如果此时发生中断，ds和es中的值对于中断处理程序来说是错误的！
    mov ax, GDATA_SEL
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ;进入真正的中断处理函数
    push dword [esp + 13 * 4]         ; 压入引起中断的指令地址或该指令下一条指令的地址
    push dword %1
    call [ds:intr_handler_table + %1 * 4]
    add esp, 8

    jmp intr_exit
    

SECTION .data
    dd intr_%1_entry

%endmacro

SECTION .text
intr_exit:
    pop gs
    pop fs
    pop es
    pop ds
    popad
    add esp, 4  ;跳过错误码
    iret

;中断处理程序
; 内部异常
INTR_VECTOR 0x00, NO_ERROR_CODE
INTR_VECTOR 0x01, NO_ERROR_CODE
INTR_VECTOR 0x02, NO_ERROR_CODE
INTR_VECTOR 0x03, NO_ERROR_CODE
INTR_VECTOR 0x04, NO_ERROR_CODE
INTR_VECTOR 0x05, NO_ERROR_CODE
INTR_VECTOR 0x06, NO_ERROR_CODE
INTR_VECTOR 0x07, NO_ERROR_CODE
INTR_VECTOR 0x08, ERROR_CODE
INTR_VECTOR 0x09, NO_ERROR_CODE
INTR_VECTOR 0x0a, ERROR_CODE
INTR_VECTOR 0x0b, ERROR_CODE
INTR_VECTOR 0x0c, ERROR_CODE
INTR_VECTOR 0x0d, ERROR_CODE
INTR_VECTOR 0x0e, ERROR_CODE
INTR_VECTOR 0x0f, NO_ERROR_CODE
INTR_VECTOR 0x10, NO_ERROR_CODE
INTR_VECTOR 0x11, ERROR_CODE
INTR_VECTOR 0x12, NO_ERROR_CODE
INTR_VECTOR 0x13, NO_ERROR_CODE

; Intel保留
INTR_VECTOR 0x14, NO_ERROR_CODE
INTR_VECTOR 0x15, NO_ERROR_CODE
INTR_VECTOR 0x16, NO_ERROR_CODE
INTR_VECTOR 0x17, NO_ERROR_CODE
INTR_VECTOR 0x18, NO_ERROR_CODE
INTR_VECTOR 0x19, NO_ERROR_CODE
INTR_VECTOR 0x1a, NO_ERROR_CODE
INTR_VECTOR 0x1b, NO_ERROR_CODE
INTR_VECTOR 0x1c, NO_ERROR_CODE
INTR_VECTOR 0x1d, NO_ERROR_CODE
INTR_VECTOR 0x1e, NO_ERROR_CODE
INTR_VECTOR 0x1f, NO_ERROR_CODE

; 外部中断
INTR_VECTOR 0x20, NO_ERROR_CODE
INTR_VECTOR 0x21, NO_ERROR_CODE
INTR_VECTOR 0x22, NO_ERROR_CODE
INTR_VECTOR 0x23, NO_ERROR_CODE
INTR_VECTOR 0x24, NO_ERROR_CODE
INTR_VECTOR 0x25, NO_ERROR_CODE
INTR_VECTOR 0x26, NO_ERROR_CODE
INTR_VECTOR 0x27, NO_ERROR_CODE
INTR_VECTOR 0x28, NO_ERROR_CODE
INTR_VECTOR 0x29, NO_ERROR_CODE
INTR_VECTOR 0x2a, NO_ERROR_CODE
INTR_VECTOR 0x2b, NO_ERROR_CODE
INTR_VECTOR 0x2c, NO_ERROR_CODE
INTR_VECTOR 0x2d, NO_ERROR_CODE
INTR_VECTOR 0x2e, NO_ERROR_CODE
INTR_VECTOR 0x2f, NO_ERROR_CODE


SECTION .text
; 系统调用中断处理函数
syscall_handler:
    sti             ; 在系统调用中断处理过程中，允许时钟、键盘等外部中断嵌套       

    push dword 0    ; 占位用，与intr_exit中的出栈过程保持形式上的统一         
    pushad
    push ds
    push es
    push fs
    push gs

    ; 中断进入内核后最重要的是把所有的段寄存器转移到内核全局段中
    ; 例如：put_char中会修改ds和es，如果此时发生中断，ds和es中的值对于中断处理程序来说是错误的！
    push eax            ; 注意使用ax之前要保护eax中的系统调用号
    mov ax, GDATA_SEL
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    pop eax

    ;进入真正的中断处理函数
    push edx
    push ecx
    push ebx
    call [ds:syscall_table + eax * 4]       ; 根据eax中的系统调用号进入真正的系统调用函数
    add esp, 12

    mov [esp + 44], eax                     ; 返回值放到中断栈的eax映像中，在中断返回时该返回值被写入到eax寄存器中
    jmp intr_exit

    
