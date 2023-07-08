#include "thread.h"
#include "debug.h"
#include "string.h"
#include "interrupt.h"
#include "list.h"
#include "print.h"
#include "process.h"
#include "tss.h"
#include "ide.h"
#include "init.h"
#include "stdio.h"
#include "_syscall.h"

#define PID_CNT 32768

uint8_t pid_bitmap_bits[PID_CNT >> 3];

struct 
{
    bitmap pid_bitmap;
    pid_t start_pid;
    mutex_lock lock;
} pid_pool;

void pid_init(void);


void idle(void *arg UNUSED);                // 系统空闲时运行的线程

task_struct *current = MAIN_THREAD;       // 指向当前处于TASK_RUNNING状态的TCB
task_struct *process_init;                  // init进程
task_struct *thread_idle;                   // idle线程 
list thread_ready_list;                   // 线程的就绪队列
list thread_all_list;                     // 线程的全队列

extern partition *root_part;                         // 根目录所在的分区

// 系统启动初期将线程有关的数据结构初始化
void thread_init(void)
{
    // 将就绪队列和全队列初始化（之前忘记初始化导致了奇怪的问题）
    list_init(&thread_all_list);
    list_init(&thread_ready_list);

    // 初始化pid池
    pid_init();

    // 创建init进程
    process_init = create_process("/sbin/init", 10, "init");

    // 创建idle线程
    thread_idle = thread_start("idle", idle, NULL, 1);

    // 使主线程合法化
    make_main_thread();

    put_str("Init thread successfully!\n");
}

// 初始化task_struct
void thread_task_struct_init(task_struct *pthread, const uint32_t priority, const char *name)
{
    ASSERT(pthread != NULL && name != NULL);
    pthread->kstack_ptr = (uint32_t)pthread + PAGE_SIZE;
    pthread->magic = MAGIC;
    pthread->priority = priority;
    pthread->ticks = priority;
    pthread->elapsed_ticks = 0;
    pthread->status = (pthread == MAIN_THREAD ? TASK_RUNNING : TASK_READY);
    pthread->pdt_base = NULL;
    pthread->pid = alloc_pid();
    strcpy((char *)pthread->name, name);
    pthread->fd_table[0] = stdin;
    pthread->fd_table[1] = stdout;
    pthread->fd_table[2] = stderr;
    pthread->wd_part = root_part;
    pthread->wd_i_no = root_part->sb->root_i_no;
    pthread->parent = pthread->child = pthread->y_sibling = pthread->o_sibling = NULL;
    for (uint32_t i = 3; i < MAX_FILES_OPEN_PER_PROC; ++i)
    {
        pthread->fd_table[i] = -1;
    }
}         

// 初始化内核栈
void thread_kstack_init(task_struct *pthread, thread_pfunc function, void *arg)   
{
    ASSERT(pthread != NULL && function != NULL);
    // 为中断栈和线程栈预留空间
    pthread->kstack_ptr -= sizeof(intr_stack);      
    pthread->kstack_ptr -= sizeof(thread_stack);

    thread_stack *pts = (thread_stack *)pthread->kstack_ptr;
    pts->ebp = pts->ebx = pts->esi = pts->edi = 0;
    pts->arg = arg;
    pts->function = function;
    pts->eip = (uint32_t)kernel_thread;
}   

// 创建一个线程
task_struct *thread_start(const char *name, thread_pfunc function, void *arg, const uint32_t priority) 
{
    ASSERT(name != NULL && function != NULL);

    task_struct *pthread = (task_struct *)get_kernel_pages(1);
    ASSERT(pthread != NULL);
    thread_task_struct_init(pthread, priority, name);
    thread_kstack_init(pthread, function, arg);

    // 把线程加入全队列
    ASSERT(!list_find(&thread_all_list, &pthread->all_list_node));
    list_push_back(&thread_all_list, &pthread->all_list_node);

    // 把线程加入就绪队列
    ASSERT(!list_find(&thread_ready_list, &pthread->general_list_node));
    list_push_back(&thread_ready_list, &pthread->general_list_node);

    return pthread;
}

