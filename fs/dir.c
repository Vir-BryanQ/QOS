#include "dir.h"
#include "ide.h"
#include "_syscall.h"
#include "debug.h"
#include "inode.h"
#include "string.h"
#include "global.h"
#include "file.h"
#include "memory.h"

extern partition *root_part;        // 根目录所在分区

bool is_mount_point(node *pnode, int i_no);     // 作为list_traversal的回调函数判断某个inode是否属于挂载点

// 打开part分区中inode编号为i_no的目录
dir *dir_open(partition *part, uint32_t i_no)
{
    dir *pdir = (dir *)kmalloc(sizeof(dir));
    ASSERT(pdir);
    pdir->p_inode = (inode *)inode_open(part, i_no);
    pdir->d_pos = 0;
    pdir->buf = NULL;
    return pdir;
}       

// 关闭指定目录
void dir_close(dir *pdir)
{
    ASSERT(pdir);
    inode_close(pdir->p_inode);
    if (pdir->buf)
    {
        sys_free(pdir->buf);
    }
    sys_free(pdir);
}      

// 作为list_traversal的回调函数判断某个inode是否属于挂载点
bool is_mount_point(node *pnode, int i_no)
{
    mount_point *p_mnt_pt = member2struct(pnode, mount_point, list_node);
    return (p_mnt_pt->i_no == (uint32_t)i_no);
}     

// 在pdir的目录表中搜索名为filename的文件并返回对应目录项 
partition *dir_search(dir *pdir, const char *filename, dentry *p_dentry)
{
    // 将目录表的所有数据块地址存储到all_blocks中
    uint32_t *all_blocks = (uint32_t *)kmalloc(560);
    ASSERT(all_blocks);
    memset(all_blocks, 0, 560);
    for(uint32_t i = 0; i < 12; ++i)
    {
        all_blocks[i] = pdir->p_inode->i_sectors[i];
    }
    if (pdir->p_inode->i_sectors[12])
    {
        read_disk(pdir->p_inode->part->my_disk, all_blocks + 12, pdir->p_inode->i_sectors[12], 1);
    }

    uint32_t de_cnt_per_sec = SECTOR_SIZE / sizeof(dentry);
    dentry *buf = (dentry *)kmalloc(SECTOR_SIZE);
    ASSERT(buf);
    for (uint32_t i = 0; i < 140; ++i)
    {
        if (all_blocks[i])
        {
            read_disk(pdir->p_inode->part->my_disk, buf, all_blocks[i], 1);
            for (uint32_t j = 0; j < de_cnt_per_sec; ++j)
            {
                if (buf[j].f_type != FT_UNKNOWN && !strcmp(filename, buf[j].filename))
                {
                    memcpy(p_dentry, buf + j, sizeof(dentry));
                    partition *ret_part = pdir->p_inode->part;
                    if (buf[j].f_type == FT_DIRECTORY)
                    {
                        // 判断该目录是否是另一个分区的挂载点
                        node *pnode = list_traversal(&pdir->p_inode->part->mount_list, is_mount_point, buf[j].i_no);
                        if (pnode)
                        {
                            mount_point *p_mt_pt = member2struct(pnode, mount_point, list_node);
                            p_dentry->i_no = p_mt_pt->part->sb->root_i_no;
                            ret_part = p_mt_pt->part;
                        }
                        
                        // 将挂载于另一个分区的分区根目录中的 .. 目录项重定向
                        if (pdir->p_inode->part->parent_part && !strcmp(p_dentry->filename, "..") 
                            && pdir->p_inode->i_no == pdir->p_inode->part->sb->root_i_no)
                        {
                            p_dentry->i_no = pdir->p_inode->part->mount_p_i_no;
                            ret_part = pdir->p_inode->part->parent_part;
                        }
                    }
                    sys_free(buf);
                    sys_free(all_blocks);
                    return ret_part;
                }
            }
        }
    }
    sys_free(buf);
    sys_free(all_blocks);
    return NULL;
}

