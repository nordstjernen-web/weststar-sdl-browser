/* Weststar SDL Browser — nstext: ellipsizing a line. Pango's
 * ellipsize.c, ported off GLib: a gap grows out from the cut position
 * (the start, the middle or the end of the line) one span of clusters at
 * a time, on the side that keeps the gap balanced around that position,
 * until the line with an ellipsis in the gap fits; the line's runs are
 * then rebuilt around the ellipsis run. The cluster iterator and the run
 * split are Pango's pango-glyph-item.c.
 *
 * Copyright (C) 2004 Red Hat Software
 * Copyright 2026 Andreas Røsdal
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "nstext-layout-private.h"

NtFontset *nt_context_load_fontset (NsPangoContext *context, const NsPangoFontDescription *desc,
                                    NsPangoLanguage *language);

/* Overall, the way we ellipsize is we grow a "gap" out from an original
 * gap center position until:
 *
 *  line_width - gap_width + ellipsize_width <= goal_width
 *
 * Line:  [-------------------------------------------]
 * Runs:  [------)[---------------)[------------------]
 * Gap center:                 *
 * Gap:             [----------------------]
 *
 * The gap center may be at the start or end in which case the gap grows
 * in only one direction.
 *
 * Note the line and last run are logically closed at the end; this allows
 * us to use a gap position at x=line_width and still have it be part of
 * of a run.
 *
 * We grow the gap out one "span" at a time, where a span is simply a
 * consecutive run of clusters that we can't interrupt with an ellipsis.
 *
 * When choosing whether to grow the gap at the start or the end, we
 * calculate the next span to remove in both directions and see which
 * causes the smaller increase in:
 *
 *  MAX (gap_end - gap_center, gap_start - gap_center)
 *
 * All computations are done using logical order; the ellipsization
 * process occurs before the runs are ordered into visual order.
 */

static void
run_free (NtRun *run)
{
  if (!run)
    return;
  nt_item_free (run->item);
  nt_glyph_string_free (run->glyphs);
  free (run);
}

/* ---- the state --------------------------------------------------------- */

/* Keeps information about a single run */
typedef struct
{
  NtRun *run;
  int    start_offset; /* Character offset of run start */
  int    width;        /* Width of run in Pango units */
} RunInfo;

/* Iterator to a position within the ellipsized line */
typedef struct
{
  NtClusterIter run_iter;
  int         run_index;
} LineIter;

typedef struct
{
  NsPangoLayout         *layout;
  const NsPangoAttrList *attrs;   /* used for itemization/shaping; may be NULL */

  RunInfo *run_info;
  int      n_runs;

  int total_width;          /* Original width of line in Pango units */
  int gap_center;           /* Goal for center of gap */

  NtRun *ellipsis_run;      /* Run created to hold ellipsis */
  int    ellipsis_width;    /* Width of ellipsis, in Pango units */
  int    ellipsis_is_cjk;   /* Whether the first character in the gap is
                             * wide; this asks for a mid-line ellipsis */

  /* The attribute range the ellipsis was shaped with (Pango keeps an
   * attribute iterator there): it starts at gap_start_attr_start and
   * contains the byte gap_start_attr_index. */
  int      have_gap_start_attr;
  unsigned gap_start_attr_start;
  int      gap_start_attr_index;

  LineIter gap_start_iter;  /* Iterator pointing to the first cluster in gap */
  int      gap_start_x;     /* x position of start of gap, in Pango units */
  LineIter gap_end_iter;    /* Iterator pointing to last cluster in gap */
  int      gap_end_x;       /* x position of end of gap, in Pango units */
} EllipsizeState;

