/* Pango
 * pangogfx-font.c: fontconfig fonts of the gfx backend, rasterized by FreeType
 *
 * Derived from pangocairo-font.c and pangocairo-fcfont.c. Metrics and
 * extents follow what cairo's FreeType backend reported to pangocairo
 * with metrics hinting on (its default for image surfaces), so text lays
 * out as it did: line metrics are rounded to whole pixels, advances come
 * from HarfBuzz rounded to whole pixels by the shaper, and ink extents are
 * FreeType's grid-fitted ones.
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

#include "config.h"

#include <math.h>
#include <string.h>

#include "pangogfx-private.h"

/* Glyph extents are cached in a small table indexed by the glyph's low
 * bits, as pangocairo did. */
#define EXTENTS_CACHE_SIZE 256 /* a power of two */

/* Rasterized glyphs: a font drops its cache when it would hold more than
 * this, or when all fonts together would. */
#define IMAGE_CACHE_FONT_BYTES  ((gsize) 2 << 20)
#define IMAGE_CACHE_TOTAL_BYTES ((gsize) 48 << 20)

typedef struct
{
  NsPangoGlyph     glyph;
  int              width;
  NsPangoRectangle ink;
} ExtentsEntry;

typedef struct
{
  const char         *sample_str;
  NsPangoFontMetrics *metrics;
} MetricsInfo;

typedef struct
{
  guint64              key;
  NsPangoGfxGlyphImage image;
  gsize                bytes;
} ImageEntry;

struct _NsPangoGfxFont
{
  NsPangoFcFont parent_instance;

  /* Guards the FreeType face and the caches: a layout made on one thread
   * may be drawn on another. */
  GMutex lock;

  /* Font space (y down, the em is 1) to user space, as pangocairo's font
   * matrix: the em size, fontconfig's matrix and the gravity's rotation. */
  gfx_matrix_t   font_matrix;
  double         size;          /* the em in user space */
  NsPangoGravity gravity;
  double         ctm_sx, ctm_sy; /* the context's scale, for metrics */
  FT_Matrix      shape;          /* fontconfig's matrix (synthetic oblique) */
  gboolean       has_shape;
  gboolean       embolden;
  FT_Int32       load_flags;

  hb_blob_t     *blob;
  FT_Face        face;
  gboolean       face_failed;
  FT_Size        metric_size;   /* at the layout scale */
  double         metric_scale;  /* bitmap strike scale at that size */
  FT_Size        render_size;   /* at the last device size drawn */
  FT_F26Dot6     render_px;
  double         render_scale;

  gboolean         extents_ready;
  NsPangoRectangle font_extents;
  ExtentsEntry    *extents_cache;

  GSList               *metrics_by_lang;
  NsPangoGfxHexBoxInfo *hbi;

  GHashTable *images;
  gsize       image_bytes;
};

typedef struct
{
  NsPangoFcFontClass parent_class;
} NsPangoGfxFontClass;

G_DEFINE_TYPE (NsPangoGfxFont, ns_pango_gfx_font, NS_TYPE_PANGO_FC_FONT)

/* ---- FreeType ---------------------------------------------------------- */

static FT_Library ft_library;
static GMutex     ft_library_lock; /* face creation and destruction */
static gsize      image_total_bytes;

static FT_Library
get_ft_library (void)
{
  static gsize once;

  if (g_once_init_enter (&once))
    {
      if (FT_Init_FreeType (&ft_library) != 0)
        ft_library = NULL;
      g_once_init_leave (&once, 1);
    }

  return ft_library;
}

static double
pattern_pixel_size (const FcPattern *pattern)
{
  double size, dpi;

  if (FcPatternGetDouble (pattern, FC_PIXEL_SIZE, 0, &size) == FcResultMatch)
    return size;

  if (FcPatternGetDouble (pattern, FC_DPI, 0, &dpi) != FcResultMatch)
    dpi = 72;

  if (FcPatternGetDouble (pattern, FC_SIZE, 0, &size) == FcResultMatch)
    return size * dpi / 72.;

  return 18.;
}

