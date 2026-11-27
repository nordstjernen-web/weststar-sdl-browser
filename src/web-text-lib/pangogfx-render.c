/* Pango
 * pangogfx-render.c: drawing Pango layouts with gfx
 *
 * Derived from pangocairo-render.c.
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

#include "config.h"

#include <math.h>
#include <string.h>

#include "pangogfx-private.h"

/* Threads, locks and queues from the SDL layer; see the header. */
#include "ns-glib-threads.h"

#define NS_TYPE_PANGO_GFX_RENDERER (ns_pango_gfx_renderer_get_type ())

typedef struct
{
  NsPangoRenderer parent_instance;

  gfx_t   *g;
  gboolean do_path;
  double   x_offset, y_offset;
} NsPangoGfxRenderer;

typedef struct
{
  NsPangoRendererClass parent_class;
} NsPangoGfxRendererClass;

static GType ns_pango_gfx_renderer_get_type (void) G_GNUC_CONST;

G_DEFINE_TYPE (NsPangoGfxRenderer, ns_pango_gfx_renderer, NS_TYPE_PANGO_RENDERER)

/* The source for a part: its colour attribute when it has one, else gfx's
 * current source (with the part's alpha, when that is set, on a solid
 * one). */
static void
set_color (NsPangoGfxRenderer *grenderer,
           NsPangoRenderPart   part)
{
  NsPangoRenderer *renderer = (NsPangoRenderer *) grenderer;
  NsPangoColor *color = ns_pango_renderer_get_color (renderer, part);
  guint16 a = ns_pango_renderer_get_alpha (renderer, part);
  double red = 0., green = 0., blue = 0., alpha = 1.;

  if (!a && !color)
    return;

  if (color)
    {
      red = color->red / 65535.;
      green = color->green / 65535.;
      blue = color->blue / 65535.;
    }
  else if (!gfx_pattern_get_rgba (gfx_get_source (grenderer->g),
                                  &red, &green, &blue, &alpha))
    {
      red = green = blue = 0.;
      alpha = 1.;
    }

  if (a)
    alpha = a / 65535.;

  gfx_set_source_rgba (grenderer->g, red, green, blue, alpha);
}

/* ---- glyphs ------------------------------------------------------------ */

typedef struct
{
  NsPangoGlyph glyph;
  double       x, y; /* the origin in user space */
} PlacedGlyph;

/* User space to the target's pixels. */
static void
user_to_pixels (gfx_t        *g,
                gfx_matrix_t *m,
                gfx_matrix_t *pixels_to_device)
{
  gfx_surface_t *target = gfx_get_group_target (g);
  double sx = 1, sy = 1, ox = 0, oy = 0;
  gfx_matrix_t dev;

  gfx_surface_get_device_scale (target, &sx, &sy);
  gfx_surface_get_device_offset (target, &ox, &oy);
  gfx_matrix_init (&dev, sx, 0, 0, sy, ox, oy);
  gfx_get_matrix (g, m);
  gfx_matrix_multiply (m, m, &dev);

  /* The CTM under which user space is the target's pixels. */
  *pixels_to_device = dev;
  if (gfx_matrix_invert (pixels_to_device) != GFX_STATUS_SUCCESS)
    gfx_matrix_init_identity (pixels_to_device);
}

static gboolean
operator_is_bounded (gfx_operator_t op)
{
  switch (op)
    {
    case GFX_OPERATOR_IN:
    case GFX_OPERATOR_OUT:
    case GFX_OPERATOR_DEST_IN:
    case GFX_OPERATOR_DEST_ATOP:
      return FALSE;
    default:
      return TRUE;
    }
}

/* Where a glyph lands: the pixel and quarter-pixel phase, as cairo's image
 * compositor rounds glyph positions. */
static void
glyph_position (double  v,
                int    *pixel,
                int    *phase)
{
  double p = floor (v + 0.125);

  *pixel = (int) p;
  *phase = (int) (floor (4 * (v + 0.125)) - 4 * p) & 3;
}

