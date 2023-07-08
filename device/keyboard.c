#include "stdint.h"
#include "interrupt.h"
#include "io.h"
#include "stdbool.h"
#include "ioqueue.h"
#include "print.h"
#include "keyboard.h"

// 字符控制键当成转义字符处理
#define backspace '\b'
#define enter '\r'
#define delete '\x7f'
#define esc '\x1b'
#define tab '\t'

// 操作控制建当做空字符处理，仅占位
#define ctrl_l 0
#define ctrl_r 0
#define alt_l 0
#define alt_r 0
#define shift_l 0
#define shift_r 0
#define capslk 0

// 操作控制键对应的扫描码
#define shift_l_make 0x2a
#define shift_r_make 0x36
#define alt_l_make 0x38
#define alt_r_make 0xe038
#define ctrl_l_make 0x1d
#define ctrl_r_make 0xe01d
#define capslk_make 0x3a 

#define KEY_NR 0x3b     // 目前仅支持主键盘上的键

void intr_keyboard_handler(void);    // 键盘中断处理程序

#define CHAR_BUF_SIZE 64

ioqueue kb_buf;                     // 键盘输入缓冲区
uint8_t char_buf[CHAR_BUF_SIZE];    // 键盘字符缓冲区

// 键盘通码与ascii码之间的映射表, 暂时只处理主键盘上的键
static uint8_t key_map[KEY_NR][2] = 
{
    /* 0x00 */      {0, 0},
    /* 0x01 */      {esc, esc},
    /* 0x02 */      {'1', '!'},
    /* 0x03 */      {'2', '@'},
    /* 0x04 */      {'3', '#'},
    /* 0x05 */      {'4', '$'},
    /* 0x06 */      {'5', '%'},
    /* 0x07 */      {'6', '^'},
    /* 0x08 */      {'7', '&'},
    /* 0x09 */      {'8', '*'},
    /* 0x0a */      {'9', '('},
    /* 0x0b */      {'0', ')'},
    /* 0x0c */      {'-', '_'},
    /* 0x0d */      {'=', '+'},
    /* 0x0e */      {backspace, backspace},
    /* 0x0f */      {tab, tab},
    /* 0x10 */      {'q', 'Q'},
    /* 0x11 */      {'w', 'W'},
    /* 0x12 */      {'e', 'E'},
    /* 0x13 */      {'r', 'R'},
    /* 0x14 */      {'t', 'T'},
    /* 0x15 */      {'y', 'Y'},
    /* 0x16 */      {'u', 'U'},
    /* 0x17 */      {'i', 'I'},
    /* 0x18 */      {'o', 'O'},
    /* 0x19 */      {'p', 'P'},
    /* 0x1a */      {'[', '{'},
    /* 0x1b */      {']', '}'},
    /* 0x1c */      {enter, enter},
    /* 0x1d */      {ctrl_l, ctrl_l},
    /* 0x1e */      {'a', 'A'},
    /* 0x1f */      {'s', 'S'},
    /* 0x20 */      {'d', 'D'},
    /* 0x21 */      {'f', 'F'},
    /* 0x22 */      {'g', 'G'},
    /* 0x23 */      {'h', 'H'},
    /* 0x24 */      {'j', 'J'},
    /* 0x25 */      {'k', 'K'},
    /* 0x26 */      {'l', 'L'},
    /* 0x27 */      {';', ':'},
    /* 0x28 */      {'\'', '"'},
    /* 0x29 */      {'`', '~'},
    /* 0x2a */      {shift_l, shift_l},
    /* 0x2b */      {'\\', '|'},
    /* 0x2c */      {'z', 'Z'},
    /* 0x2d */      {'x', 'X'},
    /* 0x2e */      {'c', 'C'},
    /* 0x2f */      {'v', 'V'},
    /* 0x30 */      {'b', 'B'},
    /* 0x31 */      {'n', 'N'},
    /* 0x32 */      {'m', 'M'},
    /* 0x33 */      {',', '<'},
    /* 0x34 */      {'.', '>'},
    /* 0x35 */      {'/', '?'},
    /* 0x36 */      {shift_r, shift_r},
    /* 0x37 */      {'*', '*'},
    /* 0x38 */      {alt_l, alt_l},
    /* 0x39 */      {' ', ' '},
    /* 0x3a */      {capslk, capslk}
};

