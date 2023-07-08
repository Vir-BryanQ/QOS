#include "_syscall.h"
#include "thread.h"
#include "string.h"
#include "console.h"
#include "global.h"
#include "debug.h"
#include "interrupt.h"
#include "print.h"
#include "fs.h"
#include "stdio.h"
#include "inode.h"
#include "ide.h"
#include "process.h"
#include "ioqueue.h"
#include "keyboard.h"
#include "exec.h"
#include "pipe.h"

typedef void *syscall;

// 真正系统调用服务函数的入口地址表，由syscall_handler访问维护
syscall syscall_table[] = 
{
    sys_getpid, 
    sys_write,
    sys_malloc,
    sys_free,
    sys_open,
    sys_close,
    sys_read,
    sys_lseek,
    sys_unlink,
    sys_mkdir,
    sys_opendir,
    sys_closedir,
    sys_readdir,
    sys_rmdir,
    sys_rewinddir,
    sys_getcwd,
    sys_chdir,
    sys_stat,
    sys_mount,
    sys_umount,
    sys_fork,
    sys_putchar,
    sys_clear,
    sys_ps,
    sys_execv,
    sys_exit,
    sys_wait,
    sys_pipe,
    sys_fd_redirect
};

/***        真正提供服务的系统调用函数          ***/


uint32_t sys_getpid(void)
{
    return current->pid;
}

int32_t sys_write(const uint32_t fd, const void *buf, uint32_t cnt)
{
    if (fd >= MAX_FILES_OPEN_PER_PROC)
    {
        printk("sys_write: invalid fd.\n");
        return -1;
    }

    if (is_pipe(fd))
    {
        return pipe_write(fd, (uint8_t *)buf, cnt);
    }

    if (fd == stdout)
    {
        console_put_str(buf);
        return strlen(buf);
    }
    uint32_t g_idx = current->fd_table[fd];
    if (g_idx == -1)
    {
        printk("sys_write: fd provided hasn't been attached with any file.\n");
        return -1;
    }
    if (!(file_table[g_idx].flag & O_WRONLY || file_table[g_idx].flag & O_RDWR))
    {
        printk("sys_write: unable to write a file(fd = %u) opened without O_WRONLY flag or O_RDWR flag.\n", fd);
        return -1;
    }
    return file_write(file_table + g_idx, buf, cnt);
}

void *sys_malloc(const uint32_t size)
{
    if (current->pdt_base && current->u_mblock_descs[0].free_list.head.next->prev != &current->u_mblock_descs[0].free_list.head)
    {
        for (uint32_t i = 0; i < MBLOCK_DESC_CNT; ++i)
        {
            /* 由于链接关系的错乱，必须修正进程的内存块描述符数组 */
            if (!current->u_mblock_descs[i].free_list.length)
            {
                current->u_mblock_descs[i].free_list.head.next = &current->u_mblock_descs[i].free_list.tail;
                current->u_mblock_descs[i].free_list.tail.prev = &current->u_mblock_descs[i].free_list.head;
            }
            else
            {
                current->u_mblock_descs[i].free_list.head.next->prev = &current->u_mblock_descs[i].free_list.head;
                current->u_mblock_descs[i].free_list.tail.prev->next = &current->u_mblock_descs[i].free_list.tail;
            }

            /* 必须修正arena中的描述符指针, 因为原来的指针指向的是父进程的描述符 */
            list_traversal(&current->u_mblock_descs[i].free_list, fix_arena_pdesc, (int)&current->u_mblock_descs[i]);
        }
    }

    return _malloc(current->pdt_base ? PF_USER : PF_KERNEL, size);
}

