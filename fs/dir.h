#ifndef __FS_DIR_H
#define __FS_DIR_H

#include "stdint.h"
#include "list.h"

#define SECTOR_SIZE 512                 // 扇区大小

#define MAX_FILENAME_LEN 32

typedef struct inode inode;
typedef struct partition partition;

typedef enum file_type
{
    FT_UNKNOWN,         // 未知文件类型
    FT_REGULAR,         // 普通文件类型
    FT_DIRECTORY        // 目录文件类型
} file_type;

// 目录表中的目录项
typedef struct dentry
{
    uint32_t i_no;          // 目录项对应文件的inode编号
    char filename[MAX_FILENAME_LEN];     // 文件名
    file_type f_type;       // 文件类型
} dentry;

// 用于操作目录的目录结构
typedef struct dir
{
    inode *p_inode;                     // 该目录结构对应的目录inode编号
    uint32_t d_pos;                     // 目录项指针，用于读取目录表中的目录项
    dentry* buf;                       // 目录缓冲区指针
} dir;

// 记录挂载点信息
typedef struct mount_point
{
    partition *part;        // 挂载在该挂载点上的分区
    uint32_t i_no;          // 该挂载点的inode编号
    node list_node;         // 用于将该结构挂在分区的mount_list上
} mount_point;

// 记录父目录的相关信息，主要用于生成当前工作目录的绝对路径
typedef struct parent_dir_info
{
    partition *part;    // 父目录所在分区
    uint32_t i_no;      // 父目录的inode编号
    uint32_t i_no_to_search;    // 需要在父目录表中搜索的inode编号
} parent_dir_info;

extern dir *dir_open(partition *part, uint32_t i_no);       // 打开part分区中inode编号为i_no的目录
extern void dir_close(dir *pdir);       // 关闭指定目录
extern partition *dir_search(dir *pdir, const char *filename, dentry *p_dentry); // 在pdir的目录表中搜索名为filename的文件并返回对应目录项
extern bool add_dentry(dir *pdir, dentry *p_dentry);        // 在目录pdir下增加一个目录项
extern void dentry_init(uint32_t i_no, const char *filename, file_type f_type, dentry *p_dentry);   // 初始化目录项
extern bool del_dentry(dir *pdir, const char *filename);    // 在pdir的目录表中删除名为filename的目录项
extern int32_t dir_create(dir *pdir, const char *dirname);  // 在pdir下创建一个名为dirname的空目录
extern dentry *dir_read(dir *pdir); // 从pdir中读取一个目录项，读取的目录项由 d_pos 决定
extern void find_parent_dir(partition *part, const uint32_t i_no, parent_dir_info *pd_inf);  // 获取指定文件所在的父目录信息
extern void get_child_dir_name(parent_dir_info *pd_inf, char *path);    // 在父目录表中获取子目录的名字并拼接到path中

#endif
