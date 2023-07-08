%include "config.s"

SECTION loader vstart=LOADER_BASE_ADDR

total_mem_size dd 0     ; 占用四个字节，位于物理地址0x900处

; 将来所有用户进程共享一个TSS，任务切换时只需修改ESP0即可，该TSS位于物理地址0x904处
TSS:
    times 2 dd 0
    dd GDATA_SEL       ; SS0
    times (102 - ($ - TSS)) db 0
    dw 103               ; 不存在IO位图

start:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov sp, LOADER_BASE_ADDR

    ;获取物理内存大小 0xe820
    mov di, ards_buf
    mov edx, 0x534d4150
    xor ebx, ebx
.get_ards:
    mov eax, 0xe820
    mov ecx, 20
    int 0x15
    jc .e820_error
    inc word [ards_nr]
    add di, 20
    cmp ebx, 0
    jne .get_ards

    xor esi, esi
    mov cx, [ards_nr]
    mov di, ards_buf
.get_megs:
    mov eax, [di]
    mov edx, [di + 8]
    add eax, edx
    cmp eax, esi
    jle .next_ards
    mov esi, eax
.next_ards:
    add di, 20
    loop .get_megs

    mov [total_mem_size], esi
    jmp .L

.e820_error:
    jmp $
    
.L:
    cli

    lgdt [gdt48]

    in al, 0x92
    or al, 0x02
    out 0x92, al
    
    mov eax, cr0
    or eax, 0x01
    mov cr0, eax

    jmp dword GCODE_SEL:next

[bits 32]
next:
    mov ax, GDATA_SEL
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x7c00

    call clear_screen
    mov esi, msg_0
    call print_string

    ; 准备开启分页机制
    ;1. 设置页目录表及页表
    call setup_page

    sgdt [gdt48]

    ;更新显存段基地址
    or dword [GDT + 28], 0xc0000000 

    ;更新GDT基地址
    add dword [gdt48 + 2], KERNEL_SPACE_ADDR

    lgdt [gdt48]

    ;将esp移到1GB高端的内核空间
    ;使栈指针指向0xc009f000
    ;注意，这里不能直接用 add esp, 0xc0000000 将 esp 迁移到 0xc0007c00
    ;原因是 将来内核的入口地址是0xc0001500, 随着内核体积的增大，其占据的地址会覆盖0xc0001500 到 0xc0009000
    ;这也就会导致加载内核的时候会将在0xc0007c00处的栈数据覆盖掉，导致栈中的数据是错误的，引发不可预测问题
    ;在实际调试中，当第三次 mem_copy 函数返回时，就引发了上述问题，因为栈已经被内核数据破坏了
    mov esp, 0xc009f000

    ;2. 设置PDBR (CR3)
    mov eax, PAGE_DIR_TAB_ADDR
    mov cr3, eax

    ;3. 开启分页机制
    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax

    call clear_screen
    mov esi, msg_1
    call print_string

    ;将kernel.bin加载到KERNEL_BIN_ADDR处
    mov dx, 0x1f2
    mov al, KERNEL_SECT_NR
    out dx, al

    mov dx, 0x1f3
    mov al, KERNEL_START_SECT
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
    mov edi, KERNEL_BIN_ADDR
    mov ecx, KERNEL_SECT_NR * 512 / 2
.read:
    in ax, dx
    mov [edi], ax
    add edi, 2
    loop .read


    ;将kernel.bin解析后将kernel映像的各个段加载到对应虚拟地址中
    xor ecx, ecx
    xor edx, edx
    mov ebx, [KERNEL_BIN_ADDR + 0x1c]    ;program header所在文件的偏移
    mov cx, [KERNEL_BIN_ADDR + 0x2c]    ;program header的数目
    mov dx, [KERNEL_BIN_ADDR + 0x2a]    ;program header的大小

    add ebx, KERNEL_BIN_ADDR
.load_a_segment:
    cmp dword [ebx], PT_LOAD
    jne .next_segment

    mov edi, [ebx + 0x08]           ;段加载的虚拟地址
    
    push dword [ebx + 0x10]     ;段的大小
    mov eax, [ebx + 0x04]       ;段在文件中的偏移
    add eax, KERNEL_BIN_ADDR
    push eax                    ;源地址src
    push edi                     ;目的地址   
    call mem_copy
    add esp, 12
