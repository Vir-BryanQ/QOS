#ifndef __FS_PIPE_H
#define __FS_PIPE_H

#include "stdbool.h"
#include "stdint.h"

#define PIPE_FLAG 0xff

extern bool is_pipe(const uint32_t fd);           // 判断指定描述符是否属于管道描述符
extern uint32_t pipe_read(const uint32_t fd, uint8_t *buf, const uint32_t cnt);    // 从管道中读取cnt个字节到buf中
extern uint32_t pipe_write(const uint32_t fd, uint8_t *buf, const uint32_t cnt);   // 将buf中的cnt个字节写入到管道中
extern int32_t pipe_close(const uint32_t fd);      // 关闭指定管道

#endif