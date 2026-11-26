/* Weststar SDL Browser — gfx: a bridge to Cairo for call sites not ported yet.
 *
 * gfx and Cairo share their pixel formats, so Cairo can draw into a gfx
 * target directly. While the engine moves to gfx piece by piece, code that
 * still needs Cairo (text through pangocairo, the SVG renderer) draws with
 * the cairo_t that gfx_cairo_begin hands out: it targets the current gfx
 * target (or group) with gfx's matrix and clip, and its source is opaque
 * black. gfx_cairo_end finishes it; the gfx context must not be used in
 * between. This file goes away with the Cairo dependency.
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

#endif
