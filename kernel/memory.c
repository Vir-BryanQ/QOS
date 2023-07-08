#include "memory.h"
#include "string.h"
#include "global.h"
#include "debug.h"
#include "print.h"
#include "thread.h"
#include "interrupt.h"

#define MEM_BITMAP 0xc009a000          // 所有内存池对应的bitmap均存放在0xc009a000 - 0xc009e000区间

virt_mem_pool kernel_vm_pool;   // 内核专用的虚拟内存池
phy_mem_pool kernel_pm_pool;    // 内核物理内存池
phy_mem_pool user_pm_pool;      // 用户物理内存池

mem_block_desc k_mblock_descs[MBLOCK_DESC_CNT];     // 内核的内存块描述符组

void *alloc_vpages(pool_flag pf, const uint32_t pg_cnt);    // 在指定类型的虚拟池中分配连续pg_cnt个虚拟页, 并返回首页的虚拟地址
void *alloc_a_ppage(pool_flag pf);                       // 在指定类型的物理池中分配一个物理页，并返回该物理页的物理地址
void set_mmap(void *phy_addr, void *virt_addr);           // 在页表项中设置新的虚拟页和物理页之间的映射
void free_vpages(const uint32_t pg_cnt, void *ptr);         // 在虚拟池中释放ptr起的连续pg_cnt个页
void reset_mmap(void *ptr);                                 // 在页表中清除虚拟地址ptr与物理地址之间的映射

uint8_t *pg_ref_tab;        // 页引用表, 用于记录所有物理页的引用数

void mem_init(void)
{
    put_str("Start to init memory......\n");

    // 获取物理内存大小，位于loader模块中，在地址0x900开始的4个字节
    uint32_t total_mem = *(uint32_t *)0x900;

    // 将低端1MB和页表占用的内存减去
    uint32_t used_mem = 0x100000 + PAGE_SIZE * 256;
    uint32_t free_mem = total_mem - used_mem;
    uint32_t free_pages = free_mem / PAGE_SIZE;

    // 内核池和用户池分别占用一半的可用物理内存
    uint32_t kernel_free_pages = free_pages / 2;
    uint32_t user_free_pages = kernel_free_pages;

    // 初始化内核物理池
    kernel_pm_pool.phy_addr_start = (void *)used_mem;
    kernel_pm_pool.pool_size = kernel_free_pages * PAGE_SIZE;
    kernel_pm_pool.pmp_bitmap.bytes_length = kernel_free_pages / 8;
    kernel_pm_pool.pmp_bitmap.btmp_ptr = (uint8_t *)MEM_BITMAP;
    bitmap_init(&kernel_pm_pool.pmp_bitmap);

    // 初始化用户物理池
    user_pm_pool.phy_addr_start = kernel_pm_pool.phy_addr_start + kernel_pm_pool.pool_size;
    user_pm_pool.pool_size = user_free_pages * PAGE_SIZE;
    user_pm_pool.pmp_bitmap.bytes_length = user_free_pages / 8;
    user_pm_pool.pmp_bitmap.btmp_ptr = kernel_pm_pool.pmp_bitmap.btmp_ptr + kernel_pm_pool.pmp_bitmap.bytes_length;
    bitmap_init(&user_pm_pool.pmp_bitmap);

    // 初始化内核虚拟池
    kernel_vm_pool.virt_addr_start = (void *)KERNEL_VMP_START;     //不能写成0x100000, 否则会发现内核缺少了3个物理页(被用作页表)
    kernel_vm_pool.vmp_bitmap.bytes_length = kernel_pm_pool.pmp_bitmap.bytes_length;
    kernel_vm_pool.vmp_bitmap.btmp_ptr = user_pm_pool.pmp_bitmap.btmp_ptr + user_pm_pool.pmp_bitmap.bytes_length;
    bitmap_init(&kernel_vm_pool.vmp_bitmap);

    // 初始化内存池的互斥锁
    mutex_lock_init(&kernel_pm_pool.mutex);
    mutex_lock_init(&kernel_vm_pool.mutex);
    mutex_lock_init(&user_pm_pool.mutex);

    // 初始化内核的内存块描述符组
    mblock_desc_init(k_mblock_descs);

    // 初始化页引用表
    uint32_t prt_len = user_pm_pool.pmp_bitmap.bytes_length << 3;
    pg_ref_tab = get_kernel_pages(DIV_ROUND_UP(prt_len, PAGE_SIZE));
    memset(pg_ref_tab, 0, prt_len);

    // 输出内核物理池信息
    put_str("kernel physical pool:\n");
    put_str("start physical address: 0x"); put_int((uint32_t)kernel_pm_pool.phy_addr_start); put_char('\n');
    put_str("pool size: 0x"); put_int(kernel_pm_pool.pool_size); put_str(" bytes\n");
    put_str("bitmap length: 0x"); put_int(kernel_pm_pool.pmp_bitmap.bytes_length); put_str(" bytes\n");
    put_str("bitmap start address: 0x"); put_int((uint32_t)kernel_pm_pool.pmp_bitmap.btmp_ptr); put_char('\n');

    // 输出用户物理池信息
    put_str("user physical pool:\n");
    put_str("start physical address: 0x"); put_int((uint32_t)user_pm_pool.phy_addr_start); put_char('\n');
    put_str("pool size: 0x"); put_int(user_pm_pool.pool_size); put_str(" bytes\n");
    put_str("bitmap length: 0x"); put_int(user_pm_pool.pmp_bitmap.bytes_length); put_str(" bytes\n");
    put_str("bitmap start address: 0x"); put_int((uint32_t)user_pm_pool.pmp_bitmap.btmp_ptr); put_char('\n');

    // 输出内核虚拟池信息
    put_str("kernel virtual pool:\n");
    put_str("start virtual address: 0x"); put_int((uint32_t)kernel_vm_pool.virt_addr_start); put_char('\n');
    put_str("bitmap length: 0x"); put_int(kernel_vm_pool.vmp_bitmap.bytes_length); put_str(" bytes\n");
    put_str("bitmap start address: 0x"); put_int((uint32_t)kernel_vm_pool.vmp_bitmap.btmp_ptr); put_char('\n');

    put_str("Init memory done!\n");
}

