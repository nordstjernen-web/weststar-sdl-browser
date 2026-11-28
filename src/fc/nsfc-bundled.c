/* Weststar SDL Browser — nsfc: the fonts compiled into the browser.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * With -Dbundled_fonts the browser carries a few fonts of its own, for a
 * system with none of a kind or none at all; nsfc-scan.c lists them after
 * the system's, under file names that are not paths (NSFC_BUNDLED_PREFIX),
 * and the font map reads their bytes from here instead of from a file.
 */

#include "nsfc-private.h"

#include <string.h>

/* The table, written at configure time by src/fc/meson.build: nothing but
 * the terminating entry without the option. */
#include "nsfc_bundled_fonts.h"

const void *
NsFcBundledFontData(const FcChar8 *file, size_t *len)
{
    const size_t prefix = sizeof NSFC_BUNDLED_PREFIX - 1;
    if (!file || strncmp((const char *)file, NSFC_BUNDLED_PREFIX, prefix) != 0)
        return NULL;
    for (const NsFcBundledFont *f = nsfc_bundled_fonts; f->name; f++) {
        if (strcmp((const char *)file + prefix, f->name) == 0) {
            if (len) *len = f->len;
            return f->data;
        }
    }
    return NULL;
}
