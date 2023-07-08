#ifndef __USERPROG_PROCESS_H
#define __USERPROG_PROCESS_H

#define USER_SPACE_START 0x8048000                                      // 用户空间的起始地址

#include "stdint.h"

typedef struct task_struct task_struct; 

extern task_struct *create_process(const char *pathname, const uint32_t priority, const char *name);  // 从可执行文件创建一个用户进程
extern void switch_page_table(task_struct *pthread);                         // 进程切换时切换页表

extern void copy_process(task_struct *child);  // 将父进程(即当前进程)的进程体、PCB、内核栈复制给子进程

extern void adopt_children(task_struct *stepparent, task_struct *parent);   // 将parent的所有子进程过继给stepparent
extern void release_process_resource(void);         // 释放当前进程占有的大部分资源
extern void process_exit(task_struct *pthread);     // 彻底结束进程


#endif