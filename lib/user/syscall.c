#include "syscall.h"

#define SYS_GETPID 0
#define SYS_WRITE 1
#define SYS_MALLOC 2
#define SYS_FREE 3
#define SYS_OPEN 4
#define SYS_CLOSE 5
#define SYS_READ 6
#define SYS_LSEEK 7
#define SYS_UNLINK 8
#define SYS_MKDIR 9
#define SYS_OPENDIR 10
#define SYS_CLOSEDIR 11
#define SYS_READDIR 12
#define SYS_RMDIR 13
#define SYS_REWINDDIR 14
#define SYS_GETCWD 15
#define SYS_CHDIR 16
#define SYS_STAT 17
#define SYS_MOUNT 18
#define SYS_UMOUNT 19
#define SYS_FORK 20
#define SYS_PUTCHAR 21
#define SYS_CLEAR 22
#define SYS_PS 23
#define SYS_EXECV 24
#define SYS_EXIT 25
#define SYS_WAIT 26
#define SYS_PIPE 27
#define SYS_FD_REDIRECT 28


#define _syscall0(SYS_NR) \
({  \
    int32_t retval; \
    asm volatile ("int $0x80\n\t": "=a"(retval): "a"(SYS_NR)); \
    retval;  \
})

#define _syscall1(SYS_NR, ARG0) \
({  \
    int32_t retval; \
    asm volatile ("int $0x80\n\t": "=a"(retval): "a"(SYS_NR), "b"(ARG0)); \
    retval;  \
})

#define _syscall2(SYS_NR, ARG0, ARG1) \
({  \
    int32_t retval; \
    asm volatile ("int $0x80\n\t": "=a"(retval): "a"(SYS_NR), "b"(ARG0), "c"(ARG1)); \
    retval;  \
})

#define _syscall3(SYS_NR, ARG0, ARG1, ARG2) \
({  \
    int32_t retval; \
    asm volatile ("int $0x80\n\t": "=a"(retval): "a"(SYS_NR), "b"(ARG0), "c"(ARG1), "d"(ARG2)); \
    retval;  \
})


/***        系统调用的用户接口函数          ***/


// 获取调用者的pid
uint32_t getpid(void)
{
    return _syscall0(SYS_GETPID);
}    

// 将buf处起始的cnt个字节写入fd指向的文件中，成功则返回写入的字节数，失败返回-1
int32_t write(const uint32_t fd, const void *buf, uint32_t cnt)
{
    return _syscall3(SYS_WRITE, fd, buf, cnt);
}  

// 请求在用户堆空间中分配size字节的内存空间，分配成功则返回该内存空间的首虚拟地址，否则返回NULL
void *malloc(const uint32_t size)
{
    return (void *)_syscall1(SYS_MALLOC, size);
}

// 按照指定方式打开文件, 成功返回对应的文件描述符，失败返回-1
int32_t open(const char* pathname, const uint8_t flag)
{
    return _syscall2(SYS_OPEN, pathname, flag);
}

// 关闭fd指向的文件
int32_t close(const uint32_t fd)
{
    return _syscall1(SYS_CLOSE, fd);
}

// 从fd指向的文件中读取cnt个字节到内存中的buf处, 成功返回读取的字节数，失败返回-1
int32_t read(const uint32_t fd, void *buf, const uint32_t cnt)
{
    return _syscall3(SYS_READ, fd, buf, cnt);
}  

// 释放ptr指向的内存块
void free(void *ptr)
{
    _syscall1(SYS_FREE, ptr);
} 

// 将文件指针定位到指定偏移处，将文件指针定位到指定偏移处, 成功返回新的文件指针，失败返回-1
int32_t lseek(const uint32_t fd, const int32_t offset, const uint8_t whence)
{
    return _syscall3(SYS_LSEEK, fd,  offset, whence);
}

// 删除指定路径上的文件，成功返回0，失败返回-1
int32_t unlink(const char *pathname)
{
    return _syscall1(SYS_UNLINK, pathname);
}

// 创建一个空目录，成功返回0，失败返回-1
int32_t mkdir(const char *pathname)
{
    return _syscall1(SYS_MKDIR, pathname);
} 

// 打开指定目录，成功返回对应的目录指针，失败返回NULL
dir *opendir(const char *pathname)
{
    return (dir *)_syscall1(SYS_OPENDIR, pathname);
}  

// 关闭指定目录，成功返回0，失败返回-1
int32_t closedir(dir *pdir)
{
    return _syscall1(SYS_CLOSEDIR, pdir);
}

// 从指定目录中读取一个目录项，成功返回目录项指针，失败返回NULL
dentry *readdir(dir *pdir)
{
    return (dentry *)_syscall1(SYS_READDIR, pdir);
}

// 删除指定空目录，成功返回0，失败返回-1
int32_t rmdir(const char* pathname)
{
    return _syscall1(SYS_RMDIR, pathname); 
}

// 将目录结构的d_pos置零
void rewinddir(dir *pdir)
{
    _syscall1(SYS_REWINDDIR, pdir);
} 

// 获取当前工作目录的绝对路径
char *getcwd(char *buf, uint32_t size)
{
    return (char *)_syscall2(SYS_GETCWD, buf, size);
}

// 将当前工作路径改到指定路径, 成功返回0， 失败返回-1
int32_t chdir(const char *pathname)
{
    return _syscall1(SYS_CHDIR, pathname);
}

// 读取指定路径上的文件的信息，成功后存储在buf中并返回0，若失败则返回-1
int32_t stat(const char *pathname, struct stat *buf)
{
    return _syscall2(SYS_STAT, pathname, buf);
}

// 将指定分区上的文件系统挂载到指定路径, 成功返回0， 失败返回-1
int32_t mount(const char *source, const char *target)
{
    return _syscall2(SYS_MOUNT, source, target);
}

// 卸载指定挂载点上的文件系统, 成功返回0， 失败返回-1
int32_t umount(const char *target)
{
    return _syscall1(SYS_UMOUNT, target);
} 

// 创建一个相同的子进程, 成功则父进程返回子进程pid，子进程返回0，失败则返回-1
int32_t fork(void)
{
    return _syscall0(SYS_FORK);
} 

// 往屏幕上打印一个字符
void putchar(char ch)
{
    _syscall1(SYS_PUTCHAR, ch);
}

// 清除屏幕上的所有内容并使光标复位
void clear(void)
{
    _syscall0(SYS_CLEAR);
}  

// 列出所有进程的信息和状态
void ps(void)
{
    _syscall0(SYS_PS);
}  

// 用指定路径上的可执行文件体替换当前进程的进程体，若成功则转到入口地址开始运行，若失败则返回-1
int32_t execv(const char *pathname, char *argv[])
{
    return _syscall2(SYS_EXECV, pathname, argv);
}   

// 释放当前进程占有的大部分资源并记录其退出状态，使当前进程进入挂起状态
void exit(const int32_t status)
{
    _syscall1(SYS_EXIT, status);
}

// 使当前进程等待某一个子进程调用exit函数退出，获取该子进程的退出状态并返回其pid，若当前进程无子进程则返回-1 
int32_t wait(int32_t *status)
{
    return _syscall1(SYS_WAIT, status);
} 

// 创建一个管道，成功返回0，失败返回-1
int32_t pipe(uint32_t pipe_fd[2])
{
    return _syscall1(SYS_PIPE, pipe_fd);
}

// 文件描述符重定位, 成功返回0，失败返回-1
int32_t fd_redirect(uint32_t old_fd, uint32_t new_fd)
{
    return _syscall2(SYS_FD_REDIRECT, old_fd, new_fd);
} 