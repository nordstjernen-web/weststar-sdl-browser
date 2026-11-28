/* Weststar SDL Browser — nstext: the font map, fontsets, fonts, metrics,
 * glyph extents and glyph rasterization.
 *
 * Fonts are found as Pango's fontconfig backend finds them: a pattern
 * made from the font description and language, fontconfig's
 * substitutions, FcFontSetMatch for the first font of a fontset and
 * FcFontSetSort for the fallbacks, over the SFNT fonts of fontconfig's
 * system and application sets (or nsfc's, without fontconfig). A font is
 * a HarfBuzz font at the pattern's pixel size; its glyphs are measured
 * (hinted) and rasterized with FreeType, as the gfx backend of the Pango
 * fork nstext replaced (web-text-lib's pangogfx-font.c) did, whose
 * rasterizing code this follows.
 *
 * Copyright (C) 2000-2005 Red Hat Software (pangofc, pangocairo)
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "nstext-private.h"

#include <math.h>
#include <stdio.h>

#include FT_OUTLINE_H
#include FT_SIZES_H
#include FT_SYNTHESIS_H
#include FT_MULTIPLE_MASTERS_H

#define EXTENTS_CACHE_SIZE 256 /* a power of two */
#define IMAGE_CACHE_FONT_BYTES  ((size_t) 2 << 20)
#define IMAGE_CACHE_TOTAL_BYTES ((size_t) 48 << 20)
#define UNKNOWN_GLYPH_HEIGHT 14

typedef struct NtGlyphExtents
{
  uint32_t         glyph;
  int              width;
  NsPangoRectangle ink;
} NtGlyphExtents;

typedef struct
{
  NtGlyphImage image;
  size_t       bytes;
} ImageEntry;

/* ---- FreeType ---------------------------------------------------------- */

static FT_Library ft_library;
static NsOnce     ft_once;
static NsMutex    ft_library_lock;
static SDL_AtomicInt image_total_bytes;

static FT_Library
get_ft_library (void)
{
  if (ns_once_enter (&ft_once))
    {
      if (FT_Init_FreeType (&ft_library) != 0)
        ft_library = NULL;
      ns_once_leave (&ft_once);
    }
  return ft_library;
}

/* FreeType load flags from fontconfig's rendering properties, as cairo's
 * FreeType backend chose them for grayscale antialiasing. */
static FT_Int32
pattern_load_flags (const FcPattern *pattern)
{
  FT_Int32 flags = FT_LOAD_DEFAULT;
  FcBool b;
  int hint_style;

  if (FcPatternGetInteger (pattern, FC_HINT_STYLE, 0, &hint_style) != FcResultMatch)
    hint_style = FC_HINT_FULL;
  if (FcPatternGetBool (pattern, FC_HINTING, 0, &b) == FcResultMatch && !b)
    hint_style = FC_HINT_NONE;

  if (hint_style == FC_HINT_NONE)
    flags |= FT_LOAD_NO_HINTING;
  else if (hint_style == FC_HINT_SLIGHT)
    flags |= FT_LOAD_TARGET_LIGHT;

  if (FcPatternGetBool (pattern, FC_AUTOHINT, 0, &b) == FcResultMatch && b)
    flags |= FT_LOAD_FORCE_AUTOHINT;

  if (FcPatternGetBool (pattern, FC_EMBEDDED_BITMAP, 0, &b) != FcResultMatch || !b)
    flags |= FT_LOAD_NO_BITMAP;

  return flags;
}

static int
set_pixel_size (FT_Face face, FT_Size size, FT_F26Dot6 x_px, FT_F26Dot6 y_px,
                double *scale)
{
  *scale = 1.0;
  if (FT_Activate_Size (size) != 0)
    return FALSE;
  if (FT_IS_SCALABLE (face))
    return FT_Set_Char_Size (face, NT_MAX (x_px, 1), NT_MAX (y_px, 1), 0, 0) == 0;
  if (face->num_fixed_sizes > 0)
    {
      int best = 0, i;

      for (i = 1; i < face->num_fixed_sizes; i++)
        {
          FT_Pos have = face->available_sizes[best].y_ppem;
          FT_Pos cand = face->available_sizes[i].y_ppem;

          if ((have < y_px && cand > have) || (cand >= y_px && cand < have))
            best = i;
        }
      if (FT_Select_Size (face, best) != 0)
        return FALSE;
      if (face->available_sizes[best].y_ppem > 0)
        *scale = (double) y_px / face->available_sizes[best].y_ppem;
      return TRUE;
    }
  return FALSE;
}

/* The FreeType face, opened on first use from the bytes HarfBuzz maps,
 * with the same variation coordinates. Called with the lock held. */
static FT_Face
get_face (NtFont *font)
{
  FT_Library library;
  hb_blob_t *blob;
  const char *data;
  unsigned int length = 0;
  int index = 0;
  FT_Face face = NULL;

  if (font->face || font->face_failed)
    return font->face;
  font->face_failed = TRUE;

  library = get_ft_library ();
  if (!library || !font->hb_font)
    return NULL;

  blob = hb_face_reference_blob (font->hb_face);
  data = hb_blob_get_data (blob, &length);
  hb_blob_destroy (blob); /* the face keeps it */
  if (!data || length == 0)
    return NULL;

  FcPatternGetInteger (font->pattern, FC_INDEX, 0, &index);

  ns_mutex_lock (&ft_library_lock);
  if (FT_New_Memory_Face (library, (const FT_Byte *) data, (FT_Long) length,
                          index, &face) != 0)
    face = NULL;
  if (face && (FT_New_Size (face, &font->metric_size) != 0 ||
               FT_New_Size (face, &font->render_size) != 0))
    {
      FT_Done_Face (face);
      face = NULL;
    }
  ns_mutex_unlock (&ft_library_lock);
  if (!face)
    return NULL;

  if (FT_HAS_MULTIPLE_MASTERS (face))
    {
      unsigned int n_coords = 0;
      const float *coords = hb_font_get_var_coords_design (font->hb_font, &n_coords);

      if (coords && n_coords > 0)
        {
          FT_Fixed *fixed = nt_new (FT_Fixed, n_coords);
          unsigned int i;

          for (i = 0; i < n_coords; i++)
            fixed[i] = (FT_Fixed) lround (coords[i] * 65536.0);
          FT_Set_Var_Design_Coordinates (face, n_coords, fixed);
          free (fixed);
        }
    }

  /* Hinted at the device size, as the fork's gfx backend (cairo's FreeType
   * backend before it) measures under a scaled context. */
  if (!set_pixel_size (face, font->metric_size,
                       (FT_F26Dot6) lround (font->size * font->ctm_sx * 64),
                       (FT_F26Dot6) lround (font->size * font->ctm_sy * 64),
                       &font->metric_scale))
    {
      ns_mutex_lock (&ft_library_lock);
      FT_Done_Face (face);
      ns_mutex_unlock (&ft_library_lock);
      return NULL;
    }

  font->face = face;
  font->face_failed = FALSE;
  return face;
}

static FT_Int32
glyph_load_flags (NtFont *font, FT_Face face)
{
  FT_Int32 flags = font->load_flags;

  if (FT_HAS_COLOR (face))
    flags |= FT_LOAD_COLOR;
  return flags;
}

/* ---- extents ----------------------------------------------------------- */

static void
init_font_extents (NtFont *font)
{
  hb_font_extents_t extents;

  hb_font_get_h_extents (font->hb_font, &extents);

  font->font_extents.x = 0;
  font->font_extents.width = 0;
  font->font_extents.y = - extents.ascender;
  font->font_extents.height = extents.ascender - extents.descender;

  /* Metrics are hinted. */
  if (font->font_extents.y < 0)
    font->font_extents.y = NS_PANGO_UNITS_FLOOR (font->font_extents.y);
  else
    font->font_extents.y = NS_PANGO_UNITS_CEIL (font->font_extents.y);
  if (font->font_extents.height < 0)
    font->font_extents.height = NS_PANGO_UNITS_FLOOR (extents.ascender) -
                                NS_PANGO_UNITS_CEIL (extents.descender);
  else
    font->font_extents.height = NS_PANGO_UNITS_CEIL (extents.ascender) -
                                NS_PANGO_UNITS_FLOOR (extents.descender);

  font->extents_cache = nt_new0 (NtGlyphExtents, EXTENTS_CACHE_SIZE);
  font->extents_cache[0].glyph = 1; /* glyph 1 is never in bucket 0 */
  font->extents_ready = TRUE;
}

static int
units_from_double (double d)
{
  return (int) floor (d * NS_PANGO_SCALE + 0.5);
}

static void
hb_glyph_extents (NtFont *font, uint32_t glyph, NtGlyphExtents *entry)
{
  hb_glyph_extents_t ext;

  entry->width = hb_font_get_glyph_h_advance (font->hb_font, glyph);
  if (hb_font_get_glyph_extents (font->hb_font, glyph, &ext))
    {
      entry->ink.x = ext.x_bearing;
      entry->ink.y = - ext.y_bearing;
      entry->ink.width = ext.width;
      entry->ink.height = - ext.height;
    }
}

/* What cairo's FreeType backend reported: the hinted outline's box and
 * advance at the device size, in user space. Called with the lock held. */