// 在指定类型的虚拟池中分配连续pg_cnt个虚拟页, 并返回首页的虚拟地址
void *alloc_vpages(pool_flag pf, const uint32_t pg_cnt)
{
    virt_mem_pool *p_vm_pool = (pf == PF_KERNEL ? &kernel_vm_pool : &current->user_vm_pool);

    mutex_lock_acquire(&p_vm_pool->mutex);
    ASSERT(pg_cnt <= p_vm_pool->vmp_bitmap.bytes_length << 3);
    int32_t bit_idx = bitmap_scan(&p_vm_pool->vmp_bitmap, pg_cnt);
    mutex_lock_release(&p_vm_pool->mutex);

    if (bit_idx == -1) return NULL;
    return p_vm_pool->virt_addr_start + bit_idx * PAGE_SIZE;
}  

// 在指定类型的物理池中分配一个物理页，并返回该物理页的物理地址
void *alloc_a_ppage(pool_flag pf)
{
    phy_mem_pool *p_pm_pool = (pf == PF_KERNEL ? &kernel_pm_pool : &user_pm_pool);

    mutex_lock_acquire(&p_pm_pool->mutex);
    int32_t bit_idx = bitmap_scan(&p_pm_pool->pmp_bitmap, 1);
    mutex_lock_release(&p_pm_pool->mutex);

    if (bit_idx == -1) return NULL;
    return p_pm_pool->phy_addr_start + bit_idx * PAGE_SIZE;
}   

