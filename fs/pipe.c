#include "pipe.h"
#include "thread.h"
#include "file.h"
#include "ioqueue.h"
#include "_syscall.h"

// 判断指定描述符是否属于管道描述符
bool is_pipe(const uint32_t fd)
{
    return ((current->fd_table[fd] != -1) ? (file_table[current->fd_table[fd]].flag == PIPE_FLAG) : false);
}   

// 从管道中读取cnt个字节到buf中
uint32_t pipe_read(const uint32_t fd, uint8_t *buf, const uint32_t cnt)
{
    for (uint32_t i = 0; i < cnt; ++i)
    {
        buf[i] = ioqueue_pop_front((ioqueue *)file_table[current->fd_table[fd]].p_inode);
    }
    return cnt;
}   

// 将buf中的cnt个字节写入到管道中
uint32_t pipe_write(const uint32_t fd, uint8_t *buf, const uint32_t cnt)
{
    for (uint32_t i = 0; i < cnt; ++i)
    {
        ioqueue_push_back((ioqueue *)file_table[current->fd_table[fd]].p_inode, buf[i]);
    }
    return cnt;
}   

// 关闭指定管道
int32_t pipe_close(const uint32_t fd)
{
    int32_t g_idx = current->fd_table[fd];
    current->fd_table[fd] = -1;

    if (!(--file_table[g_idx].f_pos))
    {
        mfree_pages(1, ((ioqueue *)file_table[g_idx].p_inode)->buffer);
        sys_free(file_table[g_idx].p_inode);
        file_table[g_idx].p_inode = NULL;
    }

    return 0;
}    