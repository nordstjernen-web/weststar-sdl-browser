/* Weststar SDL Browser — nscore: hash tables.
 *
 * The design is GLib's (ghash.c): buckets in a power-of-two array with a
 * stored hash per bucket (0 unused, 1 a tombstone, 2 and above a key),
 * the first bucket at (hash * 11) % p for the prime p below the size,
 * probing by 1, 2, 3... steps, growth once 15/16 of the buckets are used
 * (tombstones included), shrinking below 1/4, the new size from
 * nnodes * 1.333, and the in-place rehash that walks the old buckets in
 * order and evicts into the new positions. Kept the same so that the
 * engine's tables iterate in the order they did on GLib. Written anew from
 * that description.
 *
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ns-hash.h"
#include "ns-mem.h"

#include <string.h>

#define NS_HASH_MIN_SHIFT 3
#define NS_UNUSED 0u
#define NS_TOMBSTONE 1u
#define NS_IS_REAL(h) ((h) >= 2u)

struct NsHashTable {
    unsigned int size;
    unsigned int mod;
    unsigned int mask;
    unsigned int nnodes;
    unsigned int noccupied;  /* nodes plus tombstones */
    void **keys;
    void **values;
    unsigned int *hashes;
    NsHashFunc hash_func;
    NsEqualFunc key_equal_func;
    int ref_count;
    NsDestroyNotify key_destroy_func;
    NsDestroyNotify value_destroy_func;
};

/* For each shift, the largest prime below 1 << shift. */
static const unsigned int ns_prime_mod[32] = {
    1, 2, 3, 7, 13, 31, 61, 127, 251, 509, 1021, 2039, 4093, 8191, 16381,
    32749, 65521, 131071, 262139, 524287, 1048573, 2097143, 4194301, 8388593,
    16777213, 33554393, 67108859, 134217689, 268435399, 536870909,
    1073741789, 2147483647,
};

static void
ns_hash_set_shift(NsHashTable *t, int shift)
{
    t->size = 1u << shift;
    t->mod = ns_prime_mod[shift];
    t->mask = t->size - 1;
}

static void
ns_hash_set_shift_from_size(NsHashTable *t, int size)
{
    int shift = 0;
    for (; size; shift++) size >>= 1;
    if (shift < NS_HASH_MIN_SHIFT) shift = NS_HASH_MIN_SHIFT;
    ns_hash_set_shift(t, shift);
}

static inline unsigned int
ns_hash_to_index(const NsHashTable *t, unsigned int hash)
{
    return (hash * 11u) % t->mod;
}

static void
ns_hash_setup_storage(NsHashTable *t)
{
    ns_hash_set_shift(t, NS_HASH_MIN_SHIFT);
    t->nnodes = 0;
    t->noccupied = 0;
    t->keys = ns_malloc0_n(t->size, sizeof(void *));
    t->values = ns_malloc0_n(t->size, sizeof(void *));
    t->hashes = ns_malloc0_n(t->size, sizeof(unsigned int));
}

NsHashTable *
ns_hash_table_new_full(NsHashFunc hash_func, NsEqualFunc key_equal_func,
                       NsDestroyNotify key_destroy_func,
                       NsDestroyNotify value_destroy_func)
{
    NsHashTable *t = ns_malloc0(sizeof *t);
    t->hash_func = hash_func ? hash_func : ns_direct_hash;
    t->key_equal_func = key_equal_func;
    t->ref_count = 1;
    t->key_destroy_func = key_destroy_func;
    t->value_destroy_func = value_destroy_func;
    ns_hash_setup_storage(t);
    return t;
}

NsHashTable *
ns_hash_table_new(NsHashFunc hash_func, NsEqualFunc key_equal_func)
{
    return ns_hash_table_new_full(hash_func, key_equal_func, NULL, NULL);
}

/* The bucket holding key, or else where it would go: the first tombstone
 * on its probe path, or the unused bucket that ends the path. */
