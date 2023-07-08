LOADER_SECT_NR equ 4    ;loader占据的扇区数
LOADER_START_SECT equ 1     ; loader所在起始扇区
LOADER_BASE_ADDR equ 0x900   ; 读取loader指定目标内存地址
LOADER_START_OFFSET equ 108     ; 程序入口偏移，跳过total_mem_size 和 TSS所占用的空间
LOADER_START equ LOADER_BASE_ADDR + LOADER_START_OFFSET ;loader入口地址, 跳过total_mem_size所占用的空间
PAGE_DIR_TAB_ADDR equ 0x100000  ; 页目录表起始地址

;页表相关属性
PAGE_US_U equ 1_00b 
PAGE_RW_W equ 1_0b
PAGE_P_T  equ 1b
PAGE_US_S equ 0_00b
PAGE_RW_R equ 0_0b
PAGE_P_F  equ 0b

PAGE_SIZE equ 0x1000    ;4KB的页

KERNEL_PDE_INDEX equ 768    
KERNEL_SPACE_ADDR equ 0xc0000000 ;开启分页机制后内核空间的起始地址

GCODE_SEL equ 0x0008        ; 内核全局代码段选择子
GDATA_SEL equ 0x0010        ; 内核全局数据段选择子
VIDEO_SEL equ 0x0018        ; 显存段选择子
TSS_SEL    equ 0x0020       ; TSS选择子

KERNEL_BIN_ADDR equ 0x70000             ;kernel.bin加载到的目标起始地址
KERNEL_SECT_NR equ 250                  ;kernel所占扇区数不会超过250个
KERNEL_START_SECT equ (LOADER_SECT_NR + 1)  ;kernel所在起始扇区
KERNEL_ENTRY_POINT equ 0xc0001500           ;kernel入口地址

PT_LOAD equ 1   ;可加载段标识