/* Adds a coverage image into an accumulated A8 mask. */
static void
accumulate (gfx_surface_t       *acc,
            int                  ax,
            int                  ay,
            const NsPangoGfxGlyphImage *image,
            int                  x,
            int                  y)
{
  gfx_surface_t *s = image->surface;
  int w = gfx_image_surface_get_width (s), h = gfx_image_surface_get_height (s);
  int aw = gfx_image_surface_get_width (acc), ah = gfx_image_surface_get_height (acc);
  int as = gfx_image_surface_get_stride (acc), ss = gfx_image_surface_get_stride (s);
  unsigned char *ad = gfx_image_surface_get_data (acc);
  const unsigned char *sd = gfx_image_surface_get_data (s);
  int i, j;

  for (j = 0; j < h; j++)
    {
      int ty = y + j - ay;

      if (ty < 0 || ty >= ah)
        continue;
      for (i = 0; i < w; i++)
        {
          int tx = x + i - ax;
          unsigned v;

          if (tx < 0 || tx >= aw)
            continue;
          if (image->color)
            {
              guint32 px;

              memcpy (&px, sd + (size_t) j * (size_t) ss + 4 * (size_t) i, 4);
              v = px >> 24;
            }
          else
            v = sd[(size_t) j * (size_t) ss + (size_t) i];
          v += ad[(size_t) ty * (size_t) as + (size_t) tx];
          ad[(size_t) ty * (size_t) as + (size_t) tx] = (unsigned char) MIN (v, 255u);
        }
    }
}

/* Composites one rasterized glyph at a pixel position, in gfx's source;
 * a colour glyph is painted as it is. gfx's CTM is the pixel one. */
static void
composite_image (gfx_t                      *g,
                 const NsPangoGfxGlyphImage *image,
                 int                         x,
                 int                         y,
                 double                      alpha)
{
  gfx_pattern_t *pattern;
  gfx_matrix_t m;

  if (image->scale == 1.0)
    {
      if (!image->color)
        {
          gfx_mask_surface (g, image->surface, x + image->x, y + image->y);
          return;
        }
      gfx_matrix_init_translate (&m, - (x + image->x), - (y + image->y));
    }
  else
    {
      /* A bitmap strike drawn at another size: pixel u is texel
       * (u - x) / scale - image->x (each call applies before the last). */
      gfx_matrix_init_translate (&m, - image->x, - image->y);
      gfx_matrix_scale (&m, 1 / image->scale, 1 / image->scale);
      gfx_matrix_translate (&m, - x, - y);
    }

  pattern = gfx_pattern_create_for_surface (image->surface);
  gfx_pattern_set_matrix (pattern, &m);
  if (image->scale != 1.0)
    gfx_pattern_set_filter (pattern, GFX_FILTER_GOOD);
  if (image->color)
    {
      gfx_save (g);
      gfx_set_source (g, pattern);
      gfx_paint_with_alpha (g, alpha);
      gfx_restore (g);
    }
  else
    gfx_mask (g, pattern);
  gfx_pattern_destroy (pattern);
}

static void draw_unknown_glyph (NsPangoGfxRenderer *grenderer,
                                NsPangoFont        *font,
                                NsPangoGlyphInfo   *gi,
                                double              cx,
                                double              cy);

/* Draws (or, for a path, appends) glyphs of one font: as cached bitmaps at
 * the device's resolution when the CTM allows, else as filled outlines.
 * The caller has set the source. */
