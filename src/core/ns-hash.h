/* Weststar SDL Browser — nscore: hash tables (GHashTable) and the common
 * hash and equality functions.
 *
 * The table is laid out as GLib's is (open addressing over a power-of-two
 * array, a prime modulus, quadratic probing, the same growth and shrink
 * points and the same rehashing order), so a table filled the same way
 * iterates in the same order as GLib's would, and ns_str_hash is GLib's
 * hash of a string. See THIRD-PARTY-LICENSES.md.
 *
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_HASH_H
#define NS_HASH_H

#include "ns-base.h"
#include "ns-list.h"

NS_BEGIN_DECLS

typedef struct NsHashTable NsHashTable;

typedef struct NsHashTableIter {
    NsHashTable *table;
    ptrdiff_t position;
    void *reserved[2];
} NsHashTableIter;

typedef void (*NsHFunc)(void *key, void *value, void *user_data);
typedef int (*NsHRFunc)(void *key, void *value, void *user_data);

NsHashTable *ns_hash_table_new(NsHashFunc hash_func, NsEqualFunc key_equal_func);
NsHashTable *ns_hash_table_new_full(NsHashFunc hash_func, NsEqualFunc key_equal_func,
                                    NsDestroyNotify key_destroy_func,
                                    NsDestroyNotify value_destroy_func);
void ns_hash_table_destroy(NsHashTable *hash_table);
NsHashTable *ns_hash_table_ref(NsHashTable *hash_table);
void ns_hash_table_unref(NsHashTable *hash_table);
int ns_hash_table_insert(NsHashTable *hash_table, void *key, void *value);
int ns_hash_table_replace(NsHashTable *hash_table, void *key, void *value);
int ns_hash_table_add(NsHashTable *hash_table, void *key);
int ns_hash_table_remove(NsHashTable *hash_table, const void *key);
void ns_hash_table_remove_all(NsHashTable *hash_table);
int ns_hash_table_steal(NsHashTable *hash_table, const void *key);
int ns_hash_table_steal_extended(NsHashTable *hash_table, const void *lookup_key,
                                 void **stolen_key, void **stolen_value);
void ns_hash_table_steal_all(NsHashTable *hash_table);
void *ns_hash_table_lookup(NsHashTable *hash_table, const void *key);
int ns_hash_table_lookup_extended(NsHashTable *hash_table, const void *lookup_key,
                                  void **orig_key, void **value);
int ns_hash_table_contains(NsHashTable *hash_table, const void *key);
unsigned int ns_hash_table_size(NsHashTable *hash_table);
void ns_hash_table_foreach(NsHashTable *hash_table, NsHFunc func, void *user_data);
unsigned int ns_hash_table_foreach_remove(NsHashTable *hash_table, NsHRFunc func,
                                          void *user_data);
NsList *ns_hash_table_get_keys(NsHashTable *hash_table);
NsList *ns_hash_table_get_values(NsHashTable *hash_table);

void ns_hash_table_iter_init(NsHashTableIter *iter, NsHashTable *hash_table);
int ns_hash_table_iter_next(NsHashTableIter *iter, void **key, void **value);
NsHashTable *ns_hash_table_iter_get_hash_table(NsHashTableIter *iter);
void ns_hash_table_iter_remove(NsHashTableIter *iter);
void ns_hash_table_iter_replace(NsHashTableIter *iter, void *value);
void ns_hash_table_iter_steal(NsHashTableIter *iter);

int ns_str_equal(const void *v1, const void *v2);
unsigned int ns_str_hash(const void *v);
int ns_direct_equal(const void *v1, const void *v2);
unsigned int ns_direct_hash(const void *v);
int ns_int_equal(const void *v1, const void *v2);
unsigned int ns_int_hash(const void *v);
int ns_int64_equal(const void *v1, const void *v2);
unsigned int ns_int64_hash(const void *v);
int ns_double_equal(const void *v1, const void *v2);
unsigned int ns_double_hash(const void *v);

NS_END_DECLS

#endif