.next_segment:
    add ebx, edx            ;ebx指向下一个program header
    loop .load_a_segment
    
.enter_kernel:
    call clear_screen
    mov esi, msg_2
    call print_string

    ; 加载TR寄存器（为用户进程做准备, 相当于init_all中的tss_init()
    mov ax, TSS_SEL
    ltr ax

    call clear_screen
    jmp KERNEL_ENTRY_POINT

; 原型：mem_cpy(dst, src, size)
mem_copy:
    push ebp
    mov ebp, esp
    push ecx
    push edi
    push esi

    cld
    mov edi, [ebp + 8]
    mov esi, [ebp + 12]
    mov ecx, [ebp + 16]
rep movsb 

    pop esi
    pop edi
    pop ecx
    pop ebp
    ret


clear_screen:
    push eax
    push ecx
    push edx
    push es

    ;显存段
    mov ax, VIDEO_SEL
    mov es, ax

    mov ecx, 4000
.clear:
    mov byte [es:ecx - 1], 0
    loop .clear

    ;使光标复位
    mov dx, 0x3d4
    mov al, 0x0e
    out dx, al
    mov dx, 0x3d5
    xor al, al
    out dx, al

    mov dx, 0x3d4
    mov al, 0x0f
    out dx, al
    mov dx, 0x3d5
    xor al, al
    out dx, al

    pop es
    pop edx
    pop ecx
    pop eax
    ret

print_string:
    push eax
    push ebx
    push edx
    push es

    ;显存段
    mov ax, VIDEO_SEL
    mov es, ax

    xor ebx, ebx
    xor edx, edx
    mov ah, 0x0a
.print:
    mov al, [esi + ebx]
    cmp al, 0
    je .end_print
    mov [es:edx], al
    mov [es:edx + 1], ah
    add edx, 2
    inc ebx
    jmp .print

.end_print:
    pop es
    pop edx
    pop ebx
    pop eax
    ret


setup_page:
    pushad

    ;设置第一个页目录项
    mov eax, PAGE_DIR_TAB_ADDR + PAGE_SIZE
    or eax, PAGE_P_T | PAGE_RW_W | PAGE_US_S
    mov ebx, PAGE_DIR_TAB_ADDR
    mov [ebx], eax

    ;255个页目录项（从第768个页目录项开始）
    mov esi, KERNEL_PDE_INDEX
    mov ecx, 255
.set_pde:
    mov [ebx + esi * 4], eax
    add eax, PAGE_SIZE
    inc esi
    loop .set_pde

    ;使最后一个页目录项保存页目录表的地址
    mov dword [ebx + esi * 4], PAGE_DIR_TAB_ADDR | PAGE_P_T | PAGE_RW_W | PAGE_US_S


    ;设置页表项
    mov ecx, 256
    xor eax, eax
    or eax, PAGE_P_T | PAGE_RW_W | PAGE_US_S
    mov ebx, PAGE_DIR_TAB_ADDR + PAGE_SIZE
    xor esi, esi
.set_pte:
    mov [ebx + esi * 4], eax
    add eax, PAGE_SIZE
    inc esi
    loop .set_pte

    popad
    ret


GDT:
    dd 0x0, 0x0                     ; 空描述符
    dd 0x0000ffff, 0x00cf9800       ; 内核全局代码段描述符
    dd 0x0000ffff, 0x00cf9200       ; 内核全局数据段描述符
    dd 0x80000007, 0x00c0920b       ; 显存段描述符
    dd 0x09040067, 0xc0008900       ; TSS描述符，基址直接定为虚拟地址0xc0000904, 界限为0x67(103)
    dd 0x0000ffff, 0x00cff800       ; 用户全局代码段描述符
    dd 0x0000ffff, 0x00cff200       ; 用户全局数据段描述符
GDT_END:

gdt48:
    dw (GDT_END - GDT - 1)
    dd GDT

msg_0 db "Protected Mode OK!   ", 0
msg_1 db "Page Mode ok!   ", 0
msg_2 db "Load Kernel Successfully! ", 0

ards_buf:
times 200 db 0
ards_nr dw 0

times (2048 - ($ - $$)) db 0