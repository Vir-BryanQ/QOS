#ifndef __DEVICE_CONSOLE_H
#define __DEVICE_CONSOLE_H

#include "stdint.h"

extern void console_init(void);

// 字符输出的同步版本
extern void console_put_char(uint8_t);
extern void console_put_str(const char *);
extern void console_put_int(uint32_t);

// 光标和属性相关函数的同步版本
extern uint16_t console_get_cursor(void);
extern uint16_t console_set_cursor(uint16_t);
extern uint16_t console_get_text_attrib(void);
extern uint8_t console_set_text_attrib(uint8_t);

#endif