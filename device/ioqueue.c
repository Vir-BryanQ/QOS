#include "ioqueue.h"

void ioqueue_init(ioqueue *pioqueue, uint8_t *buffer, uint32_t buf_size)
{
    sem_init(&pioqueue->full, 0);
    sem_init(&pioqueue->empty, buf_size);
    mutex_lock_init(&pioqueue->mutex);

    pioqueue->buffer = buffer;
    pioqueue->buf_size = buf_size;

    pioqueue->head = 0;
    pioqueue->tail = 0;
}

// 入队，写缓冲区
void ioqueue_push_back(ioqueue *pioqueue, const uint8_t val)
{
    sem_down(&pioqueue->empty);
    mutex_lock_acquire(&pioqueue->mutex);

    pioqueue->buffer[pioqueue->tail] = val;
    pioqueue->tail = (pioqueue->tail + 1) % pioqueue->buf_size;

    mutex_lock_release(&pioqueue->mutex);
    sem_up(&pioqueue->full);
}   

// 出队， 读缓冲区
uint8_t ioqueue_pop_front(ioqueue *pioqueue)
{
    sem_down(&pioqueue->full);
    mutex_lock_acquire(&pioqueue->mutex);

    uint8_t val = pioqueue->buffer[pioqueue->head];
    pioqueue->head = (pioqueue->head + 1) % pioqueue->buf_size;

    mutex_lock_release(&pioqueue->mutex);
    sem_up(&pioqueue->empty);

    return val;
}        