#include "interrupt.h"
#include "global.h"
#include "print.h"
#include "io.h"
#include "stdint.h"
#include "thread.h"
#include "string.h"
#include "_syscall.h"

#define sti() asm("sti")
#define cli() asm("cli")

#define INTR_CNT 0x81     //已定义的中断数

//定义门描述符
typedef struct gate_desc
{   
    uint16_t offset_low_16_bits;
    uint16_t selector;
    uint8_t dcount;
    uint8_t attrib;
    uint16_t offset_high_16_bits;
} PACKED gate_desc ;                 // PACKED宏保证各个成员之间没有空隙，即该结构体的大小为 8字节，恰好是一个门描述符的大小

void idt_init(void);                                                                         //安装idt内所有门描述符
void make_gate_desc(gate_desc *p_desc, intr_entry offset, uint16_t selector, uint8_t dcount, uint8_t attrib);          //安装单个门描述符
void pic_init(void);        //初始化可编程中断控制器
void exception_init(void);   //初始化异常处理向量
void general_intr_handler(uint32_t vec_nr, uint32_t intr_addr);     //通用中断处理函数
void PF_handler(uint32_t vec_nr UNUSED, uint32_t intr_addr);                                //缺页异常处理函数

char *intr_name[] = {
    "#DE Divide Error",
    "#DB Debug Exception",
    "NMI Interrupt",
    "#BP Breakpoint Exception",
    "#OF Overflow Exception",
    "#BR BOUND Range Exceeded EXception",
    "#UD Invalid Opcode Exception",
    "#NM Device Not Available EXception",
    "#DF Double Fault Exception",
    "Coprocessor Segment Overrun",
    "#TS Invalid TSS Exception",
    "#NP Segment Not Present",
    "#SS Stack Fault Exception",
    "#GP General Protection Exception",
    "#PF Page-Fault Exception",
    NULL,   //第十五项是Intel保留项，未使用
    "#MF x87 FPU Floating-Point Error",
    "#AC Alignment Check Exception",
    "#MC Machine-Check Exception",
    "#XF SIMD Floating-Point Exception"
};

gate_desc idt[INTR_CNT];    //中断描述符表
intr_entry intr_handler_table[INTR_CNT]; //中断处理函数地址表

//中断初始化
void intr_init()
{
    //安装idt内所有门描述符
    idt_init();
    put_str("Init IDT successfully!\n");

    //初始化异常处理向量
    exception_init();
    put_str("Init exception successfully!\n");

    //设置8259A 芯片
    pic_init();
    put_str("Init PIC successfully!\n");

    //设置LDTR
    //注意，在C标准中 *未要求* 结构体各个成员之间必须连续分布，在进行4字节对齐后，一个2字节成员后面会有两个字节的空隙，然后才是下一个成员
    //因此此处采取结构体idt48执行lidt指令会出现错误
    //idt48 _idt48 = {sizeof(idt) - 1, (uint32_t)idt};
    uint64_t idt48 = (sizeof(idt) - 1) | ((uint64_t)(uint32_t)idt << 16);
    asm ("lidt %0"::"m"(idt48));
    put_str("Init interrupt successfully!\n");
}

//安装idt内所有门描述符
void idt_init()
{
    for (int i = 0; i < INTR_CNT; i++)
    {
        make_gate_desc(&idt[i], intr_entry_table[i], GCODE_SEL, 0, 0x8e);
    }

    // 安装系统调用的中断处理函数, DPL = 3的中断门
    make_gate_desc(&idt[0x80], syscall_handler, GCODE_SEL, 0, 0xee);
}

 //安装单个门描述符
void make_gate_desc(gate_desc *p_desc, intr_entry offset, uint16_t selector, uint8_t dcount, uint8_t attrib)
{
    p_desc->selector = selector;
    p_desc->dcount = dcount;
    p_desc->attrib = attrib;
    p_desc->offset_low_16_bits = (uint16_t)(uint32_t)offset;
    p_desc->offset_high_16_bits = (uint16_t)((uint32_t)offset >> 16);
}

