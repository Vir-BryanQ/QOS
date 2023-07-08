#include "fs.h"
#include "inode.h"
#include "superblock.h"
#include "_syscall.h"
#include "debug.h"
#include "ide.h"
#include "global.h"
#include "string.h"
#include "list.h"
#include "file.h"
#include "stdio.h"
#include "thread.h"

#define FS_MAGIC    0x20010828              // 文件系统魔数

partition *root_part;                         // 根目录所在的分区

void partition_format(partition *part);                     // 分区格式化
bool part_listnode_format(node *pnode, int arg UNUSED);     // 作为list_traversal的回调函数对不存在可识别文件系统的分区进行格式化
bool part_listnode_mount(node *pnode, int part_name);     // 作为list_traversal的回调函数对名为part_name的分区进行挂载

// 初始化文件系统
void fs_init(void)
{
    // 将所有未格式化的分区格式化
    list_traversal(&partition_list, part_listnode_format, 0);
    printk("Format partition done!\n");

    // 加载根文件系统
    node *pnode = list_traversal(&partition_list, part_listnode_mount, (int)"sdb1");
    ASSERT(pnode);
    root_part = member2struct(pnode, partition, list_node);
    printk("Successful to mount '%s' on '/'\n", root_part->name);

    // 初始化file_table
    for (uint32_t i = 0; i < MAX_FILES_OPEN; ++i)
    {
        file_table[i].p_inode = NULL;
    }
    file_table[0].flag = file_table[1].flag = file_table[2].flag = 0;   // 确保前三个文件结构被不会被识别为管道

    printk("Init filesystem successfully!\n");
}         