//使内核主线程合法化
void make_main_thread(void)
{
    thread_task_struct_init(MAIN_THREAD, 10, "kernel_main_thread");

    // 由于内核主线程已经在运行态，因此无需将其加入就绪队列，只需加入全队列
    ASSERT(!list_find(&thread_all_list, &MAIN_THREAD->all_list_node));
    list_push_back(&thread_all_list, &MAIN_THREAD->all_list_node);
}        

// 负责调用线程入口函数，真正启动线程
void kernel_thread(thread_pfunc function, void *arg)
{
    set_intr_status(INTR_ON);
    function(arg);
}       

// 线程调度函数
void schedule(void)
{

    ASSERT(get_intr_status() == INTR_OFF);
    // 时间片轮转调度算法
    if (current->status == TASK_RUNNING)
    {
        // 如果当前线程的状态是TASK_RUNNING, 说明是时间片用完引起的调度，需要把线程状态设置为TASK_READY并放回就绪队列
        ASSERT(current->ticks == 0);
        current->status = TASK_READY;
        current->ticks = current->priority;

        // 将当前运行的进程放回到就绪队列中
        ASSERT(!list_find(&thread_ready_list, &current->general_list_node));
        list_push_back(&thread_ready_list, &current->general_list_node);

    }
    // TASK_HANGING TASK_BLOCKDE TASK_WAITING则无需将其放入就绪队列中

    // 将就绪队列头部的线程转入运行态
    if (thread_ready_list.length == 0)
    {
        // 若没有就绪线程，则唤醒idle线程并执行
        thread_unblock(thread_idle);
    }
    task_struct *next = member2struct(list_pop_front(&thread_ready_list), task_struct, general_list_node);

    next->status = TASK_RUNNING;            // 不要忘记把新线程的状态置为运行态

    // 如果切换目标是当前线程，则无需切换
    if (current == next)
    {
        return;
    }
    
    // 当线程和线程直接切换时，无需切换页表
    if (!(current->pdt_base == NULL && next->pdt_base == NULL))
    {
        switch_page_table(next);
    }
    
    // 修改TSS中的ESP0
    update_tss_esp0(next);

    switch_to(next);
}

// 阻塞调用该函数的线程（原子操作）
void thread_block(pthread_status status)
{
    intr_status old_status = set_intr_status(INTR_OFF);
    
    ASSERT(status == TASK_BLOCKED || status == TASK_HANGING || status == TASK_WAITING);
    current->status = status;
    schedule();

    set_intr_status(old_status);
}   

// 使pthread指向的线程接触阻塞态（原子操作）
void thread_unblock(task_struct *pthread)
{
    intr_status old_status = set_intr_status(INTR_OFF);

    ASSERT(pthread != NULL);
    ASSERT(list_find(&thread_all_list, &pthread->all_list_node));
    if (pthread->status == TASK_BLOCKED || pthread->status == TASK_HANGING || pthread->status == TASK_WAITING)
    {
        pthread->status = TASK_READY;
        // 接触阻塞的线程将在剩余的时间片下继续运行
        // 将该进程放回到就绪队列的队头中，以保证该线程尽快得到调度
        ASSERT(!list_find(&thread_ready_list, &pthread->general_list_node));
        list_push_front(&thread_ready_list, &pthread->general_list_node);
    }

    set_intr_status(old_status);
}  

// 调用该函数的线程主动让出CPU使用权，回到就绪态
void thread_yield(void)
{
    intr_status old_status = set_intr_status(INTR_OFF);

    ASSERT(current->status == TASK_RUNNING);
    current->status = TASK_READY;
    // 将当前运行的进程放回到就绪队列中
    ASSERT(!list_find(&thread_ready_list, &current->general_list_node));
    list_push_back(&thread_ready_list, &current->general_list_node);
    schedule();

    set_intr_status(old_status);
}