static unsigned int
ns_hash_lookup_node(NsHashTable *t, const void *key, unsigned int *hash_return)
{
    unsigned int hash = t->hash_func(key);
    if (!NS_IS_REAL(hash)) hash = 2;
    *hash_return = hash;

    unsigned int index = ns_hash_to_index(t, hash);
    unsigned int node_hash = t->hashes[index];
    unsigned int first_tombstone = 0;
    int have_tombstone = 0;
    unsigned int step = 0;
    while (node_hash != NS_UNUSED) {
        if (node_hash == hash) {
            void *node_key = t->keys[index];
            if (t->key_equal_func ? t->key_equal_func(node_key, key) : node_key == key)
                return index;
        } else if (node_hash == NS_TOMBSTONE && !have_tombstone) {
            first_tombstone = index;
            have_tombstone = 1;
        }
        step++;
        index = (index + step) & t->mask;
        node_hash = t->hashes[index];
    }
    return have_tombstone ? first_tombstone : index;
}

static inline int
ns_bit_get(const uint32_t *bits, unsigned int i)
{
    return (bits[i / 32] >> (i % 32)) & 1;
}

static inline void
ns_bit_set(uint32_t *bits, unsigned int i)
{
    bits[i / 32] |= 1u << (i % 32);
}

static void
ns_hash_resize(NsHashTable *t)
{
    unsigned int old_size = t->size;
    ns_hash_set_shift_from_size(t, (int)(t->nnodes * 1.333));

    uint32_t *moved;
    if (t->size > old_size) {
        t->keys = ns_realloc_n(t->keys, t->size, sizeof(void *));
        t->values = ns_realloc_n(t->values, t->size, sizeof(void *));
        t->hashes = ns_realloc_n(t->hashes, t->size, sizeof(unsigned int));
        memset(t->hashes + old_size, 0, (t->size - old_size) * sizeof(unsigned int));
        memset(t->keys + old_size, 0, (t->size - old_size) * sizeof(void *));
        memset(t->values + old_size, 0, (t->size - old_size) * sizeof(void *));
        moved = ns_malloc0_n((t->size + 31) / 32, sizeof(uint32_t));
    } else {
        moved = ns_malloc0_n((old_size + 31) / 32, sizeof(uint32_t));
    }

    /* Walk the old buckets in order. Each key not yet moved is taken out
     * and put in its new bucket; a key found there is taken out in turn
     * and placed next, until a free bucket ends the chain. */
    for (unsigned int i = 0; i < old_size; i++) {
        unsigned int node_hash = t->hashes[i];
        if (!NS_IS_REAL(node_hash)) {
            t->hashes[i] = NS_UNUSED;
            continue;
        }
        if (ns_bit_get(moved, i)) continue;

        t->hashes[i] = NS_UNUSED;
        void *key = t->keys[i], *value = t->values[i];
        t->keys[i] = NULL;
        t->values[i] = NULL;
        for (;;) {
            unsigned int index = ns_hash_to_index(t, node_hash);
            unsigned int step = 0;
            while (ns_bit_get(moved, index)) {
                step++;
                index = (index + step) & t->mask;
            }
            ns_bit_set(moved, index);

            unsigned int replaced_hash = t->hashes[index];
            t->hashes[index] = node_hash;
            void *replaced_key = t->keys[index], *replaced_value = t->values[index];
            t->keys[index] = key;
            t->values[index] = value;
            if (!NS_IS_REAL(replaced_hash)) break;
            node_hash = replaced_hash;
            key = replaced_key;
            value = replaced_value;
        }
    }
    ns_free(moved);

    if (t->size < old_size) {
        t->keys = ns_realloc_n(t->keys, t->size, sizeof(void *));
        t->values = ns_realloc_n(t->values, t->size, sizeof(void *));
        t->hashes = ns_realloc_n(t->hashes, t->size, sizeof(unsigned int));
    }
    t->noccupied = t->nnodes;
}

static void
ns_hash_maybe_resize(NsHashTable *t)
{
    unsigned int noccupied = t->noccupied;
    unsigned int size = t->size;
    if ((size > t->nnodes * 4 && size > 1u << NS_HASH_MIN_SHIFT) ||
        size <= noccupied + noccupied / 16)
        ns_hash_resize(t);
}