// 分区格式化
void partition_format(partition *part)
{
    /***  超级块初始化   ***/
    superblock *sb = (superblock *)kmalloc(SECTOR_SIZE);
    ASSERT(sb);

    // 初始化各个区域的扇区数
    sb->part_sects = part->sec_cnt;
    sb->inode_bitmap_sects = DIV_ROUND_UP(MAX_FILE_CNT, BITS_PER_SECTOR);
    sb->inode_table_sects = DIV_ROUND_UP(MAX_FILE_CNT * sizeof(inode), SECTOR_SIZE);
    uint32_t free_blks = sb->part_sects - (1 + 1 + sb->inode_bitmap_sects + sb->inode_table_sects);
    sb->block_bitmap_sects = DIV_ROUND_UP(free_blks, BITS_PER_SECTOR);
    sb->blocks_sects = free_blks - sb->block_bitmap_sects;
    sb->block_bitmap_sects = DIV_ROUND_UP(sb->blocks_sects, BITS_PER_SECTOR);

    // 初始化各个区域的起始LBA
    sb->part_lba = part->start_lba;
    sb->block_bitmap_lba = sb->part_lba + 2;
    sb->inode_bitmap_lba = sb->block_bitmap_lba + sb->block_bitmap_sects;
    sb->inode_table_lba = sb->inode_bitmap_lba + sb->inode_bitmap_sects;
    sb->blocks_lba = sb->inode_table_lba + sb->inode_table_sects;

    // 初始化其他信息
    sb->dentry_size = sizeof(dentry);
    sb->magic = FS_MAGIC;
    sb->inode_cnt = MAX_FILE_CNT;
    sb->root_i_no = 0;
    
    // 将超级块写入硬盘
    write_disk(part->my_disk, sb, sb->part_lba + 1, 1);

    /***  块位图初始化   ***/
    uint8_t *buf = (uint8_t *)sb;

    // 清空缓冲区前保存有用信息
    uint32_t part_lba = sb->part_lba;
    uint32_t part_sects = sb->part_sects;
    uint32_t block_bitmap_lba = sb->block_bitmap_lba;
    uint32_t block_bitmap_sects = sb->block_bitmap_sects;
    uint32_t blocks_lba = sb->blocks_lba;
    uint32_t blocks_sects = sb->blocks_sects;
    uint32_t inode_bitmap_lba = sb->inode_bitmap_lba;
    uint32_t inode_bitmap_sects = sb->inode_bitmap_sects;
    uint32_t inode_table_lba = sb->inode_table_lba;
    uint32_t inode_table_sects = sb->inode_table_sects;
    uint32_t root_i_no = sb->root_i_no;
    uint32_t magic = sb->magic;
    uint32_t dentry_size = sb->dentry_size;

    memset(buf, 0, SECTOR_SIZE);
    for (uint32_t i = 0; i < block_bitmap_sects; ++i)
    {
        if (i == 0)
        {
            // 块位图第一个扇区的位0要设置为1，将一个块分配给根目录表
            buf[0] = 0x01;
        }

        if (i == block_bitmap_sects - 1)
        {
            // 块位图最后一个扇区的多余二进制位要设置为1，是块位图的管理范围和数据块区范围一致
            uint32_t invl_blks = block_bitmap_sects * BITS_PER_SECTOR - blocks_sects;
            uint32_t invl_bytes = invl_blks / 8;
            uint32_t invl_bits = invl_blks % 8;
            memset(buf + SECTOR_SIZE - invl_bytes, 0xff, invl_bytes);
            buf[SECTOR_SIZE - 1 - invl_bytes] |= (0xff >> (8 - invl_bits));
        }

        // 将块位图写入到硬盘中
        write_disk(part->my_disk, buf, block_bitmap_lba + i, 1);

        if (i == 0)
        {
            buf[0] = 0;
        }
    }

    /***  inode位图初始化   ***/
    memset(buf, 0, SECTOR_SIZE);
    for (uint32_t i = 0; i < inode_bitmap_sects; ++i)
    {
        if (i == 0)
        {
            // inode位图第一个扇区的位0要设置为1，将一个inode分配给根目录
            buf[0] = 0x01;
        }

        // 将inode位图写入到硬盘中
        write_disk(part->my_disk, buf, inode_bitmap_lba + i, 1);

        if (i == 0)
        {
            buf[0] = 0;
        }
    }

    /***  inode表初始化   ***/
    inode *p_inode = (inode *)buf;

    // 设置根目录的inode
    p_inode->i_no = root_i_no;
    p_inode->i_size = 2 * sizeof(dentry);
    p_inode->open_cnt = 0;
    p_inode->part = NULL;
    p_inode->list_node.next = p_inode->list_node.prev = NULL;
    p_inode->i_sectors[0] = blocks_lba;
    for (uint32_t i = 1; i < 13; ++i)
    {
        p_inode->i_sectors[i] = 0;
    }
    
    // 将根目录的inode写入硬盘
    write_disk(part->my_disk, p_inode, inode_table_lba, 1);

    /***  数据块区初始化   ***/
    dentry *p_dentry = (dentry *)buf;
    memset(p_dentry, 0, SECTOR_SIZE);           // 缓冲区清零，保证写入根目录表时除了 . 和 .. 以外其他所有目录项均为 FT_UNKNOWN

    // 在根目录表中设置 . 和 .. 两个目录项
    p_dentry[0].f_type = FT_DIRECTORY;
    p_dentry[0].i_no = root_i_no;
    strcpy(p_dentry[0].filename, ".");

    p_dentry[1].f_type = FT_DIRECTORY;
    p_dentry[1].i_no = root_i_no;
    strcpy(p_dentry[1].filename, "..");

    // 将根目录表写入硬盘
    write_disk(part->my_disk, p_dentry, blocks_lba, 1);

    // 输出文件系统信息
    printk("%s\n", part->name);
    printk("magic number: 0x%x     root_i_no: %u     dentry_size: %u\n", magic, root_i_no, dentry_size);
    printk("partition: LBA  %u   sectors  %u\n", part_lba, part_sects);
    printk("block bitmap: LBA  %u   sectors  %u\n", block_bitmap_lba, block_bitmap_sects);
    printk("inode bitmap: LBA  %u   sectors  %u\n", inode_bitmap_lba, inode_bitmap_sects);
    printk("inode table: LBA  %u   sectors  %u\n", inode_table_lba, inode_table_sects);
    printk("blocks area: LBA  %u   sectors  %u\n", blocks_lba, blocks_sects);
    
    sys_free(buf);
}   

// 作为list_traversal的回调函数对不存在可识别文件系统的分区进行格式化
bool part_listnode_format(node *pnode, int arg UNUSED)
{
    superblock *sb = (superblock *)kmalloc(SECTOR_SIZE);
    ASSERT(sb);

    // 将超级块读入后根据魔数判断该分区是否存在文件系统
    partition *part = member2struct(pnode, partition, list_node);
    read_disk(part->my_disk, sb, part->start_lba + 1, 1);
    if (sb->magic != FS_MAGIC)
    {
        partition_format(part);
    }

    sys_free(sb);
    return false;
}    

