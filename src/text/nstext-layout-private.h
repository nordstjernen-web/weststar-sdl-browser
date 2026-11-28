/* Weststar SDL Browser — nstext: the layout's internals, shared with the
 * renderer.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_TEXT_LAYOUT_PRIVATE_H
#define NS_TEXT_LAYOUT_PRIVATE_H

#include "nstext-private.h"

/* A run: an item and its glyphs (Pango's PangoGlyphItem). */
typedef struct
{
  NtItem        *item;
  NtGlyphString *glyphs;
  int            y_offset;       /* rise, Pango units, up */
  int            start_x_offset;
  int            end_x_offset;
} NtRun;

typedef struct
{
  NsPangoLayoutLine pub;  /* first: the engine's view */
  int               ref_count;
  NtRun           **runs; /* logical order while built, visual after */
  int               n_runs, cap_runs;
  int               cached;
  NsPangoRectangle  ink, logical;
  int               height;
} NtLine;

typedef struct
{
  int              baseline;
  NsPangoRectangle ink_rect;
  NsPangoRectangle logical_rect;
} NtExtents;

struct _PangoLayout
{
  int                     ref_count;
  NsPangoContext         *context;
  unsigned int            context_serial;

  char                   *text;
  int                     length;
  int                     n_chars;
  NsPangoAttrList        *attrs;
  NsPangoFontDescription *font_desc;
  NsPangoTabArray        *tabs;

  int                     width;
  int                     height;
  int                     indent;
  int                     spacing;
  float                   line_spacing;
  int                     justify;
  NsPangoAlignment        alignment;
  int                     auto_dir;
  NsPangoWrapMode         wrap;
  NsPangoEllipsizeMode    ellipsize;

  int                     tab_width;
  uint32_t                decimal;

  NsPangoLogAttr         *log_attrs;
  NtLine                **lines;
  int                     n_lines;
  int                     have_lines;

  int                     logical_cached, ink_cached;
  NsPangoRectangle        logical_rect, ink_rect;
  int                     is_wrapped, is_ellipsized;
};

void nt_layout_check_lines (NsPangoLayout *layout);
void nt_run_get_extents (const NtRun *run, NsPangoRectangle *ink, NsPangoRectangle *logical);
/* The extents of every line in layout coordinates (malloc'ed). */
NtExtents *nt_layout_line_extents (NsPangoLayout *layout, int *layout_width);

#endif
