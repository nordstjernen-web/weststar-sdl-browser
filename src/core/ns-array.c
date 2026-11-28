/* Weststar SDL Browser — nscore: arrays and byte blocks.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ns-array.h"
#include "ns-mem.h"

#include <string.h>

/* ---- sorting ------------------------------------------------------------- */

/* Top-down merge sort: halves of n/2 and n - n/2, merged taking from the
 * left half while it compares less or equal. */
static void
ns_msort(char *b, size_t n, size_t s, NsCompareDataFunc cmp, void *ud, char *tmp)
{
    if (n <= 1) return;
    size_t n1 = n / 2, n2 = n - n1;
    char *b1 = b, *b2 = b + n1 * s;
    ns_msort(b1, n1, s, cmp, ud, tmp);
    ns_msort(b2, n2, s, cmp, ud, tmp);
    char *out = tmp;
    while (n1 > 0 && n2 > 0) {
        if (cmp(b1, b2, ud) <= 0) {
            memcpy(out, b1, s);
            b1 += s;
            n1--;
        } else {
            memcpy(out, b2, s);
            b2 += s;
            n2--;
        }
        out += s;
    }
    if (n1 > 0) memcpy(out, b1, n1 * s);
    memcpy(b, tmp, (n - n2) * s);
}

void
ns_sort_with_data(void *base, size_t n, size_t size, NsCompareDataFunc compare,
                  void *user_data)
{
    if (n <= 1 || size == 0) return;
    char stack[1024];
    char *tmp = n * size <= sizeof stack ? stack : ns_malloc_n(n, size);
    ns_msort(base, n, size, compare, user_data, tmp);
    if (tmp != stack) ns_free(tmp);
}

typedef struct {
    NsCompareFunc func;
} NsPlainCompare;

static int
ns_plain_compare(const void *a, const void *b, void *data)
{
    return ((NsPlainCompare *)data)->func(a, b);
}

/* ---- NsArray --------------------------------------------------------------- */

typedef struct {
    char *data;
    unsigned int len;
    unsigned int elt_capacity;
    unsigned int elt_size;
    unsigned int zero_terminated : 1;
    unsigned int clear : 1;
    int ref_count;
    NsDestroyNotify clear_func;
} NsRealArray;

#define NS_ELT(a, i) ((a)->data + (size_t)(a)->elt_size * (i))
#define NS_ELT_LEN(a, n) ((size_t)(a)->elt_size * (n))

static void
ns_array_zero_terminate(NsRealArray *a)
{
    if (a->zero_terminated) memset(NS_ELT(a, a->len), 0, a->elt_size);
}

static void
ns_array_expand(NsRealArray *a, unsigned int len)
{
    unsigned int want = a->len + len + a->zero_terminated;
    if (want < a->len) ns_mem_fatal("array would overflow, adding", len, 0);
    if (want > a->elt_capacity) {
        size_t bytes = 16;
        while (bytes < NS_ELT_LEN(a, want)) bytes <<= 1;
        a->elt_capacity = (unsigned int)(bytes / a->elt_size);
        a->data = ns_realloc(a->data, bytes);
    }
}

NsArray *
ns_array_sized_new(int zero_terminated, int clear, unsigned int element_size,
                   unsigned int reserved_size)
{
    NsRealArray *a = ns_malloc(sizeof *a);
    a->data = NULL;
    a->len = 0;
    a->elt_capacity = 0;
    a->elt_size = element_size ? element_size : 1;
    a->zero_terminated = zero_terminated ? 1 : 0;
    a->clear = clear ? 1 : 0;
    a->ref_count = 1;
    a->clear_func = NULL;
    if (a->zero_terminated || reserved_size) {
        ns_array_expand(a, reserved_size);
        ns_array_zero_terminate(a);
    }
    return (NsArray *)a;
}

NsArray *
ns_array_new(int zero_terminated, int clear, unsigned int element_size)
{
    return ns_array_sized_new(zero_terminated, clear, element_size, 0);
}

