#ifndef __LIB_KERNEL_PRINT_H
#define __LIB_KERNEL_PRINT_H

#include "stdint.h"

extern void put_char(uint8_t);
extern void put_str(const char *);
extern void put_int(uint32_t);
extern uint16_t get_cursor(void);
extern uint16_t set_cursor(uint16_t);
extern uint16_t get_text_attrib(void);
extern uint8_t set_text_attrib(uint8_t);

#endif