/* Compute global information needed for the itemization process */
static void
init_state (EllipsizeState *state, NtLine *line, const NsPangoAttrList *attrs)
{
  NsPangoLayout *layout = line->pub.layout;
  int i, start_offset;

  memset (state, 0, sizeof *state);
  state->layout = layout;
  state->attrs = attrs;

  state->n_runs = line->n_runs;
  state->run_info = nt_new (RunInfo, state->n_runs ? state->n_runs : 1);

  start_offset = nt_utf8_strlen (layout->text, line->pub.start_index);

  state->total_width = 0;
  for (i = 0; i < line->n_runs; i++)
    {
      NtRun *run = line->runs[i];
      int width = nt_glyph_string_get_width (run->glyphs);

      state->run_info[i].run = run;
      state->run_info[i].width = width;
      state->run_info[i].start_offset = start_offset;
      state->total_width += width;

      start_offset += run->item->num_chars;
    }
}

static void
free_state (EllipsizeState *state)
{
  free (state->run_info);
}

/* Computes the width of a single cluster */
static int
get_cluster_width (LineIter *iter)
{
  NtClusterIter *run_iter = &iter->run_iter;
  NtGlyphString *glyphs = run_iter->run->glyphs;
  int width = 0, i;

  if (run_iter->start_glyph < run_iter->end_glyph) /* LTR */
    {
      for (i = run_iter->start_glyph; i < run_iter->end_glyph; i++)
        width += glyphs->glyphs[i].width;
    }
  else                                             /* RTL */
    {
      for (i = run_iter->start_glyph; i > run_iter->end_glyph; i--)
        width += glyphs->glyphs[i].width;
    }

  return width;
}

/* Move forward one cluster. Returns FALSE if we were already at the end */
static int
line_iter_next_cluster (EllipsizeState *state, LineIter *iter)
{
  if (!nt_cluster_iter_next (&iter->run_iter))
    {
      if (iter->run_index == state->n_runs - 1)
        return FALSE;
      iter->run_index++;
      nt_cluster_iter_init_start (&iter->run_iter, state->run_info[iter->run_index].run,
                               state->layout->text);
    }
  return TRUE;
}

/* Move backward one cluster. Returns FALSE if we were already at the start */
static int
line_iter_prev_cluster (EllipsizeState *state, LineIter *iter)
{
  if (!nt_cluster_iter_prev (&iter->run_iter))
    {
      if (iter->run_index == 0)
        return FALSE;
      iter->run_index--;
      nt_cluster_iter_init_end (&iter->run_iter, state->run_info[iter->run_index].run,
                             state->layout->text);
    }
  return TRUE;
}

/* An ellipsization boundary starts a cluster (forced by the structure of
 * the code) and a grapheme (checked here). */

/* Checks if there is an ellipsization boundary before the cluster */
static int
starts_at_ellipsization_boundary (EllipsizeState *state, LineIter *iter)
{
  RunInfo *run_info = &state->run_info[iter->run_index];

  if (iter->run_iter.start_char == 0 && iter->run_index == 0)
    return TRUE;

  return state->layout->log_attrs[run_info->start_offset + iter->run_iter.start_char]
           .is_cursor_position;
}

/* Checks if there is an ellipsization boundary after the cluster (Pango
 * looks one character further than the cluster's end; so does this). */
static int
ends_at_ellipsization_boundary (EllipsizeState *state, LineIter *iter)
{
  RunInfo *run_info = &state->run_info[iter->run_index];

  if (iter->run_iter.end_char == run_info->run->item->num_chars &&
      iter->run_index == state->n_runs - 1)
    return TRUE;

  return state->layout->log_attrs[run_info->start_offset + iter->run_iter.end_char + 1]
           .is_cursor_position;
}

/* ---- the ellipsis ------------------------------------------------------ */

/* Walks an attribute iterator to the range containing the byte index. */
static void
advance_iterator_to (NtAttrIter *it, int index)
{
  while (it->end <= (unsigned) index && nt_attr_iter_next (it))
    ;
}

/* Pango's itemization of the ellipsis text (always one item). Without
 * fallback the item gets the first font of the description's fontset,
 * whether or not that has the character, as Pango's itemizer with
 * NS_PANGO_ATTR_FALLBACK off. */
