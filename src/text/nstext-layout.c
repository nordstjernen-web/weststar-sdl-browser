/* Weststar SDL Browser — nstext: layouts. Paragraphs are itemized,
 * shaped and broken into lines at the opportunities of UAX #14 with
 * Pango's greedy algorithm (pango-layout.c: process_item, process_line),
 * so that lines break where the fork breaks them; then lines are
 * hyphenated, ellipsized, reordered for bidi and justified, and measured
 * and hit-tested as Pango does.
 *
 * Copyright (C) 2000, 2001, 2006 Red Hat Software
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "nstext-layout-private.h"

#include <locale.h>
#include <math.h>
#include <stdio.h>

NtFontset *nt_context_load_fontset (NsPangoContext *context, const NsPangoFontDescription *desc,
                                    NsPangoLanguage *language);

/* ---- runs and lines ---------------------------------------------------- */

static void
run_free (NtRun *run)
{
  if (!run)
    return;
  nt_item_free (run->item);
  nt_glyph_string_free (run->glyphs);
  free (run);
}

static NtLine *
line_new (NsPangoLayout *layout)
{
  NtLine *line = nt_new0 (NtLine, 1);

  line->pub.layout = layout;
  line->ref_count = 1;
  return line;
}

static void
line_free (NtLine *line)
{
  int i;

  for (i = 0; i < line->n_runs; i++)
    run_free (line->runs[i]);
  free (line->runs);
  free (line);
}

static void
line_append_run (NtLine *line, NtRun *run)
{
  if (line->n_runs == line->cap_runs)
    {
      line->cap_runs = line->cap_runs ? line->cap_runs * 2 : 8;
      line->runs = nt_renew (NtRun *, line->runs, line->cap_runs);
    }
  line->runs[line->n_runs++] = run;
}

/* ---- item properties --------------------------------------------------- */

typedef struct
{
  int uline_single, uline_double, uline_low, uline_error;
  int oline_single;
  int strikethrough;
  int letter_spacing;
  int word_spacing;
  const NtAttr *shape;
} ItemProperties;

static void
get_item_properties (const NtItem *item, ItemProperties *p)
{
  int i;

  memset (p, 0, sizeof *p);
  for (i = 0; i < item->n_extra; i++)
    {
      const NtAttr *a = item->extra_attrs[i];

      switch ((NtAttrType) a->base.type)
        {
        case NT_ATTR_UNDERLINE:
          switch ((NsPangoUnderline) a->v.i)
            {
            case NS_PANGO_UNDERLINE_SINGLE:
            case NS_PANGO_UNDERLINE_SINGLE_LINE:
              p->uline_single = TRUE;
              break;
            case NS_PANGO_UNDERLINE_DOUBLE:
            case NS_PANGO_UNDERLINE_DOUBLE_LINE:
              p->uline_double = TRUE;
              break;
            case NS_PANGO_UNDERLINE_LOW:
              p->uline_low = TRUE;
              break;
            case NS_PANGO_UNDERLINE_ERROR:
            case NS_PANGO_UNDERLINE_ERROR_LINE:
              p->uline_error = TRUE;
              break;
            case NS_PANGO_UNDERLINE_NONE:
            default:
              break;
            }
          break;
        case NT_ATTR_OVERLINE:
          p->oline_single = a->v.i == NS_PANGO_OVERLINE_SINGLE;
          break;
        case NT_ATTR_STRIKETHROUGH:
          p->strikethrough = a->v.i;
          break;
        case NT_ATTR_LETTER_SPACING:
          p->letter_spacing = a->v.i;
          break;
        case NT_ATTR_WORD_SPACING:
          p->word_spacing = a->v.i;
          break;
        case NT_ATTR_SHAPE:
          p->shape = a;
          break;
        default:
          break;
        }
    }
}

static void
shape_get_extents (int n_chars, const NsPangoRectangle *shape_ink,
                   const NsPangoRectangle *shape_logical, NsPangoRectangle *ink,
                   NsPangoRectangle *logical)
{
  if (n_chars > 0)
    {
      if (ink)
        {
          ink->x = NT_MIN (shape_ink->x, shape_ink->x + shape_logical->width * (n_chars - 1));
          ink->width = NT_MAX (shape_ink->width,
                               shape_ink->width + shape_logical->width * (n_chars - 1));
          ink->y = shape_ink->y;
          ink->height = shape_ink->height;
        }
      if (logical)
        {
          logical->x = NT_MIN (shape_logical->x,
                               shape_logical->x + shape_logical->width * (n_chars - 1));
          logical->width = NT_MAX (shape_logical->width, shape_logical->width * n_chars);
          logical->y = shape_logical->y;
          logical->height = shape_logical->height;
        }
    }
  else
    {
      if (ink)
        ink->x = ink->y = ink->width = ink->height = 0;
      if (logical)
        logical->x = logical->y = logical->width = logical->height = 0;
    }
}

/* ---- layouts ----------------------------------------------------------- */

NsPangoLayout *
ns_pango_layout_new (NsPangoContext *context)
{
  NsPangoLayout *layout = nt_new0 (NsPangoLayout, 1);

  layout->ref_count = 1;
  layout->context = ns_pango_context_ref (context);
  layout->context_serial = context->serial;
  layout->text = nt_strdup ("");
  layout->width = -1;
  layout->height = -1;
  layout->alignment = NS_PANGO_ALIGN_LEFT;
  layout->auto_dir = TRUE;
  layout->wrap = NS_PANGO_WRAP_WORD;
  layout->ellipsize = NS_PANGO_ELLIPSIZE_NONE;
  layout->tab_width = -1;
  return layout;
}

static void
layout_clear_lines (NsPangoLayout *layout)
{
  int i;

  for (i = 0; i < layout->n_lines; i++)
    {
      NtLine *line = layout->lines[i];

      line->pub.layout = NULL;
      if (--line->ref_count == 0)
        line_free (line);
    }
  free (layout->lines);
  layout->lines = NULL;
  layout->n_lines = 0;
  layout->have_lines = FALSE;
  layout->logical_cached = layout->ink_cached = FALSE;
  layout->is_wrapped = layout->is_ellipsized = FALSE;
}

static void
layout_changed (NsPangoLayout *layout)
{
  layout_clear_lines (layout);
  free (layout->log_attrs);
  layout->log_attrs = NULL;
  layout->tab_width = -1;
}

NsPangoLayout *
ns_pango_layout_ref (NsPangoLayout *layout)
{
  if (layout)
    SDL_AddAtomicInt ((SDL_AtomicInt *) (void *) &layout->ref_count, 1);
  return layout;
}

void
ns_pango_layout_unref (NsPangoLayout *layout)
{
  if (!layout || SDL_AddAtomicInt ((SDL_AtomicInt *) (void *) &layout->ref_count, -1) != 1)
    return;
  layout_changed (layout);
  ns_pango_context_unref (layout->context);
  ns_pango_attr_list_unref (layout->attrs);
  ns_pango_font_description_free (layout->font_desc);
  ns_pango_tab_array_free (layout->tabs);
  free (layout->text);
  free (layout);
}

NsPangoContext *
ns_pango_layout_get_context (NsPangoLayout *layout)
{
  return layout->context;
}

void
ns_pango_layout_context_changed (NsPangoLayout *layout)
{
  layout_changed (layout);
  layout->context_serial = layout->context->serial;
}

static void
check_context_changed (NsPangoLayout *layout)
{
  nt_context_check_fontmap (layout->context);
  if (layout->context_serial != layout->context->serial)
    {
      layout->context_serial = layout->context->serial;
      layout_changed (layout);
    }
}

void
ns_pango_layout_set_text (NsPangoLayout *layout, const char *text, int length)
{
  char *valid;
  int n;

  if (!text)
    text = "";
  if (length < 0)
    length = (int) strlen (text);
  valid = nt_utf8_make_valid (text, length, &n);
  if (n == layout->length && strcmp (valid, layout->text) == 0)
    {
      free (valid);
      return;
    }
  free (layout->text);
  layout->text = valid;
  layout->length = n;
  layout->n_chars = nt_utf8_strlen (valid, n);
  layout_changed (layout);
}

const char *
ns_pango_layout_get_text (NsPangoLayout *layout)
{
  return layout->text;
}

void
ns_pango_layout_set_attributes (NsPangoLayout *layout, NsPangoAttrList *attrs)
{
  NsPangoAttrList *old = layout->attrs;

  if (!old && !attrs)
    return;
  layout->attrs = ns_pango_attr_list_ref (attrs);
  ns_pango_attr_list_unref (old);
  layout_changed (layout);
}

void
ns_pango_layout_set_font_description (NsPangoLayout *layout,
                                      const NsPangoFontDescription *desc)
{
  if (desc == layout->font_desc)
    return;
  if (desc && layout->font_desc && nt_font_description_equal (desc, layout->font_desc) &&
      desc->mask == layout->font_desc->mask)
    return;
  ns_pango_font_description_free (layout->font_desc);
  layout->font_desc = ns_pango_font_description_copy (desc);
  layout_changed (layout);
}

const NsPangoFontDescription *
ns_pango_layout_get_font_description (NsPangoLayout *layout)
{
  return layout->font_desc;
}

#define SETTER(name, field, type)                                   \
  void ns_pango_layout_set_##name (NsPangoLayout *layout, type v)  \
  {                                                                 \
    if (layout->field != v)                                         \
      {                                                             \
        layout->field = v;                                          \
        layout_changed (layout);                                    \
      }                                                             \
  }

SETTER (height, height, int)
SETTER (wrap, wrap, NsPangoWrapMode)
SETTER (indent, indent, int)
SETTER (line_spacing, line_spacing, float)
SETTER (alignment, alignment, NsPangoAlignment)
SETTER (ellipsize, ellipsize, NsPangoEllipsizeMode)

void
ns_pango_layout_set_width (NsPangoLayout *layout, int width)
{
  if (width < 0)
    width = -1;
  if (layout->width != width)
    {
      layout->width = width;
      layout_changed (layout);
    }
}

int ns_pango_layout_get_width (NsPangoLayout *layout) { return layout->width; }

void
ns_pango_layout_set_justify (NsPangoLayout *layout, int justify)
{
  justify = justify != 0;
  if (layout->justify != justify)
    {
      layout->justify = justify;
      layout_changed (layout);
    }
}

int ns_pango_layout_get_justify (NsPangoLayout *layout) { return layout->justify; }
NsPangoAlignment ns_pango_layout_get_alignment (NsPangoLayout *layout) { return layout->alignment; }

void
ns_pango_layout_set_auto_dir (NsPangoLayout *layout, int auto_dir)
{
  auto_dir = auto_dir != 0;
  if (layout->auto_dir != auto_dir)
    {
      layout->auto_dir = auto_dir;
      layout_changed (layout);
    }
}

void
ns_pango_layout_set_tabs (NsPangoLayout *layout, NsPangoTabArray *tabs)
{
  NsPangoTabArray *copy = NULL;

  if (tabs)
    {
      copy = ns_pango_tab_array_new (tabs->n, tabs->in_pixels);
      if (tabs->n)
        memcpy (copy->tabs, tabs->tabs, (size_t) tabs->n * sizeof *tabs->tabs);
    }
  ns_pango_tab_array_free (layout->tabs);
  layout->tabs = copy;
  layout_changed (layout);
}

/* The layout's attributes with its font description first, as Pango's
 * get_effective_attributes. */
static NsPangoAttrList *
effective_attributes (NsPangoLayout *layout)
{
  NsPangoAttrList *list = ns_pango_attr_list_new ();
  int i;

  if (layout->font_desc)
    {
      NsPangoAttribute *a = ns_pango_attr_font_desc_new (layout->font_desc);

      a->start_index = 0;
      a->end_index = (unsigned) -1;
      /* insert_before: ahead of everything starting at 0 */
      list->attrs = nt_new (NtAttr *, 8);
      list->cap = 8;
      list->attrs[list->n++] = (NtAttr *) (void *) a;
    }
  if (layout->attrs)
    for (i = 0; i < layout->attrs->n; i++)
      ns_pango_attr_list_insert (list, &nt_attr_copy (layout->attrs->attrs[i])->base);
  return list;
}

static int
affects_itemization (int type)
{
  switch ((NtAttrType) type)
    {
    case NT_ATTR_LANGUAGE: case NT_ATTR_FAMILY: case NT_ATTR_STYLE:
    case NT_ATTR_WEIGHT: case NT_ATTR_VARIANT: case NT_ATTR_STRETCH:
    case NT_ATTR_ABSOLUTE_SIZE: case NT_ATTR_FONT_DESC:
    case NT_ATTR_LETTER_SPACING: case NT_ATTR_WORD_SPACING:
    case NT_ATTR_SHAPE: case NT_ATTR_RISE:
      return TRUE;
    default:
      return FALSE;
    }
}

