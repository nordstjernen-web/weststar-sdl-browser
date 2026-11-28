/* Weststar SDL Browser — nstext: drawing layouts with gfx. Lines are
 * drawn run by run as Pango's renderer draws them (pango-renderer.c:
 * backgrounds, glyphs, then underlines, overlines and strikethroughs
 * merged across runs), and glyphs as the fork's gfx backend draws them
 * (src/web-text-lib/pangogfx-render.c: cached FreeType bitmaps at the
 * device's resolution, quarter-pixel phases, outlines otherwise, colour
 * glyphs as they are, hex boxes for missing characters).
 *
 * Copyright (C) 2004 Red Hat Software
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "nstext-layout-private.h"

#include <math.h>
#include <stdio.h>

enum
{
  PART_FOREGROUND,
  PART_BACKGROUND,
  PART_UNDERLINE,
  PART_STRIKETHROUGH,
  PART_OVERLINE,
  N_PARTS
};

typedef struct
{
  NsPangoUnderline underline;
  NsPangoRectangle underline_rect;
  NsPangoOverline  overline;
  NsPangoRectangle overline_rect;
  int              strikethrough;
  NsPangoRectangle strikethrough_rect;
  int              strikethrough_glyphs;
  int              logical_rect_end;
} LineState;

typedef struct
{
  gfx_t   *g;
  int      do_path;
  double   x_offset, y_offset;

  int      color_set[N_PARTS];
  NtColor  color[N_PARTS];
  uint16_t alpha[N_PARTS];

  NsPangoUnderline underline;
  NsPangoOverline  overline;
  int              strikethrough;
  LineState       *line_state;
} Renderer;

static void
set_color (Renderer *r, int part)
{
  double red = 0., green = 0., blue = 0., alpha = 1.;
  uint16_t a = r->alpha[part];

  if (!a && !r->color_set[part])
    return;
  if (r->color_set[part])
    {
      red = r->color[part].red / 65535.;
      green = r->color[part].green / 65535.;
      blue = r->color[part].blue / 65535.;
    }
  else if (!gfx_pattern_get_rgba (gfx_get_source (r->g), &red, &green, &blue, &alpha))
    {
      red = green = blue = 0.;
      alpha = 1.;
    }
  if (a)
    alpha = a / 65535.;
  gfx_set_source_rgba (r->g, red, green, blue, alpha);
}

/* ---- glyphs ------------------------------------------------------------ */

typedef struct
{
  uint32_t glyph;
  double   x, y;
} PlacedGlyph;

static void
user_to_pixels (gfx_t *g, gfx_matrix_t *m, gfx_matrix_t *pixels_to_device)
{
  gfx_surface_t *target = gfx_get_group_target (g);
  double sx = 1, sy = 1, ox = 0, oy = 0;
  gfx_matrix_t dev;

  gfx_surface_get_device_scale (target, &sx, &sy);
  gfx_surface_get_device_offset (target, &ox, &oy);
  gfx_matrix_init (&dev, sx, 0, 0, sy, ox, oy);
  gfx_get_matrix (g, m);
  gfx_matrix_multiply (m, m, &dev);
  *pixels_to_device = dev;
  if (gfx_matrix_invert (pixels_to_device) != GFX_STATUS_SUCCESS)
    gfx_matrix_init_identity (pixels_to_device);
}

static int
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

static void
glyph_position (double v, int *pixel, int *phase)
{
  double p = floor (v + 0.125);

  *pixel = (int) p;
  *phase = (int) (floor (4 * (v + 0.125)) - 4 * p) & 3;
}

static void
accumulate (gfx_surface_t *acc, int ax, int ay, const NtGlyphImage *image, int x, int y)
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
              uint32_t px;

              memcpy (&px, sd + (size_t) j * (size_t) ss + 4 * (size_t) i, 4);
              v = px >> 24;
            }
          else
            v = sd[(size_t) j * (size_t) ss + (size_t) i];
          v += ad[(size_t) ty * (size_t) as + (size_t) tx];
          ad[(size_t) ty * (size_t) as + (size_t) tx] = (unsigned char) NT_MIN (v, 255u);
        }
    }
}

static void
composite_image (gfx_t *g, const NtGlyphImage *image, int x, int y, double alpha)
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

