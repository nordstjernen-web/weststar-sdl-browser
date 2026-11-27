/* Pango
 * pangogfx-fontmap.c: the fontconfig font map of the gfx backend
 *
 * Derived from pangocairo-fcfontmap.c, pangocairo-fontmap.c and
 * pangocairo-context.c.
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

#include <string.h>

#include "pangogfx-private.h"

/* Threads, locks and queues from the SDL layer; see the header. */
#include "ns-glib-threads.h"

typedef struct
{
  NsPangoFcFontMapClass parent_class;
} NsPangoGfxFontMapClass;

G_DEFINE_TYPE (NsPangoGfxFontMap, ns_pango_gfx_font_map, NS_TYPE_PANGO_FC_FONT_MAP)

static guint
ns_pango_gfx_font_map_get_serial (NsPangoFontMap *fontmap)
{
  return NS_PANGO_GFX_FONT_MAP (fontmap)->serial;
}

static void
ns_pango_gfx_font_map_changed (NsPangoFontMap *fontmap)
{
  NsPangoGfxFontMap *gfontmap = NS_PANGO_GFX_FONT_MAP (fontmap);

  gfontmap->serial++;
  if (gfontmap->serial == 0)
    gfontmap->serial++;
}

/* Cairo's fontconfig backend also merged its font options into the
 * pattern here; this backend has none (grayscale antialiasing, hinting as
 * fontconfig says), so fontconfig's own substitutions are all there is. */
static void
ns_pango_gfx_font_map_fontset_key_substitute (NsPangoFcFontMap    *fcfontmap,
                                              NsPangoFcFontsetKey *fontkey G_GNUC_UNUSED,
                                              FcPattern           *pattern)
{
  FcConfigSubstitute (ns_pango_fc_font_map_get_config (fcfontmap), pattern,
                      FcMatchPattern);

  if (fcfontmap->substitute_func)
    fcfontmap->substitute_func (pattern, fcfontmap->substitute_data);

#ifdef HAVE_FC_CONFIG_SET_DEFAULT_SUBSTITUTE
  FcConfigSetDefaultSubstitute (ns_pango_fc_font_map_get_config (fcfontmap),
                                pattern);
#else
  FcDefaultSubstitute (pattern);
#endif
}

static double
ns_pango_gfx_font_map_get_resolution (NsPangoFcFontMap *fcfontmap,
                                      NsPangoContext   *context G_GNUC_UNUSED)
{
  return NS_PANGO_GFX_FONT_MAP (fcfontmap)->dpi;
}

static NsPangoFcFont *
ns_pango_gfx_font_map_create_font (NsPangoFcFontMap *fcfontmap,
                                   NsPangoFcFontKey *key)
{
  return _ns_pango_gfx_font_new (NS_PANGO_GFX_FONT_MAP (fcfontmap), key);
}

static void
ns_pango_gfx_font_map_class_init (NsPangoGfxFontMapClass *klass)
{
  NsPangoFontMapClass *fontmap_class = NS_PANGO_FONT_MAP_CLASS (klass);
  NsPangoFcFontMapClass *fcfontmap_class = NS_PANGO_FC_FONT_MAP_CLASS (klass);

  fontmap_class->get_serial = ns_pango_gfx_font_map_get_serial;
  fontmap_class->changed = ns_pango_gfx_font_map_changed;

  fcfontmap_class->fontset_key_substitute = ns_pango_gfx_font_map_fontset_key_substitute;
  fcfontmap_class->get_resolution = ns_pango_gfx_font_map_get_resolution;
  fcfontmap_class->create_font = ns_pango_gfx_font_map_create_font;
  /* No context key: every context shares the fonts. */
}

static void
ns_pango_gfx_font_map_init (NsPangoGfxFontMap *gfontmap)
{
  gfontmap->serial = 1;
  gfontmap->dpi = 96.0;
}

static GPrivate default_font_map = G_PRIVATE_INIT (g_object_unref); /* MT-safe */

NsPangoFontMap *
ns_pango_gfx_font_map_get_default (void)
{
  NsPangoFontMap *fontmap = g_private_get (&default_font_map);

  if (G_UNLIKELY (!fontmap))
    {
      fontmap = g_object_new (NS_TYPE_PANGO_GFX_FONT_MAP, NULL);
      g_private_replace (&default_font_map, fontmap);
    }

  return fontmap;
}

/* ---- contexts ---------------------------------------------------------- */

void
ns_pango_gfx_update_context (gfx_t          *g,
                             NsPangoContext *context)
{
  const NsPangoMatrix identity = NS_PANGO_MATRIX_INIT;
  const NsPangoMatrix *current;
  NsPangoMatrix matrix;
  gfx_matrix_t gm;

  g_return_if_fail (NS_PANGO_IS_CONTEXT (context));

  if (g)
    gfx_get_matrix (g, &gm);
  else
    gfx_matrix_init_identity (&gm);

  matrix.xx = gm.xx;
  matrix.yx = gm.yx;
  matrix.xy = gm.xy;
  matrix.yy = gm.yy;
  matrix.x0 = 0;
  matrix.y0 = 0;

  current = ns_pango_context_get_matrix (context);
  if (!current)
    current = &identity;

  /* Metrics are hinted, so the layout depends on the scale (not on the
   * translation). */
  if (memcmp (&matrix, current, sizeof matrix) != 0)
    {
      ns_pango_context_set_matrix (context, &matrix);
      ns_pango_context_changed (context);
    }
}

NsPangoContext *
ns_pango_gfx_create_context (gfx_t *g)
{
  NsPangoContext *context;

  context = ns_pango_font_map_create_context (ns_pango_gfx_font_map_get_default ());
  if (g)
    ns_pango_gfx_update_context (g, context);

  return context;
}

NsPangoLayout *
ns_pango_gfx_create_layout (gfx_t *g)
{
  NsPangoContext *context = ns_pango_gfx_create_context (g);
  NsPangoLayout *layout = ns_pango_layout_new (context);

  g_object_unref (context);

  return layout;
}