static int
affects_break_or_shape (int type)
{
  return type == NT_ATTR_ALLOW_BREAKS || type == NT_ATTR_INSERT_HYPHENS ||
         type == NT_ATTR_FONT_FEATURES;
}

static int
affects_drawing (int type)
{
  switch ((NtAttrType) type)
    {
    case NT_ATTR_FOREGROUND: case NT_ATTR_BACKGROUND: case NT_ATTR_UNDERLINE:
    case NT_ATTR_STRIKETHROUGH: case NT_ATTR_UNDERLINE_COLOR:
    case NT_ATTR_STRIKETHROUGH_COLOR: case NT_ATTR_FOREGROUND_ALPHA:
    case NT_ATTR_BACKGROUND_ALPHA: case NT_ATTR_OVERLINE: case NT_ATTR_OVERLINE_COLOR:
      return TRUE;
    default:
      return FALSE;
    }
}

static NsPangoAttrList *
filter_attrs (const NsPangoAttrList *list, int (*pred) (int))
{
  NsPangoAttrList *out = NULL;
  int i;

  for (i = 0; list && i < list->n; i++)
    if (pred (list->attrs[i]->base.type))
      {
        if (!out)
          out = ns_pango_attr_list_new ();
        ns_pango_attr_list_insert (out, &nt_attr_copy (list->attrs[i])->base);
      }
  return out;
}

/* ---- the font at an index (empty lines, tabs) -------------------------- */

static NsPangoFontDescription *
font_desc_at (NsPangoLayout *layout, int index, NsPangoLanguage **lang)
{
  NsPangoFontDescription *desc = ns_pango_font_description_copy (layout->context->font_desc);

  if (lang)
    *lang = NULL;
  if (layout->font_desc)
    nt_font_description_merge (desc, layout->font_desc, TRUE);
  if (layout->attrs)
    {
      NtAttrIter it;

      nt_attr_iter_init (&it, layout->attrs);
      do
        {
          if ((int) it.start <= index && (unsigned) index < it.end)
            {
              nt_attr_iter_get_font (&it, desc, lang);
              break;
            }
        }
      while (nt_attr_iter_next (&it));
      nt_attr_iter_destroy (&it);
    }
  return desc;
}

static NtFont *
load_font (NsPangoContext *context, const NsPangoFontDescription *desc, NsPangoLanguage *lang)
{
  NtFontset *fs = nt_context_load_fontset (context, desc, lang ? lang : context->language);
  NtFont *font = NULL;
  int i;

  for (i = 0; i < 4 && !font; i++)
    font = nt_fontset_get_font_at (fs, i);
  nt_font_ref (font);
  nt_fontset_unref (fs);
  return font;
}

static void
empty_extents_at_index (NsPangoLayout *layout, int index, NsPangoRectangle *logical, int *height)
{
  NsPangoLanguage *lang;
  NsPangoFontDescription *desc = font_desc_at (layout, index, &lang);
  NtFont *font = load_font (layout->context, desc, lang);

  logical->x = logical->width = 0;
  if (font)
    {
      NsPangoFontMetrics *m = nt_font_get_metrics (font);

      logical->y = - m->ascent;
      logical->height = m->ascent + m->descent;
      if (height)
        *height = m->height;
      ns_pango_font_metrics_unref (m);
      nt_font_unref (font);
    }
  else
    {
      logical->y = logical->height = 0;
      if (height)
        *height = 0;
    }
  ns_pango_font_description_free (desc);
}

/* ---- the paragraph breaking state -------------------------------------- */

typedef struct
{
  NtGlyphString  *glyphs;  /* the tab run's glyphs */
  int             index;
  int             width;
  int             pos;
  NsPangoTabAlign align;
  uint32_t        decimal;
} LastTabState;

typedef enum
{
  BREAK_NONE_FIT,
  BREAK_SOME_FIT,
  BREAK_ALL_FIT,
  BREAK_EMPTY_FIT,
  BREAK_LINE_SEPARATOR
} BreakResult;

typedef struct
{
  int line_height;
  int remaining_height;

  NtItem       **items;      /* items[head .. n) are left */
  int            head, n, cap;
  NsPangoDirection base_dir;
  int            line_of_par;

  NtGlyphString *glyphs;
  int            start_offset;
  ItemProperties properties;
  int           *log_widths;
  int            num_log_widths;
  int            log_widths_offset;

  int            line_start_index;
  int            line_start_offset;

  const NsPangoAttrList *attrs; /* the itemization attributes */

  int            line_width;
  int            remaining_width;
  int            hyphen_width;

  LastTabState   last_tab;
} ParaBreakState;

static NtItem *
state_first (ParaBreakState *s)
{
  return s->head < s->n ? s->items[s->head] : NULL;
}

/* The item at the head, when there is one. */
static NtItem *
state_head (ParaBreakState *s)
{
  return s->items[s->head];
}

static int
state_has_next (ParaBreakState *s)
{
  return s->head + 1 < s->n;
}

static void
state_push_front (ParaBreakState *s, NtItem *item)
{
  if (s->head == 0)
    {
      int grow = 16;

      s->items = nt_renew (NtItem *, s->items, s->cap + grow);
      memmove (s->items + grow, s->items, (size_t) s->n * sizeof *s->items);
      s->head += grow;
      s->n += grow;
      s->cap += grow;
    }
  s->items[--s->head] = item;
}

static void
ensure_tab_width (NsPangoLayout *layout)
{
  if (layout->tab_width == -1)
    {
      NsPangoLanguage *lang;
      NsPangoFontDescription *desc = font_desc_at (layout, 0, &lang);
      NtFontset *fs = nt_context_load_fontset (layout->context, desc,
                                               lang ? lang : layout->context->language);
      int position;
      NtFont *font = nt_fontset_get_font (fs, ' ', &position);
      NtItem item;
      NtGlyphString *glyphs = nt_glyph_string_new ();
      static const char eight_spaces[] = "        ";

      if (!font)
        font = nt_fontset_get_font_at (fs, 0);
      memset (&item, 0, sizeof item);
      item.length = 8;
      item.num_chars = 8;
      item.font = font;
      item.script = HB_SCRIPT_COMMON;
      item.language = lang ? lang : layout->context->language;
      nt_shape_item (eight_spaces, 8, &item, NULL, glyphs);
      layout->tab_width = nt_glyph_string_get_width (glyphs);
      nt_glyph_string_free (glyphs);
      nt_fontset_unref (fs);
      ns_pango_font_description_free (desc);
      if (layout->tab_width <= 0)
        layout->tab_width = 50 * NS_PANGO_SCALE;
    }
}

static void
get_tab_pos (NtLine *line, int index, int *tab_pos, NsPangoTabAlign *alignment,
             uint32_t *decimal, int *is_default)
{
  NsPangoLayout *layout = line->pub.layout;
  int n_tabs = 0, in_pixels = FALSE, offset = 0;

  if (layout->alignment != NS_PANGO_ALIGN_CENTER)
    {
      if (line->pub.is_paragraph_start && layout->indent >= 0)
        offset = layout->indent;
      else if (!line->pub.is_paragraph_start && layout->indent < 0)
        offset = - layout->indent;
    }

  if (layout->tabs)
    {
      n_tabs = layout->tabs->n;
      in_pixels = layout->tabs->in_pixels;
      *is_default = FALSE;
    }
  else
    *is_default = TRUE;

  *decimal = 0;
  if (layout->tabs && index < n_tabs)
    {
      *tab_pos = layout->tabs->tabs[index].location;
      *alignment = layout->tabs->tabs[index].align;
      if (in_pixels)
        *tab_pos *= NS_PANGO_SCALE;
    }
  else if (layout->tabs && n_tabs > 0)
    {
      int last_pos = layout->tabs->tabs[n_tabs - 1].location;
      int next_to_last_pos = n_tabs > 1 ? layout->tabs->tabs[n_tabs - 2].location : 0;
      int tab_width;

      *alignment = layout->tabs->tabs[n_tabs - 1].align;
      if (in_pixels)
        {
          next_to_last_pos *= NS_PANGO_SCALE;
          last_pos *= NS_PANGO_SCALE;
        }
      if (last_pos > next_to_last_pos)
        tab_width = last_pos - next_to_last_pos;
      else
        tab_width = layout->tab_width;
      *tab_pos = last_pos + tab_width * (index - n_tabs + 1);
    }
  else
    {
      *tab_pos = layout->tab_width * index;
      *alignment = NS_PANGO_TAB_LEFT;
    }
  *tab_pos -= offset;
}

static void
shape_tab (NtLine *line, LastTabState *tab_state, int current_width, NtGlyphString *glyphs)
{
  NsPangoLayout *layout = line->pub.layout;
  int i, space_width, tab_pos = 0;
  NsPangoTabAlign tab_align = NS_PANGO_TAB_LEFT;
  uint32_t tab_decimal = 0;

  nt_glyph_string_set_size (glyphs, 1);
  glyphs->glyphs[0].glyph = NT_GLYPH_EMPTY;
  glyphs->glyphs[0].x_offset = 0;
  glyphs->glyphs[0].y_offset = 0;
  glyphs->glyphs[0].is_cluster_start = 1;
  glyphs->glyphs[0].is_color = 0;
  glyphs->log_clusters[0] = 0;

  ensure_tab_width (layout);
  space_width = layout->tab_width / 8;

  for (i = tab_state->index; ; i++)
    {
      int is_default;

      get_tab_pos (line, i, &tab_pos, &tab_align, &tab_decimal, &is_default);
      if (tab_pos >= current_width + (is_default ? space_width : 1))
        {
          glyphs->glyphs[0].width = tab_pos - current_width;
          break;
        }
    }

  if (tab_decimal == 0)
    {
      if (layout->decimal == 0)
        {
          const char *dp = localeconv ()->decimal_point;

          layout->decimal = dp && *dp ? nt_utf8_get_char (dp) : '.';
        }
      tab_decimal = layout->decimal;
    }

  tab_state->glyphs = glyphs;
  tab_state->index = i;
  tab_state->width = current_width;
  tab_state->pos = tab_pos;
  tab_state->align = tab_align;
  tab_state->decimal = tab_decimal;
}

static int
can_break_at (NsPangoLayout *layout, int offset, NsPangoWrapMode wrap)
{
  if (offset == layout->n_chars)
    return TRUE;
  if (wrap == NS_PANGO_WRAP_NONE)
    return FALSE;
  if (wrap == NS_PANGO_WRAP_CHAR)
    return layout->log_attrs[offset].is_char_break;
  return layout->log_attrs[offset].is_line_break;
}

static int
can_break_in (NsPangoLayout *layout, int start_offset, int num_chars, int allow_break_at_start)
{
  int i;

  for (i = allow_break_at_start ? 0 : 1; i < num_chars; i++)
    if (can_break_at (layout, start_offset + i, layout->wrap))
      return TRUE;
  return FALSE;
}

static int
should_ellipsize_current_line (NsPangoLayout *layout, ParaBreakState *state)
{
  if (layout->ellipsize == NS_PANGO_ELLIPSIZE_NONE || layout->width < 0)
    return FALSE;
  if (layout->wrap == NS_PANGO_WRAP_NONE)
    return TRUE;
  if (layout->height >= 0)
    return state->line_height * 2 > state->remaining_height;
  return state->line_of_par == - layout->height;
}

static void
get_decimal_prefix_width (const NtItem *item, const NtGlyphString *glyphs, const char *text,
                          uint32_t decimal, int *width, int *found)
{
  int *log_widths = nt_new (int, item->num_chars ? item->num_chars : 1);
  const char *p;
  int i;

  nt_glyph_logical_widths (glyphs, text, item, log_widths);
  *width = 0;
  *found = FALSE;
  for (i = 0, p = text + item->offset; i < item->num_chars; i++, p = nt_utf8_next_char (p))
    {
      if (nt_utf8_get_char (p) == decimal)
        {
          *width += log_widths[i] / 2;
          *found = TRUE;
          break;
        }
      *width += log_widths[i];
    }
  free (log_widths);
}

static int
line_width_so_far (ParaBreakState *state, NtLine *line)
{
  int i, width = 0;

  if (state->remaining_width > -1)
    return state->line_width - state->remaining_width;
  for (i = 0; i < line->n_runs; i++)
    width += nt_glyph_string_get_width (line->runs[i]->glyphs);
  return width;
}

