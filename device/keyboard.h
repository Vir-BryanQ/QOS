#ifndef __DEVICE_KEYBOARD_H
#define __DEVICE_KEYBOARD_H

#include "ioqueue.h"

extern ioqueue kb_buf;  // 环形缓冲区

extern void keyboard_init(void);

#endif