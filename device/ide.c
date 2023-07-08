#include "ide.h"
#include "io.h"
#include "debug.h"
#include "global.h"
#include "timer.h"
#include "stdio.h"
#include "_syscall.h"
#include "interrupt.h"
#include "string.h"
#include "memory.h"

// 分区表项
typedef struct partition_table_entry
{
    uint8_t bootable;                   // 是否可引导
    uint8_t start_head;                 // 起始磁头号
    uint8_t start_sector;               // 起始扇区号
    uint8_t start_cylinder;             // 起始柱面号
    uint8_t type_id;                    // 分区类型号
    uint8_t end_head;                 // 结束磁头号
    uint8_t end_sector;               // 结束扇区号
    uint8_t end_cylinder;             // 结束柱面号 
    uint32_t offset_lba;              // 分区的起始偏移扇区
    uint32_t sec_cnt;                   // 分区占据的扇区数
} PACKED partition_table_entry;         // 保证结构体大小为16字节

// 主引导扇区
typedef struct mbr_sec
{
    uint8_t mbr_code[446];              // 主引导代码，主要用于占位
    partition_table_entry dpt[4];       // 硬盘分区表
    uint16_t signature;                 // 结束标记字
} PACKED mbr_sec;                       // 保证结构体大小为512字节

#define port_data(pchannel) ((pchannel)->port_base + 0)
#define port_error(pchannel) ((pchannel)->port_base + 1)
#define port_sec_cnt(pchannel) ((pchannel)->port_base + 2)
#define port_lba_l(pchannel) ((pchannel)->port_base + 3)
#define port_lba_m(pchannel) ((pchannel)->port_base + 4)
#define port_lba_h(pchannel) ((pchannel)->port_base + 5)
#define port_dev(pchannel) ((pchannel)->port_base + 6)
#define port_cmd(pchannel) ((pchannel)->port_base + 7)
#define port_status(pchannel) port_cmd(pchannel)

// dev端口中的一些关键位
#define BIT_DEV_MBS  0xa0                 //  设备端口的第7位和第5位固定为1
#define BIT_DEV_LBA  0x40                  // 选择LBA方式进行硬盘读写
#define BIT_DEV_SLAVE 0x10                 // 选择从盘进行读写
#define BIT_DEV_MASTER 0                   // 选择主盘进行读写

// status寄存器中的一些关机位
#define BIT_STAT_BSY 0x80                // 硬盘忙
#define BIT_STAT_DRDY 0x40               // 驱动器准备就绪
#define BIT_STAT_DRQ 0x08                // 硬盘数据就绪，随时可进行数据传输
#define BIT_STAT_ERR 0x01               // 硬盘出错，出错原因在error端口中

// 硬盘的一些控制命令
#define CMD_IDENTIFY 0xec               // 获取硬盘相关信息
#define CMD_READ 0x20                   // 读硬盘
#define CMD_WRITE 0x30                  // 写硬盘

void intr_disk_handler(const uint32_t intr_no);                                 // 硬盘中断处理函数
void scan_partition(disk *hd, const uint32_t start_lba);                           //  扫描分区表获取分区信息          
void select_disk(disk *hd);         // 在dev端口中选择指定硬盘
void select_sectors(disk *hd, const uint32_t start_lba, const uint8_t sec_cnt);     // 选择读取的起始扇区和扇区数
void send_cmd(disk *hd, const uint8_t cmd);             // 向指定硬盘发送命令
bool disk_is_ready(disk *hd);                           // 测试硬盘是否已将数据准备完毕 
void read_from_sectors(disk *hd, void *dst, const uint32_t sec_cnt);            // 从指定硬盘读取sec_cnt个扇区数据到内存dst处
void write_to_sectors(disk *hd, void *src, const uint32_t sec_cnt);             // 将内存src处的sec_cnt个扇区数据写入到硬盘中
void identify_disk(disk *hd);                                                   // 获取硬盘参数
bool partition_info(node *pnode, int arg UNUSED);                               // 输出分区信息

uint8_t channel_cnt;               // ide通道数量
ide_channel channels[2];            // 个人计算机一般只支持两个ide通道

list partition_list;                // 分区链表

uint8_t p_no;            // 用于标记主分区的下标
uint8_t l_no;            // 用于标记扩展分区的下标
uint32_t ext_part_lba;   // 总扩展分区的起始LBA 

extern bool init_finish;