static NtGlyphString *
shape_run (NtLine *line, ParaBreakState *state, NtItem *item)
{
  NsPangoLayout *layout = line->pub.layout;
  NtGlyphString *glyphs = nt_glyph_string_new ();

  if (layout->text[item->offset] == '\t')
    shape_tab (line, &state->last_tab, line_width_so_far (state, line), glyphs);
  else
    {
      if (state->properties.shape)
        nt_shape_shape_attr (layout->text, item, state->properties.shape, glyphs);
      else
        nt_shape_item (layout->text, layout->length, item,
                       layout->log_attrs + state->start_offset, glyphs);

      if (state->properties.letter_spacing)
        nt_glyph_letter_space (layout->text, item, layout->log_attrs + state->start_offset,
                               state->properties.letter_spacing, glyphs);
      if (state->properties.word_spacing)
        nt_glyph_word_space (layout->text, item, state->properties.word_spacing, glyphs);

      if (state->last_tab.glyphs)
        {
          int w = state->last_tab.pos - state->last_tab.width;

          if (state->last_tab.align == NS_PANGO_TAB_RIGHT)
            w -= nt_glyph_string_get_width (glyphs);
          else if (state->last_tab.align == NS_PANGO_TAB_CENTER)
            w -= nt_glyph_string_get_width (glyphs) / 2;
          else if (state->last_tab.align == NS_PANGO_TAB_DECIMAL)
            {
              int width, found;

              get_decimal_prefix_width (item, glyphs, layout->text, state->last_tab.decimal,
                                        &width, &found);
              w -= width;
            }
          state->last_tab.glyphs->glyphs[0].width = NT_MAX (w, 0);
        }
    }
  return glyphs;
}

static void
insert_run (NtLine *line, ParaBreakState *state, NtItem *run_item, NtGlyphString *glyphs,
            int last_run)
{
  NtRun *run = nt_new0 (NtRun, 1);

  run->item = run_item;
  if (glyphs)
    run->glyphs = glyphs;
  else if (last_run && state->log_widths_offset == 0 && state->glyphs &&
           !(run_item->flags & NT_ITEM_NEED_HYPHEN))
    {
      run->glyphs = state->glyphs;
      state->glyphs = NULL;
    }
  else
    run->glyphs = shape_run (line, state, run_item);

  if (last_run && state->glyphs)
    {
      nt_glyph_string_free (state->glyphs);
      state->glyphs = NULL;
    }

  line_append_run (line, run);
  line->pub.length += run_item->length;

  if (state->last_tab.glyphs && run->glyphs != state->last_tab.glyphs)
    {
      int found_decimal = FALSE, width;

      if (state->last_tab.align == NS_PANGO_TAB_RIGHT)
        state->last_tab.width += nt_glyph_string_get_width (run->glyphs);
      else if (state->last_tab.align == NS_PANGO_TAB_CENTER)
        state->last_tab.width += nt_glyph_string_get_width (run->glyphs) / 2;
      else if (state->last_tab.align == NS_PANGO_TAB_DECIMAL)
        {
          int w;

          get_decimal_prefix_width (run->item, run->glyphs, line->pub.layout->text,
                                    state->last_tab.decimal, &w, &found_decimal);
          state->last_tab.width += w;
        }
      width = NT_MAX (state->last_tab.pos - state->last_tab.width, 0);
      state->last_tab.glyphs->glyphs[0].width = width;
      if (found_decimal || width == 0)
        state->last_tab.glyphs = NULL;
    }
}

static NtItem *
uninsert_run (NtLine *line)
{
  NtRun *run = line->runs[--line->n_runs];
  NtItem *item = run->item;

  line->pub.length -= item->length;
  nt_glyph_string_free (run->glyphs);
  free (run);
  return item;
}

static int
break_needs_hyphen (NsPangoLayout *layout, ParaBreakState *state, int pos)
{
  return layout->log_attrs[state->start_offset + pos].break_inserts_hyphen ||
         layout->log_attrs[state->start_offset + pos].break_removes_preceding;
}

static int
find_hyphen_width (const NtItem *item)
{
  hb_codepoint_t glyph;

  if (!item->font)
    return 0;
  if (hb_font_get_nominal_glyph (item->font->hb_font, 0x2010, &glyph) ||
      hb_font_get_nominal_glyph (item->font->hb_font, '-', &glyph))
    return hb_font_get_glyph_h_advance (item->font->hb_font, glyph);
  return 0;
}

static void
ensure_hyphen_width (ParaBreakState *state)
{
  if (state->hyphen_width < 0)
    state->hyphen_width = find_hyphen_width (state_head (state));
}

static int
find_break_extra_width (NsPangoLayout *layout, ParaBreakState *state, int pos)
{
  if (layout->log_attrs[state->start_offset + pos].break_inserts_hyphen)
    {
      ensure_hyphen_width (state);
      if (layout->log_attrs[state->start_offset + pos].break_removes_preceding && pos > 0)
        return state->hyphen_width - state->log_widths[state->log_widths_offset + pos - 1];
      return state->hyphen_width;
    }
  else if (pos > 0 && layout->log_attrs[state->start_offset + pos - 1].is_white)
    return - state->log_widths[state->log_widths_offset + pos - 1];
  return 0;
}

static void
compute_log_widths (NsPangoLayout *layout, ParaBreakState *state)
{
  NtItem *item = state_head (state);

  if (item->num_chars > state->num_log_widths)
    {
      state->log_widths = nt_renew (int, state->log_widths, item->num_chars);
      state->num_log_widths = item->num_chars;
    }
  nt_glyph_logical_widths (state->glyphs, layout->text, item, state->log_widths);
}

static int
tab_width_change (ParaBreakState *state)
{
  if (state->last_tab.glyphs)
    return state->last_tab.glyphs->glyphs[0].width - (state->last_tab.pos - state->last_tab.width);
  return 0;
}

static void
item_unsplit (NtItem *item, int split_index, int split_offset)
{
  item->offset -= split_index;
  item->length += split_index;
  item->num_chars += split_offset;
  item->char_offset -= split_offset;
}

static BreakResult
process_item (NsPangoLayout *layout, NtLine *line, ParaBreakState *state,
              int force_fit, int no_break_at_end, int is_last_item)
{
  NtItem *item = state_head (state);
  int width, extra_width, orig_extra_width, length, i, num_chars, orig_width;
  int processing_new_item;
  NsPangoWrapMode wrap;
  int break_num_chars, break_width, break_extra_width;
  NtGlyphString *break_glyphs;
  int safe_distance;

  if (!state->glyphs)
    {
      get_item_properties (item, &state->properties);
      state->glyphs = shape_run (line, state, item);
      state->log_widths_offset = 0;
      processing_new_item = TRUE;
    }
  else
    processing_new_item = FALSE;

  if (nt_utf8_get_char (layout->text + item->offset) == 0x2028 &&
      !should_ellipsize_current_line (layout, state))
    {
      insert_run (line, state, item, NULL, TRUE);
      state->log_widths_offset += item->num_chars;
      return BREAK_LINE_SEPARATOR;
    }

  if (state->remaining_width < 0 && !no_break_at_end)
    {
      insert_run (line, state, item, NULL, TRUE);
      return BREAK_ALL_FIT;
    }

  if (processing_new_item)
    width = nt_glyph_string_get_width (state->glyphs);
  else
    {
      width = 0;
      for (i = 0; i < item->num_chars; i++)
        width += state->log_widths[state->log_widths_offset + i];
    }

  if (layout->text[item->offset] == '\t')
    {
      insert_run (line, state, item, NULL, TRUE);
      state->remaining_width -= width;
      state->remaining_width = NT_MAX (state->remaining_width, 0);
      return BREAK_ALL_FIT;
    }

  wrap = layout->wrap;

  if (!no_break_at_end &&
      can_break_at (layout, state->start_offset + item->num_chars, wrap))
    {
      if (processing_new_item)
        {
          compute_log_widths (layout, state);
          processing_new_item = FALSE;
        }
      extra_width = !is_last_item ? find_break_extra_width (layout, state, item->num_chars) : 0;
    }
  else
    extra_width = 0;

  if ((width + extra_width <= state->remaining_width || (item->num_chars == 1 && line->n_runs == 0) ||
       (state->last_tab.glyphs && state->last_tab.align != NS_PANGO_TAB_LEFT)) &&
      !no_break_at_end)
    {
      NtGlyphString *glyphs = shape_run (line, state, item);

      width = nt_glyph_string_get_width (glyphs) + tab_width_change (state);
      if (width + extra_width <= state->remaining_width || (item->num_chars == 1 && line->n_runs == 0))
        {
          insert_run (line, state, item, glyphs, TRUE);
          state->remaining_width -= width;
          state->remaining_width = NT_MAX (state->remaining_width, 0);
          return BREAK_ALL_FIT;
        }
      nt_glyph_string_free (glyphs);
    }

  orig_width = width;
  orig_extra_width = extra_width;
  break_width = width;
  break_extra_width = extra_width;
  break_num_chars = item->num_chars;
  wrap = layout->wrap;
  break_glyphs = NULL;

  {
    NsPangoFontMetrics *metrics = nt_font_get_metrics (item->font);

    safe_distance = metrics->approximate_char_width * 3;
    ns_pango_font_metrics_unref (metrics);
  }

  if (processing_new_item)
    {
      compute_log_widths (layout, state);
      processing_new_item = FALSE;
    }

retry_break:
  for (num_chars = 0, width = 0;
       num_chars < (no_break_at_end ? item->num_chars : (item->num_chars + 1));
       num_chars++)
    {
      if (!is_last_item || num_chars != item->num_chars)
        extra_width = find_break_extra_width (layout, state, num_chars);
      else
        extra_width = 0;

      if (NT_MIN (width + extra_width, width) > state->remaining_width + safe_distance &&
          break_num_chars < item->num_chars)
        break;

      if (can_break_at (layout, state->start_offset + num_chars, wrap) &&
          (num_chars > 0 || line->n_runs > 0))
        {
          if (num_chars == 0 || width + extra_width < state->remaining_width - safe_distance)
            {
              break_num_chars = num_chars;
              break_width = width;
              break_extra_width = extra_width;
            }
          else
            {
              int new_break_width;
              NtItem *new_item;
              NtGlyphString *glyphs;

              length = (int) (nt_utf8_offset_to_pointer (layout->text + item->offset, num_chars) -
                              (layout->text + item->offset));

              if (num_chars < item->num_chars)
                {
                  new_item = nt_item_split (item, length, num_chars);
                  if (break_needs_hyphen (layout, state, num_chars))
                    new_item->flags |= NT_ITEM_NEED_HYPHEN;
                  else
                    new_item->flags &= ~(unsigned) NT_ITEM_NEED_HYPHEN;
                }
              else
                new_item = item;

              glyphs = shape_run (line, state, new_item);
              new_break_width = nt_glyph_string_get_width (glyphs) + tab_width_change (state);

              if (num_chars > 0 && (item != new_item || !is_last_item) &&
                  layout->log_attrs[state->start_offset + num_chars - 1].is_white)
                extra_width = - state->log_widths[state->log_widths_offset + num_chars - 1];
              else if (item == new_item && !is_last_item &&
                       break_needs_hyphen (layout, state, num_chars))
                extra_width = state->hyphen_width;
              else
                extra_width = 0;

              if (new_item != item)
                {
                  nt_item_free (new_item);
                  item_unsplit (item, length, num_chars);
                }

              if (break_num_chars == item->num_chars ||
                  new_break_width + extra_width <= state->remaining_width ||
                  new_break_width + extra_width < break_width + break_extra_width)
                {
                  break_num_chars = num_chars;
                  break_width = new_break_width;
                  break_extra_width = extra_width;
                  nt_glyph_string_free (break_glyphs);
                  break_glyphs = glyphs;
                }
              else
                nt_glyph_string_free (glyphs);
            }
        }

      if (num_chars < item->num_chars)
        width += state->log_widths[state->log_widths_offset + num_chars];
    }

  if (wrap == NS_PANGO_WRAP_WORD_CHAR && force_fit &&
      break_width + break_extra_width > state->remaining_width)
    {
      wrap = NS_PANGO_WRAP_CHAR;
      break_num_chars = item->num_chars;
      break_width = orig_width;
      break_extra_width = orig_extra_width;
      nt_glyph_string_free (break_glyphs);
      break_glyphs = NULL;
      goto retry_break;
    }

  if (force_fit || break_width + break_extra_width <= state->remaining_width)
    {
      if (state->remaining_width >= 0)
        {
          state->remaining_width -= break_width + break_extra_width;
          state->remaining_width = NT_MAX (state->remaining_width, 0);
        }

      if (break_num_chars == item->num_chars)
        {
          if (can_break_at (layout, state->start_offset + break_num_chars, wrap) &&
              break_needs_hyphen (layout, state, break_num_chars))
            item->flags |= NT_ITEM_NEED_HYPHEN;
          insert_run (line, state, item, NULL, TRUE);
          nt_glyph_string_free (break_glyphs);
          return BREAK_ALL_FIT;
        }
      else if (break_num_chars == 0)
        {
          nt_glyph_string_free (break_glyphs);
          return BREAK_EMPTY_FIT;
        }
      else
        {
          NtItem *new_item;

          length = (int) (nt_utf8_offset_to_pointer (layout->text + item->offset, break_num_chars) -
                          (layout->text + item->offset));
          new_item = nt_item_split (item, length, break_num_chars);
          insert_run (line, state, new_item, break_glyphs, FALSE);
          state->log_widths_offset += break_num_chars;
          return BREAK_SOME_FIT;
        }
    }

  nt_glyph_string_free (state->glyphs);
  state->glyphs = NULL;
  nt_glyph_string_free (break_glyphs);
  return BREAK_NONE_FIT;
}

