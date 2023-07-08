#ifndef __USERPROG_TSS_H
#define __USERPROG_TSS_H

typedef struct task_struct task_struct;

extern void update_tss_esp0(task_struct *pthread);        // 修改TSS结构中的ESP0字段

#endif