static void
draw_placed_glyphs (NsPangoGfxRenderer *grenderer,
                    NsPangoGfxFont     *font,
                    const PlacedGlyph  *glyphs,
                    int                 n)
{
  gfx_t *g = grenderer->g;
  gfx_matrix_t u2p, pixels, user;
  gfx_surface_t *acc = NULL;
  gfx_rectangle_int_t box = { 0, 0, 0, 0 };
  double device_size, alpha = 1.;
  int i;

  if (n <= 0)
    return;

  if (grenderer->do_path)
    {
      for (i = 0; i < n; i++)
        _ns_pango_gfx_font_append_glyph_path (font, g, glyphs[i].glyph,
                                              glyphs[i].x, glyphs[i].y);
      return;
    }

  user_to_pixels (g, &u2p, &pixels);

  if (!_ns_pango_gfx_font_device_size (font, &u2p, &device_size))
    {
      gfx_new_path (g);
      for (i = 0; i < n; i++)
        _ns_pango_gfx_font_append_glyph_path (font, g, glyphs[i].glyph,
                                              glyphs[i].x, glyphs[i].y);
      gfx_set_fill_rule (g, GFX_FILL_RULE_WINDING);
      gfx_fill (g);
      return;
    }

  if (!operator_is_bounded (gfx_get_operator (g)))
    {
      /* Everything the operator reaches outside the glyphs changes too:
       * draw them as one mask, as cairo shows one run of glyphs. */
      if (!gfx_clip_pixel_box (g, &box, NULL))
        return;
      acc = gfx_image_surface_create (GFX_FORMAT_A8, box.width, box.height);
      if (gfx_surface_status (acc) != GFX_STATUS_SUCCESS)
        {
          gfx_surface_destroy (acc);
          return;
        }
    }

  gfx_pattern_get_rgba (gfx_get_source (g), NULL, NULL, NULL, &alpha);

  gfx_get_matrix (g, &user);
  gfx_set_matrix (g, &pixels);

  for (i = 0; i < n; i++)
    {
      NsPangoGfxGlyphImage image;
      double dx = glyphs[i].x, dy = glyphs[i].y;
      int px, py, xphase, yphase;

      gfx_matrix_transform_point (&u2p, &dx, &dy);
      if (!(fabs (dx) < 1e7 && fabs (dy) < 1e7))
        continue;
      glyph_position (dx, &px, &xphase);
      glyph_position (dy, &py, &yphase);

      if (!_ns_pango_gfx_font_get_glyph_image (font, glyphs[i].glyph,
                                               device_size, xphase, yphase,
                                               &image))
        {
          /* FreeType could not render it: fill the outline. */
          gfx_set_matrix (g, &user);
          gfx_new_path (g);
          _ns_pango_gfx_font_append_glyph_path (font, g, glyphs[i].glyph,
                                                glyphs[i].x, glyphs[i].y);
          gfx_fill (g);
          gfx_set_matrix (g, &pixels);
          continue;
        }
      if (!image.surface)
        continue;

      if (acc && image.scale == 1.0)
        accumulate (acc, box.x, box.y, &image, px + image.x, py + image.y);
      else
        composite_image (g, &image, px, py, alpha);

      gfx_surface_destroy (image.surface);
    }

  if (acc)
    {
      gfx_mask_surface (g, acc, box.x, box.y);
      gfx_surface_destroy (acc);
    }

  gfx_set_matrix (g, &user);
}

#define STACK_GLYPHS 256

static void
ns_pango_gfx_renderer_draw_glyphs (NsPangoRenderer    *renderer,
                                   NsPangoFont        *font,
                                   NsPangoGlyphString *glyphs,
                                   int                 x,
                                   int                 y)
{
  NsPangoGfxRenderer *grenderer = (NsPangoGfxRenderer *) renderer;
  NsPangoRenderComponent components = ns_pango_renderer_get_components (renderer);
  double base_x = grenderer->x_offset + (double) x / NS_PANGO_SCALE;
  double base_y = grenderer->y_offset + (double) y / NS_PANGO_SCALE;
  PlacedGlyph stack[STACK_GLYPHS];
  PlacedGlyph *placed = stack;
  int x_position = 0;
  int i, n = 0;
  gboolean usable = font && NS_PANGO_IS_GFX_FONT (font);

  gfx_save (grenderer->g);
  if (!grenderer->do_path)
    set_color (grenderer, NS_PANGO_RENDER_PART_FOREGROUND);

  if (glyphs->num_glyphs > STACK_GLYPHS)
    placed = g_new (PlacedGlyph, glyphs->num_glyphs);

  for (i = 0; i < glyphs->num_glyphs; i++)
    {
      NsPangoGlyphInfo *gi = &glyphs->glyphs[i];
      NsPangoRenderComponent kind = gi->attr.is_color ? NS_PANGO_RENDER_COMPONENT_COLOR_GLYPH
                                                      : NS_PANGO_RENDER_COMPONENT_PLAIN_GLYPH;

      if ((components & kind) != 0 && gi->glyph != NS_PANGO_GLYPH_EMPTY)
        {
          double cx = base_x + (double) (x_position + gi->geometry.x_offset) / NS_PANGO_SCALE;
          double cy = gi->geometry.y_offset == 0 ?
                      base_y :
                      base_y + (double) gi->geometry.y_offset / NS_PANGO_SCALE;

          if ((gi->glyph & NS_PANGO_GLYPH_UNKNOWN_FLAG) || !usable)
            {
              /* Keep the order of what has been gathered so far. */
              if (usable)
                draw_placed_glyphs (grenderer, (NsPangoGfxFont *) font, placed, n);
              n = 0;
              if (gi->glyph != (0x20 | NS_PANGO_GLYPH_UNKNOWN_FLAG))
                draw_unknown_glyph (grenderer, font, gi, cx, cy);
            }
          else
            {
              placed[n].glyph = gi->glyph;
              placed[n].x = cx;
              placed[n].y = cy;
              n++;
            }
        }
      x_position += gi->geometry.width;
    }

  if (usable)
    draw_placed_glyphs (grenderer, (NsPangoGfxFont *) font, placed, n);

  if (placed != stack)
    g_free (placed);

  gfx_restore (grenderer->g);
}