// 在目录pdir下增加一个目录项
bool add_dentry(dir *pdir, dentry *p_dentry)
{
    // 将目录表的所有数据块地址存储到all_blocks中
    uint32_t *all_blocks = (uint32_t *)kmalloc(560);
    ASSERT(all_blocks);
    memset(all_blocks, 0, 560);
    for(uint32_t i = 0; i < 12; ++i)
    {
        all_blocks[i] = pdir->p_inode->i_sectors[i];
    }
    if (pdir->p_inode->i_sectors[12])
    {
        read_disk(pdir->p_inode->part->my_disk, all_blocks + 12, pdir->p_inode->i_sectors[12], 1);
    }

    for (uint32_t i = 0; i < 140; ++i)
    {
        if (!all_blocks[i])
        {
            if (i < 12)
            {
                // 分配一个数据块
                uint32_t blk_lba = bitmap_alloc(pdir->p_inode->part, BLOCK_BITMAP);
                if (blk_lba == -1)
                {
                    sys_free(all_blocks);
                    return false;
                }

                void *buf = kmalloc(SECTOR_SIZE);
                ASSERT(buf);
                memset(buf, 0, SECTOR_SIZE);
                memcpy(buf, p_dentry, sizeof(dentry));
                write_disk(pdir->p_inode->part->my_disk, buf, blk_lba, 1);

                pdir->p_inode->i_sectors[i] = all_blocks[i] = blk_lba;
                pdir->p_inode->i_size += sizeof(dentry);

                inode_sync(pdir->p_inode);
                bitmap_sync(pdir->p_inode->part, BLOCK_BITMAP, blk_lba - pdir->p_inode->part->sb->blocks_lba);
                sys_free(buf);
                sys_free(all_blocks);
                return true;
            }
            else if (i == 12 && pdir->p_inode->i_sectors[12] == 0)
            {
                // 分配一个索引块
                uint32_t indirect_blk_lba = bitmap_alloc(pdir->p_inode->part, BLOCK_BITMAP);
                if (indirect_blk_lba == -1)
                {
                    sys_free(all_blocks);
                    return false;
                }
                // 分配一个数据块
                uint32_t blk_lba = bitmap_alloc(pdir->p_inode->part, BLOCK_BITMAP);
                if (blk_lba == -1)
                {
                    // 将之前分配的间接块回滚到未分配状态
                    bitmap_set(&pdir->p_inode->part->block_bitmap, indirect_blk_lba - pdir->p_inode->part->sb->blocks_lba, 0);
                    sys_free(all_blocks);
                    return false;
                }

                void *buf = kmalloc(SECTOR_SIZE);
                ASSERT(buf);
                memset(buf, 0, SECTOR_SIZE);
                memcpy(buf, p_dentry, sizeof(dentry));
                write_disk(pdir->p_inode->part->my_disk, buf, blk_lba, 1);

                all_blocks[12] = blk_lba;
                write_disk(pdir->p_inode->part->my_disk, all_blocks + 12, indirect_blk_lba, 1);

                pdir->p_inode->i_sectors[12] = indirect_blk_lba;
                pdir->p_inode->i_size += sizeof(dentry);
                
                inode_sync(pdir->p_inode);
                bitmap_sync(pdir->p_inode->part, BLOCK_BITMAP, blk_lba - pdir->p_inode->part->sb->blocks_lba);
                bitmap_sync(pdir->p_inode->part, BLOCK_BITMAP, indirect_blk_lba - pdir->p_inode->part->sb->blocks_lba);
                sys_free(buf);
                sys_free(all_blocks);
                return true;
            }
            else
            {
                // 分配一个数据块
                uint32_t blk_lba = bitmap_alloc(pdir->p_inode->part, BLOCK_BITMAP);
                if (blk_lba == -1)
                {
                    sys_free(all_blocks);
                    return false;
                }

                void *buf = kmalloc(SECTOR_SIZE);
                ASSERT(buf);
                memset(buf, 0, SECTOR_SIZE);
                memcpy(buf, p_dentry, sizeof(dentry));
                write_disk(pdir->p_inode->part->my_disk, buf, blk_lba, 1);

                all_blocks[i] = blk_lba;
                write_disk(pdir->p_inode->part->my_disk, all_blocks + 12, pdir->p_inode->i_sectors[12], 1);

                pdir->p_inode->i_size += sizeof(dentry);

                inode_sync(pdir->p_inode);
                bitmap_sync(pdir->p_inode->part, BLOCK_BITMAP, blk_lba - pdir->p_inode->part->sb->blocks_lba);
                sys_free(buf);
                sys_free(all_blocks);
                return true;
            }
        }
        uint32_t de_cnt_per_sec = SECTOR_SIZE / sizeof(dentry);
        dentry *buf = (dentry *)kmalloc(SECTOR_SIZE);
        ASSERT(buf);
        read_disk(pdir->p_inode->part->my_disk, buf, all_blocks[i], 1);
        for (uint32_t j = 0; j < de_cnt_per_sec; ++j)
        {
            if (buf[j].f_type == FT_UNKNOWN)
            {
                memcpy(buf + j, p_dentry, sizeof(dentry));
                write_disk(pdir->p_inode->part->my_disk, buf, all_blocks[i], 1);

                pdir->p_inode->i_size += sizeof(dentry);
                inode_sync(pdir->p_inode);
                sys_free(buf);
                sys_free(all_blocks);
                return true;
            }
        }
        sys_free(buf);
    }
    sys_free(all_blocks);
    return false;
}  

