/* Weststar SDL Browser — nscore: lists and queues.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ns-list.h"
#include "ns-mem.h"

NsList *
ns_list_alloc(void)
{
    return ns_malloc0(sizeof(NsList));
}

void
ns_list_free_1(NsList *list)
{
    ns_free(list);
}

void
ns_list_free(NsList *list)
{
    while (list) {
        NsList *next = list->next;
        ns_free(list);
        list = next;
    }
}

void
ns_list_free_full(NsList *list, NsDestroyNotify free_func)
{
    for (NsList *l = list; l; l = l->next) free_func(l->data);
    ns_list_free(list);
}

NsList *
ns_list_last(NsList *list)
{
    if (list)
        while (list->next) list = list->next;
    return list;
}

NsList *
ns_list_first(NsList *list)
{
    if (list)
        while (list->prev) list = list->prev;
    return list;
}

NsList *
ns_list_append(NsList *list, void *data)
{
    NsList *node = ns_list_alloc();
    node->data = data;
    if (!list) return node;
    NsList *last = ns_list_last(list);
    last->next = node;
    node->prev = last;
    return list;
}

NsList *
ns_list_prepend(NsList *list, void *data)
{
    NsList *node = ns_list_alloc();
    node->data = data;
    node->next = list;
    if (list) {
        node->prev = list->prev;
        if (list->prev) list->prev->next = node;
        list->prev = node;
    }
    return node;
}

NsList *
ns_list_insert_before(NsList *list, NsList *sibling, void *data)
{
    if (!list) {
        list = ns_list_alloc();
        list->data = data;
        return list;
    }
    if (!sibling) return ns_list_append(list, data);
    NsList *node = ns_list_alloc();
    node->data = data;
    node->prev = sibling->prev;
    node->next = sibling;
    sibling->prev = node;
    if (node->prev) {
        node->prev->next = node;
        return list;
    }
    return node;
}

NsList *
ns_list_remove_link(NsList *list, NsList *llink)
{
    if (!llink) return list;
    if (llink->prev) llink->prev->next = llink->next;
    if (llink->next) llink->next->prev = llink->prev;
    if (llink == list) list = list->next;
    llink->next = NULL;
    llink->prev = NULL;
    return list;
}

NsList *
ns_list_delete_link(NsList *list, NsList *link_)
{
    list = ns_list_remove_link(list, link_);
    ns_free(link_);
    return list;
}

NsList *
ns_list_remove(NsList *list, const void *data)
{
    for (NsList *l = list; l; l = l->next)
        if (l->data == data) return ns_list_delete_link(list, l);
    return list;
}

NsList *
ns_list_reverse(NsList *list)
{
    NsList *last = NULL;
    while (list) {
        last = list;
        list = last->next;
        last->next = last->prev;
        last->prev = list;
    }
    return last;
}

NsList *
ns_list_copy(NsList *list)
{
    NsList *head = NULL, *tail = NULL;
    for (; list; list = list->next) {
        NsList *node = ns_list_alloc();
        node->data = list->data;
        node->prev = tail;
        if (tail) tail->next = node;
        else head = node;
        tail = node;
    }
    return head;
}

NsList *
ns_list_concat(NsList *list1, NsList *list2)
{
    if (!list2) return list1;
    if (!list1) return list2;
    NsList *last = ns_list_last(list1);
    last->next = list2;
    list2->prev = last;
    return list1;
}

NsList *
ns_list_nth(NsList *list, unsigned int n)
{
    while (n-- > 0 && list) list = list->next;
    return list;
}

void *
ns_list_nth_data(NsList *list, unsigned int n)
{
    list = ns_list_nth(list, n);
    return list ? list->data : NULL;
}

NsList *
ns_list_find(NsList *list, const void *data)
{
    for (; list; list = list->next)
        if (list->data == data) break;
    return list;
}

unsigned int
ns_list_length(NsList *list)
{
    unsigned int n = 0;
    for (; list; list = list->next) n++;
    return n;
}

void
ns_list_foreach(NsList *list, NsFunc func, void *user_data)
{
    while (list) {
        NsList *next = list->next;
        func(list->data, user_data);
        list = next;
    }
}

/* Stable merge sort of the links: halves split at the middle, merged
 * taking from the first while it compares less or equal. */