/* ---- unknown glyphs ---------------------------------------------------- */

/* Modifies the gfx state without saving it; the caller does. */
static void
draw_frame (NsPangoGfxRenderer *grenderer,
            double              x,
            double              y,
            double              width,
            double              height,
            double              line_width,
            gboolean            invalid)
{
  gfx_t *g = grenderer->g;

  if (grenderer->do_path)
    {
      double d2 = line_width * .5, d = line_width;

      /* An outer box in one winding direction and an inner one in the
       * other, which works for both fill rules. */
      gfx_rectangle (g, x - d2, y - d2, width + d, height + d);

      if (invalid)
        {
          double alpha, tan_alpha2, cos_alpha;
          double sx, sy;

          alpha = atan2 (height, width);

          tan_alpha2 = tan (alpha * .5);
          if (tan_alpha2 < 1e-5 || (sx = d2 / tan_alpha2, 2. * sx > width - d))
            sx = (width - d) * .5;

          cos_alpha = cos (alpha);
          if (cos_alpha < 1e-5 || (sy = d2 / cos_alpha, 2. * sy > height - d))
            sy = (height - d) * .5;

          /* top triangle */
          gfx_new_sub_path (g);
          gfx_line_to (g, x + width - sx, y + d2);
          gfx_line_to (g, x + sx, y + d2);
          gfx_line_to (g, x + .5 * width, y + .5 * height - sy);
          gfx_close_path (g);

          /* bottom triangle */
          gfx_new_sub_path (g);
          gfx_line_to (g, x + width - sx, y + height - d2);
          gfx_line_to (g, x + .5 * width, y + .5 * height + sy);
          gfx_line_to (g, x + sx, y + height - d2);
          gfx_close_path (g);

          alpha = G_PI_2 - alpha;
          tan_alpha2 = tan (alpha * .5);
          if (tan_alpha2 < 1e-5 || (sy = d2 / tan_alpha2, 2. * sy > height - d))
            sy = (height - d) * .5;

          cos_alpha = cos (alpha);
          if (cos_alpha < 1e-5 || (sx = d2 / cos_alpha, 2. * sx > width - d))
            sx = (width - d) * .5;

          /* left triangle */
          gfx_new_sub_path (g);
          gfx_line_to (g, x + d2, y + sy);
          gfx_line_to (g, x + d2, y + height - sy);
          gfx_line_to (g, x + .5 * width - sx, y + .5 * height);
          gfx_close_path (g);

          /* right triangle */
          gfx_new_sub_path (g);
          gfx_line_to (g, x + width - d2, y + sy);
          gfx_line_to (g, x + .5 * width + sx, y + .5 * height);
          gfx_line_to (g, x + width - d2, y + height - sy);
          gfx_close_path (g);
        }
      else
        gfx_rectangle (g, x + width - d2, y + d2, - (width - d), height - d);
    }
  else
    {
      gfx_new_path (g);
      gfx_rectangle (g, x, y, width, height);

      if (invalid)
        {
          /* an X */
          gfx_new_sub_path (g);
          gfx_move_to (g, x, y);
          gfx_rel_line_to (g, width, height);

          gfx_new_sub_path (g);
          gfx_move_to (g, x + width, y);
          gfx_rel_line_to (g, -width, height);

          gfx_set_line_cap (g, GFX_LINE_CAP_BUTT);
        }

      gfx_set_line_width (g, line_width);
      gfx_set_line_join (g, GFX_LINE_JOIN_MITER);
      gfx_set_miter_limit (g, 2.);
      gfx_stroke (g);
    }
}

static void
fill_or_keep (NsPangoGfxRenderer *grenderer)
{
  if (!grenderer->do_path)
    gfx_fill (grenderer->g);
}