// 硬盘相关初始化
void ide_init(void)
{
    // 初始化分区链表
    list_init(&partition_list);

    // 物理地址0x475处存储着用户硬盘的数量，可依据改数量推算ide通道的数量
    uint8_t disk_cnt = *(uint8_t *)0x475;   
    channel_cnt = (disk_cnt + 1) / 2;        

    for (uint8_t i = 0; i < channel_cnt; i++)
    {
        sprintf(channels[i].name, "ide%d", i);
        mutex_lock_init(&channels[i].mutex);
        sem_init(&channels[i].disk_done, 0);
        channels[i].intr_is_expected = false;

        switch (i)
        {
            case 0:
            {
                channels[i].port_base = 0x1f0;
                channels[i].intr_no = 0x2e;
                break;
            }
            case 1:
            {
                channels[i].port_base = 0x170;
                channels[i].intr_no = 0x2f;
                break;
            }
            default:
            {
                break;
            }
        }
        // 注册硬盘中断的处理函数
        intr_handler_table[channels[i].intr_no] = (intr_entry)intr_disk_handler;    

        // 初始化硬盘
        uint8_t channel_disk_cnt = (disk_cnt > 2 ? 2 : disk_cnt);
        for (uint8_t j = 0; j < channel_disk_cnt; j++)
        {
            sprintf(channels[i].disks[j].name, "sd%c", 'a' + i * 2 + j);
            channels[i].disks[j].my_channel = &channels[i];
            channels[i].disks[j].dev_no = j;

            l_no = p_no = 0;
            scan_partition(&channels[i].disks[j], 0);

            // 输出硬盘参数
            identify_disk(&channels[i].disks[j]);
        }
        disk_cnt -= channel_disk_cnt;
    }

    printk("All partitions' infomation: \n");
    list_traversal(&partition_list, partition_info, 0);

    printk("Init IDE successfully!\n");
}  

// 硬盘中断处理函数
void intr_disk_handler(const uint32_t intr_no)
{
    uint32_t channel_no = intr_no - 0x2e;
    if (channels[channel_no].intr_is_expected)
    {
        sem_up(&channels[channel_no].disk_done);            // 唤醒等待中断的线程
        channels[channel_no].intr_is_expected = false;
    }
    inb(port_status(&channels[channel_no]));            // 读一下状态端口，使硬盘可以产生下一次中断
}                                

//  扫描分区表获取分区信息
void scan_partition(disk *hd, const uint32_t start_lba)
{
    mbr_sec *p_mbr_sec = (mbr_sec *)kmalloc(sizeof(mbr_sec));
    ASSERT(p_mbr_sec != NULL);

    read_disk(hd, p_mbr_sec, start_lba, 1);
    ASSERT (p_mbr_sec->signature == 0xaa55);

    for (int i = 0; i < 4; i++)
    {
        if (p_mbr_sec->dpt[i].type_id == 0x05)
        {
            // 若为扩展分区
            // 注意：下一个EBR相对地址 = 下一个EBR绝对地址 - 总扩展分区第一个EBR绝对地址(即总扩展分区第一个扇区)
            // 而不是： 下一个EBR绝对地址 - 当前EBR绝对地址
            // 否则会出现解析错误
            if (start_lba == 0)
            {
                // 初始化总扩展分区地址
                ext_part_lba = p_mbr_sec->dpt[i].offset_lba;
                scan_partition(hd, ext_part_lba);
            }
            else
            {
                scan_partition(hd, ext_part_lba + p_mbr_sec->dpt[i].offset_lba);
            }
        }
        else if (p_mbr_sec->dpt[i].type_id != 0)        // 若为有效分区类型
        {
            if (start_lba == 0)
            {
                // 若基准lba为0，说明该分区为主分区
                sprintf(hd->primary[p_no].name, "%s%d", hd->name, p_no + 1);
                hd->primary[p_no].my_disk = hd;
                hd->primary[p_no].start_lba = p_mbr_sec->dpt[i].offset_lba;
                hd->primary[p_no].sec_cnt = p_mbr_sec->dpt[i].sec_cnt;
                hd->primary[p_no].type_id = p_mbr_sec->dpt[i].type_id;
                list_push_back(&partition_list, &hd->primary[p_no].list_node);

                p_no++;
                ASSERT(p_no < 4);
            }
            else
            {
                // 否则该分区为逻辑分区
                sprintf(hd->logic[l_no].name, "%s%d", hd->name, l_no + 5);
                hd->logic[l_no].my_disk = hd;
                hd->logic[l_no].start_lba = start_lba + p_mbr_sec->dpt[i].offset_lba;
                hd->logic[l_no].sec_cnt = p_mbr_sec->dpt[i].sec_cnt;
                hd->logic[l_no].type_id = p_mbr_sec->dpt[i].type_id;
                hd->logic[l_no].sb = NULL;      // 用于区分一个分区是否已挂载
                list_push_back(&partition_list, &hd->logic[l_no].list_node);

                l_no++;
                if (l_no >= 16)
                {
                    return;
                }
            }
        }
    }

    sys_free(p_mbr_sec);
}                           


