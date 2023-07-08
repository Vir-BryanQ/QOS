#ifndef __LLB_KERNEL_STDBOOL_H
#define __LIB_KERNEL_STDBOOL_H

#define bool unsigned int
#define false 0
#define true 1              // 曾经误写为0，导致内存管理模块出错

#endif