// 挂载指定分区
void partition_mount(partition *part)
{
    superblock *sb = (superblock *)kmalloc(SECTOR_SIZE);
    ASSERT(sb);

    // 读入超级块
    read_disk(part->my_disk, sb, part->start_lba + 1, 1);
    ASSERT(sb->magic == FS_MAGIC);
    part->sb = (superblock *)kmalloc(sizeof(superblock));
    ASSERT(part->sb);
    memcpy(part->sb, sb, sizeof(superblock));

    // 读入块位图
    part->block_bitmap.bytes_length = sb->block_bitmap_sects * SECTOR_SIZE;
    part->block_bitmap.btmp_ptr = (uint8_t *)kmalloc(part->block_bitmap.bytes_length);
    ASSERT(part->block_bitmap.btmp_ptr);
    read_disk(part->my_disk, part->block_bitmap.btmp_ptr, sb->block_bitmap_lba, sb->block_bitmap_sects);

    // 读入inode位图
    part->inode_bitmap.bytes_length = sb->inode_bitmap_sects * SECTOR_SIZE;
    part->inode_bitmap.btmp_ptr = (uint8_t *)kmalloc(part->inode_bitmap.bytes_length);
    ASSERT(part->inode_bitmap.btmp_ptr);
    read_disk(part->my_disk, part->inode_bitmap.btmp_ptr, sb->inode_bitmap_lba, sb->inode_bitmap_sects);

    // 初始化打开文件链表
    list_init(&part->inode_list);

    // 初始化挂载信息
    list_init(&part->mount_list);
    part->parent_part = NULL;
    part->mount_i_no = 0;
    part->mount_p_i_no = 0;

    // 输出挂载分区的信息
    /* printk("mounted partition: %s\n", part->name);
    printk("magic number: 0x%x     root_i_no: %u     dentry_size: %u\n", sb->magic, sb->root_i_no, sb->dentry_size);
    printk("partition: LBA  %u   sectors  %u\n", sb->part_lba, sb->part_sects);
    printk("block bitmap: LBA  %u   sectors  %u\n", sb->block_bitmap_lba, sb->block_bitmap_sects);
    printk("inode bitmap: LBA  %u   sectors  %u\n", sb->inode_bitmap_lba, sb->inode_bitmap_sects);
    printk("inode table: LBA  %u   sectors  %u\n", sb->inode_table_lba, sb->inode_table_sects);
    printk("blocks area: LBA  %u   sectors  %u\n", sb->blocks_lba, sb->blocks_sects); */

    sys_free(sb);
}  

// 作为list_traversal的回调函数对名为part_name的分区进行挂载
bool part_listnode_mount(node *pnode, int part_name)
{
    partition *part = member2struct(pnode, partition, list_node);
    if (!strcmp(part->name, (const char *)part_name))
    {
        partition_mount(part);
        return true;
    }
    return false;
}  

// 获取指定路径的路径深度
uint32_t path_depth(const char *pathname)
{
    ASSERT(pathname != NULL);
    uint32_t depth = 0;
    while (*pathname)
    {
        // 跳过连续的分隔符
        while (*pathname == '/')
        {
            pathname++;
        }
        // 避免将形如 /home/ubuntu/ 的路径深度记为3
        if (*pathname == 0)
        {
            break;
        }
        while (*pathname != '/' && *pathname != 0)
        {
            pathname++;
        }
        depth++;
    }
    return depth;
} 

// 路径解析，每解析一层，返回该层的文件名filename和下一个分隔符的地址
char *path_parse(const char *pathname, char *filename)
{
    ASSERT(filename != NULL && pathname != NULL);
    // 跳过连续的分隔符
    while (pathname[0] == '/')
    {
        pathname++;
    }

    while (pathname[0] != '/' && pathname[0] != 0)
    {
        filename[0] = pathname[0];
        pathname++;
        filename++;
    }

    return (char *)pathname;
}    