static void
compute_glyph_extents (NtFont *font, uint32_t glyph, NtGlyphExtents *entry)
{
  FT_Face face;
  FT_GlyphSlot slot;
  const FT_Glyph_Metrics *m;
  double xf, yf;
  double x_bearing, y_bearing, width, height, advance;

  memset (entry, 0, sizeof *entry);
  entry->glyph = glyph;

  face = get_face (font);
  if (!face)
    {
      hb_glyph_extents (font, glyph, entry);
      return;
    }
  if (FT_Activate_Size (font->metric_size) != 0)
    return;
  FT_Set_Transform (face, font->has_shape ? &font->shape : NULL, NULL);
  if (FT_Load_Glyph (face, glyph, glyph_load_flags (font, face)) != 0)
    return;

  slot = face->glyph;
  if (font->embolden && slot->format == FT_GLYPH_FORMAT_OUTLINE)
    FT_GlyphSlot_Embolden (slot);
  m = &slot->metrics;
  /* Device pixels (26.6) to user space. */
  xf = font->metric_scale / (64.0 * font->ctm_sx);
  yf = font->metric_scale / (64.0 * font->ctm_sy);

  if (font->load_flags & FT_LOAD_NO_HINTING)
    {
      FT_Pos x1 = m->horiBearingX & -64;
      FT_Pos x2 = (m->horiBearingX + m->width + 63) & -64;
      FT_Pos y1 = (-m->horiBearingY) & -64;
      FT_Pos y2 = (-m->horiBearingY + m->height + 63) & -64;

      x_bearing = x1 * xf;
      y_bearing = y1 * yf;
      width = (x2 - x1) * xf;
      height = (y2 - y1) * yf;
      advance = ((m->horiAdvance + 32) & -64) * xf;
    }
  else
    {
      x_bearing = m->horiBearingX * xf;
      y_bearing = -m->horiBearingY * yf;
      width = m->width * xf;
      height = m->height * yf;
      advance = m->horiAdvance * xf;
    }

  entry->width = units_from_double (advance);
  entry->ink.x = units_from_double (x_bearing);
  entry->ink.y = units_from_double (y_bearing);
  entry->ink.width = units_from_double (width);
  entry->ink.height = units_from_double (height);
}

static void get_glyph_extents_missing (NtFont *font, uint32_t glyph,
                                       NsPangoRectangle *ink, NsPangoRectangle *logical);

void
nt_font_get_glyph_extents (NtFont *font, uint32_t glyph,
                           NsPangoRectangle *ink_rect, NsPangoRectangle *logical_rect)
{
  NtGlyphExtents entry;

  if (!font)
    {
      /* No font: Pango's made-up box. */
      if (ink_rect)
        {
          ink_rect->x = NS_PANGO_SCALE;
          ink_rect->y = - (UNKNOWN_GLYPH_HEIGHT - 1) * NS_PANGO_SCALE;
          ink_rect->width = 8 * NS_PANGO_SCALE;
          ink_rect->height = (UNKNOWN_GLYPH_HEIGHT - 2) * NS_PANGO_SCALE;
        }
      if (logical_rect)
        {
          logical_rect->x = 0;
          logical_rect->y = - UNKNOWN_GLYPH_HEIGHT * NS_PANGO_SCALE;
          logical_rect->width = 10 * NS_PANGO_SCALE;
          logical_rect->height = UNKNOWN_GLYPH_HEIGHT * NS_PANGO_SCALE;
        }
      return;
    }

  if ((glyph & NT_GLYPH_UNKNOWN_FLAG) && glyph != NT_GLYPH_EMPTY)
    {
      get_glyph_extents_missing (font, glyph, ink_rect, logical_rect);
      return;
    }

  ns_mutex_lock (&font->lock);
  if (!font->extents_ready)
    init_font_extents (font);

  if (glyph == NT_GLYPH_EMPTY)
    {
      if (ink_rect)
        ink_rect->x = ink_rect->y = ink_rect->width = ink_rect->height = 0;
      if (logical_rect)
        *logical_rect = font->font_extents;
      ns_mutex_unlock (&font->lock);
      return;
    }

  {
    NtGlyphExtents *slot = &font->extents_cache[glyph & (EXTENTS_CACHE_SIZE - 1)];

    if (slot->glyph != glyph)
      compute_glyph_extents (font, glyph, slot);
    entry = *slot;
  }

  if (ink_rect)
    *ink_rect = entry.ink;
  if (logical_rect)
    {
      *logical_rect = font->font_extents;
      logical_rect->width = entry.width;
    }
  ns_mutex_unlock (&font->lock);
}

/* ---- hex boxes --------------------------------------------------------- */

static void
font_ascent_descent (NtFont *font, double *ascent, double *descent)
{
  FT_Face face;

  ns_mutex_lock (&font->lock);
  face = get_face (font);
  if (face)
    {
      double yf = font->metric_scale / (64.0 * font->ctm_sy);

      *ascent = font->metric_size->metrics.ascender * yf;
      *descent = - font->metric_size->metrics.descender * yf;
    }
  else
    {
      *ascent = UNKNOWN_GLYPH_HEIGHT;
      *descent = 0;
    }
  ns_mutex_unlock (&font->lock);
}

#define HINT(value) (ceil ((value) - 1e-5))

NtHexBox *
nt_font_get_hex_box (NtFont *font)
{
  static const char hexdigits[] = "0123456789ABCDEF";
  NtHexBox *hbi;
  NsPangoFontDescription *desc;
  NtFont *mini_font;
  double mini_size, pad, width = 0, height = 0, ascent, descent;
  int i;

  ns_mutex_lock (&font->lock);
  hbi = font->hexbox;
  ns_mutex_unlock (&font->lock);
  if (hbi)
    return hbi;

  desc = ns_pango_font_description_new ();
  ns_pango_font_description_set_family (desc, "monospace");
  mini_size = HINT (font->size / 2.2);
  ns_pango_font_description_set_absolute_size (desc, units_from_double (mini_size));
  mini_font = nt_font_map_load_font (font->fontmap, desc,
                                     ns_pango_language_from_string ("en"));
  ns_pango_font_description_free (desc);
  if (!mini_font)
    return NULL;

  for (i = 0; i < 16; i++)
    {
      hb_codepoint_t g;
      NsPangoRectangle ink;

      if (!hb_font_get_nominal_glyph (mini_font->hb_font, (hb_codepoint_t) hexdigits[i], &g))
        continue;
      nt_font_get_glyph_extents (mini_font, g, &ink, NULL);
      width = NT_MAX (width, (double) ink.width / NS_PANGO_SCALE);
      height = NT_MAX (height, (double) ink.height / NS_PANGO_SCALE);
    }

  font_ascent_descent (font, &ascent, &descent);
  if (ascent + descent <= 0)
    {
      ascent = UNKNOWN_GLYPH_HEIGHT;
      descent = 0;
    }

  pad = (ascent + descent) / 43;
  pad = NT_MIN (pad, mini_size);

  hbi = nt_new0 (NtHexBox, 1);
  hbi->font = mini_font;
  hbi->rows = 2;
  hbi->digit_width = HINT (width);
  hbi->digit_height = HINT (height);
  hbi->pad_x = HINT (pad);
  hbi->pad_y = HINT (pad);
  hbi->line_width = NT_MIN (hbi->pad_x, hbi->pad_y);
  hbi->box_height = 3 * hbi->pad_y + hbi->rows * (hbi->pad_y + hbi->digit_height);

  if (hbi->box_height <= ascent)
    hbi->box_descent = 2 * hbi->pad_y;
  else if (hbi->box_height <= ascent + descent - 2 * hbi->pad_y)
    hbi->box_descent = 2 * hbi->pad_y + hbi->box_height - ascent;
  else
    hbi->box_descent = descent * hbi->box_height / (ascent + descent);
  hbi->box_descent = HINT (hbi->box_descent);

  ns_mutex_lock (&font->lock);
  if (!font->hexbox)
    font->hexbox = hbi;
  else
    {
      nt_font_unref (hbi->font);
      free (hbi);
      hbi = font->hexbox;
    }
  ns_mutex_unlock (&font->lock);
  return hbi;
}

#undef HINT

/* The nick Pango draws for a default ignorable in its box. */
static const char *
ignorable_nick (nt_unichar ch, int *rows, int *cols)
{
  static const struct { nt_unichar ch; const char *nick; } nicks[] = {
    { 0x00ad, "SHY" }, { 0x034f, "CGJ" }, { 0x061c, "ALM" },
    { 0x200b, "ZWS" }, { 0x200c, "ZWNJ" }, { 0x200d, "ZWJ" },
    { 0x200e, "LRM" }, { 0x200f, "RLM" }, { 0x2028, "LS" }, { 0x2029, "PS" },
    { 0x202a, "LRE" }, { 0x202b, "RLE" }, { 0x202c, "PDF" }, { 0x202d, "LRO" },
    { 0x202e, "RLO" }, { 0x2060, "WJ" }, { 0x2066, "LRI" }, { 0x2067, "RLI" },
    { 0x2068, "FSI" }, { 0x2069, "PDI" }, { 0xfeff, "ZWNBSP" },
  };
  size_t i;

  for (i = 0; i < NT_N_ELEMENTS (nicks); i++)
    if (nicks[i].ch == ch)
      {
        size_t n = strlen (nicks[i].nick);

        *rows = n > 3 ? 2 : 1;
        *cols = (int) ((n + (size_t) *rows - 1) / (size_t) *rows);
        return nicks[i].nick;
      }
  return NULL;
}

static void
get_space_extents (NtFont *font, NsPangoRectangle *ink, NsPangoRectangle *logical)
{
  int width = units_from_double (font->size) / 4;

  if (ink)
    {
      ink->x = ink->y = ink->height = 0;
      ink->width = width;
    }
  if (logical)
    {
      ns_mutex_lock (&font->lock);
      if (!font->extents_ready)
        init_font_extents (font);
      *logical = font->font_extents;
      ns_mutex_unlock (&font->lock);
      logical->width = width;
    }
}

