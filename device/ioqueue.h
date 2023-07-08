#ifndef __DEVICE_IOQUEUE_H
#define __DEVICE_IOQUEUE_H

#include "stdint.h"
#include "sync.h"

// 环形线性队列
typedef struct ioqueue
{
    // 实现入队和出队多线程同步和互斥的相关信号量和锁
    semaphore full;     // 队列中满槽位的数量
    semaphore empty;    // 队列中空槽位的数量
    mutex_lock mutex;   // 缓冲区互斥锁

    uint8_t *buffer;            // 缓冲区指针
    uint32_t buf_size;          // 缓冲区大小
    uint32_t head;              // 队头指针，用于读出数据
    uint32_t tail;              // 队尾指针，用于写入数据
} ioqueue;

extern void ioqueue_init(ioqueue *pioqueue, uint8_t *buffer, uint32_t buf_size);
extern void ioqueue_push_back(ioqueue *pioqueue, const uint8_t val);    // 入队，写缓冲区
extern uint8_t ioqueue_pop_front(ioqueue *pioqueue);        // 出队， 读缓冲区

#endif