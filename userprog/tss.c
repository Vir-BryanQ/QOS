#include "tss.h"
#include "thread.h"
#include "debug.h"
#include "global.h"
#include "stdint.h"

#define TSS_VADDR   0xc0000904  // TSS结构位于loader模块中，位于虚拟地址0xc0000904处 

typedef struct tss_struct 
{
    uint32_t backlink;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t esp1;
    uint32_t ss1;
    uint32_t esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint32_t es;
    uint32_t cs;
    uint32_t ss;
    uint32_t ds;
    uint32_t fs;
    uint32_t gs;
    uint32_t ldt_sel;
    uint32_t iobitmap_off;
} tss_struct;

// 修改TSS结构中的ESP0字段
void update_tss_esp0(task_struct *pthread)
{
    ((tss_struct *)TSS_VADDR)->esp0 = (uint32_t)pthread + PAGE_SIZE;
}   