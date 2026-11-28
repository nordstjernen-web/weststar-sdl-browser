/* Weststar SDL Browser — nstext: items, glyph strings, shaping with
 * HarfBuzz, letter and word spacing, and positions within glyph strings.
 *
 * Shaping follows Pango's shape.c: the whole paragraph is given to
 * HarfBuzz as context, clusters are byte offsets, characters a font does
 * not have become "unknown" glyphs (drawn as hex boxes), and positions are
 * rounded to whole pixels as Pango does for hinted fonts. The cluster
 * walks and caret positions follow pango-glyph-item.c and glyphstring.c.
 *
 * Copyright (C) 1999, 2004 Red Hat Software
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "nstext-private.h"

#include <math.h>
#include <stdio.h>

/* ---- items ------------------------------------------------------------- */

NtItem *
nt_item_new (void)
{
  return nt_new0 (NtItem, 1);
}

NtItem *
nt_item_copy (const NtItem *item)
{
  NtItem *c = nt_new (NtItem, 1);
  int i;

  *c = *item;
  nt_font_ref (c->font);
  c->extra_attrs = item->n_extra ? nt_new (NtAttr *, item->n_extra) : NULL;
  for (i = 0; i < item->n_extra; i++)
    c->extra_attrs[i] = nt_attr_copy (item->extra_attrs[i]);
  return c;
}

void
nt_item_free (NtItem *item)
{
  int i;

  if (!item)
    return;
  nt_font_unref (item->font);
  for (i = 0; i < item->n_extra; i++)
    ns_pango_attribute_destroy (&item->extra_attrs[i]->base);
  free (item->extra_attrs);
  free (item);
}

NtItem *
nt_item_split (NtItem *item, int split_index, int split_offset)
{
  NtItem *first = nt_item_copy (item);

  first->length = split_index;
  first->num_chars = split_offset;
  item->offset += split_index;
  item->length -= split_index;
  item->num_chars -= split_offset;
  item->char_offset += split_offset;
  return first;
}

NtAttr *
nt_item_find_attr (const NtItem *item, NtAttrType type)
{
  int i;
  NtAttr *found = NULL;

  for (i = 0; i < item->n_extra; i++)
    if (item->extra_attrs[i]->base.type == (int) type)
      found = item->extra_attrs[i];
  return found;
}

void
nt_item_list_append (NtItemList *l, NtItem *item)
{
  if (l->n == l->cap)
    {
      l->cap = l->cap ? l->cap * 2 : 16;
      l->items = nt_renew (NtItem *, l->items, l->cap);
    }
  l->items[l->n++] = item;
}

void
nt_item_list_clear (NtItemList *l)
{
  int i;

  for (i = 0; i < l->n; i++)
    nt_item_free (l->items[i]);
  free (l->items);
  l->items = NULL;
  l->n = l->cap = 0;
}

/* ---- glyph strings ----------------------------------------------------- */

NtGlyphString *
nt_glyph_string_new (void)
{
  return nt_new0 (NtGlyphString, 1);
}

void
nt_glyph_string_free (NtGlyphString *gs)
{
  if (!gs)
    return;
  free (gs->glyphs);
  free (gs->log_clusters);
  free (gs);
}

void
nt_glyph_string_set_size (NtGlyphString *gs, int n)
{
  if (n > gs->space)
    {
      int space = gs->space ? gs->space : 4;

      while (space < n)
        space *= 2;
      gs->glyphs = nt_renew (NtGlyph, gs->glyphs, space);
      gs->log_clusters = nt_renew (int, gs->log_clusters, space);
      gs->space = space;
    }
  gs->num_glyphs = n;
}

NtGlyphString *
nt_glyph_string_copy (const NtGlyphString *gs)
{
  NtGlyphString *c = nt_glyph_string_new ();

  nt_glyph_string_set_size (c, gs->num_glyphs);
  if (gs->num_glyphs)
    {
      memcpy (c->glyphs, gs->glyphs, (size_t) gs->num_glyphs * sizeof *gs->glyphs);
      memcpy (c->log_clusters, gs->log_clusters, (size_t) gs->num_glyphs * sizeof (int));
    }
  return c;
}

int
nt_glyph_string_get_width (const NtGlyphString *gs)
{
  int i, w = 0;

  for (i = 0; i < gs->num_glyphs; i++)
    w += gs->glyphs[i].width;
  return w;
}