static void
draw_unknown_glyph (NsPangoGfxRenderer *grenderer,
                    NsPangoFont        *font,
                    NsPangoGlyphInfo   *gi,
                    double              cx,
                    double              cy)
{
  gfx_t *g = grenderer->g;
  NsPangoGfxHexBoxInfo *hbi = NULL;
  char buf[7];
  const char *name = NULL;
  const char *p;
  gunichar ch;
  gboolean invalid_input;
  double x0, y0, width, lsb;
  int row, col, rows = 0, cols = 0;

  gfx_save (g);
  if (!grenderer->do_path)
    gfx_new_path (g);

  ch = gi->glyph & ~NS_PANGO_GLYPH_UNKNOWN_FLAG;
  invalid_input = G_UNLIKELY (gi->glyph == NS_PANGO_GLYPH_INVALID_INPUT || ch > 0x10FFFF);

  if (font && NS_PANGO_IS_GFX_FONT (font))
    hbi = _ns_pango_gfx_font_get_hex_box_info ((NsPangoGfxFont *) font);

  if (!hbi || !NS_PANGO_IS_GFX_FONT (hbi->font))
    {
      draw_frame (grenderer,
                  cx + 1.5,
                  cy + 1.5 - NS_PANGO_UNKNOWN_GLYPH_HEIGHT,
                  (double) gi->geometry.width / NS_PANGO_SCALE - 3.0,
                  NS_PANGO_UNKNOWN_GLYPH_HEIGHT - 3.0,
                  1.0,
                  invalid_input);
      goto done;
    }

  if (G_UNLIKELY (invalid_input))
    {
      rows = hbi->rows;
      cols = 1;
    }
  else if (ch == 0x2423 || g_unichar_type (ch) == G_UNICODE_SPACE_SEPARATOR)
    {
      /* A centred dot for a space made visible. */
      double x = cx + 0.5 * ((double) gi->geometry.width / NS_PANGO_SCALE);
      double y = cy + hbi->box_descent - 0.5 * hbi->box_height;

      gfx_new_sub_path (g);
      gfx_arc (g, x, y, 1.5 * hbi->line_width, 0, 2 * G_PI);
      gfx_close_path (g);
      fill_or_keep (grenderer);
      goto done;
    }
  else if (ch == '\t')
    {
      /* An arrow like ↦. */
      double y = cy + hbi->box_descent - 0.5 * hbi->box_height;
      double w = (double) gi->geometry.width / NS_PANGO_SCALE;
      double offset = 0.2 * w;
      double x = cx + offset;
      double al = w - 2 * offset;                 /* arrow length */
      double tl = MIN (hbi->digit_width, 0.75 * al); /* tip length */
      double tw2 = 2.5 * hbi->line_width;         /* tip width / 2 */
      double lw2 = 0.5 * hbi->line_width;         /* line width / 2 */

      gfx_move_to (g, x - lw2, y - tw2);
      gfx_line_to (g, x + lw2, y - tw2);
      gfx_line_to (g, x + lw2, y - lw2);
      gfx_line_to (g, x + al - tl, y - lw2);
      gfx_line_to (g, x + al - tl, y - tw2);
      gfx_line_to (g, x + al, y);
      gfx_line_to (g, x + al - tl, y + tw2);
      gfx_line_to (g, x + al - tl, y + lw2);
      gfx_line_to (g, x + lw2, y + lw2);
      gfx_line_to (g, x + lw2, y + tw2);
      gfx_line_to (g, x - lw2, y + tw2);
      gfx_close_path (g);
      fill_or_keep (grenderer);
      goto done;
    }
  else if (ch == '\n' || ch == 0x2028 || ch == 0x2029)
    {
      /* An arrow like ↵. */
      double w = (double) gi->geometry.width / NS_PANGO_SCALE;
      double offset = 0.2 * w;
      double al = w - 2 * offset;                 /* arrow length */
      double tl = MIN (hbi->digit_width, 0.75 * al); /* tip length */
      double ah = al - 0.5 * tl;                  /* arrow height */
      double tw2 = 2.5 * hbi->line_width;         /* tip width / 2 */
      double x = cx + offset;
      double y = cy - (hbi->box_height - al) / 2;
      double lw2 = 0.5 * hbi->line_width;         /* line width / 2 */

      gfx_move_to (g, x, y);
      gfx_line_to (g, x + tl, y - tw2);
      gfx_line_to (g, x + tl, y - lw2);
      gfx_line_to (g, x + al - lw2, y - lw2);
      gfx_line_to (g, x + al - lw2, y - ah);
      gfx_line_to (g, x + al + lw2, y - ah);
      gfx_line_to (g, x + al + lw2, y + lw2);
      gfx_line_to (g, x + tl, y + lw2);
      gfx_line_to (g, x + tl, y + tw2);
      gfx_close_path (g);
      fill_or_keep (grenderer);
      goto done;
    }
  else if ((name = ns_pango_get_ignorable_size (ch, &rows, &cols)))
    {
      /* Default ignorables are shown by their nick. */
    }
  else
    {
      /* Everything else gets a traditional hex box. */
      rows = hbi->rows;
      cols = (ch > 0xffff ? 6 : 4) / rows;
      g_snprintf (buf, sizeof buf, (ch > 0xffff) ? "%06X" : "%04X", ch);
      name = buf;
    }

  width = 3 * hbi->pad_x + cols * (hbi->digit_width + hbi->pad_x);
  lsb = ((double) gi->geometry.width / NS_PANGO_SCALE - width) * .5;
  lsb = floor (lsb / hbi->pad_x) * hbi->pad_x;

  draw_frame (grenderer,
              cx + lsb + 1.5 * hbi->pad_x,
              cy + hbi->box_descent - hbi->box_height + hbi->pad_y * .5,
              width - hbi->pad_x,
              hbi->box_height - hbi->pad_y,
              hbi->line_width,
              invalid_input);

  if (invalid_input || !name)
    goto done;

  x0 = cx + lsb + hbi->pad_x * 3;
  y0 = cy + hbi->box_descent - hbi->pad_y * 2 - ((hbi->rows - rows) * hbi->digit_height / 2);

  {
    hb_font_t *mini_hb = ns_pango_font_get_hb_font (hbi->font);
    PlacedGlyph digits[16];
    int n = 0;

    for (row = 0, p = name; row < rows; row++)
      {
        double y = y0 - (rows - 1 - row) * (hbi->digit_height + hbi->pad_y);

        for (col = 0; col < cols && *p && n < 16; col++, p++)
          {
            hb_codepoint_t glyph;

            if (!mini_hb || !hb_font_get_nominal_glyph (mini_hb, (hb_codepoint_t) (guchar) *p, &glyph))
              continue;
            digits[n].glyph = glyph;
            digits[n].x = x0 + col * (hbi->digit_width + hbi->pad_x);
            digits[n].y = y;
            n++;
          }
      }

    draw_placed_glyphs (grenderer, (NsPangoGfxFont *) hbi->font, digits, n);
  }

done:
  gfx_restore (g);
}