static int
ns_hash_insert_node(NsHashTable *t, unsigned int index, unsigned int key_hash,
                    void *new_key, void *new_value, int keep_new_key, int reusing_key)
{
    unsigned int old_hash = t->hashes[index];
    int exists = NS_IS_REAL(old_hash);
    void *key_to_free = NULL, *value_to_free = NULL, *key_to_keep;

    if (exists) {
        value_to_free = t->values[index];
        if (keep_new_key) {
            key_to_free = t->keys[index];
            key_to_keep = new_key;
        } else {
            key_to_free = new_key;
            key_to_keep = t->keys[index];
        }
    } else {
        t->hashes[index] = key_hash;
        key_to_keep = new_key;
    }
    t->keys[index] = key_to_keep;
    t->values[index] = new_value;

    if (!exists) {
        t->nnodes++;
        if (old_hash == NS_UNUSED) {
            t->noccupied++;
            ns_hash_maybe_resize(t);
        }
    } else {
        if (t->key_destroy_func && !reusing_key) t->key_destroy_func(key_to_free);
        if (t->value_destroy_func) t->value_destroy_func(value_to_free);
    }
    return !exists;
}

static int
ns_hash_insert_internal(NsHashTable *t, void *key, void *value, int keep_new_key)
{
    unsigned int key_hash;
    unsigned int index = ns_hash_lookup_node(t, key, &key_hash);
    return ns_hash_insert_node(t, index, key_hash, key, value, keep_new_key, 0);
}

int
ns_hash_table_insert(NsHashTable *hash_table, void *key, void *value)
{
    return ns_hash_insert_internal(hash_table, key, value, 0);
}

int
ns_hash_table_replace(NsHashTable *hash_table, void *key, void *value)
{
    return ns_hash_insert_internal(hash_table, key, value, 1);
}

int
ns_hash_table_add(NsHashTable *hash_table, void *key)
{
    return ns_hash_insert_internal(hash_table, key, key, 1);
}

static void
ns_hash_remove_node(NsHashTable *t, unsigned int i, int notify)
{
    void *key = t->keys[i], *value = t->values[i];
    t->hashes[i] = NS_TOMBSTONE;
    t->keys[i] = NULL;
    t->values[i] = NULL;
    t->nnodes--;
    if (notify && t->key_destroy_func) t->key_destroy_func(key);
    if (notify && t->value_destroy_func) t->value_destroy_func(value);
}

static int
ns_hash_remove_internal(NsHashTable *t, const void *key, int notify)
{
    unsigned int hash;
    unsigned int i = ns_hash_lookup_node(t, key, &hash);
    if (!NS_IS_REAL(t->hashes[i])) return 0;
    ns_hash_remove_node(t, i, notify);
    ns_hash_maybe_resize(t);
    return 1;
}

int
ns_hash_table_remove(NsHashTable *hash_table, const void *key)
{
    return ns_hash_remove_internal(hash_table, key, 1);
}

int
ns_hash_table_steal(NsHashTable *hash_table, const void *key)
{
    return ns_hash_remove_internal(hash_table, key, 0);
}

int
ns_hash_table_steal_extended(NsHashTable *hash_table, const void *lookup_key,
                             void **stolen_key, void **stolen_value)
{
    NsHashTable *t = hash_table;
    unsigned int hash;
    unsigned int i = ns_hash_lookup_node(t, lookup_key, &hash);
    if (!NS_IS_REAL(t->hashes[i])) {
        if (stolen_key) *stolen_key = NULL;
        if (stolen_value) *stolen_value = NULL;
        return 0;
    }
    if (stolen_key) {
        *stolen_key = t->keys[i];
        t->keys[i] = NULL;
    }
    if (stolen_value) {
        *stolen_value = t->values[i];
        t->values[i] = NULL;
    }
    ns_hash_remove_node(t, i, 0);
    ns_hash_maybe_resize(t);
    return 1;
}

/* Empties the table. Destroy notifies run on a detached copy, so that they
 * may use the table, which already looks empty (or, at destruction,
 * gone). */