void sys_free(void *ptr)
{
    if (current->pdt_base && current->u_mblock_descs[0].free_list.head.next->prev != &current->u_mblock_descs[0].free_list.head)
    {
        for (uint32_t i = 0; i < MBLOCK_DESC_CNT; ++i)
        {
            /* 由于链接关系的错乱，必须修正进程的内存块描述符数组 */
            if (!current->u_mblock_descs[i].free_list.length)
            {
                current->u_mblock_descs[i].free_list.head.next = &current->u_mblock_descs[i].free_list.tail;
                current->u_mblock_descs[i].free_list.tail.prev = &current->u_mblock_descs[i].free_list.head;
            }
            else
            {
                current->u_mblock_descs[i].free_list.head.next->prev = &current->u_mblock_descs[i].free_list.head;
                current->u_mblock_descs[i].free_list.tail.prev->next = &current->u_mblock_descs[i].free_list.tail;
            }

            /* 必须修正arena中的描述符指针, 因为原来的指针指向的是父进程的描述符 */
            list_traversal(&current->u_mblock_descs[i].free_list, fix_arena_pdesc, (int)&current->u_mblock_descs[i]);
        }
    }

    phy_mem_pool *p_pm_pool = (ptr >= kernel_vm_pool.virt_addr_start ? &kernel_pm_pool : &user_pm_pool);
    mutex_lock_acquire(&p_pm_pool->mutex);

    ASSERT(ptr != NULL);
    ASSERT(ptr >= (void *)KERNEL_VMP_START || 
            (ptr < (void *)KERNEL_SPACE_START && ptr >= current->user_vm_pool.virt_addr_start));

    arena *parena = BLOCK2ARENA(ptr);
    ASSERT(parena->large == 0 || parena->large == 1);
    if (parena->large && parena->pdesc == NULL)
    {
        mfree_pages(parena->cnt, (void *)parena);
    }
    else
    {
        mem_block_desc *pdesc = parena->pdesc;
        mem_block *blk_addr = ARENA2BLOCK(parena, (((uint32_t)ptr - (uint32_t)parena - sizeof(arena)) / pdesc->block_size));
        ASSERT(!list_find(&pdesc->free_list, &blk_addr->list_node));
        list_push_back(&pdesc->free_list, &blk_addr->list_node);
        parena->cnt++;

        // 若回收内存块后该arena的所有块均空闲，应释放该arena
        if (parena->cnt == pdesc->block_cnt_per_arena)
        {
            intr_status old_status = set_intr_status(INTR_OFF);
            // 此处要注意在释放arena之前要将所有的内存块从free_list中去除，否则会导致奇怪的缺页故障
            for (uint32_t blk_idx = 0; blk_idx < parena->cnt; blk_idx++)
            {
                blk_addr = ARENA2BLOCK(parena, blk_idx);
                ASSERT(list_find(&pdesc->free_list, &blk_addr->list_node));
                list_remove(&pdesc->free_list, &blk_addr->list_node);
            }
            set_intr_status(old_status);

            mfree_pages(1, (void *)parena);
        }
    }

    mutex_lock_release(&p_pm_pool->mutex);
}

