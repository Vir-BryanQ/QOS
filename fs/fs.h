#ifndef __FS_FS_H
#define __FS_FS_H

#include "dir.h"

typedef struct partition partition;
typedef struct file file;

#define MAX_PATH_LEN 256    // 最大路径长度

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 4

// 用于记录路径搜索过程中得到的信息
typedef struct search_record
{
    char search_path[MAX_PATH_LEN];    // 已经搜索过的路径
    dir *parent_dir;         // 最后一次查找到的文件所处的目录
    file_type f_type;        // 查找到的文件的类型
    partition *part;         // 查找到的文件所在的分区
    uint32_t i_no;           // 查找到的文件的inode编号
} search_record;

// 存储一个文件的相关信息
struct stat
{
    uint32_t i_no;
    uint32_t i_size;
    file_type f_type;
};

extern partition *root_part;       // 根目录所在的分区

extern void search_file(const char *pathname, search_record *sr);  // 按照给定的路径搜索文件，将结构存储在search_record结构中

extern void fs_init(void);         // 初始化文件系统
extern int32_t file_create(dir *pdir, const char *filename);    // 在目录pdir下创建一个名为filename的文件
extern int32_t file_open(partition *part, const uint32_t i_no, const uint8_t flag);     // 以指定方式打开文件
extern int32_t file_close(file *p_file);        // 关闭p_file指向的文件
extern int32_t file_write(file *p_file, const void *buf, uint32_t cnt);   // 将buf处的cnt个字节写入p_file指向的文件中
extern int32_t file_read(file *p_file, void *buf, uint32_t cnt);    // 从p_file指向的文件读取cnt个字节到buf处

extern uint32_t path_depth(const char *pathname);  // 获取指定路径的路径深度
extern char *path_parse(const char *pathname, char *filename); // 路径解析，每解析一层，返回该层的文件名filename和下一个分隔符的地址

extern void partition_mount(partition *part);      // 挂载指定分区

#endif
