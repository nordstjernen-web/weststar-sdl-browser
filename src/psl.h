/* Weststar SDL Browser — the Public Suffix List, compiled in.
 * The two questions the engine asks of the list (is a name a public
 * suffix, and what is a host's registrable domain), answered as libpsl's
 * psl_is_public_suffix() and psl_registrable_domain() answer them over
 * psl_builtin(): both sections of the list (ICANN and private), wildcard
 * and exception rules, the implicit "*" rule for a single label. Names are
 * matched byte for byte, as libpsl's built-in list matches them: callers
 * pass them lowercased; a non-ASCII name matches the rules written in
 * UTF-8, an A-label (xn--) one their punycode forms, which the table holds
 * as well. The table is src/psl-table.h, made by scripts/gen-psl-table.py
 * from the list's pinned version.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef NS_PSL_H
#define NS_PSL_H

#include <stdbool.h>
#include <stddef.h>

/* True if `domain` is a public suffix ("com", "co.uk", "foo.ck", any single
 * label); true for NULL, as libpsl says. A trailing dot is ignored. */
bool ns_psl_is_public_suffix(const char *domain);

/* The registrable domain of `domain` (the public suffix and one label
 * before it), as a pointer into `domain`; NULL if it has none (it is a
 * public suffix itself, starts with a dot, or is NULL). */
const char *ns_psl_registrable_domain(const char *domain);

/* The list's version: its commit and date. */
const char *ns_psl_version(void);

/* For the tests: calls `fn` for every entry of the table, with the rule's
 * name and its flags (NS_PSL_EXCEPTION, NS_PSL_WILDCARD); returns the count. */
enum { NS_PSL_EXCEPTION = 1, NS_PSL_WILDCARD = 2 };
size_t ns_psl_foreach(void (*fn)(const char *name, int flags, void *data),
                      void *data);

#endif