int32_t sys_open(const char* pathname, const uint8_t flag)
{
    ASSERT (flag <= 7);

    if (pathname[strlen(pathname) - 1] == '/')
    {
        // 如果提供的路径最后一个字符是 ‘/’, 则认为该路径指向一个目录
        printk("Unable to open a directory. Please use opendir() instead.\n");
        return -1;
    }

    search_record *sr = (search_record *)kmalloc(sizeof(search_record));
    ASSERT(sr);
    search_file(pathname, sr);
    if (sr->f_type != FT_UNKNOWN)
    {
        if (path_depth(sr->search_path) < path_depth(pathname))
        {
            // 处理用户提供的路径中把某个文件当目录的情况
            printk("Unable to access '%s': not a directory.\n", sr->search_path);
            dir_close(sr->parent_dir);
            sys_free(sr);
            return -1;
        }
        if (flag & O_CREAT)
        {
            printk("A file or directory with the same name already exists.\n");
            dir_close(sr->parent_dir);
            sys_free(sr);
            return -1;
        }
        else
        {
            if (sr->f_type == FT_DIRECTORY)
            {
                printk("Unable to open a directory '%s'. Please use opendir() instead.\n", sr->search_path);
                dir_close(sr->parent_dir);
                sys_free(sr);
                return -1;
            }

            int32_t ret_val = file_open(sr->part, sr->i_no, flag);
            dir_close(sr->parent_dir);
            sys_free(sr);
            return ret_val;
        }
    }
    else
    {
        if (flag & O_CREAT)
        {
            if (path_depth(pathname) == path_depth(sr->search_path))
            {
                char *filename = strrchr(sr->search_path, '/');
                filename = (filename ? (filename + 1) : sr->search_path);

                int32_t i_no = file_create(sr->parent_dir, filename);
                if (i_no == -1)
                {
                    dir_close(sr->parent_dir);
                    sys_free(sr);
                    return -1;
                }
                int32_t ret_val = file_open(sr->parent_dir->p_inode->part, (uint32_t)i_no, flag);
                dir_close(sr->parent_dir);
                sys_free(sr);
                return ret_val;
            }
            else
            {
                printk("Unable to access '%s': fail to create a file.\n", sr->search_path);
                dir_close(sr->parent_dir);
                sys_free(sr);
                return -1;
            }
        }
        else
        {
            printk("Unable to access '%s': no this file or directory.\n", sr->search_path);
            dir_close(sr->parent_dir);
            sys_free(sr);
            return -1;
        }
    }
}

int32_t sys_close(const uint32_t fd)
{
    if ((fd >= MAX_FILES_OPEN_PER_PROC) || (fd < 3))
    {
        return -1;
    }

    if (is_pipe(fd))
    {
        return pipe_close(fd);
    } 

    int32_t g_idx = current->fd_table[fd];
    if (g_idx < 3)
    {
        return -1;
    }
    current->fd_table[fd] = -1;
    return file_close(&file_table[g_idx]);
}

int32_t sys_read(const uint32_t fd, void *buf, const uint32_t cnt)
{
    if (fd >= MAX_FILES_OPEN_PER_PROC)
    {
        printk("sys_read: invalid fd.\n");
        return -1;
    }

    if (is_pipe(fd))
    {
        return pipe_read(fd, (uint8_t *)buf, cnt);
    }
    
    if (fd == stdin)
    {
        for (uint32_t i = 0; i < cnt; ++i, ++buf)
        {
            *(uint8_t *)buf = ioqueue_pop_front(&kb_buf);
        }
        return cnt;
    }

    uint32_t g_idx = current->fd_table[fd];
    if (g_idx == -1)
    {
        printk("sys_read: fd provided hasn't been attached with any file.\n");
        return -1;
    }
    if (file_table[fd].flag & O_WRONLY)
    {
        printk("sys_read: unable to read a file(fd = %u) opened with O_WRONLY flag\n", fd);
        return -1;
    }
    return file_read(file_table + g_idx, buf, cnt);
}

int32_t sys_lseek(const uint32_t fd, const int32_t offset, const uint8_t whence)
{
    if (fd >= MAX_FILES_OPEN_PER_PROC)
    {
        printk("sys_lseek: invalid fd.\n");
        return -1;
    }
    uint32_t g_idx = current->fd_table[fd];
    if (g_idx == -1)
    {
        printk("sys_lseek: fd provided hasn't been attached with any file.\n");
        return -1;
    }

    uint32_t new_pos;
    switch (whence)
    {
        case SEEK_SET:
        {
            new_pos = offset;
            break;
        }
        case SEEK_CUR:
        {
            new_pos = file_table[g_idx].f_pos + offset;
            break;
        }
        case SEEK_END:
        {
            new_pos = file_table[g_idx].p_inode->i_size + offset;
            break;
        }
        default:
        {
            printk("sys_lseek: the value of argument 'whence' is invalid.\n");
            return -1;
        }
    }

    if (new_pos >= file_table[g_idx].p_inode->i_size)
    {
        printk("sys_lseek: the f_pos of file(fd = %u) has exceeded the filesize\n", fd);
        return -1;
    }

    file_table[g_idx].f_pos = new_pos;
    return new_pos;
}

