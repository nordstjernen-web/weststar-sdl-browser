/* Weststar SDL Browser — nscore: arrays (GArray, GPtrArray, GByteArray)
 * and immutable byte blocks (GBytes).
 *
 * The public fields are GLib's (data/len, pdata/len), and so are the
 * conventions: removing an element runs the array's free or clear
 * function on it, freeing with free_segment false hands the buffer back,
 * and sorting is stable.
 *
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_ARRAY_H
#define NS_ARRAY_H

#include "ns-base.h"

NS_BEGIN_DECLS

typedef struct NsArray {
    char *data;
    unsigned int len;
} NsArray;

typedef struct NsPtrArray {
    void **pdata;
    unsigned int len;
} NsPtrArray;

typedef struct NsByteArray {
    uint8_t *data;
    unsigned int len;
} NsByteArray;

typedef struct NsGBytes NsGBytes;

#define ns_array_index(a, t, i) (((t *)(void *)(a)->data)[(i)])
#define ns_array_append_val(a, v) ns_array_append_vals((a), &(v), 1)
#define ns_array_prepend_val(a, v) ns_array_prepend_vals((a), &(v), 1)
#define ns_array_insert_val(a, i, v) ns_array_insert_vals((a), (i), &(v), 1)
#define ns_ptr_array_index(a, i) ((a)->pdata)[(i)]

NsArray *ns_array_new(int zero_terminated, int clear, unsigned int element_size);
NsArray *ns_array_sized_new(int zero_terminated, int clear, unsigned int element_size,
                            unsigned int reserved_size);
char *ns_array_free(NsArray *array, int free_segment);
NsArray *ns_array_ref(NsArray *array);
void ns_array_unref(NsArray *array);
unsigned int ns_array_get_element_size(NsArray *array);
NsArray *ns_array_append_vals(NsArray *array, const void *data, unsigned int len);
NsArray *ns_array_prepend_vals(NsArray *array, const void *data, unsigned int len);
NsArray *ns_array_insert_vals(NsArray *array, unsigned int index_, const void *data,
                              unsigned int len);
NsArray *ns_array_set_size(NsArray *array, unsigned int length);
NsArray *ns_array_remove_index(NsArray *array, unsigned int index_);
NsArray *ns_array_remove_index_fast(NsArray *array, unsigned int index_);
NsArray *ns_array_remove_range(NsArray *array, unsigned int index_, unsigned int length);
void ns_array_sort(NsArray *array, NsCompareFunc compare_func);
void ns_array_sort_with_data(NsArray *array, NsCompareDataFunc compare_func,
                             void *user_data);
void ns_array_set_clear_func(NsArray *array, NsDestroyNotify clear_func);

NsPtrArray *ns_ptr_array_new(void);
NsPtrArray *ns_ptr_array_new_with_free_func(NsDestroyNotify element_free_func);
NsPtrArray *ns_ptr_array_sized_new(unsigned int reserved_size);
NsPtrArray *ns_ptr_array_new_full(unsigned int reserved_size,
                                  NsDestroyNotify element_free_func);
NsPtrArray *ns_ptr_array_copy(NsPtrArray *array, NsCopyFunc func, void *user_data);
void **ns_ptr_array_free(NsPtrArray *array, int free_segment);
NsPtrArray *ns_ptr_array_ref(NsPtrArray *array);
void ns_ptr_array_unref(NsPtrArray *array);
void ns_ptr_array_set_free_func(NsPtrArray *array, NsDestroyNotify element_free_func);
void ns_ptr_array_set_size(NsPtrArray *array, int length);
void *ns_ptr_array_remove_index(NsPtrArray *array, unsigned int index_);
void *ns_ptr_array_remove_index_fast(NsPtrArray *array, unsigned int index_);
void *ns_ptr_array_steal_index(NsPtrArray *array, unsigned int index_);
void *ns_ptr_array_steal_index_fast(NsPtrArray *array, unsigned int index_);
int ns_ptr_array_remove(NsPtrArray *array, void *data);
int ns_ptr_array_remove_fast(NsPtrArray *array, void *data);
NsPtrArray *ns_ptr_array_remove_range(NsPtrArray *array, unsigned int index_,
                                      unsigned int length);
void ns_ptr_array_add(NsPtrArray *array, void *data);
void ns_ptr_array_insert(NsPtrArray *array, int index_, void *data);
void ns_ptr_array_sort(NsPtrArray *array, NsCompareFunc compare_func);
void ns_ptr_array_sort_with_data(NsPtrArray *array, NsCompareDataFunc compare_func,
                                 void *user_data);
int ns_ptr_array_find(NsPtrArray *haystack, const void *needle, unsigned int *index_);
int ns_ptr_array_find_with_equal_func(NsPtrArray *haystack, const void *needle,
                                      NsEqualFunc equal_func, unsigned int *index_);
void ns_ptr_array_foreach(NsPtrArray *array, NsFunc func, void *user_data);

NsByteArray *ns_byte_array_new(void);
NsByteArray *ns_byte_array_sized_new(unsigned int reserved_size);
uint8_t *ns_byte_array_free(NsByteArray *array, int free_segment);
NsGBytes *ns_byte_array_free_to_bytes(NsByteArray *array);
NsByteArray *ns_byte_array_ref(NsByteArray *array);
void ns_byte_array_unref(NsByteArray *array);
NsByteArray *ns_byte_array_append(NsByteArray *array, const uint8_t *data,
                                  unsigned int len);
NsByteArray *ns_byte_array_prepend(NsByteArray *array, const uint8_t *data,
                                   unsigned int len);
NsByteArray *ns_byte_array_set_size(NsByteArray *array, unsigned int length);
NsByteArray *ns_byte_array_remove_range(NsByteArray *array, unsigned int index_,
                                        unsigned int length);

NsGBytes *ns_bytes_new(const void *data, size_t size);
NsGBytes *ns_bytes_new_take(void *data, size_t size);
NsGBytes *ns_bytes_new_static(const void *data, size_t size);
NsGBytes *ns_bytes_new_with_free_func(const void *data, size_t size,
                                     NsDestroyNotify free_func, void *user_data);
NsGBytes *ns_bytes_ref(NsGBytes *bytes);
void ns_bytes_unref(NsGBytes *bytes);
const void *ns_bytes_get_data(NsGBytes *bytes, size_t *size);
size_t ns_bytes_get_size(NsGBytes *bytes);
unsigned int ns_bytes_hash(const void *bytes);
int ns_bytes_equal(const void *bytes1, const void *bytes2);
int ns_bytes_compare(const void *bytes1, const void *bytes2);

/* A stable merge sort of n elements of size bytes (GLib's g_qsort_with_data
 * is stable too, and the engine's comparisons may leave ties). */
void ns_sort_with_data(void *base, size_t n, size_t size, NsCompareDataFunc compare,
                       void *user_data);

NS_END_DECLS

#endif
