#ifndef __LIB_KERNEL_BITMAP_H
#define __LIB_KERNEL_BITMAP_H

#include "stdint.h"
#include "stdbool.h"

typedef struct bitmap
{
    uint32_t bytes_length;
    uint8_t *btmp_ptr;
} bitmap;

extern void bitmap_init(bitmap *btmp);  // 将位图中所有的位清零
extern bool bitmap_test(bitmap *btmp, uint32_t bit_idx);    // 返回指定位的状态
extern void bitmap_set(bitmap *btmp, uint32_t bit_idx, const uint8_t val);      // 将指定位置为val(0或1)
extern int32_t bitmap_scan(bitmap *btmp, uint32_t cnt);        // 在bitmap中申请连续cnt个位, 并返回首个位的位索引

#endif