static void
line_set_resolved_dir (NtLine *line, NsPangoDirection direction)
{
  switch (direction)
    {
    case NS_PANGO_DIRECTION_RTL:
    case NS_PANGO_DIRECTION_WEAK_RTL:
    case NS_PANGO_DIRECTION_TTB_LTR:
      line->pub.resolved_dir = NS_PANGO_DIRECTION_RTL;
      break;
    default:
      line->pub.resolved_dir = NS_PANGO_DIRECTION_LTR;
      break;
    }
}

/* ---- line post-processing ---------------------------------------------- */

static int
line_get_width (NtLine *line)
{
  int i, w = 0;

  for (i = 0; i < line->n_runs; i++)
    w += nt_glyph_string_get_width (line->runs[i]->glyphs);
  return w;
}

static void
add_missing_hyphen (NtLine *line, ParaBreakState *state, NtRun *run)
{
  NsPangoLayout *layout = line->pub.layout;
  NtItem *item = run->item;
  int line_chars = 0, i;

  for (i = 0; i < line->n_runs; i++)
    line_chars += line->runs[i]->item->num_chars;

  if (layout->log_attrs[state->line_start_offset + line_chars].break_inserts_hyphen &&
      !(item->flags & NT_ITEM_NEED_HYPHEN))
    {
      int width = nt_glyph_string_get_width (run->glyphs);
      int start_offset = state->start_offset;

      state->start_offset = state->line_start_offset + line_chars - item->num_chars;
      nt_glyph_string_free (run->glyphs);
      item->flags |= NT_ITEM_NEED_HYPHEN;
      run->glyphs = shape_run (line, state, item);
      state->start_offset = start_offset;
      state->remaining_width += nt_glyph_string_get_width (run->glyphs) - width;
    }
}

static void
zero_line_final_space (NtLine *line, ParaBreakState *state, NtRun *run)
{
  NsPangoLayout *layout = line->pub.layout;
  NtGlyphString *glyphs = run->glyphs;
  int glyph;

  if (glyphs->num_glyphs < 1 || state->start_offset == 0 ||
      !layout->log_attrs[state->start_offset - 1].is_white)
    return;
  glyph = run->item->level % 2 ? 0 : glyphs->num_glyphs - 1;
  if (glyphs->num_glyphs >= 2 &&
      glyphs->log_clusters[glyph] ==
        glyphs->log_clusters[glyph + (run->item->level % 2 ? 1 : -1)])
    return;
  glyphs->glyphs[glyph].width = 0;
  glyphs->glyphs[glyph].glyph = NT_GLYPH_EMPTY;
}

static void
apply_baseline_shift (NtLine *line)
{
  int i;

  for (i = 0; i < line->n_runs; i++)
    {
      NtRun *run = line->runs[i];
      NtAttr *rise = nt_item_find_attr (run->item, NT_ATTR_RISE);

      run->y_offset = rise ? rise->v.i : 0;
      run->start_x_offset = 0;
      run->end_x_offset = 0;
    }
}

static void
reorder_runs (NtRun **runs, int n)
{
  int min_level = 255, i, start;
  NtRun **out;
  int n_out = 0;

  if (n <= 1)
    return;
  for (i = 0; i < n; i++)
    min_level = NT_MIN (min_level, runs[i]->item->level);
  out = nt_new (NtRun *, n);

  /* Runs above the lowest level are reordered recursively; the lowest
   * level's are visited in order, backwards when it is odd. */
  start = 0;
  if (min_level % 2)
    {
      int end = n;

      for (i = n - 1; i >= -1; i--)
        if (i < 0 || runs[i]->item->level == min_level)
          {
            if (end - (i + 1) > 0)
              {
                reorder_runs (runs + i + 1, end - (i + 1));
                memcpy (out + n_out, runs + i + 1, (size_t) (end - (i + 1)) * sizeof *runs);
                n_out += end - (i + 1);
              }
            if (i >= 0)
              out[n_out++] = runs[i];
            end = i;
          }
    }
  else
    {
      for (i = 0; i <= n; i++)
        if (i == n || runs[i]->item->level == min_level)
          {
            if (i - start > 0)
              {
                reorder_runs (runs + start, i - start);
                memcpy (out + n_out, runs + start, (size_t) (i - start) * sizeof *runs);
                n_out += i - start;
              }
            if (i < n)
              out[n_out++] = runs[i];
            start = i + 1;
          }
    }
  memcpy (runs, out, (size_t) n * sizeof *runs);
  free (out);
}

static void
line_reorder (NtLine *line)
{
  int level_or = 0, level_and = 1, i;

  for (i = 0; i < line->n_runs; i++)
    {
      level_or |= line->runs[i]->item->level;
      level_and &= line->runs[i]->item->level;
    }
  if ((level_or & 1) && !(level_and & 1))
    reorder_runs (line->runs, line->n_runs);
  else if (level_and & 1)
    for (i = 0; i < line->n_runs / 2; i++)
      {
        NtRun *t = line->runs[i];

        line->runs[i] = line->runs[line->n_runs - 1 - i];
        line->runs[line->n_runs - 1 - i] = t;
      }
}

/* Where a run's characters start in the layout's log attrs, as Pango
 * reads the item's character offset. The ellipsis item keeps the one its
 * own itemization gave it, 0 (ellipsize.c fixes up the text it covers but
 * not that), so its cluster is measured against the layout's first
 * characters: this keeps carets and justification where the fork puts
 * them. */
static int
run_log_attrs_offset (const NtRun *run)
{
  return (run->item->flags & NT_ITEM_IS_ELLIPSIS) ? 0 : run->item->char_offset;
}

/* Pango's distribute_letter_spacing: half before, the rest after, whole
 * pixels when the space is. */
static void
distribute_letter_spacing (int letter_spacing, int *space_left, int *space_right)
{
  *space_left = letter_spacing / 2;
  if ((letter_spacing & (NS_PANGO_SCALE - 1)) == 0)
    *space_left = NS_PANGO_UNITS_ROUND (*space_left);
  *space_right = letter_spacing - *space_left;
}

/* Extra space between clusters, for lines without spaces to widen
 * (Pango's justify_clusters): each grapheme cluster with a width gets an
 * equal share, carrying the rounding residual on, split before and after
 * it; none before the line's first cluster or after its last. */
static void
justify_clusters (NtLine *line, ParaBreakState *state)
{
  const char *text = line->pub.layout->text;
  const NsPangoLogAttr *log_attrs = line->pub.layout->log_attrs;
  int total_remaining_width, total_gaps = 0;
  int added_so_far = 0, gaps_so_far, is_hinted, mode, r;

  total_remaining_width = state->remaining_width;
  if (total_remaining_width <= 0)
    return;

  /* hint to full pixel if total remaining width was so */
  is_hinted = (total_remaining_width & (NS_PANGO_SCALE - 1)) == 0;

  for (mode = 0; mode < 2; mode++) /* measure, then adjust */
    {
      int leftedge = TRUE;
      NtGlyphString *rightmost_glyphs = NULL;
      int rightmost_space = 0;
      int residual = 0;

      added_so_far = 0;
      gaps_so_far = 0;

      for (r = 0; r < line->n_runs; r++)
        {
          NtRun *run = line->runs[r];
          NtGlyphString *glyphs = run->glyphs;
          NtClusterIter cluster_iter;
          int have_cluster, dir, offset;

          dir = run->item->level % 2 == 0 ? +1 : -1;
          offset = run_log_attrs_offset (run);

          for (have_cluster = dir > 0 ? nt_cluster_iter_init_start (&cluster_iter, run, text)
                                      : nt_cluster_iter_init_end (&cluster_iter, run, text);
               have_cluster;
               have_cluster = dir > 0 ? nt_cluster_iter_next (&cluster_iter)
                                      : nt_cluster_iter_prev (&cluster_iter))
            {
              int i, width = 0;

              /* don't expand in the middle of graphemes */
              if (!log_attrs[offset + cluster_iter.start_char].is_cursor_position)
                continue;

              for (i = cluster_iter.start_glyph; i != cluster_iter.end_glyph; i += dir)
                width += glyphs->glyphs[i].width;

              /* also don't expand zero-width clusters. */
              if (width == 0)
                continue;

              gaps_so_far++;

              if (mode == 1)
                {
                  int leftmost, rightmost;
                  int adjustment, space_left, space_right;

                  adjustment = total_remaining_width / total_gaps + residual;
                  if (is_hinted)
                    {
                      int old_adjustment = adjustment;

                      adjustment = NS_PANGO_UNITS_ROUND (adjustment);
                      residual = old_adjustment - adjustment;
                    }
                  /* distribute to before/after */
                  distribute_letter_spacing (adjustment, &space_left, &space_right);

                  if (cluster_iter.start_glyph < cluster_iter.end_glyph)
                    {
                      /* LTR */
                      leftmost = cluster_iter.start_glyph;
                      rightmost = cluster_iter.end_glyph - 1;
                    }
                  else
                    {
                      /* RTL */
                      leftmost = cluster_iter.end_glyph + 1;
                      rightmost = cluster_iter.start_glyph;
                    }
                  /* Don't add to left-side of left-most glyph of left-most non-zero run. */
                  if (leftedge)
                    leftedge = FALSE;
                  else
                    {
                      glyphs->glyphs[leftmost].width += space_left;
                      glyphs->glyphs[leftmost].x_offset += space_left;
                      added_so_far += space_left;
                    }
                  /* Don't add to right-side of right-most glyph of right-most
                   * non-zero run: saved so we can undo it later. */
                  rightmost_glyphs = glyphs;
                  rightmost_space = space_right;

                  glyphs->glyphs[rightmost].width += space_right;
                  added_so_far += space_right;
                }
            }
        }

      if (mode == 0)
        {
          total_gaps = gaps_so_far - 1;

          if (total_gaps == 0)
            return; /* a single cluster, can't really justify it */
        }
      else if (rightmost_glyphs)
        {
          /* As Pango: off the run's last glyph. */
          rightmost_glyphs->glyphs[rightmost_glyphs->num_glyphs - 1].width -= rightmost_space;
          added_so_far -= rightmost_space;
        }
    }

  state->remaining_width -= added_so_far;
}

static void
justify_words (NtLine *line, ParaBreakState *state)
{
  const char *text = line->pub.layout->text;
  const NsPangoLogAttr *log_attrs = line->pub.layout->log_attrs;
  int total_remaining_width = state->remaining_width, total_space_width = 0;
  int added_so_far = 0, spaces_so_far, is_hinted, mode, r;

  if (total_remaining_width <= 0)
    return;
  is_hinted = (total_remaining_width & (NS_PANGO_SCALE - 1)) == 0;

  for (mode = 0; mode < 2; mode++)
    {
      added_so_far = 0;
      spaces_so_far = 0;
      for (r = 0; r < line->n_runs; r++)
        {
          NtRun *run = line->runs[r];
          NtGlyphString *glyphs = run->glyphs;
          int offset = run->item->char_offset;
          const NtItem *item = run->item;
          int dir = (item->level & 1) ? -1 : 1;
          int gi = dir > 0 ? 0 : glyphs->num_glyphs - 1;
          int char_pos = 0;
          const char *p = text + item->offset;

          /* Walk clusters in logical order. */
          while (gi >= 0 && gi < glyphs->num_glyphs)
            {
              int cluster = glyphs->log_clusters[gi], end_gi = gi;
              int next_cluster_bytes, k;

              while (end_gi >= 0 && end_gi < glyphs->num_glyphs &&
                     glyphs->log_clusters[end_gi] == cluster)
                end_gi += dir;
              next_cluster_bytes = (end_gi >= 0 && end_gi < glyphs->num_glyphs)
                                     ? glyphs->log_clusters[end_gi] : item->length;
              p = text + item->offset + cluster;
              char_pos = nt_utf8_pointer_to_offset (text + item->offset, p);

              if (log_attrs[offset + char_pos].is_expandable_space)
                for (k = gi; k != end_gi; k += dir)
                  {
                    int glyph_width = glyphs->glyphs[k].width;

                    if (glyph_width == 0)
                      continue;
                    spaces_so_far += glyph_width;
                    if (mode == 1)
                      {
                        int adjustment = (int) (((int64_t) spaces_so_far * total_remaining_width) /
                                                total_space_width) - added_so_far;

                        if (is_hinted)
                          adjustment = NS_PANGO_UNITS_ROUND (adjustment);
                        glyphs->glyphs[k].width += adjustment;
                        added_so_far += adjustment;
                      }
                  }
              (void) next_cluster_bytes;
              gi = end_gi;
            }
        }
      if (mode == 0)
        {
          total_space_width = spaces_so_far;
          if (total_space_width == 0)
            {
              justify_clusters (line, state);
              return;
            }
        }
    }
  state->remaining_width -= added_so_far;
}

