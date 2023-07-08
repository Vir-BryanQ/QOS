#ifndef __FS_INODE_H
#define __FS_INODE_H

#include "stdint.h"
#include "stdbool.h"
#include "list.h"

#define MAX_FILE_CNT 4096   // 最大支持的文件数量

typedef struct partition partition;

// 文件索引节点，用于唯一标识一个文件
typedef struct inode
{
    uint32_t i_no;          // inode编号
    partition *part;        // inode节点所在的分区
    uint32_t i_size;        // 对目录而言，该项是目录表中所有有效目录项的总大小，对文件而言，该项标识文件大小
    uint32_t open_cnt;      // 文件打开次数

    node list_node;         // 用于将inode挂到打开文件链表中的节点

    uint32_t i_sectors[13]; // 采用混合索引方式， 0-11 是直接索引， 12是一级间接索引
} inode;

// 该结构用于根据inode编号获取到inode在硬盘中的位置
typedef struct inode_position
{
    bool two_sec;           // inode是否跨扇区
    uint32_t lba;           // inode的起始扇区地址
    uint32_t offset;        // inode的扇区内偏移
} inode_position;

extern void inode_locate(partition *part, uint32_t i_no, inode_position *i_pos);        // 根据inode编号定位到inode的物理位置
extern inode *inode_open(partition *part, uint32_t i_no);               // 打开分区part中编号为i_no的inode
extern void inode_close(inode *p_inode);            // 关闭指定inode
extern void inode_init(partition *part, uint32_t i_no, inode *p_inode);      // 初始化指定inode
extern void inode_sync(inode *p_inode);            // 将指定inode同步到硬盘中
extern void inode_release(partition *part, uint32_t i_no);   // 将指定inode和inode所指向的文件存储空间释放

#endif