void
nt_glyph_string_extents_range (const NtGlyphString *gs, int start, int end, NtFont *font,
                               NsPangoRectangle *ink, NsPangoRectangle *logical)
{
  int x_pos = 0, i;

  if (ink)
    ink->x = ink->y = ink->width = ink->height = 0;
  if (logical)
    logical->x = logical->y = logical->width = logical->height = 0;

  for (i = start; i < end; i++)
    {
      NsPangoRectangle gi, gl;
      const NtGlyph *g = &gs->glyphs[i];

      nt_font_get_glyph_extents (font, g->glyph, ink ? &gi : NULL, logical ? &gl : NULL);

      if (ink && gi.width != 0 && gi.height != 0)
        {
          if (ink->width == 0 || ink->height == 0)
            {
              ink->x = x_pos + gi.x + g->x_offset;
              ink->width = gi.width;
              ink->y = gi.y + g->y_offset;
              ink->height = gi.height;
            }
          else
            {
              int new_x, new_y;

              new_x = NT_MIN (ink->x, x_pos + gi.x + g->x_offset);
              ink->width = NT_MAX (ink->x + ink->width,
                                   x_pos + gi.x + gi.width + g->x_offset) - new_x;
              ink->x = new_x;
              new_y = NT_MIN (ink->y, gi.y + g->y_offset);
              ink->height = NT_MAX (ink->y + ink->height,
                                    gi.y + gi.height + g->y_offset) - new_y;
              ink->y = new_y;
            }
        }

      if (logical)
        {
          logical->width += g->width;
          if (i == start)
            {
              logical->y = gl.y;
              logical->height = gl.height;
            }
          else
            {
              int new_y = NT_MIN (logical->y, gl.y);

              logical->height = NT_MAX (logical->y + logical->height,
                                        gl.y + gl.height) - new_y;
              logical->y = new_y;
            }
        }
      x_pos += g->width;
    }
}

void
nt_glyph_string_extents (const NtGlyphString *gs, NtFont *font,
                         NsPangoRectangle *ink, NsPangoRectangle *logical)
{
  nt_glyph_string_extents_range (gs, 0, gs->num_glyphs, font, ink, logical);
}

static void
glyph_string_reverse (NtGlyphString *gs)
{
  int i, j;

  for (i = 0, j = gs->num_glyphs - 1; i < j; i++, j--)
    {
      NtGlyph g = gs->glyphs[i];
      int c = gs->log_clusters[i];

      gs->glyphs[i] = gs->glyphs[j];
      gs->glyphs[j] = g;
      gs->log_clusters[i] = gs->log_clusters[j];
      gs->log_clusters[j] = c;
    }
}

/* ---- the cluster walk (pango-glyph-item.c) ----------------------------- */

typedef struct
{
  const NtGlyphString *gs;
  const NtItem        *item;
  const char          *text;
  int start_glyph, start_index, start_char;
  int end_glyph, end_index, end_char;
} ClusterIter;

#define ITEM_LTR(item) (((item)->level & 1) == 0)

static int
cluster_next (ClusterIter *it)
{
  int glyph_index = it->end_glyph;
  const NtGlyphString *glyphs = it->gs;
  const NtItem *item = it->item;
  int cluster;

  if (ITEM_LTR (item))
    {
      if (glyph_index == glyphs->num_glyphs)
        return FALSE;
    }
  else if (glyph_index < 0)
    return FALSE;

  it->start_glyph = it->end_glyph;
  it->start_index = it->end_index;
  it->start_char = it->end_char;

  cluster = glyphs->log_clusters[glyph_index];
  for (;;)
    {
      glyph_index += ITEM_LTR (item) ? 1 : -1;
      if (glyph_index == glyphs->num_glyphs || glyph_index < 0)
        {
          it->end_index = item->offset + item->length;
          it->end_char = item->num_chars;
          break;
        }
      if (glyphs->log_clusters[glyph_index] > cluster)
        {
          it->end_index = item->offset + glyphs->log_clusters[glyph_index];
          it->end_char += nt_utf8_strlen (it->text + it->start_index,
                                          it->end_index - it->start_index);
          break;
        }
    }
  it->end_glyph = glyph_index;
  return TRUE;
}

static int
cluster_init (ClusterIter *it, const NtGlyphString *gs, const NtItem *item, const char *text)
{
  it->gs = gs;
  it->item = item;
  it->text = text;
  it->end_glyph = ITEM_LTR (item) ? 0 : gs->num_glyphs - 1;
  it->end_index = item->offset;
  it->end_char = 0;
  it->start_glyph = it->end_glyph;
  it->start_index = it->end_index;
  it->start_char = it->end_char;
  if (gs->num_glyphs == 0)
    return FALSE;
  return cluster_next (it);
}

void
nt_glyph_letter_space (const char *text, const NtItem *item,
                       const NsPangoLogAttr *log_attrs, int letter_spacing,
                       NtGlyphString *glyphs)
{
  ClusterIter it;
  int have;
  NtGlyph *g = glyphs->glyphs;

  for (have = cluster_init (&it, glyphs, item, text); have; have = cluster_next (&it))
    {
      if (!log_attrs[it.start_char].is_cursor_position)
        {
          if (g[it.start_glyph].width == 0 && it.start_glyph < it.end_glyph)
            g[it.start_glyph].x_offset -= letter_spacing;
          continue;
        }
      if (it.start_glyph < it.end_glyph)
        g[it.end_glyph - 1].width += letter_spacing;
      else
        {
          g[it.end_glyph + 1].x_offset += letter_spacing;
          g[it.end_glyph + 1].width += letter_spacing;
        }
    }
}

static int
is_word_separator (nt_unichar ch)
{
  return ch == 0x0020 || ch == 0x00A0 || ch == 0x1361 || ch == 0x10100 ||
         ch == 0x10101 || ch == 0x1039F || ch == 0x103D0;
}

