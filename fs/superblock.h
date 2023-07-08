#ifndef __FS_SUPERBLOCK_H
#define __FS_SUPERBLOCK_H

#include "stdint.h"

// 存储文件系统元信息的超级块
typedef struct superblock
{
    uint32_t magic;                 // 魔数，用于识别文件系统类型

    uint32_t inode_cnt;             // inode表中所含inode数量，也是文件的最大数量

    uint32_t part_lba;              // 文件系统所在分区的起始LBA
    uint32_t part_sects;            // 文件系统所在分区的扇区数

    uint32_t block_bitmap_lba;      // 块位图的起始LBA
    uint32_t block_bitmap_sects;    // 块位图所占扇区数

    uint32_t inode_bitmap_lba;      // inode位图的起始LBA
    uint32_t inode_bitmap_sects;    // inode位图所占扇区数

    uint32_t inode_table_lba;       // inode表的起始LBA
    uint32_t inode_table_sects;     // inode表所占扇区数

    uint32_t blocks_lba;            // 数据块区的起始LBA
    uint32_t blocks_sects;          // 数据块区所占扇区数

    uint32_t root_i_no;             // 根目录的inode编号
    uint32_t dentry_size;           // 目录项大小
} superblock;

#endif