int32_t sys_unlink(const char *pathname)
{
    search_record *sr = (search_record *)kmalloc(sizeof(search_record));
    ASSERT(sr);
    search_file(pathname, sr);

    if (sr->f_type == FT_UNKNOWN)
    {
        printk("sys_unlink: unable to find '%s'\n", sr->search_path);
        dir_close(sr->parent_dir);
        sys_free(sr);
        return -1;
    }
    else if (sr->f_type == FT_DIRECTORY)
    {
        printk("Unable to unlink a directory '%s'. Please use rmdir() instead.\n", sr->search_path);
        dir_close(sr->parent_dir);
        sys_free(sr);
        return -1;
    }

    // 检测文件是否已打开
    for (uint32_t i = 0; i < MAX_FILES_OPEN; ++i)
    {
        if (file_table[i].p_inode && (file_table[i].p_inode->part == sr->part) && (file_table[i].p_inode->i_no == sr->i_no))
        {
            printk("sys_unlink: unable to delete the file '%s': this file has been opened\n", sr->search_path);
            dir_close(sr->parent_dir);
            sys_free(sr);
            return -1;
        }
    }

    inode_release(sr->part, sr->i_no);
    
    char *filename = strrchr(sr->search_path, '/');
    filename = (filename ? (filename + 1) : sr->search_path);
    del_dentry(sr->parent_dir, filename);

    dir_close(sr->parent_dir);
    sys_free(sr);
    return 0;
}

int32_t sys_mkdir(const char *pathname)
{
    search_record *sr = (search_record *)kmalloc(sizeof(search_record));
    ASSERT(sr);
    search_file(pathname, sr);
    if (sr->f_type == FT_UNKNOWN)
    {
        if (path_depth(sr->search_path) == path_depth(pathname))
        {
            char *filename = strrchr(sr->search_path, '/');
            filename = (filename ? (filename + 1) : sr->search_path);
            int32_t ret_val = dir_create(sr->parent_dir, filename);
            dir_close(sr->parent_dir);
            sys_free(sr);
            return ret_val;
        }
        else
        {
            printk("sys_mkdir: unable to access the directory '%s'\n", sr->search_path);
            dir_close(sr->parent_dir);
            sys_free(sr);
            return -1;
        }
    }
    else
    {
        if (path_depth(sr->search_path) < path_depth(pathname))
        {
            printk("sys_mkdir: unable to access '%s': not a directory.\n", sr->search_path);
            dir_close(sr->parent_dir);
            sys_free(sr);
            return -1;
        }
        printk("sys_mkdir: a file or directory with the same name already exists\n");
        dir_close(sr->parent_dir);
        sys_free(sr);
        return -1;
    }
}

dir *sys_opendir(const char *pathname)
{
    search_record *sr = (search_record *)kmalloc(sizeof(search_record));
    ASSERT(sr);
    search_file(pathname, sr);

    if (sr->f_type != FT_UNKNOWN)
    {
        if (sr->f_type == FT_REGULAR)
        {
            printk("sys_opendir: unable to open a regular file and please use open() instead\n");
            dir_close(sr->parent_dir);
            sys_free(sr);
            return NULL;
        }
        dir *pdir = dir_open(sr->part, sr->i_no);
        dir_close(sr->parent_dir);
        sys_free(sr);
        return pdir;
    }
    else
    {
        printk("sys_opendir: unable to find the directory '%s'\n", sr->search_path);
        dir_close(sr->parent_dir);
        sys_free(sr);
        return NULL;
    }
}

int32_t sys_closedir(dir *pdir)
{
    if (pdir)
    {
        dir_close(pdir);
        return 0;
    }
    return -1;
}