void
nt_glyph_word_space (const char *text, const NtItem *item, int word_spacing,
                     NtGlyphString *glyphs)
{
  ClusterIter it;
  int have;

  if (word_spacing == 0)
    return;
  for (have = cluster_init (&it, glyphs, item, text); have; have = cluster_next (&it))
    {
      if (!is_word_separator (nt_utf8_get_char (text + it.start_index)))
        continue;
      if (it.start_glyph < it.end_glyph)
        glyphs->glyphs[it.end_glyph - 1].width += word_spacing;
      else
        glyphs->glyphs[it.end_glyph + 1].width += word_spacing;
    }
}

void
nt_glyph_logical_widths (const NtGlyphString *gs, const char *text, const NtItem *item,
                         int *widths)
{
  ClusterIter it;
  int have, dir = ITEM_LTR (item) ? 1 : -1;
  int i;

  for (i = 0; i < item->num_chars; i++)
    widths[i] = 0;
  for (have = cluster_init (&it, gs, item, text); have; have = cluster_next (&it))
    {
      int gi, ci, num_chars, cluster_width = 0, char_width;

      for (gi = it.start_glyph; gi != it.end_glyph; gi += dir)
        cluster_width += gs->glyphs[gi].width;
      num_chars = it.end_char - it.start_char;
      if (num_chars)
        {
          char_width = cluster_width / num_chars;
          for (ci = it.start_char; ci < it.end_char; ci++)
            widths[ci] = char_width;
          widths[it.start_char] += cluster_width - (char_width * num_chars);
        }
    }
}

/* ---- positions within a glyph string (glyphstring.c) ------------------ */

void
nt_glyph_index_to_x (const NtGlyphString *glyphs, const char *text_all, const NtItem *item,
                     const NsPangoLogAttr *attrs, int index, int trailing, int *x_pos)
{
  const char *text = text_all + item->offset;
  int length = item->length;
  int i, start_xpos = 0, end_xpos = 0, width = 0;
  int start_index = -1, end_index = -1;
  int cluster_chars = 0, cluster_offset = 0;
  int start_glyph_pos = -1, end_glyph_pos = -1;
  const char *p;

  if (!x_pos)
    return;
  if (glyphs->num_glyphs == 0)
    {
      *x_pos = 0;
      return;
    }

  if (item->level % 2)
    {
      for (i = glyphs->num_glyphs - 1; i >= 0; i--)
        width += glyphs->glyphs[i].width;
      for (i = glyphs->num_glyphs - 1; i >= 0; i--)
        {
          if (glyphs->log_clusters[i] > index)
            {
              end_index = glyphs->log_clusters[i];
              end_xpos = width;
              break;
            }
          if (glyphs->log_clusters[i] != start_index)
            {
              start_index = glyphs->log_clusters[i];
              start_xpos = width;
            }
          width -= glyphs->glyphs[i].width;
        }
      for (i = glyphs->num_glyphs - 1; i >= 0; i--)
        if (glyphs->log_clusters[i] == start_index)
          {
            if (end_glyph_pos < 0)
              end_glyph_pos = i;
            start_glyph_pos = i;
          }
    }
  else
    {
      for (i = 0; i < glyphs->num_glyphs; i++)
        {
          if (glyphs->log_clusters[i] > index)
            {
              end_index = glyphs->log_clusters[i];
              end_xpos = width;
              break;
            }
          if (glyphs->log_clusters[i] != start_index)
            {
              start_index = glyphs->log_clusters[i];
              start_xpos = width;
            }
          width += glyphs->glyphs[i].width;
        }
      for (i = 0; i < glyphs->num_glyphs; i++)
        if (glyphs->log_clusters[i] == start_index)
          {
            if (start_glyph_pos < 0)
              start_glyph_pos = i;
            end_glyph_pos = i;
          }
    }

  if (end_index == -1)
    {
      end_index = length;
      end_xpos = (item->level % 2) ? 0 : width;
    }

  for (p = text + start_index, i = attrs ? nt_utf8_pointer_to_offset (text, text + start_index) : 0;
       p < text + end_index;
       p = nt_utf8_next_char (p), i++)
    {
      if (attrs && !attrs[i].is_cursor_position)
        continue;
      if (p < text + index)
        cluster_offset++;
      cluster_chars++;
    }

  if (trailing)
    cluster_offset = NT_MIN (cluster_offset + 1, cluster_chars);

  if (!cluster_chars)
    {
      *x_pos = start_xpos;
      return;
    }

  if (cluster_offset > 0 && cluster_offset < cluster_chars && item->font)
    {
      hb_font_t *hb_font = item->font->hb_font;
      hb_position_t caret;
      unsigned int caret_count = 1;
      int glyph_pos = -1;
      unsigned int num_carets;

      if (start_glyph_pos == end_glyph_pos)
        glyph_pos = start_glyph_pos;
      else
        {
          hb_face_t *hb_face = hb_font_get_face (hb_font);

          for (i = start_glyph_pos; i <= end_glyph_pos; i++)
            if (hb_ot_layout_get_glyph_class (hb_face, glyphs->glyphs[i].glyph) !=
                HB_OT_LAYOUT_GLYPH_CLASS_MARK)
              {
                if (glyph_pos != -1)
                  {
                    glyph_pos = -2;
                    break;
                  }
                glyph_pos = i;
              }
        }
      if (glyph_pos >= 0 && !(glyphs->glyphs[glyph_pos].glyph & NT_GLYPH_UNKNOWN_FLAG))
        {
          num_carets = hb_ot_layout_get_ligature_carets (hb_font,
                                                         (item->level % 2) ? HB_DIRECTION_RTL
                                                                           : HB_DIRECTION_LTR,
                                                         glyphs->glyphs[glyph_pos].glyph,
                                                         (unsigned) (cluster_offset - 1),
                                                         &caret_count, &caret);
          if (caret_count > 0 && num_carets > 0)
            {
              if (item->level % 2)
                *x_pos = end_xpos + caret;
              else
                *x_pos = start_xpos + caret;
              *x_pos += glyphs->glyphs[glyph_pos].x_offset;
              return;
            }
        }
    }

  *x_pos = ((cluster_chars - cluster_offset) * start_xpos +
            cluster_offset * end_xpos) / cluster_chars;
}

