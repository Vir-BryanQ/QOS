[bits 32]
GCODE_SEL equ 0x0008
GDATA_SEL equ 0x0010
VIDEO_SEL equ 0x0018

global get_cursor
global set_cursor
global get_text_attrib
global set_text_attrib
global put_char
global put_str
global put_int

get_cursor:
    push ebx

    xor ebx, ebx
    mov dx, 0x3d4
    mov al, 0x0e
    out dx, al
    mov dx, 0x3d5
    in al, dx
    mov bh, al

    mov dx, 0x3d4
    mov al, 0x0f
    out dx, al
    mov dx, 0x3d5
    in al, dx
    mov bl, al

    ; 将光标位置返回
    mov ax, bx

    pop ebx
    ret


set_cursor:
    push ebp
    mov ebp, esp
    push ebx
    push es
    push ds

    ; 获取旧光标的位置
    call get_cursor
    push ax

    xor ebx, ebx
    mov bx, [ebp + 8]
    mov dx, 0x3d4
    mov al, 0x0e
    out dx, al
    mov dx, 0x3d5
    mov al, bh
    out dx, al

    mov dx, 0x3d4
    mov al, 0x0f
    out dx, al
    mov dx, 0x3d5
    mov al, bl
    out dx, al

    mov ax, VIDEO_SEL
    mov es, ax
    mov ax, GDATA_SEL
    mov ds, ax

    ; 设置光标所在位置的属性，保证光标可见
    mov al, [text_attrib]
    mov byte [es:ebx * 2 + 1], al

    ; 将旧光标的位置返回
    pop ax

    pop ds
    pop es
    pop ebx
    pop ebp
    ret

get_text_attrib:
    mov al, [text_attrib]
    ret

set_text_attrib:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push es

    mov ax, VIDEO_SEL
    mov es, ax
    
    ;获取光标位置
    xor esi, esi
    call get_cursor
    mov si, ax

    mov al, [text_attrib]  ; 将旧的属性返回
    mov bl, [ebp + 8]
    mov [text_attrib], bl
    mov [es:esi * 2 + 1], bl

    pop es
    pop esi
    pop ebx
    pop ebp
    ret
text_attrib db 0x0a         ; 默认为高亮绿色

put_char:
    push ebp
    mov ebp, esp
    pushad
    push es
    push ds
    
    mov ax, VIDEO_SEL
    mov es, ax
    
    ;获取光标位置, 存储于bx中
    xor ebx, ebx
    call get_cursor
    mov bx, ax

    ; 待打印字符
    mov cl, [ebp + 8]
    cmp cl, 0xd
    je .CRLF
    cmp cl, 0xa
    je .CRLF
    cmp cl, 0x08
    je .backspace

    mov [es:ebx * 2], cl
    inc ebx
.check:
    cmp ebx, 2000
    jae .roll_screen
    push bx
    call set_cursor
    add esp, 2
.end_put:
    pop ds
    pop es
    popad
    mov esp, ebp
    pop ebp
    ret

.CRLF:
    xor dx, dx
    mov ax, bx
    mov cx, 80
    div cx
    sub bx, dx
    add bx, 80
    jmp .check

.backspace:
    ;当光标位于0处时，不需要前移
    cmp ebx, 0
    je .end_put
    dec ebx
    mov byte [es:ebx * 2], 0
    push bx
    call set_cursor
    add esp, 2
    jmp .end_put

.roll_screen:
    cld
    mov ax, VIDEO_SEL
    mov ds, ax
    xor edi, edi
    mov esi, 160
    mov ecx, 1920
rep movsw

    mov eax, 1920
    mov ecx, 80
.clear_last_line:   
    mov byte [es:eax * 2], 0
    inc eax
    loop .clear_last_line

    ;修正光标位置
    sub ebx, 80
    push bx
    call set_cursor
    add esp, 2
    jmp .end_put


put_str:
    push ebp
    mov ebp, esp
    push ebx
    push ecx
    push ds

    mov bx, GDATA_SEL
    mov ds, bx

    mov ebx, [ebp + 8]
    xor ecx, ecx
.put_str:
    mov cl, [ebx]
    cmp cl, 0
    je .end_put_str
    push ecx
    call put_char
    add esp, 4
    inc ebx
    jmp .put_str     
    
.end_put_str:
    pop ds
    pop ecx
    pop ebx
    mov esp, ebp
    pop ebp
    ret

put_int:
    push ebp
    mov ebp, esp
    push eax
    push ebx
    push ecx
    push edx
    push ds

    mov ax, GDATA_SEL
    mov ds, ax

    mov eax, [ebp + 8]
    mov ecx, 8
.get_char:
    mov bl, al
    and bl, 0x0f
    cmp bl, 10
    jb .is_0_to_9
    sub bl, 10
    add bl, 'a'
    mov [str_buffer + ecx - 1], bl
    jmp .loop
.is_0_to_9:
    add bl, '0'    
    mov [str_buffer + ecx - 1], bl
.loop:
    shr eax, 4
    loop .get_char  

    xor eax, eax
.z:
    cmp byte [str_buffer + eax], '0'
    jne .nz
    inc eax
    jmp .z

.full0:
    dec eax
    jmp .put_int
.nz:
    cmp byte [str_buffer + eax], 0
    je .full0
.put_int:   
    add eax, str_buffer
    push eax
    call put_str
    add esp, 4

    pop ds
    pop edx
    pop ecx
    pop ebx
    pop eax
    mov esp, ebp
    pop ebp 
    ret

str_buffer times 9 db 0