// 在页表项中设置新的虚拟页和物理页之间的映射
void set_mmap(void *phy_addr, void *virt_addr)
{
    ASSERT(phy_addr != NULL && virt_addr != NULL);
    uint32_t *pde_ptr = (uint32_t *)PDE_PTR((uint32_t)virt_addr);
    uint32_t *pte_ptr = (uint32_t *)PTE_PTR((uint32_t)virt_addr);

    if (!(*pde_ptr & PG_P_1))
    {
        // 页表不存在
        // 在内核物理池中为新页表分配空间并修改页目录项
        void *pt_ptr = alloc_a_ppage(PF_KERNEL);    
        ASSERT(pt_ptr != NULL);
        *pde_ptr = ((uint32_t)pt_ptr & 0xfffff000)| (virt_addr >= kernel_vm_pool.virt_addr_start ? PG_US_S : PG_US_U)
                                                | PG_RW_W | PG_P_1; 
        
        // 清空页表，避免原来存在的垃圾数据干扰地址映射
        memset((void *)((uint32_t)pte_ptr & 0xfffff000), 0, PAGE_SIZE);  
    } 
    // 修改页表项
    *pte_ptr = ((uint32_t)phy_addr & 0xfffff000)| (virt_addr >= kernel_vm_pool.virt_addr_start ? PG_US_S : PG_US_U)
                                                | PG_RW_W | PG_P_1;  
}   

// 为内核或用户进程分配pg_cnt个页，并返回首个虚拟页的虚拟地址
void *malloc_pages(pool_flag pf, const uint32_t pg_cnt)
{
    // 先在虚拟池中分配连续pg_cnt个页面
    void *vpages_start_addr = alloc_vpages(pf, pg_cnt);
    
    ASSERT(vpages_start_addr != NULL);

    // 为所有虚拟页面分别分配物理页并建立映射
    uint32_t _pg_cnt = pg_cnt;
    void *_vpages_start_addr = vpages_start_addr;
    while (_pg_cnt)
    {
        void *ppage_addr = alloc_a_ppage(pf);
        ASSERT(ppage_addr != NULL);
        set_mmap(ppage_addr, _vpages_start_addr);
        _pg_cnt--;
        _vpages_start_addr += PAGE_SIZE;
    }

    return vpages_start_addr;
}   

// 为内核分配pg_cnt个页，并返回首个虚拟页的虚拟地址
void *get_kernel_pages(const uint32_t pg_cnt)
{
    return malloc_pages(PF_KERNEL, pg_cnt);
} 

// 为用户进程分配pg_cnt个页，并返回首个虚拟页的虚拟地址
void *get_user_pages(const uint32_t pg_cnt)
{
    return malloc_pages(PF_USER, pg_cnt);
} 

// 分配指定虚拟地址所在的页，返回虚拟地址对应虚拟页的起始地址
void *get_a_page(void *virt_addr)
{
    ASSERT(virt_addr != NULL);
    ASSERT(virt_addr >= kernel_vm_pool.virt_addr_start || 
            (virt_addr < (void *)KERNEL_SPACE_START && virt_addr >= current->user_vm_pool.virt_addr_start));
    pool_flag pf = (virt_addr >= kernel_vm_pool.virt_addr_start) ? PF_KERNEL : PF_USER;
    virt_mem_pool *p_vm_pool = (pf == PF_KERNEL ? &kernel_vm_pool : &current->user_vm_pool);

    mutex_lock_acquire(&p_vm_pool->mutex);

    // 分配虚拟地址对应的虚拟页
    // ### 在涉及到算术运算时，最好把指针强制转化为整型，因为指针的算术运算具有一定的不确定性
    uint32_t bit_idx = ((uint32_t)virt_addr - (uint32_t)p_vm_pool->virt_addr_start) / PAGE_SIZE;
    if (bitmap_test(&p_vm_pool->vmp_bitmap, bit_idx))
    {
        // 若虚拟页已被分配
        mutex_lock_release(&p_vm_pool->mutex);
        return NULL;
    }
    // 将该页分配出去
    bitmap_set(&p_vm_pool->vmp_bitmap, bit_idx, 1);

    // 分配一个物理页
    void *ppage_addr = alloc_a_ppage(pf);
    if (ppage_addr == NULL)
    {
        bitmap_set(&p_vm_pool->vmp_bitmap, bit_idx, 0);     // 如果物理页分配失败，需要释放虚拟页
        mutex_lock_release(&p_vm_pool->mutex);
        return NULL;
    }

    // 在页表中建立物理页与虚拟页之间的映射
    set_mmap(ppage_addr, (void *)((uint32_t)virt_addr & 0xfffff000)); 

    mutex_lock_release(&p_vm_pool->mutex);

    return (void *)((uint32_t)virt_addr & 0xfffff000);  
}     

