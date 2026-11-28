/* Weststar SDL Browser — prints how nstext lays out and draws a set of
 * ellipsized and justified layouts, including the middle ellipsis, which
 * no CSS reaches. Run before and after a change to nstext, the outputs
 * show what it moved (docs/text-layout.md):
 *
 *   ninja -C builddir text-layout-dump
 *   builddir/text-layout-dump > before.txt
 *   (change, rebuild)
 *   builddir/text-layout-dump > after.txt && diff before.txt after.txt
 *
 * Every layout prints its size, each line's start, length and extents,
 * the position of each character and a hash of its pixels; with a
 * directory argument the pictures are written there as PNG too.
 *
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"
#include "ns_pango.h"

typedef struct
{
  const char *name;
  const char *text;
  int width;                /* pixels */
  NsPangoEllipsizeMode mode;
  int justify;
  int letter_spacing;       /* Pango units */
  const char *font;
  int rtl;                  /* the context's base direction */
} Case;

static const Case cases[] = {
  { "end-latin", "An English line that is far too long for its box.", 200,
    NS_PANGO_ELLIPSIZE_END, 0, 0, "sans 16px", 0 },
  { "start-latin", "An English line that is far too long for its box.", 200,
    NS_PANGO_ELLIPSIZE_START, 0, 0, "sans 16px", 0 },
  { "middle-latin", "An English line that is far too long for its box.", 200,
    NS_PANGO_ELLIPSIZE_MIDDLE, 0, 0, "sans 16px", 0 },
  { "middle-latin-narrow", "Short words a b c d e f g h i j", 90,
    NS_PANGO_ELLIPSIZE_MIDDLE, 0, 0, "sans 16px", 0 },
  { "middle-hebrew", "\327\251\327\225\327\250\327\224 \327\221\327\242\327\221\327\250\327\231\327\252 "
    "\327\251\327\220\327\250\327\225\327\233\327\224 \327\236\327\223\327\231 \327\221\327\251\327\221\327\231\327\234 "
    "\327\224\327\252\327\231\327\221\327\224 \327\251\327\234\327\224", 150,
    NS_PANGO_ELLIPSIZE_MIDDLE, 0, 0, "sans 16px", 1 },
  { "middle-mixed", "Mixed English \327\242\327\221\327\250\327\231\327\252 \327\221\327\220\327\236\327\246\327\242 "
    "and English again 123 456.", 170, NS_PANGO_ELLIPSIZE_MIDDLE, 0, 0, "sans 16px", 0 },
  { "middle-arabic", "\330\263\330\267\330\261 \330\250\330\247\331\204\331\204\330\272\330\251 "
    "\330\247\331\204\330\271\330\261\330\250\331\212\330\251 \330\267\331\210\331\212\331\204 "
    "\330\254\330\257\330\247 \330\271\331\204\331\211 \330\265\331\206\330\257\331\210\331\202\331\207", 130,
    NS_PANGO_ELLIPSIZE_MIDDLE, 0, 0, "sans 16px", 1 },
  { "middle-cjk", "\346\227\245\346\234\254\350\252\236\343\201\256\346\226\207\347\253\240\343\201\257"
    "\343\201\250\343\201\246\343\202\202\351\225\267\343\201\204\343\201\256\343\201\247\347\256\261"
    "\343\201\253\345\205\245\343\202\212\343\201\276\343\201\233\343\202\223", 160,
    NS_PANGO_ELLIPSIZE_MIDDLE, 0, 0, "sans 16px", 0 },
  { "middle-letter-spacing", "Letter spacing widens this line a lot.", 180,
    NS_PANGO_ELLIPSIZE_MIDDLE, 0, 3 * NS_PANGO_SCALE, "sans 16px", 0 },
  { "middle-marks", "e\314\201e\314\201e\314\201e\314\201e\314\201e\314\201e\314\201e\314\201"
    "e\314\201e\314\201e\314\201e\314\201e\314\201e\314\201", 70,
    NS_PANGO_ELLIPSIZE_MIDDLE, 0, 0, "serif 16px", 0 },
  { "middle-justify", "A justified line that is cut in the middle, wide.", 210,
    NS_PANGO_ELLIPSIZE_MIDDLE, 1, 0, "sans 16px", 0 },
  { "middle-tiny", "Tiny box", 4, NS_PANGO_ELLIPSIZE_MIDDLE, 0, 0, "sans 16px", 0 },
  { "end-tiny", "Tiny box", 4, NS_PANGO_ELLIPSIZE_END, 0, 0, "sans 16px", 0 },
  { "start-tiny", "Tiny box", 4, NS_PANGO_ELLIPSIZE_START, 0, 0, "sans 16px", 0 },
  { "justify-cjk", "\346\227\245\346\234\254\350\252\236\343\201\256\346\226\207\347\253\240\343\201\257"
    "\345\215\230\350\252\236\343\201\256\351\226\223\343\201\253\347\251\272\347\231\275\343\201\214"
    "\343\201\252\343\201\204\343\201\256\343\201\247\343\200\201\344\270\241\347\253\257\346\217\203"
    "\343\201\210\343\201\247\343\201\257\346\226\207\345\255\227\343\201\250\346\226\207\345\255\227"
    "\343\201\256\351\226\223\343\201\253\344\275\231\347\231\275\343\201\214\345\210\206\343\201\221"
    "\343\202\211\343\202\214\343\201\276\343\201\231\343\200\202", 173,
    NS_PANGO_ELLIPSIZE_NONE, 1, 0, "sans 16px", 0 },
  { "justify-cjk-rtl", "\346\227\245\346\234\254\350\252\236\343\201\256\346\226\207\347\253\240\343\201\257"
    "\345\215\230\350\252\236\343\201\256\351\226\223\343\201\253\347\251\272\347\231\275\343\201\214"
    "\343\201\252\343\201\204\343\201\256\343\201\247\343\200\201", 141,
    NS_PANGO_ELLIPSIZE_NONE, 1, 0, "sans 15.5px", 1 },
};