static void
get_glyph_extents_missing (NtFont *font, uint32_t glyph,
                           NsPangoRectangle *ink, NsPangoRectangle *logical)
{
  NtHexBox *hbi;
  nt_unichar ch = glyph & ~NT_GLYPH_UNKNOWN_FLAG;
  int rows, cols;

  if (ch == 0x20 || ch == 0x2423)
    {
      get_space_extents (font, ink, logical);
      return;
    }

  hbi = nt_font_get_hex_box (font);
  if (!hbi)
    {
      nt_font_get_glyph_extents (NULL, glyph, ink, logical);
      return;
    }

  if (glyph == NT_GLYPH_INVALID_INPUT || ch > 0x10FFFF)
    {
      rows = hbi->rows;
      cols = 1;
    }
  else if (!ignorable_nick (ch, &rows, &cols))
    {
      rows = hbi->rows;
      cols = (ch > 0xffff ? 6 : 4) / rows;
    }

  if (ink)
    {
      ink->x = (int) (NS_PANGO_SCALE * hbi->pad_x);
      ink->y = (int) (NS_PANGO_SCALE * (hbi->box_descent - hbi->box_height));
      ink->width = (int) (NS_PANGO_SCALE * (4 * hbi->pad_x + cols * (hbi->digit_width + hbi->pad_x)));
      ink->height = (int) (NS_PANGO_SCALE * hbi->box_height);
    }
  if (logical)
    {
      logical->x = 0;
      logical->y = (int) (NS_PANGO_SCALE * (hbi->box_descent - (hbi->box_height + hbi->pad_y)));
      logical->width = (int) (NS_PANGO_SCALE * (5 * hbi->pad_x + cols * (hbi->digit_width + hbi->pad_x)));
      logical->height = (int) (NS_PANGO_SCALE * (hbi->box_height + 2 * hbi->pad_y));
    }
}

/* ---- metrics ----------------------------------------------------------- */

NsPangoFontMetrics *
nt_font_metrics_new (void)
{
  NsPangoFontMetrics *m = nt_new0 (NsPangoFontMetrics, 1);

  m->ref_count = 1;
  return m;
}

NsPangoFontMetrics *
ns_pango_font_metrics_ref (NsPangoFontMetrics *m)
{
  if (m)
    SDL_AddAtomicInt ((SDL_AtomicInt *) (void *) &m->ref_count, 1);
  return m;
}

void
ns_pango_font_metrics_unref (NsPangoFontMetrics *m)
{
  if (m && SDL_AddAtomicInt ((SDL_AtomicInt *) (void *) &m->ref_count, -1) == 1)
    free (m);
}

int ns_pango_font_metrics_get_ascent (NsPangoFontMetrics *m) { return m->ascent; }
int ns_pango_font_metrics_get_descent (NsPangoFontMetrics *m) { return m->descent; }
int ns_pango_font_metrics_get_height (NsPangoFontMetrics *m) { return m->height; }
int ns_pango_font_metrics_get_approximate_char_width (NsPangoFontMetrics *m)
{ return m->approximate_char_width; }
int ns_pango_font_metrics_get_approximate_digit_width (NsPangoFontMetrics *m)
{ return m->approximate_digit_width; }
int ns_pango_font_metrics_get_underline_position (NsPangoFontMetrics *m)
{ return m->underline_position; }
int ns_pango_font_metrics_get_underline_thickness (NsPangoFontMetrics *m)
{ return m->underline_thickness; }
int ns_pango_font_metrics_get_strikethrough_position (NsPangoFontMetrics *m)
{ return m->strikethrough_position; }
int ns_pango_font_metrics_get_strikethrough_thickness (NsPangoFontMetrics *m)
{ return m->strikethrough_thickness; }

/* Measuring the approximate widths lays text out, which asks for metrics
 * again: the guard is per thread. */
static NsPrivate in_get_metrics = NS_PRIVATE_INIT (NULL);

/* Implemented by the layout: the widths of a sample string and of the
 * digits in this font. */
void nt_layout_measure_font (NtFont *font, const char *sample,
                             int *char_width, int *digit_width);

NsPangoFontMetrics *
nt_font_get_metrics (NtFont *font)
{
  NsPangoFontMetrics *metrics;
  hb_font_extents_t extents;
  hb_position_t position;
  FcMatrix *fc_matrix;
  int have_transform = FALSE;

  if (!font)
    {
      metrics = nt_font_metrics_new ();
      metrics->ascent = NS_PANGO_SCALE * UNKNOWN_GLYPH_HEIGHT;
      metrics->height = metrics->ascent;
      metrics->approximate_char_width = NS_PANGO_SCALE * 10;
      metrics->approximate_digit_width = NS_PANGO_SCALE * 10;
      metrics->underline_thickness = NS_PANGO_SCALE;
      metrics->underline_position = -NS_PANGO_SCALE;
      metrics->strikethrough_thickness = NS_PANGO_SCALE;
      metrics->strikethrough_position = NS_PANGO_SCALE * UNKNOWN_GLYPH_HEIGHT / 2;
      return metrics;
    }

  ns_mutex_lock (&font->lock);
  metrics = font->metrics;
  ns_mutex_unlock (&font->lock);
  if (metrics)
    return ns_pango_font_metrics_ref (metrics);

  metrics = nt_font_metrics_new ();
  hb_font_get_extents_for_direction (font->hb_font, HB_DIRECTION_LTR, &extents);

  if (FcPatternGetMatrix (font->pattern, FC_MATRIX, 0, &fc_matrix) == FcResultMatch)
    have_transform = (fc_matrix->xx != 1 || fc_matrix->xy != 0 ||
                      fc_matrix->yx != 0 || fc_matrix->yy != 1);
  if (have_transform)
    {
      metrics->descent = (int) (- extents.descender * fc_matrix->yy);
      metrics->ascent = (int) (extents.ascender * fc_matrix->yy);
      metrics->height = (int) ((extents.ascender - extents.descender + extents.line_gap) *
                               fc_matrix->yy);
    }
  else
    {
      metrics->descent = - extents.descender;
      metrics->ascent = extents.ascender;
      metrics->height = extents.ascender - extents.descender + extents.line_gap;
    }

  if (hb_ot_metrics_get_position (font->hb_font, HB_OT_METRICS_TAG_UNDERLINE_SIZE, &position) &&
      position != 0)
    metrics->underline_thickness = position;
  else
    metrics->underline_thickness = NS_PANGO_SCALE;
  if (hb_ot_metrics_get_position (font->hb_font, HB_OT_METRICS_TAG_UNDERLINE_OFFSET, &position) &&
      position != 0)
    metrics->underline_position = position;
  else
    metrics->underline_position = - NS_PANGO_SCALE;
  if (hb_ot_metrics_get_position (font->hb_font, HB_OT_METRICS_TAG_STRIKEOUT_SIZE, &position) &&
      position != 0)
    metrics->strikethrough_thickness = position;
  else
    metrics->strikethrough_thickness = NS_PANGO_SCALE;
  if (hb_ot_metrics_get_position (font->hb_font, HB_OT_METRICS_TAG_STRIKEOUT_OFFSET, &position) &&
      position != 0)
    metrics->strikethrough_position = position;
  else
    metrics->strikethrough_position = metrics->ascent / 2;

  /* Metrics are hinted. */
  metrics->ascent = NS_PANGO_PIXELS_CEIL (metrics->ascent) * NS_PANGO_SCALE;
  metrics->descent = NS_PANGO_PIXELS_CEIL (metrics->descent) * NS_PANGO_SCALE;
  metrics->height = NS_PANGO_PIXELS_CEIL (metrics->height) * NS_PANGO_SCALE;
  metrics->underline_position = NS_PANGO_PIXELS_CEIL (metrics->underline_position) * NS_PANGO_SCALE;
  metrics->underline_thickness = NS_PANGO_PIXELS_CEIL (metrics->underline_thickness) * NS_PANGO_SCALE;
  metrics->strikethrough_position = NS_PANGO_PIXELS_CEIL (metrics->strikethrough_position) * NS_PANGO_SCALE;
  metrics->strikethrough_thickness = NS_PANGO_PIXELS_CEIL (metrics->strikethrough_thickness) * NS_PANGO_SCALE;

  if (!ns_private_get (&in_get_metrics))
    {
      ns_private_set (&in_get_metrics, (void *) 1);
      nt_layout_measure_font (font,
                              nt_language_sample_string (ns_pango_language_get_default ()),
                              &metrics->approximate_char_width,
                              &metrics->approximate_digit_width);
      ns_private_set (&in_get_metrics, NULL);
    }

  ns_mutex_lock (&font->lock);
  if (!font->metrics)
    font->metrics = ns_pango_font_metrics_ref (metrics);
  ns_mutex_unlock (&font->lock);
  return metrics;
}

double
nt_font_get_size (NtFont *font)
{
  return font ? font->size : 0;
}

/* ---- rasterizing ------------------------------------------------------- */

int
nt_font_device_size (NtFont *font, const gfx_matrix_t *g, double *device_size)
{
  double tol = 1e-6 * (fabs (g->xx) + fabs (g->yy));

  if (g->xx <= 0 || fabs (g->xy) > tol || fabs (g->yx) > tol ||
      fabs (g->xx - g->yy) > tol)
    return FALSE;
  *device_size = font->size * g->xx;
  return *device_size > 0 && *device_size < 4096;
}

static void
image_entry_free (void *data)
{
  ImageEntry *entry = data;

  if (entry->image.surface)
    gfx_surface_destroy (entry->image.surface);
  free (entry);
}

