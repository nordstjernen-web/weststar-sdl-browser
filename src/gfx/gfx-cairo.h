/* Weststar SDL Browser — gfx: a bridge to Cairo for call sites not ported yet.
 *
 * gfx and Cairo share their pixel formats, so Cairo can draw into a gfx
 * target directly. While the engine moves to gfx piece by piece, code that
 * still needs Cairo (text through pangocairo) draws with the cairo_t that
 * gfx_cairo_begin hands out: it targets the current gfx target (or group)
 * with gfx's matrix and clip, and gfx's source when that is a solid colour
 * (else opaque black). gfx_cairo_end finishes it; the gfx context must not
 * be used in between. This file goes away with the Cairo dependency.
 *
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_GFX_CAIRO_H
#define NS_GFX_CAIRO_H

#include "gfx.h"

#include <cairo.h>

cairo_t *gfx_cairo_begin(gfx_t *g);
void     gfx_cairo_end(gfx_t *g, cairo_t *cr);

/* A surface sharing a gfx surface's pixels (it must outlive the result). */
cairo_surface_t *gfx_cairo_surface_wrap(gfx_surface_t *s);

/* The other way: a gfx context drawing into what a cairo_t targets, for
 * code moved to gfx that is still called with a cairo_t. It has the
 * cairo_t's matrix and clip, and draws straight into an image target's
 * pixels; a clip that is not a set of pixel rectangles is applied by
 * drawing into a Cairo group. A vector target (PDF) gets an image drawn
 * at twice its resolution. gfx_from_cairo_end() puts the result in place
 * and frees the context; cr must not be used in between. */
typedef struct gfx_from_cairo gfx_from_cairo;

/* Appends a path Cairo made (user space) to g's path. */
void gfx_append_cairo_path(gfx_t *g, const cairo_path_t *path);

gfx_t *gfx_from_cairo_begin(cairo_t *cr, gfx_from_cairo **state);
void   gfx_from_cairo_end(cairo_t *cr, gfx_t *g, gfx_from_cairo *state);

#endif