// 按照给定的路径搜索文件，将结构存储在search_record结构中
void search_file(const char *pathname, search_record *sr)
{
    memset(sr, 0, sizeof(search_record));

    // 区分绝对路径和相对路径
    sr->parent_dir = (pathname[0] == '/' ? dir_open(root_part, root_part->sb->root_i_no) : dir_open(current->wd_part, current->wd_i_no));

    partition *tmp_part;
    uint32_t tmp_i_no;
    partition *ret_part;
    dentry dir_e; 
    char name[MAX_FILENAME_LEN] = {0};
    char *subpath = path_parse(pathname, name);
    if (!name[0])
    {
        // 当路径是 / 时
        strcat(sr->search_path, "/");
        sr->f_type = FT_DIRECTORY;
        sr->part = root_part;
        sr->i_no = root_part->sb->root_i_no;
        return;
    }
    while (name[0])
    {
        if (sr->search_path[0] || pathname[0] == '/')
        {
            // 相对路径的search_path不需要以 ‘/’ 开头
            strcat(sr->search_path, "/");
        }
        strcat(sr->search_path, name);

        ret_part = dir_search(sr->parent_dir, name, &dir_e);
        if (!ret_part)
        {
            sr->f_type = FT_UNKNOWN;
            return;
        }
        if (dir_e.f_type == FT_REGULAR)
        {
            sr->f_type = FT_REGULAR;
            sr->part = ret_part;
            sr->i_no = dir_e.i_no;
            return;
        }

        // 这里需要把原来目录的信息暂存起来，如果目标文件是个目录，可用该信息找到并打开目标文件的父目录
        tmp_part = sr->parent_dir->p_inode->part;
        tmp_i_no = sr->parent_dir->p_inode->i_no;
        dir_close(sr->parent_dir);
        sr->parent_dir = dir_open(ret_part, dir_e.i_no);
        memset(name, 0, MAX_FILENAME_LEN);
        subpath = path_parse(subpath, name);
    }
    sr->f_type = FT_DIRECTORY;
    sr->part = ret_part;
    sr->i_no = dir_e.i_no;
    dir_close(sr->parent_dir);
    sr->parent_dir = dir_open(tmp_part, tmp_i_no);
}  

// 在目录pdir下创建一个名为filename的文件
int32_t file_create(dir *pdir, const char *filename)
{
    if (strlen(filename) >= MAX_FILENAME_LEN)
    {
        return -1;
    }

    uint32_t new_i_no = bitmap_alloc(pdir->p_inode->part, INODE_BITMAP);
    if (new_i_no == -1)
    {
        return -1;
    }
    inode new_inode;
    inode_init(pdir->p_inode->part, new_i_no, &new_inode);

    dentry dir_e;
    dentry_init(new_i_no, filename, FT_REGULAR, &dir_e);

    if (!add_dentry(pdir, &dir_e))
    {
        bitmap_set(&pdir->p_inode->part->inode_bitmap, new_i_no, 0);
        return -1;
    }

    inode_sync(&new_inode);
    bitmap_sync(pdir->p_inode->part, INODE_BITMAP, new_i_no);
    return new_i_no;
}

// 以指定方式打开文件
int32_t file_open(partition *part, const uint32_t i_no, const uint8_t flag)
{
    int32_t g_idx = get_free_slot_in_file_table();
    int32_t l_idx = get_free_slot_in_fd_table();
    if (l_idx == -1 || g_idx == -1)
    {
        return -1;
    }

    file_table[g_idx].p_inode = inode_open(part, i_no);
    file_table[g_idx].flag = flag;
    file_table[g_idx].f_pos = 0;

    current->fd_table[l_idx] = g_idx;

    return l_idx;
}     

// 关闭p_file指向的文件
int32_t file_close(file *p_file)
{
    if (!p_file)
    {
        return -1;
    }
    inode_close(p_file->p_inode);
    p_file->p_inode = NULL;
    return 0;
}    