//初始化可编程中断控制器
//这里设置的是8259A芯片
void pic_init()
{
    //设置主片, 中断号0x20 - 0x27
    outb(0x11, 0x20);   //ICW1
    outb(0x20, 0x21);   //ICW2
    outb(0x04, 0x21);   //ICW3
    outb(0x01, 0x21);   //ICW4

    //设置从片, 注意不要把 a0和a1 写成 20和21 否则会导致时间中断变成一般性保护异常(因为主片初始化了两次，导致时间中断号为0x70, 超出了IDT的界限)
    //中断号修改为 0x28 - 0x2f
    outb(0x11, 0xa0);   //ICW1
    outb(0x28, 0xa1);   //ICW2
    outb(0x02, 0xa1);   //ICW3
    outb(0x01, 0xa1);   //ICW4

    //设置IMR
    outb(0xf8, 0x21);   //主片：允许时钟中断、键盘中断、来自从片的中断   OCW1
    outb(0x3f, 0xa1);   //从片：允许来自IDE0和IDE1的硬盘中断    OCW1
}

//初始化异常处理向量
void exception_init()
{
    for (int i = 0; i < INTR_CNT; i++)
    {
        intr_handler_table[i] = (intr_entry)general_intr_handler;
    }
    intr_handler_table[0x0e] = (intr_entry)PF_handler;
}

void general_intr_handler(uint32_t vec_nr, uint32_t intr_addr)
{
    // IR7和IR15会产生伪中断，应当忽略
    if (vec_nr == 0x27 || vec_nr == 0x2f)
    {
        return;
    }

    uint8_t old_attrib = set_text_attrib(0x0c);   // 设置字体为高亮红
    
    put_str("Exception: 0x"); put_int(vec_nr); put_char('\n');
    put_str(current->pdt_base ? "Process: " : "Thread: "); put_str(current->name);
    put_str("  ID: "); put_int(current->pid); 
    put_str("  Address: "); put_int(intr_addr); put_char('\n');
    put_str(intr_name[vec_nr]); put_char('\n');

    set_text_attrib(old_attrib);

    if (current->pdt_base)
    {
        sys_exit(-1);
    }
    else
    {
        thread_exit();
    }
}

//缺页异常处理函数
void PF_handler(uint32_t vec_nr UNUSED, uint32_t intr_addr)
{
    uint32_t vaddr;
    asm volatile ("movl %%cr2, %0": "=a"(vaddr));   // cr2寄存器中存储了导致缺页故障的虚拟地址

    uint32_t *pde_ptr = (uint32_t *)PDE_PTR((uint32_t)vaddr);
    uint32_t *pte_ptr = (uint32_t *)PTE_PTR((uint32_t)vaddr);

    if ((*pde_ptr & PG_P_1) && (*pte_ptr & PG_P_1) && !(*pte_ptr & PG_RW_W))
    {
        uint32_t ppage = (uint32_t)vaddr2paddr((void *)vaddr);
        uint32_t vpage = vaddr & 0xfffff000;

        /* 处理写保护异常，实现写时复制 */
        if (test_dec_pg_ref(ppage))
        {
            void *buf = get_kernel_pages(1);
            memcpy(buf, (void *)vpage, PAGE_SIZE);

            get_a_page_without_setting_vbitmap((void *)vpage);
            memcpy((void *)vpage, buf, PAGE_SIZE);

            mfree_pages(1, buf);
        }
        else
        {
            (*pte_ptr) |= PG_RW_W;
        }
    }
    else
    {
        uint8_t old_attrib = set_text_attrib(0x0c);   // 设置字体为高亮红
        
        put_str("Exception: 0xe"); put_char('\n');
        put_str(current->pdt_base ? "Process: " : "Thread: "); put_str(current->name);
        put_str("  ID: "); put_int(current->pid); 
        put_str("  Address: "); put_int(intr_addr); put_char('\n');
        put_str(intr_name[0x0e]); put_char('\n');
        put_str("Page-Fault virtual address: 0x"); put_int(vaddr); put_char('\n');

        set_text_attrib(old_attrib);

        if (current->pdt_base)
        {
            sys_exit(-1);
        }
        else
        {
            thread_exit();
        }
    }
    
}                              


// 获取中断状态
intr_status get_intr_status()
{
    return (EFLAGS & EFLAGS_IF) ? INTR_ON : INTR_OFF;
}          

// 设置中断状态
intr_status set_intr_status(intr_status status)
{
    intr_status old_status = get_intr_status();
    if (status == INTR_ON) 
    {
        sti();
    }
    else
    {
        cli();
    }
    return old_status;
}