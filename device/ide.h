#ifndef __DEVICE_IDE_H
#define __DEVICE_IDE_H

#include "stdint.h"
#include "stdbool.h"
#include "list.h"
#include "sync.h"
#include "superblock.h"
#include "bitmap.h"

typedef struct ide_channel ide_channel;
typedef struct disk disk;
typedef struct partition partition; 

struct partition
{
    char name[8];          // 分区名
    disk *my_disk;          // 分区所在硬盘
    uint32_t start_lba;     // 起始逻辑块地址
    uint32_t sec_cnt;       // 分区占据的扇区数
    uint8_t type_id;        // 分区的类型号
    node list_node;         // 用于将分区挂到分区链表中

    // 以下成员和文件系统有关，在挂载分区时会得到初始化
    superblock *sb;         // 指向该分区的超级块
    bitmap block_bitmap;    // 该分区的块位图
    bitmap inode_bitmap;    // 该分区的inode位图
    list inode_list;        // 该分区的打开文件链表

    list mount_list;        // 挂载在该分区上的其他分区
    partition *parent_part; // 非空时表示本分区挂载于一个父分区
    uint32_t mount_i_no;    // 本分区在父分区上的挂载点inode编号
    uint32_t mount_p_i_no;  // 本分区在父分区上的挂载点所在父目录的inode编号
};

struct disk
{
    char name[8];
    ide_channel *my_channel;        // 硬盘所属ide通道
    partition primary[4];           // mbr最多支持四个主分区
    partition logic[16];            // mbr可支持任意数量的逻辑分区，这里将逻辑分区的上限人为限定在16个
    uint8_t dev_no;                 // 指示硬盘是主盘还是从盘，0为主盘，1为从盘
};

struct ide_channel
{
    char name[8];
    disk disks[2];                  // 一个ide通道最多有两块硬盘
    uint32_t port_base;             // 起始端口号
    uint32_t intr_no;               // 中断号
    mutex_lock mutex;               // 通道互斥锁，方便区分中断来自主盘还是从盘
    bool intr_is_expected;          // 指示当前通道是否正在等待中断
    semaphore disk_done;            // 线程发送硬盘命令后使用该信号量阻塞自己，等待硬盘中断将其唤醒
};

extern ide_channel channels[];         
extern list partition_list;                // 分区链表

extern void ide_init(void);                 // 硬盘相关初始化
extern void read_disk(disk *hd, void *dst, uint32_t start_lba, uint32_t sec_cnt);     // 从指定硬盘中读取start_lba起始的sec_cnt个扇区到dst
extern void write_disk(disk *hd, void *src, uint32_t start_lba, uint32_t sec_cnt);   // 将内存src处的sec_cnt个扇区数据写入到硬盘start_lba处

extern bool part_name_check(node *pnode, int arg);         // 作为list_traversal的回调函数以分区名查找指定分区

#endif