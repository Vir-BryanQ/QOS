#ifndef __KERNEL_DEBUG_H
#define __KERNEL_DEBUG_H

// 输出错误信息并进入自旋状态
extern void panic_spin(const char *filename, const int line, const char *func, const char *condition);

#ifdef NDEBUG
    #define ASSERT(CONDITION) ((void)0) 
#else
    #define ASSERT(CONDITION) if (!(CONDITION)) panic_spin(__FILE__, __LINE__, __func__, #CONDITION)
#endif

#endif