dentry *sys_readdir(dir *pdir)
{
    if (!pdir)
    {
        return NULL;
    }
    return dir_read(pdir);
}

int32_t sys_rmdir(const char* pathname)
{
    search_record *sr = (search_record *)kmalloc(sizeof(search_record));
    ASSERT(sr);
    search_file(pathname, sr);

    if (sr->f_type != FT_UNKNOWN)
    {
        if (sr->f_type == FT_REGULAR)
        {
            printk("sys_rmdir: unable to remove a regular file and please use unlink() instead\n");
            dir_close(sr->parent_dir);
            sys_free(sr);
            return -1;
        }
        inode *p_inode = inode_open(sr->part, sr->i_no);
        ASSERT(p_inode->i_size >= 2 * sizeof(dentry));
        if (p_inode->i_size > 2 * sizeof(dentry))
        {
            printk("sys_rmdir: unable to remove a non-empty directory '%s'\n", sr->search_path);
            inode_close(p_inode);
            dir_close(sr->parent_dir);
            sys_free(sr);
            return -1;
        }
        inode_close(p_inode);

        inode_release(sr->part, sr->i_no);

        char *filename = strrchr(sr->search_path, '/');
        filename = (filename ? (filename + 1) : sr->search_path);
        del_dentry(sr->parent_dir, filename);
        
        dir_close(sr->parent_dir);
        sys_free(sr);
        return 0;
    }
    else
    {
        printk("sys_rmdir: unable to find '%s'\n", sr->search_path);
        dir_close(sr->parent_dir);
        sys_free(sr);
        return -1;
    }
}

void sys_rewinddir(dir *pdir)
{
    if (pdir)
    {
        pdir->d_pos = 0;
    }
}

char *sys_getcwd(char *buf, uint32_t size)
{
    if (!buf)
    {
        buf = kmalloc(MAX_PATH_LEN);
        ASSERT(buf);
        size = MAX_PATH_LEN;
    }
    memset(buf, 0, size);

    // 如果是根目录
    if (current->wd_part == root_part && current->wd_i_no == root_part->sb->root_i_no)
    {
        strcat(buf, "/");
        return buf;
    }

    parent_dir_info pd_inf;
    partition *part = current->wd_part;
    uint32_t i_no = current->wd_i_no;
    char reversed_path[MAX_PATH_LEN] = {0};         // 存储反向路径，类似于：/ubuntu/home
    while (!(part == root_part && i_no == root_part->sb->root_i_no))
    {
        find_parent_dir(part, i_no, &pd_inf);
        get_child_dir_name(&pd_inf, reversed_path);
        part = pd_inf.part;
        i_no = pd_inf.i_no;
    }

    // 将反向路径转换为正向路径
    char *last_slash;
    while((last_slash = strrchr(reversed_path, '/')))
    {
        strcat(buf, last_slash);
        *last_slash = 0;
    }

    return buf;
}

int32_t sys_chdir(const char *pathname)
{
    search_record *sr = (search_record *)kmalloc(sizeof(search_record));
    ASSERT(sr);
    search_file(pathname, sr);

    if (sr->f_type != FT_UNKNOWN)
    {
        if (sr->f_type == FT_REGULAR)
        {
            printk("sys_chdir: '%s': not a directory\n", sr->search_path);
            dir_close(sr->parent_dir);
            sys_free(sr);
            return -1;
        }
        current->wd_part = sr->part;
        current->wd_i_no = sr->i_no;
        dir_close(sr->parent_dir);
        sys_free(sr);
        return 0;
    }
    else
    {
        printk("sys_chdir: unable to find '%s'\n", sr->search_path);
        dir_close(sr->parent_dir);
        sys_free(sr);
        return -1;
    }
}