static char *
ns_array_release(NsRealArray *a, int free_segment, int preserve_wrapper)
{
    char *seg = NULL;
    if (free_segment) {
        if (a->clear_func)
            for (unsigned int i = 0; i < a->len; i++) a->clear_func(NS_ELT(a, i));
        ns_free(a->data);
    } else {
        seg = a->data;
    }
    if (preserve_wrapper) {
        a->data = NULL;
        a->len = 0;
        a->elt_capacity = 0;
    } else {
        ns_free(a);
    }
    return seg;
}

char *
ns_array_free(NsArray *array, int free_segment)
{
    if (!array) return NULL;
    NsRealArray *a = (NsRealArray *)array;
    int last = ns_atomic_ref_count_dec(&a->ref_count);
    return ns_array_release(a, free_segment, !last);
}

NsArray *
ns_array_ref(NsArray *array)
{
    ns_atomic_ref_count_inc(&((NsRealArray *)array)->ref_count);
    return array;
}

void
ns_array_unref(NsArray *array)
{
    if (!array) return;
    NsRealArray *a = (NsRealArray *)array;
    if (ns_atomic_ref_count_dec(&a->ref_count)) ns_array_release(a, 1, 0);
}

unsigned int
ns_array_get_element_size(NsArray *array)
{
    return ((NsRealArray *)array)->elt_size;
}

NsArray *
ns_array_append_vals(NsArray *array, const void *data, unsigned int len)
{
    NsRealArray *a = (NsRealArray *)array;
    if (len == 0) return array;
    ns_array_expand(a, len);
    memcpy(NS_ELT(a, a->len), data, NS_ELT_LEN(a, len));
    a->len += len;
    ns_array_zero_terminate(a);
    return array;
}

NsArray *
ns_array_prepend_vals(NsArray *array, const void *data, unsigned int len)
{
    return ns_array_insert_vals(array, 0, data, len);
}

NsArray *
ns_array_insert_vals(NsArray *array, unsigned int index_, const void *data,
                     unsigned int len)
{
    NsRealArray *a = (NsRealArray *)array;
    if (len == 0) return array;
    if (index_ >= a->len) {
        ns_array_expand(a, index_ - a->len + len);
        return ns_array_append_vals(ns_array_set_size(array, index_), data, len);
    }
    ns_array_expand(a, len);
    memmove(NS_ELT(a, index_ + len), NS_ELT(a, index_), NS_ELT_LEN(a, a->len - index_));
    memcpy(NS_ELT(a, index_), data, NS_ELT_LEN(a, len));
    a->len += len;
    ns_array_zero_terminate(a);
    return array;
}

NsArray *
ns_array_set_size(NsArray *array, unsigned int length)
{
    NsRealArray *a = (NsRealArray *)array;
    if (length > a->len) {
        ns_array_expand(a, length - a->len);
        if (a->clear) memset(NS_ELT(a, a->len), 0, NS_ELT_LEN(a, length - a->len));
    } else if (length < a->len) {
        ns_array_remove_range(array, length, a->len - length);
    }
    a->len = length;
    if (a->data) ns_array_zero_terminate(a);
    return array;
}

NsArray *
ns_array_remove_index(NsArray *array, unsigned int index_)
{
    NsRealArray *a = (NsRealArray *)array;
    if (index_ >= a->len) return NULL;
    if (a->clear_func) a->clear_func(NS_ELT(a, index_));
    if (index_ != a->len - 1)
        memmove(NS_ELT(a, index_), NS_ELT(a, index_ + 1),
                NS_ELT_LEN(a, a->len - index_ - 1));
    a->len--;
    ns_array_zero_terminate(a);
    return array;
}

