/* Weststar SDL Browser — nscore: memory.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ns-mem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
ns_mem_fatal(const char *what, size_t a, size_t b)
{
    if (b)
        fprintf(stderr, "nscore: %s %llu*%llu bytes\n", what,
                (unsigned long long)a, (unsigned long long)b);
    else
        fprintf(stderr, "nscore: %s %llu bytes\n", what, (unsigned long long)a);
    fflush(stderr);
    abort();
}

void *
ns_malloc(size_t n_bytes)
{
    if (n_bytes == 0) return NULL;
    void *p = malloc(n_bytes);
    if (!p) ns_mem_fatal("failed to allocate", n_bytes, 0);
    return p;
}

void *
ns_malloc0(size_t n_bytes)
{
    if (n_bytes == 0) return NULL;
    void *p = calloc(1, n_bytes);
    if (!p) ns_mem_fatal("failed to allocate", n_bytes, 0);
    return p;
}

void *
ns_realloc(void *mem, size_t n_bytes)
{
    if (n_bytes == 0) {
        free(mem);
        return NULL;
    }
    void *p = realloc(mem, n_bytes);
    if (!p) ns_mem_fatal("failed to allocate", n_bytes, 0);
    return p;
}

static int
ns_mul_overflows(size_t a, size_t b)
{
    return b != 0 && a > SIZE_MAX / b;
}

void *
ns_malloc_n(size_t n_blocks, size_t n_block_bytes)
{
    if (ns_mul_overflows(n_blocks, n_block_bytes))
        ns_mem_fatal("overflow allocating", n_blocks, n_block_bytes);
    return ns_malloc(n_blocks * n_block_bytes);
}

void *
ns_malloc0_n(size_t n_blocks, size_t n_block_bytes)
{
    if (ns_mul_overflows(n_blocks, n_block_bytes))
        ns_mem_fatal("overflow allocating", n_blocks, n_block_bytes);
    return ns_malloc0(n_blocks * n_block_bytes);
}

void *
ns_realloc_n(void *mem, size_t n_blocks, size_t n_block_bytes)
{
    if (ns_mul_overflows(n_blocks, n_block_bytes))
        ns_mem_fatal("overflow allocating", n_blocks, n_block_bytes);
    return ns_realloc(mem, n_blocks * n_block_bytes);
}

void *
ns_try_malloc(size_t n_bytes)
{
    return n_bytes ? malloc(n_bytes) : NULL;
}

void *
ns_try_malloc0(size_t n_bytes)
{
    return n_bytes ? calloc(1, n_bytes) : NULL;
}

void *
ns_try_malloc_n(size_t n_blocks, size_t n_block_bytes)
{
    if (ns_mul_overflows(n_blocks, n_block_bytes)) return NULL;
    return ns_try_malloc(n_blocks * n_block_bytes);
}

void *
ns_try_malloc0_n(size_t n_blocks, size_t n_block_bytes)
{
    if (ns_mul_overflows(n_blocks, n_block_bytes)) return NULL;
    return ns_try_malloc0(n_blocks * n_block_bytes);
}

void
ns_free(void *mem)
{
    free(mem);
}

void *
ns_memdup2(const void *mem, size_t byte_size)
{
    if (!mem || byte_size == 0) return NULL;
    void *p = ns_malloc(byte_size);
    memcpy(p, mem, byte_size);
    return p;
}

void
ns_atomic_ref_count_init(int *arc)
{
    __atomic_store_n(arc, 1, __ATOMIC_RELEASE);
}

void
ns_atomic_ref_count_inc(int *arc)
{
    __atomic_add_fetch(arc, 1, __ATOMIC_ACQ_REL);
}

int
ns_atomic_ref_count_dec(int *arc)
{
    return __atomic_sub_fetch(arc, 1, __ATOMIC_ACQ_REL) == 0;
}
