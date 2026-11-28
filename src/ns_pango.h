/* Weststar SDL Browser — the text layer the engine talks to.
 * The engine reaches text shaping only through here: either web-text-lib,
 * the in-tree Pango fork (meson -Dtext=pango, the default), or nstext,
 * src/text/, the same API without Pango or GObject (-Dtext=native).
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_PANGO_H
#define NS_PANGO_H

#ifdef NS_TEXT_NATIVE

#include <text/nstext.h>

#else

#include <web-text-lib/pangogfx.h>
#include <web-text-lib/pangofc-fontmap.h>
#include <web-text-lib/ns-pango-cache.h>

/* Layouts are GObjects in the fork and plain reference-counted objects in
 * nstext: the engine holds and drops them with these. */
static inline NsPangoLayout *
ns_pango_layout_ref (NsPangoLayout *layout)
{
  return (NsPangoLayout *) g_object_ref (layout);
}

static inline void
ns_pango_layout_unref (NsPangoLayout *layout)
{
  g_object_unref (layout);
}

#endif

#endif