static void
image_cache_clear (NtFont *font)
{
  if (font->images)
    nt_hash_remove_all (font->images);
  SDL_AddAtomicInt (&image_total_bytes, - (int) font->image_bytes);
  font->image_bytes = 0;
}

static gfx_surface_t *
surface_from_bitmap (const FT_Bitmap *bitmap, int *color)
{
  gfx_surface_t *surface;
  unsigned char *dst;
  int stride, x, y;
  int w = (int) bitmap->width, h = (int) bitmap->rows;

  *color = bitmap->pixel_mode == FT_PIXEL_MODE_BGRA;
  if (w <= 0 || h <= 0)
    return NULL;
  if (bitmap->pixel_mode != FT_PIXEL_MODE_GRAY &&
      bitmap->pixel_mode != FT_PIXEL_MODE_MONO &&
      bitmap->pixel_mode != FT_PIXEL_MODE_BGRA)
    return NULL;

  surface = gfx_image_surface_create (*color ? GFX_FORMAT_ARGB32 : GFX_FORMAT_A8, w, h);
  if (gfx_surface_status (surface) != GFX_STATUS_SUCCESS)
    {
      gfx_surface_destroy (surface);
      return NULL;
    }
  dst = gfx_image_surface_get_data (surface);
  stride = gfx_image_surface_get_stride (surface);

  for (y = 0; y < h; y++)
    {
      const unsigned char *src = bitmap->buffer + (ptrdiff_t) y * bitmap->pitch;
      unsigned char *row = dst + (size_t) y * (size_t) stride;

      switch (bitmap->pixel_mode)
        {
        case FT_PIXEL_MODE_GRAY:
          if (bitmap->num_grays == 256)
            memcpy (row, src, (size_t) w);
          else
            for (x = 0; x < w; x++)
              row[x] = (unsigned char) (src[x] * 255 / NT_MAX (bitmap->num_grays - 1, 1));
          break;
        case FT_PIXEL_MODE_MONO:
          for (x = 0; x < w; x++)
            row[x] = (src[x >> 3] & (0x80 >> (x & 7))) ? 255 : 0;
          break;
        default: /* BGRA: premultiplied, as gfx's native-endian ARGB32 */
          for (x = 0; x < w; x++)
            {
              uint32_t px = (uint32_t) src[4 * x] |
                            (uint32_t) src[4 * x + 1] << 8 |
                            (uint32_t) src[4 * x + 2] << 16 |
                            (uint32_t) src[4 * x + 3] << 24;
              memcpy (row + 4 * x, &px, 4);
            }
          break;
        }
    }
  gfx_surface_mark_dirty (surface);
  return surface;
}

/* Called with the lock held. */
static int
render_glyph (NtFont *font, uint32_t glyph, FT_F26Dot6 px, int xphase, int yphase,
              NtGlyphImage *image)
{
  FT_Face face = get_face (font);
  FT_GlyphSlot slot;

  memset (image, 0, sizeof *image);
  image->scale = 1.0;
  if (!face)
    return FALSE;

  if (font->render_px != px)
    {
      if (!set_pixel_size (face, font->render_size, px, px, &font->render_scale))
        {
          font->render_px = 0;
          return FALSE;
        }
      font->render_px = px;
    }
  else if (FT_Activate_Size (font->render_size) != 0)
    return FALSE;

  FT_Set_Transform (face, font->has_shape ? &font->shape : NULL, NULL);
  if (FT_Load_Glyph (face, glyph, glyph_load_flags (font, face)) != 0)
    return FALSE;

  slot = face->glyph;
  if (slot->format == FT_GLYPH_FORMAT_OUTLINE)
    {
      if (font->embolden)
        FT_GlyphSlot_Embolden (slot);
      FT_Outline_Translate (&slot->outline, xphase * 16, - yphase * 16);
    }
  if (slot->format != FT_GLYPH_FORMAT_BITMAP &&
      FT_Render_Glyph (slot, FT_RENDER_MODE_NORMAL) != 0)
    return FALSE;

  image->surface = surface_from_bitmap (&slot->bitmap, &image->color);
  image->x = slot->bitmap_left;
  image->y = - slot->bitmap_top;
  image->scale = font->render_scale;
  return TRUE;
}

int
nt_font_get_glyph_image (NtFont *font, uint32_t glyph, double device_size,
                         int xphase, int yphase, NtGlyphImage *image)
{
  FT_F26Dot6 px = (FT_F26Dot6) lround (device_size * 64);
  uint64_t key = ((uint64_t) px << 28) | ((uint64_t) (glyph & 0xFFFFFF) << 4) |
                 ((uint64_t) (xphase & 3) << 2) | (uint64_t) (yphase & 3);
  ImageEntry *entry;

  ns_mutex_lock (&font->lock);
  if (!font->images)
    font->images = nt_hash_new (image_entry_free);

  entry = nt_hash_lookup (font->images, (const char *) &key, sizeof key);
  if (!entry)
    {
      NtGlyphImage fresh;
      size_t bytes;

      if (!render_glyph (font, glyph, px, xphase, yphase, &fresh))
        {
          ns_mutex_unlock (&font->lock);
          return FALSE;
        }
      bytes = sizeof (ImageEntry) + 64;
      if (fresh.surface)
        bytes += (size_t) gfx_image_surface_get_stride (fresh.surface) *
                 (size_t) gfx_image_surface_get_height (fresh.surface);
      if (font->image_bytes + bytes > IMAGE_CACHE_FONT_BYTES ||
          (size_t) SDL_GetAtomicInt (&image_total_bytes) + bytes > IMAGE_CACHE_TOTAL_BYTES)
        image_cache_clear (font);

      entry = nt_new0 (ImageEntry, 1);
      entry->image = fresh;
      entry->bytes = bytes;
      nt_hash_insert (font->images, (const char *) &key, sizeof key, entry);
      font->image_bytes += bytes;
      SDL_AddAtomicInt (&image_total_bytes, (int) bytes);
    }

  *image = entry->image;
  if (image->surface)
    gfx_surface_reference (image->surface);
  ns_mutex_unlock (&font->lock);
  return TRUE;
}

typedef struct
{
  gfx_t       *g;
  gfx_matrix_t m;
  double       last_x, last_y;
  int          open;
} OutlineSink;

static void
sink_point (OutlineSink *sink, const FT_Vector *v, double *x, double *y)
{
  *x = v->x;
  *y = v->y;
  gfx_matrix_transform_point (&sink->m, x, y);
}

static int
sink_move_to (const FT_Vector *to, void *user)
{
  OutlineSink *sink = user;
  double x, y;

  sink_point (sink, to, &x, &y);
  if (sink->open)
    gfx_close_path (sink->g);
  gfx_move_to (sink->g, x, y);
  sink->open = TRUE;
  sink->last_x = x;
  sink->last_y = y;
  return 0;
}

static int
sink_line_to (const FT_Vector *to, void *user)
{
  OutlineSink *sink = user;
  double x, y;

  sink_point (sink, to, &x, &y);
  gfx_line_to (sink->g, x, y);
  sink->last_x = x;
  sink->last_y = y;
  return 0;
}

static int
sink_conic_to (const FT_Vector *control, const FT_Vector *to, void *user)
{
  OutlineSink *sink = user;
  double cx, cy, x, y;

  sink_point (sink, control, &cx, &cy);
  sink_point (sink, to, &x, &y);
  gfx_curve_to (sink->g,
                sink->last_x + 2.0 / 3.0 * (cx - sink->last_x),
                sink->last_y + 2.0 / 3.0 * (cy - sink->last_y),
                x + 2.0 / 3.0 * (cx - x),
                y + 2.0 / 3.0 * (cy - y),
                x, y);
  sink->last_x = x;
  sink->last_y = y;
  return 0;
}

static int
sink_cubic_to (const FT_Vector *c1, const FT_Vector *c2, const FT_Vector *to, void *user)
{
  OutlineSink *sink = user;
  double x1, y1, x2, y2, x, y;

  sink_point (sink, c1, &x1, &y1);
  sink_point (sink, c2, &x2, &y2);
  sink_point (sink, to, &x, &y);
  gfx_curve_to (sink->g, x1, y1, x2, y2, x, y);
  sink->last_x = x;
  sink->last_y = y;
  return 0;
}

void
nt_font_append_glyph_path (NtFont *font, gfx_t *g, uint32_t glyph, double x, double y)
{
  static const FT_Outline_Funcs funcs = {
    sink_move_to, sink_line_to, sink_conic_to, sink_cubic_to, 0, 0
  };
  OutlineSink sink;
  FT_Face face;
  FT_Outline *outline;
  gfx_matrix_t font_matrix;
  double upem;

  ns_mutex_lock (&font->lock);
  face = get_face (font);
  if (!face || !FT_IS_SCALABLE (face) || face->units_per_EM == 0)
    goto done;

  FT_Set_Transform (face, NULL, NULL);
  if (FT_Load_Glyph (face, glyph, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING |
                                  FT_LOAD_NO_BITMAP) != 0 ||
      face->glyph->format != FT_GLYPH_FORMAT_OUTLINE)
    goto done;

  outline = &face->glyph->outline;
  if (font->embolden)
    FT_Outline_Embolden (outline, face->units_per_EM / 24);

  /* fontconfig's matrix flipped into y-down font space, the em scaled. */
  gfx_matrix_init (&font_matrix,
                   font->shape.xx / 65536.0, - font->shape.yx / 65536.0,
                   - font->shape.xy / 65536.0, font->shape.yy / 65536.0, 0, 0);
  gfx_matrix_scale (&font_matrix, font->size, font->size);

  upem = face->units_per_EM;
  gfx_matrix_init (&sink.m, 1 / upem, 0, 0, -1 / upem, 0, 0);
  gfx_matrix_multiply (&sink.m, &sink.m, &font_matrix);
  sink.m.x0 += x;
  sink.m.y0 += y;
  sink.g = g;
  sink.last_x = x;
  sink.last_y = y;
  sink.open = FALSE;

  if (outline->n_contours > 0)
    {
      FT_Outline_Decompose (outline, &funcs, &sink);
      if (sink.open)
        gfx_close_path (g);
    }

done:
  ns_mutex_unlock (&font->lock);
}

