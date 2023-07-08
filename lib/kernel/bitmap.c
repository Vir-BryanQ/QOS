#include "bitmap.h"
#include "string.h"
#include "debug.h"
#include "global.h"
#include "stdint.h"

#define BTMP_MASK 1

// 将位图中所有的位清零
void bitmap_init(bitmap *btmp)
{
    ASSERT(btmp != NULL);
    memset(btmp->btmp_ptr, 0, btmp->bytes_length);
}

// 返回指定位的状态
bool bitmap_test(bitmap *btmp, uint32_t bit_idx)
{
    ASSERT(btmp != NULL && bit_idx < (btmp->bytes_length << 3));
    uint32_t byte_idx = bit_idx / 8;
    uint32_t bit_off = bit_idx % 8;
    return (btmp->btmp_ptr[byte_idx] & (uint8_t)(BTMP_MASK << bit_off)) ? true : false;
}   

// 将指定位置为val(0或1)
void bitmap_set(bitmap *btmp, uint32_t bit_idx, const uint8_t val)
{
    ASSERT(btmp != NULL && bit_idx < (btmp->bytes_length << 3) && (val == 0 || val == 1));
    uint32_t byte_idx = bit_idx / 8;
    uint32_t bit_off = bit_idx % 8;
    if (val) btmp->btmp_ptr[byte_idx] |= (BTMP_MASK << bit_off);  // 置1
    else    btmp->btmp_ptr[byte_idx] &= ~(BTMP_MASK << bit_off);  // 置0
}   

// 在bitmap中申请连续cnt个位, 并返回首个位的位索引, 找不到则返回-1
int32_t bitmap_scan(bitmap *btmp, uint32_t cnt)
{
    ASSERT(btmp != NULL && cnt <= (btmp->bytes_length << 3));

    // 逐字节扫描，找到第一个非0xff的字节
    uint32_t byte_idx = 0;
    while (byte_idx < btmp->bytes_length && btmp->btmp_ptr[byte_idx] == 0xff) byte_idx++;

    if (byte_idx == btmp->bytes_length) return -1;

    // 在查找到的字节中查找第一个为0的位
    uint32_t bit_idx = byte_idx << 3;
    while (bitmap_test(btmp, bit_idx)) bit_idx++; 
    
    // 从第一个为0的位出发寻找连续的cnt个值为0的位
    bit_idx++;
    uint32_t _cnt = 1;
    while (_cnt < cnt && bit_idx < (btmp->bytes_length << 3))
    {
        if (bitmap_test(btmp, bit_idx))
        {
            _cnt = 0;
            bit_idx++;
            while (bitmap_test(btmp, bit_idx) && bit_idx < (btmp->bytes_length << 3)) bit_idx++; 
            if (bit_idx == btmp->bytes_length << 3) return -1;
        }
        _cnt++;
        bit_idx++;
    }

    if (_cnt == cnt)
    {
        bit_idx--;
        while (_cnt)
        {
            bitmap_set(btmp, bit_idx, 1);
            bit_idx--;
            _cnt--;
        }
        bit_idx++;
        return (int32_t)bit_idx;
    }
    else
    {
        return -1;
    }
}