// 结束当前线程
void thread_exit(void)
{
    for (uint32_t i = 3; i < MAX_FILES_OPEN_PER_PROC; ++i)
    {
        if (current->fd_table[i] != -1)
        {
            sys_close(i);
        }
    }

    set_intr_status(INTR_OFF);

    current->status = TASK_DIED;
    if (MAIN_THREAD->status == TASK_WAITING)
    {
        thread_unblock(MAIN_THREAD);
    }
    schedule();
}

// 寻找已结束的线程
bool find_died_thread(node *pnode, int arg UNUSED)
{
    task_struct *pthread = member2struct(pnode, task_struct, all_list_node);
    return (pthread->status == TASK_DIED && !pthread->pdt_base);
} 

void pid_init(void)
{
    pid_pool.pid_bitmap.btmp_ptr = pid_bitmap_bits;
    pid_pool.pid_bitmap.bytes_length = (PID_CNT >> 3);
    bitmap_init(&pid_pool.pid_bitmap);

    pid_pool.start_pid = 1;
    mutex_lock_init(&pid_pool.lock);
}

// 分配pid
pid_t alloc_pid(void)
{
    mutex_lock_acquire(&pid_pool.lock);
    int32_t bit_idx = bitmap_scan(&pid_pool.pid_bitmap, 1);
    ASSERT(bit_idx != -1);
    mutex_lock_release(&pid_pool.lock);
    return bit_idx + pid_pool.start_pid;
}  

// 回收pid
void release_pid(pid_t pid)
{
    mutex_lock_acquire(&pid_pool.lock);
    bitmap_set(&pid_pool.pid_bitmap, pid - pid_pool.start_pid, 0);
    mutex_lock_release(&pid_pool.lock);
}                

// 系统空闲时运行的线程
void idle(void *arg UNUSED)
{
    while (1)
    {
        thread_block(TASK_BLOCKED);
        asm volatile ("sti; hlt");
    }
}    

// 以填充空格的方式以指定格式输出数据
uint32_t pad_print(void *data, char format, uint32_t pad_size)
{
    char buf[pad_size + 1];
    uint32_t len;
    switch (format)
    {
        case 'u':
        {
            len = sprintf(buf, "%u", *(uint32_t *)data);
            break;
        }
        case 's':
        {
            len = sprintf(buf, "%s", data);
            break;
        }
    }

    memset(buf + len, ' ', pad_size - len);
    buf[pad_size] = 0;

    return (!CPL ? printk("%s", buf) : printf("%s", buf));
} 

// 获取线程链表节点对应的线程信息
bool node2thread_info(node *pnode, int arg UNUSED)
{
    task_struct *pthread = member2struct(pnode, task_struct, all_list_node);
    pad_print(&pthread->pid, 'u', 8);
    if (pthread->parent)
    {
        pad_print(&pthread->parent->pid, 'u', 8);
    }
    else
    {
        pad_print("-1", 's', 8);
    }

    switch (pthread->status)
    {
        case TASK_BLOCKED:
        {
            pad_print("BLOCKED", 's', 12);
            break;
        }
        case TASK_DIED:
        {
            pad_print("DIED", 's', 12);
            break;
        }
        case TASK_HANGING:
        {
            pad_print("HANGING", 's', 12);
            break;
        }
        case TASK_READY:
        {
            pad_print("READY", 's', 12);
            break;
        }
        case TASK_RUNNING:
        {
            pad_print("RUNNING", 's', 12);
            break;
        }
        case TASK_WAITING:
        {
            pad_print("WAITING", 's', 12);
            break;
        }
    }
    pad_print(&pthread->elapsed_ticks, 'u', 12);
    printk("%s\n", pthread->name);

    return false; 
}