/* ---- fonts ------------------------------------------------------------- */

NtFont *
nt_font_ref (NtFont *font)
{
  if (font)
    SDL_AddAtomicInt ((SDL_AtomicInt *) (void *) &font->ref_count, 1);
  return font;
}

static void
font_free (NtFont *font)
{
  image_cache_clear (font);
  nt_hash_free (font->images);
  if (font->hexbox)
    {
      nt_font_unref (font->hexbox->font);
      free (font->hexbox);
    }
  free (font->extents_cache);
  ns_pango_font_metrics_unref (font->metrics);
  if (font->face)
    {
      ns_mutex_lock (&ft_library_lock);
      FT_Done_Face (font->face);
      ns_mutex_unlock (&ft_library_lock);
    }
  hb_font_destroy (font->hb_font);
  hb_face_destroy (font->hb_face);
  FcPatternDestroy (font->pattern);
  free (font->key);
  ns_mutex_clear (&font->lock);
  free (font);
}

void
nt_font_unref (NtFont *font)
{
  if (font && SDL_AddAtomicInt ((SDL_AtomicInt *) (void *) &font->ref_count, -1) == 1)
    font_free (font);
}

int
nt_font_has_char (NtFont *font, nt_unichar wc)
{
  if (!font)
    return FALSE;
  if (font->charset)
    return FcCharSetHasChar (font->charset, wc);
  {
    hb_codepoint_t g;

    return hb_font_get_nominal_glyph (font->hb_font, wc, &g);
  }
}

static void
matrix_font_scale_factors (double xx, double yx, double xy, double yy,
                           double *xscale, double *yscale)
{
  double major = sqrt (xx * xx + yx * yx), minor;

  if (major)
    minor = fabs (xx * yy - yx * xy) / major;
  else
    minor = 0;
  *xscale = major;
  *yscale = minor;
}

/* Applies "wght=700,wdth=80"-like settings to the coordinates. */
static void
parse_variations (const char *variations, const hb_ot_var_axis_info_t *axes,
                  unsigned int n_axes, float *coords)
{
  const char *p = variations;

  while (p && *p)
    {
      const char *end = strchr (p, ',');
      hb_variation_t var;
      size_t len = end ? (size_t) (end - p) : strlen (p);

      if (hb_variation_from_string (p, (int) len, &var))
        {
          unsigned int i;

          for (i = 0; i < n_axes; i++)
            if (axes[i].tag == var.tag)
              {
                float v = var.value;

                if (v < axes[i].min_value)
                  v = axes[i].min_value;
                if (v > axes[i].max_value)
                  v = axes[i].max_value;
                coords[axes[i].axis_index] = v;
                break;
              }
        }
      p = end ? end + 1 : NULL;
    }
}

static hb_face_t *
font_map_get_hb_face (NsPangoFontMap *fontmap, const char *file, int index)
{
  char key[1100];
  hb_face_t *face;
  int n = snprintf (key, sizeof key, "%s#%d", file, index);

  if (n < 0 || (size_t) n >= sizeof key)
    return hb_face_get_empty ();
  face = nt_hash_lookup (fontmap->faces, key, (size_t) n);
  if (!face)
    {
      hb_blob_t *blob = NULL;

#ifdef NS_FC_INTREE
      size_t bundled_len = 0;
      const void *bundled = NsFcBundledFontData ((const FcChar8 *) file, &bundled_len);

      if (bundled)
        blob = hb_blob_create ((const char *) bundled, (unsigned int) bundled_len,
                               HB_MEMORY_MODE_READONLY, NULL, NULL);
      else
#endif
        blob = hb_blob_create_from_file (file);
      face = hb_face_create (blob, (unsigned int) (index & 0xFFFF));
      hb_blob_destroy (blob);
      nt_hash_insert (fontmap->faces, key, (size_t) n, face);
    }
  return hb_face_reference (face);
}

/* A font for a rendered pattern; desc_variations are the description's. */
static NtFont *
font_new (NsPangoFontMap *fontmap, FcPattern *pattern, const char *desc_variations,
          double ctm_sx, double ctm_sy)
{
  NtFont *font;
  FcChar8 *file = NULL;
  int index = 0;
  FcMatrix fc_matrix, *fc_matrix_val;
  FcBool b;
  double pixel_size, point_size, dpi, x, y, x_scale, y_scale, fc_scale;
  NtString key = { 0 };
  int i;
  FcCharSet *cs = NULL;

  if (FcPatternGetString (pattern, FC_FILE, 0, &file) != FcResultMatch || !file)
    return NULL;
  FcPatternGetInteger (pattern, FC_INDEX, 0, &index);

  if (FcPatternGetDouble (pattern, FC_SIZE, 0, &point_size) != FcResultMatch)
    point_size = 13.;
  if (FcPatternGetDouble (pattern, FC_PIXEL_SIZE, 0, &pixel_size) != FcResultMatch)
    {
      if (FcPatternGetDouble (pattern, FC_DPI, 0, &dpi) != FcResultMatch)
        dpi = 72.;
      pixel_size = point_size * dpi / 72.;
    }

  FcMatrixInit (&fc_matrix);
  for (i = 0; FcPatternGetMatrix (pattern, FC_MATRIX, i, &fc_matrix_val) == FcResultMatch; i++)
    FcMatrixMultiply (&fc_matrix, &fc_matrix, fc_matrix_val);

  nt_string_printf (&key, "%s|%d|%.4f|%.6f,%.6f,%.6f,%.6f|%.4f,%.4f|%s|", (const char *) file,
                    index, pixel_size, fc_matrix.xx, fc_matrix.xy, fc_matrix.yx,
                    fc_matrix.yy, ctm_sx, ctm_sy, desc_variations ? desc_variations : "");
  {
    FcChar8 *v = NULL;
    int hs = -1;
    FcBool emb = FcFalse, hinting = FcTrue, autohint = FcFalse, bitmaps = FcFalse;

    FcPatternGetString (pattern, FC_FONT_VARIATIONS, 0, &v);
    FcPatternGetInteger (pattern, FC_HINT_STYLE, 0, &hs);
    FcPatternGetBool (pattern, FC_EMBOLDEN, 0, &emb);
    FcPatternGetBool (pattern, FC_HINTING, 0, &hinting);
    FcPatternGetBool (pattern, FC_AUTOHINT, 0, &autohint);
    FcPatternGetBool (pattern, FC_EMBEDDED_BITMAP, 0, &bitmaps);
    nt_string_printf (&key, "%s|%d|%d%d%d%d", v ? (const char *) v : "", hs, emb,
                      hinting, autohint, bitmaps);
  }

  font = nt_hash_lookup (fontmap->fonts, key.str, key.len);
  if (font)
    {
      free (key.str);
      return nt_font_ref (font);
    }

  font = nt_new0 (NtFont, 1);
  font->ref_count = 1;
  font->fontmap = fontmap;
  font->key = key.str;
  FcPatternReference (pattern);
  font->pattern = pattern;
  if (FcPatternGetCharSet (pattern, FC_CHARSET, 0, &cs) == FcResultMatch)
    font->charset = cs;

  font->embolden = FcPatternGetBool (pattern, FC_EMBOLDEN, 0, &b) == FcResultMatch && b;
  font->load_flags = pattern_load_flags (pattern);

  /* fontconfig scales a bitmap font (colour emoji) with a matrix; its
   * uniform scale goes into the size, its shape stays a transform. */
  fc_scale = sqrt (fabs (fc_matrix.xx * fc_matrix.yy - fc_matrix.xy * fc_matrix.yx));
  /* The pattern's pixel size includes the context's scale; the em in user
   * space does not. */
  font->size = pixel_size / ctm_sy;
  font->ctm_sx = ctm_sx;
  font->ctm_sy = ctm_sy;
  {
    FcMatrix shape = fc_matrix;

    if (fc_scale > 0 && fabs (fc_scale - 1.0) > 1e-6)
      {
        font->size *= fc_scale;
        shape.xx /= fc_scale;
        shape.xy /= fc_scale;
        shape.yx /= fc_scale;
        shape.yy /= fc_scale;
      }
    font->shape.xx = (FT_Fixed) lround (shape.xx * 65536.0);
    font->shape.xy = (FT_Fixed) lround (shape.xy * 65536.0);
    font->shape.yx = (FT_Fixed) lround (shape.yx * 65536.0);
    font->shape.yy = (FT_Fixed) lround (shape.yy * 65536.0);
    font->has_shape = font->shape.xx != 0x10000 || font->shape.xy != 0 ||
                      font->shape.yx != 0 || font->shape.yy != 0x10000;
  }

  /* The HarfBuzz font, as pangofc makes it: the pattern's pixel size in
   * Pango units, the context's and fontconfig's scales divided out, the
   * slant and the emboldening synthesized. */
  matrix_font_scale_factors (fc_matrix.xx, - fc_matrix.yx, fc_matrix.xy, - fc_matrix.yy,
                             &x, &y);
  x_scale = x / ctm_sx;
  y_scale = y / ctm_sy;
  font->hb_face = font_map_get_hb_face (fontmap, (const char *) file, index);
  font->hb_font = hb_font_create (font->hb_face);
  hb_font_set_scale (font->hb_font,
                     (int) (pixel_size * NS_PANGO_SCALE * x_scale),
                     (int) (pixel_size * NS_PANGO_SCALE * y_scale));
  hb_font_set_ptem (font->hb_font, (float) point_size);
  {
    /* The slant ratio of the y-down font matrix. */
    double fxx = fc_matrix.xx, fyx = -fc_matrix.yx, fxy = fc_matrix.xy, fyy = -fc_matrix.yy;
    double x0 = fxy, y0 = fyy, x1 = fxx, y1 = fyx;
    double slant = (x0 * x0 + y0 * y0) != 0 ? (x0 * x1 + y0 * y1) / (x0 * x0 + y0 * y0) : 0;

    font->slant = slant;
#if HB_VERSION_ATLEAST (3, 3, 0)
    hb_font_set_synthetic_slant (font->hb_font, (float) slant);
#endif
  }
#if HB_VERSION_ATLEAST (7, 0, 0)
  if (font->embolden)
    hb_font_set_synthetic_bold (font->hb_font, 1.f / 24, 1.f / 24, FALSE);
#endif

  {
    unsigned int n_axes = hb_ot_var_get_axis_infos (font->hb_face, 0, NULL, NULL);

    if (n_axes > 0)
      {
        hb_ot_var_axis_info_t *axes = nt_new0 (hb_ot_var_axis_info_t, n_axes);
        float *coords = nt_new (float, n_axes);
        FcChar8 *variations = NULL;
        unsigned int k;

        hb_ot_var_get_axis_infos (font->hb_face, 0, &n_axes, axes);
        for (k = 0; k < n_axes; k++)
          {
            coords[axes[k].axis_index] = axes[k].default_value;
            if (axes[k].tag == HB_TAG ('o', 'p', 's', 'z'))
              coords[axes[k].axis_index] = (float) point_size;
          }
        if (index != 0 && (index >> 16) > 0)
          {
            unsigned int instance = (unsigned int) (index >> 16) - 1;

            hb_ot_var_named_instance_get_design_coords (font->hb_face, instance, &n_axes, coords);
          }
        if (FcPatternGetString (pattern, FC_FONT_VARIATIONS, 0, &variations) == FcResultMatch)
          parse_variations ((const char *) variations, axes, n_axes, coords);
        if (desc_variations)
          parse_variations (desc_variations, axes, n_axes, coords);
        hb_font_set_var_coords_design (font->hb_font, coords, n_axes);
        free (coords);
        free (axes);
      }
  }

  font->is_color = hb_ot_color_has_png (font->hb_face) || hb_ot_color_has_layers (font->hb_face) ||
                   hb_ot_color_has_paint (font->hb_face);

  if (getenv ("NSTEXT_DEBUG"))
    {
      hb_font_extents_t e;

      hb_font_get_h_extents (font->hb_font, &e);
      fprintf (stderr, "nstext: font %s size %.3f asc %d desc %d gap %d\n", font->key,
               font->size, e.ascender, e.descender, e.line_gap);
    }
  nt_hash_insert (fontmap->fonts, font->key, strlen (font->key), nt_font_ref (font));
  return font;
}

