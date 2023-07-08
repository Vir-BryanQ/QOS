#ifndef __KERNEL_INTERRUPT_H
#define __KERNEL_INTERRUPT_H

typedef void* intr_entry;   
extern intr_entry intr_entry_table[];   //中断入口表
extern intr_entry intr_handler_table[]; //中断处理函数表

extern void intr_init(void);                   //中断初始化的主函数

// 中断状态：中断开--中断关
typedef enum intr_status 
{
    INTR_OFF, INTR_ON
} intr_status;

extern intr_status get_intr_status(void);                       // 获取中断状态
extern intr_status set_intr_status(intr_status status);     // 设置中断状态

extern void syscall_handler(void);                  // 系统调用中断处理函数

#endif


