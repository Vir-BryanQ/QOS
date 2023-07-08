#ifndef __USERPROG_EXEC_H
#define __USERPROG_EXEC_H

extern void *load_prog(const char *pathname);       // 加载可执行文件体，成功则返回程序的入口地址，失败则返回NULL

#endif