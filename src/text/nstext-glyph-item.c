/* Weststar SDL Browser — nstext: runs (Pango's glyph items). The cluster
 * iterator and the split of a run, as Pango's pango-glyph-item.c, ported
 * off GLib; ellipsizing and justification walk runs with them.
 *
 * Copyright (C) 2002 Red Hat Software
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

#define RUN_LTR(r) (((r)->item->level & 1) == 0)

int
nt_cluster_iter_next (NtClusterIter *iter)
{
  int glyph_index = iter->end_glyph;
  NtGlyphString *glyphs = iter->run->glyphs;
  NtItem *item = iter->run->item;
  int cluster;

  if (RUN_LTR (iter->run))
    {
      if (glyph_index == glyphs->num_glyphs)
        return FALSE;
    }
  else
    {
      if (glyph_index < 0)
        return FALSE;
    }

  iter->start_glyph = iter->end_glyph;
  iter->start_index = iter->end_index;
  iter->start_char = iter->end_char;

  cluster = glyphs->log_clusters[glyph_index];
  for (;;)
    {
      glyph_index += RUN_LTR (iter->run) ? 1 : -1;

      if (glyph_index < 0 || glyph_index == glyphs->num_glyphs)
        {
          iter->end_index = item->offset + item->length;
          iter->end_char = item->num_chars;
          break;
        }

      if (glyphs->log_clusters[glyph_index] > cluster)
        {
          iter->end_index = item->offset + glyphs->log_clusters[glyph_index];
          iter->end_char += nt_utf8_strlen (iter->text + iter->start_index,
                                            iter->end_index - iter->start_index);
          break;
        }
    }

  iter->end_glyph = glyph_index;
  return TRUE;
}

int
nt_cluster_iter_prev (NtClusterIter *iter)
{
  int glyph_index = iter->start_glyph;
  NtGlyphString *glyphs = iter->run->glyphs;
  NtItem *item = iter->run->item;
  int cluster;

  if (RUN_LTR (iter->run))
    {
      if (glyph_index == 0)
        return FALSE;
    }
  else
    {
      if (glyph_index == glyphs->num_glyphs - 1)
        return FALSE;
    }

  iter->end_glyph = iter->start_glyph;
  iter->end_index = iter->start_index;
  iter->end_char = iter->start_char;

  if (RUN_LTR (iter->run))
    {
      cluster = glyphs->log_clusters[glyph_index - 1];
      for (;;)
        {
          if (glyph_index == 0)
            {
              iter->start_index = item->offset;
              iter->start_char = 0;
              break;
            }

          glyph_index--;

          if (glyphs->log_clusters[glyph_index] < cluster)
            {
              glyph_index++;
              iter->start_index = item->offset + glyphs->log_clusters[glyph_index];
              iter->start_char -= nt_utf8_strlen (iter->text + iter->start_index,
                                                  iter->end_index - iter->start_index);
              break;
            }
        }
    }
  else
    {
      cluster = glyphs->log_clusters[glyph_index + 1];
      for (;;)
        {
          if (glyph_index == glyphs->num_glyphs - 1)
            {
              iter->start_index = item->offset;
              iter->start_char = 0;
              break;
            }

          glyph_index++;

          if (glyphs->log_clusters[glyph_index] < cluster)
            {
              glyph_index--;
              iter->start_index = item->offset + glyphs->log_clusters[glyph_index];
              iter->start_char -= nt_utf8_strlen (iter->text + iter->start_index,
                                                  iter->end_index - iter->start_index);
              break;
            }
        }
    }

  iter->start_glyph = glyph_index;
  return TRUE;
}

int
nt_cluster_iter_init_start (NtClusterIter *iter, NtRun *run, const char *text)
{
  iter->run = run;
  iter->text = text;
  iter->end_glyph = RUN_LTR (run) ? 0 : run->glyphs->num_glyphs - 1;
  iter->end_index = run->item->offset;
  iter->end_char = 0;
  iter->start_glyph = iter->end_glyph;
  iter->start_index = iter->end_index;
  iter->start_char = iter->end_char;
  return nt_cluster_iter_next (iter);
}

int
nt_cluster_iter_init_end (NtClusterIter *iter, NtRun *run, const char *text)
{
  iter->run = run;
  iter->text = text;
  iter->start_glyph = RUN_LTR (run) ? run->glyphs->num_glyphs : -1;
  iter->start_index = run->item->offset + run->item->length;
  iter->start_char = run->item->num_chars;
  iter->end_glyph = iter->start_glyph;
  iter->end_index = iter->start_index;
  iter->end_char = iter->start_char;
  return nt_cluster_iter_prev (iter);
}

/* Pango's pango_glyph_item_split: the first split_index bytes of orig
 * (snapped to a cluster start) become a new run, which is returned. */
NtRun *
nt_glyph_item_split (NtRun *orig, const char *text, int split_index)
{
  NtGlyphString *gs = orig->glyphs;
  NtRun *first;
  int i, num_glyphs, num_remaining, split_offset;

  if (RUN_LTR (orig))
    {
      for (i = 0; i < gs->num_glyphs; i++)
        if (gs->log_clusters[i] >= split_index)
          break;
      if (i == gs->num_glyphs)
        return NULL;
      split_index = gs->log_clusters[i];
      num_glyphs = i;
    }
  else
    {
      for (i = gs->num_glyphs - 1; i >= 0; i--)
        if (gs->log_clusters[i] >= split_index)
          break;
      if (i < 0)
        return NULL;
      split_index = gs->log_clusters[i];
      num_glyphs = gs->num_glyphs - 1 - i;
    }

  num_remaining = gs->num_glyphs - num_glyphs;

  first = nt_new0 (NtRun, 1);
  split_offset = nt_utf8_strlen (text + orig->item->offset, split_index);
  first->item = nt_item_split (orig->item, split_index, split_offset);

  first->glyphs = nt_glyph_string_new ();
  nt_glyph_string_set_size (first->glyphs, num_glyphs);

  if (RUN_LTR (orig))
    {
      memcpy (first->glyphs->glyphs, gs->glyphs, (size_t) num_glyphs * sizeof *gs->glyphs);
      memcpy (first->glyphs->log_clusters, gs->log_clusters, (size_t) num_glyphs * sizeof (int));
      memmove (gs->glyphs, gs->glyphs + num_glyphs, (size_t) num_remaining * sizeof *gs->glyphs);
      for (i = num_glyphs; i < gs->num_glyphs; i++)
        gs->log_clusters[i - num_glyphs] = gs->log_clusters[i] - split_index;
    }
  else
    {
      memcpy (first->glyphs->glyphs, gs->glyphs + num_remaining,
              (size_t) num_glyphs * sizeof *gs->glyphs);
      memcpy (first->glyphs->log_clusters, gs->log_clusters + num_remaining,
              (size_t) num_glyphs * sizeof (int));
      for (i = 0; i < num_remaining; i++)
        gs->log_clusters[i] = gs->log_clusters[i] - split_index;
    }

  gs->num_glyphs -= num_glyphs;

  first->y_offset = orig->y_offset;
  first->start_x_offset = orig->start_x_offset;
  first->end_x_offset = -orig->start_x_offset;
  return first;
}
