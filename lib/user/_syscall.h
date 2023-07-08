#ifndef __LIB_USER__SYSCALL_H
#define __LIB_USER__SYSCALL_H

#include "stdint.h"

typedef struct dir dir;
typedef struct dentry dentry;
struct stat;

extern uint32_t sys_getpid(void);
extern int32_t sys_write(const uint32_t fd, const void *buf, uint32_t cnt);
extern void *sys_malloc(const uint32_t size);
extern void sys_free(void *ptr);
extern int32_t sys_open(const char* pathname, const uint8_t flag);
extern int32_t sys_close(const uint32_t fd); 
extern int32_t sys_read(const uint32_t fd, void *buf, const uint32_t cnt); 
extern int32_t sys_lseek(const uint32_t fd, const int32_t offset, const uint8_t whence);
extern int32_t sys_unlink(const char *pathname);
extern int32_t sys_mkdir(const char *pathname);
extern dir *sys_opendir(const char *pathname);
extern int32_t sys_closedir(dir *pdir);
extern dentry *sys_readdir(dir *pdir);
extern int32_t sys_rmdir(const char* pathname); 
extern void sys_rewinddir(dir *pdir);
extern char *sys_getcwd(char *buf, uint32_t size);
extern int32_t sys_chdir(const char *pathname);
extern int32_t sys_stat(const char *pathname, struct stat *buf);
extern int32_t sys_mount(const char *source, const char *target);
extern int32_t sys_umount(const char *target);
extern int32_t sys_fork(void);
extern void sys_putchar(char ch);
extern void sys_clear(void);
extern void sys_ps(void);
extern int32_t sys_execv(const char *pathname, char *argv[]);
extern void sys_exit(const int32_t status);
extern int32_t sys_wait(int32_t *status);
extern int32_t sys_pipe(uint32_t pipe_fd[2]);
extern int32_t sys_fd_redirect(uint32_t old_fd, uint32_t new_fd);

#endif