NsArray *
ns_array_remove_index_fast(NsArray *array, unsigned int index_)
{
    NsRealArray *a = (NsRealArray *)array;
    if (index_ >= a->len) return NULL;
    if (a->clear_func) a->clear_func(NS_ELT(a, index_));
    if (index_ != a->len - 1)
        memcpy(NS_ELT(a, index_), NS_ELT(a, a->len - 1), a->elt_size);
    a->len--;
    ns_array_zero_terminate(a);
    return array;
}

NsArray *
ns_array_remove_range(NsArray *array, unsigned int index_, unsigned int length)
{
    NsRealArray *a = (NsRealArray *)array;
    if (index_ > a->len || index_ + length > a->len || index_ + length < index_)
        return NULL;
    if (length == 0) return array;
    if (a->clear_func)
        for (unsigned int i = 0; i < length; i++) a->clear_func(NS_ELT(a, index_ + i));
    if (index_ + length != a->len)
        memmove(NS_ELT(a, index_), NS_ELT(a, index_ + length),
                NS_ELT_LEN(a, a->len - (index_ + length)));
    a->len -= length;
    ns_array_zero_terminate(a);
    return array;
}

void
ns_array_sort(NsArray *array, NsCompareFunc compare_func)
{
    NsRealArray *a = (NsRealArray *)array;
    NsPlainCompare pc = { compare_func };
    if (a->len > 0) ns_sort_with_data(a->data, a->len, a->elt_size, ns_plain_compare, &pc);
}

void
ns_array_sort_with_data(NsArray *array, NsCompareDataFunc compare_func, void *user_data)
{
    NsRealArray *a = (NsRealArray *)array;
    if (a->len > 0) ns_sort_with_data(a->data, a->len, a->elt_size, compare_func, user_data);
}

void
ns_array_set_clear_func(NsArray *array, NsDestroyNotify clear_func)
{
    ((NsRealArray *)array)->clear_func = clear_func;
}

/* ---- NsPtrArray ------------------------------------------------------------ */

typedef struct {
    void **pdata;
    unsigned int len;
    unsigned int alloc;
    int ref_count;
    NsDestroyNotify element_free_func;
} NsRealPtrArray;

static void
ns_ptr_array_expand(NsRealPtrArray *a, unsigned int len)
{
    if (a->len + len < a->len) ns_mem_fatal("array would overflow, adding", len, 0);
    if (a->len + len > a->alloc) {
        size_t bytes = 16;
        while (bytes < sizeof(void *) * ((size_t)a->len + len)) bytes <<= 1;
        a->alloc = (unsigned int)(bytes / sizeof(void *));
        a->pdata = ns_realloc(a->pdata, bytes);
    }
}

NsPtrArray *
ns_ptr_array_new_full(unsigned int reserved_size, NsDestroyNotify element_free_func)
{
    NsRealPtrArray *a = ns_malloc(sizeof *a);
    a->pdata = NULL;
    a->len = 0;
    a->alloc = 0;
    a->ref_count = 1;
    a->element_free_func = element_free_func;
    if (reserved_size) ns_ptr_array_expand(a, reserved_size);
    return (NsPtrArray *)a;
}

NsPtrArray *
ns_ptr_array_new(void)
{
    return ns_ptr_array_new_full(0, NULL);
}

NsPtrArray *
ns_ptr_array_new_with_free_func(NsDestroyNotify element_free_func)
{
    return ns_ptr_array_new_full(0, element_free_func);
}

NsPtrArray *
ns_ptr_array_sized_new(unsigned int reserved_size)
{
    return ns_ptr_array_new_full(reserved_size, NULL);
}

NsPtrArray *
ns_ptr_array_copy(NsPtrArray *array, NsCopyFunc func, void *user_data)
{
    NsRealPtrArray *a = (NsRealPtrArray *)array;
    NsRealPtrArray *c = (NsRealPtrArray *)ns_ptr_array_new_full(0, a->element_free_func);
    if (a->alloc > 0) {
        ns_ptr_array_expand(c, a->len);
        if (a->len > 0) {
            if (func)
                for (unsigned int i = 0; i < a->len; i++) c->pdata[i] = func(a->pdata[i], user_data);
            else
                memcpy(c->pdata, a->pdata, a->len * sizeof(void *));
            c->len = a->len;
        }
    }
    return (NsPtrArray *)c;
}

