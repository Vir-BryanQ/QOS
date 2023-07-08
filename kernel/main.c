#include "init.h"
#include "interrupt.h"
#include "thread.h"

int main(void)
{
    init_all();

    set_intr_status(INTR_ON);

    while (1)
    {
        node *pnode = list_traversal(&thread_all_list, find_died_thread, 0);
        if (pnode)
        {
            task_struct *pthread = member2struct(pnode, task_struct, all_list_node);
            list_remove(&thread_all_list, &pthread->all_list_node);
            release_pid(pthread->pid);
            mfree_pages(1, pthread);
        }
        else
        {
            thread_block(TASK_WAITING);
        }
    }

    return 0;
}