static void
draw_placed_glyphs (Renderer *r, NtFont *font, const PlacedGlyph *glyphs, int n)
{
  gfx_t *g = r->g;
  gfx_matrix_t u2p, pixels, user;
  gfx_surface_t *acc = NULL;
  gfx_rectangle_int_t box = { 0, 0, 0, 0 };
  double device_size, alpha = 1.;
  int i;

  if (n <= 0)
    return;

  if (r->do_path)
    {
      for (i = 0; i < n; i++)
        nt_font_append_glyph_path (font, g, glyphs[i].glyph, glyphs[i].x, glyphs[i].y);
      return;
    }

  user_to_pixels (g, &u2p, &pixels);
  if (!nt_font_device_size (font, &u2p, &device_size))
    {
      gfx_new_path (g);
      for (i = 0; i < n; i++)
        nt_font_append_glyph_path (font, g, glyphs[i].glyph, glyphs[i].x, glyphs[i].y);
      gfx_set_fill_rule (g, GFX_FILL_RULE_WINDING);
      gfx_fill (g);
      return;
    }

  if (!operator_is_bounded (gfx_get_operator (g)))
    {
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
      NtGlyphImage image;
      double dx = glyphs[i].x, dy = glyphs[i].y;
      int px, py, xphase, yphase;

      gfx_matrix_transform_point (&u2p, &dx, &dy);
      if (!(fabs (dx) < 1e7 && fabs (dy) < 1e7))
        continue;
      glyph_position (dx, &px, &xphase);
      glyph_position (dy, &py, &yphase);

      if (!nt_font_get_glyph_image (font, glyphs[i].glyph, device_size, xphase, yphase, &image))
        {
          gfx_set_matrix (g, &user);
          gfx_new_path (g);
          nt_font_append_glyph_path (font, g, glyphs[i].glyph, glyphs[i].x, glyphs[i].y);
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

/* ---- unknown glyphs ---------------------------------------------------- */

static void
draw_frame (Renderer *r, double x, double y, double width, double height,
            double line_width, int invalid)
{
  gfx_t *g = r->g;

  if (r->do_path)
    {
      double d2 = line_width * .5, d = line_width;

      gfx_rectangle (g, x - d2, y - d2, width + d, height + d);
      gfx_rectangle (g, x + width - d2, y + d2, - (width - d), height - d);
      (void) invalid;
    }
  else
    {
      gfx_new_path (g);
      gfx_rectangle (g, x, y, width, height);
      if (invalid)
        {
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
draw_unknown_glyph (Renderer *r, NtFont *font, const NtGlyph *gi, double cx, double cy)
{
  gfx_t *g = r->g;
  NtHexBox *hbi = font ? nt_font_get_hex_box (font) : NULL;
  char buf[7];
  nt_unichar ch = gi->glyph & ~NT_GLYPH_UNKNOWN_FLAG;
  int invalid_input = gi->glyph == NT_GLYPH_INVALID_INPUT || ch > 0x10FFFF;
  double x0, y0, width, lsb;
  int row, col, rows, cols;
  const char *p;

  gfx_save (g);
  if (!r->do_path)
    gfx_new_path (g);

  if (!hbi)
    {
      draw_frame (r, cx + 1.5, cy + 1.5 - 14, (double) gi->width / NS_PANGO_SCALE - 3.0,
                  14 - 3.0, 1.0, invalid_input);
      goto done;
    }

  if (invalid_input)
    {
      rows = hbi->rows;
      cols = 1;
      buf[0] = '\0';
    }
  else
    {
      rows = hbi->rows;
      cols = (ch > 0xffff ? 6 : 4) / rows;
      snprintf (buf, sizeof buf, (ch > 0xffff) ? "%06X" : "%04X", (unsigned) ch);
    }

  width = 3 * hbi->pad_x + cols * (hbi->digit_width + hbi->pad_x);
  lsb = ((double) gi->width / NS_PANGO_SCALE - width) * .5;
  lsb = floor (lsb / hbi->pad_x) * hbi->pad_x;

  draw_frame (r, cx + lsb + 1.5 * hbi->pad_x,
              cy + hbi->box_descent - hbi->box_height + hbi->pad_y * .5,
              width - hbi->pad_x, hbi->box_height - hbi->pad_y, hbi->line_width, invalid_input);
  if (invalid_input)
    goto done;

  x0 = cx + lsb + hbi->pad_x * 3;
  y0 = cy + hbi->box_descent - hbi->pad_y * 2;
  {
    PlacedGlyph digits[16];
    int n = 0;

    for (row = 0, p = buf; row < rows; row++)
      {
        double y = y0 - (rows - 1 - row) * (hbi->digit_height + hbi->pad_y);

        for (col = 0; col < cols && *p && n < 16; col++, p++)
          {
            hb_codepoint_t glyph;

            if (!hb_font_get_nominal_glyph (hbi->font->hb_font, (hb_codepoint_t) (unsigned char) *p,
                                            &glyph))
              continue;
            digits[n].glyph = glyph;
            digits[n].x = x0 + col * (hbi->digit_width + hbi->pad_x);
            digits[n].y = y;
            n++;
          }
      }
    draw_placed_glyphs (r, hbi->font, digits, n);
  }

done:
  gfx_restore (g);
}

#define STACK_GLYPHS 256

static void
draw_glyphs (Renderer *r, NtFont *font, const NtGlyphString *glyphs, int x, int y)
{
  double base_x = r->x_offset + (double) x / NS_PANGO_SCALE;
  double base_y = r->y_offset + (double) y / NS_PANGO_SCALE;
  PlacedGlyph stack[STACK_GLYPHS];
  PlacedGlyph *placed = stack;
  int x_position = 0, i, n = 0;

  gfx_save (r->g);
  if (!r->do_path)
    set_color (r, PART_FOREGROUND);
  if (glyphs->num_glyphs > STACK_GLYPHS)
    placed = nt_new (PlacedGlyph, glyphs->num_glyphs);

  for (i = 0; i < glyphs->num_glyphs; i++)
    {
      const NtGlyph *gi = &glyphs->glyphs[i];

      if (gi->glyph != NT_GLYPH_EMPTY)
        {
          double cx = base_x + (double) (x_position + gi->x_offset) / NS_PANGO_SCALE;
          double cy = gi->y_offset == 0 ? base_y : base_y + (double) gi->y_offset / NS_PANGO_SCALE;

          if ((gi->glyph & NT_GLYPH_UNKNOWN_FLAG) || !font)
            {
              if (font)
                draw_placed_glyphs (r, font, placed, n);
              n = 0;
              if (gi->glyph != (0x20 | NT_GLYPH_UNKNOWN_FLAG))
                draw_unknown_glyph (r, font, gi, cx, cy);
            }
          else
            {
              placed[n].glyph = gi->glyph;
              placed[n].x = cx;
              placed[n].y = cy;
              n++;
            }
        }
      x_position += gi->width;
    }
  if (font)
    draw_placed_glyphs (r, font, placed, n);
  if (placed != stack)
    free (placed);
  gfx_restore (r->g);
}

/* ---- rectangles and error underlines ----------------------------------- */

static void
draw_rectangle (Renderer *r, int part, int x, int y, int width, int height)
{
  gfx_t *g = r->g;

  if (!r->do_path)
    {
      gfx_save (g);
      set_color (r, part);
      gfx_new_path (g);
    }
  gfx_rectangle (g, r->x_offset + (double) x / NS_PANGO_SCALE,
                 r->y_offset + (double) y / NS_PANGO_SCALE,
                 (double) width / NS_PANGO_SCALE, (double) height / NS_PANGO_SCALE);
  if (!r->do_path)
    {
      gfx_fill (g);
      gfx_restore (g);
    }
}

#define HEIGHT_SQUARES 2.5

static void
error_underline_path (gfx_t *g, double x, double y, double width, double height)
{
  double square = height / HEIGHT_SQUARES;
  double unit_width = (HEIGHT_SQUARES - 1) * square;
  double double_width = 2 * unit_width;
  int width_units = (int) ((width + unit_width / 2) / unit_width);
  double y_top, y_bottom, x_left, x_middle, x_right;
  int i;

  x += (width - width_units * unit_width) / 2;
  y_top = y;
  y_bottom = y + height;
  x_middle = x + unit_width;
  x_right = x + double_width;
  gfx_move_to (g, x - square / 2, y_top + square / 2);
  for (i = 0; i < width_units - 2; i += 2)
    {
      gfx_line_to (g, x_middle, y_bottom);
      gfx_line_to (g, x_right, y_top + square);
      x_middle += double_width;
      x_right += double_width;
    }
  gfx_line_to (g, x_middle, y_bottom);
  if (i + 1 == width_units)
    gfx_line_to (g, x_middle + square / 2, y_bottom - square / 2);
  else if (i + 2 == width_units)
    {
      gfx_line_to (g, x_right + square / 2, y_top + square / 2);
      gfx_line_to (g, x_right, y_top);
    }
  x_left = x_middle - unit_width;
  for (; i >= 0; i -= 2)
    {
      gfx_line_to (g, x_middle, y_bottom - square);
      gfx_line_to (g, x_left, y_top);
      x_left -= double_width;
      x_middle -= double_width;
    }
}

static void
draw_error_underline (Renderer *r, int x, int y, int width, int height)
{
  gfx_t *g = r->g;

  if (!r->do_path)
    {
      gfx_save (g);
      set_color (r, PART_UNDERLINE);
      gfx_new_path (g);
    }
  error_underline_path (g, r->x_offset + (double) x / NS_PANGO_SCALE,
                        r->y_offset + (double) y / NS_PANGO_SCALE,
                        (double) width / NS_PANGO_SCALE, (double) height / NS_PANGO_SCALE);
  if (!r->do_path)
    {
      gfx_fill (g);
      gfx_restore (g);
    }
}

/* ---- decorations (pango-renderer.c) ------------------------------------ */

static void
draw_underline (Renderer *r, LineState *state)
{
  NsPangoRectangle *rect = &state->underline_rect;
  NsPangoUnderline underline = state->underline;

  state->underline = NS_PANGO_UNDERLINE_NONE;
  switch (underline)
    {
    case NS_PANGO_UNDERLINE_DOUBLE:
    case NS_PANGO_UNDERLINE_DOUBLE_LINE:
      draw_rectangle (r, PART_UNDERLINE, rect->x, rect->y + 2 * rect->height, rect->width,
                      rect->height);
      draw_rectangle (r, PART_UNDERLINE, rect->x, rect->y, rect->width, rect->height);
      break;
    case NS_PANGO_UNDERLINE_SINGLE:
    case NS_PANGO_UNDERLINE_LOW:
    case NS_PANGO_UNDERLINE_SINGLE_LINE:
      draw_rectangle (r, PART_UNDERLINE, rect->x, rect->y, rect->width, rect->height);
      break;
    case NS_PANGO_UNDERLINE_ERROR:
    case NS_PANGO_UNDERLINE_ERROR_LINE:
      draw_error_underline (r, rect->x, rect->y, rect->width, 3 * rect->height);
      break;
    case NS_PANGO_UNDERLINE_NONE:
    default:
      break;
    }
}

static void
draw_overline (Renderer *r, LineState *state)
{
  NsPangoRectangle *rect = &state->overline_rect;
  NsPangoOverline overline = state->overline;

  state->overline = NS_PANGO_OVERLINE_NONE;
  if (overline == NS_PANGO_OVERLINE_SINGLE)
    draw_rectangle (r, PART_OVERLINE, rect->x, rect->y, rect->width, rect->height);
}

static void
draw_strikethrough (Renderer *r, LineState *state)
{
  NsPangoRectangle *rect = &state->strikethrough_rect;
  int num_glyphs = state->strikethrough_glyphs;

  if (state->strikethrough && num_glyphs > 0)
    draw_rectangle (r, PART_STRIKETHROUGH, rect->x, rect->y / num_glyphs, rect->width,
                    rect->height / num_glyphs);
  state->strikethrough = FALSE;
  state->strikethrough_glyphs = 0;
  rect->x += rect->width;
  rect->width = 0;
  rect->y = 0;
  rect->height = 0;
}

static void
add_underline (Renderer *r, LineState *state, NsPangoFontMetrics *metrics, int base_x,
               int base_y, NsPangoRectangle *ink, NsPangoRectangle *logical)
{
  NsPangoRectangle *current = &state->underline_rect;
  NsPangoRectangle new_rect;
  int thickness = metrics->underline_thickness, position = metrics->underline_position;

  new_rect.x = base_x + NT_MIN (ink->x, logical->x);
  new_rect.width = NT_MAX (ink->width, logical->width);
  new_rect.height = thickness;
  new_rect.y = base_y;

  switch (r->underline)
    {
    case NS_PANGO_UNDERLINE_SINGLE:
    case NS_PANGO_UNDERLINE_DOUBLE:
    case NS_PANGO_UNDERLINE_ERROR:
      new_rect.y -= position;
      break;
    case NS_PANGO_UNDERLINE_LOW:
      new_rect.y += ink->y + ink->height + thickness;
      break;
    case NS_PANGO_UNDERLINE_SINGLE_LINE:
    case NS_PANGO_UNDERLINE_DOUBLE_LINE:
    case NS_PANGO_UNDERLINE_ERROR_LINE:
      new_rect.y -= position;
      if (state->underline == r->underline)
        {
          new_rect.y = NT_MAX (current->y, new_rect.y);
          new_rect.height = NT_MAX (current->height, new_rect.height);
          current->y = new_rect.y;
          current->height = new_rect.height;
        }
      break;
    case NS_PANGO_UNDERLINE_NONE:
    default:
      break;
    }

  if (r->underline == state->underline && new_rect.y == current->y &&
      new_rect.height == current->height)
    current->width = new_rect.x + new_rect.width - current->x;
  else
    {
      draw_underline (r, state);
      *current = new_rect;
      state->underline = r->underline;
    }
}

static void
add_overline (Renderer *r, LineState *state, NsPangoFontMetrics *metrics, int base_x,
              int base_y, NsPangoRectangle *ink, NsPangoRectangle *logical)
{
  NsPangoRectangle *current = &state->overline_rect;
  NsPangoRectangle new_rect;

  new_rect.x = base_x + NT_MIN (ink->x, logical->x);
  new_rect.width = NT_MAX (ink->width, logical->width);
  new_rect.height = metrics->underline_thickness;
  new_rect.y = base_y - metrics->ascent;
  if (state->overline == r->overline)
    {
      new_rect.y = NT_MIN (current->y, new_rect.y);
      new_rect.height = NT_MAX (current->height, new_rect.height);
      current->y = new_rect.y;
      current->height = new_rect.height;
    }
  if (r->overline == state->overline && new_rect.y == current->y &&
      new_rect.height == current->height)
    current->width = new_rect.x + new_rect.width - current->x;
  else
    {
      draw_overline (r, state);
      *current = new_rect;
      state->overline = r->overline;
    }
}

static void
add_strikethrough (Renderer *r NT_UNUSED, LineState *state, NsPangoFontMetrics *metrics,
                   int base_x, int base_y, NsPangoRectangle *ink, NsPangoRectangle *logical,
                   int num_glyphs)
{
  NsPangoRectangle *current = &state->strikethrough_rect;
  NsPangoRectangle new_rect;

  new_rect.x = base_x + NT_MIN (ink->x, logical->x);
  new_rect.width = NT_MAX (ink->width, logical->width);
  new_rect.y = (base_y - metrics->strikethrough_position) * num_glyphs;
  new_rect.height = metrics->strikethrough_thickness * num_glyphs;
  if (state->strikethrough)
    {
      current->width = new_rect.x + new_rect.width - current->x;
      current->y += new_rect.y;
      current->height += new_rect.height;
      state->strikethrough_glyphs += num_glyphs;
    }
  else
    {
      *current = new_rect;
      state->strikethrough = TRUE;
      state->strikethrough_glyphs = num_glyphs;
    }
}

/* A colour or alpha is about to change: what is pending of that part is
 * drawn first, in the colour it had (Pango's handle_line_state_change). */
static void
part_changed (Renderer *r, int part)
{
  LineState *state = r->line_state;

  if (!state)
    return;
  if (part == PART_UNDERLINE && state->underline != NS_PANGO_UNDERLINE_NONE)
    {
      NsPangoRectangle *rect = &state->underline_rect;

      rect->width = state->logical_rect_end - rect->x;
      draw_underline (r, state);
      state->underline = r->underline;
      rect->x = state->logical_rect_end;
      rect->width = 0;
    }
  if (part == PART_OVERLINE && state->overline != NS_PANGO_OVERLINE_NONE)
    {
      NsPangoRectangle *rect = &state->overline_rect;

      rect->width = state->logical_rect_end - rect->x;
      draw_overline (r, state);
      state->overline = r->overline;
      rect->x = state->logical_rect_end;
      rect->width = 0;
    }
  if (part == PART_STRIKETHROUGH && state->strikethrough)
    {
      NsPangoRectangle *rect = &state->strikethrough_rect;

      rect->width = state->logical_rect_end - rect->x;
      draw_strikethrough (r, state);
      state->strikethrough = r->strikethrough;
    }
}

static void
set_part_color (Renderer *r, int part, const NtColor *c)
{
  if ((!c && !r->color_set[part]) ||
      (c && r->color_set[part] && r->color[part].red == c->red &&
       r->color[part].green == c->green && r->color[part].blue == c->blue))
    return;
  part_changed (r, part);
  r->color_set[part] = c != NULL;
  if (c)
    r->color[part] = *c;
}

static void
set_part_alpha (Renderer *r, int part, uint16_t alpha)
{
  if (r->alpha[part] == alpha)
    return;
  part_changed (r, part);
  r->alpha[part] = alpha;
}

static void
prepare_run (Renderer *r, const NtRun *run)
{
  const NtColor *fg = NULL, *bg = NULL, *ul = NULL, *ol = NULL, *st = NULL;
  uint16_t fg_alpha = 0, bg_alpha = 0;
  int i;

  r->underline = NS_PANGO_UNDERLINE_NONE;
  r->overline = NS_PANGO_OVERLINE_NONE;
  r->strikethrough = FALSE;

  for (i = 0; i < run->item->n_extra; i++)
    {
      const NtAttr *a = run->item->extra_attrs[i];

      switch ((NtAttrType) a->base.type)
        {
        case NT_ATTR_UNDERLINE: r->underline = (NsPangoUnderline) a->v.i; break;
        case NT_ATTR_OVERLINE: r->overline = (NsPangoOverline) a->v.i; break;
        case NT_ATTR_STRIKETHROUGH: r->strikethrough = a->v.i; break;
        case NT_ATTR_FOREGROUND: fg = &a->v.color; break;
        case NT_ATTR_BACKGROUND: bg = &a->v.color; break;
        case NT_ATTR_UNDERLINE_COLOR: ul = &a->v.color; break;
        case NT_ATTR_OVERLINE_COLOR: ol = &a->v.color; break;
        case NT_ATTR_STRIKETHROUGH_COLOR: st = &a->v.color; break;
        case NT_ATTR_FOREGROUND_ALPHA: fg_alpha = (uint16_t) a->v.i; break;
        case NT_ATTR_BACKGROUND_ALPHA: bg_alpha = (uint16_t) a->v.i; break;
        default: break;
        }
    }
  if (!ul)
    ul = fg;
  if (!ol)
    ol = fg;
  if (!st)
    st = fg;

  set_part_color (r, PART_FOREGROUND, fg);
  set_part_color (r, PART_BACKGROUND, bg);
  set_part_color (r, PART_UNDERLINE, ul);
  set_part_color (r, PART_STRIKETHROUGH, st);
  set_part_color (r, PART_OVERLINE, ol);
  set_part_alpha (r, PART_FOREGROUND, fg_alpha);
  set_part_alpha (r, PART_BACKGROUND, bg_alpha);
  set_part_alpha (r, PART_UNDERLINE, fg_alpha);
  set_part_alpha (r, PART_STRIKETHROUGH, fg_alpha);
  set_part_alpha (r, PART_OVERLINE, fg_alpha);
}

static void
draw_layout_line (Renderer *r, NtLine *line, int x, int y)
{
  int x_off = 0, glyph_string_width, i;
  LineState state;
  int got_overall = FALSE;
  NsPangoRectangle overall_rect;

  memset (&state, 0, sizeof state);
  r->line_state = &state;

  for (i = 0; i < line->n_runs; i++)
    {
      NtRun *run = line->runs[i];
      NtAttr *shape = nt_item_find_attr (run->item, NT_ATTR_SHAPE);
      NsPangoRectangle ink_rect, logical_rect;
      int y_off, decorated;

      if (run->item->flags & NT_ITEM_IS_ELLIPSIS)
        shape = NULL;
      prepare_run (r, run);
      decorated = r->underline != NS_PANGO_UNDERLINE_NONE ||
                  r->overline != NS_PANGO_OVERLINE_NONE || r->strikethrough;

      if (shape)
        {
          NsPangoRectangle si = shape->v.shape.ink, sl = shape->v.shape.logical;
          int n = run->glyphs->num_glyphs;

          ink_rect.x = NT_MIN (si.x, si.x + sl.width * (n - 1));
          ink_rect.width = NT_MAX (si.width, si.width + sl.width * (n - 1));
          ink_rect.y = si.y;
          ink_rect.height = si.height;
          logical_rect.x = NT_MIN (sl.x, sl.x + sl.width * (n - 1));
          logical_rect.width = NT_MAX (sl.width, sl.width * n);
          logical_rect.y = sl.y;
          logical_rect.height = sl.height;
          glyph_string_width = logical_rect.width;
        }
      else if (decorated)
        {
          nt_glyph_string_extents (run->glyphs, run->item->font, &ink_rect, &logical_rect);
          glyph_string_width = logical_rect.width;
        }
      else
        glyph_string_width = nt_glyph_string_get_width (run->glyphs);

      state.logical_rect_end = x + x_off + glyph_string_width;
      x_off += run->start_x_offset;
      y_off = run->y_offset;

      if (r->color_set[PART_BACKGROUND] && !r->do_path)
        {
          if (!got_overall)
            {
              ns_pango_layout_line_get_extents (&line->pub, NULL, &overall_rect);
              got_overall = TRUE;
            }
          draw_rectangle (r, PART_BACKGROUND, x + x_off, y + overall_rect.y,
                          glyph_string_width, overall_rect.height);
        }

      /* Shapes only reserve space: the engine paints what they stand for. */
      if (!shape)
        draw_glyphs (r, run->item->font, run->glyphs, x + x_off, y - y_off);

      if (decorated)
        {
          NsPangoFontMetrics *metrics = nt_font_get_metrics (run->item->font);

          if (r->underline != NS_PANGO_UNDERLINE_NONE)
            add_underline (r, &state, metrics, x + x_off, y - y_off, &ink_rect, &logical_rect);
          if (r->overline != NS_PANGO_OVERLINE_NONE)
            add_overline (r, &state, metrics, x + x_off, y - y_off, &ink_rect, &logical_rect);
          if (r->strikethrough)
            add_strikethrough (r, &state, metrics, x + x_off, y - y_off, &ink_rect,
                               &logical_rect, run->glyphs->num_glyphs);
          ns_pango_font_metrics_unref (metrics);
        }

      if (r->underline == NS_PANGO_UNDERLINE_NONE && state.underline != NS_PANGO_UNDERLINE_NONE)
        draw_underline (r, &state);
      if (r->overline == NS_PANGO_OVERLINE_NONE && state.overline != NS_PANGO_OVERLINE_NONE)
        draw_overline (r, &state);
      if (!r->strikethrough && state.strikethrough)
        draw_strikethrough (r, &state);

      x_off += glyph_string_width;
      x_off += run->end_x_offset;
    }

  draw_underline (r, &state);
  draw_overline (r, &state);
  draw_strikethrough (r, &state);
  r->line_state = NULL;
}

static void
renderer_begin (Renderer *r, gfx_t *g, int do_path)
{
  memset (r, 0, sizeof *r);
  r->g = g;
  r->do_path = do_path;
  if (gfx_has_current_point (g))
    gfx_get_current_point (g, &r->x_offset, &r->y_offset);
  if (!do_path)
    gfx_new_path (g);
}

static void
renderer_end (Renderer *r, int had_point)
{
  if (!r->do_path)
    gfx_new_path (r->g);
  if (had_point)
    gfx_move_to (r->g, r->x_offset, r->y_offset);
}

static void
draw_layout (Renderer *r, NsPangoLayout *layout)
{
  NtExtents *ext = nt_layout_line_extents (layout, NULL);
  int i;

  for (i = 0; ext && i < layout->n_lines; i++)
    draw_layout_line (r, layout->lines[i], ext[i].logical_rect.x, ext[i].baseline);
  free (ext);
}

void
ns_pango_gfx_show_layout (gfx_t *g, NsPangoLayout *layout)
{
  Renderer r;
  int had_point;

  if (!g || !layout)
    return;
  had_point = gfx_has_current_point (g);
  renderer_begin (&r, g, FALSE);
  draw_layout (&r, layout);
  renderer_end (&r, had_point);
}

void
ns_pango_gfx_show_layout_line (gfx_t *g, NsPangoLayoutLine *line)
{
  Renderer r;
  int had_point;

  if (!g || !line)
    return;
  had_point = gfx_has_current_point (g);
  renderer_begin (&r, g, FALSE);
  draw_layout_line (&r, (NtLine *) line, 0, 0);
  renderer_end (&r, had_point);
}

void
ns_pango_gfx_layout_path (gfx_t *g, NsPangoLayout *layout)
{
  Renderer r;
  int had_point;

  if (!g || !layout)
    return;
  had_point = gfx_has_current_point (g);
  renderer_begin (&r, g, TRUE);
  draw_layout (&r, layout);
  renderer_end (&r, had_point);
}
