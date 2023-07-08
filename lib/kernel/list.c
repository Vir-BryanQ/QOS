#include "list.h"
#include "debug.h"
#include "interrupt.h"
#include "global.h"

void list_init(list *plist)
{
    intr_status old_status = set_intr_status(INTR_OFF);

    ASSERT(plist != NULL);
    plist->head.prev = plist->tail.next = NULL;
    plist->head.next = &plist->tail;
    plist->tail.prev = &plist->head;
    plist->length = 0;

    set_intr_status(old_status);
}

// 实际上这里plist参数是冗余的，但是可以提高访问length成员变量的效率
void list_insert_before(list *plist, node *before, node *elem)
{
    intr_status old_status = set_intr_status(INTR_OFF);

    ASSERT(plist != NULL && before != NULL && elem != NULL);
    ASSERT(before != &plist->head);             // 不允许在头结点前面插入
    ASSERT(!list_find(plist, elem));
    elem->prev = before->prev;
    elem->next = before;
    before->prev = elem;
    elem->prev->next = elem;
    plist->length++;

    set_intr_status(old_status);
}

// 实际上这里plist参数是冗余的，但是可以提高访问length成员变量的效率
void list_remove(list *plist, node *elem)
{
    intr_status old_status = set_intr_status(INTR_OFF);

    ASSERT(plist != NULL && elem != NULL && plist->length != 0);
    ASSERT(list_find(plist, elem));         // 待删除元素必须在链表中存在
    ASSERT(elem != &plist->head && elem != &plist->tail);           // 不允许删除头结点和尾节点
    elem->prev->next = elem->next;
    elem->next->prev = elem->prev;
    plist->length--;

    set_intr_status(old_status);
}

void list_push_front(list *plist, node *elem)
{
    intr_status old_status = set_intr_status(INTR_OFF);

    ASSERT(plist != NULL && elem != NULL);
    ASSERT(!list_find(plist, elem));
    list_insert_before(plist, plist->head.next, elem);

    set_intr_status(old_status);
}

node *list_pop_front(list *plist)
{
    intr_status old_status = set_intr_status(INTR_OFF);

    ASSERT(plist != NULL && plist->length != 0);
    node *tmp = plist->head.next;
    list_remove(plist, tmp);

    set_intr_status(old_status);
    return tmp;
}

void list_push_back(list *plist, node *elem)
{
    intr_status old_status = set_intr_status(INTR_OFF);

    ASSERT(plist != NULL && elem != NULL);
    ASSERT(!list_find(plist, elem));
    list_insert_before(plist, &plist->tail, elem);

    set_intr_status(old_status);
}

node *list_pop_back(list *plist)
{
    intr_status old_status = set_intr_status(INTR_OFF);

    ASSERT(plist != NULL && plist->length != 0);
    node *tmp = plist->tail.prev;
    list_remove(plist, tmp);

    set_intr_status(old_status);
    return tmp;
}

node *list_traversal(list *plist, func_ptr function, int arg)
{
    intr_status old_status = set_intr_status(INTR_OFF);

    ASSERT(plist != NULL && function != NULL);
    node *pnode = &plist->head;
    while (pnode != NULL)
    {
        if (pnode != &plist->head && pnode != &plist->tail && function(pnode, arg))
        {
            set_intr_status(old_status);
            return pnode;
        }
        pnode = pnode->next;
    }

    set_intr_status(old_status);
    return NULL;
}

bool list_find(list *plist, node *elem)
{
    intr_status old_status = set_intr_status(INTR_OFF);

    ASSERT((plist != NULL && elem != NULL));
    node *pnode = &plist->head;
    while (pnode != NULL)
    {
        if (pnode == elem)
        {
            set_intr_status(old_status);
            return true;
        }
        pnode = pnode->next;
    }

    set_intr_status(old_status);
    return false;
}