static NtItem *
itemize_text (EllipsizeState *state, const char *text, const NsPangoAttrList *attrs,
              int enable_fallback)
{
  NsPangoContext *context = state->layout->context;
  NtItemList items = { 0 };
  NtItem *item;
  int i;

  nt_itemize (context, context->base_dir, text, 0, (int) strlen (text), attrs, NULL, &items);
  item = items.items[0];
  for (i = 1; i < items.n; i++)
    nt_item_free (items.items[i]);
  free (items.items);

  if (!enable_fallback)
    {
      NsPangoFontDescription *desc = ns_pango_font_description_copy (context->font_desc);
      NtAttrIter it;
      NtFontset *fs;
      NtFont *font = NULL;

      nt_attr_iter_init (&it, attrs);
      nt_attr_iter_get_font (&it, desc, NULL);
      nt_attr_iter_destroy (&it);

      /* pango_font_map_load_font: the context's language. */
      fs = nt_context_load_fontset (context, desc, context->language);
      for (i = 0; i < 4 && !font; i++)
        font = nt_fontset_get_font_at (fs, i);
      nt_font_ref (font);
      nt_fontset_unref (fs);
      ns_pango_font_description_free (desc);

      nt_font_unref (item->font);
      item->font = font;
    }

  return item;
}

/* Shapes the ellipsis using the font and is_cjk information computed by
 * update_ellipsis_shape() from the first character in the gap. */
static void
shape_ellipsis (EllipsizeState *state)
{
  NsPangoAttrList *attrs = ns_pango_attr_list_new ();
  NtGlyphString *glyphs;
  NtItem *item;
  const char *ellipsis_text;
  int len, i;

  /* Create/reset state->ellipsis_run */
  if (!state->ellipsis_run)
    {
      state->ellipsis_run = nt_new0 (NtRun, 1);
      state->ellipsis_run->glyphs = nt_glyph_string_new ();
    }
  nt_item_free (state->ellipsis_run->item);
  state->ellipsis_run->item = NULL;

  /* Create an attribute list by copying font attributes, leaving out
   * things that could be problematic like shapes, or super/subscripts.
   * The attributes are those of the range at the gap's first run, one of
   * each type (font descriptions all), in the list's order. */
  if (state->attrs)
    {
      NtAttrIter it;
      NtAttr **found;
      int n_found = 0, j;

      nt_attr_iter_init (&it, state->attrs);
      advance_iterator_to (&it, state->gap_start_attr_index);
      found = nt_new (NtAttr *, it.n_open + 1);
      for (i = it.n_open - 1; i >= 0; i--)
        {
          NtAttr *a = it.open[i];
          int dup = FALSE;

          if (a->base.type != NT_ATTR_FONT_DESC)
            for (j = 0; j < n_found; j++)
              if (found[j]->base.type == a->base.type)
                dup = TRUE;
          if (!dup)
            found[n_found++] = a;
        }
      for (j = n_found - 1; j >= 0; j--)
        {
          switch ((NtAttrType) found[j]->base.type)
            {
            case NT_ATTR_LANGUAGE:
            case NT_ATTR_FAMILY:
            case NT_ATTR_STYLE:
            case NT_ATTR_WEIGHT:
            case NT_ATTR_VARIANT:
            case NT_ATTR_STRETCH:
            case NT_ATTR_FONT_DESC:
            case NT_ATTR_LETTER_SPACING:
            case NT_ATTR_ABSOLUTE_SIZE:
              {
                NtAttr *copy = nt_attr_copy (found[j]);

                copy->base.start_index = 0;
                copy->base.end_index = 0x7fffffff;
                ns_pango_attr_list_insert (attrs, &copy->base);
              }
              break;
            default:
              break;
            }
        }
      free (found);
      nt_attr_iter_destroy (&it);
    }

  /* First try using a specific ellipsis character in the best matching font */
  if (state->ellipsis_is_cjk)
    ellipsis_text = "\342\213\257"; /* U+22EF: MIDLINE HORIZONTAL ELLIPSIS, used for CJK */
  else
    ellipsis_text = "\342\200\246"; /* U+2026: HORIZONTAL ELLIPSIS */

  item = itemize_text (state, ellipsis_text, attrs, FALSE);

  /* If that fails we use "..." in the first matching font */
  if (!item->font || !nt_font_has_char (item->font, nt_utf8_get_char (ellipsis_text)))
    {
      nt_item_free (item);
      ellipsis_text = "...";
      item = itemize_text (state, ellipsis_text, attrs, TRUE);
    }

  ns_pango_attr_list_unref (attrs);

  state->ellipsis_run->item = item;

  /* Now shape */
  glyphs = state->ellipsis_run->glyphs;
  len = (int) strlen (ellipsis_text);
  nt_shape_item (ellipsis_text, len, item, NULL, glyphs);

  state->ellipsis_width = 0;
  for (i = 0; i < glyphs->num_glyphs; i++)
    state->ellipsis_width += glyphs->glyphs[i].width;
}