static uint32_t
hash_bytes (const unsigned char *p, size_t n, uint32_t h)
{
  size_t i;

  for (i = 0; i < n; i++)
    h = (h ^ p[i]) * 16777619u;
  return h;
}

static void
dump_case (NsPangoContext *context, const Case *c, const char *png_dir)
{
  NsPangoLayout *layout = ns_pango_layout_new (context);
  NsPangoFontDescription *desc = ns_pango_font_description_from_string (c->font);
  NsPangoRectangle ink, logical;
  NsPangoLayoutIter *iter;
  const char *p;
  int w, h;

  ns_pango_context_set_base_dir (context, c->rtl ? NS_PANGO_DIRECTION_RTL : NS_PANGO_DIRECTION_LTR);
  ns_pango_layout_context_changed (layout);
  ns_pango_layout_set_font_description (layout, desc);
  ns_pango_font_description_free (desc);
  if (c->letter_spacing)
    {
      NsPangoAttrList *attrs = ns_pango_attr_list_new ();

      ns_pango_attr_list_insert (attrs, ns_pango_attr_letter_spacing_new (c->letter_spacing));
      ns_pango_layout_set_attributes (layout, attrs);
      ns_pango_attr_list_unref (attrs);
    }
  ns_pango_layout_set_text (layout, c->text, -1);
  ns_pango_layout_set_width (layout, c->width * NS_PANGO_SCALE);
  ns_pango_layout_set_ellipsize (layout, c->mode);
  ns_pango_layout_set_justify (layout, c->justify);
  if (c->mode != NS_PANGO_ELLIPSIZE_NONE)
    ns_pango_layout_set_height (layout, -1);

  ns_pango_layout_get_extents (layout, &ink, &logical);
  printf ("%s: lines %d ink %d,%d %dx%d logical %d,%d %dx%d\n", c->name,
          ns_pango_layout_get_line_count (layout), ink.x, ink.y, ink.width, ink.height,
          logical.x, logical.y, logical.width, logical.height);

  iter = ns_pango_layout_get_iter (layout);
  do
    {
      NsPangoLayoutLine *line = ns_pango_layout_iter_get_line_readonly (iter);

      ns_pango_layout_iter_get_line_extents (iter, &ink, &logical);
      printf ("  line %d+%d baseline %d ink %d,%d %dx%d logical %d,%d %dx%d\n",
              line->start_index, line->length, ns_pango_layout_iter_get_baseline (iter),
              ink.x, ink.y, ink.width, ink.height,
              logical.x, logical.y, logical.width, logical.height);
    }
  while (ns_pango_layout_iter_next_line (iter));
  ns_pango_layout_iter_free (iter);

  printf ("  pos");
  for (p = c->text; *p; )
    {
      NsPangoRectangle pos;
      int index = (int) (p - c->text);

      ns_pango_layout_index_to_pos (layout, index, &pos);
      printf (" %d:%d/%d", index, pos.x, pos.width);
      do
        p++;
      while ((*p & 0xc0) == 0x80);
    }
  printf ("\n");

  ns_pango_layout_get_pixel_size (layout, &w, &h);

  printf ("  hit");
  {
    int x;

    for (x = -2; x <= c->width + 2; x += 3)
      {
        int index = -1, trailing = -1;
        int inside = ns_pango_layout_xy_to_index (layout, x * NS_PANGO_SCALE,
                                                  NS_PANGO_SCALE, &index, &trailing);

        printf (" %d:%d%s%d", x, index, inside ? "+" : "~", trailing);
      }
  }
  printf ("\n");

  {
    NsPangoLayoutLine *line = ns_pango_layout_get_line_readonly (layout, 0);
    int len = (int) strlen (c->text), k;

    for (k = 0; k < 3; k++)
      {
        int from = k == 0 ? 0 : k == 1 ? len / 3 : len / 2;
        int to = k == 0 ? len : k == 1 ? len / 2 : len;
        int *ranges = NULL, n_ranges = 0, r;

        while (from > 0 && (c->text[from] & 0xc0) == 0x80)
          from--;
        while (to < len && (c->text[to] & 0xc0) == 0x80)
          to++;
        ns_pango_layout_line_get_x_ranges (line, from, to, &ranges, &n_ranges);
        printf ("  ranges %d-%d:", from, to);
        for (r = 0; r < n_ranges; r++)
          printf (" %d..%d", ranges[2 * r], ranges[2 * r + 1]);
        printf ("\n");
        free (ranges);
      }
  }
  {
    int sw = c->width + 40, sh = h + 8;
    gfx_surface_t *surface = gfx_image_surface_create (GFX_FORMAT_ARGB32, sw, sh);
    gfx_t *g = gfx_create (surface);

    gfx_set_source_rgb (g, 1, 1, 1);
    gfx_paint (g);
    gfx_set_source_rgb (g, 0, 0, 0);
    gfx_move_to (g, 4, 4);
    ns_pango_gfx_show_layout (g, layout);
    gfx_destroy (g);
    gfx_surface_flush (surface);
    printf ("  pixels %dx%d %08x\n", sw, sh,
            (unsigned) hash_bytes (gfx_image_surface_get_data (surface),
                                   (size_t) gfx_image_surface_get_stride (surface) * (size_t) sh,
                                   2166136261u));
    if (png_dir)
      {
        char path[1024];

        snprintf (path, sizeof path, "%s/%s.png", png_dir, c->name);
        gfx_surface_write_to_png (surface, path);
      }
    gfx_surface_destroy (surface);
  }

  ns_pango_layout_unref (layout);
}

int
main (int argc, char **argv)
{
  gfx_surface_t *surface = gfx_image_surface_create (GFX_FORMAT_ARGB32, 1, 1);
  gfx_t *g = gfx_create (surface);
  NsPangoContext *context = ns_pango_gfx_create_context (g);
  size_t i;

  for (i = 0; i < sizeof cases / sizeof cases[0]; i++)
    dump_case (context, &cases[i], argc > 1 ? argv[1] : NULL);

  ns_pango_context_unref (context);
  gfx_destroy (g);
  gfx_surface_destroy (surface);
  return 0;
}