// 将buf处的cnt个字节写入p_file指向的文件中
int32_t file_write(file *p_file, const void *buf, uint32_t cnt)
{
    if (!p_file || !buf)
    {
        return -1;
    }

    if (!cnt)
    {
        return 0;
    }

    uint32_t sec_cnt_before_writing = DIV_ROUND_UP(p_file->p_inode->i_size, SECTOR_SIZE);
    uint32_t sec_cnt_after_writing = DIV_ROUND_UP(p_file->p_inode->i_size + cnt, SECTOR_SIZE);

    uint32_t *all_blocks = (uint32_t *)kmalloc(560);
    ASSERT(all_blocks);
    memset(all_blocks, 0, 560);

    // 将需要的块预先分配
    uint8_t cond;               // 块分配失败时可根据该值确定回滚的步骤
    uint32_t idx_fail_to_alloc; // 记录分配失败时正在等待写入的all_blocks元素的下标值
    uint32_t blk_lba;
    if (sec_cnt_before_writing < sec_cnt_after_writing)
    {
        if (sec_cnt_before_writing <= 12)
        {
            if (sec_cnt_after_writing <= 12)
            {
                if (p_file->p_inode->i_size % SECTOR_SIZE)
                {
                    all_blocks[sec_cnt_before_writing - 1] = p_file->p_inode->i_sectors[sec_cnt_before_writing - 1];
                }

                // 分配数据块
                for (uint32_t i = sec_cnt_before_writing; i < sec_cnt_after_writing; ++i)
                {
                    blk_lba = bitmap_alloc(p_file->p_inode->part, BLOCK_BITMAP);
                    if (blk_lba == -1)
                    {
                        cond = 0;
                        idx_fail_to_alloc = i;
                        goto roll_back;
                    }
                    all_blocks[i] = p_file->p_inode->i_sectors[i] = blk_lba;
                }
            }
            else
            {
                if (p_file->p_inode->i_size % SECTOR_SIZE)
                {
                    all_blocks[sec_cnt_before_writing - 1] = p_file->p_inode->i_sectors[sec_cnt_before_writing - 1];
                }

                // 分配一个索引块
                uint32_t indirect_blk_lba = bitmap_alloc(p_file->p_inode->part, BLOCK_BITMAP);
                if (indirect_blk_lba == -1)
                {
                    sys_free(all_blocks);
                    return -1;
                }
                p_file->p_inode->i_sectors[12] = indirect_blk_lba;

                // 分配数据块
                for (uint32_t i = sec_cnt_before_writing; i < sec_cnt_after_writing; ++i)
                {
                    blk_lba = bitmap_alloc(p_file->p_inode->part, BLOCK_BITMAP);
                    if (blk_lba == -1)
                    {
                        cond = 1;
                        idx_fail_to_alloc = i;
                        goto roll_back;
                    }
                    all_blocks[i] = blk_lba;
                    if (i < 12)
                    {
                        p_file->p_inode->i_sectors[i] = blk_lba;
                    }
                }

                // 将索引块和位图同步到硬盘中
                write_disk(p_file->p_inode->part->my_disk, all_blocks + 12, indirect_blk_lba, 1);
                bitmap_sync(p_file->p_inode->part, BLOCK_BITMAP, indirect_blk_lba - p_file->p_inode->part->sb->blocks_lba);
            }
        }
        else
        {
            ASSERT(p_file->p_inode->i_sectors[12]);
            read_disk(p_file->p_inode->part->my_disk, all_blocks + 12, p_file->p_inode->i_sectors[12], 1);

            // 分配数据块
            for (uint32_t i = sec_cnt_before_writing; i < sec_cnt_after_writing; ++i)
            {
                blk_lba = bitmap_alloc(p_file->p_inode->part, BLOCK_BITMAP);
                if (blk_lba == -1)
                {
                    cond = 0;
                    idx_fail_to_alloc = i;
                    goto roll_back;
                }
                all_blocks[i] = blk_lba;
            }

            // 将索引块同步到硬盘中
            write_disk(p_file->p_inode->part->my_disk, all_blocks + 12, p_file->p_inode->i_sectors[12], 1);
        }

        // 将位图同步到硬盘中
        for (uint32_t i = sec_cnt_before_writing; i < sec_cnt_after_writing; ++i)
        {
            bitmap_sync(p_file->p_inode->part, BLOCK_BITMAP, all_blocks[i] - p_file->p_inode->part->sb->blocks_lba);
        }
    }
    else
    {
        if (sec_cnt_before_writing <= 12)
        {
            // 很显然可以断言这里的 i_size 必然不是 SECTOR_SIZE 的整数倍
            all_blocks[sec_cnt_before_writing - 1] = p_file->p_inode->i_sectors[sec_cnt_before_writing - 1];
        }
        else
        {
            ASSERT(p_file->p_inode->i_sectors[12]);
            read_disk(p_file->p_inode->part->my_disk, all_blocks + 12, p_file->p_inode->i_sectors[12], 1);
        }
    }

    // 将数据写入文件
    p_file->f_pos = p_file->p_inode->i_size;        // 从文件尾开始写入
    uint32_t sec_idx = p_file->f_pos / SECTOR_SIZE;
    uint32_t sec_offset = p_file->f_pos % SECTOR_SIZE;
    uint32_t bytes_left_in_sec = SECTOR_SIZE - sec_offset;
    uint32_t bytes_write_done = 0;
    uint32_t bytes_to_write;
    void *buf_to_write = kmalloc(SECTOR_SIZE);
    ASSERT(buf_to_write);
    while (cnt)
    {
        ASSERT(all_blocks[sec_idx]);
        bytes_to_write = (cnt > bytes_left_in_sec) ? bytes_left_in_sec : cnt;

        read_disk(p_file->p_inode->part->my_disk, buf_to_write, all_blocks[sec_idx], 1);
        memcpy(buf_to_write + sec_offset, buf, bytes_to_write);
        write_disk(p_file->p_inode->part->my_disk, buf_to_write, all_blocks[sec_idx], 1);

        buf += bytes_to_write;
        bytes_write_done += bytes_to_write;
        cnt -= bytes_to_write;
        ++sec_idx;
        sec_offset = 0;
        bytes_left_in_sec = SECTOR_SIZE;
    }
    p_file->f_pos += bytes_write_done;

    p_file->p_inode->i_size = p_file->f_pos;
    inode_sync(p_file->p_inode);

    sys_free(all_blocks);
    sys_free(buf_to_write);
    return bytes_write_done;

// 块分配失败时回滚块位图
roll_back:
    switch (cond)
    {
        case 0:
        {
            bitmap_set(&p_file->p_inode->part->block_bitmap, 
            p_file->p_inode->i_sectors[12] - p_file->p_inode->part->sb->blocks_lba, 0);

            p_file->p_inode->i_sectors[12] = 0;
        }
        case 1:
        {
            for (uint32_t i = sec_cnt_before_writing; i < idx_fail_to_alloc; ++i)
            {
                bitmap_set(&p_file->p_inode->part->block_bitmap, all_blocks[i] - p_file->p_inode->part->sb->blocks_lba, 0);
                if (i < 12)
                {
                    p_file->p_inode->i_sectors[i] = 0;
                }
            }
            sys_free(all_blocks);
            return -1;
        }
    }
    return -1;          // 该行代码不会被执行到，作用主要是解除编译器警告
} 