int32_t sys_stat(const char *pathname, struct stat *buf)
{
    search_record *sr = (search_record *)kmalloc(sizeof(search_record));
    ASSERT(sr);
    search_file(pathname, sr);

    if (sr->f_type != FT_UNKNOWN)
    {
        if (path_depth(sr->search_path) < path_depth(pathname))
        {
            printk("sys_stat: '%s': not a directory\n", sr->search_path);
            dir_close(sr->parent_dir);
            sys_free(sr);
            return -1;
        }
        inode *p_inode = inode_open(sr->part, sr->i_no);
        buf->i_size = p_inode->i_size;
        buf->i_no = p_inode->i_no;
        buf->f_type = sr->f_type;
        inode_close(p_inode);
        dir_close(sr->parent_dir);
        sys_free(sr);
        return 0;
    }
    else
    {
        // printk("sys_stat: unable to find '%s'\n", sr->search_path);
        dir_close(sr->parent_dir);
        sys_free(sr);
        return -1;
    }
}

// 辅助函数
extern bool mnt_pt_check(node *pnode, int arg);
bool mnt_pt_check(node *pnode, int arg)
{
    mount_point *mnt_pt = member2struct(pnode, mount_point, list_node);
    return (mnt_pt->i_no == (uint32_t)arg);
}

int32_t sys_mount(const char *source, const char *target)
{
    search_record *sr = (search_record *)kmalloc(sizeof(search_record));
    ASSERT(sr);
    search_file(target, sr);

    if (sr->f_type !=FT_UNKNOWN)
    {
        if (sr->f_type == FT_REGULAR)
        {
            printk("sys_mount: '%s': not a directory\n", sr->search_path);
            dir_close(sr->parent_dir);
            sys_free(sr);
            return -1;
        }
        node *pnode = list_traversal(&partition_list, part_name_check, (int)source);
        if (!pnode)
        {
            printk("sys_mount: unable to find the partition named '%s'\n", source);
            dir_close(sr->parent_dir);
            sys_free(sr);
            return -1;
        }
        partition *child_part = member2struct(pnode, partition, list_node);

        // 如果目标文件系统原来已挂载于其他挂载点，则应该先将其从原挂载点卸载
        mount_point tmp;
        if (child_part->parent_part)
        {
            node *pnode = list_traversal(&child_part->parent_part->mount_list, mnt_pt_check, child_part->mount_i_no);
            ASSERT(pnode);
            list_remove(&child_part->parent_part->mount_list, pnode);
            mount_point *mnt_pt = member2struct(pnode, mount_point, list_node);
            memcpy(&tmp, mnt_pt, sizeof(mount_point));
            sys_free(mnt_pt);
        }

        partition *parent_part;
        uint32_t mp_i_no;
        uint32_t mp_p_i_no;
        if (sr->i_no == sr->part->sb->root_i_no && sr->part->parent_part)
        {
            // 如果挂载点上已经挂载了某个文件系统

            if (sr->part->mount_list.length)
            {
                // 如果挂载点上已挂载的文件系统下面还挂载了其他的文件系统
                printk("sys_mount: target is busy\n");

                // 回滚卸载操作
                mount_point *mnt_pt = (mount_point *)kmalloc(sizeof(mount_point));
                ASSERT(mnt_pt);
                memcpy(mnt_pt, &tmp, sizeof(mount_point));
                list_push_front(&child_part->parent_part->mount_list, &mnt_pt->list_node);

                dir_close(sr->parent_dir);
                sys_free(sr);
                return -1;
            }

            parent_part = sr->part->parent_part;
            mp_i_no = sr->part->mount_i_no;
            mp_p_i_no = sr->part->mount_p_i_no;

            // 将原来已挂载的文件系统卸载
            sr->part->parent_part = NULL;
            node *pnode = list_traversal(&parent_part->mount_list, mnt_pt_check, sr->part->mount_i_no);
            if (pnode)
            {
                // 由于同一个文件系统可能会被重复挂载于同一个挂载点，因此此处无法执行 ASSERT(pnode);
                list_remove(&parent_part->mount_list, pnode);
                mount_point *mnt_pt = member2struct(pnode, mount_point, list_node);
                sys_free(mnt_pt);
            }
        }
        else
        {
            parent_part = sr->part;
            mp_i_no = sr->i_no;
            mp_p_i_no = sr->parent_dir->p_inode->i_no;
        }

        // 如果文件系统的相关信息尚未加载到内存，需要先加载文件系统的相关信息
        if (!child_part->sb)
        {
            partition_mount(child_part);
        }

        // 挂载目标文件系统
        mount_point *mnt_pt = (mount_point *)kmalloc(sizeof(mount_point));
        ASSERT(mnt_pt);
        mnt_pt->i_no = mp_i_no;
        mnt_pt->part = child_part;
        list_push_front(&parent_part->mount_list, &mnt_pt->list_node);  

        child_part->parent_part = parent_part;
        child_part->mount_i_no = mp_i_no;
        child_part->mount_p_i_no = mp_p_i_no;

        dir_close(sr->parent_dir);
        sys_free(sr);
        return 0;
    }   
    else
    {
        printk("sys_mount: unable to find '%s'\n", sr->search_path);
        dir_close(sr->parent_dir);
        sys_free(sr);
        return -1;
    }
}

