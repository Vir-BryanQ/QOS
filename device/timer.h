#ifndef __DEVICE_TIMER_H
#define __DEVICE_TIMER_H

#include "stdint.h"

extern uint32_t ticks;                 //从系统开中断开始到当前时刻总共发生的时钟中断数 

extern void timer_init(void);   //初始化8253定时计数器
extern void sleep_ms(const uint32_t m_seconds);      // 使当前线程睡眠m_seconds毫秒

#endif