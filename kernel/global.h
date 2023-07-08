#ifndef __KERNEL_GLOBAL_H
#define __KERNEL_GLOBAL_H

#define NULL ((void *)0)

#define GCODE_SEL 0x0008    // 内核全局代码段选择子
#define GDATA_SEL 0x0010    // 内核全局数据段选择子
#define VIDEO_SEL 0x0018    // 显存段选择子
#define TSS_SEL    0x0020   // TSS选择子
#define UGCODE_SEL 0x002b   // 用户全局代码段选择子
#define UGDATA_SEL 0x0033   // 用户全局数据段选择子

#define nop() asm("nop")
#define hlt() asm("hlt")

#define EFLAGS_IF (1 << 9)
#define EFLAGS_MBS (1 << 1)     // EFLAGS的第1位固定为1
#define EFLAGS_IOPL_3 (3 << 12)   // 允许用户进程使用任何IO端口
#define EFLAGS_IOPL_0 0         // 在不存在IO许可位图的情况下，不允许用户进程使用任何IO端口
#define EFLAGS ({ uint32_t eflags; asm volatile ("pushfl; popl %0": "=a"(eflags)); eflags; })

#define CR3 ({ void *cr3; asm volatile ("movl %%cr3, %0": "=a"(cr3)); cr3; })

#define CPL ({ uint8_t cpl; asm volatile ("movl %%cs, %%eax; andl $0x00000003, %%eax;": "=a"(cpl)); cpl; })

#define UNUSED __attribute__((unused))      // 该属性可以避免编译器产生警告信息
#define PACKED __attribute__((packed))      // 该属性可以避免定义结构体时编译器为了对齐在各个成员之间添加空隙

#define DIV_ROUND_UP(val0, val1)  (((val0) + (val1) - 1) / (val1))        // 上取整除法

#endif