// 在dev端口中选择指定硬盘
void select_disk(disk *hd)
{
    outb(BIT_DEV_MBS | BIT_DEV_LBA | (hd->dev_no ? BIT_DEV_SLAVE : BIT_DEV_MASTER), port_dev(hd->my_channel));
}   

// 选择读取的起始扇区和扇区数
void select_sectors(disk *hd, const uint32_t start_lba, const uint8_t sec_cnt)
{
    outb(sec_cnt, port_sec_cnt(hd->my_channel));
    outb((uint8_t)start_lba, port_lba_l(hd->my_channel));
    outb((uint8_t)(start_lba >> 8), port_lba_m(hd->my_channel));
    outb((uint8_t)(start_lba >> 16), port_lba_h(hd->my_channel));
    outb((uint8_t)(((start_lba >> 24) & 0x0f) | BIT_DEV_MBS | BIT_DEV_LBA | (hd->dev_no ? BIT_DEV_SLAVE : BIT_DEV_MASTER)), 
    port_dev(hd->my_channel));
}   

// 向指定硬盘发送命令
void send_cmd(disk *hd, const uint8_t cmd)
{
    hd->my_channel->intr_is_expected = true;
    outb(cmd, port_cmd(hd->my_channel));
} 

// 测试硬盘是否已将数据准备完毕 
bool disk_is_ready(disk *hd)
{
    if (init_finish)
    {
        uint32_t time_limit = 31 * 1000;            // 最多等待31s
        while (time_limit)
        {
            if (~(inb(port_status(hd->my_channel))) & BIT_STAT_BSY)
            {
                // 如果硬盘不忙但是数据没有准备好，说明出错了
                return inb(port_status(hd->my_channel)) & BIT_STAT_DRQ;
            }
            // 每间隔10ms检测一次
            sleep_ms(10);
            time_limit -= 10;
        }
        // 若31s后硬盘仍未准备就绪，则认为硬盘出错
        return false;
    }
    else
    {
        // 轮询方式
        while (!(~(inb(port_status(hd->my_channel))) & BIT_STAT_BSY));
        return inb(port_status(hd->my_channel)) & BIT_STAT_DRQ;
    }
}         

 // 从指定硬盘读取sec_cnt个扇区数据到内存dst处
void read_from_sectors(disk *hd, void *dst, const uint32_t sec_cnt)
{
    insw(port_data(hd->my_channel), dst, sec_cnt * 256);
}   

// 将内存src处的sec_cnt个扇区数据写入到硬盘中
void write_to_sectors(disk *hd, void *src, const uint32_t sec_cnt)
{
    outsw(src, port_data(hd->my_channel), sec_cnt * 256);
}            


// 从指定硬盘中读取start_lba起始的sec_cnt个扇区到dst
void read_disk(disk *hd, void *dst, uint32_t start_lba, uint32_t sec_cnt)
{
    ASSERT(hd != NULL);
    mutex_lock_acquire(&hd->my_channel->mutex);
    while (sec_cnt)
    {
        uint8_t sec_op = (uint8_t)(sec_cnt >= 256 ? 256 : sec_cnt); 

        select_sectors(hd, start_lba, sec_op);
        send_cmd(hd, CMD_READ);
        if (init_finish)
        {
            sem_down(&hd->my_channel->disk_done);        // 发送命令后阻塞自己，等待硬盘中断
        }

        if (!disk_is_ready(hd))
        {
            char error[80];
            sprintf(error, "Read disk error:  disk: %s  sector: %d\n", hd->name, start_lba);
            panic_spin(__FILE__, __LINE__, __func__, error);
        }

        read_from_sectors(hd, dst, sec_op);

        sec_cnt -= (uint32_t)sec_op;
        start_lba += sec_op;
        dst += (sec_op * 512);
    }

    mutex_lock_release(&hd->my_channel->mutex);
}   

