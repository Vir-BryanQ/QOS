#include "exec.h"
#include "stdint.h"
#include "fs.h"
#include "global.h"
#include "_syscall.h"
#include "string.h"
#include "debug.h"
#include "file.h"
#include "memory.h"

extern void intr_exit(void);

typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

int32_t load_seg(uint32_t fd, Elf32_Off offset, Elf32_Word filesz, Elf32_Addr vaddr);    // 将可执行文件中指定偏移处的段加载到指定虚拟地址处，成功返回0，失败返回-1

// 32位elf文件头
typedef struct 
{
    unsigned char e_ident[16];
    Elf32_Half e_type;
    Elf32_Half e_machine;
    Elf32_Word e_version;
    Elf32_Addr e_entry;
    Elf32_Off e_phoff;
    Elf32_Off e_shoff;
    Elf32_Word e_flags;
    Elf32_Half e_ehsize;
    Elf32_Half e_phentsize;
    Elf32_Half e_phnum;
    Elf32_Half e_shentsize;
    Elf32_Half e_shnum;
    Elf32_Half e_shstrndx;
} Elf32_Ehdr;

//  程序头表项, 即段描述头
typedef struct 
{
    Elf32_Word p_type;
    Elf32_Off p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
} Elf32_Phdr;

// 段类型
#define PT_NULL 0
#define PT_LOAD 1
#define PT_DYNAMINC 2
#define PT_INTERP 3
#define PT_NOTE 4
#define PT_SHLIB 5
#define PT_PHDR 6

// 将可执行文件中指定偏移处的段加载到指定虚拟地址处，成功返回0，失败返回-1
int32_t load_seg(uint32_t fd, Elf32_Off offset, Elf32_Word filesz, Elf32_Addr vaddr)
{
    Elf32_Addr start_page = vaddr & 0xfffff000;
    Elf32_Addr end_page = (vaddr + filesz - 1) & 0xfffff000;

    uint32_t *pde_ptr;
    uint32_t *pte_ptr;
    for (Elf32_Addr cur_page = start_page; cur_page <= end_page; cur_page += PAGE_SIZE)
    {
        pde_ptr = (uint32_t *)PDE_PTR((uint32_t)cur_page);
        pte_ptr = (uint32_t *)PTE_PTR((uint32_t)cur_page);
        if (!((*pde_ptr & PG_P_1) && (*pte_ptr & PG_P_1)))
        {
            if (!get_a_page((void *)cur_page))
            {
                return -1;
            }
        } 
        else if (is_shared_page((uint32_t)vaddr2paddr((void *)cur_page)))
        {
            if (!get_a_page_without_setting_vbitmap((void *)cur_page))
            {
                return -1;
            }
        }
    }

    return ((sys_lseek(fd, offset, SEEK_SET) != -1) && (sys_read(fd, (void *)vaddr, filesz) == filesz)) ? 0 : -1;
}   


// 加载可执行文件体，成功则返回程序的入口地址，失败则返回NULL
void *load_prog(const char *pathname)
{
    int32_t fd = sys_open(pathname, O_RDONLY);
    if (fd == -1)
    {
        return NULL;
    }

    void *ret = NULL;
    Elf32_Ehdr elf_header;

    // 读取elf头
    if (sys_read(fd, &elf_header, sizeof(Elf32_Ehdr)) != sizeof(Elf32_Ehdr))
    {
        goto done_0;
    }

    // 校验elf头
    if (memcmp(elf_header.e_ident, "\177ELF\1\1\1", 7) 
        || elf_header.e_type != 2
        || elf_header.e_machine != 3
        || elf_header.e_version != 1
        || elf_header.e_phnum > 1024
        || elf_header.e_phentsize != sizeof(Elf32_Phdr))
    {
        goto done_0;
    }

    uint32_t phdr_tab_size = elf_header.e_phentsize * elf_header.e_phnum;
    Elf32_Phdr *prog_header_tab =  kmalloc(phdr_tab_size);
    ASSERT(prog_header_tab);

    // 读取程序头表
    if ((sys_lseek(fd, elf_header.e_phoff, SEEK_SET) == -1)
        || (sys_read(fd, prog_header_tab, phdr_tab_size) != phdr_tab_size))
    {
        goto done_1;
    }
    
    for (Elf32_Half i = 0; i < elf_header.e_phnum; ++i)
    {
        if (prog_header_tab[i].p_type == PT_LOAD)
        {
            if (load_seg(fd, prog_header_tab[i].p_offset, prog_header_tab[i].p_filesz, prog_header_tab[i].p_vaddr) == -1)
            {
                goto done_1;
            }
        }
    }

    ret = (void *)elf_header.e_entry;

done_1:
    sys_free(prog_header_tab);
done_0:
    sys_close(fd);
    return ret;
}      