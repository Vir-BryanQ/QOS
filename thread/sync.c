#include "sync.h"
#include "thread.h"
#include "interrupt.h"
#include "global.h"
#include "debug.h"

// 信号量初始化
void sem_init(semaphore *psem, const int32_t value)
{
    pthread_status old_status = set_intr_status(INTR_OFF);

    ASSERT(psem != NULL);
    psem->value = value;
    list_init(&psem->block_queue);

    set_intr_status(old_status);
}  

// 信号量的P操作(原子操作)
void sem_down(semaphore *psem)
{
    pthread_status old_status = set_intr_status(INTR_OFF);

    ASSERT(psem != NULL);
    psem->value--;
    if (psem->value < 0)
    {
        // 没有资源时应当进入阻塞队列阻塞等待
        ASSERT(!list_find(&psem->block_queue, &current->general_list_node));
        list_push_back(&psem->block_queue, &current->general_list_node); 
        thread_block(TASK_BLOCKED);
    }

    set_intr_status(old_status);
}  

// 信号量的V操作(原子操作)
void sem_up(semaphore *psem)
{
    pthread_status old_status = set_intr_status(INTR_OFF);

    ASSERT(psem != NULL);
    psem->value++;
    if (psem->value <= 0)
    {
        // 如果阻塞队列有至少一个线程在等待，将该队列的队头线程唤醒
        thread_unblock(member2struct(list_pop_front(&psem->block_queue), task_struct, general_list_node));
    }

    set_intr_status(old_status);
}  

// 锁的初始化
void mutex_lock_init(mutex_lock *plock)
{
    pthread_status old_status = set_intr_status(INTR_OFF);

    ASSERT(plock != NULL);
    plock->holder = NULL;
    plock->acquire_nr = 0;
    sem_init(&plock->sem, 1);

    set_intr_status(old_status);
}

// 获取锁     
void mutex_lock_acquire(mutex_lock *plock)
{
    pthread_status old_status = set_intr_status(INTR_OFF);

    ASSERT(plock != NULL);
    if (plock->holder != current)
    {
        // 若当前未持有锁，则尝试获取获取锁
        sem_down(&plock->sem);
        plock->holder = current;
        ASSERT(plock->acquire_nr == 0);
        plock->acquire_nr = 1;
    }
    else
    {
        // 若已经持有锁，则不应该继续尝试获取锁，否则会造成死锁
        ASSERT(plock->acquire_nr >= 1);
        plock->acquire_nr++;
    }

    set_intr_status(old_status);
}  

// 释放锁, 如果释放锁的操作不是原子的，那么如果在plock->holder = NULL; 执行完之后发生了时钟中断，
// 时钟中断中某些获取锁的操作可能会导致死锁，因为sem_up 和 plock->holder = NULL 没有一次性执行完毕
void mutex_lock_release(mutex_lock *plock)
{
    pthread_status old_status = set_intr_status(INTR_OFF);

    ASSERT(plock != NULL && plock->holder == current);
    ASSERT(plock->acquire_nr >= 1);

    // 申请了多少次锁，就得释放多少次锁
    plock->acquire_nr--;
    if (plock->acquire_nr > 0)
    {
        set_intr_status(old_status);
        return;
    }
    // 只有最后一次释放锁才会真正释放该锁
    ASSERT(plock->acquire_nr == 0);
    plock->holder = NULL;
    sem_up(&plock->sem);

    set_intr_status(old_status);
}  

