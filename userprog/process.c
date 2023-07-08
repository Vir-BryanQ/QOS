#include "process.h"
#include "thread.h"
#include "bitmap.h"
#include "debug.h"
#include "memory.h"
#include "global.h"
#include "string.h"
#include "print.h"
#include "inode.h"
#include "interrupt.h"
#include "stdio.h"
#include "_syscall.h"
#include "exec.h"
#include "pipe.h"

void create_pg_dir(task_struct *pthread);                             // 为用户进程创建并初始化页目录表
void create_user_vm_pool(task_struct *pthread);                       // 为用户进程创建并初始化用户虚拟内存池 
void start_process(void *pathname);                             // 加载可执行文件体并进行栈的初始化工作以启动进程
void intr_exit(void);                                                 // 位于kernel.s中的中断出口函数

// 从可执行文件创建一个用户进程
task_struct *create_process(const char *pathname, const uint32_t priority, const char *name) 
{
    ASSERT(name && pathname);

    task_struct *pthread = (task_struct *)get_kernel_pages(1);
    ASSERT(pthread != NULL);
    thread_task_struct_init(pthread, priority, (name ? name : pathname));
    thread_kstack_init(pthread, start_process, (void *)pathname);

    create_pg_dir(pthread);
    create_user_vm_pool(pthread);

    // 初始化用户进程的内存块描述符组
    mblock_desc_init(pthread->u_mblock_descs);

    // 把进程加入全队列
    ASSERT(!list_find(&thread_all_list, &pthread->all_list_node));
    list_push_back(&thread_all_list, &pthread->all_list_node);

    // 把进程加入就绪队列
    ASSERT(!list_find(&thread_ready_list, &pthread->general_list_node));
    list_push_back(&thread_ready_list, &pthread->general_list_node);

    return pthread;
}   

// 为用户进程创建并初始化页目录表
void create_pg_dir(task_struct *pthread)
{
    pthread->pdt_base = get_kernel_pages(1);
    ASSERT(pthread->pdt_base);

    // 复制原页目录表的768 到 1023项复制到新页目录表的相同位置
    memcpy((void *)((uint32_t)pthread->pdt_base + 0x300 * 4), (void *)(0xfffff000 + 0x300 * 4), 0x100 * 4);

    // 使新目录表的最后一项指向自己的物理地址
    void *pdt_paddr = vaddr2paddr(pthread->pdt_base);
    ASSERT(pdt_paddr != NULL);
    // 不要忘记加上属性位，否则会出现缺页故障
    *(uint32_t *)((uint32_t)pthread->pdt_base + 0x3ff * 4) = (uint32_t)pdt_paddr | PG_US_S | PG_RW_W | PG_P_1;  

    // 清空页目录表的剩余项, 避免垃圾数据干扰地址映射
    memset(pthread->pdt_base, 0, PAGE_SIZE - 0x100 * 4);
}   

// 为用户进程创建并初始化用户虚拟内存池
void create_user_vm_pool(task_struct *pthread)
{
    mutex_lock_init(&pthread->user_vm_pool.mutex);
    pthread->user_vm_pool.virt_addr_start = (void *)USER_SPACE_START;
    pthread->user_vm_pool.vmp_bitmap.bytes_length = (KERNEL_SPACE_START - USER_SPACE_START) / PAGE_SIZE / 8;
    uint32_t btmp_pg_cnt = DIV_ROUND_UP(pthread->user_vm_pool.vmp_bitmap.bytes_length, PAGE_SIZE);      // 除不尽时向上取整
    pthread->user_vm_pool.vmp_bitmap.btmp_ptr = get_kernel_pages(btmp_pg_cnt);
    ASSERT(pthread->user_vm_pool.vmp_bitmap.btmp_ptr);
    bitmap_init(&pthread->user_vm_pool.vmp_bitmap);
}


