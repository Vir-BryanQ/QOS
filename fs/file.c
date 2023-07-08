#include "file.h"
#include "thread.h"
#include "ide.h"

file file_table[MAX_FILES_OPEN];    // 文件结构表 

// 在file_table中获取一个空闲的槽位，成功返回下标，失败返回-1
int32_t get_free_slot_in_file_table(void)
{
    for (uint32_t i = 3; i < MAX_FILES_OPEN; ++i)
    {
        if (!file_table[i].p_inode)
        {
            return i;
        }
    }
    return -1;
}   

// 在fd_table中获取一个空闲的槽位，成功返回下标，失败返回-1
int32_t get_free_slot_in_fd_table(void)
{
    for (uint32_t i = 3; i < MAX_FILES_OPEN_PER_PROC; ++i)
    {
        if (current->fd_table[i] == -1)
        {
            return i;
        }
    }
    return -1;
}      

// 在指定分区的inode位图中分配一个inode或块位图中分配一个块, 失败则返回-1
int32_t bitmap_alloc(partition *part, bitmap_t bm_t)    
{
    bitmap *p_btmp = ((bm_t == INODE_BITMAP) ? &part->inode_bitmap : &part->block_bitmap);
    int32_t bit_idx = bitmap_scan(p_btmp, 1);
    if (bit_idx != -1)
    {
        // inode位图分配时返回的是inode编号，块位图分配时返回的是块的LBA
        return (bm_t == INODE_BITMAP) ? bit_idx : (bit_idx + part->sb->blocks_lba);
    }
    return -1;
} 

// 将指定分区位图中偏移为bit_idx的位所在的扇区同步到硬盘中
void bitmap_sync(partition *part, bitmap_t bm_t, uint32_t bit_idx)
{
    bitmap *p_btmp;
    uint32_t btmp_lba;
    uint32_t sec_offset = bit_idx / BITS_PER_SECTOR;
    if (bm_t == INODE_BITMAP)
    {
        p_btmp = &part->inode_bitmap;
        btmp_lba = part->sb->inode_bitmap_lba + sec_offset;
    } 
    else if (bm_t == BLOCK_BITMAP)
    {
        p_btmp = &part->block_bitmap;
        btmp_lba = part->sb->block_bitmap_lba + sec_offset;
    }

    write_disk(part->my_disk, p_btmp->btmp_ptr + sec_offset * SECTOR_SIZE, btmp_lba, 1);
}  

