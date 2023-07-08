#include "inode.h"
#include "ide.h"
#include "dir.h"
#include "_syscall.h"
#include "thread.h"
#include "global.h"
#include "debug.h"
#include "string.h"
#include "process.h"

extern partition *root_part;                         // 根目录所在的分区

bool inode_check(node *pnode, int i_no);            // 作为list_traversal的回调函数判断指定的inode节点的编号是否是i_no

// 根据inode编号定位到inode的物理位置
void inode_locate(partition *part, uint32_t i_no, inode_position *i_pos)
{
    i_pos->lba = part->sb->inode_table_lba + ((i_no * sizeof(inode)) / SECTOR_SIZE);
    i_pos->offset = ((i_no * sizeof(inode)) % SECTOR_SIZE);
    i_pos->two_sec = ((SECTOR_SIZE - i_pos->offset) < sizeof(inode));
}       

// 作为list_traversal的回调函数判断指定的inode节点的编号是否是i_no
bool inode_check(node *pnode, int i_no)
{
    inode *p_inode = member2struct(pnode, inode, list_node);
    return (p_inode->i_no == i_no); 
}       

// 打开分区part中编号为i_no的inode
inode *inode_open(partition *part, uint32_t i_no)
{
    if (!part || i_no >= part->sb->inode_cnt)
    {
        return NULL;
    }

    node *pnode = list_traversal(&part->inode_list, inode_check, i_no);
    if (pnode)
    {
        // 如果待打开的inode已经在打开文件链表中存在，只需将打开计数值加一即可
        inode *p_inode = member2struct(pnode, inode, list_node);
        ++p_inode->open_cnt;
        return p_inode;
    }

    inode *p_inode = (inode *)kmalloc(sizeof(inode));     
    ASSERT(p_inode);

    uint8_t *buf;
    inode_position i_pos;
    inode_locate(part, i_no, &i_pos);
    uint32_t sec_cnt = i_pos.two_sec ? 2 : 1;
    buf = (uint8_t *)kmalloc(SECTOR_SIZE * sec_cnt);
    ASSERT(buf);
    read_disk(part->my_disk, buf, i_pos.lba, sec_cnt);

    memcpy(p_inode, buf + i_pos.offset, sizeof(inode));

    ASSERT((p_inode->open_cnt == 0) && !p_inode->part);
    p_inode->open_cnt = 1;
    p_inode->part = part;
    list_push_front(&part->inode_list, &p_inode->list_node);          // 该inode可能很快就会被访问，将其放到链表头

    sys_free(buf);
    return p_inode;
}  

// 关闭指定inode
void inode_close(inode *p_inode)
{
    ASSERT(list_find(&p_inode->part->inode_list, &p_inode->list_node));
    if (--p_inode->open_cnt == 0)
    {
        list_remove(&p_inode->part->inode_list, &p_inode->list_node);
        sys_free(p_inode);
    }
}   

// 初始化指定inode
void inode_init(partition *part, uint32_t i_no, inode *p_inode)
{
    memset(p_inode, 0, sizeof(inode));
    p_inode->part = part;
    p_inode->i_no = i_no;
}      

// 将指定inode同步到硬盘中
void inode_sync(inode *p_inode)
{
    inode_position i_pos;
    inode_locate(p_inode->part, p_inode->i_no, &i_pos);
    uint32_t sec_cnt = i_pos.two_sec ? 2 : 1;
    uint8_t *buf = (uint8_t *)kmalloc(SECTOR_SIZE * sec_cnt);
    ASSERT(buf);
    read_disk(p_inode->part->my_disk, buf, i_pos.lba, sec_cnt);

    memcpy(buf + i_pos.offset, p_inode, sizeof(inode));

    // 清除无关项
    inode *p = (inode *)(buf + i_pos.offset);
    p->open_cnt = 0;
    p->part = NULL;
    p->list_node.next = p->list_node.prev = NULL;

    write_disk(p_inode->part->my_disk, buf, i_pos.lba, sec_cnt);

    sys_free(buf);
} 

// 将指定inode和inode所指向的文件存储空间释放
void inode_release(partition *part, uint32_t i_no)
{
    ASSERT(part && i_no < MAX_FILE_CNT);

    inode *p_inode = inode_open(part, i_no);
    ASSERT(p_inode);

    // 释放inode
    bitmap_set(&part->inode_bitmap, i_no, 0);
    bitmap_sync(part, INODE_BITMAP, i_no);

    uint32_t *all_blocks = (uint32_t *)kmalloc(560);
    ASSERT(all_blocks);
    memset(all_blocks, 0, 560);
    for (uint32_t i = 0; i < 12; ++i)
    {
        all_blocks[i] = p_inode->i_sectors[i];
    }
    if (p_inode->i_sectors[12])
    {
        read_disk(part->my_disk, all_blocks + 12, p_inode->i_sectors[12], 1);
        bitmap_set(&part->block_bitmap, p_inode->i_sectors[12] - part->sb->blocks_lba, 0);  // 释放索引块
        bitmap_sync(part, BLOCK_BITMAP, p_inode->i_sectors[12] - part->sb->blocks_lba);
    }

    // 释放数据块
    uint32_t blk_cnt = DIV_ROUND_UP(p_inode->i_size, SECTOR_SIZE);
    for (uint32_t i = 0; i < blk_cnt; ++i)
    {
        ASSERT(all_blocks[i]);
        bitmap_set(&part->block_bitmap, all_blocks[i] - part->sb->blocks_lba, 0);
        bitmap_sync(part, BLOCK_BITMAP, all_blocks[i] - part->sb->blocks_lba);
    }

    sys_free(all_blocks);
    // 释放inode的硬盘空间之后必须要关闭inode，否则会导致内存中残留的inode影响新文件的打开操作，新文件的大小被写入一个错误的值
    ASSERT(p_inode->open_cnt == 1);
    inode_close(p_inode); 
}