void
nt_glyph_x_to_index (const NtGlyphString *glyphs, const char *text_all, const NtItem *item,
                     int x_pos, int *index, int *trailing)
{
  const char *text = text_all + item->offset;
  int length = item->length;
  int i, start_xpos = 0, end_xpos = 0, width = 0;
  int start_index = -1, end_index = -1;
  int cluster_chars = 0;
  const char *p;
  int found = FALSE;

  if (item->level % 2)
    {
      for (i = glyphs->num_glyphs - 1; i >= 0; i--)
        width += glyphs->glyphs[i].width;
      for (i = glyphs->num_glyphs - 1; i >= 0; i--)
        {
          if (glyphs->log_clusters[i] != start_index)
            {
              if (found)
                {
                  end_index = glyphs->log_clusters[i];
                  end_xpos = width;
                  break;
                }
              start_index = glyphs->log_clusters[i];
              start_xpos = width;
            }
          width -= glyphs->glyphs[i].width;
          if (width <= x_pos && x_pos < width + glyphs->glyphs[i].width)
            found = TRUE;
        }
    }
  else
    {
      for (i = 0; i < glyphs->num_glyphs; i++)
        {
          if (glyphs->log_clusters[i] != start_index)
            {
              if (found)
                {
                  end_index = glyphs->log_clusters[i];
                  end_xpos = width;
                  break;
                }
              start_index = glyphs->log_clusters[i];
              start_xpos = width;
            }
          if (width <= x_pos && x_pos < width + glyphs->glyphs[i].width)
            found = TRUE;
          width += glyphs->glyphs[i].width;
        }
    }

  if (end_index == -1)
    {
      end_index = length;
      end_xpos = (item->level % 2) ? 0 : width;
    }

  for (p = text + start_index; p < text + end_index; p = nt_utf8_next_char (p))
    cluster_chars++;

  if (start_xpos == end_xpos)
    {
      if (index)
        *index = start_index;
      if (trailing)
        *trailing = FALSE;
      return;
    }
  {
    double cp = ((double) (x_pos - start_xpos) * cluster_chars) / (end_xpos - start_xpos);

    if (start_xpos < end_xpos)
      {
        if (index)
          {
            const char *q = text + start_index;
            int k = 0;

            while (k + 1 <= cp)
              {
                q = nt_utf8_next_char (q);
                k++;
              }
            *index = (int) (q - text);
          }
        if (trailing)
          *trailing = (cp - (int) cp >= 0.5) ? TRUE : FALSE;
      }
    else
      {
        if (index)
          {
            const char *q = text + start_index;
            int k = 0;

            while (k + 1 < cp)
              {
                q = nt_utf8_next_char (q);
                k++;
              }
            *index = (int) (q - text);
          }
        if (trailing)
          {
            double cp_flip = cluster_chars - cp;

            *trailing = (cp_flip - (int) cp_flip >= 0.5) ? FALSE : TRUE;
          }
      }
  }
}

/* ---- shaping ----------------------------------------------------------- */

/* Made-up glyphs (unknown-glyph boxes, the empty glyph) lie far outside the
 * glyph space; HarfBuzz sees a stand-in just past the font's last glyph
 * instead, as in the fork's shape.c (its coverage caches keep 14 bits). */
#define STAND_IN_END (1u << 14)
#define MAX_STAND_INS 64

typedef struct
{
  NtFont        *font;
  hb_font_t     *parent;
  hb_codepoint_t stand_in_base;
  hb_codepoint_t stand_ins[MAX_STAND_INS];
  unsigned int   n_stand_ins;
} ShapeContext;

static hb_codepoint_t
stand_in_for (ShapeContext *c, hb_codepoint_t made_up)
{
  unsigned int i;

  for (i = 0; i < c->n_stand_ins; i++)
    if (c->stand_ins[i] == made_up)
      return c->stand_in_base + i;
  if (c->stand_in_base + c->n_stand_ins >= STAND_IN_END || c->n_stand_ins == MAX_STAND_INS)
    return made_up;
  c->stand_ins[c->n_stand_ins] = made_up;
  return c->stand_in_base + c->n_stand_ins++;
}

static hb_codepoint_t
made_up_for (const ShapeContext *c, hb_codepoint_t glyph)
{
  if (glyph >= c->stand_in_base && glyph - c->stand_in_base < c->n_stand_ins)
    return c->stand_ins[glyph - c->stand_in_base];
  return glyph;
}