static void
line_postprocess (NtLine *line, ParaBreakState *state, int wrapped)
{
  NsPangoLayout *layout = line->pub.layout;
  int ellipsized = FALSE;

  add_missing_hyphen (line, state, line->runs[line->n_runs - 1]);
  if (wrapped)
    zero_line_final_space (line, state, line->runs[line->n_runs - 1]);

  apply_baseline_shift (line);

  if (state->line_width >= 0 && should_ellipsize_current_line (layout, state))
    ellipsized = nt_line_ellipsize (line, state->attrs, state->line_width);

  line_reorder (line);

  if (layout->justify && (wrapped || ellipsized))
    {
      if (state->remaining_width < 0)
        state->remaining_width = state->line_width - line_get_width (line);
      justify_words (line, state);
    }

  layout->is_wrapped |= wrapped;
  layout->is_ellipsized |= ellipsized;
}

static void
add_line (NsPangoLayout *layout, NtLine *line, ParaBreakState *state)
{
  layout->lines = nt_renew (NtLine *, layout->lines, layout->n_lines + 1);
  layout->lines[layout->n_lines++] = line;
  if (layout->height >= 0)
    {
      NsPangoRectangle logical;

      ns_pango_layout_line_get_extents (&line->pub, NULL, &logical);
      state->remaining_height -= logical.height;
      state->remaining_height -= layout->spacing;
      state->line_height = logical.height;
    }
}

static void
process_line (NsPangoLayout *layout, ParaBreakState *state)
{
  NtLine *line = line_new (layout);
  int have_break = FALSE, break_remaining_width = 0, break_start_offset = 0;
  int break_n_runs = 0, wrapped = FALSE;

  line->pub.start_index = state->line_start_index;
  line->pub.is_paragraph_start = state->line_of_par == 1;
  line_set_resolved_dir (line, state->base_dir);

  state->line_width = layout->width;
  if (state->line_width >= 0 && layout->alignment != NS_PANGO_ALIGN_CENTER)
    {
      if (line->pub.is_paragraph_start && layout->indent >= 0)
        state->line_width -= layout->indent;
      else if (!line->pub.is_paragraph_start && layout->indent < 0)
        state->line_width += layout->indent;
      if (state->line_width < 0)
        state->line_width = 0;
    }

  if (should_ellipsize_current_line (layout, state))
    state->remaining_width = -1;
  else
    state->remaining_width = state->line_width;

  state->last_tab.glyphs = NULL;
  state->last_tab.index = 0;
  state->last_tab.align = NS_PANGO_TAB_LEFT;

  while (state_first (state))
    {
      NtItem *item = state_head (state);
      BreakResult result;
      int old_num_chars = item->num_chars;
      int old_remaining_width = state->remaining_width;
      int first_item_in_line = line->n_runs == 0;
      int last_item_in_line = !state_has_next (state);

      result = process_item (layout, line, state, !have_break, FALSE, last_item_in_line);

      switch (result)
        {
        case BREAK_ALL_FIT:
          if (layout->text[item->offset] != '\t' &&
              can_break_in (layout, state->start_offset, old_num_chars, !first_item_in_line))
            {
              have_break = TRUE;
              break_remaining_width = old_remaining_width;
              break_start_offset = state->start_offset;
              break_n_runs = line->n_runs - 1;
            }
          state->head++;
          state->start_offset += old_num_chars;
          break;

        case BREAK_EMPTY_FIT:
          wrapped = TRUE;
          goto done;

        case BREAK_SOME_FIT:
          state->start_offset += old_num_chars - item->num_chars;
          wrapped = TRUE;
          goto done;

        case BREAK_NONE_FIT:
          while (line->n_runs > break_n_runs)
            {
              NtRun *run = line->runs[line->n_runs - 1];

              if (run->glyphs == state->last_tab.glyphs)
                {
                  state->last_tab.glyphs = NULL;
                  state->last_tab.index = 0;
                  state->last_tab.align = NS_PANGO_TAB_LEFT;
                }
              state_push_front (state, uninsert_run (line));
            }
          state->start_offset = break_start_offset;
          state->remaining_width = break_remaining_width;
          last_item_in_line = !state_has_next (state);

          item = state_head (state);
          old_num_chars = item->num_chars;
          result = process_item (layout, line, state, TRUE, TRUE, last_item_in_line);
          switch (result)
            {
            case BREAK_ALL_FIT:
              state->head++;
              state->start_offset += old_num_chars;
              break;
            case BREAK_EMPTY_FIT:
              wrapped = TRUE;
              break;
            case BREAK_SOME_FIT:
              state->start_offset += old_num_chars - item->num_chars;
              wrapped = TRUE;
              break;
            default:
              break;
            }
          goto done;

        case BREAK_LINE_SEPARATOR:
          state->head++;
          state->start_offset += old_num_chars;
          wrapped = TRUE;
          goto done;

        default:
          break;
        }
    }

done:
  if (line->n_runs > 0)
    line_postprocess (line, state, wrapped);
  add_line (layout, line, state);
  state->line_of_par++;
  state->line_start_index += line->pub.length;
  state->line_start_offset = state->start_offset;
}

/* ---- splitting runs at the drawing attributes -------------------------- */

static NtRun *
run_split (NsPangoLayout *layout, NtRun *orig, int split_index)
{
  NtRun *first = nt_new0 (NtRun, 1);
  NtGlyphString *gs = orig->glyphs;
  int split_chars = nt_utf8_strlen (layout->text + orig->item->offset, split_index);
  int i, n1 = 0, n2 = 0;
  NtGlyphString *a = nt_glyph_string_new (), *b = nt_glyph_string_new ();

  nt_glyph_string_set_size (a, gs->num_glyphs);
  nt_glyph_string_set_size (b, gs->num_glyphs);
  for (i = 0; i < gs->num_glyphs; i++)
    if (gs->log_clusters[i] < split_index)
      {
        a->glyphs[n1] = gs->glyphs[i];
        a->log_clusters[n1++] = gs->log_clusters[i];
      }
    else
      {
        b->glyphs[n2] = gs->glyphs[i];
        b->log_clusters[n2++] = gs->log_clusters[i] - split_index;
      }
  a->num_glyphs = n1;
  b->num_glyphs = n2;
  nt_glyph_string_free (gs);
  orig->glyphs = b;

  first->item = nt_item_split (orig->item, split_index, split_chars);
  first->glyphs = a;
  first->y_offset = orig->y_offset;
  first->start_x_offset = orig->start_x_offset;
  first->end_x_offset = 0;
  orig->start_x_offset = 0;
  return first;
}

/* The drawing attributes covering [start, end) into the item. */
static void
attach_drawing_attrs (NtItem *item, const NsPangoAttrList *draw)
{
  int i;
  unsigned s = (unsigned) item->offset, e = (unsigned) (item->offset + item->length);

  for (i = 0; i < draw->n; i++)
    {
      const NtAttr *a = draw->attrs[i];

      if (a->base.start_index <= s && a->base.end_index >= e && s < e)
        {
          item->extra_attrs = nt_renew (NtAttr *, item->extra_attrs, item->n_extra + 1);
          item->extra_attrs[item->n_extra++] = nt_attr_copy (a);
        }
    }
}

static void
apply_drawing_attrs (NsPangoLayout *layout, const NsPangoAttrList *draw)
{
  int l;

  if (!draw)
    return;
  for (l = 0; l < layout->n_lines; l++)
    {
      NtLine *line = layout->lines[l];
      NtRun **out = NULL;
      int n_out = 0, cap = 0, r;

      for (r = 0; r < line->n_runs; r++)
        {
          NtRun *run = line->runs[r];
          int rtl = run->item->level & 1;
          NtRun *pieces[64];
          int n_pieces = 0, i, k;

          /* Cut at every attribute boundary inside the run (not the
           * ellipsis, which stands for text it does not show). */
          if (!(run->item->flags & NT_ITEM_IS_ELLIPSIS))
            for (;;)
              {
                unsigned s = (unsigned) run->item->offset;
                unsigned e = (unsigned) (run->item->offset + run->item->length);
                unsigned cut = e;
                const char *p;

                for (i = 0; i < draw->n; i++)
                  {
                    unsigned as = draw->attrs[i]->base.start_index;
                    unsigned ae = draw->attrs[i]->base.end_index;

                    if (as > s && as < cut)
                      cut = as;
                    if (ae > s && ae < cut)
                      cut = ae;
                  }
                if (cut >= e || n_pieces == 63)
                  break;
                /* At a character boundary. */
                p = layout->text + cut;
                while (p > layout->text + s && (*(const unsigned char *) p & 0xC0) == 0x80)
                  p--;
                if (p == layout->text + s)
                  break;
                pieces[n_pieces++] = run_split (layout, run, (int) (p - (layout->text + s)));
              }
          pieces[n_pieces++] = run;

          for (i = 0; i < n_pieces; i++)
            attach_drawing_attrs (pieces[i]->item, draw);

          if (n_out + n_pieces > cap)
            {
              cap = (n_out + n_pieces) * 2;
              out = nt_renew (NtRun *, out, cap);
            }
          /* Visual order: an RTL run's logical pieces right to left. */
          for (k = 0; k < n_pieces; k++)
            out[n_out++] = pieces[rtl ? n_pieces - 1 - k : k];
        }
      free (line->runs);
      line->runs = out;
      line->n_runs = n_out;
      line->cap_runs = cap;
      line->cached = FALSE;
    }
}

/* ---- checking lines ---------------------------------------------------- */

void
nt_layout_check_lines (NsPangoLayout *layout)
{
  NsPangoAttrList *attrs, *itemize_attrs, *shape_attrs, *draw_attrs;
  NsPangoDirection prev_base_dir = NS_PANGO_DIRECTION_NEUTRAL, base_dir = NS_PANGO_DIRECTION_NEUTRAL;
  const char *start;
  int start_offset, need_log_attrs, done = FALSE;
  ParaBreakState state;

  check_context_changed (layout);
  if (layout->have_lines)
    return;
  layout->have_lines = TRUE;

  attrs = effective_attributes (layout);
  itemize_attrs = filter_attrs (attrs, affects_itemization);
  shape_attrs = filter_attrs (attrs, affects_break_or_shape);
  draw_attrs = filter_attrs (attrs, affects_drawing);

  need_log_attrs = layout->log_attrs == NULL;
  if (need_log_attrs)
    layout->log_attrs = nt_new0 (NsPangoLogAttr, layout->n_chars + 1);

  start_offset = 0;
  start = layout->text;

  if (layout->auto_dir)
    {
      prev_base_dir = nt_find_base_dir (layout->text, layout->length);
      if (prev_base_dir == NS_PANGO_DIRECTION_NEUTRAL)
        prev_base_dir = layout->context->base_dir;
    }
  else
    base_dir = layout->context->base_dir;

  memset (&state, 0, sizeof state);
  state.remaining_height = layout->height;
  state.line_height = -1;
  state.attrs = itemize_attrs;
  if (layout->height >= 0)
    {
      NsPangoRectangle logical;
      int height = 0;

      empty_extents_at_index (layout, 0, &logical, &height);
      state.line_height = layout->line_spacing == 0.0f ? logical.height
                                                       : (int) (layout->line_spacing * height);
    }

  do
    {
      int delimiter_index, next_para_index, delim_len, i;
      const char *end;
      NtItemList items = { 0 };

      nt_find_paragraph_boundary (start, (int) ((layout->text + layout->length) - start),
                                  &delimiter_index, &next_para_index);
      if (layout->auto_dir)
        {
          base_dir = nt_find_base_dir (start, delimiter_index);
          if (base_dir == NS_PANGO_DIRECTION_NEUTRAL)
            base_dir = prev_base_dir;
          else
            prev_base_dir = base_dir;
        }
      end = start + delimiter_index;
      delim_len = next_para_index - delimiter_index;
      if (end == layout->text + layout->length)
        done = TRUE;

      nt_itemize (layout->context, base_dir, layout->text, (int) (start - layout->text),
                  (int) (end - start), itemize_attrs, NULL, &items);
      nt_apply_attrs_to_items (&items, shape_attrs);

      if (need_log_attrs)
        {
          int para_len = delimiter_index + delim_len;
          int offset = 0;

          nt_default_break (start, para_len, layout->log_attrs + start_offset,
                            layout->n_chars + 1 - start_offset);
          for (i = 0; i < items.n; i++)
            {
              NtItem *item = items.items[i];

              nt_tailor_break (layout->text + item->offset, item->length, item->script,
                               item->language, layout->log_attrs + start_offset + offset,
                               item->num_chars + 1);
              offset += item->num_chars;
            }
          if (shape_attrs && items.n)
            nt_attr_break (start, para_len, shape_attrs, items.items[0]->offset,
                           layout->log_attrs + start_offset, layout->n_chars + 1 - start_offset);
        }
      nt_itemize_post_process (layout->context, layout->text, layout->log_attrs, &items);

      state.items = items.items;
      state.head = 0;
      state.n = items.n;
      state.cap = items.cap;
      state.base_dir = base_dir;
      state.line_of_par = 1;
      state.start_offset = start_offset;
      state.line_start_offset = start_offset;
      state.line_start_index = (int) (start - layout->text);
      state.glyphs = NULL;
      state.line_width = -1;
      state.remaining_width = -1;
      state.log_widths_offset = 0;
      state.hyphen_width = -1;

      if (state.n > 0)
        {
          while (state_first (&state))
            process_line (layout, &state);
        }
      else
        {
          NtLine *empty = line_new (layout);

          empty->pub.start_index = state.line_start_index;
          empty->pub.is_paragraph_start = TRUE;
          line_set_resolved_dir (empty, base_dir);
          add_line (layout, empty, &state);
        }
      /* Items given to runs are theirs; the array is ours. */
      free (state.items);
      state.items = NULL;
      nt_glyph_string_free (state.glyphs);
      state.glyphs = NULL;

      if (layout->height >= 0 && state.remaining_height < state.line_height)
        done = TRUE;
      if (!done)
        start_offset += nt_utf8_strlen (start, (int) (end - start) + delim_len);
      start = end + delim_len;
    }
  while (!done);

  free (state.log_widths);
  apply_drawing_attrs (layout, draw_attrs);

  ns_pango_attr_list_unref (draw_attrs);
  ns_pango_attr_list_unref (shape_attrs);
  ns_pango_attr_list_unref (itemize_attrs);
  ns_pango_attr_list_unref (attrs);
}