/* Updates the shaping of the ellipsis if necessary when we move the
 * position of the start of the gap.
 *
 * The shaping of the ellipsis is determined by two things:
 *
 * - The font attributes applied to the first character in the gap
 * - Whether the first character in the gap is wide or not. If the
 *   first character is wide, then we assume that we are ellipsizing
 *   East-Asian text, so prefer a mid-line ellipsizes to a baseline
 *   ellipsis, since that's typical practice for Chinese/Japanese/Korean.
 */
static void
update_ellipsis_shape (EllipsizeState *state)
{
  int recompute = FALSE, is_cjk;
  nt_unichar start_wc;

  /* See if the current attribute range contains the new start position */
  if (state->have_gap_start_attr &&
      (unsigned) state->gap_start_iter.run_iter.start_index < state->gap_start_attr_start)
    state->have_gap_start_attr = FALSE;

  /* Check whether we need to recompute the ellipsis because of new font attributes */
  if (!state->have_gap_start_attr)
    {
      int index = state->run_info[state->gap_start_iter.run_index].run->item->offset;

      state->gap_start_attr_index = index;
      state->gap_start_attr_start = 0;
      if (state->attrs)
        {
          NtAttrIter it;

          nt_attr_iter_init (&it, state->attrs);
          advance_iterator_to (&it, index);
          state->gap_start_attr_start = it.start;
          nt_attr_iter_destroy (&it);
        }
      state->have_gap_start_attr = TRUE;
      recompute = TRUE;
    }

  /* Check whether we need to recompute the ellipsis because we switch from
   * CJK to not or vice-versa */
  start_wc = nt_utf8_get_char (state->layout->text + state->gap_start_iter.run_iter.start_index);
  is_cjk = nt_unichar_iswide (start_wc) ? TRUE : FALSE;

  if (is_cjk != state->ellipsis_is_cjk)
    {
      state->ellipsis_is_cjk = is_cjk;
      recompute = TRUE;
    }

  if (recompute)
    shape_ellipsis (state);
}

/* ---- the gap ----------------------------------------------------------- */

/* Computes the position of the gap center and finds the smallest span
 * containing it */
