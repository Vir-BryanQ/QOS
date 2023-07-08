#include "stdio.h"
#include "syscall.h"
#include "global.h"
#include "string.h"
#include "console.h"
#include "file.h"

typedef char *va_list;
#define va_start(ap, v) ap = (va_list)&v
#define va_arg(ap, t) *((t*)(ap += 4))
#define va_end(ap) ap = NULL

uint32_t vsprintf(char *str, const char *format, va_list ap);   // 将格式化字符串format转化为普通字符串str
void itoa(const uint32_t val, char **buf, const uint32_t base);    // 将val转化为base进制的字符串并存储在 *buf 中

// 往屏幕上打印字符串
uint32_t printf(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    char str[1024];    //  存储转化后的字符串
    vsprintf(str, format, ap);
    va_end(ap);
    return write(stdout, str, 1024);
}

// 内核专用的屏幕打印函数
uint32_t printk(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    char str[1024];    //  存储转化后的字符串
    vsprintf(str, format, ap);
    va_end(ap);
    console_put_str(str);
    return strlen(str);
}

// 往buf中传送字符串
uint32_t sprintf(char *buf, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    vsprintf(buf, format, ap);
    va_end(ap);
    return strlen(buf);
}   

// 将格式化字符串format转化为普通字符串str
uint32_t vsprintf(char *str, const char *format, va_list ap)
{
    char *str_idx = str;
    char *format_idx = (char *)format;
    while (*format_idx)
    {
        if (*format_idx != '%')
        {
            *str_idx = *format_idx;
            str_idx++;
            format_idx++;
            continue;
        }
        format_idx++;
        switch (*format_idx)
        {
            case 'x':
            {
                uint32_t arg = va_arg(ap, uint32_t);
                itoa(arg, &str_idx, 16);
                break;
            }
            case 'o':
            {
                uint32_t arg = va_arg(ap, uint32_t);
                itoa(arg, &str_idx, 8);
                break;
            }
            case 'p':
            {
                uint32_t arg = va_arg(ap, uint32_t);
                memset(str_idx, '0', 8);          // 为指针值预留出固定的8字节空间
                str_idx += 8;

                uint32_t tmp = arg;
                do
                {
                    str_idx--;
                } while ((tmp /= 16));

                itoa(arg, &str_idx, 16);
                break;
            }
            case 'd':
            {
                int32_t arg = va_arg(ap, int32_t);
                if (arg < 0)
                {
                    arg *= -1;
                    *str_idx = '-';
                    str_idx++;
                }
                itoa(arg, &str_idx, 10);
                break;
            }
            case 'u':
            {
                uint32_t arg = va_arg(ap, uint32_t);
                itoa(arg, &str_idx, 10);
                break;
            }
            case 's':
            {
                char *arg = va_arg(ap, char *);
                strcpy(str_idx, arg);
                str_idx += strlen(arg);
                break;
            }
            case 'c':
            {
                char ch = va_arg(ap, char);
                *str_idx = ch;
                str_idx++;
                break;
            }
            case '%':
            {
                *str_idx = '%';
                str_idx++;
                break;
            }
            default:
            {
                break;
            }
        }
        format_idx++;
    }
    *str_idx = 0;
    return strlen(str);
}  

// 将val转化为base进制的字符串并存储在 *buf 中
void itoa(const uint32_t val, char **buf, const uint32_t base)
{
    uint32_t i = val / base;    
    uint32_t m = val % base;
    if (i)
    {
        itoa(i, buf, base);
    }
    if (m < 10)
    {
        *(*buf) = m + '0';
    }
    else
    {
        *(*buf) = m - 10 + 'a';
    }
    (*buf)++;
}  