/* ---- extents ----------------------------------------------------------- */

void
nt_run_get_extents (const NtRun *run, NsPangoRectangle *run_ink, NsPangoRectangle *run_logical)
{
  ItemProperties p;
  NsPangoRectangle logical;
  int has_underline, has_overline;

  get_item_properties (run->item, &p);
  has_underline = p.uline_single || p.uline_double || p.uline_low || p.uline_error;
  has_overline = p.oline_single;

  if (!run_logical && (has_underline || has_overline || p.strikethrough))
    run_logical = &logical;
  if (!run_logical)
    run_logical = &logical;

  if (p.shape && !(run->item->flags & NT_ITEM_IS_ELLIPSIS))
    shape_get_extents (run->item->num_chars, &p.shape->v.shape.ink,
                       &p.shape->v.shape.logical, run_ink, run_logical);
  else
    nt_glyph_string_extents (run->glyphs, run->item->font, run_ink, run_logical);

  if (run_ink && (has_underline || has_overline || p.strikethrough))
    {
      NsPangoFontMetrics *m = nt_font_get_metrics (run->item->font);
      int ut = m->underline_thickness, up = m->underline_position;
      int st = m->strikethrough_thickness, sp = m->strikethrough_position;
      int new_pos = NT_MIN (run_ink->x, run_logical->x);

      run_ink->width = NT_MAX (run_ink->x + run_ink->width,
                               run_logical->x + run_logical->width) - new_pos;
      run_ink->x = new_pos;
      if (p.strikethrough && run_ink->height == 0)
        {
          run_ink->height = st;
          run_ink->y = -sp;
        }
      if (p.oline_single)
        {
          run_ink->y -= ut;
          run_ink->height += ut;
        }
      if (p.uline_low)
        run_ink->height += 2 * ut;
      if (p.uline_single)
        run_ink->height = NT_MAX (run_ink->height, ut - up - run_ink->y);
      if (p.uline_double)
        run_ink->height = NT_MAX (run_ink->height, 3 * ut - up - run_ink->y);
      if (p.uline_error)
        run_ink->height = NT_MAX (run_ink->height, 3 * ut - up - run_ink->y);
      ns_pango_font_metrics_unref (m);
    }

  if (run_ink)
    run_ink->y -= run->y_offset;
  run_logical->y -= run->y_offset;
}

static int
run_height (const NtRun *run)
{
  NsPangoFontMetrics *m = nt_font_get_metrics (run->item->size_font ? run->item->size_font
                                                                    : run->item->font);
  int h = m->height;

  ns_pango_font_metrics_unref (m);
  return h;
}

static void
line_get_extents_and_height (NtLine *line, NsPangoRectangle *ink_rect,
                             NsPangoRectangle *logical_rect, int *height)
{
  int x_pos = 0, i;

  if (!line->cached)
    {
      NsPangoRectangle ink = { 0 }, logical = { 0 };
      int h = 0;

      for (i = 0; i < line->n_runs; i++)
        {
          NtRun *run = line->runs[i];
          NsPangoRectangle run_ink, run_logical;
          int new_pos;

          nt_run_get_extents (run, &run_ink, &run_logical);

          if (ink.width == 0 || ink.height == 0)
            {
              ink = run_ink;
              ink.x += x_pos;
            }
          else if (run_ink.width != 0 && run_ink.height != 0)
            {
              new_pos = NT_MIN (ink.x, x_pos + run_ink.x);
              ink.width = NT_MAX (ink.x + ink.width, x_pos + run_ink.x + run_ink.width) - new_pos;
              ink.x = new_pos;
              new_pos = NT_MIN (ink.y, run_ink.y);
              ink.height = NT_MAX (ink.y + ink.height, run_ink.y + run_ink.height) - new_pos;
              ink.y = new_pos;
            }

          new_pos = NT_MIN (logical.x, x_pos + run_logical.x);
          logical.width = NT_MAX (logical.x + logical.width,
                                  x_pos + run_logical.x + run_logical.width) - new_pos;
          logical.x = new_pos;
          if (i == 0)
            {
              logical.y = run_logical.y;
              logical.height = run_logical.height;
            }
          else
            {
              new_pos = NT_MIN (logical.y, run_logical.y);
              logical.height = NT_MAX (logical.y + logical.height,
                                       run_logical.y + run_logical.height) - new_pos;
              logical.y = new_pos;
            }
          h = NT_MAX (h, abs (run_height (run)));
          x_pos += run_logical.width;
        }

      if (line->n_runs == 0 && line->pub.layout)
        empty_extents_at_index (line->pub.layout, line->pub.start_index, &logical, &h);

      line->ink = ink;
      line->logical = logical;
      line->height = h;
      line->cached = TRUE;
    }
  if (ink_rect)
    *ink_rect = line->ink;
  if (logical_rect)
    *logical_rect = line->logical;
  if (height)
    *height = line->height;
}

void
ns_pango_layout_line_get_extents (NsPangoLayoutLine *pline, NsPangoRectangle *ink_rect,
                                  NsPangoRectangle *logical_rect)
{
  line_get_extents_and_height ((NtLine *) pline, ink_rect, logical_rect, NULL);
}

static NsPangoAlignment
get_alignment (NsPangoLayout *layout, NtLine *line)
{
  NsPangoAlignment alignment = layout->alignment;
  NsPangoDirection base = layout->context->base_dir;
  int line_dir = line->pub.resolved_dir == NS_PANGO_DIRECTION_RTL ? -1 : 1;
  int base_dir = 0;

  switch (base)
    {
    case NS_PANGO_DIRECTION_LTR: case NS_PANGO_DIRECTION_WEAK_LTR: case NS_PANGO_DIRECTION_TTB_RTL:
      base_dir = 1;
      break;
    case NS_PANGO_DIRECTION_RTL: case NS_PANGO_DIRECTION_WEAK_RTL: case NS_PANGO_DIRECTION_TTB_LTR:
      base_dir = -1;
      break;
    default:
      base_dir = 0;
      break;
    }
  if (alignment != NS_PANGO_ALIGN_CENTER && layout->auto_dir && line_dir == -base_dir)
    {
      if (alignment == NS_PANGO_ALIGN_LEFT)
        alignment = NS_PANGO_ALIGN_RIGHT;
      else if (alignment == NS_PANGO_ALIGN_RIGHT)
        alignment = NS_PANGO_ALIGN_LEFT;
    }
  return alignment;
}

static void
get_x_offset (NsPangoLayout *layout, NtLine *line, int layout_width, int line_width,
              int *x_offset)
{
  NsPangoAlignment alignment = get_alignment (layout, line);

  if (layout_width == 0)
    *x_offset = 0;
  else if (alignment == NS_PANGO_ALIGN_RIGHT)
    *x_offset = layout_width - line_width;
  else if (alignment == NS_PANGO_ALIGN_CENTER)
    {
      *x_offset = (layout_width - line_width) / 2;
      if (((layout_width | line_width) & (NS_PANGO_SCALE - 1)) == 0)
        *x_offset = NS_PANGO_UNITS_ROUND (*x_offset);
    }
  else
    *x_offset = 0;

  if (alignment == NS_PANGO_ALIGN_CENTER)
    return;
  if (line->pub.is_paragraph_start)
    {
      if (layout->indent > 0)
        *x_offset += alignment == NS_PANGO_ALIGN_LEFT ? layout->indent : -layout->indent;
    }
  else if (layout->indent < 0)
    *x_offset += alignment == NS_PANGO_ALIGN_LEFT ? -layout->indent : layout->indent;
}

static void
line_extents_layout_coords (NsPangoLayout *layout, NtLine *line, int layout_width, int y_offset,
                            int *baseline, NsPangoRectangle *ink_layout,
                            NsPangoRectangle *logical_layout, int first_line)
{
  NsPangoRectangle line_ink, line_logical;
  int x_offset, new_baseline, height;

  line_get_extents_and_height (line, &line_ink, &line_logical, &height);
  get_x_offset (layout, line, layout_width, line_logical.width, &x_offset);

  if (first_line || !baseline || layout->line_spacing == 0.0f)
    new_baseline = y_offset - line_logical.y;
  else
    new_baseline = *baseline + (int) (layout->line_spacing * height);

  if (ink_layout)
    {
      *ink_layout = line_ink;
      ink_layout->x = line_ink.x + x_offset;
      ink_layout->y = new_baseline + line_ink.y;
    }
  if (logical_layout)
    {
      *logical_layout = line_logical;
      logical_layout->x = line_logical.x + x_offset;
      logical_layout->y = new_baseline + line_logical.y;
    }
  if (baseline)
    *baseline = new_baseline;
}

static void
get_extents_internal (NsPangoLayout *layout, NsPangoRectangle *ink_rect,
                      NsPangoRectangle *logical_rect, NtExtents **line_extents)
{
  int y_offset = 0, width, need_width = FALSE, i, baseline = 0;

  nt_layout_check_lines (layout);

  if (ink_rect && layout->ink_cached)
    {
      *ink_rect = layout->ink_rect;
      ink_rect = NULL;
    }
  if (logical_rect && layout->logical_cached)
    {
      *logical_rect = layout->logical_rect;
      logical_rect = NULL;
    }
  if (!ink_rect && !logical_rect && !line_extents)
    return;

  width = layout->width;
  if (layout->auto_dir)
    {
      for (i = 0; i < layout->n_lines && !need_width; i++)
        if (get_alignment (layout, layout->lines[i]) != NS_PANGO_ALIGN_LEFT)
          need_width = TRUE;
    }
  else if (layout->alignment != NS_PANGO_ALIGN_LEFT)
    need_width = TRUE;

  if (width == -1 && need_width && (ink_rect || line_extents))
    {
      NsPangoRectangle overall;

      get_extents_internal (layout, NULL, &overall, NULL);
      width = overall.width;
    }

  if (logical_rect)
    logical_rect->x = logical_rect->y = logical_rect->width = logical_rect->height = 0;
  if (line_extents && layout->n_lines > 0)
    *line_extents = nt_new (NtExtents, layout->n_lines);

  for (i = 0; i < layout->n_lines; i++)
    {
      NtLine *line = layout->lines[i];
      NsPangoRectangle ink_layout, logical_layout;
      int new_pos;

      line_extents_layout_coords (layout, line, width, y_offset, &baseline,
                                  ink_rect ? &ink_layout : NULL, &logical_layout, i == 0);
      if (line_extents && layout->n_lines > 0)
        {
          (*line_extents)[i].baseline = baseline;
          (*line_extents)[i].ink_rect = ink_rect ? ink_layout : logical_layout;
          (*line_extents)[i].logical_rect = logical_layout;
        }

      if (ink_rect)
        {
          if (i == 0)
            *ink_rect = ink_layout;
          else
            {
              new_pos = NT_MIN (ink_rect->x, ink_layout.x);
              ink_rect->width = NT_MAX (ink_rect->x + ink_rect->width,
                                        ink_layout.x + ink_layout.width) - new_pos;
              ink_rect->x = new_pos;
              new_pos = NT_MIN (ink_rect->y, ink_layout.y);
              ink_rect->height = NT_MAX (ink_rect->y + ink_rect->height,
                                         ink_layout.y + ink_layout.height) - new_pos;
              ink_rect->y = new_pos;
            }
        }

      if (logical_rect)
        {
          if (layout->width == -1)
            logical_rect->width = NT_MAX (logical_rect->width, logical_layout.width);
          else if (i == 0)
            {
              logical_rect->x = logical_layout.x;
              logical_rect->width = logical_layout.width;
            }
          else
            {
              new_pos = NT_MIN (logical_rect->x, logical_layout.x);
              logical_rect->width = NT_MAX (logical_rect->x + logical_rect->width,
                                            logical_layout.x + logical_layout.width) - new_pos;
              logical_rect->x = new_pos;
            }
          logical_rect->height = logical_layout.y + logical_layout.height - logical_rect->y;
        }

      y_offset = logical_layout.y + logical_layout.height + layout->spacing;
    }

  if (ink_rect)
    {
      layout->ink_rect = *ink_rect;
      layout->ink_cached = TRUE;
    }
  if (logical_rect)
    {
      layout->logical_rect = *logical_rect;
      layout->logical_cached = TRUE;
    }
}

