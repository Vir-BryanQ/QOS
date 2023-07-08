#ifndef __KERNEL_IO_H
#define __KERNEL_IO_H

#include "stdint.h"

// 向port端口写入8位数据data
static inline void outb(const uint8_t data, const uint16_t port)
{
    asm ("outb %b0, %w1"::"a"(data), "d"(port));
}

// 从port端口读取一个字节并返回
static inline uint8_t inb(const uint16_t port)
{
    uint8_t data;
    asm ("inb %w1, %b0":"=a"(data): "d"(port));
    return data;
}

// 将内存src处的word_cnt个16位数据连续写入port端口
static inline void outsw(void *src, const uint16_t port, const uint32_t word_cnt)
{
    asm ("cld; rep outsw"::"S"(src), "d"(port), "c"(word_cnt));
}

// 从port端口中读取word_cnt个16位数据到内存dst处
static inline void insw(const uint16_t port, void *dst, const uint32_t word_cnt)
{
    asm ("cld; rep insw"::"d"(port), "D"(dst), "c"(word_cnt));
}

#endif