#include "interrupt.h"
#include "timer.h"
#include "memory.h"
#include "thread.h"
#include "console.h"
#include "keyboard.h"
#include "init.h"
#include "ide.h"
#include "fs.h"
#include "_syscall.h"
#include "syscall.h"
#include "stdio.h"
#include "debug.h"

#define NEED_WRITE false
#define START_SEC 600
#define FILE_SZ 13732
#define STORAGE_PATH "/bin/excep"

bool init_finish = false;

void other_init(void);

void init_all(void)
{
    intr_init();        // 中断初始化
    timer_init();       // 8253定时计数器初始化
    console_init();     // 控制台初始化
    keyboard_init();    // 键盘初始化
    mem_init();         // 内存初始化
    ide_init();         // 硬盘初始化
    fs_init();          // 文件系统初始化
    thread_init();      // 线程初始化
    other_init();       // 其他初始化
    
    init_finish = true;
}

// 一些初始化操作必须在某些特定初始化操作完成后才能进行，为了避免循环依赖，这类初始化操作统一由other_init进行
void other_init(void)
{
    sys_mkdir("/home");
    sys_mkdir("/bin");
    sys_mkdir("/sbin");

    // 挂载其他分区
    printk(sys_mount("sdc1", "/home") == 0 ? 
    "Successful to mount 'sdc1' on '/home'\n" : "Fail to mount 'sdc1' on '/home'\n");

    if (NEED_WRITE)
    {
        uint32_t sec_cnt = DIV_ROUND_UP(FILE_SZ, SECTOR_SIZE);
        void *buf = kmalloc(sec_cnt * SECTOR_SIZE);
        ASSERT(buf);
        read_disk(&channels[0].disks[0], buf, START_SEC, sec_cnt);

        int32_t ret_val;
        struct stat _stat;
        if (!sys_stat(STORAGE_PATH, &_stat))
        {
            ret_val = sys_unlink(STORAGE_PATH);
            ASSERT (!ret_val);
        }

        int32_t fd = sys_open(STORAGE_PATH, O_CREAT | O_WRONLY);
        ASSERT (fd != -1);

        ret_val = sys_write(fd, buf, FILE_SZ);
        ASSERT(ret_val == FILE_SZ);

        sys_close(fd);

        sys_free(buf);

        printk("Successful to store a file to the local filesystem\n");
    }
}


