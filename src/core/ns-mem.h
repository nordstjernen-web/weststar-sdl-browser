/* Weststar SDL Browser — nscore: memory.
 *
 * As GLib's: an allocation that fails aborts the program (the ns_try_
 * forms return NULL instead), a request for 0 bytes gives NULL, and the
 * _n forms check the multiplication for overflow.
 *
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_MEM_H
#define NS_MEM_H

#include "ns-base.h"

NS_BEGIN_DECLS

void *ns_malloc(size_t n_bytes) NS_MALLOC_ATTR;
void *ns_malloc0(size_t n_bytes) NS_MALLOC_ATTR;
void *ns_realloc(void *mem, size_t n_bytes) NS_WARN_UNUSED;
void *ns_malloc_n(size_t n_blocks, size_t n_block_bytes) NS_MALLOC_ATTR;
void *ns_malloc0_n(size_t n_blocks, size_t n_block_bytes) NS_MALLOC_ATTR;
void *ns_realloc_n(void *mem, size_t n_blocks, size_t n_block_bytes) NS_WARN_UNUSED;
void *ns_try_malloc(size_t n_bytes) NS_MALLOC_ATTR;
void *ns_try_malloc0(size_t n_bytes) NS_MALLOC_ATTR;
void *ns_try_malloc_n(size_t n_blocks, size_t n_block_bytes) NS_MALLOC_ATTR;
void *ns_try_malloc0_n(size_t n_blocks, size_t n_block_bytes) NS_MALLOC_ATTR;
void ns_free(void *mem);
void *ns_memdup2(const void *mem, size_t byte_size) NS_MALLOC_ATTR;

/* Reference counts that are changed from several threads (gatomicrefcount). */
void ns_atomic_ref_count_init(int *arc);
void ns_atomic_ref_count_inc(int *arc);
int ns_atomic_ref_count_dec(int *arc);   /* true when it reached zero */

/* Prints "nscore: <message>" to stderr and aborts. */
NS_NORETURN void ns_mem_fatal(const char *what, size_t a, size_t b);

NS_END_DECLS

#endif