/* ---- the font map ------------------------------------------------------ */

static int
is_supported_font_format (FcPattern *pattern)
{
  FcChar8 *s;

  if (FcPatternGetString (pattern, FC_FILE, 0, &s) != FcResultMatch)
    return FALSE;
  if (FcPatternGetString (pattern, FC_FONT_WRAPPER, 0, &s) == FcResultMatch)
    return strcmp ((const char *) s, "SFNT") == 0;
  if (FcPatternGetString (pattern, FC_FONTFORMAT, 0, &s) != FcResultMatch)
    return FALSE;
  return nt_ascii_strcasecmp ((const char *) s, "TrueType") == 0 ||
         nt_ascii_strcasecmp ((const char *) s, "CFF") == 0;
}

static FcPattern *
pattern_set_order (FcPattern *pat, int order)
{
  int o;

  if (FcPatternGetInteger (pat, FC_ORDER, 0, &o) == FcResultMatch && o == order)
    {
      FcPatternReference (pat);
      return pat;
    }
  pat = FcPatternDuplicate (pat);
  FcPatternRemove (pat, FC_ORDER, 0);
  FcPatternAddInteger (pat, FC_ORDER, order);
  return pat;
}

/* The SFNT fonts of fontconfig's system and application sets. */
static FcFontSet *
font_map_all_fonts (NsPangoFontMap *fontmap)
{
  if (!fontmap->all_fonts)
    {
      FcFontSet *sets[2];
      FcFontSet *result = FcFontSetCreate ();
      int set, i;

      sets[0] = FcConfigGetFonts (NULL, FcSetSystem);
      sets[1] = FcConfigGetFonts (NULL, FcSetApplication);
      for (set = 0; set < 2; set++)
        {
          if (!sets[set])
            continue;
          for (i = 0; i < sets[set]->nfont; i++)
            {
              FcPattern *pat = sets[set]->fonts[i];

              if (is_supported_font_format (pat))
                FcFontSetAdd (result, pattern_set_order (pat, set));
            }
        }
      fontmap->all_fonts = result;
    }
  return fontmap->all_fonts;
}

struct _NtFontset
{
  int             ref_count;
  NsPangoFontMap *fontmap;
  FcPattern      *pattern;   /* the substituted search pattern */
  FcPattern      *match;     /* FcFontSetMatch's, rendered */
  FcFontSet      *sorted;    /* FcFontSetSort's, made on demand */
  int             sort_done;
  char           *variations;
  double          ctm_sx, ctm_sy;
  NtFont        **fonts;
  int             n_fonts, cap_fonts;
  int             exhausted;
  NtHash         *char_cache; /* char -> font position + 1 */
};

static void
fontset_free (NtFontset *fs)
{
  int i;

  for (i = 0; i < fs->n_fonts; i++)
    nt_font_unref (fs->fonts[i]);
  free (fs->fonts);
  if (fs->match)
    FcPatternDestroy (fs->match);
  if (fs->sorted)
    FcFontSetDestroy (fs->sorted);
  FcPatternDestroy (fs->pattern);
  free (fs->variations);
  nt_hash_free (fs->char_cache);
  free (fs);
}

NtFontset *
nt_fontset_ref (NtFontset *fs)
{
  if (fs)
    SDL_AddAtomicInt ((SDL_AtomicInt *) (void *) &fs->ref_count, 1);
  return fs;
}

void
nt_fontset_unref (NtFontset *fs)
{
  if (fs && SDL_AddAtomicInt ((SDL_AtomicInt *) (void *) &fs->ref_count, -1) == 1)
    fontset_free (fs);
}

static void
fontset_unref_void (void *p)
{
  nt_fontset_unref (p);
}

static void
font_unref_void (void *p)
{
  nt_font_unref (p);
}

static void
face_destroy_void (void *p)
{
  hb_face_destroy (p);
}

static double
stretch_to_fc_width (NsPangoStretch stretch)
{
  static const double widths[] = { 50, 62.5, 75, 87.5, 100, 112.5, 125, 150, 200 };

  if ((unsigned) stretch < NT_N_ELEMENTS (widths))
    return widths[stretch];
  return 100;
}

static int
style_to_fc_slant (NsPangoStyle style)
{
  switch (style)
    {
    case NS_PANGO_STYLE_ITALIC: return FC_SLANT_ITALIC;
    case NS_PANGO_STYLE_OBLIQUE: return FC_SLANT_OBLIQUE;
    case NS_PANGO_STYLE_NORMAL:
    default: return FC_SLANT_ROMAN;
    }
}

/* The size in Pango units of device pixels, as the fontset key's
 * (pangofc's get_scaled_size: the context's scale on the font's height). */
static int
desc_pixel_size (const NsPangoFontDescription *desc, double dpi, double ctm_sy)
{
  double size = (desc->mask & NT_DESC_SIZE) ? desc->size : 0;

  if (desc->mask & NT_DESC_SIZE && !desc->size_is_absolute)
    size = size * dpi / 72.;
  return (int) (.5 + ctm_sy * size);
}

static FcPattern *
make_pattern (const NsPangoFontDescription *desc, NsPangoLanguage *language,
              int pixel_size, double dpi)
{
  FcPattern *pattern = FcPatternCreate ();
  const char *family = (desc->mask & NT_DESC_FAMILY) ? desc->family : NULL;

  FcPatternAddDouble (pattern, FC_WEIGHT, FcWeightFromOpenTypeDouble (desc->weight));
  FcPatternAddInteger (pattern, FC_SLANT, style_to_fc_slant (desc->style));
  FcPatternAddDouble (pattern, FC_WIDTH, stretch_to_fc_width (desc->stretch));
  FcPatternAddBool (pattern, FC_VARIABLE, FcDontCare);
  FcPatternAddDouble (pattern, FC_DPI, dpi);
  FcPatternAddDouble (pattern, FC_SIZE, pixel_size * (72. / 1024. / dpi));
  FcPatternAddDouble (pattern, FC_PIXEL_SIZE, pixel_size / 1024.);
  FcPatternAddInteger (pattern, FC_ORDER, 1000);
  FcPatternAddBool (pattern, FC_COLOR, desc->color);

  if (desc->variations)
    FcPatternAddString (pattern, FC_FONT_VARIATIONS, (const FcChar8 *) desc->variations);

  if (family)
    {
      const char *p = family;

      while (*p)
        {
          const char *end = strchr (p, ',');
          size_t len = end ? (size_t) (end - p) : strlen (p);
          char *one = nt_strndup (p, len);

          FcPatternAddString (pattern, FC_FAMILY, (const FcChar8 *) one);
          free (one);
          if (!end)
            break;
          p = end + 1;
        }
    }

  if (language)
    FcPatternAddString (pattern, FC_LANG, (const FcChar8 *) language->tag);

  return pattern;
}

