#ifndef __KERNEL_MEMORY_H
#define __KERNEL_MEMORY_H

#include "stdint.h"
#include "bitmap.h"
#include "sync.h"

#define PAGE_SIZE 4096
#define KERNEL_SPACE_START  0xc0000000            // 内核空间的起始地址
#define KERNEL_VMP_START  0xc0100000       // 内核虚拟池的起始地址

#define PG_P_0 0
#define PG_P_1 1
#define PG_RW_R 0
#define PG_RW_W 2
#define PG_US_S 0       
#define PG_US_U 4   // 曾经因为PG_US_U错写为0而导致在iret进入用户态之后出现了奇怪的缺页故障，在内核态下却没有该故障，调试了好几个小时

#define MBLOCK_DESC_CNT    7    // 内存块描述符的种类

#define ARENA2BLOCK(arena_addr, block_idx) \
((mem_block *)((uint32_t)arena_addr + sizeof(arena) + block_idx * (arena_addr->pdesc->block_size)))    // 获取arena中偏移为block_idx内存块的地址
#define BLOCK2ARENA(block_addr) ((arena *)((uint32_t)block_addr & 0xfffff000))   // 将内存块的地址转化为对应arena的地址

#define VADDR_H10_M(VADDR) (((VADDR) & 0xffc00000) >> 10)       // 虚拟地址的高10位右移到中间10位
#define VADDR_H10_L(VADDR) (((VADDR) & 0xffc00000) >> 20)       // 虚拟地址的高10位右移到低12位中的高10位
#define VADDR_M10_L(VADDR) (((VADDR) & 0x003ff000) >> 10)       // 虚拟地址的中间10位右移到低12位中的高10位

#define PDE_PTR(VADDR) (0xfffff000 | VADDR_H10_L(VADDR))       // 虚拟地址对应的页目录项指针
#define PTE_PTR(VADDR) (0Xffc00000 | VADDR_H10_M(VADDR) | VADDR_M10_L(VADDR))   // 虚拟地址对应的页表项指针

typedef enum pool_flag
{
    PF_KERNEL, PF_USER
} pool_flag;

// 虚拟内存池
typedef struct virt_mem_pool
{
    bitmap vmp_bitmap;
    void *virt_addr_start;
    mutex_lock mutex;       // 实现互斥访问
} virt_mem_pool;

// 物理内存池
typedef struct phy_mem_pool
{
    bitmap pmp_bitmap;
    void *phy_addr_start;
    uint32_t pool_size;
    mutex_lock mutex;       // 实现互斥访问
} phy_mem_pool;

typedef struct mem_block_desc
{
    uint32_t block_size;            // 内存块大小(规格)
    uint32_t block_cnt_per_arena;    // 每个arena中包含的内存块数量
    list free_list;                 // 空闲内存块链表
} mem_block_desc;

typedef struct mem_block
{
    node list_node;
} mem_block;

typedef struct arena
{
    mem_block_desc *pdesc;          // 指向本arena对应的内存块描述符, 对于大内存块的arena，此项为NULL
    bool large;                     
    uint32_t cnt;                   // 当large为false时，cnt代表arena中空闲内存块的数量，当large为true时，cnt代表页框的数量
} arena;

extern virt_mem_pool kernel_vm_pool;   // 内核专用的虚拟内存池
extern phy_mem_pool kernel_pm_pool;    // 内核物理内存池
extern phy_mem_pool user_pm_pool;      // 用户物理内存池

extern mem_block_desc k_mblock_descs[];

extern void mem_init(void); 
extern void *malloc_pages(pool_flag pf, const uint32_t pg_cnt);     // 为内核或用户进程分配pg_cnt个页，并返回首个虚拟页的虚拟地址
extern void *get_kernel_pages(const uint32_t pg_cnt);               // 为内核分配pg_cnt个页，并返回首个虚拟页的虚拟地址
extern void *get_user_pages(const uint32_t pg_cnt);                 // 为用户进程分配pg_cnt个页，并返回首个虚拟页的虚拟地址
extern void *get_a_page(void *virt_addr);             // 分配指定虚拟地址所在的页，返回虚拟地址对应虚拟页的起始地址
extern void *get_a_page_without_setting_vbitmap(void *virt_addr);    // 功能与get_a_page相同，只是不设置虚拟地址位图中的对应位
extern void *vaddr2paddr(void *vaddr);                 // 将虚拟地址转换为物理地址后返回
extern void mblock_desc_init(mem_block_desc *pdesc);                // 初始化内存块描述符
extern void mfree_pages(const uint32_t pg_cnt, void *ptr);   // 释放ptr起的连续pg_cnt个页
extern void free_a_ppage(void *paddr);                       // 在物理池中释放paddr处的一个页, 对于共享的用户物理页，仅仅使其引用数减一
extern void free_a_page_without_setting_pbitmap(void *vaddr);       // 释放虚拟页并修改页表，但是不释放物理页

extern void inc_pg_ref(uint32_t page);       // 使指定物理页的引用数加一
extern uint8_t test_dec_pg_ref(uint32_t page);       // 检测指定物理页的引用数，若引用数大于零，使其引用数减一，并返回原引用数
extern bool is_shared_page(uint32_t page);        // 判断指定物理页是否是一个共享页

extern void *kmalloc(const uint32_t size);      // 在内核堆空间中分配指定大小的内存
extern void *_malloc(pool_flag pf, const uint32_t size);      // sys_malloc 和 kmalloc 的分配过程由该函数实现

extern bool fix_arena_pdesc(node *pnode, int correct_pdesc);   // 修正arena中的描述符指针

#endif