static void
ns_hash_remove_all_nodes(NsHashTable *t, int notify, int destruction)
{
    t->nnodes = 0;
    t->noccupied = 0;
    if (!notify || (!t->key_destroy_func && !t->value_destroy_func)) {
        if (!destruction) {
            memset(t->hashes, 0, t->size * sizeof(unsigned int));
            memset(t->keys, 0, t->size * sizeof(void *));
            memset(t->values, 0, t->size * sizeof(void *));
        }
        return;
    }
    unsigned int old_size = t->size;
    void **old_keys = t->keys, **old_values = t->values;
    unsigned int *old_hashes = t->hashes;
    t->keys = NULL;
    t->values = NULL;
    t->hashes = NULL;
    if (!destruction)
        ns_hash_setup_storage(t);
    else
        t->size = t->mod = t->mask = 0;
    for (unsigned int i = 0; i < old_size; i++) {
        if (NS_IS_REAL(old_hashes[i])) {
            if (t->key_destroy_func) t->key_destroy_func(old_keys[i]);
            if (t->value_destroy_func) t->value_destroy_func(old_values[i]);
        }
    }
    ns_free(old_keys);
    ns_free(old_values);
    ns_free(old_hashes);
}

void
ns_hash_table_remove_all(NsHashTable *hash_table)
{
    ns_hash_remove_all_nodes(hash_table, 1, 0);
    ns_hash_maybe_resize(hash_table);
}

void
ns_hash_table_steal_all(NsHashTable *hash_table)
{
    ns_hash_remove_all_nodes(hash_table, 0, 0);
    ns_hash_maybe_resize(hash_table);
}

NsHashTable *
ns_hash_table_ref(NsHashTable *hash_table)
{
    ns_atomic_ref_count_inc(&hash_table->ref_count);
    return hash_table;
}

void
ns_hash_table_unref(NsHashTable *hash_table)
{
    if (!hash_table) return;
    if (ns_atomic_ref_count_dec(&hash_table->ref_count)) {
        ns_hash_remove_all_nodes(hash_table, 1, 1);
        ns_free(hash_table->keys);
        ns_free(hash_table->values);
        ns_free(hash_table->hashes);
        ns_free(hash_table);
    }
}

void
ns_hash_table_destroy(NsHashTable *hash_table)
{
    if (!hash_table) return;
    ns_hash_table_remove_all(hash_table);
    ns_hash_table_unref(hash_table);
}

void *
ns_hash_table_lookup(NsHashTable *hash_table, const void *key)
{
    unsigned int hash;
    unsigned int i = ns_hash_lookup_node(hash_table, key, &hash);
    return NS_IS_REAL(hash_table->hashes[i]) ? hash_table->values[i] : NULL;
}

int
ns_hash_table_lookup_extended(NsHashTable *hash_table, const void *lookup_key,
                              void **orig_key, void **value)
{
    unsigned int hash;
    unsigned int i = ns_hash_lookup_node(hash_table, lookup_key, &hash);
    if (!NS_IS_REAL(hash_table->hashes[i])) {
        if (orig_key) *orig_key = NULL;
        if (value) *value = NULL;
        return 0;
    }
    if (orig_key) *orig_key = hash_table->keys[i];
    if (value) *value = hash_table->values[i];
    return 1;
}

int
ns_hash_table_contains(NsHashTable *hash_table, const void *key)
{
    unsigned int hash;
    unsigned int i = ns_hash_lookup_node(hash_table, key, &hash);
    return NS_IS_REAL(hash_table->hashes[i]);
}

unsigned int
ns_hash_table_size(NsHashTable *hash_table)
{
    return hash_table->nnodes;
}

void
ns_hash_table_foreach(NsHashTable *hash_table, NsHFunc func, void *user_data)
{
    for (unsigned int i = 0; i < hash_table->size; i++)
        if (NS_IS_REAL(hash_table->hashes[i]))
            func(hash_table->keys[i], hash_table->values[i], user_data);
}

