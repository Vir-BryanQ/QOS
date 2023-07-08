[bits 32]

extern current
global switch_to

SECTION .text

; 线程切换函数
; 使用纯汇编可以保证出入栈操作严格遵循我们定义的栈结构
switch_to:
    push edi
    push esi
    push ebp
    push ebx

    ; eax中保存的是next变量的值，之前错误地把该行删了(判断是否需要切换的代码转移到schedule中了)，导致了奇怪的缺页中断，调试了很久
    mov eax, [esp + 20]

    mov ebx, [current]
    mov [ebx], esp      ; 将esp存入kstack_ptr变量中

    mov [current], eax  ; current = next

    mov esp, [eax]       ; 切换到新线程或新进程的栈

    pop ebx
    pop ebp
    pop esi
    pop edi
    ret