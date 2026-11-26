/* Pango
 * pangogfx-private.h: the gfx backend's font map, fonts and renderer
 *
 * Copyright (C) 2000-2005 Red Hat Software
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

#ifndef __NS_PANGOGFX_PRIVATE_H__
#define __NS_PANGOGFX_PRIVATE_H__

/* The fork's private headers are written for its own (lax) warning
 * flags; this backend is built with the browser's. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wundef"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wcast-align"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#include "pangogfx.h"
#include "pangofc-fontmap-private.h"
#include "pangofc-private.h"
#include "pango-font-private.h"
#include "pango-impl-utils.h"
#include "pango-renderer.h"
#include <hb.h>
#include <hb-ot.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include FT_SIZES_H
#include FT_SYNTHESIS_H
#include FT_MULTIPLE_MASTERS_H
#pragma GCC diagnostic pop

G_BEGIN_DECLS

/* ---- font map ---------------------------------------------------------- */

#define NS_TYPE_PANGO_GFX_FONT_MAP (ns_pango_gfx_font_map_get_type ())
#define NS_PANGO_GFX_FONT_MAP(o) \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), NS_TYPE_PANGO_GFX_FONT_MAP, NsPangoGfxFontMap))
#define NS_PANGO_IS_GFX_FONT_MAP(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((o), NS_TYPE_PANGO_GFX_FONT_MAP))

typedef struct _NsPangoGfxFontMap NsPangoGfxFontMap;

struct _NsPangoGfxFontMap
{
  NsPangoFcFontMap parent_instance;

  guint  serial;
  double dpi;
};

GType ns_pango_gfx_font_map_get_type (void) G_GNUC_CONST;

/* ---- fonts ------------------------------------------------------------- */

#define NS_TYPE_PANGO_GFX_FONT (ns_pango_gfx_font_get_type ())
#define NS_PANGO_GFX_FONT(o) \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), NS_TYPE_PANGO_GFX_FONT, NsPangoGfxFont))
#define NS_PANGO_IS_GFX_FONT(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((o), NS_TYPE_PANGO_GFX_FONT))

typedef struct _NsPangoGfxFont NsPangoGfxFont;

GType ns_pango_gfx_font_get_type (void) G_GNUC_CONST;

NsPangoFcFont *_ns_pango_gfx_font_new (NsPangoGfxFontMap *fontmap,
                                       NsPangoFcFontKey  *key);

/* A rasterized glyph: its pixels (A8 coverage, or premultiplied ARGB32 for
 * a colour glyph) and where its top-left pixel lies relative to the pixel
 * holding the glyph origin. A bitmap strike drawn at another size carries
 * the scale to draw it with. */
typedef struct
{
  gfx_surface_t *surface; /* referenced; NULL for a blank glyph */
  int            x, y;
  gboolean       color;
  double         scale;
} NsPangoGfxGlyphImage;

/* Whether glyphs drawn with this user-to-device matrix can be rasterized
 * as bitmaps (the matrix is a translation and a uniform scale once the
 * font's own matrix is applied), and the device pixel size to use. */
gboolean _ns_pango_gfx_font_device_size (NsPangoGfxFont     *font,
                                         const gfx_matrix_t *user_to_device,
                                         double             *device_size);

/* The glyph rasterized at a device pixel size with its origin moved right
 * and down by xphase / 4 and yphase / 4 of a pixel. FALSE when FreeType
 * cannot render it. */
gboolean _ns_pango_gfx_font_get_glyph_image (NsPangoGfxFont       *font,
                                             NsPangoGlyph          glyph,
                                             double                device_size,
                                             int                   xphase,
                                             int                   yphase,
                                             NsPangoGfxGlyphImage *image);

/* Appends the glyph's outline, its origin at (x, y) in user space. */
void _ns_pango_gfx_font_append_glyph_path (NsPangoGfxFont *font,
                                           gfx_t          *g,
                                           NsPangoGlyph    glyph,
                                           double          x,
                                           double          y);

/* The hex box layout for unknown glyphs, as pangocairo draws them: digits
 * from a small monospace font inside a frame. */
typedef struct
{
  NsPangoFont *font;     /* the mini font for the digits */
  int          rows;
  double       digit_width;
  double       digit_height;
  double       pad_x;
  double       pad_y;
  double       line_width;
  double       box_descent;
  double       box_height;
} NsPangoGfxHexBoxInfo;

NsPangoGfxHexBoxInfo *_ns_pango_gfx_font_get_hex_box_info (NsPangoGfxFont *font);

/* From pangofc-fontmap.c. */
gpointer get_gravity_class (void);

G_END_DECLS

#endif /* __NS_PANGOGFX_PRIVATE_H__ */
