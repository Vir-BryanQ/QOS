#include "interrupt.h"
#include "print.h"
#include "global.h"
#include "debug.h"

// 输出错误信息并进入自旋状态
void panic_spin(const char *filename, const int line, const char *func, const char *condition)
{
    set_intr_status(INTR_OFF);

    // 清出六行的空间
    set_cursor(0);
    for (int i = 0; i < 480; i++)
    {
        put_char(0);
    }

    set_text_attrib(0x0c);   // 设置字体为高亮红
    set_cursor(0);
    put_str("! ! ! ! ! panic ! ! ! ! !\n");
    put_str("File: "); put_str(filename); put_char('\n');
    put_str("Line: 0x"); put_int(line); put_char('\n');
    put_str("Function: "); put_str(func); put_char('\n');
    put_str("Condition: "); put_str(condition); put_char('\n');

    hlt();
}