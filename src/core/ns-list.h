/* Weststar SDL Browser — nscore: doubly-linked lists (GList) and queues
 * (GQueue), with GLib's fields.
 *
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_LIST_H
#define NS_LIST_H

#include "ns-base.h"

NS_BEGIN_DECLS

typedef struct NsList NsList;
struct NsList {
    void *data;
    NsList *next;
    NsList *prev;
};

typedef struct NsQueue {
    NsList *head;
    NsList *tail;
    unsigned int length;
} NsQueue;

#define NS_QUEUE_INIT { NULL, NULL, 0 }

NsList *ns_list_alloc(void);
void ns_list_free(NsList *list);
void ns_list_free_1(NsList *list);
void ns_list_free_full(NsList *list, NsDestroyNotify free_func);
NsList *ns_list_append(NsList *list, void *data);
NsList *ns_list_prepend(NsList *list, void *data);
NsList *ns_list_insert_before(NsList *list, NsList *sibling, void *data);
NsList *ns_list_remove(NsList *list, const void *data);
NsList *ns_list_remove_link(NsList *list, NsList *llink);
NsList *ns_list_delete_link(NsList *list, NsList *link_);
NsList *ns_list_reverse(NsList *list);
NsList *ns_list_copy(NsList *list);
NsList *ns_list_concat(NsList *list1, NsList *list2);
NsList *ns_list_nth(NsList *list, unsigned int n);
void *ns_list_nth_data(NsList *list, unsigned int n);
NsList *ns_list_find(NsList *list, const void *data);
NsList *ns_list_last(NsList *list);
NsList *ns_list_first(NsList *list);
unsigned int ns_list_length(NsList *list);
NsList *ns_list_sort(NsList *list, NsCompareFunc compare_func);
NsList *ns_list_sort_with_data(NsList *list, NsCompareDataFunc compare_func,
                               void *user_data);
void ns_list_foreach(NsList *list, NsFunc func, void *user_data);

NsQueue *ns_queue_new(void);
void ns_queue_free(NsQueue *queue);
void ns_queue_free_full(NsQueue *queue, NsDestroyNotify free_func);
void ns_queue_init(NsQueue *queue);
void ns_queue_clear(NsQueue *queue);
void ns_queue_clear_full(NsQueue *queue, NsDestroyNotify free_func);
int ns_queue_is_empty(NsQueue *queue);
unsigned int ns_queue_get_length(NsQueue *queue);
void ns_queue_push_head(NsQueue *queue, void *data);
void ns_queue_push_tail(NsQueue *queue, void *data);
void *ns_queue_pop_head(NsQueue *queue);
void *ns_queue_pop_tail(NsQueue *queue);
void *ns_queue_peek_head(NsQueue *queue);
void *ns_queue_peek_tail(NsQueue *queue);
void ns_queue_delete_link(NsQueue *queue, NsList *link_);
void ns_queue_unlink(NsQueue *queue, NsList *link_);

NS_END_DECLS

#endif