// 从p_file指向的文件读取cnt个字节到buf处
int32_t file_read(file *p_file, void *buf, uint32_t cnt)
{
    if (!p_file || !buf)
    {
        return -1;
    }

    if (!cnt)
    {
        return 0;
    }

    uint32_t *all_blocks = (uint32_t *)kmalloc(560);
    ASSERT(all_blocks);
    memset(all_blocks, 0, 560);
    for (uint32_t i = 0; i < 12; ++i)
    {
        all_blocks[i] = p_file->p_inode->i_sectors[i];
    }
    if (p_file->p_inode->i_sectors[12])
    {
        read_disk(p_file->p_inode->part->my_disk, all_blocks + 12, p_file->p_inode->i_sectors[12], 1);
    }

    uint32_t sec_idx = p_file->f_pos / SECTOR_SIZE;
    uint32_t sec_offset = p_file->f_pos % SECTOR_SIZE;
    uint32_t bytes_left_in_sec = SECTOR_SIZE - sec_offset;
    uint32_t bytes_left_in_file = p_file->p_inode->i_size - p_file->f_pos;
    uint32_t bytes_to_read;
    uint32_t bytes_read_done = 0;
    void *buf_to_read = kmalloc(SECTOR_SIZE);
    ASSERT(buf_to_read);
    while (p_file->f_pos < p_file->p_inode->i_size && cnt)
    {
        bytes_to_read = (bytes_left_in_file > cnt) ? cnt : bytes_left_in_file;
        bytes_to_read = (bytes_to_read > bytes_left_in_sec) ? bytes_left_in_sec : bytes_to_read;

        ASSERT(all_blocks[sec_idx]);
        read_disk(p_file->p_inode->part->my_disk, buf_to_read, all_blocks[sec_idx], 1);
        memcpy(buf, buf_to_read + sec_offset, bytes_to_read);

        bytes_read_done += bytes_to_read;
        buf += bytes_to_read;
        p_file->f_pos += bytes_to_read;
        cnt -= bytes_to_read;
        bytes_left_in_file -= bytes_to_read;
        ++sec_idx;
        sec_offset = 0;
        bytes_left_in_sec = SECTOR_SIZE;
    }

    sys_free(all_blocks);
    sys_free(buf_to_read);
    return bytes_read_done;
}
