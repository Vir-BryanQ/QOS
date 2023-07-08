#ifndef __LIB_KERNEL_LIST_H
#define __LIB_KERNEL_LIST_H

#include "stdint.h"
#include "stdbool.h"

// 获取结构体成员在结构体内部的偏移量
#define member_offset(struct_type, member_name) \
(uint32_t)(&(((struct_type *)0)->member_name))    

// 将成员变量的地址转换为对应结构体变量的地址
#define member2struct(member_addr, struct_type, member_name) \
(struct_type *)((uint32_t)(member_addr) - member_offset(struct_type, member_name))

typedef struct node
{
    struct node *prev;
    struct node *next;
} node;

// 双向链表
typedef struct list
{
    node head;
    node tail;
    uint32_t length;
} list;

// 回调函数指针
typedef bool (*func_ptr)(node *elem, int arg);

extern void list_init(list *plist);
extern void list_insert_before(list *plist, node *before, node *elem);
extern void list_remove(list *plist, node *elem);
extern void list_push_front(list *plist, node *elem);
extern node *list_pop_front(list *plist);
extern void list_push_back(list *plist, node *elem);
extern node *list_pop_back(list *plist);
extern node *list_traversal(list *plist, func_ptr function, int arg);
extern bool list_find(list *plist, node *elem);


#endif