/* ---- rectangles, trapezoids, error underlines -------------------------- */

static void
ns_pango_gfx_renderer_draw_rectangle (NsPangoRenderer  *renderer,
                                      NsPangoRenderPart part,
                                      int               x,
                                      int               y,
                                      int               width,
                                      int               height)
{
  NsPangoGfxRenderer *grenderer = (NsPangoGfxRenderer *) renderer;
  gfx_t *g = grenderer->g;

  if (!grenderer->do_path)
    {
      gfx_save (g);
      set_color (grenderer, part);
      gfx_new_path (g);
    }

  gfx_rectangle (g,
                 grenderer->x_offset + (double) x / NS_PANGO_SCALE,
                 grenderer->y_offset + (double) y / NS_PANGO_SCALE,
                 (double) width / NS_PANGO_SCALE,
                 (double) height / NS_PANGO_SCALE);

  if (!grenderer->do_path)
    {
      gfx_fill (g);
      gfx_restore (g);
    }
}

static void
ns_pango_gfx_renderer_draw_trapezoid (NsPangoRenderer  *renderer,
                                      NsPangoRenderPart part,
                                      double            y1_,
                                      double            x11,
                                      double            x21,
                                      double            y2,
                                      double            x12,
                                      double            x22)
{
  NsPangoGfxRenderer *grenderer = (NsPangoGfxRenderer *) renderer;
  gfx_t *g = grenderer->g;
  double x, y;

  gfx_save (g);

  if (!grenderer->do_path)
    {
      set_color (grenderer, part);
      gfx_new_path (g);
    }

  /* The coordinates are in device space, offset by the origin. */
  x = grenderer->x_offset;
  y = grenderer->y_offset;
  gfx_user_to_device_distance (g, &x, &y);
  gfx_identity_matrix (g);
  gfx_translate (g, x, y);

  gfx_move_to (g, x11, y1_);
  gfx_line_to (g, x21, y1_);
  gfx_line_to (g, x22, y2);
  gfx_line_to (g, x12, y2);
  gfx_close_path (g);

  if (!grenderer->do_path)
    gfx_fill (g);

  gfx_restore (g);
}

