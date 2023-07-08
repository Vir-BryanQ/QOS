#ifndef __LIB_USER_SYSCALL_H
#define __LIB_USER_SYSCALL_H

#include "stdint.h"

typedef struct dir dir;
typedef struct dentry dentry;
struct stat;

extern uint32_t getpid(void);             // 获取调用者的pid
extern int32_t write(const uint32_t fd, const void *buf, uint32_t cnt); // 将buf处起始的cnt个字节写入fd指向的文件中，成功则返回写入的字节数，失败返回-1
extern void *malloc(const uint32_t size);   // 请求在用户堆空间中分配size字节的内存空间，分配成功则返回该内存空间的首虚拟地址，否则返回NULL
extern void free(void *ptr);                // 释放ptr指向的内存块
extern int32_t open(const char* pathname, const uint8_t flag);    // 按照指定方式打开文件, 成功返回对应的文件描述符，失败返回-1
extern int32_t close(const uint32_t fd);     // 关闭fd指向的文件
extern int32_t read(const uint32_t fd, void *buf, const uint32_t cnt);  // 从fd指向的文件中读取cnt个字节到内存中的buf处, 成功返回读取的字节数，失败返回-1
extern int32_t lseek(const uint32_t fd, const int32_t offset, const uint8_t whence); // 将文件指针定位到指定偏移处, 成功返回新的文件指针，失败返回-1
extern int32_t unlink(const char *pathname); // 删除指定路径上的文件，成功返回0，失败返回-1
extern int32_t mkdir(const char *pathname);  // 创建一个空目录，成功返回0，失败返回-1
extern dir *opendir(const char *pathname);   // 打开指定目录，成功返回对应的目录指针，失败返回NULL
extern int32_t closedir(dir *pdir); // 关闭指定目录，成功返回0，失败返回-1
extern dentry *readdir(dir *pdir); // 从指定目录中读取一个目录项，成功返回目录项指针，失败返回NULL
extern int32_t rmdir(const char* pathname); // 删除指定空目录，成功返回0，失败返回-1
extern void rewinddir(dir *pdir); // 将目录结构的d_pos置零
extern char *getcwd(char *buf, uint32_t size); // 获取当前工作目录的绝对路径
extern int32_t chdir(const char *pathname); // 将当前工作路径改到指定路径, 成功返回0， 失败返回-1
extern int32_t stat(const char *pathname, struct stat *buf); // 读取指定路径上的文件的信息，成功后存储在buf中并返回0，若失败则返回-1
extern int32_t mount(const char *source, const char *target); // 将指定分区上的文件系统挂载到指定路径, 成功返回0， 失败返回-1
extern int32_t umount(const char *target);  // 卸载指定挂载点上的文件系统, 成功返回0， 失败返回-1
extern int32_t fork(void);  // 创建一个相同的子进程, 成功则父进程返回子进程pid，子进程返回0，失败则返回-1
extern void putchar(char ch);   // 往屏幕上打印一个字符
extern void clear(void);    // 清除屏幕上的所有内容并使光标复位
extern void ps(void);       // 列出所有进程的信息和状态
extern int32_t execv(const char *pathname, char *argv[]);   // 用指定路径上的可执行文件体替换当前进程的进程体，若成功则转到入口地址开始运行，若失败则返回-1
extern void exit(const int32_t status);   // 释放当前进程占有的大部分资源并记录其退出状态，使当前进程进入挂起状态
extern int32_t wait(int32_t *status);   // 使当前进程等待某一个子进程调用exit函数退出，获取该子进程的退出状态并返回其pid，若当前进程无子进程则返回-1 
extern int32_t pipe(uint32_t pipe_fd[2]); // 创建一个管道，成功返回0，失败返回-1
extern int32_t fd_redirect(uint32_t old_fd, uint32_t new_fd);  // 文件描述符重定位, 成功返回0，失败返回-1

#endif