// 初始化目录项
void dentry_init(uint32_t i_no, const char *filename, file_type f_type, dentry *p_dentry)
{
    strcpy(p_dentry->filename, filename);
    p_dentry->i_no = i_no;
    p_dentry->f_type = f_type;
}   

// 在pdir的目录表中删除名为filename的目录项
bool del_dentry(dir *pdir, const char *filename)
{
    ASSERT(pdir && filename);

    uint32_t *all_blocks = (uint32_t *)kmalloc(560);
    ASSERT(all_blocks);
    memset(all_blocks, 0, 560);
    for (uint32_t i = 0; i < 12; ++i)
    {
        all_blocks[i] = pdir->p_inode->i_sectors[i];
    }
    if (pdir->p_inode->i_sectors[12])
    {
        read_disk(pdir->p_inode->part->my_disk, all_blocks + 12, pdir->p_inode->i_sectors[12], 1);
    }

    dentry *buf = (dentry *)kmalloc(SECTOR_SIZE);
    ASSERT(buf);
    uint32_t de_cnt_per_sec = SECTOR_SIZE / sizeof(dentry);
    int32_t de_to_del_idx = -1;             // 待删除目录项的索引
    uint32_t valid_de_cnt_in_this_sec;  // 除去要删除的目录项以外当前扇区包含的有效目录项的数目
    for (uint32_t i = 0; i < 140; ++i)
    {
        if (all_blocks[i])
        {
            read_disk(pdir->p_inode->part->my_disk, buf, all_blocks[i], 1);
            valid_de_cnt_in_this_sec = 0;
            for (uint32_t j = 0; j < de_cnt_per_sec; ++j)
            {
                if (buf[j].f_type != FT_UNKNOWN)
                {
                    if (!strcmp(buf[j].filename, filename))
                    {
                        de_to_del_idx = j;
                    }
                    else
                    {
                        ++valid_de_cnt_in_this_sec;
                    }
                }
            }
            if (de_to_del_idx != -1)
            {
                pdir->p_inode->i_size -= sizeof(dentry);

                // 如果删除某个目录项之后，该扇区不包含任何有效目录项，则应该回收该扇区
                if (valid_de_cnt_in_this_sec == 0)
                {
                    bitmap_set(&pdir->p_inode->part->block_bitmap, all_blocks[i] - pdir->p_inode->part->sb->blocks_lba, 0);
                    if (i < 12)
                    {
                        pdir->p_inode->i_sectors[i] = 0;
                    }
                    else
                    {
                        // 如果释放某个数据块之后，索引块也变为空，则应当一同回收索引块
                        all_blocks[i] = 0;
                        for (uint32_t i = 12; i < 140; ++i)
                        {
                            if (all_blocks[i])
                            {
                                write_disk(pdir->p_inode->part->my_disk, all_blocks + 12, pdir->p_inode->i_sectors[12], 1);
                                bitmap_sync(pdir->p_inode->part, BLOCK_BITMAP, all_blocks[i] - pdir->p_inode->part->sb->blocks_lba);
                                inode_sync(pdir->p_inode);

                                sys_free(all_blocks);
                                sys_free(buf);
                                return true;
                            }
                        }
                        bitmap_set(&pdir->p_inode->part->block_bitmap, pdir->p_inode->i_sectors[12] - pdir->p_inode->part->sb->blocks_lba, 0);
                        bitmap_sync(pdir->p_inode->part, BLOCK_BITMAP, pdir->p_inode->i_sectors[12] - pdir->p_inode->part->sb->blocks_lba);
                        pdir->p_inode->i_sectors[12] = 0;
                    }
                    bitmap_sync(pdir->p_inode->part, BLOCK_BITMAP, all_blocks[i] - pdir->p_inode->part->sb->blocks_lba);
                }
                else
                {
                    buf[de_to_del_idx].f_type = FT_UNKNOWN;
                    write_disk(pdir->p_inode->part->my_disk, buf, all_blocks[i], 1);
                }

                inode_sync(pdir->p_inode);
                sys_free(all_blocks);
                sys_free(buf);
                return true;
            }
        }
    }

    sys_free(all_blocks);
    sys_free(buf);
    return false;
} 