/* Draws an error underline that looks like one of:
 *              H       E                H
 *     /\      /\      /\        /\      /\               -
 *   A/  \    /  \    /  \     A/  \    /  \              |
 *    \   \  /    \  /   /D     \   \  /    \             |
 *     \   \/  C   \/   /        \   \/   C  \            | height = HEIGHT_SQUARES * square
 *      \      /\  F   /          \  F   /\   \           |
 *       \    /  \    /            \    /  \   \G         |
 *        \  /    \  /              \  /    \  /          |
 *         \/      \/                \/      \/           -
 *         B                         B
 *         |---|
 *       unit_width = (HEIGHT_SQUARES - 1) * square
 *
 * The x, y, width, height passed in give the desired bounding box;
 * x/width are adjusted to make the underline a integer number of units
 * wide.
 */
#define HEIGHT_SQUARES 2.5

static void
draw_error_underline (gfx_t *g,
                      double x,
                      double y,
                      double width,
                      double height)
{
  double square = height / HEIGHT_SQUARES;
  double unit_width = (HEIGHT_SQUARES - 1) * square;
  double double_width = 2 * unit_width;
  int width_units = (int) ((width + unit_width / 2) / unit_width);
  double y_top, y_bottom;
  double x_left, x_middle, x_right;
  int i;

  x += (width - width_units * unit_width) / 2;

  y_top = y;
  y_bottom = y + height;

  /* Bottom of squiggle */
  x_middle = x + unit_width;
  x_right = x + double_width;
  gfx_move_to (g, x - square / 2, y_top + square / 2); /* A */
  for (i = 0; i < width_units - 2; i += 2)
    {
      gfx_line_to (g, x_middle, y_bottom); /* B */
      gfx_line_to (g, x_right, y_top + square); /* C */

      x_middle += double_width;
      x_right += double_width;
    }
  gfx_line_to (g, x_middle, y_bottom); /* B */

  if (i + 1 == width_units)
    gfx_line_to (g, x_middle + square / 2, y_bottom - square / 2); /* G */
  else if (i + 2 == width_units)
    {
      gfx_line_to (g, x_right + square / 2, y_top + square / 2); /* D */
      gfx_line_to (g, x_right, y_top); /* E */
    }

  /* Top of squiggle */
  x_left = x_middle - unit_width;
  for (; i >= 0; i -= 2)
    {
      gfx_line_to (g, x_middle, y_bottom - square); /* F */
      gfx_line_to (g, x_left, y_top); /* H */

      x_left -= double_width;
      x_middle -= double_width;
    }
}

static void
ns_pango_gfx_renderer_draw_error_underline (NsPangoRenderer *renderer,
                                            int              x,
                                            int              y,
                                            int              width,
                                            int              height)
{
  NsPangoGfxRenderer *grenderer = (NsPangoGfxRenderer *) renderer;
  gfx_t *g = grenderer->g;

  if (!grenderer->do_path)
    {
      gfx_save (g);
      set_color (grenderer, NS_PANGO_RENDER_PART_UNDERLINE);
      gfx_new_path (g);
    }

  draw_error_underline (g,
                        grenderer->x_offset + (double) x / NS_PANGO_SCALE,
                        grenderer->y_offset + (double) y / NS_PANGO_SCALE,
                        (double) width / NS_PANGO_SCALE,
                        (double) height / NS_PANGO_SCALE);

  if (!grenderer->do_path)
    {
      gfx_fill (g);
      gfx_restore (g);
    }
}

/* Shape attributes only reserve space here (inline replaced boxes): the
 * engine paints what they stand for itself. */
static void
ns_pango_gfx_renderer_draw_shape (NsPangoRenderer  *renderer G_GNUC_UNUSED,
                                  NsPangoAttrShape *attr G_GNUC_UNUSED,
                                  int               x G_GNUC_UNUSED,
                                  int               y G_GNUC_UNUSED)
{
}