// 功能与get_a_page相同，只是不设置虚拟地址位图中的对应位
void *get_a_page_without_setting_vbitmap(void *virt_addr)
{
    pool_flag pf = (virt_addr >= kernel_vm_pool.virt_addr_start) ? PF_KERNEL : PF_USER;

    // 分配一个物理页
    void *ppage_addr = alloc_a_ppage(pf);
    if (ppage_addr == NULL)
    {
        return NULL;
    }

    // 在页表中建立物理页与虚拟页之间的映射
    set_mmap(ppage_addr, (void *)((uint32_t)virt_addr & 0xfffff000)); 

    return (void *)((uint32_t)virt_addr & 0xfffff000);
} 

// 将虚拟地址转换为物理地址后返回
void *vaddr2paddr(void *vaddr)
{
    uint32_t *pde_ptr = (uint32_t *)PDE_PTR((uint32_t)vaddr);
    uint32_t *pte_ptr = (uint32_t *)PTE_PTR((uint32_t)vaddr);

    if (!(*pde_ptr & PG_P_1 && *pte_ptr & PG_P_1))
    {
        // 该虚拟地址尚未与任何物理地址建立映射
        return NULL;
    }
    // 物理地址 = 物理页地址 + 页内偏移
    void *paddr = (void *)((*pte_ptr & 0xfffff000) + ((uint32_t)vaddr & 0x00000fff));
    
    return paddr;
}             

// 初始化内存块描述符
void mblock_desc_init(mem_block_desc *pdesc)
{
    uint32_t block_size = 16;
    for (int i = 0; i < MBLOCK_DESC_CNT; i++, block_size *= 2)
    {
        pdesc[i].block_size = block_size;
        pdesc[i].block_cnt_per_arena = (PAGE_SIZE - sizeof(arena)) / block_size;
        list_init(&pdesc[i].free_list);
    }
}       

// 在虚拟池中释放ptr起的连续pg_cnt个页
void free_vpages(const uint32_t pg_cnt, void *ptr)
{
    ASSERT(!((uint32_t)ptr & 0x00000fff));
    ASSERT(ptr >= kernel_vm_pool.virt_addr_start || 
            (ptr < (void *)KERNEL_SPACE_START && ptr >= current->user_vm_pool.virt_addr_start));
    virt_mem_pool *p_vm_pool = (ptr >= kernel_vm_pool.virt_addr_start ? &kernel_vm_pool : &current->user_vm_pool);

    mutex_lock_acquire(&p_vm_pool->mutex);
    uint32_t _pg_cnt = pg_cnt;
    uint32_t bit_idx = ((uint32_t)ptr - (uint32_t)p_vm_pool->virt_addr_start) / PAGE_SIZE;
    while (_pg_cnt)
    {
        bitmap_set(&p_vm_pool->vmp_bitmap, bit_idx, 0);
        _pg_cnt--;
        bit_idx++;
    }
    mutex_lock_release(&p_vm_pool->mutex);
}   