static void **
ns_ptr_array_release(NsRealPtrArray *a, int free_segment, int preserve_wrapper)
{
    void **seg = NULL;
    if (free_segment) {
        void **stolen = a->pdata;
        a->pdata = NULL;
        if (a->element_free_func)
            for (unsigned int i = 0; i < a->len; i++) a->element_free_func(stolen[i]);
        ns_free(stolen);
    } else {
        seg = a->pdata;
    }
    if (preserve_wrapper) {
        a->pdata = NULL;
        a->len = 0;
        a->alloc = 0;
    } else {
        ns_free(a);
    }
    return seg;
}

void **
ns_ptr_array_free(NsPtrArray *array, int free_segment)
{
    if (!array) return NULL;
    NsRealPtrArray *a = (NsRealPtrArray *)array;
    int last = ns_atomic_ref_count_dec(&a->ref_count);
    return ns_ptr_array_release(a, free_segment, !last);
}

NsPtrArray *
ns_ptr_array_ref(NsPtrArray *array)
{
    ns_atomic_ref_count_inc(&((NsRealPtrArray *)array)->ref_count);
    return array;
}

void
ns_ptr_array_unref(NsPtrArray *array)
{
    if (!array) return;
    NsRealPtrArray *a = (NsRealPtrArray *)array;
    if (ns_atomic_ref_count_dec(&a->ref_count)) ns_ptr_array_release(a, 1, 0);
}

void
ns_ptr_array_set_free_func(NsPtrArray *array, NsDestroyNotify element_free_func)
{
    ((NsRealPtrArray *)array)->element_free_func = element_free_func;
}

void
ns_ptr_array_set_size(NsPtrArray *array, int length)
{
    NsRealPtrArray *a = (NsRealPtrArray *)array;
    if (length < 0) return;
    unsigned int n = (unsigned int)length;
    if (n > a->len) {
        ns_ptr_array_expand(a, n - a->len);
        for (unsigned int i = a->len; i < n; i++) a->pdata[i] = NULL;
        a->len = n;
    } else if (n < a->len) {
        ns_ptr_array_remove_range(array, n, a->len - n);
    }
}

static void *
ns_ptr_array_take(NsRealPtrArray *a, unsigned int index_, int fast, int free_element)
{
    if (index_ >= a->len) return NULL;
    void *result = a->pdata[index_];
    if (free_element && a->element_free_func) a->element_free_func(result);
    if (index_ != a->len - 1) {
        if (fast)
            a->pdata[index_] = a->pdata[a->len - 1];
        else
            memmove(a->pdata + index_, a->pdata + index_ + 1,
                    sizeof(void *) * (a->len - index_ - 1));
    }
    a->len--;
    return result;
}

void *
ns_ptr_array_remove_index(NsPtrArray *array, unsigned int index_)
{
    return ns_ptr_array_take((NsRealPtrArray *)array, index_, 0, 1);
}

void *
ns_ptr_array_remove_index_fast(NsPtrArray *array, unsigned int index_)
{
    return ns_ptr_array_take((NsRealPtrArray *)array, index_, 1, 1);
}

void *
ns_ptr_array_steal_index(NsPtrArray *array, unsigned int index_)
{
    return ns_ptr_array_take((NsRealPtrArray *)array, index_, 0, 0);
}

void *
ns_ptr_array_steal_index_fast(NsPtrArray *array, unsigned int index_)
{
    return ns_ptr_array_take((NsRealPtrArray *)array, index_, 1, 0);
}

int
ns_ptr_array_remove(NsPtrArray *array, void *data)
{
    unsigned int i;
    if (!ns_ptr_array_find(array, data, &i)) return 0;
    ns_ptr_array_remove_index(array, i);
    return 1;
}

