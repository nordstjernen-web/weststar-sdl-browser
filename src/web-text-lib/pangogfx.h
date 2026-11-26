/* Pango
 * pangogfx.h: drawing Pango text with gfx, the browser's rasterizer
 *
 * Replaces pangocairo. Fonts come from fontconfig (a NsPangoFcFontMap
 * subclass), are shaped by HarfBuzz as before and are rasterized with
 * FreeType; gfx composites the glyphs in its current source and operator.
 *
 * Copyright (C) 1999, 2004 Red Hat, Inc.
 * Copyright 2026 Andreas Røsdal
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef __NS_PANGOGFX_H__
#define __NS_PANGOGFX_H__

#include <web-text-lib/pango.h>
#include <gfx.h>

G_BEGIN_DECLS

/* The font map of the calling thread, made on first use. Each thread has
 * its own, as with pangocairo: fonts and their glyph caches belong to it. */
NsPangoFontMap *ns_pango_gfx_font_map_get_default (void);

/* A context on the default font map. With a gfx context, the context's
 * matrix is gfx's current one (without translation), so text is laid out
 * (hinted) for that scale; with NULL it is the identity. */
NsPangoContext *ns_pango_gfx_create_context (gfx_t *g);
void            ns_pango_gfx_update_context (gfx_t          *g,
                                             NsPangoContext *context);
NsPangoLayout  *ns_pango_gfx_create_layout  (gfx_t *g);

/* Draw a layout (its top-left corner) or a line (its baseline's left end)
 * at gfx's current point, or at the origin when there is none, in gfx's
 * current source and operator. Glyphs are rasterized at the device's
 * resolution when gfx's matrix is a translation and a uniform scale, and
 * filled as outlines otherwise. The current path is replaced; the current
 * point is kept. */
void ns_pango_gfx_show_layout      (gfx_t             *g,
                                    NsPangoLayout     *layout);
void ns_pango_gfx_show_layout_line (gfx_t             *g,
                                    NsPangoLayoutLine *line);

/* Appends the layout's glyph outlines (and its underlines and boxes) to
 * gfx's current path, the layout's top-left at the current point. */
void ns_pango_gfx_layout_path      (gfx_t             *g,
                                    NsPangoLayout     *layout);

G_END_DECLS

#endif /* __NS_PANGOGFX_H__ */
