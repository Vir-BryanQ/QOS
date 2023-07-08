#include "string.h"
#include "debug.h"
#include "global.h"

// 将dst处的cnt个字节的值设置为val
void memset(void *dst, const uint8_t val, uint32_t cnt)
{
    ASSERT(dst != NULL);
    uint8_t *_dst = (uint8_t *)dst;
    while (cnt)
    {
        *_dst = val;
        cnt--;
        _dst++;
    }
}

// 将src处的cnt个字节复制到dst处
void memcpy(void *dst, const void *src, uint32_t cnt)
{
    ASSERT(dst != NULL && src != NULL);
    uint8_t *_dst = (uint8_t *)dst;
    uint8_t *_src = (uint8_t *)src;
    while (cnt)
    {
        *_dst = *_src;
        cnt--;
        _dst++;
        _src++;
    }
} 

// 比较a和b处起的cnt个字节，a == b返回0， a > b返回1, a < b返回-1
int8_t memcmp(const void *a, const void *b, uint32_t cnt)
{
    ASSERT(a != NULL && b != NULL);
    uint8_t *_a = (uint8_t *)a;
    uint8_t *_b = (uint8_t *)b;
    while (cnt)
    {
        if (*_a != *_b)
        {
            return ((*_a > *_b) ? 1 : -1);
        }
        cnt--;
        _a++;
        _b++;
    }
    return 0;
}

// 将字符串从src复制到dst
char *strcpy(char *dst, const char *src)
{
    ASSERT(dst != NULL && src != NULL);
    char *tmp = dst;
    while ((*dst = *src))
    {
        dst++;
        src++;
    }
    return tmp;
}

// 获取字符串str的长度
uint32_t strlen(const char *str)
{
    ASSERT(str != NULL);
    uint32_t len = 0;
    while (*str) 
    {
        str++;
        len++;
    }
    return len;
}        

// a == b返回0， a > b返回1, a < b返回-1
int8_t strcmp(const char *a, const char *b)
{
    ASSERT(a != NULL && b != NULL);
    while (*a != 0 )
    {
        if (*a != *b)
        {
            return ((*a > *b) ? 1 : -1);
        }
        a++;
        b++;
    }
    return ((*a == *b) ? 0 : -1);
}

// 在str中查找并返回首个字符ch的指针，查找失败时返回NULL
char *strchr(const char *str, const uint8_t ch)
{
    ASSERT(str != NULL && ch != 0);
    while (*str)
    {
        if (*str == ch)
        {
            return (char *)str;
        }
        str++;
    }
    return NULL;
}  

// 从后往前查找ch
char *strrchr(const char *str, const uint8_t ch)
{
    ASSERT(str != NULL && ch != 0);
    char *last_ptr = NULL;
    while (*str)
    {
        if (*str == ch)
        {
            last_ptr = (char *)str;
        }
        str++;
    }
    return last_ptr;
}

// 将字符串src拼接到dst中， 并返回dst
char *strcat(char *dst, const char *src)
{
    ASSERT(dst != NULL && src != NULL);
    char *tmp = dst;
    while (*dst) dst++;
    while ((*dst = *src))
    {
        dst++;
        src++;
    }

    return tmp;
}

// 在str中查找ch并返回ch出现的次数
uint32_t strchrs(const char *str, const uint8_t ch)
{
    ASSERT(str != NULL && ch != 0);
    uint32_t cnt = 0;
    while (*str)
    {
        if (*str == ch) cnt++;
        str++;
    }
    return cnt;
} 