// 在pdir下创建一个名为dirname的空目录
int32_t dir_create(dir *pdir, const char *dirname)
{
    // 分配一个inode
    int32_t i_no = bitmap_alloc(pdir->p_inode->part, INODE_BITMAP);
    if (i_no == -1)
    {
        return -1;
    }

    // 分配一个块作为目录表
    int32_t blk_lba = bitmap_alloc(pdir->p_inode->part, BLOCK_BITMAP);
    if (blk_lba == -1)
    {
        bitmap_set(&pdir->p_inode->part->inode_bitmap, i_no, 0);
        return -1;
    }

    // 在父目录表中添加对应目录项
    dentry dir_e;
    dentry_init(i_no, dirname, FT_DIRECTORY, &dir_e);
    if (!add_dentry(pdir, &dir_e))
    {
        bitmap_set(&pdir->p_inode->part->inode_bitmap, i_no, 0);
        bitmap_set(&pdir->p_inode->part->block_bitmap, blk_lba - pdir->p_inode->part->sb->blocks_lba, 0);
        return -1;
    }

    // 初始化inode
    inode new_inode;
    inode_init(pdir->p_inode->part, i_no, &new_inode);

    // 初始化目录表
    dentry *buf = (dentry *)kmalloc(SECTOR_SIZE);
    ASSERT(buf);
    memset(buf, 0, SECTOR_SIZE);
    buf[0].i_no = i_no;
    buf[0].f_type = FT_DIRECTORY;
    strcpy(buf[0].filename, ".");
    buf[1].i_no = pdir->p_inode->i_no;
    buf[1].f_type = FT_DIRECTORY;
    strcpy(buf[1].filename, "..");

    new_inode.i_size = 2 * sizeof(dentry);
    new_inode.i_sectors[0] = blk_lba;

    // 将inode、目录表和位图同步到硬盘
    write_disk(pdir->p_inode->part->my_disk, buf, blk_lba, 1);
    inode_sync(&new_inode);
    bitmap_sync(pdir->p_inode->part, INODE_BITMAP, i_no);
    bitmap_sync(pdir->p_inode->part, BLOCK_BITMAP, blk_lba - pdir->p_inode->part->sb->blocks_lba);

    sys_free(buf);
    return 0;
}  

