#include "print.h"
#include "sync.h"
#include "console.h"

mutex_lock console_lock;    // 用于实现控制台输出同步的互斥锁
mutex_lock cursor_lock;     // 实现光标寄存器的互斥访问
mutex_lock text_attrib_lock;   // 实现文本属性字节的互斥访问

void console_init(void)
{
    mutex_lock_init(&console_lock);
    mutex_lock_init(&text_attrib_lock);
    mutex_lock_init(&cursor_lock);
    put_str("Init console successfully!\n");
}

// 字符输出的同步版本
void console_put_char(uint8_t ch)
{
    mutex_lock_acquire(&console_lock);
    put_char(ch);
    mutex_lock_release(&console_lock);
}

void console_put_str(const char *str)
{
    mutex_lock_acquire(&console_lock);
    put_str(str);
    mutex_lock_release(&console_lock);
}

void console_put_int(uint32_t val)
{
    mutex_lock_acquire(&console_lock);
    put_int(val);
    mutex_lock_release(&console_lock);
}


// 光标和属性相关函数的同步版本
uint16_t console_get_cursor(void)
{
    mutex_lock_acquire(&cursor_lock);
    uint16_t cursor_pos = get_cursor();
    mutex_lock_release(&cursor_lock);
    return cursor_pos;
}

uint16_t console_set_cursor(uint16_t pos)
{
    mutex_lock_acquire(&cursor_lock);
    uint16_t old_pos = set_cursor(pos);
    mutex_lock_release(&cursor_lock);
    return old_pos;
}

uint16_t console_get_text_attrib(void)
{
    mutex_lock_acquire(&text_attrib_lock);
    uint8_t text_attrib = get_text_attrib();
    mutex_lock_release(&text_attrib_lock);
    return text_attrib;
}

uint8_t console_set_text_attrib(uint8_t attrib)
{
    mutex_lock_acquire(&text_attrib_lock);
    uint8_t old_attrib = set_text_attrib(attrib);
    mutex_lock_release(&text_attrib_lock);
    return old_attrib;
}