NtExtents *
nt_layout_line_extents (NsPangoLayout *layout, int *layout_width)
{
  NtExtents *ext = NULL;

  if (layout->width == -1)
    {
      NsPangoRectangle logical;

      get_extents_internal (layout, NULL, &logical, &ext);
      if (layout_width)
        *layout_width = logical.width;
    }
  else
    {
      get_extents_internal (layout, NULL, NULL, &ext);
      if (layout_width)
        *layout_width = layout->width;
    }
  return ext;
}

void
ns_pango_layout_get_extents (NsPangoLayout *layout, NsPangoRectangle *ink_rect,
                             NsPangoRectangle *logical_rect)
{
  get_extents_internal (layout, ink_rect, logical_rect, NULL);
}

static void
extents_to_pixels (NsPangoRectangle *r)
{
  int x1, y1, x2, y2;

  if (!r)
    return;
  x1 = NS_PANGO_PIXELS_FLOOR (r->x);
  y1 = NS_PANGO_PIXELS_FLOOR (r->y);
  x2 = NS_PANGO_PIXELS_CEIL (r->x + r->width);
  y2 = NS_PANGO_PIXELS_CEIL (r->y + r->height);
  r->x = x1;
  r->y = y1;
  r->width = x2 - x1;
  r->height = y2 - y1;
}

void
ns_pango_layout_get_pixel_extents (NsPangoLayout *layout, NsPangoRectangle *ink_rect,
                                   NsPangoRectangle *logical_rect)
{
  ns_pango_layout_get_extents (layout, ink_rect, logical_rect);
  extents_to_pixels (ink_rect);
  extents_to_pixels (logical_rect);
}

void
ns_pango_layout_get_size (NsPangoLayout *layout, int *width, int *height)
{
  NsPangoRectangle logical;

  ns_pango_layout_get_extents (layout, NULL, &logical);
  if (width)
    *width = logical.width;
  if (height)
    *height = logical.height;
}

void
ns_pango_layout_get_pixel_size (NsPangoLayout *layout, int *width, int *height)
{
  NsPangoRectangle logical;

  ns_pango_layout_get_extents (layout, NULL, &logical);
  extents_to_pixels (&logical);
  if (width)
    *width = logical.width;
  if (height)
    *height = logical.height;
}

int
ns_pango_layout_get_baseline (NsPangoLayout *layout)
{
  NtExtents *ext = nt_layout_line_extents (layout, NULL);
  int baseline = ext ? ext[0].baseline : 0;

  free (ext);
  return baseline;
}

int
ns_pango_layout_get_line_count (NsPangoLayout *layout)
{
  nt_layout_check_lines (layout);
  return layout->n_lines;
}

NsPangoLayoutLine *
ns_pango_layout_get_line_readonly (NsPangoLayout *layout, int line)
{
  nt_layout_check_lines (layout);
  if (line < 0 || line >= layout->n_lines)
    return NULL;
  return &layout->lines[line]->pub;
}

const NsPangoLogAttr *
ns_pango_layout_get_log_attrs_readonly (NsPangoLayout *layout, int *n_attrs)
{
  nt_layout_check_lines (layout);
  if (n_attrs)
    *n_attrs = layout->n_chars + 1;
  return layout->log_attrs;
}

/* ---- positions --------------------------------------------------------- */

void
ns_pango_layout_line_index_to_x (NsPangoLayoutLine *pline, int index, int trailing, int *x_pos)
{
  NtLine *line = (NtLine *) pline;
  NsPangoLayout *layout = pline->layout;
  int width = 0, i;

  for (i = 0; i < line->n_runs; i++)
    {
      NtRun *run = line->runs[i];

      if (run->item->offset <= index && run->item->offset + run->item->length > index)
        {
          int offset = nt_utf8_pointer_to_offset (layout->text, layout->text + index);

          if (trailing)
            {
              while (index < pline->start_index + pline->length && offset + 1 < layout->n_chars &&
                     !layout->log_attrs[offset + 1].is_cursor_position)
                {
                  offset++;
                  index = (int) (nt_utf8_next_char (layout->text + index) - layout->text);
                }
            }
          else
            {
              while (index > pline->start_index && !layout->log_attrs[offset].is_cursor_position)
                {
                  offset--;
                  index = (int) (nt_utf8_prev_char (layout->text + index) - layout->text);
                }
            }
          nt_glyph_index_to_x (run->glyphs, layout->text, run->item,
                               layout->log_attrs + run_log_attrs_offset (run),
                               index - run->item->offset, trailing, x_pos);
          if (x_pos)
            *x_pos += width;
          return;
        }
      width += nt_glyph_string_get_width (run->glyphs);
    }
  if (x_pos)
    *x_pos = width;
}

static NtLine *
index_to_line (NsPangoLayout *layout, int index, int *line_nr)
{
  NtLine *line = NULL;
  int i, nr = -1;

  for (i = 0; i < layout->n_lines; i++)
    {
      NtLine *l = layout->lines[i];

      if (l->pub.start_index > index)
        break;
      line = l;
      nr = i;
      if (l->pub.start_index + l->pub.length > index)
        break;
    }
  if (line_nr)
    *line_nr = nr;
  return line;
}

void
ns_pango_layout_index_to_line_x (NsPangoLayout *layout, int index, int trailing, int *line,
                                 int *x_pos)
{
  NtLine *l;
  int nr;

  nt_layout_check_lines (layout);
  if (index < 0)
    index = 0;
  if (index > layout->length)
    index = layout->length;
  l = index_to_line (layout, index, &nr);
  if (l)
    {
      if (index > l->pub.start_index + l->pub.length)
        index = l->pub.start_index + l->pub.length;
      if (line)
        *line = nr;
      ns_pango_layout_line_index_to_x (&l->pub, index, trailing, x_pos);
    }
  else
    {
      if (line)
        *line = -1;
      if (x_pos)
        *x_pos = -1;
    }
}

int
ns_pango_layout_line_x_to_index (NsPangoLayoutLine *pline, int x_pos, int *index, int *trailing)
{
  NtLine *line = (NtLine *) pline;
  NsPangoLayout *layout = pline->layout;
  int start_pos = 0, first_index, first_offset, last_index, last_offset;
  int end_index, end_offset, last_trailing, suppress_last_trailing = FALSE, i;

  first_index = pline->start_index;
  if (pline->length == 0)
    {
      if (index)
        *index = first_index;
      if (trailing)
        *trailing = 0;
      return FALSE;
    }

  first_offset = nt_utf8_pointer_to_offset (layout->text, layout->text + first_index);
  end_index = first_index + pline->length;
  end_offset = first_offset + nt_utf8_pointer_to_offset (layout->text + first_index,
                                                         layout->text + end_index);
  last_index = end_index;
  last_offset = end_offset;
  last_trailing = 0;
  do
    {
      last_index = (int) (nt_utf8_prev_char (layout->text + last_index) - layout->text);
      last_offset--;
      last_trailing++;
    }
  while (last_offset > first_offset && !layout->log_attrs[last_offset].is_cursor_position);

  for (i = 0; i < layout->n_lines; i++)
    if (layout->lines[i] == line)
      {
        if (i + 1 < layout->n_lines &&
            pline->start_index + pline->length == layout->lines[i + 1]->pub.start_index)
          suppress_last_trailing = TRUE;
        break;
      }

  if (x_pos < 0)
    {
      if (index)
        *index = pline->resolved_dir == NS_PANGO_DIRECTION_LTR ? first_index : last_index;
      if (trailing)
        *trailing = (pline->resolved_dir == NS_PANGO_DIRECTION_LTR || suppress_last_trailing)
                      ? 0 : last_trailing;
      return FALSE;
    }

  for (i = 0; i < line->n_runs; i++)
    {
      NtRun *run = line->runs[i];
      int logical_width = nt_glyph_string_get_width (run->glyphs);

      if (x_pos >= start_pos && x_pos < start_pos + logical_width)
        {
          int offset, char_trailing = 0, grapheme_start_index, grapheme_start_offset;
          int grapheme_end_offset, pos = 0, char_index;

          nt_glyph_x_to_index (run->glyphs, layout->text, run->item, x_pos - start_pos,
                               &pos, &char_trailing);
          char_index = run->item->offset + pos;
          offset = nt_utf8_pointer_to_offset (layout->text, layout->text + char_index);

          grapheme_start_offset = offset;
          grapheme_start_index = char_index;
          while (grapheme_start_offset > first_offset &&
                 !layout->log_attrs[grapheme_start_offset].is_cursor_position)
            {
              grapheme_start_index = (int) (nt_utf8_prev_char (layout->text + grapheme_start_index) -
                                            layout->text);
              grapheme_start_offset--;
            }
          grapheme_end_offset = offset;
          do
            grapheme_end_offset++;
          while (grapheme_end_offset < end_offset &&
                 !layout->log_attrs[grapheme_end_offset].is_cursor_position);

          if (index)
            *index = grapheme_start_index;
          if (trailing)
            {
              if ((grapheme_end_offset == end_offset && suppress_last_trailing) ||
                  offset + char_trailing <= (grapheme_start_offset + grapheme_end_offset) / 2)
                *trailing = 0;
              else
                *trailing = grapheme_end_offset - grapheme_start_offset;
            }
          return TRUE;
        }
      start_pos += logical_width;
    }

  if (index)
    *index = pline->resolved_dir == NS_PANGO_DIRECTION_LTR ? last_index : first_index;
  if (trailing)
    *trailing = (pline->resolved_dir == NS_PANGO_DIRECTION_LTR && !suppress_last_trailing)
                  ? last_trailing : 0;
  return FALSE;
}

static void
line_yrange (NsPangoLayout *layout, const NtExtents *ext, int i, int *y0, int *y1)
{
  int half_spacing = layout->spacing / 2;

  if (y0)
    *y0 = i == 0 ? ext[i].logical_rect.y
                 : ext[i].logical_rect.y - (layout->spacing - half_spacing);
  if (y1)
    *y1 = i == layout->n_lines - 1
            ? ext[i].logical_rect.y + ext[i].logical_rect.height
            : ext[i].logical_rect.y + ext[i].logical_rect.height + half_spacing;
}

int
ns_pango_layout_xy_to_index (NsPangoLayout *layout, int x, int y, int *index, int *trailing)
{
  NtExtents *ext = nt_layout_line_extents (layout, NULL);
  NtLine *prev_line = NULL, *found = NULL;
  int found_line_x = 0, prev_last = 0, prev_line_x = 0, outside = FALSE, i, retval;

  for (i = 0; i < layout->n_lines; i++)
    {
      int first_y, last_y;
      NtLine *line = layout->lines[i];

      line_yrange (layout, ext, i, &first_y, &last_y);
      if (y < first_y)
        {
          if (prev_line && y < (prev_last + (first_y - prev_last) / 2))
            {
              found = prev_line;
              found_line_x = prev_line_x;
            }
          else
            {
              if (!prev_line)
                outside = TRUE;
              found = line;
              found_line_x = x - ext[i].logical_rect.x;
            }
        }
      else if (y >= first_y && y < last_y)
        {
          found = line;
          found_line_x = x - ext[i].logical_rect.x;
        }
      prev_line = line;
      prev_last = last_y;
      prev_line_x = x - ext[i].logical_rect.x;
      if (found)
        break;
    }
  free (ext);
  if (!found)
    {
      outside = TRUE;
      found = prev_line;
      found_line_x = prev_line_x;
    }
  if (!found)
    {
      if (index)
        *index = 0;
      if (trailing)
        *trailing = 0;
      return FALSE;
    }
  retval = ns_pango_layout_line_x_to_index (&found->pub, found_line_x, index, trailing);
  return outside ? FALSE : retval;
}