unsigned int
ns_hash_table_foreach_remove(NsHashTable *hash_table, NsHRFunc func, void *user_data)
{
    unsigned int removed = 0;
    for (unsigned int i = 0; i < hash_table->size; i++) {
        if (NS_IS_REAL(hash_table->hashes[i]) &&
            func(hash_table->keys[i], hash_table->values[i], user_data)) {
            ns_hash_remove_node(hash_table, i, 1);
            removed++;
        }
    }
    ns_hash_maybe_resize(hash_table);
    return removed;
}

/* In the order GLib's give: the last bucket first. */
NsList *
ns_hash_table_get_keys(NsHashTable *hash_table)
{
    NsList *list = NULL;
    for (unsigned int i = 0; i < hash_table->size; i++)
        if (NS_IS_REAL(hash_table->hashes[i]))
            list = ns_list_prepend(list, hash_table->keys[i]);
    return list;
}

NsList *
ns_hash_table_get_values(NsHashTable *hash_table)
{
    NsList *list = NULL;
    for (unsigned int i = 0; i < hash_table->size; i++)
        if (NS_IS_REAL(hash_table->hashes[i]))
            list = ns_list_prepend(list, hash_table->values[i]);
    return list;
}

void
ns_hash_table_iter_init(NsHashTableIter *iter, NsHashTable *hash_table)
{
    iter->table = hash_table;
    iter->position = -1;
    iter->reserved[0] = iter->reserved[1] = NULL;
}

int
ns_hash_table_iter_next(NsHashTableIter *iter, void **key, void **value)
{
    NsHashTable *t = iter->table;
    ptrdiff_t pos = iter->position;
    do {
        pos++;
        if (pos >= (ptrdiff_t)t->size) {
            iter->position = pos;
            return 0;
        }
    } while (!NS_IS_REAL(t->hashes[pos]));
    if (key) *key = t->keys[pos];
    if (value) *value = t->values[pos];
    iter->position = pos;
    return 1;
}

NsHashTable *
ns_hash_table_iter_get_hash_table(NsHashTableIter *iter)
{
    return iter->table;
}

void
ns_hash_table_iter_remove(NsHashTableIter *iter)
{
    ns_hash_remove_node(iter->table, (unsigned int)iter->position, 1);
}

void
ns_hash_table_iter_steal(NsHashTableIter *iter)
{
    ns_hash_remove_node(iter->table, (unsigned int)iter->position, 0);
}

void
ns_hash_table_iter_replace(NsHashTableIter *iter, void *value)
{
    NsHashTable *t = iter->table;
    unsigned int i = (unsigned int)iter->position;
    ns_hash_insert_node(t, i, t->hashes[i], t->keys[i], value, 1, 1);
}

/* ---- hash and equality functions ----------------------------------------- */

int
ns_str_equal(const void *v1, const void *v2)
{
    return strcmp(v1, v2) == 0;
}

/* Bernstein's: h = h * 33 + c over the bytes as signed chars, from 5381. */
unsigned int
ns_str_hash(const void *v)
{
    uint32_t h = 5381;
    for (const signed char *p = v; *p; p++) h = (h << 5) + h + (uint32_t)*p;
    return h;
}

int
ns_direct_equal(const void *v1, const void *v2)
{
    return v1 == v2;
}

unsigned int
ns_direct_hash(const void *v)
{
    return (unsigned int)(uintptr_t)v;
}

int
ns_int_equal(const void *v1, const void *v2)
{
    return *(const int *)v1 == *(const int *)v2;
}

unsigned int
ns_int_hash(const void *v)
{
    return (unsigned int)*(const int *)v;
}

int
ns_int64_equal(const void *v1, const void *v2)
{
    return *(const int64_t *)v1 == *(const int64_t *)v2;
}

unsigned int
ns_int64_hash(const void *v)
{
    uint64_t bits = *(const uint64_t *)v;
    return (unsigned int)((bits >> 32) ^ (bits & 0xffffffffu));
}

int
ns_double_equal(const void *v1, const void *v2)
{
    return *(const double *)v1 == *(const double *)v2;
}

unsigned int
ns_double_hash(const void *v)
{
    uint64_t bits;
    memcpy(&bits, v, sizeof bits);
    return (unsigned int)((bits >> 32) ^ (bits & 0xffffffffu));
}
