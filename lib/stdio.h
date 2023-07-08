#ifndef __LIB_STDIO_H
#define __LIB_STDIO_H

#include "stdint.h"

extern uint32_t printf(const char *format, ...);                // 往屏幕上打印字符串
extern uint32_t sprintf(char *buf, const char *format, ...);    // 往buf中传送字符串

extern uint32_t printk(const char *format, ...);                     // 内核专用的屏幕打印函数

#endif