static hb_bool_t
nominal_or_unknown (ShapeContext *c, hb_codepoint_t unicode, hb_codepoint_t *glyph)
{
  if (hb_font_get_nominal_glyph (c->parent, unicode, glyph))
    return TRUE;
  /* HarfBuzz synthesizes other spaces from U+0020; for U+0020 itself we
   * answer an unknown glyph with a space's width. */
  if (nt_unichar_type (unicode) == HB_UNICODE_GENERAL_CATEGORY_SPACE_SEPARATOR)
    {
      if (unicode == 0x20)
        {
          *glyph = NT_GET_UNKNOWN_GLYPH (0x20);
          return TRUE;
        }
      return FALSE;
    }
  *glyph = NT_GET_UNKNOWN_GLYPH (unicode);
  return unicode > 0x10FFFF;
}

static hb_bool_t
hb_nominal_glyph (hb_font_t *font NT_UNUSED, void *font_data, hb_codepoint_t unicode,
                  hb_codepoint_t *glyph, void *user_data NT_UNUSED)
{
  ShapeContext *c = font_data;
  hb_bool_t found = nominal_or_unknown (c, unicode, glyph);

  if (*glyph > 0xFFFF)
    *glyph = stand_in_for (c, *glyph);
  return found;
}

static hb_position_t
hb_h_advance (hb_font_t *font NT_UNUSED, void *font_data, hb_codepoint_t glyph,
              void *user_data NT_UNUSED)
{
  ShapeContext *c = font_data;

  glyph = made_up_for (c, glyph);
  if (glyph & NT_GLYPH_UNKNOWN_FLAG)
    {
      NsPangoRectangle logical;

      nt_font_get_glyph_extents (c->font, glyph, NULL, &logical);
      return logical.width;
    }
  return hb_font_get_glyph_h_advance (c->parent, glyph);
}

static hb_bool_t
hb_glyph_extents_func (hb_font_t *font NT_UNUSED, void *font_data, hb_codepoint_t glyph,
                       hb_glyph_extents_t *extents, void *user_data NT_UNUSED)
{
  ShapeContext *c = font_data;

  glyph = made_up_for (c, glyph);
  if (glyph & NT_GLYPH_UNKNOWN_FLAG)
    {
      NsPangoRectangle ink;

      nt_font_get_glyph_extents (c->font, glyph, &ink, NULL);
      extents->x_bearing = ink.x;
      extents->y_bearing = ink.y;
      extents->width = ink.width;
      extents->height = ink.height;
      return TRUE;
    }
  return hb_font_get_glyph_extents (c->parent, glyph, extents);
}

static hb_font_funcs_t *
shape_funcs (void)
{
  static hb_font_funcs_t *funcs;
  static NsOnce once;

  if (ns_once_enter (&once))
    {
      hb_font_funcs_t *f = hb_font_funcs_create ();

      hb_font_funcs_set_nominal_glyph_func (f, hb_nominal_glyph, NULL, NULL);
      hb_font_funcs_set_glyph_h_advance_func (f, hb_h_advance, NULL, NULL);
      hb_font_funcs_set_glyph_extents_func (f, hb_glyph_extents_func, NULL, NULL);
      hb_font_funcs_make_immutable (f);
      funcs = f;
      ns_once_leave (&once);
    }
  return funcs;
}

static int
glyph_has_color (hb_font_t *font, hb_codepoint_t glyph)
{
  hb_face_t *face = hb_font_get_face (font);
  hb_blob_t *blob;

#if HB_VERSION_ATLEAST (7, 0, 0)
  if (hb_ot_color_glyph_has_paint (face, glyph))
    return TRUE;
#endif
  if (hb_ot_color_glyph_get_layers (face, glyph, 0, NULL, NULL) > 0)
    return TRUE;
  if (hb_ot_color_has_png (face))
    {
      blob = hb_ot_color_glyph_reference_png (font, glyph);
      if (blob)
        {
          unsigned int length = hb_blob_get_length (blob);

          hb_blob_destroy (blob);
          if (length > 0)
            return TRUE;
        }
    }
  return FALSE;
}