int32_t sys_umount(const char *target)
{
    search_record *sr = (search_record *)kmalloc(sizeof(search_record));
    ASSERT(sr);
    search_file(target, sr);

    if (sr->f_type != FT_UNKNOWN)
    {
        if (!(sr->i_no == sr->part->sb->root_i_no && sr->part->parent_part))
        {
            printk("sys_umount: '%s': not mounted\n", sr->search_path);
            dir_close(sr->parent_dir);
            sys_free(sr);
            return -1;
        }
        if (sr->part->mount_list.length)
        {
            printk("sys_umount: target is busy\n");
            dir_close(sr->parent_dir);
            sys_free(sr);
            return -1;
        }

        node *pnode = list_traversal(&sr->part->parent_part->mount_list, mnt_pt_check, sr->part->mount_i_no);
        ASSERT(pnode);
        list_remove(&sr->part->parent_part->mount_list, pnode);
        mount_point *mnt_pt = member2struct(pnode, mount_point, list_node);
        sys_free(mnt_pt);
        sr->part->parent_part = NULL;
        
        dir_close(sr->parent_dir);
        sys_free(sr);
        return 0;
    }
    else
    {
        printk("sys_umount: unable to find '%s'\n", sr->search_path);
        dir_close(sr->parent_dir);
        sys_free(sr);
        return -1;
    }
}

int32_t sys_fork(void)
{
    task_struct *child = get_kernel_pages(1);
    ASSERT(child);

    copy_process(child);

    intr_status old_status = set_intr_status(INTR_OFF);

    list_push_back(&thread_all_list, &child->all_list_node);
    list_push_back(&thread_ready_list, &child->general_list_node);

    thread_yield();         // 尽可能让子进程先运行，减少内存拷贝的开销

    set_intr_status(old_status);

    return child->pid;
}

void sys_putchar(char ch)
{
    console_put_char(ch);
}

void sys_clear(void)
{
    for (uint32_t i = 0; i < 25 * 80 * 2; i += 2)
    {
        *(uint8_t *)(0xc00b8000 + i) = 0;
    }
    set_cursor(0);
}

void sys_ps(void)
{
    printk("PID     PPID    STAT        TICKS       NAME\n");
    list_traversal(&thread_all_list, node2thread_info, 0);
}

