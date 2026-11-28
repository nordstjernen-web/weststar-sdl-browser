/* Weststar SDL Browser — the Public Suffix List, compiled in.
 * The lookups follow libpsl 0.23's is_public_suffix() and
 * psl_registrable_domain() step by step, quirks included, so that the
 * answers are libpsl's (src/psl-test.c compares the two).
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "psl.h"

#include <string.h>

#include "psl-table.h"

#define NS_PSL_BLOCKS (sizeof(ns_psl_index) / sizeof(ns_psl_index[0]))

_Static_assert(NS_PSL_FLAG_EXCEPTION == NS_PSL_EXCEPTION &&
               NS_PSL_FLAG_WILDCARD == NS_PSL_WILDCARD,
               "psl-table.h flags");

/* Orders two byte strings as the generator sorted them. */
static int
psl_compare(const unsigned char *a, size_t alen,
            const unsigned char *b, size_t blen)
{
    int c = memcmp(a, b, alen < blen ? alen : blen);
    if (c) return c;
    return alen < blen ? -1 : alen > blen ? 1 : 0;
}

/* The flags of the entry for name[0..len), or -1 if there is none. */
static int
psl_lookup(const char *name, size_t len)
{
    if (len == 0 || len > NS_PSL_MAX_LEN) return -1;
    unsigned char key[NS_PSL_MAX_LEN];
    for (size_t i = 0; i < len; i++)
        key[i] = (unsigned char)name[len - 1 - i];

    /* The last block whose first entry is not after the key. */
    size_t lo = 0, hi = NS_PSL_BLOCKS;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        const unsigned char *e = ns_psl_blob + ns_psl_index[mid];
        if (psl_compare(e + 2, e[1], key, len) <= 0) lo = mid + 1;
        else hi = mid;
    }
    if (lo == 0) return -1;

    size_t block = lo - 1;
    const unsigned char *p = ns_psl_blob + ns_psl_index[block];
    const unsigned char *end = block + 1 < NS_PSL_BLOCKS
        ? ns_psl_blob + ns_psl_index[block + 1]
        : ns_psl_blob + sizeof(ns_psl_blob);
    unsigned char cur[64];
    while (p < end) {
        size_t shared = p[0] & 63u, rest = p[1];
        int flags = p[0] >> 6;
        memcpy(cur + shared, p + 2, rest);
        p += 2 + rest;
        int c = psl_compare(cur, shared + rest, key, len);
        if (c == 0) return flags;
        if (c > 0) break;
    }
    return -1;
}

/* libpsl's is_public_suffix() for type 0 or PSL_TYPE_ANY, the built-in list. */
static bool
psl_is_public_suffix(const char *domain)
{
    /* Called without leading dots; one is skipped all the same. */
    if (*domain == '.') domain++;
    /* A trailing dot is not looked up: co.uk and co.uk. are both public
     * suffixes. (libpsl's second lookup, below, keeps it.) */
    size_t len = strlen(domain);
    if (len > 0 && domain[len - 1] == '.') len--;

    unsigned nlabels = 1;
    for (size_t i = 0; i < len; i++) {
        if (domain[i] == '.') {
            if (nlabels == 255) return false;
            nlabels++;
        }
    }
    /* A single label: the prevailing "*" rule. */
    if (nlabels == 1) return true;

    int flags = psl_lookup(domain, len);
    if (flags >= 0) return !(flags & NS_PSL_EXCEPTION);

    /* *.foo.bar makes anything.foo.bar a public suffix. */
    const char *parent = strchr(domain, '.');
    if (parent) {
        parent++;
        flags = psl_lookup(parent, strlen(parent));
        if (flags >= 0 && (flags & NS_PSL_WILDCARD)) return true;
    }
    return false;
}

bool
ns_psl_is_public_suffix(const char *domain)
{
    if (!domain) return true;
    return psl_is_public_suffix(domain);
}

const char *
ns_psl_registrable_domain(const char *domain)
{
    if (!domain || *domain == '.') return NULL;

    /* As libpsl does, look at no more than the last nine labels. */
    int nlabels = 0;
    for (const char *p = domain + strlen(domain); p > domain;) {
        p--;
        if (*p == '.' && ++nlabels > 8) {
            domain = p + 1;
            break;
        }
    }

    /* From the left, so that a rule like forgot.his.name (with name, and
     * not his.name, a public suffix) is found. */
    const char *regdom = NULL;
    while (!psl_is_public_suffix(domain)) {
        const char *dot = strchr(domain, '.');
        if (!dot) break;
        regdom = domain;
        domain = dot + 1;
    }
    return regdom;
}

const char *
ns_psl_version(void)
{
    return NS_PSL_COMMIT " (" NS_PSL_DATE ")";
}

size_t
ns_psl_foreach(void (*fn)(const char *name, int flags, void *data), void *data)
{
    size_t count = 0;
    for (size_t b = 0; b < NS_PSL_BLOCKS; b++) {
        const unsigned char *p = ns_psl_blob + ns_psl_index[b];
        const unsigned char *end = b + 1 < NS_PSL_BLOCKS
            ? ns_psl_blob + ns_psl_index[b + 1]
            : ns_psl_blob + sizeof(ns_psl_blob);
        unsigned char cur[64];
        while (p < end) {
            size_t shared = p[0] & 63u, rest = p[1], len = shared + rest;
            int flags = p[0] >> 6;
            memcpy(cur + shared, p + 2, rest);
            p += 2 + rest;
            char name[65];
            for (size_t i = 0; i < len; i++)
                name[i] = (char)cur[len - 1 - i];
            name[len] = '\0';
            fn(name, flags, data);
            count++;
        }
    }
    return count;
}