int
ns_ptr_array_remove_fast(NsPtrArray *array, void *data)
{
    unsigned int i;
    if (!ns_ptr_array_find(array, data, &i)) return 0;
    ns_ptr_array_remove_index_fast(array, i);
    return 1;
}

NsPtrArray *
ns_ptr_array_remove_range(NsPtrArray *array, unsigned int index_, unsigned int length)
{
    NsRealPtrArray *a = (NsRealPtrArray *)array;
    if (index_ > a->len || index_ + length > a->len || index_ + length < index_)
        return NULL;
    if (length == 0) return array;
    if (a->element_free_func)
        for (unsigned int i = index_; i < index_ + length; i++) a->element_free_func(a->pdata[i]);
    if (index_ + length != a->len)
        memmove(a->pdata + index_, a->pdata + index_ + length,
                sizeof(void *) * (a->len - (index_ + length)));
    a->len -= length;
    return array;
}

void
ns_ptr_array_add(NsPtrArray *array, void *data)
{
    NsRealPtrArray *a = (NsRealPtrArray *)array;
    ns_ptr_array_expand(a, 1);
    a->pdata[a->len++] = data;
}

void
ns_ptr_array_insert(NsPtrArray *array, int index_, void *data)
{
    NsRealPtrArray *a = (NsRealPtrArray *)array;
    if (index_ < -1 || index_ > (int)a->len) return;
    ns_ptr_array_expand(a, 1);
    unsigned int at = index_ < 0 ? a->len : (unsigned int)index_;
    if (at < a->len)
        memmove(a->pdata + at + 1, a->pdata + at, sizeof(void *) * (a->len - at));
    a->pdata[at] = data;
    a->len++;
}

void
ns_ptr_array_sort(NsPtrArray *array, NsCompareFunc compare_func)
{
    NsPlainCompare pc = { compare_func };
    if (array->len > 0)
        ns_sort_with_data(array->pdata, array->len, sizeof(void *), ns_plain_compare, &pc);
}

void
ns_ptr_array_sort_with_data(NsPtrArray *array, NsCompareDataFunc compare_func,
                            void *user_data)
{
    if (array->len > 0)
        ns_sort_with_data(array->pdata, array->len, sizeof(void *), compare_func, user_data);
}

int
ns_ptr_array_find_with_equal_func(NsPtrArray *haystack, const void *needle,
                                  NsEqualFunc equal_func, unsigned int *index_)
{
    for (unsigned int i = 0; i < haystack->len; i++) {
        if (equal_func ? equal_func(haystack->pdata[i], needle)
                       : haystack->pdata[i] == needle) {
            if (index_) *index_ = i;
            return 1;
        }
    }
    return 0;
}

int
ns_ptr_array_find(NsPtrArray *haystack, const void *needle, unsigned int *index_)
{
    return ns_ptr_array_find_with_equal_func(haystack, needle, NULL, index_);
}

void
ns_ptr_array_foreach(NsPtrArray *array, NsFunc func, void *user_data)
{
    for (unsigned int i = 0; i < array->len; i++) func(array->pdata[i], user_data);
}

/* ---- NsByteArray: an NsArray of bytes ---------------------------------------- */

NsByteArray *
ns_byte_array_new(void)
{
    return (NsByteArray *)ns_array_sized_new(0, 0, 1, 0);
}

NsByteArray *
ns_byte_array_sized_new(unsigned int reserved_size)
{
    return (NsByteArray *)ns_array_sized_new(0, 0, 1, reserved_size);
}

uint8_t *
ns_byte_array_free(NsByteArray *array, int free_segment)
{
    return (uint8_t *)ns_array_free((NsArray *)array, free_segment);
}

NsGBytes *
ns_byte_array_free_to_bytes(NsByteArray *array)
{
    size_t len = array->len;
    if (len == 0) {
        ns_byte_array_free(array, 1);
        return ns_bytes_new(NULL, 0);
    }
    return ns_bytes_new_take(ns_byte_array_free(array, 0), len);
}