static void
find_initial_span (EllipsizeState *state)
{
  NtClusterIter *run_iter;
  NtRun *run;
  int have_cluster, i, x, cluster_width;

  switch (state->layout->ellipsize)
    {
    case NS_PANGO_ELLIPSIZE_START:
      state->gap_center = 0;
      break;
    case NS_PANGO_ELLIPSIZE_MIDDLE:
      state->gap_center = state->total_width / 2;
      break;
    case NS_PANGO_ELLIPSIZE_END:
    case NS_PANGO_ELLIPSIZE_NONE:
    default:
      state->gap_center = state->total_width;
      break;
    }

  /* Find the run containing the gap center */
  x = 0;
  for (i = 0; i < state->n_runs; i++)
    {
      if (x + state->run_info[i].width > state->gap_center)
        break;
      x += state->run_info[i].width;
    }

  if (i == state->n_runs) /* Last run is a closed interval, so back off one run */
    {
      i--;
      x -= state->run_info[i].width;
    }

  /* Find the cluster containing the gap center */
  state->gap_start_iter.run_index = i;
  run_iter = &state->gap_start_iter.run_iter;
  run = state->run_info[i].run;

  cluster_width = 0; /* the line must have at least one cluster */
  for (have_cluster = nt_cluster_iter_init_start (run_iter, run, state->layout->text);
       have_cluster;
       have_cluster = nt_cluster_iter_next (run_iter))
    {
      cluster_width = get_cluster_width (&state->gap_start_iter);

      if (x + cluster_width > state->gap_center)
        break;

      x += cluster_width;
    }

  if (!have_cluster) /* Last cluster is a closed interval, so back off one cluster */
    x -= cluster_width;

  state->gap_end_iter = state->gap_start_iter;

  state->gap_start_x = x;
  state->gap_end_x = x + cluster_width;

  /* Expand the gap to a full span */
  while (!starts_at_ellipsization_boundary (state, &state->gap_start_iter))
    {
      line_iter_prev_cluster (state, &state->gap_start_iter);
      state->gap_start_x -= get_cluster_width (&state->gap_start_iter);
    }

  while (!ends_at_ellipsization_boundary (state, &state->gap_end_iter))
    {
      line_iter_next_cluster (state, &state->gap_end_iter);
      state->gap_end_x += get_cluster_width (&state->gap_end_iter);
    }

  update_ellipsis_shape (state);
}

/* Removes one span from the start or end of the gap. Returns FALSE if
 * there's nothing left to remove in either direction. */
static int
remove_one_span (EllipsizeState *state)
{
  LineIter new_gap_start_iter, new_gap_end_iter;
  int new_gap_start_x, new_gap_end_x, width = 0;

  /* Find one span backwards and forward from the gap */
  new_gap_start_iter = state->gap_start_iter;
  new_gap_start_x = state->gap_start_x;
  do
    {
      if (!line_iter_prev_cluster (state, &new_gap_start_iter))
        break;
      width = get_cluster_width (&new_gap_start_iter);
      new_gap_start_x -= width;
    }
  while (!starts_at_ellipsization_boundary (state, &new_gap_start_iter) || width == 0);

  new_gap_end_iter = state->gap_end_iter;
  new_gap_end_x = state->gap_end_x;
  do
    {
      if (!line_iter_next_cluster (state, &new_gap_end_iter))
        break;
      width = get_cluster_width (&new_gap_end_iter);
      new_gap_end_x += width;
    }
  while (!ends_at_ellipsization_boundary (state, &new_gap_end_iter) || width == 0);

  if (state->gap_end_x == new_gap_end_x && state->gap_start_x == new_gap_start_x)
    return FALSE;

  /* In the case where we could remove a span from either end of the gap,
   * we look at which causes the smaller increase in
   * MAX (gap_end - gap_center, gap_start - gap_center) */
  if (state->gap_end_x == new_gap_end_x ||
      (state->gap_start_x != new_gap_start_x &&
       state->gap_center - new_gap_start_x < new_gap_end_x - state->gap_center))
    {
      state->gap_start_iter = new_gap_start_iter;
      state->gap_start_x = new_gap_start_x;

      update_ellipsis_shape (state);
    }
  else
    {
      state->gap_end_iter = new_gap_end_iter;
      state->gap_end_x = new_gap_end_x;
    }

  return TRUE;
}

/* Fixes up the properties of the ellipsis run once we've determined the
 * final extents of the gap */