static NtFontset *
font_map_load_fontset_scaled (NsPangoFontMap *fontmap, const NsPangoFontDescription *desc,
                              NsPangoLanguage *language, double ctm_sx, double ctm_sy)
{
  NtString key = { 0 };
  NtFontset *fs;
  int pixel_size = desc_pixel_size (desc, fontmap->dpi, ctm_sy);
  FcResult result;

  nt_string_printf (&key, "%s|%d|%d|%d|%d|%d|%d|%s|%s|%.4f,%.4f",
                    (desc->mask & NT_DESC_FAMILY) && desc->family ? desc->family : "",
                    desc->style, desc->weight, desc->stretch, desc->variant, desc->color,
                    pixel_size,
                    desc->variations ? desc->variations : "",
                    language ? language->tag : "", ctm_sx, ctm_sy);

  ns_mutex_lock (&fontmap->lock);
  fs = nt_hash_lookup (fontmap->fontsets, key.str, key.len);
  if (fs)
    {
      nt_fontset_ref (fs);
      ns_mutex_unlock (&fontmap->lock);
      free (key.str);
      return fs;
    }

  fs = nt_new0 (NtFontset, 1);
  fs->ref_count = 1;
  fs->fontmap = fontmap;
  fs->ctm_sx = ctm_sx;
  fs->ctm_sy = ctm_sy;
  fs->variations = nt_strdup (desc->variations);
  fs->pattern = make_pattern (desc, language, pixel_size, fontmap->dpi);
  FcConfigSubstitute (NULL, fs->pattern, FcMatchPattern);
  FcDefaultSubstitute (fs->pattern);

  {
    FcFontSet *all = font_map_all_fonts (fontmap);
    /* The current configuration, named: fontconfig's FcFontSetSort (2.17)
     * reads config->prefer_app_fonts without looking up a NULL config, as
     * FcFontSort does; the set functions are given it alike. */
    FcConfig *config = FcConfigReference (NULL);

    fs->match = FcFontSetMatch (config, &all, 1, fs->pattern, &result);
    FcConfigDestroy (config);
  }
  fs->char_cache = nt_hash_new (NULL);

  nt_hash_insert (fontmap->fontsets, key.str, key.len, nt_fontset_ref (fs));
  ns_mutex_unlock (&fontmap->lock);
  free (key.str);
  return fs;
}

NtFontset *
nt_font_map_load_fontset (NsPangoFontMap *fontmap, const NsPangoFontDescription *desc,
                          NsPangoLanguage *language)
{
  return font_map_load_fontset_scaled (fontmap, desc, language, 1.0, 1.0);
}

NtFont *
nt_fontset_get_font_at (NtFontset *fs, int i)
{
  NsPangoFontMap *fontmap = fs->fontmap;

  ns_mutex_lock (&fontmap->lock);
  while (i >= fs->n_fonts && !fs->exhausted)
    {
      FcPattern *font_pattern = NULL;
      NtFont *font = NULL;
      int prepared = FALSE;

      if (fs->n_fonts == 0 && fs->match)
        font_pattern = fs->match;
      else
        {
          if (!fs->sort_done)
            {
              FcFontSet *all = font_map_all_fonts (fontmap);
              FcConfig *config = FcConfigReference (NULL); /* see above */
              FcResult result;

              fs->sorted = config ? FcFontSetSort (config, &all, 1, fs->pattern,
                                                   FcTrue, NULL, &result)
                                  : NULL;
              FcConfigDestroy (config);
              fs->sort_done = TRUE;
            }
          /* The sorted list's first is the match. */
          if (fs->sorted && fs->n_fonts < fs->sorted->nfont)
            {
              font_pattern = FcFontRenderPrepare (NULL, fs->pattern,
                                                  fs->sorted->fonts[fs->n_fonts]);
              prepared = TRUE;
            }
        }
      if (font_pattern)
        font = font_new (fontmap, font_pattern, fs->variations, fs->ctm_sx, fs->ctm_sy);
      if (prepared)
        FcPatternDestroy (font_pattern);
      if (!font_pattern)
        {
          fs->exhausted = TRUE;
          break;
        }
      if (fs->n_fonts == fs->cap_fonts)
        {
          fs->cap_fonts = fs->cap_fonts ? fs->cap_fonts * 2 : 8;
          fs->fonts = nt_renew (NtFont *, fs->fonts, fs->cap_fonts);
        }
      fs->fonts[fs->n_fonts++] = font; /* may be NULL: a font that did not load */
    }
  ns_mutex_unlock (&fontmap->lock);
  return i < fs->n_fonts ? fs->fonts[i] : NULL;
}

NtFont *
nt_fontset_get_font (NtFontset *fs, nt_unichar wc, int *position)
{
  uintptr_t cached;
  int i;

  ns_mutex_lock (&fs->fontmap->lock);
  cached = (uintptr_t) nt_hash_lookup (fs->char_cache, (const char *) &wc, sizeof wc);
  ns_mutex_unlock (&fs->fontmap->lock);
  if (cached)
    {
      *position = (int) cached - 1;
      return nt_fontset_get_font_at (fs, (int) cached - 1);
    }

  for (i = 0; ; i++)
    {
      NtFont *font = nt_fontset_get_font_at (fs, i);

      if (!font)
        {
          if (i >= fs->n_fonts)
            break; /* past the end */
          continue;
        }
      if (nt_font_has_char (font, wc))
        {
          *position = i;
          ns_mutex_lock (&fs->fontmap->lock);
          nt_hash_insert (fs->char_cache, (const char *) &wc, sizeof wc,
                          (void *) (uintptr_t) (i + 1));
          ns_mutex_unlock (&fs->fontmap->lock);
          return font;
        }
    }
  *position = i;
  return NULL;
}

NtFont *
nt_font_map_load_font (NsPangoFontMap *fontmap, const NsPangoFontDescription *desc,
                       NsPangoLanguage *language)
{
  NtFontset *fs = nt_font_map_load_fontset (fontmap, desc, language);
  NtFont *font = NULL;
  int i;

  for (i = 0; i < 4 && !font; i++)
    font = nt_fontset_get_font_at (fs, i);
  nt_font_ref (font);
  nt_fontset_unref (fs);
  return font;
}

static void
font_map_free (void *data)
{
  NsPangoFontMap *fontmap = data;
  int i;

  nt_hash_free (fontmap->fontsets);
  nt_hash_free (fontmap->fonts);
  nt_hash_free (fontmap->faces);
  for (i = 0; i < fontmap->n_families; i++)
    {
      free (fontmap->families[i]->name);
      free (fontmap->families[i]);
    }
  free (fontmap->families);
  if (fontmap->all_fonts)
    FcFontSetDestroy (fontmap->all_fonts);
  ns_mutex_clear (&fontmap->lock);
  free (fontmap);
}

static NsPrivate default_font_map = NS_PRIVATE_INIT (font_map_free);

NsPangoFontMap *
ns_pango_gfx_font_map_get_default (void)
{
  NsPangoFontMap *fontmap = ns_private_get (&default_font_map);

  if (!fontmap)
    {
      fontmap = nt_new0 (NsPangoFontMap, 1);
      fontmap->serial = 1;
      fontmap->dpi = 96.0;
      fontmap->fontsets = nt_hash_new (fontset_unref_void);
      fontmap->fonts = nt_hash_new (font_unref_void);
      fontmap->faces = nt_hash_new (face_destroy_void);
      FcInit ();
      ns_private_set (&default_font_map, fontmap);
    }
  return fontmap;
}

unsigned int
ns_pango_font_map_get_serial (NsPangoFontMap *fontmap)
{
  return fontmap ? fontmap->serial : 0;
}

static void
font_map_clear_families (NsPangoFontMap *fontmap)
{
  int i;

  for (i = 0; i < fontmap->n_families; i++)
    {
      free (fontmap->families[i]->name);
      free (fontmap->families[i]);
    }
  free (fontmap->families);
  fontmap->families = NULL;
  fontmap->n_families = 0;
}

void
ns_pango_fc_font_map_config_changed (NsPangoFcFontMap *fontmap)
{
  if (!fontmap)
    return;
  ns_mutex_lock (&fontmap->lock);
  /* Fonts in use stay alive through their references; new lookups see
   * fontconfig's new fonts. */
  nt_hash_remove_all (fontmap->fontsets);
  nt_hash_remove_all (fontmap->fonts);
  if (fontmap->all_fonts)
    {
      FcFontSetDestroy (fontmap->all_fonts);
      fontmap->all_fonts = NULL;
    }
  font_map_clear_families (fontmap);
  fontmap->serial++;
  if (fontmap->serial == 0)
    fontmap->serial++;
  ns_mutex_unlock (&fontmap->lock);
}

static int
family_cmp (const void *a, const void *b)
{
  const NsPangoFontFamily *fa = *(NsPangoFontFamily *const *) a;
  const NsPangoFontFamily *fb = *(NsPangoFontFamily *const *) b;

  return nt_ascii_strcasecmp (fa->name, fb->name);
}