int32_t sys_execv(const char *pathname, char *argv[])
{
    uint32_t argc = 0;
    char **_argv = NULL;
    if (argv)
    {
        uint32_t arg_blk_size = 0;
        while (argv[argc])
        {
            arg_blk_size += (strlen(argv[argc]) + 1);
            ++argc;
        }

        char *arg_blk = (char *)(KERNEL_SPACE_START - arg_blk_size);
        _argv = (char **)(arg_blk - sizeof(char *) * (argc + 1));

        for (uint32_t i = 0; i < argc; ++i)
        {
            _argv[i] = strcpy(arg_blk, argv[i]);
            arg_blk += (strlen(argv[i]) + 1);
        }
        _argv[argc] = NULL;
    }

    ASSERT(strlen(pathname) < MAX_THREAD_NAME_LEN);
    strcpy(current->name, pathname);
    
    void *entry_point = load_prog(pathname);
    if (!entry_point)
    {
        return -1;
    }

    mblock_desc_init(current->u_mblock_descs);      // 刷新内存块描述符

    intr_stack *pis = (intr_stack *)((uint32_t)current + PAGE_SIZE - sizeof(intr_stack));
    pis->esp = _argv ? (uint32_t)_argv : KERNEL_SPACE_START;
    pis->eip = (uint32_t)entry_point;
    pis->ebx = (uint32_t)_argv;
    pis->ecx = argc;

    asm volatile ("movl %0, %%esp; jmp intr_exit":: "a"(pis));

    return 0;
}

void sys_exit(const int32_t status)
{
    ASSERT(current->parent);

    current->exit_status = status;

    adopt_children(process_init, current);
    release_process_resource();

    set_intr_status(INTR_OFF);

    if (current->parent->status == TASK_WAITING)
    {
        thread_unblock(current->parent);
    }

    thread_block(TASK_HANGING);
}

int32_t sys_wait(int32_t *status)
{
    if (!current->child)
    {
        return -1;
    }

    while (1)
    {
        task_struct *p = current->child;
        while (p)
        {
            if (p->status == TASK_HANGING)
            {
                *status = p->exit_status;
                pid_t pid = p->pid;

                p->status = TASK_DIED;
                process_exit(p);

                return pid;
            }
            p = p->o_sibling;
        }
        thread_block(TASK_WAITING);
    }
}

int32_t sys_pipe(uint32_t pipe_fd[2])
{
    int32_t g_idx = get_free_slot_in_file_table();
    int32_t l_idx0 = get_free_slot_in_fd_table();
    if ((g_idx == -1) || (l_idx0 == -1))
    {
        return -1;
    }

    /* 
    鉴于get_free_slot_in_fd_table的实现方式，必须采用以下写法避免两次获取到相同的返回值 
    实际上，这种方式是不合理的，在多进程并发的情况下可能会出现问题，因此这个补丁是暂时的
    在并发情况下，文件系统仍需完善
    */
    current->fd_table[l_idx0] = 0;
    int32_t l_idx1 = get_free_slot_in_fd_table();
    if (l_idx1 == -1)
    {
        current->fd_table[l_idx0] = -1;
        return -1;
    }

    ioqueue *pioqueue = kmalloc(sizeof(ioqueue));
    if (!pioqueue)
    {
        return -1;
    }

    void *buf = get_kernel_pages(1);
    if (!buf)
    {
        sys_free(pioqueue);
        return -1;
    }

    ioqueue_init(pioqueue, buf, PAGE_SIZE);

    /* 
    这里复用了inode结构的三个成员：
    p_inode 指向管道对应的环形缓冲队列
    flag 用于区分管道文件和普通文件
    f_pos 表示管道的打开数 
    */
    file_table[g_idx].p_inode = (inode *)pioqueue;
    file_table[g_idx].flag = PIPE_FLAG;
    file_table[g_idx].f_pos = 2;

    current->fd_table[l_idx0] = current->fd_table[l_idx1] = g_idx;
    pipe_fd[0] = l_idx0;
    pipe_fd[1] = l_idx1;

    return 0;
}

int32_t sys_fd_redirect(uint32_t old_fd, uint32_t new_fd)
{
    if ((old_fd >= MAX_FILES_OPEN_PER_PROC) || (new_fd >= MAX_FILES_OPEN_PER_PROC))
    {
        return -1;
    }
    current->fd_table[old_fd] = (new_fd < 3) ? new_fd : current->fd_table[new_fd];
    return 0;
}