static void
fixup_ellipsis_run (EllipsizeState *state, int extra_width)
{
  NtGlyphString *glyphs = state->ellipsis_run->glyphs;
  NtItem *item = state->ellipsis_run->item;
  int level, i;

  /* Make the entire glyphstring into a single logical cluster */
  for (i = 0; i < glyphs->num_glyphs; i++)
    {
      glyphs->log_clusters[i] = 0;
      glyphs->glyphs[i].is_cluster_start = FALSE;
    }

  if (glyphs->num_glyphs > 0)
    {
      glyphs->glyphs[0].is_cluster_start = TRUE;
      glyphs->glyphs[glyphs->num_glyphs - 1].width += extra_width;
    }

  /* Fix up the item to point to the entire elided text */
  item->offset = state->gap_start_iter.run_iter.start_index;
  item->length = state->gap_end_iter.run_iter.end_index - item->offset;
  item->num_chars = nt_utf8_strlen (state->layout->text + item->offset, item->length);
  item->char_offset = state->run_info[state->gap_start_iter.run_index].start_offset +
                      state->gap_start_iter.run_iter.start_char;

  /* The level for the item is the minimum level of the elided text */
  level = 0x7fffffff;
  for (i = state->gap_start_iter.run_index; i <= state->gap_end_iter.run_index; i++)
    level = NT_MIN (level, state->run_info[i].run->item->level);

  item->level = (uint8_t) level;

  item->flags |= NT_ITEM_IS_ELLIPSIS;
}

/* Computes the new list of runs for the line */
static void
set_run_list (EllipsizeState *state, NtLine *line)
{
  NtRun *partial_start_run = NULL, *partial_end_run = NULL;
  RunInfo *run_info;
  NtClusterIter *run_iter;
  NtRun **result = nt_new (NtRun *, state->n_runs + 3);
  int n = 0, i;

  /* We first cut out the pieces of the starting and ending runs we want to
   * preserve; we do the end first in case the end and the start are the
   * same. Doing the start first would disturb the indices for the end. */
  run_info = &state->run_info[state->gap_end_iter.run_index];
  run_iter = &state->gap_end_iter.run_iter;
  if (run_iter->end_char != run_info->run->item->num_chars)
    {
      partial_end_run = run_info->run;
      run_info->run = nt_glyph_item_split (run_info->run, state->layout->text,
                                 run_iter->end_index - run_info->run->item->offset);
    }

  run_info = &state->run_info[state->gap_start_iter.run_index];
  run_iter = &state->gap_start_iter.run_iter;
  if (run_iter->start_char != 0)
    partial_start_run = nt_glyph_item_split (run_info->run, state->layout->text,
                                   run_iter->start_index - run_info->run->item->offset);

  /* Now assemble the new list of runs */
  for (i = 0; i < state->gap_start_iter.run_index; i++)
    result[n++] = state->run_info[i].run;

  if (partial_start_run)
    result[n++] = partial_start_run;

  result[n++] = state->ellipsis_run;

  if (partial_end_run)
    result[n++] = partial_end_run;

  for (i = state->gap_end_iter.run_index + 1; i < state->n_runs; i++)
    result[n++] = state->run_info[i].run;

  /* And free the ones we didn't use */
  for (i = state->gap_start_iter.run_index; i <= state->gap_end_iter.run_index; i++)
    run_free (state->run_info[i].run);

  free (line->runs);
  line->runs = result;
  line->n_runs = n;
  line->cap_runs = state->n_runs + 3;
}

/* Computes the width of the line as currently ellipsized */
static int
current_width (EllipsizeState *state)
{
  return state->total_width - (state->gap_end_x - state->gap_start_x) + state->ellipsis_width;
}

/* Pango's _pango_layout_line_ellipsize. */
int
nt_line_ellipsize (NtLine *line, const NsPangoAttrList *attrs, int goal_width)
{
  EllipsizeState state;
  int is_ellipsized = FALSE;

  if (line->pub.layout->ellipsize == NS_PANGO_ELLIPSIZE_NONE || goal_width < 0 ||
      line->n_runs == 0)
    return FALSE;

  init_state (&state, line, attrs);

  if (state.total_width <= goal_width)
    goto out;

  find_initial_span (&state);

  while (current_width (&state) > goal_width)
    {
      if (!remove_one_span (&state))
        break;
    }

  fixup_ellipsis_run (&state, NT_MAX (goal_width - current_width (&state), 0));

  set_run_list (&state, line);
  is_ellipsized = TRUE;

 out:
  free_state (&state);
  return is_ellipsized;
}