void
ns_pango_font_map_list_families (NsPangoFontMap *fontmap, NsPangoFontFamily ***families,
                                 int *n_families)
{
  ns_mutex_lock (&fontmap->lock);
  if (!fontmap->families)
    {
      FcFontSet *all = font_map_all_fonts (fontmap);
      NtHash *seen = nt_hash_new (NULL);
      int i, cap = 64;

      fontmap->families = nt_new (NsPangoFontFamily *, cap);
      for (i = 0; i < all->nfont; i++)
        {
          FcChar8 *name = NULL;
          char lower[256];
          size_t n, k;

          if (FcPatternGetString (all->fonts[i], FC_FAMILY, 0, &name) != FcResultMatch || !name)
            continue;
          n = strlen ((const char *) name);
          if (n >= sizeof lower)
            continue;
          for (k = 0; k < n; k++)
            lower[k] = nt_ascii_tolower ((char) name[k]);
          if (nt_hash_lookup (seen, lower, n))
            continue;
          nt_hash_insert (seen, lower, n, (void *) 1);
          if (fontmap->n_families == cap)
            {
              cap *= 2;
              fontmap->families = nt_renew (NsPangoFontFamily *, fontmap->families, cap);
            }
          fontmap->families[fontmap->n_families] = nt_new0 (NsPangoFontFamily, 1);
          fontmap->families[fontmap->n_families]->name = nt_strdup ((const char *) name);
          fontmap->n_families++;
        }
      nt_hash_free (seen);
      qsort (fontmap->families, (size_t) fontmap->n_families, sizeof *fontmap->families,
             family_cmp);
    }
  if (families)
    {
      *families = nt_new (NsPangoFontFamily *, fontmap->n_families + 1);
      memcpy (*families, fontmap->families,
              (size_t) fontmap->n_families * sizeof **families);
    }
  if (n_families)
    *n_families = fontmap->n_families;
  ns_mutex_unlock (&fontmap->lock);
}

const char *
ns_pango_font_family_get_name (NsPangoFontFamily *family)
{
  return family ? family->name : NULL;
}

/* ---- contexts ---------------------------------------------------------- */

NsPangoContext *
ns_pango_gfx_create_context (gfx_t *g)
{
  NsPangoContext *c = nt_new0 (NsPangoContext, 1);

  c->ref_count = 1;
  c->fontmap = ns_pango_gfx_font_map_get_default ();
  c->font_desc = ns_pango_font_description_from_string ("serif 12");
  c->language = ns_pango_language_get_default ();
  c->base_dir = NS_PANGO_DIRECTION_WEAK_LTR;
  c->scale_x = c->scale_y = 1.0;
  c->serial = 1;
  c->fontmap_serial = c->fontmap->serial;
  c->metrics = nt_hash_new ((void (*) (void *)) ns_pango_font_metrics_unref);
  if (g)
    ns_pango_gfx_update_context (g, c);
  return c;
}

void
ns_pango_gfx_update_context (gfx_t *g, NsPangoContext *context)
{
  gfx_matrix_t gm;
  double sx, sy;

  if (!context)
    return;
  if (g)
    gfx_get_matrix (g, &gm);
  else
    gfx_matrix_init_identity (&gm);
  matrix_font_scale_factors (gm.xx, gm.yx, gm.xy, gm.yy, &sx, &sy);
  if (!(sx > 0) || !(sy > 0))
    sx = sy = 1.0;
  if (sx != context->scale_x || sy != context->scale_y)
    {
      context->scale_x = sx;
      context->scale_y = sy;
      context->serial++;
      nt_hash_remove_all (context->metrics);
    }
}

NsPangoContext *
ns_pango_context_ref (NsPangoContext *context)
{
  if (context)
    SDL_AddAtomicInt ((SDL_AtomicInt *) (void *) &context->ref_count, 1);
  return context;
}

void
ns_pango_context_unref (NsPangoContext *context)
{
  if (context && SDL_AddAtomicInt ((SDL_AtomicInt *) (void *) &context->ref_count, -1) == 1)
    {
      ns_pango_font_description_free (context->font_desc);
      nt_hash_free (context->metrics);
      free (context);
    }
}

void
nt_context_check_fontmap (NsPangoContext *context)
{
  if (context->fontmap_serial != context->fontmap->serial)
    {
      context->fontmap_serial = context->fontmap->serial;
      context->serial++;
      nt_hash_remove_all (context->metrics);
    }
}

void
ns_pango_context_set_base_dir (NsPangoContext *context, NsPangoDirection direction)
{
  if (context->base_dir != direction)
    {
      context->base_dir = direction;
      context->serial++;
    }
}

NsPangoDirection
ns_pango_context_get_base_dir (NsPangoContext *context)
{
  return context->base_dir;
}

NsPangoFontDescription *
ns_pango_context_get_font_description (NsPangoContext *context)
{
  return context->font_desc;
}

NsPangoLayout *
ns_pango_gfx_create_layout (gfx_t *g)
{
  NsPangoContext *context = ns_pango_gfx_create_context (g);
  NsPangoLayout *layout = ns_pango_layout_new (context);

  ns_pango_context_unref (context);
  return layout;
}

/* The font scale of the context's matrix, for fontsets: the size is the
 * device size, the fonts' metrics are hinted at it. */
NtFontset *nt_context_load_fontset (NsPangoContext *context, const NsPangoFontDescription *desc,
                                    NsPangoLanguage *language);

NtFontset *
nt_context_load_fontset (NsPangoContext *context, const NsPangoFontDescription *desc,
                         NsPangoLanguage *language)
{
  return font_map_load_fontset_scaled (context->fontmap, desc, language,
                                       context->scale_x, context->scale_y);
}

static NsPangoStretch
fc_width_to_stretch (int width)
{
  switch (width)
    {
    case FC_WIDTH_ULTRACONDENSED: return NS_PANGO_STRETCH_ULTRA_CONDENSED;
    case FC_WIDTH_EXTRACONDENSED: return NS_PANGO_STRETCH_EXTRA_CONDENSED;
    case FC_WIDTH_CONDENSED: return NS_PANGO_STRETCH_CONDENSED;
    case FC_WIDTH_SEMICONDENSED: return NS_PANGO_STRETCH_SEMI_CONDENSED;
    case FC_WIDTH_SEMIEXPANDED: return NS_PANGO_STRETCH_SEMI_EXPANDED;
    case FC_WIDTH_EXPANDED: return NS_PANGO_STRETCH_EXPANDED;
    case FC_WIDTH_EXTRAEXPANDED: return NS_PANGO_STRETCH_EXTRA_EXPANDED;
    case FC_WIDTH_ULTRAEXPANDED: return NS_PANGO_STRETCH_ULTRA_EXPANDED;
    case FC_WIDTH_NORMAL:
    default: return NS_PANGO_STRETCH_NORMAL;
    }
}

/* The font described as pangofc describes it (font_description_from_pattern:
 * the family, slant, weight and width the font's pattern was rendered with,
 * its point size, which includes the context's scale, and its variations),
 * scaled, and loaded again through the context in its language, as Pango's
 * apply_scale_to_item does for a font scale. */
NtFont *
nt_context_load_scaled_font (NsPangoContext *context, NtFont *font, NsPangoVariant variant,
                             double scale)
{
  NsPangoFontDescription *desc = ns_pango_font_description_new ();
  FcChar8 *s;
  FcMatrix *fc_matrix;
  int i;
  double d;
  NtFontset *fs;
  NtFont *scaled = NULL;

  if (FcPatternGetString (font->pattern, FC_FAMILY, 0, &s) == FcResultMatch)
    ns_pango_font_description_set_family (desc, (const char *) s);
  if (FcPatternGetInteger (font->pattern, FC_SLANT, 0, &i) == FcResultMatch)
    ns_pango_font_description_set_style (desc, i == FC_SLANT_ITALIC ? NS_PANGO_STYLE_ITALIC
                                               : i == FC_SLANT_OBLIQUE ? NS_PANGO_STYLE_OBLIQUE
                                               : NS_PANGO_STYLE_NORMAL);
  else
    ns_pango_font_description_set_style (desc, NS_PANGO_STYLE_NORMAL);
  if (FcPatternGetDouble (font->pattern, FC_WEIGHT, 0, &d) == FcResultMatch)
    ns_pango_font_description_set_weight (desc, (NsPangoWeight) FcWeightToOpenTypeDouble (d));
  else
    ns_pango_font_description_set_weight (desc, NS_PANGO_WEIGHT_NORMAL);
  if (FcPatternGetInteger (font->pattern, FC_WIDTH, 0, &i) == FcResultMatch)
    ns_pango_font_description_set_stretch (desc, fc_width_to_stretch (i));
  else
    ns_pango_font_description_set_stretch (desc, NS_PANGO_STRETCH_NORMAL);
  ns_pango_font_description_set_variant (desc, variant);

  if (FcPatternGetDouble (font->pattern, FC_SIZE, 0, &d) == FcResultMatch)
    {
      double scale_factor = 1, sx;
      volatile double scaled_size;

      if (FcPatternGetMatrix (font->pattern, FC_MATRIX, 0, &fc_matrix) == FcResultMatch)
        matrix_font_scale_factors (fc_matrix->xx, fc_matrix->yx, fc_matrix->xy, fc_matrix->yy,
                                   &sx, &scale_factor);
      scaled_size = scale_factor * d * NS_PANGO_SCALE;
      ns_pango_font_description_set_size (desc, (int) scaled_size);
      ns_pango_font_description_set_size (desc, (int) (scale * desc->size));
    }

  if (FcPatternGetString (font->pattern, FC_FONT_VARIATIONS, 0, &s) == FcResultMatch &&
      s && *s)
    ns_pango_font_description_set_variations (desc, (const char *) s);

  fs = nt_context_load_fontset (context, desc, context->language);
  for (i = 0; i < 4 && !scaled; i++)
    scaled = nt_fontset_get_font_at (fs, i);
  nt_font_ref (scaled);
  nt_fontset_unref (fs);
  ns_pango_font_description_free (desc);
  return scaled;
}