// 从pdir中读取一个目录项，读取的目录项由 d_pos 决定
dentry *dir_read(dir *pdir)
{
    ASSERT(pdir->d_pos <= pdir->p_inode->i_size && !(pdir->d_pos % sizeof(dentry)));
    if (pdir->d_pos == pdir->p_inode->i_size)
    {
        return NULL;
    }

    uint32_t *all_blocks = (uint32_t *)kmalloc(560);
    ASSERT(all_blocks);
    memset(all_blocks, 0, 560);
    for (uint32_t i = 0; i < 12; ++i)
    {
        all_blocks[i] = pdir->p_inode->i_sectors[i];
    }
    read_disk(pdir->p_inode->part->my_disk, all_blocks + 12, pdir->p_inode->i_sectors[12], 1);

    uint32_t de_cnt_per_sec = SECTOR_SIZE / sizeof(dentry);
    uint32_t cur_pos = 0;
    if (!pdir->buf)
    {
        pdir->buf = (dentry *)sys_malloc(SECTOR_SIZE);
        ASSERT (pdir->buf);
    }
    for (uint32_t i = 0; i < 140; ++i)
    {
        if (all_blocks[i])
        {
            read_disk(pdir->p_inode->part->my_disk, pdir->buf, all_blocks[i], 1);
            for (uint32_t j = 0; j < de_cnt_per_sec; ++j)
            {
                if (pdir->buf[j].f_type != FT_UNKNOWN)
                {
                    if (cur_pos == pdir->d_pos)
                    {
                        pdir->d_pos += sizeof(dentry);
                        return pdir->buf + j;
                    }
                    else
                    {
                        cur_pos += sizeof(dentry);
                    }
                }
            }
        }
    }
    panic_spin(__FILE__, __LINE__, __func__, "Should not be here!");
    return NULL;
}

// 获取指定文件所在的父目录信息
void find_parent_dir(partition *part, const uint32_t i_no, parent_dir_info *pd_inf)
{
    dir *pdir = dir_open(part, i_no);
    dentry dir_e;
    pd_inf->part = dir_search(pdir, "..", &dir_e);
    pd_inf->i_no = dir_e.i_no;
    pd_inf->i_no_to_search = (i_no == part->sb->root_i_no && part->parent_part) ? part->mount_i_no : i_no; 
    dir_close(pdir);
}  

// 在父目录表中获取子目录的名字并拼接到path中
void get_child_dir_name(parent_dir_info *pd_inf, char *path)
{
    dir *pdir = dir_open(pd_inf->part, pd_inf->i_no);

    uint32_t *all_blocks = (uint32_t *)kmalloc(560);
    ASSERT(all_blocks);
    memset(all_blocks, 0, 560);
    for (uint32_t i = 0; i < 12; ++i)
    {
        all_blocks[i] = pdir->p_inode->i_sectors[i];
    }
    read_disk(pdir->p_inode->part->my_disk, all_blocks + 12, pdir->p_inode->i_sectors[12], 1);

    uint32_t de_cnt_per_sec = SECTOR_SIZE / sizeof(dentry);
    dentry *buf = (dentry *)kmalloc(SECTOR_SIZE);
    ASSERT(buf);
    for (uint32_t i = 0; i < 140; ++i)
    {
        if (all_blocks[i])
        {
            read_disk(pdir->p_inode->part->my_disk, buf, all_blocks[i], 1);
            for (uint32_t j = 0; j < de_cnt_per_sec; ++j)
            {
                if (buf[j].f_type != FT_UNKNOWN && buf[j].i_no == pd_inf->i_no_to_search)
                {
                    strcat(path, "/");
                    strcat(path, buf[j].filename);
                    dir_close(pdir);
                    sys_free(buf);
                    sys_free(all_blocks);
                    return;
                }
            }
        }
    }
    panic_spin(__FILE__, __LINE__, __func__, "Should not be here!");
}    