NsByteArray *
ns_byte_array_ref(NsByteArray *array)
{
    return (NsByteArray *)ns_array_ref((NsArray *)array);
}

void
ns_byte_array_unref(NsByteArray *array)
{
    ns_array_unref((NsArray *)array);
}

NsByteArray *
ns_byte_array_append(NsByteArray *array, const uint8_t *data, unsigned int len)
{
    return (NsByteArray *)ns_array_append_vals((NsArray *)array, data, len);
}

NsByteArray *
ns_byte_array_prepend(NsByteArray *array, const uint8_t *data, unsigned int len)
{
    return (NsByteArray *)ns_array_insert_vals((NsArray *)array, 0, data, len);
}

NsByteArray *
ns_byte_array_set_size(NsByteArray *array, unsigned int length)
{
    return (NsByteArray *)ns_array_set_size((NsArray *)array, length);
}

NsByteArray *
ns_byte_array_remove_range(NsByteArray *array, unsigned int index_, unsigned int length)
{
    return (NsByteArray *)ns_array_remove_range((NsArray *)array, index_, length);
}

/* ---- NsGBytes ------------------------------------------------------------------ */

struct NsGBytes {
    const void *data;
    size_t size;
    int ref_count;
    NsDestroyNotify free_func;
    void *user_data;
};

NsGBytes *
ns_bytes_new_with_free_func(const void *data, size_t size, NsDestroyNotify free_func,
                            void *user_data)
{
    NsGBytes *b = ns_malloc(sizeof *b);
    b->data = data;
    b->size = size;
    b->ref_count = 1;
    b->free_func = free_func;
    b->user_data = user_data;
    return b;
}

NsGBytes *
ns_bytes_new(const void *data, size_t size)
{
    return ns_bytes_new_take(ns_memdup2(data, size), size);
}

NsGBytes *
ns_bytes_new_take(void *data, size_t size)
{
    return ns_bytes_new_with_free_func(data, size, ns_free, data);
}

NsGBytes *
ns_bytes_new_static(const void *data, size_t size)
{
    return ns_bytes_new_with_free_func(data, size, NULL, NULL);
}

NsGBytes *
ns_bytes_ref(NsGBytes *bytes)
{
    if (bytes) ns_atomic_ref_count_inc(&bytes->ref_count);
    return bytes;
}

void
ns_bytes_unref(NsGBytes *bytes)
{
    if (!bytes) return;
    if (ns_atomic_ref_count_dec(&bytes->ref_count)) {
        if (bytes->free_func) bytes->free_func(bytes->user_data);
        ns_free(bytes);
    }
}

const void *
ns_bytes_get_data(NsGBytes *bytes, size_t *size)
{
    if (size) *size = bytes ? bytes->size : 0;
    return bytes ? bytes->data : NULL;
}

size_t
ns_bytes_get_size(NsGBytes *bytes)
{
    return bytes ? bytes->size : 0;
}

unsigned int
ns_bytes_hash(const void *bytes)
{
    const NsGBytes *b = bytes;
    uint32_t h = 5381;
    const signed char *p = b->data;
    for (size_t i = 0; i < b->size; i++) h = (h << 5) + h + (uint32_t)p[i];
    return h;
}

int
ns_bytes_equal(const void *bytes1, const void *bytes2)
{
    const NsGBytes *a = bytes1, *b = bytes2;
    return a->size == b->size && (a->size == 0 || memcmp(a->data, b->data, a->size) == 0);
}

int
ns_bytes_compare(const void *bytes1, const void *bytes2)
{
    const NsGBytes *a = bytes1, *b = bytes2;
    size_t n = a->size < b->size ? a->size : b->size;
    int r = n ? memcmp(a->data, b->data, n) : 0;
    if (r == 0) r = a->size < b->size ? -1 : a->size > b->size ? 1 : 0;
    return r;
}