// 在物理池中释放paddr处的一个页, 对于共享的用户物理页，仅仅使其引用数减一
void free_a_ppage(void *paddr)
{
    ASSERT(paddr >= kernel_pm_pool.phy_addr_start);

    if ((paddr >= user_pm_pool.phy_addr_start) && test_dec_pg_ref((uint32_t)paddr))
    {
        return;
    }

    phy_mem_pool *p_pm_pool = (paddr >= user_pm_pool.phy_addr_start ? &user_pm_pool : &kernel_pm_pool);

    mutex_lock_acquire(&p_pm_pool->mutex);
    uint32_t bit_idx = ((uint32_t)paddr - (uint32_t)p_pm_pool->phy_addr_start) / PAGE_SIZE;
    bitmap_set(&p_pm_pool->pmp_bitmap, bit_idx, 0);
    mutex_lock_release(&p_pm_pool->mutex);
}   

// 在页表中清除虚拟地址ptr与物理地址之间的映射
void reset_mmap(void *ptr)
{
    // 清除页表项的前提是页表存在
    ASSERT((*(uint32_t *)PDE_PTR((uint32_t)ptr)) & PG_P_1);
    uint32_t *pte_ptr = (uint32_t *)PTE_PTR((uint32_t)ptr);
    if (*pte_ptr & PG_P_1)
    {
        // 如果P位为1，则将其清零
        *pte_ptr &= ~PG_P_1;             // 将P位清0
        // 将块表中的对应项去除
        /*  asm volatile ("invlpg %0\n\t":: "m"(ptr));               
            asm volatile ("movl %%cr3, %%eax; movl %%eax, %%cr3":);
            asm volatile ("invlpg %0":: "m"(*(uint32_t *)ptr): "memory");
            第一种写法是错误的，这种写法相当于 invlpg [0xc00048c3], 其中 0xc00048c3可以认为是ptr的地址
            第二种写法是正确的，但是会清空整个TLB，导致系统性能降低
            第三种写法是正确的的，这种写法相当于把ptr的内容作为内存单元，即 invlpg [0x8048000]
            最终采用的写法最简洁易用, 且性能好，刚开始由于采用了第一种写法，导致连续调试了两天两夜都没调通，
            出现了许许多多 无比诡异的 bug, 一个偶然的机会下才发现了该问题，最终的得以解决，invlpg指令在
            Intel486 之前是不支持的。
        */
        asm volatile ("invlpg (%0)":: "a"(ptr): "memory");
    }
}                            

// 释放ptr起的连续pg_cnt个页
void mfree_pages(const uint32_t pg_cnt, void *ptr)
{
    ASSERT((ptr != NULL) && !((uint32_t)ptr & 0x00000fff));
    void *vaddr = ptr;
    uint32_t _pg_cnt = pg_cnt;
    while (_pg_cnt)
    {
        void *paddr = vaddr2paddr(vaddr);
        ASSERT(paddr != NULL);
        free_a_ppage(paddr);
        reset_mmap(vaddr);
        _pg_cnt--;
        vaddr = (void *)((uint32_t)vaddr + PAGE_SIZE);
    }
    free_vpages(pg_cnt, ptr);
} 

// 释放虚拟页并修改页表，但是不释放物理页
void free_a_page_without_setting_pbitmap(void *vaddr)
{
    reset_mmap(vaddr);
    free_vpages(1, vaddr);
}       

// 使指定物理页的引用数加一
void inc_pg_ref(uint32_t page)
{
    intr_status old_status = set_intr_status(INTR_OFF);

    uint32_t i = (page - (uint32_t)user_pm_pool.phy_addr_start) / PAGE_SIZE;
    ASSERT(pg_ref_tab[i] < 0xff);
    ++pg_ref_tab[i];

    set_intr_status(old_status);
}   

// 检测指定物理页的引用数，若引用数大于零，使其引用数减一，并返回原引用数
uint8_t test_dec_pg_ref(uint32_t page)       
{
    intr_status old_status = set_intr_status(INTR_OFF);

    uint32_t i = (page - (uint32_t)user_pm_pool.phy_addr_start) / PAGE_SIZE;
    uint8_t pg_ref = pg_ref_tab[i];
    if (pg_ref > 0)
    {
        --pg_ref_tab[i];
    }

    set_intr_status(old_status);

    return pg_ref;
}   

