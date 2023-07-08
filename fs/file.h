#ifndef __FS_FILE_H
#define __FS_FILE_H

#include "dir.h"

#define stdin 0
#define stdout 1
#define stderr 2

#define SEEK_SET 0      // 文件头
#define SEEK_CUR 1      // 文件指针的当前位置
#define SEEK_END 2      // 文件尾

#define MAX_FILES_OPEN_PER_PROC 16      // 每个进程的最大打开文件数
#define MAX_FILES_OPEN 128          // 最大支持的打开文件数

#define BITS_PER_SECTOR   (SECTOR_SIZE * 8)              // 每个扇区的二进制位数

typedef struct inode inode;
typedef struct partition partition;

typedef struct file
{
    inode *p_inode;         // 该文件结构对应的文件inode
    uint32_t f_pos;         // 文件的读写指针
    uint8_t flag;           // 文件的打开方式
} file;

typedef enum bitmap_t
{
    INODE_BITMAP,
    BLOCK_BITMAP
} bitmap_t;

extern file file_table[];    // 文件结构表 

extern int32_t get_free_slot_in_file_table(void);       // 在file_table中获取一个空闲的槽位，成功返回下标，失败返回-1
extern int32_t get_free_slot_in_fd_table(void);         // 在fd_table中获取一个空闲的槽位，成功返回下标，失败返回-1
extern int32_t bitmap_alloc(partition *part, bitmap_t bm_t);     // 在指定分区的inode位图中分配一个inode或块位图中分配一个块, 失败则返回-1
extern void bitmap_sync(partition *part, bitmap_t bm_t, uint32_t bit_idx);  // 将指定分区位图中偏移为bit_idx的位所在的扇区同步到硬盘中

#endif