// 将内存src处的sec_cnt个扇区数据写入到硬盘start_lba处
void write_disk(disk *hd, void *src, uint32_t start_lba, uint32_t sec_cnt)
{
    ASSERT(hd != NULL);
    mutex_lock_acquire(&hd->my_channel->mutex);
    while (sec_cnt)
    {
        uint8_t sec_op = (uint8_t)(sec_cnt >= 256 ? 256 : sec_cnt); 

        select_sectors(hd, start_lba, sec_op);
        send_cmd(hd, CMD_WRITE);

        if (!disk_is_ready(hd))
        {
            char error[80];
            sprintf(error, "Write disk error:  disk: %s  sector: %d\n", hd->name, start_lba);
            panic_spin(__FILE__, __LINE__, __func__, error);
        }

        write_to_sectors(hd, src, sec_op);

        if (init_finish)
        {
            sem_down(&hd->my_channel->disk_done);   // 写完数据后等待硬盘将数据完全写入扇区，然后发出硬盘中断唤醒当前线程以进行下一次写入
        }

        sec_cnt -= (uint32_t)sec_op;
        start_lba += sec_op;
        src += (sec_op * 512);
    }

    mutex_lock_release(&hd->my_channel->mutex);
}    

 // 获取硬盘参数
void identify_disk(disk *hd)
{
    ASSERT(hd != NULL);
    select_disk(hd);
    send_cmd(hd, CMD_IDENTIFY);
    if (init_finish)
    {
        sem_down(&hd->my_channel->disk_done);
    }
    if (!disk_is_ready(hd))
    {
        char error[80];
        sprintf(error, "Identity disk error:  disk: %s\n", hd->name);
        panic_spin(__FILE__, __LINE__, __func__, error);
    }

    void *buf = kmalloc(512);
    ASSERT(buf != NULL);
    read_from_sectors(hd, buf, 1);

    // 输出硬盘序列号
    uint32_t sn_start = 10 * 2;
    uint32_t sn_len = 20;
    for (uint32_t i = 0; i < sn_len; i += 2)
    {
        uint8_t tmp = *(uint8_t *)(buf + sn_start + i);
        *(uint8_t *)(buf + sn_start + i) = *(uint8_t *)(buf + sn_start + i + 1);
        *(uint8_t *)(buf + sn_start + i + 1) = tmp;
    }
    printk("Disk %s:\n", hd->name);
    printk("Serial Number: %s\n", buf + sn_start);

    // 输出硬盘型号
    uint32_t md_start = 27 * 2;
    uint32_t md_len = 40;
    for (uint32_t i = 0; i < md_len; i += 2)
    {
        uint8_t tmp = *(uint8_t *)(buf + md_start + i);
        *(uint8_t *)(buf + md_start + i) = *(uint8_t *)(buf + md_start + i + 1);
        *(uint8_t *)(buf + md_start + i + 1) = tmp;
    }
    printk("Model: %s\n", buf + md_start);

    // 输出硬盘扇区数和容量
    uint32_t sec_cnt = *(uint32_t *)(buf + 60 * 2);
    printk("Sectors: %u\n", sec_cnt);
    printk("Capacity: %uMB\n", (sec_cnt * 512) / 1024 / 1024);

    sys_free(buf);
}    

// 输出分区信息
bool partition_info(node *pnode, int arg UNUSED)
{
    partition *part = member2struct(pnode, partition, list_node);
    char size_unit;
    if (part->sec_cnt >= (1024 * 1024 * 1024 / 512))
    {
        size_unit = 'G';
    }
    else if (part->sec_cnt >= (1024 * 1024 / 512))
    {
        size_unit = 'M';
    }
    else if (part->sec_cnt >= (1024 / 512))
    {
        size_unit = 'K';
    }
    else
    {
        size_unit = 'B';
    }

    uint32_t size;
    switch (size_unit)
    {
        case 'G':
            size = (part->sec_cnt * 512) / 1024 / 1024 / 1024;
            break;
        case 'M':
            size = (part->sec_cnt * 512) / 1024 / 1024;
            break;
        case 'K':
            size = (part->sec_cnt * 512) / 1024;
            break;
        case 'B':
            size = (part->sec_cnt * 512);
            break;
    }

    printk("%s  start-sector:%u  end-sector:%u  sectors:%u  size:%u%c  id:%x\n", part->name, part->start_lba, 
    part->start_lba + part->sec_cnt - 1, part->sec_cnt, size, size_unit, part->type_id);

    return false;
}     

// 作为list_traversal的回调函数以分区名查找指定分区 
bool part_name_check(node *pnode, int arg)
{
    partition *part = member2struct(pnode, partition, list_node);
    return !strcmp(part->name, (const char *)arg);
}


