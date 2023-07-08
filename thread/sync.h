#ifndef __THREAD_SYNC_H
#define __THREAD_SYNC_H

#include "stdint.h"
#include "list.h"

// 信号量结构体
typedef struct semaphore
{
    int32_t value;           // value >= 0时表示某种资源的数量，value <= 0时其绝对值表示阻塞在该信号量上线程的数量
    list block_queue;       // 阻塞在该信号量上的线程队列
} semaphore;

extern void sem_init(semaphore *psem, const int32_t value);  // 信号量初始化
extern void sem_down(semaphore *psem);  // 信号量的P操作(原子操作)
extern void sem_up(semaphore *psem);    // 信号量的V操作(原子操作)

typedef struct task_struct task_struct;         // 为了避免头文件嵌套包含出现的未定义问题，需要将thread.h放在原文件中，同时加上前置声明

// 互斥锁结构体
typedef struct mutex_lock
{
    task_struct *holder;    // 锁的持有线程
    semaphore sem;          // 互斥锁是对互斥信号量的封装
    uint32_t acquire_nr;    // 锁的持有者申请锁的总次数，可用于避免死锁
} mutex_lock;

extern void mutex_lock_init(mutex_lock *plock);     // 锁的初始化
extern void mutex_lock_acquire(mutex_lock *plock);  // 获取锁
extern void mutex_lock_release(mutex_lock *plock);  // 释放锁

#endif