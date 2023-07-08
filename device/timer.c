#include "io.h"
#include "print.h"
#include "interrupt.h"
#include "thread.h"
#include "debug.h"
#include "timer.h"

#define TIMER_CLK_FREQ 1193180  //8253系列定时计数器的CLK引脚频率
#define HZ 100      //每秒产生的时钟中断次数， 相当于每10ms产生一次时钟中断
#define TIMER_START_VAL  (TIMER_CLK_FREQ/HZ)  //计数初值 == 11931
#define MS_PER_TICK  (1000/HZ)           //每个tick对应的毫秒数

#define TIMER_WORK_MODE  0x34   //工作方式：计数器0， 16位计数初值， 方式2--分频脉冲， 二进制计数

uint32_t ticks = 0;         //从系统开中断开始到当前时刻总共发生的时钟中断数

void intr_timer_handler(void);     // 时钟中断处理函数
void sleep_ticks(const uint32_t timing_ticks);      // 使当前线程睡眠timing_ticks个tick

//初始化8253定时计数器
void timer_init(void)
{
    // 注意不要把计数初值写成TIMER_CLK_FREQ了，这样的话产生的时钟中断频率是错误的(虽然没有非常明显的区别)
    outb(TIMER_WORK_MODE, 0x43);
    outb((uint8_t)TIMER_START_VAL, 0x40);        //低字节
    outb((uint8_t)(TIMER_START_VAL >> 8), 0x40); //高字节

    intr_handler_table[0x20] = (intr_entry)intr_timer_handler;      // 在中断处理函数表中注册时钟中断处理函数

    put_str("Init timer successfully!\n");
}

// 时钟中断处理函数
void intr_timer_handler(void)
{
    //判断内核栈是否溢出
    ASSERT(current->magic == MAGIC);

    ticks++;
    current->ticks--;
    current->elapsed_ticks++;
    if ((int32_t)current->ticks <= 0)
    {
        current->ticks = 0;
        // 时间片用完时需要线程调度
        schedule();
    }
}

// 使当前线程睡眠timing_ticks个tick
void sleep_ticks(const uint32_t timing_ticks)
{
    uint32_t start_ticks = ticks;
    while ((ticks - start_ticks) < timing_ticks)
    {
        thread_yield();
    }
}     

// 使当前线程睡眠m_seconds毫秒
void sleep_ms(const uint32_t m_seconds)
{
    // 在Bochs模拟器中，时钟速度比现实世界快将近20倍，如果要使模拟器时钟与现实世界相同，可以在配置文件中添加 clock: sync=realtime
    // 这样的话时钟就和现实同步了，但是Bochs的运行速度也会下降
    sleep_ticks((m_seconds + MS_PER_TICK - 1) / MS_PER_TICK);       // 为保证可靠性，睡眠毫秒数转化为ticks数时要向上取整
}   