static NsPangoGravity
pattern_gravity (const FcPattern *pattern)
{
  FcChar8 *s;

  if (FcPatternGetString (pattern, NS_PANGO_FC_GRAVITY, 0, &s) == FcResultMatch)
    {
      GEnumValue *value = g_enum_get_value_by_nick (get_gravity_class (),
                                                    (const char *) s);
      if (value)
        return (NsPangoGravity) value->value;
    }

  return NS_PANGO_GRAVITY_SOUTH;
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

/* Sets an FT_Size to a pixel size; a face without outlines gets its
 * nearest strike, and *scale is what its bitmaps must be scaled by. */
static gboolean
set_pixel_size (FT_Face     face,
                FT_Size     size,
                FT_F26Dot6  x_px,
                FT_F26Dot6  y_px,
                double     *scale)
{
  *scale = 1.0;

  if (FT_Activate_Size (size) != 0)
    return FALSE;

  if (FT_IS_SCALABLE (face))
    return FT_Set_Char_Size (face, MAX (x_px, 1), MAX (y_px, 1), 0, 0) == 0;

  if (face->num_fixed_sizes > 0)
    {
      int best = 0, i;

      for (i = 1; i < face->num_fixed_sizes; i++)
        {
          FT_Pos have = face->available_sizes[best].y_ppem;
          FT_Pos cand = face->available_sizes[i].y_ppem;

          /* The smallest strike at least as large, else the largest. */
          if ((have < y_px && cand > have) ||
              (cand >= y_px && cand < have))
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

/* The FreeType face, opened on first use from the bytes HarfBuzz already
 * maps, with the same variation coordinates. Called with the lock held. */
static FT_Face
get_face (NsPangoGfxFont *font)
{
  NsPangoFcFont *fcfont = (NsPangoFcFont *) font;
  FT_Library library;
  hb_font_t *hb_font;
  const char *data;
  unsigned int length = 0;
  int index = 0;
  FT_Face face = NULL;

  if (font->face || font->face_failed)
    return font->face;

  font->face_failed = TRUE;

  library = get_ft_library ();
  hb_font = ns_pango_font_get_hb_font (NS_PANGO_FONT (font));
  if (!library || !hb_font)
    return NULL;

  font->blob = hb_face_reference_blob (hb_font_get_face (hb_font));
  data = hb_blob_get_data (font->blob, &length);
  if (!data || length == 0)
    return NULL;

  FcPatternGetInteger (fcfont->font_pattern, FC_INDEX, 0, &index);

  g_mutex_lock (&ft_library_lock);
  if (FT_New_Memory_Face (library, (const FT_Byte *) data, (FT_Long) length,
                          index, &face) != 0)
    face = NULL;
  if (face &&
      (FT_New_Size (face, &font->metric_size) != 0 ||
       FT_New_Size (face, &font->render_size) != 0))
    {
      FT_Done_Face (face);
      face = NULL;
    }
  g_mutex_unlock (&ft_library_lock);

  if (!face)
    return NULL;

  if (FT_HAS_MULTIPLE_MASTERS (face))
    {
      unsigned int n_coords = 0;
      const float *coords = hb_font_get_var_coords_design (hb_font, &n_coords);

      if (coords && n_coords > 0)
        {
          FT_Fixed *fixed = g_new (FT_Fixed, n_coords);
          unsigned int i;

          for (i = 0; i < n_coords; i++)
            fixed[i] = (FT_Fixed) lround (coords[i] * 65536.0);
          FT_Set_Var_Design_Coordinates (face, n_coords, fixed);
          g_free (fixed);
        }
    }

  if (!set_pixel_size (face, font->metric_size,
                       (FT_F26Dot6) lround (font->size * font->ctm_sx * 64),
                       (FT_F26Dot6) lround (font->size * font->ctm_sy * 64),
                       &font->metric_scale))
    {
      g_mutex_lock (&ft_library_lock);
      FT_Done_Face (face);
      g_mutex_unlock (&ft_library_lock);
      return NULL;
    }

  font->face = face;
  font->face_failed = FALSE;

  return face;
}

static FT_Int32
glyph_load_flags (NsPangoGfxFont *font,
                  FT_Face         face)
{
  FT_Int32 flags = font->load_flags;

  if (FT_HAS_COLOR (face))
    flags |= FT_LOAD_COLOR;

  return flags;
}

/* ---- extents ----------------------------------------------------------- */

static void
init_font_extents (NsPangoGfxFont *font)
{
  hb_font_extents_t extents;

  hb_font_get_h_extents (ns_pango_font_get_hb_font (NS_PANGO_FONT (font)),
                         &extents);

  font->font_extents.x = 0;
  font->font_extents.width = 0;
  font->font_extents.height = extents.ascender - extents.descender;

  switch (font->gravity)
    {
    default:
    case NS_PANGO_GRAVITY_AUTO:
    case NS_PANGO_GRAVITY_SOUTH:
      font->font_extents.y = - extents.ascender;
      break;
    case NS_PANGO_GRAVITY_NORTH:
      font->font_extents.y = extents.descender;
      break;
    case NS_PANGO_GRAVITY_EAST:
    case NS_PANGO_GRAVITY_WEST:
      font->font_extents.y = - NS_PANGO_UNITS_ROUND (font->font_extents.height / 2);
      break;
    }

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

  if (NS_PANGO_GRAVITY_IS_IMPROPER (font->gravity))
    {
      font->font_extents.y = - font->font_extents.y;
      font->font_extents.height = - font->font_extents.height;
    }

  font->extents_cache = g_new0 (ExtentsEntry, EXTENTS_CACHE_SIZE);
  /* Glyph 1 cannot land in bucket 0, so every entry starts out invalid. */
  font->extents_cache[0].glyph = 1;
  font->extents_ready = TRUE;
}

/* HarfBuzz's (unhinted) extents, for a face FreeType cannot open. */
static void
hb_glyph_extents (NsPangoGfxFont *font,
                  NsPangoGlyph    glyph,
                  ExtentsEntry   *entry)
{
  hb_font_t *hb_font = ns_pango_font_get_hb_font (NS_PANGO_FONT (font));
  hb_glyph_extents_t ext;

  entry->width = hb_font_get_glyph_h_advance (hb_font, glyph);
  if (hb_font_get_glyph_extents (hb_font, glyph, &ext))
    {
      entry->ink.x = ext.x_bearing;
      entry->ink.y = - ext.y_bearing;
      entry->ink.width = ext.width;
      entry->ink.height = - ext.height;
    }
}

/* What cairo's FreeType backend reported: the hinted outline's grid-fitted
 * box and advance, at the layout's scale, in user space. */
static void
compute_glyph_extents (NsPangoGfxFont *font,
                       NsPangoGlyph    glyph,
                       ExtentsEntry   *entry)
{
  FT_Face face;
  FT_GlyphSlot slot;
  const FT_Glyph_Metrics *m;
  double xf, yf;
  double x_bearing, y_bearing, width, height, advance;

  memset (entry, 0, sizeof *entry);
  entry->glyph = glyph;

  face = get_face (font);
  if (!face || font->gravity != NS_PANGO_GRAVITY_SOUTH)
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

  entry->width = ns_pango_units_from_double (advance);
  entry->ink.x = ns_pango_units_from_double (x_bearing);
  entry->ink.y = ns_pango_units_from_double (y_bearing);
  entry->ink.width = ns_pango_units_from_double (width);
  entry->ink.height = ns_pango_units_from_double (height);
}

static void get_glyph_extents_missing (NsPangoGfxFont   *font,
                                       NsPangoGlyph      glyph,
                                       NsPangoRectangle *ink_rect,
                                       NsPangoRectangle *logical_rect);

static void
ns_pango_gfx_font_get_glyph_extents (NsPangoFont      *pfont,
                                     NsPangoGlyph      glyph,
                                     NsPangoRectangle *ink_rect,
                                     NsPangoRectangle *logical_rect)
{
  NsPangoGfxFont *font = (NsPangoGfxFont *) pfont;
  ExtentsEntry entry;

  if ((glyph & NS_PANGO_GLYPH_UNKNOWN_FLAG) && glyph != NS_PANGO_GLYPH_EMPTY)
    {
      get_glyph_extents_missing (font, glyph, ink_rect, logical_rect);
      return;
    }

  g_mutex_lock (&font->lock);

  if (!font->extents_ready)
    init_font_extents (font);

  if (glyph == NS_PANGO_GLYPH_EMPTY)
    {
      if (ink_rect)
        ink_rect->x = ink_rect->y = ink_rect->width = ink_rect->height = 0;
      if (logical_rect)
        *logical_rect = font->font_extents;
      g_mutex_unlock (&font->lock);
      return;
    }

  {
    ExtentsEntry *slot = &font->extents_cache[glyph & (EXTENTS_CACHE_SIZE - 1)];

    if (slot->glyph != glyph)
      compute_glyph_extents (font, glyph, slot);
    entry = *slot;
  }

  if (ink_rect)
    *ink_rect = entry.ink;
  if (logical_rect)
    {
      *logical_rect = font->font_extents;
      switch (font->gravity)
        {
        case NS_PANGO_GRAVITY_EAST:
          logical_rect->width = font->font_extents.height;
          logical_rect->x = - logical_rect->width;
          break;
        case NS_PANGO_GRAVITY_WEST:
          logical_rect->width = - font->font_extents.height;
          logical_rect->x = - logical_rect->width;
          break;
        case NS_PANGO_GRAVITY_SOUTH:
        case NS_PANGO_GRAVITY_NORTH:
        case NS_PANGO_GRAVITY_AUTO:
        default:
          logical_rect->width = entry.width;
          break;
        }
    }

  g_mutex_unlock (&font->lock);
}

/* ---- hex boxes --------------------------------------------------------- */

/* The em box of the font at the layout scale, in user space: FreeType's
 * rounded ascender and descender. */
static void
font_ascent_descent (NsPangoGfxFont *font,
                     double         *ascent,
                     double         *descent)
{
  FT_Face face;

  g_mutex_lock (&font->lock);
  face = get_face (font);
  if (face)
    {
      double yf = font->metric_scale / (64.0 * font->ctm_sy);

      *ascent = font->metric_size->metrics.ascender * yf;
      *descent = - font->metric_size->metrics.descender * yf;
    }
  else
    {
      *ascent = NS_PANGO_UNKNOWN_GLYPH_HEIGHT;
      *descent = 0;
    }
  g_mutex_unlock (&font->lock);
}

NsPangoGfxHexBoxInfo *
_ns_pango_gfx_font_get_hex_box_info (NsPangoGfxFont *font)
{
  static const char hexdigits[] = "0123456789ABCDEF";
  NsPangoGfxHexBoxInfo *hbi;
  NsPangoFontDescription *desc;
  NsPangoFontMap *fontmap;
  NsPangoContext *context;
  NsPangoFont *mini_font;
  NsPangoMatrix ctm = NS_PANGO_MATRIX_INIT;
  NsPangoMatrix pfm = NS_PANGO_MATRIX_INIT;
  NsPangoFcFontKey *key;
  const NsPangoMatrix *key_ctm;
  double scale_x = 1., scale_x_inv = 1., scale_y = 1., scale_y_inv = 1.;
  double size, mini_size, pad, width = 0, height = 0;
  double ascent, descent;
  hb_font_t *mini_hb;
  int i;

  g_mutex_lock (&font->lock);
  hbi = font->hbi;
  g_mutex_unlock (&font->lock);
  if (hbi)
    return hbi;

  key = _ns_pango_fc_font_get_font_key ((NsPangoFcFont *) font);
  key_ctm = key ? ns_pango_fc_font_key_get_matrix (key) : NULL;
  if (key_ctm)
    {
      ctm = *key_ctm;
      ctm.x0 = ctm.y0 = 0;
    }
  pfm.xx = font->font_matrix.xx;
  pfm.yx = font->font_matrix.yx;
  pfm.xy = font->font_matrix.xy;
  pfm.yy = font->font_matrix.yy;

  size = ns_pango_matrix_get_font_scale_factor (&pfm) /
         ns_pango_matrix_get_font_scale_factor (&ctm);

  /* Hint to whole device pixels. */
  scale_x = sqrt (ctm.xx * ctm.xx + ctm.yx * ctm.yx);
  scale_y = sqrt (ctm.xy * ctm.xy + ctm.yy * ctm.yy);
  if (scale_x <= 0 || scale_y <= 0)
    scale_x = scale_y = 1.;
  scale_x_inv = 1 / scale_x;
  scale_y_inv = 1 / scale_y;

#define HINT(value, scale, scale_inv) (ceil (((value) - 1e-5) * (scale)) * (scale_inv))
#define HINT_X(value) HINT ((value), scale_x, scale_x_inv)
#define HINT_Y(value) HINT ((value), scale_y, scale_y_inv)

  fontmap = ns_pango_font_get_font_map (NS_PANGO_FONT (font));
  if (!fontmap)
    return NULL;
  fontmap = g_object_ref (fontmap);

  desc = ns_pango_font_describe_with_absolute_size (NS_PANGO_FONT (font));
  ns_pango_font_description_unset_fields (desc, NS_PANGO_FONT_MASK_GRAVITY);
  ns_pango_font_description_set_family_static (desc, "monospace");
  mini_size = HINT_Y (size / 2.2);
  ns_pango_font_description_set_absolute_size (desc, ns_pango_units_from_double (mini_size));

  context = ns_pango_font_map_create_context (fontmap);
  ns_pango_context_set_matrix (context, &ctm);
  ns_pango_context_set_language (context, ns_pango_script_get_sample_language (NS_PANGO_SCRIPT_LATIN));
  mini_font = ns_pango_font_map_load_font (fontmap, context, desc);
  g_object_unref (context);
  g_object_unref (fontmap);
  ns_pango_font_description_free (desc);

  if (!mini_font)
    return NULL;

  mini_hb = ns_pango_font_get_hb_font (mini_font);
  for (i = 0; i < 16; i++)
    {
      hb_codepoint_t g;
      NsPangoRectangle ink;

      if (!mini_hb || !hb_font_get_nominal_glyph (mini_hb, (hb_codepoint_t) hexdigits[i], &g))
        continue;
      ns_pango_font_get_glyph_extents (mini_font, g, &ink, NULL);
      width = MAX (width, (double) ink.width / NS_PANGO_SCALE);
      height = MAX (height, (double) ink.height / NS_PANGO_SCALE);
    }

  font_ascent_descent (font, &ascent, &descent);
  if (ascent + descent <= 0)
    {
      ascent = NS_PANGO_UNKNOWN_GLYPH_HEIGHT;
      descent = 0;
    }

  pad = (ascent + descent) / 43;
  pad = MIN (pad, mini_size);

  hbi = g_new0 (NsPangoGfxHexBoxInfo, 1);
  hbi->font = mini_font;
  hbi->rows = 2;
  hbi->digit_width = HINT_X (width);
  hbi->digit_height = HINT_Y (height);
  hbi->pad_x = HINT_X (pad);
  hbi->pad_y = HINT_Y (pad);
  hbi->line_width = MIN (hbi->pad_x, hbi->pad_y);
  hbi->box_height = 3 * hbi->pad_y + hbi->rows * (hbi->pad_y + hbi->digit_height);

  if (hbi->box_height <= ascent)
    hbi->box_descent = 2 * hbi->pad_y;
  else if (hbi->box_height <= ascent + descent - 2 * hbi->pad_y)
    hbi->box_descent = 2 * hbi->pad_y + hbi->box_height - ascent;
  else
    hbi->box_descent = descent * hbi->box_height / (ascent + descent);
  hbi->box_descent = HINT_Y (hbi->box_descent);

#undef HINT_Y
#undef HINT_X
#undef HINT

  g_mutex_lock (&font->lock);
  if (!font->hbi)
    font->hbi = hbi;
  else
    {
      g_object_unref (hbi->font);
      g_free (hbi);
      hbi = font->hbi;
    }
  g_mutex_unlock (&font->lock);

  return hbi;
}

static void
get_space_extents (NsPangoGfxFont   *font,
                   NsPangoRectangle *ink_rect,
                   NsPangoRectangle *logical_rect)
{
  /* See https://docs.microsoft.com/en-us/typography/develop/character-design-standards/whitespace */
  int width = ns_pango_font_get_absolute_size (NS_PANGO_FONT (font)) / 4;

  if (ink_rect)
    {
      ink_rect->x = ink_rect->y = ink_rect->height = 0;
      ink_rect->width = width;
    }
  if (logical_rect)
    {
      g_mutex_lock (&font->lock);
      if (!font->extents_ready)
        init_font_extents (font);
      *logical_rect = font->font_extents;
      g_mutex_unlock (&font->lock);
      logical_rect->width = width;
    }
}

static void
get_glyph_extents_missing (NsPangoGfxFont   *font,
                           NsPangoGlyph      glyph,
                           NsPangoRectangle *ink_rect,
                           NsPangoRectangle *logical_rect)
{
  NsPangoGfxHexBoxInfo *hbi;
  gunichar ch = glyph & ~NS_PANGO_GLYPH_UNKNOWN_FLAG;
  int rows, cols;

  if (ch == 0x20 || ch == 0x2423)
    {
      get_space_extents (font, ink_rect, logical_rect);
      return;
    }

  hbi = _ns_pango_gfx_font_get_hex_box_info (font);
  if (!hbi)
    {
      ns_pango_font_get_glyph_extents (NULL, glyph, ink_rect, logical_rect);
      return;
    }

  if (G_UNLIKELY (glyph == NS_PANGO_GLYPH_INVALID_INPUT || ch > 0x10FFFF))
    {
      rows = hbi->rows;
      cols = 1;
    }
  else if (ns_pango_get_ignorable_size (ch, &rows, &cols))
    {
      /* Default ignorables get their nick in the box. */
    }
  else
    {
      rows = hbi->rows;
      cols = (ch > 0xffff ? 6 : 4) / rows;
    }

  if (ink_rect)
    {
      ink_rect->x = NS_PANGO_SCALE * hbi->pad_x;
      ink_rect->y = NS_PANGO_SCALE * (hbi->box_descent - hbi->box_height);
      ink_rect->width = NS_PANGO_SCALE * (4 * hbi->pad_x + cols * (hbi->digit_width + hbi->pad_x));
      ink_rect->height = NS_PANGO_SCALE * hbi->box_height;
    }

  if (logical_rect)
    {
      logical_rect->x = 0;
      logical_rect->y = NS_PANGO_SCALE * (hbi->box_descent - (hbi->box_height + hbi->pad_y));
      logical_rect->width = NS_PANGO_SCALE * (5 * hbi->pad_x + cols * (hbi->digit_width + hbi->pad_x));
      logical_rect->height = NS_PANGO_SCALE * (hbi->box_height + 2 * hbi->pad_y);
    }
}

/* ---- metrics ----------------------------------------------------------- */

static int
max_glyph_width (NsPangoLayout *layout)
{
  int max_width = 0;
  GSList *l, *r;

  for (l = ns_pango_layout_get_lines_readonly (layout); l; l = l->next)
    {
      NsPangoLayoutLine *line = l->data;

      for (r = line->runs; r; r = r->next)
        {
          NsPangoGlyphString *glyphs = ((NsPangoGlyphItem *) r->data)->glyphs;
          int i;

          for (i = 0; i < glyphs->num_glyphs; i++)
            if (glyphs->glyphs[i].geometry.width > max_width)
              max_width = glyphs->glyphs[i].geometry.width;
        }
    }

  return max_width;
}

static NsPangoFontMetrics *
ns_pango_gfx_font_get_metrics (NsPangoFont     *pfont,
                               NsPangoLanguage *language)
{
  NsPangoGfxFont *font = (NsPangoGfxFont *) pfont;
  /* Measuring the approximate widths lays text out, which asks for metrics
   * again; the guard against that is per thread (see pangocairo-font.c's
   * history: a process-wide flag let one thread skip another's widths). */
  static GPrivate in_get_metrics_key;
  gboolean in_get_metrics = g_private_get (&in_get_metrics_key) != NULL;
  const char *sample_str = ns_pango_language_get_sample_string (language);
  MetricsInfo *info = NULL;
  GSList *tmp_list;

  for (tmp_list = font->metrics_by_lang; tmp_list; tmp_list = tmp_list->next)
    {
      info = tmp_list->data;
      if (info->sample_str == sample_str) /* We _don't_ need strcmp */
        break;
    }

  if (!tmp_list)
    {
      NsPangoFontMap *fontmap;
      NsPangoContext *context;
      NsPangoFontMetrics *metrics;
      int height, shift;

      fontmap = ns_pango_font_get_font_map (pfont);
      if (!fontmap)
        return ns_pango_font_metrics_new ();
      fontmap = g_object_ref (fontmap);

      info = g_new0 (MetricsInfo, 1);
      font->metrics_by_lang = g_slist_prepend (font->metrics_by_lang, info);
      info->sample_str = sample_str;

      context = ns_pango_font_map_create_context (fontmap);
      ns_pango_context_set_language (context, language);

      metrics = ns_pango_fc_font_create_base_metrics_for_context ((NsPangoFcFont *) font,
                                                                  context);
      /* Metrics are hinted. */
      metrics->ascent = NS_PANGO_PIXELS_CEIL (metrics->ascent) * NS_PANGO_SCALE;
      metrics->descent = NS_PANGO_PIXELS_CEIL (metrics->descent) * NS_PANGO_SCALE;
      metrics->height = NS_PANGO_PIXELS_CEIL (metrics->height) * NS_PANGO_SCALE;
      metrics->underline_position = NS_PANGO_PIXELS_CEIL (metrics->underline_position) * NS_PANGO_SCALE;
      metrics->underline_thickness = NS_PANGO_PIXELS_CEIL (metrics->underline_thickness) * NS_PANGO_SCALE;
      metrics->strikethrough_position = NS_PANGO_PIXELS_CEIL (metrics->strikethrough_position) * NS_PANGO_SCALE;
      metrics->strikethrough_thickness = NS_PANGO_PIXELS_CEIL (metrics->strikethrough_thickness) * NS_PANGO_SCALE;
      info->metrics = metrics;

      if (!in_get_metrics)
        {
          NsPangoLayout *layout;
          NsPangoRectangle extents;
          NsPangoFontDescription *desc;
          glong sample_str_width;

          g_private_set (&in_get_metrics_key, GINT_TO_POINTER (1));

          layout = ns_pango_layout_new (context);
          desc = ns_pango_font_describe_with_absolute_size (pfont);
          ns_pango_layout_set_font_description (layout, desc);
          ns_pango_font_description_free (desc);

          ns_pango_layout_set_text (layout, sample_str, -1);
          ns_pango_layout_get_extents (layout, NULL, &extents);

          sample_str_width = ns_pango_utf8_strwidth (sample_str);
          if (sample_str_width > 0)
            metrics->approximate_char_width = (int) (extents.width / sample_str_width);

          ns_pango_layout_set_text (layout, "0123456789", -1);
          metrics->approximate_digit_width = max_glyph_width (layout);

          g_object_unref (layout);
          g_private_set (&in_get_metrics_key, NULL);
        }

      height = metrics->ascent + metrics->descent;
      switch (font->gravity)
        {
        default:
        case NS_PANGO_GRAVITY_AUTO:
        case NS_PANGO_GRAVITY_SOUTH:
          break;
        case NS_PANGO_GRAVITY_NORTH:
          metrics->ascent = metrics->descent;
          break;
        case NS_PANGO_GRAVITY_EAST:
        case NS_PANGO_GRAVITY_WEST:
          metrics->ascent = NS_PANGO_UNITS_ROUND (height / 2);
          break;
        }
      shift = (height - metrics->ascent) - metrics->descent;
      metrics->descent += shift;
      metrics->underline_position -= shift;
      metrics->strikethrough_position -= shift;
      metrics->ascent = height - metrics->descent;

      g_object_unref (context);
      g_object_unref (fontmap);
    }

  return ns_pango_font_metrics_ref (info->metrics);
}

/* ---- rasterizing ------------------------------------------------------- */

gboolean
_ns_pango_gfx_font_device_size (NsPangoGfxFont     *font,
                                const gfx_matrix_t *user_to_device,
                                double             *device_size)
{
  gfx_matrix_t g;
  double tol;

  /* The gravity's rotation, then user space to the device. */
  gfx_matrix_init_rotate (&g, ns_pango_gravity_to_rotation (font->gravity));
  gfx_matrix_multiply (&g, &g, user_to_device);

  tol = 1e-6 * (fabs (g.xx) + fabs (g.yy));
  if (g.xx <= 0 || fabs (g.xy) > tol || fabs (g.yx) > tol ||
      fabs (g.xx - g.yy) > tol)
    return FALSE;

  *device_size = font->size * g.xx;
  return *device_size > 0 && *device_size < 4096;
}

static void
image_entry_free (gpointer data)
{
  ImageEntry *entry = data;

  if (entry->image.surface)
    gfx_surface_destroy (entry->image.surface);
  g_free (entry);
}

static void
image_cache_clear (NsPangoGfxFont *font)
{
  if (font->images)
    g_hash_table_remove_all (font->images);
  g_atomic_pointer_add (&image_total_bytes, - (gssize) font->image_bytes);
  font->image_bytes = 0;
}

/* Copies a FreeType bitmap into a gfx surface. */
static gfx_surface_t *
surface_from_bitmap (const FT_Bitmap *bitmap,
                     gboolean        *color)
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
              row[x] = (unsigned char) (src[x] * 255 / MAX (bitmap->num_grays - 1, 1));
          break;
        case FT_PIXEL_MODE_MONO:
          for (x = 0; x < w; x++)
            row[x] = (src[x >> 3] & (0x80 >> (x & 7))) ? 255 : 0;
          break;
        default: /* BGRA: premultiplied, as gfx's native-endian ARGB32 */
          for (x = 0; x < w; x++)
            {
              guint32 px = (guint32) src[4 * x] |
                           (guint32) src[4 * x + 1] << 8 |
                           (guint32) src[4 * x + 2] << 16 |
                           (guint32) src[4 * x + 3] << 24;
              memcpy (row + 4 * x, &px, 4);
            }
          break;
        }
    }

  gfx_surface_mark_dirty (surface);
  return surface;
}

/* Rasterizes a glyph. Called with the lock held. */
static gboolean
render_glyph (NsPangoGfxFont       *font,
              NsPangoGlyph          glyph,
              FT_F26Dot6            px,
              int                   xphase,
              int                   yphase,
              NsPangoGfxGlyphImage *image)
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
      /* The phase moves the origin right and down; FreeType's y is up. */
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

gboolean
_ns_pango_gfx_font_get_glyph_image (NsPangoGfxFont       *font,
                                    NsPangoGlyph          glyph,
                                    double                device_size,
                                    int                   xphase,
                                    int                   yphase,
                                    NsPangoGfxGlyphImage *image)
{
  FT_F26Dot6 px = (FT_F26Dot6) lround (device_size * 64);
  guint64 key = ((guint64) px << 28) | ((guint64) (glyph & 0xFFFFFF) << 4) |
                ((guint64) (xphase & 3) << 2) | (guint64) (yphase & 3);
  ImageEntry *entry;
  gsize bytes;

  g_mutex_lock (&font->lock);

  if (!font->images)
    font->images = g_hash_table_new_full (g_int64_hash, g_int64_equal,
                                          NULL, image_entry_free);

  entry = g_hash_table_lookup (font->images, &key);
  if (!entry)
    {
      NsPangoGfxGlyphImage fresh;

      if (!render_glyph (font, glyph, px, xphase, yphase, &fresh))
        {
          g_mutex_unlock (&font->lock);
          return FALSE;
        }

      bytes = sizeof (ImageEntry) + 64;
      if (fresh.surface)
        bytes += (gsize) gfx_image_surface_get_stride (fresh.surface) *
                 (gsize) gfx_image_surface_get_height (fresh.surface);

      if (font->image_bytes + bytes > IMAGE_CACHE_FONT_BYTES ||
          (gsize) g_atomic_pointer_get (&image_total_bytes) + bytes > IMAGE_CACHE_TOTAL_BYTES)
        image_cache_clear (font);

      entry = g_new0 (ImageEntry, 1);
      entry->key = key;
      entry->image = fresh;
      entry->bytes = bytes;
      g_hash_table_insert (font->images, &entry->key, entry);
      font->image_bytes += bytes;
      g_atomic_pointer_add (&image_total_bytes, (gssize) bytes);
    }

  *image = entry->image;
  if (image->surface)
    gfx_surface_reference (image->surface);

  g_mutex_unlock (&font->lock);
  return TRUE;
}

/* ---- outlines ---------------------------------------------------------- */

typedef struct
{
  gfx_t       *g;
  gfx_matrix_t m;     /* font units (y up) to user space */
  double       last_x, last_y;
  gboolean     open;  /* a contour has been started */
} OutlineSink;

static void
sink_point (OutlineSink    *sink,
            const FT_Vector *v,
            double          *x,
            double          *y)
{
  *x = v->x;
  *y = v->y;
  gfx_matrix_transform_point (&sink->m, x, y);
}

static int
sink_move_to (const FT_Vector *to,
              void            *user)
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
sink_line_to (const FT_Vector *to,
              void            *user)
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
sink_conic_to (const FT_Vector *control,
               const FT_Vector *to,
               void            *user)
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
sink_cubic_to (const FT_Vector *control1,
               const FT_Vector *control2,
               const FT_Vector *to,
               void            *user)
{
  OutlineSink *sink = user;
  double x1, y1, x2, y2, x, y;

  sink_point (sink, control1, &x1, &y1);
  sink_point (sink, control2, &x2, &y2);
  sink_point (sink, to, &x, &y);
  gfx_curve_to (sink->g, x1, y1, x2, y2, x, y);
  sink->last_x = x;
  sink->last_y = y;
  return 0;
}

void
_ns_pango_gfx_font_append_glyph_path (NsPangoGfxFont *font,
                                      gfx_t          *g,
                                      NsPangoGlyph    glyph,
                                      double          x,
                                      double          y)
{
  static const FT_Outline_Funcs funcs = {
    sink_move_to, sink_line_to, sink_conic_to, sink_cubic_to, 0, 0
  };
  OutlineSink sink;
  FT_Face face;
  FT_Outline *outline;
  double upem;

  g_mutex_lock (&font->lock);

  face = get_face (font);
  if (!face || !FT_IS_SCALABLE (face) || face->units_per_EM == 0)
    goto done;

  /* Unscaled and unhinted: the outline as designed, in font units. */
  FT_Set_Transform (face, NULL, NULL);
  if (FT_Load_Glyph (face, glyph, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING |
                                  FT_LOAD_NO_BITMAP) != 0 ||
      face->glyph->format != FT_GLYPH_FORMAT_OUTLINE)
    goto done;

  outline = &face->glyph->outline;
  if (font->embolden)
    FT_Outline_Embolden (outline, face->units_per_EM / 24);

  /* Font units (y up) to font space (y down, the em is 1), to user space
   * at the glyph origin. */
  upem = face->units_per_EM;
  gfx_matrix_init (&sink.m, 1 / upem, 0, 0, -1 / upem, 0, 0);
  gfx_matrix_multiply (&sink.m, &sink.m, &font->font_matrix);
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
  g_mutex_unlock (&font->lock);
}

/* ---- the class --------------------------------------------------------- */

static FT_Face
ns_pango_gfx_font_lock_face (NsPangoFcFont *fcfont)
{
  NsPangoGfxFont *font = (NsPangoGfxFont *) fcfont;
  FT_Face face;

  g_mutex_lock (&font->lock);
  face = get_face (font);
  if (!face)
    {
      g_mutex_unlock (&font->lock);
      return NULL;
    }
  FT_Activate_Size (font->metric_size);

  return face; /* unlock_face releases the lock */
}

static void
ns_pango_gfx_font_unlock_face (NsPangoFcFont *fcfont)
{
  g_mutex_unlock (&((NsPangoGfxFont *) fcfont)->lock);
}

static void
free_metrics_info (gpointer data)
{
  MetricsInfo *info = data;

  ns_pango_font_metrics_unref (info->metrics);
  g_free (info);
}

static void
ns_pango_gfx_font_finalize (GObject *object)
{
  NsPangoGfxFont *font = (NsPangoGfxFont *) object;

  image_cache_clear (font);
  if (font->images)
    g_hash_table_destroy (font->images);

  if (font->hbi)
    {
      g_object_unref (font->hbi->font);
      g_free (font->hbi);
    }

  g_free (font->extents_cache);
  g_slist_free_full (font->metrics_by_lang, free_metrics_info);

  if (font->face)
    {
      g_mutex_lock (&ft_library_lock);
      FT_Done_Face (font->face); /* and its sizes */
      g_mutex_unlock (&ft_library_lock);
    }
  if (font->blob)
    hb_blob_destroy (font->blob);

  g_mutex_clear (&font->lock);

  G_OBJECT_CLASS (ns_pango_gfx_font_parent_class)->finalize (object);
}

static void
ns_pango_gfx_font_class_init (NsPangoGfxFontClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  NsPangoFontClass *font_class = NS_PANGO_FONT_CLASS (klass);
  NsPangoFcFontClass *fc_font_class = NS_PANGO_FC_FONT_CLASS (klass);

  object_class->finalize = ns_pango_gfx_font_finalize;

  font_class->get_glyph_extents = ns_pango_gfx_font_get_glyph_extents;
  font_class->get_metrics = ns_pango_gfx_font_get_metrics;

  fc_font_class->lock_face = ns_pango_gfx_font_lock_face;
  fc_font_class->unlock_face = ns_pango_gfx_font_unlock_face;
}

static void
ns_pango_gfx_font_init (NsPangoGfxFont *font)
{
  g_mutex_init (&font->lock);
}

NsPangoFcFont *
_ns_pango_gfx_font_new (NsPangoGfxFontMap *fontmap,
                        NsPangoFcFontKey  *key)
{
  const FcPattern *pattern = ns_pango_fc_font_key_get_pattern (key);
  const NsPangoMatrix *ctm = ns_pango_fc_font_key_get_matrix (key);
  NsPangoGfxFont *font;
  FcMatrix fc_matrix, *fc_matrix_val;
  FcBool embolden;
  gfx_matrix_t gravity_matrix;
  double size;
  int i;

  g_return_val_if_fail (NS_PANGO_IS_GFX_FONT_MAP (fontmap), NULL);
  g_return_val_if_fail (pattern != NULL, NULL);

  font = g_object_new (NS_TYPE_PANGO_GFX_FONT,
                       "pattern", pattern,
                       "fontmap", fontmap,
                       NULL);

  /* The pattern's pixel size includes the context's scale; the em in
   * user space does not. */
  size = pattern_pixel_size (pattern) / ns_pango_matrix_get_font_scale_factor (ctm);
  font->size = size;

  if (ctm)
    ns_pango_matrix_get_font_scale_factors (ctm, &font->ctm_sx, &font->ctm_sy);
  else
    font->ctm_sx = font->ctm_sy = 1.0;
  if (!(font->ctm_sx > 0) || !(font->ctm_sy > 0))
    font->ctm_sx = font->ctm_sy = 1.0;

  FcMatrixInit (&fc_matrix);
  for (i = 0; FcPatternGetMatrix (pattern, FC_MATRIX, i, &fc_matrix_val) == FcResultMatch; i++)
    FcMatrixMultiply (&fc_matrix, &fc_matrix, fc_matrix_val);

  font->has_shape = fc_matrix.xx != 1 || fc_matrix.xy != 0 ||
                    fc_matrix.yx != 0 || fc_matrix.yy != 1;
  font->shape.xx = (FT_Fixed) lround (fc_matrix.xx * 65536.0);
  font->shape.xy = (FT_Fixed) lround (fc_matrix.xy * 65536.0);
  font->shape.yx = (FT_Fixed) lround (fc_matrix.yx * 65536.0);
  font->shape.yy = (FT_Fixed) lround (fc_matrix.yy * 65536.0);

  /* As pangocairo: fontconfig's matrix flipped into y-down font space and
   * scaled to the em, then the gravity's rotation. */
  gfx_matrix_init (&font->font_matrix, fc_matrix.xx, - fc_matrix.yx,
                   - fc_matrix.xy, fc_matrix.yy, 0., 0.);
  gfx_matrix_scale (&font->font_matrix, size, size);
  font->gravity = pattern_gravity (pattern);
  gfx_matrix_init_rotate (&gravity_matrix, ns_pango_gravity_to_rotation (font->gravity));
  gfx_matrix_multiply (&font->font_matrix, &font->font_matrix, &gravity_matrix);

  font->embolden = FcPatternGetBool (pattern, FC_EMBOLDEN, 0, &embolden) == FcResultMatch &&
                   embolden;
  font->load_flags = pattern_load_flags (pattern);

  /* Metrics are hinted (cairo's default for raster targets). */
  ((NsPangoFcFont *) font)->is_hinted = TRUE;

  return (NsPangoFcFont *) font;
}