static void
ns_pango_gfx_renderer_init (NsPangoGfxRenderer *renderer G_GNUC_UNUSED)
{
}

static void
ns_pango_gfx_renderer_class_init (NsPangoGfxRendererClass *klass)
{
  NsPangoRendererClass *renderer_class = NS_PANGO_RENDERER_CLASS (klass);

  renderer_class->draw_glyphs = ns_pango_gfx_renderer_draw_glyphs;
  renderer_class->draw_rectangle = ns_pango_gfx_renderer_draw_rectangle;
  renderer_class->draw_trapezoid = ns_pango_gfx_renderer_draw_trapezoid;
  renderer_class->draw_error_underline = ns_pango_gfx_renderer_draw_error_underline;
  renderer_class->draw_shape = ns_pango_gfx_renderer_draw_shape;
}

/* ---- entry points ------------------------------------------------------ */

/* One renderer per thread, made on first use. */
static GPrivate thread_renderer = G_PRIVATE_INIT (g_object_unref);

static NsPangoGfxRenderer *
acquire_renderer (gfx_t   *g,
                  gboolean do_path)
{
  NsPangoGfxRenderer *grenderer = g_private_get (&thread_renderer);

  if (G_UNLIKELY (!grenderer))
    {
      grenderer = g_object_new (NS_TYPE_PANGO_GFX_RENDERER, NULL);
      g_private_set (&thread_renderer, grenderer);
    }
  else if (grenderer->g)
    {
      /* Re-entered (a shape drawn while drawing): a fresh one. */
      grenderer = g_object_new (NS_TYPE_PANGO_GFX_RENDERER, NULL);
    }

  grenderer->g = g;
  grenderer->do_path = do_path;
  grenderer->x_offset = 0.;
  grenderer->y_offset = 0.;
  if (gfx_has_current_point (g))
    gfx_get_current_point (g, &grenderer->x_offset, &grenderer->y_offset);
  ns_pango_renderer_set_components (NS_PANGO_RENDERER (grenderer),
                                    NS_PANGO_RENDER_COMPONENT_ALL);

  if (!do_path)
    gfx_new_path (g);

  return grenderer;
}

static void
release_renderer (NsPangoGfxRenderer *grenderer,
                  gboolean            had_point)
{
  gfx_t *g = grenderer->g;

  if (!grenderer->do_path)
    gfx_new_path (g);
  if (had_point)
    gfx_move_to (g, grenderer->x_offset, grenderer->y_offset);

  grenderer->g = NULL;
  if (grenderer != g_private_get (&thread_renderer))
    g_object_unref (grenderer);
}

void
ns_pango_gfx_show_layout_line (gfx_t             *g,
                               NsPangoLayoutLine *line)
{
  NsPangoGfxRenderer *grenderer;
  gboolean had_point;

  g_return_if_fail (g != NULL);
  g_return_if_fail (line != NULL);

  had_point = gfx_has_current_point (g);
  grenderer = acquire_renderer (g, FALSE);
  ns_pango_renderer_draw_layout_line (NS_PANGO_RENDERER (grenderer), line, 0, 0);
  release_renderer (grenderer, had_point);
}

void
ns_pango_gfx_show_layout (gfx_t         *g,
                          NsPangoLayout *layout)
{
  NsPangoGfxRenderer *grenderer;
  gboolean had_point;

  g_return_if_fail (g != NULL);
  g_return_if_fail (NS_PANGO_IS_LAYOUT (layout));

  had_point = gfx_has_current_point (g);
  grenderer = acquire_renderer (g, FALSE);
  ns_pango_renderer_draw_layout (NS_PANGO_RENDERER (grenderer), layout, 0, 0);
  release_renderer (grenderer, had_point);
}

void
ns_pango_gfx_layout_path (gfx_t         *g,
                          NsPangoLayout *layout)
{
  NsPangoGfxRenderer *grenderer;
  gboolean had_point;

  g_return_if_fail (g != NULL);
  g_return_if_fail (NS_PANGO_IS_LAYOUT (layout));

  had_point = gfx_has_current_point (g);
  grenderer = acquire_renderer (g, TRUE);
  ns_pango_renderer_draw_layout (NS_PANGO_RENDERER (grenderer), layout, 0, 0);
  release_renderer (grenderer, had_point);
}