static unsigned int
collect_features (const NtItem *item, hb_feature_t *features, unsigned int length)
{
  unsigned int n = 0;
  int i;

  /* The pattern's (fontconfig's) features first, as pangofc. */
  if (item->font)
    {
      FcChar8 *s;

      for (i = 0; n < length &&
                  FcPatternGetString (item->font->pattern, FC_FONT_FEATURES, i, &s) == FcResultMatch;
           i++)
        if (hb_feature_from_string ((const char *) s, -1, &features[n]))
          {
            features[n].start = 0;
            features[n].end = (unsigned) -1;
            n++;
          }
    }

  for (i = 0; i < item->n_extra && n < length; i++)
    {
      const NtAttr *a = item->extra_attrs[i];
      const char *feat;

      if (a->base.type != NT_ATTR_FONT_FEATURES)
        continue;
      for (feat = a->v.str; feat && n < length; )
        {
          const char *end = strchr (feat, ',');
          int len = end ? (int) (end - feat) : -1;

          if (hb_feature_from_string (feat, len, &features[n]))
            {
              features[n].start = a->base.start_index;
              features[n].end = a->base.end_index;
              n++;
            }
          if (!end)
            break;
          feat = end + 1;
        }
    }

  /* Ligatures off when letter spacing. */
  for (i = 0; i < item->n_extra && n < length; i++)
    {
      const NtAttr *a = item->extra_attrs[i];

      if (a->base.type == NT_ATTR_LETTER_SPACING)
        {
          static const hb_tag_t tags[] = {
            HB_TAG ('l', 'i', 'g', 'a'), HB_TAG ('c', 'l', 'i', 'g'),
            HB_TAG ('d', 'l', 'i', 'g'), HB_TAG ('h', 'l', 'i', 'g'),
          };
          size_t k;

          for (k = 0; k < NT_N_ELEMENTS (tags) && n < length; k++)
            {
              features[n].tag = tags[k];
              features[n].value = 0;
              features[n].start = a->base.start_index;
              features[n].end = a->base.end_index;
              n++;
            }
        }
    }

  /* Small caps: the font's own, where it has them. */
  {
    NtAttr *v = nt_item_find_attr (item, NT_ATTR_VARIANT);

    if (v && v->v.i != NS_PANGO_VARIANT_NORMAL && n + 1 < length)
      {
        static const char *const feats[] = {
          NULL, "smcp", "c2sc,smcp", "pcap", "c2pc,pcap", "unic", "titl"
        };
        const char *f = (unsigned) v->v.i < NT_N_ELEMENTS (feats) ? feats[v->v.i] : NULL;

        while (f && n < length)
          {
            const char *end = strchr (f, ',');

            if (hb_feature_from_string (f, end ? (int) (end - f) : -1, &features[n]))
              {
                features[n].start = 0;
                features[n].end = (unsigned) -1;
                n++;
              }
            f = end ? end + 1 : NULL;
          }
      }
  }

  return n;
}

static void
fallback_shape (const char *text, const NtItem *item, NtGlyphString *glyphs)
{
  const char *p = text + item->offset;
  int n_chars = item->num_chars, i, cluster = 0;

  nt_glyph_string_set_size (glyphs, n_chars);
  for (i = 0; i < n_chars; i++)
    {
      nt_unichar wc = nt_utf8_get_char (p);
      uint32_t glyph;
      NsPangoRectangle logical;

      if (nt_unichar_type (wc) != HB_UNICODE_GENERAL_CATEGORY_NON_SPACING_MARK)
        cluster = (int) (p - (text + item->offset));
      glyph = nt_unichar_iszerowidth (wc) ? NT_GLYPH_EMPTY : NT_GET_UNKNOWN_GLYPH (wc);
      nt_font_get_glyph_extents (item->font, glyph, NULL, &logical);
      glyphs->glyphs[i].glyph = glyph;
      glyphs->glyphs[i].x_offset = 0;
      glyphs->glyphs[i].y_offset = 0;
      glyphs->glyphs[i].width = logical.width;
      glyphs->glyphs[i].is_color = 0;
      glyphs->log_clusters[i] = cluster;
      p = nt_utf8_next_char (p);
    }
  if (item->level & 1)
    glyph_string_reverse (glyphs);
}

static NsPrivate thread_buffer = NS_PRIVATE_INIT ((void (*) (void *)) hb_buffer_destroy);