void
ns_pango_layout_index_to_pos (NsPangoLayout *layout, int index, NsPangoRectangle *pos)
{
  NtExtents *ext = nt_layout_line_extents (layout, NULL);
  NtLine *line = NULL;
  NsPangoRectangle run_logical = { 0 };
  int i, li = 0, x_pos, run_x;

  if (!ext)
    {
      pos->x = pos->y = pos->width = pos->height = 0;
      return;
    }
  for (i = 0; i < layout->n_lines; i++)
    {
      NtLine *l = layout->lines[i];

      if (l->pub.start_index > index)
        {
          index = line->pub.start_index + line->pub.length;
          break;
        }
      line = l;
      li = i;
      if (l->pub.start_index + l->pub.length >= index)
        {
          /* The run holding the index, else the end of the line. */
          int r;

          run_x = ext[i].logical_rect.x;
          for (r = 0; r <= line->n_runs; r++)
            {
              if (r == line->n_runs)
                {
                  if (line->n_runs > 0)
                    nt_run_get_extents (line->runs[line->n_runs - 1], NULL, &run_logical);
                  else
                    empty_extents_at_index (layout, 0, &run_logical, NULL);
                  run_logical.x = run_x;
                  run_logical.width = 0;
                  run_logical.y += ext[i].baseline;
                  break;
                }
              nt_run_get_extents (line->runs[r], NULL, &run_logical);
              run_logical.y += ext[i].baseline;
              run_logical.x += run_x;
              if (line->runs[r]->item->offset <= index &&
                  index < line->runs[r]->item->offset + line->runs[r]->item->length)
                break;
              run_x += nt_glyph_string_get_width (line->runs[r]->glyphs);
            }
          if (l->pub.start_index + l->pub.length > index)
            break;
        }
      if (i == layout->n_lines - 1)
        index = line->pub.start_index + line->pub.length;
    }

  if (!line)
    {
      pos->x = pos->y = pos->width = pos->height = 0;
      free (ext);
      return;
    }
  pos->y = run_logical.y;
  pos->height = run_logical.height;
  ns_pango_layout_line_index_to_x (&line->pub, index, 0, &x_pos);
  pos->x = ext[li].logical_rect.x + x_pos;
  if (index < line->pub.start_index + line->pub.length)
    {
      ns_pango_layout_line_index_to_x (&line->pub, index, 1, &x_pos);
      pos->width = (ext[li].logical_rect.x + x_pos) - pos->x;
    }
  else
    pos->width = 0;
  free (ext);
}

void
ns_pango_layout_line_get_x_ranges (NsPangoLayoutLine *pline, int start_index, int end_index,
                                   int **ranges, int *n_ranges)
{
  NtLine *line = (NtLine *) pline;
  NsPangoLayout *layout = pline->layout;
  int range_count = 0, accumulated_width = 0, x_offset, width, line_width, i;
  NsPangoAlignment alignment = get_alignment (layout, line);
  NsPangoRectangle logical;

  width = layout->width;
  if (width == -1 && alignment != NS_PANGO_ALIGN_LEFT)
    {
      NsPangoRectangle overall;

      ns_pango_layout_get_extents (layout, NULL, &overall);
      width = overall.width;
    }
  ns_pango_layout_line_get_extents (pline, NULL, &logical);
  line_width = logical.width;
  get_x_offset (layout, line, width, line_width, &x_offset);

  if (ranges)
    *ranges = nt_new (int, 2 * (2 + line->n_runs));

  if (x_offset > 0 &&
      ((pline->resolved_dir == NS_PANGO_DIRECTION_LTR && start_index < pline->start_index) ||
       (pline->resolved_dir == NS_PANGO_DIRECTION_RTL &&
        end_index > pline->start_index + pline->length)))
    {
      if (ranges)
        {
          (*ranges)[2 * range_count] = 0;
          (*ranges)[2 * range_count + 1] = x_offset;
        }
      range_count++;
    }

  for (i = 0; i < line->n_runs; i++)
    {
      NtRun *run = line->runs[i];

      if (start_index < run->item->offset + run->item->length && end_index > run->item->offset)
        {
          if (ranges)
            {
              int run_start_index = NT_MAX (start_index, run->item->offset);
              int run_end_index = NT_MIN (end_index, run->item->offset + run->item->length);
              int run_start_x = 0, run_end_x = 0;

              run_end_index = (int) (nt_utf8_prev_char (layout->text + run_end_index) - layout->text);
              nt_glyph_index_to_x (run->glyphs, layout->text, run->item,
                                   layout->log_attrs + run_log_attrs_offset (run),
                                   run_start_index - run->item->offset, FALSE, &run_start_x);
              nt_glyph_index_to_x (run->glyphs, layout->text, run->item,
                                   layout->log_attrs + run_log_attrs_offset (run),
                                   run_end_index - run->item->offset, TRUE, &run_end_x);
              (*ranges)[2 * range_count] = x_offset + accumulated_width + NT_MIN (run_start_x, run_end_x);
              (*ranges)[2 * range_count + 1] = x_offset + accumulated_width + NT_MAX (run_start_x, run_end_x);
            }
          range_count++;
        }
      if (i + 1 < line->n_runs)
        accumulated_width += nt_glyph_string_get_width (run->glyphs);
    }

  if (x_offset + line_width < layout->width &&
      ((pline->resolved_dir == NS_PANGO_DIRECTION_LTR &&
        end_index > pline->start_index + pline->length) ||
       (pline->resolved_dir == NS_PANGO_DIRECTION_RTL && start_index < pline->start_index)))
    {
      if (ranges)
        {
          (*ranges)[2 * range_count] = x_offset + line_width;
          (*ranges)[2 * range_count + 1] = layout->width;
        }
      range_count++;
    }
  if (n_ranges)
    *n_ranges = range_count;
}

/* ---- iterators --------------------------------------------------------- */

struct _PangoLayoutIter
{
  NsPangoLayout *layout;
  NtExtents     *extents;
  int            layout_width;
  int            line;
};

NsPangoLayoutIter *
ns_pango_layout_get_iter (NsPangoLayout *layout)
{
  NsPangoLayoutIter *it = nt_new0 (NsPangoLayoutIter, 1);

  it->layout = ns_pango_layout_ref (layout);
  it->extents = nt_layout_line_extents (layout, &it->layout_width);
  return it;
}

void
ns_pango_layout_iter_free (NsPangoLayoutIter *iter)
{
  if (!iter)
    return;
  free (iter->extents);
  ns_pango_layout_unref (iter->layout);
  free (iter);
}

int
ns_pango_layout_iter_next_line (NsPangoLayoutIter *iter)
{
  if (iter->line + 1 >= iter->layout->n_lines)
    return FALSE;
  iter->line++;
  return TRUE;
}

NsPangoLayoutLine *
ns_pango_layout_iter_get_line_readonly (NsPangoLayoutIter *iter)
{
  if (iter->line >= iter->layout->n_lines)
    return NULL;
  return &iter->layout->lines[iter->line]->pub;
}

void
ns_pango_layout_iter_get_line_extents (NsPangoLayoutIter *iter, NsPangoRectangle *ink_rect,
                                       NsPangoRectangle *logical_rect)
{
  const NtExtents *ext;

  if (!iter->extents || iter->line >= iter->layout->n_lines)
    return;
  ext = &iter->extents[iter->line];
  if (ink_rect)
    line_extents_layout_coords (iter->layout, iter->layout->lines[iter->line],
                                iter->layout_width, ext->logical_rect.y, NULL, ink_rect,
                                NULL, TRUE);
  if (logical_rect)
    *logical_rect = ext->logical_rect;
}

int
ns_pango_layout_iter_get_baseline (NsPangoLayoutIter *iter)
{
  if (!iter->extents || iter->line >= iter->layout->n_lines)
    return 0;
  return iter->extents[iter->line].baseline;
}

/* ---- metrics ----------------------------------------------------------- */

static int
utf8_strwidth (const char *p)
{
  int w = 0;

  for (; *p; p = nt_utf8_next_char (p))
    {
      nt_unichar c = nt_utf8_get_char (p);

      if (nt_unichar_iszerowidth (c))
        continue;
      w += nt_unichar_iswide (c) ? 2 : 1;
    }
  return w;
}

void nt_layout_measure_font (NtFont *font, const char *sample, int *char_width,
                             int *digit_width);

void
nt_layout_measure_font (NtFont *font, const char *sample, int *char_width, int *digit_width)
{
  NtItem item;
  NtGlyphString *gs = nt_glyph_string_new ();
  int i, w, width;

  memset (&item, 0, sizeof item);
  item.font = font;
  item.script = HB_SCRIPT_LATIN;
  item.language = ns_pango_language_get_default ();
  item.length = (int) strlen (sample);
  item.num_chars = nt_utf8_strlen (sample, item.length);
  nt_shape_item (sample, item.length, &item, NULL, gs);
  width = nt_glyph_string_get_width (gs);
  w = utf8_strwidth (sample);
  *char_width = w > 0 ? width / w : 0;

  item.length = 10;
  item.num_chars = 10;
  item.script = HB_SCRIPT_COMMON;
  nt_shape_item ("0123456789", 10, &item, NULL, gs);
  *digit_width = 0;
  for (i = 0; i < gs->num_glyphs; i++)
    *digit_width = NT_MAX (*digit_width, gs->glyphs[i].width);
  nt_glyph_string_free (gs);
}

NsPangoFontMetrics *
ns_pango_context_get_metrics (NsPangoContext *context, const NsPangoFontDescription *desc,
                              NsPangoLanguage *language)
{
  NsPangoFontMetrics *metrics, *cached;
  NtFontset *fs;
  NtFont *first = NULL;
  const char *sample;
  NtItemList items = { 0 };
  NsPangoAttrList *attrs;
  NtString key = { 0 };
  int i, width = 0, sw;

  nt_context_check_fontmap (context);
  if (!desc)
    desc = context->font_desc;
  if (!language)
    language = context->language;

  nt_string_printf (&key, "%s|%d|%d|%d|%d|%d|%d|%s|%s",
                    desc->family ? desc->family : "", desc->style, desc->weight, desc->stretch,
                    desc->variant, desc->size, desc->size_is_absolute,
                    desc->variations ? desc->variations : "", language ? language->tag : "");
  cached = nt_hash_lookup (context->metrics, key.str, key.len);
  if (cached)
    {
      free (key.str);
      return ns_pango_font_metrics_ref (cached);
    }

  metrics = nt_font_metrics_new ();
  fs = nt_context_load_fontset (context, desc, language);
  for (i = 0; i < 4 && !first; i++)
    first = nt_fontset_get_font_at (fs, i);
  if (first)
    {
      NsPangoFontMetrics *m = nt_font_get_metrics (first);

      *metrics = *m;
      metrics->ref_count = 1;
      ns_pango_font_metrics_unref (m);
    }
  nt_fontset_unref (fs);

  sample = nt_language_sample_string (language);
  attrs = ns_pango_attr_list_new ();
  {
    NsPangoAttribute *a = ns_pango_attr_font_desc_new (desc);

    ns_pango_attr_list_insert (attrs, a);
    a = ns_pango_attr_language_new (language);
    ns_pango_attr_list_insert (attrs, a);
  }
  nt_itemize (context, context->base_dir, sample, 0, (int) strlen (sample), attrs, NULL, &items);
  for (i = 0; i < items.n; i++)
    {
      NtItem *item = items.items[i];
      NtGlyphString *gs = nt_glyph_string_new ();

      if (item->font)
        {
          NsPangoFontMetrics *raw = nt_font_get_metrics (item->font);

          metrics->ascent = NT_MAX (metrics->ascent, raw->ascent);
          metrics->descent = NT_MAX (metrics->descent, raw->descent);
          metrics->height = NT_MAX (metrics->height, raw->height);
          ns_pango_font_metrics_unref (raw);
        }
      nt_shape_item_full (sample, (int) strlen (sample), item, NULL, FALSE, gs);
      width += nt_glyph_string_get_width (gs);
      nt_glyph_string_free (gs);
    }
  nt_item_list_clear (&items);
  ns_pango_attr_list_unref (attrs);
  sw = utf8_strwidth (sample);
  if (sw > 0)
    metrics->approximate_char_width = width / sw;
  if (getenv ("NSTEXT_DEBUG"))
    fprintf (stderr, "nstext: metrics %s lang %s sample \"%s\" width %d chars %d -> %d\n",
             key.str, language ? language->tag : "-", sample, width, sw,
             metrics->approximate_char_width);

  nt_hash_insert (context->metrics, key.str, key.len, ns_pango_font_metrics_ref (metrics));
  free (key.str);
  return metrics;
}