// 判断指定物理页是否是一个共享页
bool is_shared_page(uint32_t page)  
{
    intr_status old_status = set_intr_status(INTR_OFF);
    uint8_t pg_ref = pg_ref_tab[(page - (uint32_t)user_pm_pool.phy_addr_start) / PAGE_SIZE];
    set_intr_status(old_status);
    return pg_ref ? true : false;
}

// 在内核堆空间中分配指定大小的内存
void *kmalloc(const uint32_t size)
{
    return _malloc(PF_KERNEL, size);
}      

// sys_malloc 和 kmalloc 的分配过程由该函数实现
void *_malloc(pool_flag pf, const uint32_t size)
{
    if (!size)
    {
        return NULL;
    }

    mem_block_desc *mblock_descs;
    phy_mem_pool *p_pm_pool;
    if (pf == PF_USER)
    {
        mblock_descs = current->u_mblock_descs;
        p_pm_pool = &user_pm_pool;
    }
    else
    {
        mblock_descs = k_mblock_descs;
        p_pm_pool = &kernel_pm_pool;
    }
    mutex_lock_acquire(&p_pm_pool->mutex);

    if (size > 1024)
    {
        uint32_t pg_cnt = DIV_ROUND_UP(size + sizeof(arena), PAGE_SIZE);
        arena *parena = (arena *)malloc_pages(pf, pg_cnt);
        if (!parena)
        {
            mutex_lock_release(&p_pm_pool->mutex);
            return NULL;
        }
        parena->cnt = pg_cnt;
        parena->large = true;
        parena->pdesc = NULL;

        mutex_lock_release(&p_pm_pool->mutex);
        return (void *)(parena + 1);            // 跨过开头的arena元数据返回可用内存块的地址
    }
    else
    {
        mem_block_desc *pdesc;
        for (int i = 0; i < MBLOCK_DESC_CNT; i++)
        {
            // 寻找合适内存块规格的内存块描述符
            if (size <= mblock_descs[i].block_size)
            {
                pdesc = &mblock_descs[i];
                break;
            }
        }

        if (pdesc->free_list.length == 0)
        {
            // 当空闲块已经用完，分配一个新的页作为arena使用
            arena *parena = (arena *)malloc_pages(pf, 1);
            if (!parena)
            {
                mutex_lock_release(&p_pm_pool->mutex);
                return NULL;
            }
            parena->cnt = pdesc->block_cnt_per_arena;
            parena->large = false;
            parena->pdesc = pdesc;

            intr_status old_status = set_intr_status(INTR_OFF);
            // 将新arena中的所有内存块添加到free_list中
            for (uint32_t blk_idx = 0; blk_idx < pdesc->block_cnt_per_arena; blk_idx++)
            {
                mem_block *blk_addr = (mem_block *)ARENA2BLOCK(parena, blk_idx);
                ASSERT(!list_find(&pdesc->free_list, &blk_addr->list_node));
                list_push_back(&pdesc->free_list, &blk_addr->list_node);
            }

            set_intr_status(old_status);
        }
        // 在free_list中分配一块内存块
        void *blk_addr = (void *)member2struct(list_pop_front(&pdesc->free_list), mem_block, list_node);
        BLOCK2ARENA(blk_addr)->cnt--;
    
        mutex_lock_release(&p_pm_pool->mutex);
        return blk_addr;
    }
} 

// 修正arena中的描述符指针
bool fix_arena_pdesc(node *pnode, int correct_pdesc)
{
    void *blk_addr = (void *)member2struct(pnode, mem_block, list_node);
    arena *parena = BLOCK2ARENA(blk_addr);
    if (parena->pdesc)
    {
        parena->pdesc = (mem_block_desc *)correct_pdesc;
    }
    return false;
}


