#ifndef __THREAD_THREAD_H
#define __THREAD_THREAD_H

#include "stdint.h"
#include "memory.h"
#include "file.h"
#include "global.h"

#define MAGIC 0x20010828 
#define MAX_THREAD_NAME_LEN 32

typedef void (*thread_pfunc) (void *);      // 万能函数指针
typedef uint32_t pid_t;        // 进程标识符类型

typedef struct partition partition;

// 线程的状态
typedef enum pthread_status
{
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_DIED,
    TASK_HANGING,
    TASK_WAITING,
} pthread_status;

// PCB 或 TCB
typedef struct task_struct
{
    uint32_t kstack_ptr;       // 任务切换时存储内核栈的栈指针

    pid_t pid;                  // 进程标识符或线程标识符

    pthread_status status;      // 线程的当前状态

    uint32_t priority;          // 优先级
    uint32_t ticks;             // 时间片
    uint32_t elapsed_ticks;     // 线程在处理器上运行的总时间片

    node general_list_node;      
    node all_list_node;

    void *pdt_base;             // 页目录表基址（虚拟地址），如果是线程，则为NULL
    virt_mem_pool user_vm_pool; // 用户虚拟内存池
    mem_block_desc u_mblock_descs[MBLOCK_DESC_CNT];         // 用户进程的内存块描述符组

    char name[MAX_THREAD_NAME_LEN];              // 线程名

    int32_t fd_table[MAX_FILES_OPEN_PER_PROC];   // 文件描述符表，用于记录打开的文件

    partition *wd_part;            // 进程工作目录所在的分区
    uint32_t wd_i_no;              // 进程工作目录inode编号

    // 如果是线程，以下四项均为NULL
    task_struct *parent;           // 指向父进程
    task_struct *child;            // 指向最新创建的子进程
    task_struct *y_sibling;        // 指向所有创建时间晚于当前进程的子进程中最早创建的进程
    task_struct *o_sibling;        // 指向所有创建时间早于当前进程的子进程中最晚创建的进程

    int32_t exit_status;           // 进程的退出状态

    uint32_t magic;             // 作为内核栈和task_struct之间的界限

} task_struct;

// 进入中断入口函数intr_%1_entry后栈中的寄存器映像分配结构
typedef struct intr_stack
{
    uint32_t gs;
    uint32_t fs;
    uint32_t es;
    uint32_t ds;
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp_dummy;         // esp is ignored!
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    uint32_t err_code;
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
    uint32_t esp;
    uint32_t ss;
} intr_stack;

typedef struct thread_stack
{
    uint32_t ebx;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint32_t eip;       // 用于执行ret时的控制转移
    uint32_t unused_addr;       // 占位返回地址
    thread_pfunc function;      // 线程入口函数地址
    void *arg;          // 传递给线程入口函数的参数
} thread_stack;

#define MAIN_THREAD ((task_struct *)0xc009e000)

extern task_struct *current;                    // 指向当前处于TASK_RUNNING状态的TCB
extern task_struct *process_init;                // init进程
extern list thread_ready_list;                   // 线程的就绪队列
extern list thread_all_list;                     // 线程的全队列

extern void thread_init(void);              // 系统启动初期将线程有关的数据结构初始化
extern void thread_task_struct_init(task_struct *pthread, const uint32_t priority, const char *name);           // 初始化task_struct
extern void thread_kstack_init(task_struct *pthread, thread_pfunc function, void *arg);             // 初始化内核栈
extern task_struct *thread_start(const char *name, thread_pfunc function, void *arg, const uint32_t priority); // 创建一个线程
extern void make_main_thread(void);             //使内核主线程合法化
extern void kernel_thread(thread_pfunc function, void *arg);        // 负责调用线程入口函数，真正启动线程

extern void schedule(void);                 // 线程调度函数
extern void switch_to(task_struct *next);   // 线程切换函数

extern void thread_block(pthread_status status);    // 阻塞调用该函数的线程（原子操作）
extern void thread_unblock(task_struct *pthread);   // 使pthread指向的线程接触阻塞态（原子操作）
extern void thread_yield(void);                     // 调用该函数的线程主动让出CPU使用权，回到就绪态
extern void thread_exit(void);                      // 结束当前线程

extern bool find_died_thread(node *pnode, int arg UNUSED); // 寻找已结束的线程

extern pid_t alloc_pid(void);                      // 分配pid
extern void release_pid(pid_t pid);                // 回收pid

extern bool node2thread_info(node *pnode, int arg UNUSED);   // 获取线程链表节点对应的线程信息

extern uint32_t pad_print(void *data, char format, uint32_t pad_size);     // 以填充空格的方式以指定格式输出数据

#endif