static void
hb_shape_item (const char *paragraph_text, int paragraph_length, const NtItem *item,
               const NsPangoLogAttr *log_attrs, NtGlyphString *glyphs)
{
  ShapeContext context;
  hb_buffer_t *buffer = ns_private_get (&thread_buffer);
  hb_font_t *font;
  hb_feature_t features[48];
  unsigned int num_features, i, num_glyphs;
  hb_glyph_info_t *info;
  hb_glyph_position_t *pos;
  int item_offset = item->offset, item_length = item->length;
  int hyphen_index = 0;
  int last_cluster = -1;
  int font_is_color;

  if (!buffer)
    {
      buffer = hb_buffer_create ();
      ns_private_set (&thread_buffer, buffer);
    }
  hb_buffer_reset (buffer);

  context.font = item->font;
  context.parent = item->font->hb_font;
  context.stand_in_base = hb_face_get_glyph_count (item->font->hb_face);
  context.n_stand_ins = 0;
  font = hb_font_create_sub_font (context.parent);
  hb_font_set_funcs (font, shape_funcs (), &context, NULL);

  hb_buffer_set_direction (buffer, (item->level & 1) ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
  hb_buffer_set_script (buffer, item->script);
  hb_buffer_set_language (buffer, hb_language_from_string (item->language ? item->language->tag
                                                                         : "", -1));
  hb_buffer_set_cluster_level (buffer, HB_BUFFER_CLUSTER_LEVEL_MONOTONE_CHARACTERS);
  hb_buffer_set_flags (buffer, HB_BUFFER_FLAG_BOT | HB_BUFFER_FLAG_EOT);
  hb_buffer_set_invisible_glyph (buffer, NT_GLYPH_EMPTY);

  if (item->flags & NT_ITEM_NEED_HYPHEN)
    {
      const char *p = paragraph_text + item_offset + item_length;
      int last_char_len = (int) (p - nt_utf8_prev_char (p));

      hyphen_index = item_offset + item_length - last_char_len;
      if (log_attrs && log_attrs[item->num_chars].break_removes_preceding)
        item_length -= last_char_len;
    }

  hb_buffer_add_utf8 (buffer, paragraph_text, item_offset, (unsigned) item_offset, 0);
  hb_buffer_add_utf8 (buffer, paragraph_text, item_offset + item_length,
                      (unsigned) item_offset, item_length);
  hb_buffer_add_utf8 (buffer, paragraph_text, paragraph_length,
                      (unsigned) (item_offset + item_length), 0);

  if (item->flags & NT_ITEM_NEED_HYPHEN)
    {
      hb_codepoint_t g;

      if (hb_font_get_nominal_glyph (font, 0x2010, &g))
        hb_buffer_add (buffer, 0x2010, (unsigned) hyphen_index);
      else if (hb_font_get_nominal_glyph (font, '-', &g))
        hb_buffer_add (buffer, '-', (unsigned) hyphen_index);
    }

  num_features = collect_features (item, features, NT_N_ELEMENTS (features));
  hb_shape (font, buffer, features, num_features);

  num_glyphs = hb_buffer_get_length (buffer);
  info = hb_buffer_get_glyph_infos (buffer, NULL);
  pos = hb_buffer_get_glyph_positions (buffer, NULL);
  nt_glyph_string_set_size (glyphs, (int) num_glyphs);
  font_is_color = item->font->is_color;

  for (i = 0; i < num_glyphs; i++)
    {
      NtGlyph *g = &glyphs->glyphs[i];

      g->glyph = made_up_for (&context, info[i].codepoint);
      glyphs->log_clusters[i] = (int) info[i].cluster - item_offset;
      g->is_cluster_start = glyphs->log_clusters[i] != last_cluster;
      g->is_color = font_is_color && !(g->glyph & NT_GLYPH_UNKNOWN_FLAG) &&
                    g->glyph != NT_GLYPH_EMPTY &&
                    glyph_has_color (context.parent, g->glyph);
      last_cluster = glyphs->log_clusters[i];
      g->width = pos[i].x_advance;
      g->x_offset = pos[i].x_offset;
      g->y_offset = - pos[i].y_offset;
    }

  hb_font_destroy (font);
}

/* ---- the shape cache --------------------------------------------------- */

static NsPrivate shape_cache_key = NS_PRIVATE_INIT ((void (*) (void *)) nt_hash_free);
static SDL_AtomicInt cache_hits, cache_misses, cache_entries;

#define SHAPE_CACHE_MAX 16384
#define SHAPE_CONTEXT_BYTES 16

/* The key: everything shaping reads. The paragraph around the item is
 * context for HarfBuzz; a few bytes either side stand for it (HarfBuzz
 * looks at most a few characters into the context). */
static void
shape_key (NtString *key, const char *paragraph_text, int paragraph_length,
           const NtItem *item, const NsPangoLogAttr *log_attrs)
{
  int pre = NT_MIN (item->offset, SHAPE_CONTEXT_BYTES);
  int post = NT_MIN (paragraph_length - item->offset - item->length, SHAPE_CONTEXT_BYTES);
  int i;

  nt_string_printf (key, "%s|%d|%u|%s|%u|%d|%d|%d|", item->font->key, item->level,
                    (unsigned) item->script, item->language ? item->language->tag : "",
                    item->flags & NT_ITEM_NEED_HYPHEN,
                    log_attrs && (item->flags & NT_ITEM_NEED_HYPHEN)
                      ? log_attrs[item->num_chars].break_removes_preceding : 0,
                    pre, post);
  for (i = 0; i < item->n_extra; i++)
    {
      const NtAttr *a = item->extra_attrs[i];

      if (a->base.type == NT_ATTR_FONT_FEATURES)
        nt_string_printf (key, "F%s@%u-%u|", a->v.str,
                          a->base.start_index - (unsigned) item->offset,
                          a->base.end_index == (unsigned) -1
                            ? (unsigned) -1 : a->base.end_index - (unsigned) item->offset);
      else if (a->base.type == NT_ATTR_LETTER_SPACING)
        nt_string_append (key, "L|");
      else if (a->base.type == NT_ATTR_VARIANT)
        nt_string_printf (key, "V%d|", a->v.i);
    }
  nt_string_append_len (key, paragraph_text + item->offset - pre,
                        (size_t) (pre + item->length + post));
}

static void
glyphs_free_void (void *p)
{
  nt_glyph_string_free (p);
}

void
nt_shape_item (const char *paragraph_text, int paragraph_length, const NtItem *item,
               const NsPangoLogAttr *log_attrs, NtGlyphString *glyphs)
{
  nt_shape_item_full (paragraph_text, paragraph_length, item, log_attrs, TRUE, glyphs);
}

void
nt_shape_item_full (const char *paragraph_text, int paragraph_length, const NtItem *item,
                    const NsPangoLogAttr *log_attrs, int round_positions,
                    NtGlyphString *glyphs)
{
  NtHash *cache = NULL;
  NtString key = { 0 };
  NtGlyphString *hit;
  int i, last_cluster;

  glyphs->num_glyphs = 0;

  if (!item->font)
    {
      fallback_shape (paragraph_text, item, glyphs);
      goto finish;
    }

  /* Feature ranges are absolute indexes: an item is keyed relative to its
   * start, so only features that cover it whole are cacheable. */
  cache = ns_private_get (&shape_cache_key);
  if (!cache)
    {
      cache = nt_hash_new (glyphs_free_void);
      ns_private_set (&shape_cache_key, cache);
    }
  shape_key (&key, paragraph_text, paragraph_length, item, log_attrs);
  nt_string_append (&key, round_positions ? "|R" : "|U");
  hit = nt_hash_lookup (cache, key.str, key.len);
  if (hit)
    {
      nt_glyph_string_set_size (glyphs, hit->num_glyphs);
      memcpy (glyphs->glyphs, hit->glyphs, (size_t) hit->num_glyphs * sizeof *hit->glyphs);
      memcpy (glyphs->log_clusters, hit->log_clusters, (size_t) hit->num_glyphs * sizeof (int));
      SDL_AddAtomicInt (&cache_hits, 1);
      free (key.str);
      return;
    }
  SDL_AddAtomicInt (&cache_misses, 1);

  hb_shape_item (paragraph_text, paragraph_length, item, log_attrs, glyphs);
  if (glyphs->num_glyphs == 0)
    fallback_shape (paragraph_text, item, glyphs);

finish:
  last_cluster = glyphs->num_glyphs ? glyphs->log_clusters[0] - 1 : 0;
  for (i = 0; i < glyphs->num_glyphs; i++)
    {
      NtGlyph *g = &glyphs->glyphs[i];

      if (glyphs->log_clusters[i] != last_cluster)
        {
          g->is_cluster_start = TRUE;
          last_cluster = glyphs->log_clusters[i];
        }
      else
        g->is_cluster_start = FALSE;
      if (g->width < 0)
        {
          g->width = -g->width;
          g->x_offset += g->width;
        }
    }

  if ((item->level & 1) && glyphs->num_glyphs > 1 &&
      glyphs->log_clusters[0] < glyphs->log_clusters[glyphs->num_glyphs - 1])
    glyph_string_reverse (glyphs);

  /* Hinted: advances in whole pixels, as Pango with round_glyph_positions
   * (offsets too, in device pixels, under a scaled context). */
  if (round_positions)
    {
      double scale = item->font ? item->font->ctm_scale : 1.0;

      for (i = 0; i < glyphs->num_glyphs; i++)
        {
          NtGlyph *g = &glyphs->glyphs[i];

          if (scale == 1.0)
            g->width = NS_PANGO_UNITS_ROUND (g->width);
          else
            {
#define HINT(v) ((int) (NS_PANGO_UNITS_ROUND ((int) ((v) * scale)) / scale))
              g->width = HINT (g->width);
              g->x_offset = HINT (g->x_offset);
              g->y_offset = HINT (g->y_offset);
#undef HINT
            }
        }
    }

  if (key.str && cache)
    {
      if (nt_hash_size (cache) >= SHAPE_CACHE_MAX)
        {
          nt_hash_remove_all (cache);
          SDL_SetAtomicInt (&cache_entries, 0);
        }
      nt_hash_insert (cache, key.str, key.len, nt_glyph_string_copy (glyphs));
      SDL_AddAtomicInt (&cache_entries, 1);
      free (key.str);
    }
}

/* A shape attribute's glyphs: one empty glyph per character, as wide as
 * the attribute's logical rectangle (Pango's _pango_shape_shape). */
void
nt_shape_shape_attr (const char *text, const NtItem *item, const NtAttr *shape,
                     NtGlyphString *glyphs)
{
  const char *p = text + item->offset;
  int i;

  nt_glyph_string_set_size (glyphs, item->num_chars);
  for (i = 0; i < item->num_chars; i++, p = nt_utf8_next_char (p))
    {
      glyphs->glyphs[i].glyph = NT_GLYPH_EMPTY;
      glyphs->glyphs[i].x_offset = 0;
      glyphs->glyphs[i].y_offset = 0;
      glyphs->glyphs[i].width = shape->v.shape.logical.width;
      glyphs->glyphs[i].is_cluster_start = 1;
      glyphs->glyphs[i].is_color = 0;
      glyphs->log_clusters[i] = (int) (p - (text + item->offset));
    }
}

void
nt_shape_cache_stats (uint64_t *hits, uint64_t *misses, uint64_t *entries)
{
  if (hits)
    *hits = (uint64_t) SDL_GetAtomicInt (&cache_hits);
  if (misses)
    *misses = (uint64_t) SDL_GetAtomicInt (&cache_misses);
  if (entries)
    *entries = (uint64_t) SDL_GetAtomicInt (&cache_entries);
}

void
ns_pango_cache_get_stats (uint64_t *hits, uint64_t *misses, uint64_t *skips,
                          uint64_t *entries)
{
  nt_shape_cache_stats (hits, misses, entries);
  if (skips)
    *skips = 0;
}

void
ns_pango_break_cache_stats (uint64_t *hits, uint64_t *misses, uint64_t *skips,
                            uint64_t *entries)
{
  if (hits) *hits = 0;
  if (misses) *misses = 0;
  if (skips) *skips = 0;
  if (entries) *entries = 0;
}

void
ns_pango_item_cache_stats (uint64_t *hits, uint64_t *misses, uint64_t *skips,
                           uint64_t *entries)
{
  if (hits) *hits = 0;
  if (misses) *misses = 0;
  if (skips) *skips = 0;
  if (entries) *entries = 0;
}