// 加载可执行文件体并进行栈的初始化工作以启动进程
void start_process(void *pathname)                             
{
    void *entry_point = load_prog((const char *)pathname);
    ASSERT(entry_point);

    /* intr_stack *pis = (intr_stack *)((uint32_t)current->kstack_ptr + sizeof(thread_stack));
        如果采用以上写法，实际上在进程切换发生时，current->kstack_ptr会被修改，从而导致建立的中断栈并非位于内核
        栈的最顶端，这样在栈指针变化的过程中可能导致中断栈被破坏，从而引发一些匪夷所思的错误。因此确保中断栈总是位于内核
        栈的最顶端可以保证栈中数据的正确性 */
    intr_stack *pis = (intr_stack *)((uint32_t)current +  PAGE_SIZE - sizeof(intr_stack));
    pis->cs = UGCODE_SEL;
    pis->ss = pis->ds = pis->es = pis->fs = pis->gs = UGDATA_SEL;
    pis->eax = pis->ecx = pis->edx = pis->ebx = pis->esi = pis->edi = pis->ebp = pis->esp_dummy = 0;
    pis->eip = (uint32_t)entry_point;
    pis->eflags = EFLAGS_IF | EFLAGS_IOPL_0 | EFLAGS_MBS;  // 开中断，禁止用户进程访问任何端口
    pis->err_code = 0;

    // 用户栈位于用户空间顶端
    pis->esp = (uint32_t)get_a_page((void *)(KERNEL_SPACE_START - PAGE_SIZE));          
    pis->esp += PAGE_SIZE;
    ASSERT(pis->esp == KERNEL_SPACE_START);

    asm volatile ("movl %0, %%esp; jmp intr_exit":: "a"(pis));
}

// 进程切换时切换页表  
void switch_page_table(task_struct *pthread)
{
    // 当切换目标是内核线程，需要恢复内核线程的页表
    void *pdt_base = (!pthread->pdt_base ? (void *)0x100000 : vaddr2paddr(pthread->pdt_base));
    asm volatile ("movl %0, %%cr3\n\t": : "a"(pdt_base));
}  

// 将父进程(即当前进程)的进程体、PCB、内核栈复制给子进程
void copy_process(task_struct *child)
{
    // 复制PCB和内核栈
    memcpy(child, current, PAGE_SIZE);

    // 修改PCB
    child->elapsed_ticks = 0;
    child->pid = alloc_pid();
    child->ticks = child->priority;
    child->status = TASK_READY;
    create_pg_dir(child);
    
    char tmp[16];
    sprintf(tmp, "_f%u", child->pid);
    ASSERT(strlen(child->name) + strlen(tmp) < MAX_THREAD_NAME_LEN);
    strcat(child->name, tmp);

    /* 将子进程加入进程树中 */
    intr_status old_status = set_intr_status(INTR_OFF);

    child->parent = current;
    child->child = child->y_sibling = NULL;
    child->o_sibling = current->child;
    if (current->child)
    {
        current->child->y_sibling = child;
    }
    current->child = child;

    set_intr_status(old_status);

    for (uint32_t i = 3; i < MAX_FILES_OPEN_PER_PROC; ++i)
    {
        // 更新文件和管道的打开次数
        if (child->fd_table[i] != -1)
        {
            if (is_pipe(i))
            {
                ++file_table[child->fd_table[i]].f_pos;
            }
            else
            {
                ++file_table[child->fd_table[i]].p_inode->open_cnt;
            }
        }
    }

    /* 为方便起见，父进程的虚拟地址位图应当直接复制给子进程  */
    uint32_t btmp_pg_cnt = DIV_ROUND_UP(child->user_vm_pool.vmp_bitmap.bytes_length, PAGE_SIZE);
    child->user_vm_pool.vmp_bitmap.btmp_ptr = get_kernel_pages(btmp_pg_cnt);
    ASSERT(child->user_vm_pool.vmp_bitmap.btmp_ptr);
    memcpy(child->user_vm_pool.vmp_bitmap.btmp_ptr, current->user_vm_pool.vmp_bitmap.btmp_ptr, child->user_vm_pool.vmp_bitmap.bytes_length);

    // 修改内核栈
    intr_stack *pis = (intr_stack *)((uint32_t)child + PAGE_SIZE - sizeof(intr_stack));
    pis->eax = 0;           // 使子进程返回值为0
    *((uint32_t *)pis - 1) = (uint32_t)intr_exit;
    child->kstack_ptr = (uint32_t)((uint32_t *)pis - 5);

    // 采用写时复制技术，仅仅为子进程复制父进程的页表，而不直接复制父进程的进程体
    uint32_t *from_pgdir = current->pdt_base;
    uint32_t *to_pgdir = child->pdt_base;
    uint32_t *from_pgtab, *to_pgtab;
    for (uint32_t i = (uint32_t)current->user_vm_pool.virt_addr_start / 0x400000; i < 768; ++i)
    {
        if (from_pgdir[i] & PG_P_1)
        {
            from_pgtab = (uint32_t *)PTE_PTR(i * 0x400000);
            to_pgtab = get_kernel_pages(1);
            to_pgdir[i] = (uint32_t)vaddr2paddr(to_pgtab) | PG_US_U | PG_RW_W | PG_P_1;
            for (uint32_t j = 0; j < 1024; ++j)
            {
                if (from_pgtab[j] & PG_P_1)
                {
                    inc_pg_ref(from_pgtab[j] & 0xfffff000);
                    to_pgtab[j] = (from_pgtab[j] &= ~PG_RW_W);    // 将父进程和子进程的页表项设置为只读
                }
                else
                {
                    to_pgtab[j] = 0;
                }
            }
            free_a_page_without_setting_pbitmap(to_pgtab);
        }
    }
} 