static NsList *
ns_list_merge(NsList *l1, NsList *l2, NsCompareDataFunc cmp, void *ud)
{
    NsList head = { 0 }, *tail = &head, *prev = NULL;
    while (l1 && l2) {
        if (cmp(l1->data, l2->data, ud) <= 0) {
            tail->next = l1;
            l1 = l1->next;
        } else {
            tail->next = l2;
            l2 = l2->next;
        }
        tail = tail->next;
        tail->prev = prev;
        prev = tail;
    }
    tail->next = l1 ? l1 : l2;
    if (tail->next) tail->next->prev = tail;
    if (head.next) head.next->prev = NULL;
    return head.next;
}

static NsList *
ns_list_sort_real(NsList *list, NsCompareDataFunc cmp, void *ud)
{
    if (!list || !list->next) return list;
    NsList *l1 = list, *l2 = list->next;
    while ((l2 = l2->next) != NULL) {
        if ((l2 = l2->next) == NULL) break;
        l1 = l1->next;
    }
    l2 = l1->next;
    l1->next = NULL;
    return ns_list_merge(ns_list_sort_real(list, cmp, ud),
                         ns_list_sort_real(l2, cmp, ud), cmp, ud);
}

typedef struct {
    NsCompareFunc func;
} NsListPlainCompare;

static int
ns_list_plain_compare(const void *a, const void *b, void *data)
{
    return ((NsListPlainCompare *)data)->func(a, b);
}

NsList *
ns_list_sort(NsList *list, NsCompareFunc compare_func)
{
    NsListPlainCompare pc = { compare_func };
    return ns_list_sort_real(list, ns_list_plain_compare, &pc);
}

NsList *
ns_list_sort_with_data(NsList *list, NsCompareDataFunc compare_func, void *user_data)
{
    return ns_list_sort_real(list, compare_func, user_data);
}

/* ---- queues ------------------------------------------------------------ */

NsQueue *
ns_queue_new(void)
{
    return ns_malloc0(sizeof(NsQueue));
}

void
ns_queue_free(NsQueue *queue)
{
    if (!queue) return;
    ns_list_free(queue->head);
    ns_free(queue);
}

void
ns_queue_free_full(NsQueue *queue, NsDestroyNotify free_func)
{
    if (!queue) return;
    ns_queue_clear_full(queue, free_func);
    ns_free(queue);
}

void
ns_queue_init(NsQueue *queue)
{
    queue->head = queue->tail = NULL;
    queue->length = 0;
}

void
ns_queue_clear(NsQueue *queue)
{
    ns_list_free(queue->head);
    ns_queue_init(queue);
}

void
ns_queue_clear_full(NsQueue *queue, NsDestroyNotify free_func)
{
    if (free_func)
        for (NsList *l = queue->head; l; l = l->next) free_func(l->data);
    ns_queue_clear(queue);
}

int
ns_queue_is_empty(NsQueue *queue)
{
    return queue->head == NULL;
}

unsigned int
ns_queue_get_length(NsQueue *queue)
{
    return queue->length;
}

void
ns_queue_push_head(NsQueue *queue, void *data)
{
    queue->head = ns_list_prepend(queue->head, data);
    if (!queue->tail) queue->tail = queue->head;
    queue->length++;
}

void
ns_queue_push_tail(NsQueue *queue, void *data)
{
    NsList *node = ns_list_alloc();
    node->data = data;
    node->prev = queue->tail;
    if (queue->tail) queue->tail->next = node;
    else queue->head = node;
    queue->tail = node;
    queue->length++;
}

void
ns_queue_unlink(NsQueue *queue, NsList *link_)
{
    if (link_ == queue->tail) queue->tail = queue->tail->prev;
    queue->head = ns_list_remove_link(queue->head, link_);
    queue->length--;
}

void
ns_queue_delete_link(NsQueue *queue, NsList *link_)
{
    ns_queue_unlink(queue, link_);
    ns_free(link_);
}

void *
ns_queue_pop_head(NsQueue *queue)
{
    if (!queue->head) return NULL;
    NsList *node = queue->head;
    void *data = node->data;
    queue->head = node->next;
    if (queue->head) queue->head->prev = NULL;
    else queue->tail = NULL;
    ns_free(node);
    queue->length--;
    return data;
}

void *
ns_queue_pop_tail(NsQueue *queue)
{
    if (!queue->tail) return NULL;
    NsList *node = queue->tail;
    void *data = node->data;
    queue->tail = node->prev;
    if (queue->tail) queue->tail->next = NULL;
    else queue->head = NULL;
    ns_free(node);
    queue->length--;
    return data;
}

void *
ns_queue_peek_head(NsQueue *queue)
{
    return queue->head ? queue->head->data : NULL;
}

void *
ns_queue_peek_tail(NsQueue *queue)
{
    return queue->tail ? queue->tail->data : NULL;
}