uint16_t ext_code = 0;          // 在高字节存储扫描码前缀
bool alt_status = false;        // 表征alt键是否按下
bool ctrl_status = false;       // 表征ctrl键是否按下
bool shift_status = false;      // 表征shift是否按下
bool capslk_status = false;     // 表征大写锁定是否开启

void keyboard_init(void)
{
    intr_handler_table[0x21] = (intr_entry)intr_keyboard_handler;       // 注册键盘中断处理程序
    ioqueue_init(&kb_buf, char_buf, CHAR_BUF_SIZE);                     // 初始化键盘输入缓冲区
    put_str("Init keyboard successfully!\n");
}

// 键盘中断处理程序
void intr_keyboard_handler(void)
{
    uint16_t scan_code = (uint16_t)inb(0x60);    // 0x60端口读取键盘扫描码

    if (scan_code == 0x00e0)
    {
        // 0xe0是扫描码前缀，说明该扫描码有两个字节，应立即返回以处理下一字节
        ext_code = scan_code;
        ext_code <<= 8; // e000
        return;
    }

    if (ext_code == 0xe000)
    {
        scan_code |= ext_code;  // 将扫描码前缀拼接到高字节形成完整的扫描码
        ext_code = 0;
    }

    uint16_t make_code = scan_code & 0xff7f;    // 如果是断码，转为通码，方便处理

    if (!(make_code >= 0 && make_code <= 0x3a) || make_code == alt_r_make || make_code == ctrl_r_make)
    {
        // 附加键和小键盘暂时不处理
        return;
    }


    if (scan_code & 0x0080)
    {
        // 断码
        if (make_code == alt_l_make || make_code == alt_r_make)
        {
            alt_status = false;
            return;
        }
        else if (make_code == shift_l_make || make_code == shift_r_make)
        {
            shift_status = false;
            return;
        }
        else if (make_code == ctrl_l_make || make_code == ctrl_r_make)
        {
            ctrl_status = false;
            return;
        }
    }
    else
    {
        // 通码

        // 操作控制键的处理
        if (scan_code == alt_l_make || scan_code == alt_r_make)
        {
            alt_status = true;
            return;
        }
        else if (scan_code == shift_l_make || scan_code == shift_r_make)
        {
            shift_status = true;
            return;
        }
        else if (scan_code == ctrl_l_make || scan_code == ctrl_r_make)
        {
            ctrl_status = true;
            return;
        }
        else if (scan_code == capslk_make)
        {
            capslk_status = !capslk_status;
            return;
        }

        // 字符控制键和字符键的处理
        uint32_t sel = (((scan_code >= 0x02 && scan_code <= 0x0d) || (scan_code == 0x29) || \
                        (scan_code == 0x1a) || (scan_code == 0x1b) || (scan_code == 0x2b) || \
                        (scan_code == 0x27) || (scan_code == 0x28) || (scan_code == 0x33) || \
                        (scan_code == 0x34) || scan_code == 0x35) ? shift_status : (shift_status ^ capslk_status));
        uint8_t ch = key_map[scan_code & 0x00ff][sel];

        // 添加对快捷键 ctrl + l 和 ctrl + u 的处理
        if (ctrl_status)
        {
            switch (ch)
            {
                case 'l':
                case 'u':
                case 'd':
                {
                    ch -= 'a';
                    break;
                }
            }
        }

        ioqueue_push_back(&kb_buf, ch);
    }

}