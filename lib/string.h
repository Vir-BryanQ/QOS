#ifndef __USER_STRING_H
#define __USER_STRING_H

#include "stdint.h"

extern void memset(void *dst, const uint8_t val, uint32_t cnt);    // 将dst处的cnt个字节的值设置为val
extern void memcpy(void *dst, const void *src, uint32_t cnt);       // 将src处的cnt个字节复制到dst处
extern int8_t memcmp(const void *a, const void *b, uint32_t cnt);   // 比较a和b处起的cnt个字节，a == b返回0， a > b返回1, a < b返回-1

extern char *strcpy(char *dst, const char *src);       // 将字符串从src复制到dst
extern uint32_t strlen(const char *str);                 // 获取字符串str的长度
extern int8_t strcmp(const char *a, const char *b);  // a == b返回0， a > b返回1, a < b返回-1
extern char *strchr(const char *str, const uint8_t ch);    // 在str中查找并返回首个字符ch的指针，查找失败时返回NULL
extern char *strrchr(const char *str, const uint8_t ch);   // 从后往前查找ch
extern char *strcat(char *dst, const char *src);           // 将字符串src拼接到dst中， 并返回dst
extern uint32_t strchrs(const char *str, const uint8_t ch);    // 在str中查找ch并返回ch出现的次数

#endif