// 将parent的所有子进程过继给stepparent
void adopt_children(task_struct *stepparent, task_struct *parent)
{
    intr_status old_status = set_intr_status(INTR_OFF);

    task_struct *oldest_child = NULL;
    task_struct *p = parent->child;
    while (p)
    {
        p->parent = stepparent;
        oldest_child = p;
        p = p->o_sibling;
    }

    if (oldest_child && stepparent->child)
    {
        oldest_child->o_sibling = stepparent->child;
        stepparent->child->y_sibling = oldest_child;
    }

    if (parent->child)
    {
        stepparent->child = parent->child;
        parent->child = NULL;
    }

    set_intr_status(old_status);
}

// 释放当前进程占有的大部分资源
void release_process_resource()
{
    uint32_t *pgdir = (uint32_t *)current->pdt_base;
    uint32_t *pgtab;

    // 释放进程占有的页表和页，但是由于进程仍需要页目录提供的映射关系，因此不释放页目录
    for (uint32_t i = (uint32_t)current->user_vm_pool.virt_addr_start / 0x400000; i < 768; ++i)
    {
        if (pgdir[i] & PG_P_1)
        {
            pgtab = (uint32_t *)PTE_PTR(i * 0x400000);
            for (uint32_t j = 0; j < 1024; ++j)
            {
                if (pgtab[j] & PG_P_1)
                {
                    free_a_ppage((void *)(pgtab[j] & 0xfffff000));
                }
            }
            free_a_ppage((void *)(pgdir[i] & 0xfffff000));
        }
    }

    // 释放进程的虚拟地址位图
    mfree_pages(DIV_ROUND_UP(current->user_vm_pool.vmp_bitmap.bytes_length, PAGE_SIZE), current->user_vm_pool.vmp_bitmap.btmp_ptr);
    
    // 关闭进程打开的文件
    for (uint32_t i = 3; i < MAX_FILES_OPEN_PER_PROC; ++i)
    {
        if (current->fd_table[i] != -1)
        {
            sys_close(i);
        }
    }
}    

// 彻底结束进程
void process_exit(task_struct *pthread)
{
    ASSERT(pthread->status == TASK_DIED);
    ASSERT(pthread->parent);

    intr_status old_status = set_intr_status(INTR_OFF);

    // 将进程从进程树中移除
    if (pthread->parent->child == pthread)
    {
        pthread->parent->child = pthread->o_sibling;
    }
    if (pthread->y_sibling)
    {
        pthread->y_sibling->o_sibling = pthread->o_sibling;
    }
    if (pthread->o_sibling)
    {
        pthread->o_sibling->y_sibling = pthread->y_sibling;
    }

    list_remove(&thread_all_list, &pthread->all_list_node);

    set_intr_status(old_status);

    release_pid(pthread->pid);

    // 释放页目录、PCB和内核栈
    mfree_pages(1, pthread->pdt_base);
    mfree_pages(1, pthread);
}    