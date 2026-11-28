/* Weststar SDL Browser — nstext: itemization. A paragraph is cut into
 * items of one bidi level, script, emoji presentation and set of font
 * attributes, and each character gets the first font of its fontset that
 * covers it, as Pango's itemize.c does; bidi levels come from FriBidi as
 * in pango-bidi-type.c.
 *
 * Copyright (C) 2000, 2006 Red Hat Software
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "nstext-private.h"

#include <fribidi.h>

NtFontset *nt_context_load_fontset (NsPangoContext *context, const NsPangoFontDescription *desc,
                                    NsPangoLanguage *language);

/* ---- bidi -------------------------------------------------------------- */

static NsPangoDirection
unichar_direction (nt_unichar ch)
{
  FriBidiCharType t = fribidi_get_bidi_type (ch);

  if (!FRIBIDI_IS_STRONG (t))
    return NS_PANGO_DIRECTION_NEUTRAL;
  return FRIBIDI_IS_RTL (t) ? NS_PANGO_DIRECTION_RTL : NS_PANGO_DIRECTION_LTR;
}

NsPangoDirection
nt_find_base_dir (const char *text, int length)
{
  const char *p = text;
  NsPangoDirection dir = NS_PANGO_DIRECTION_NEUTRAL;

  while ((length < 0 || p < text + length) && *p)
    {
      dir = unichar_direction (nt_utf8_get_char (p));
      if (dir != NS_PANGO_DIRECTION_NEUTRAL)
        break;
      p = nt_utf8_next_char (p);
    }
  return dir;
}

void
nt_find_paragraph_boundary (const char *text, int length, int *delimiter_index,
                            int *next_paragraph_start)
{
  const char *p = text, *end, *start = NULL, *delimiter = NULL;
  char prev_sep = 0;

  if (length < 0)
    length = (int) strlen (text);
  end = text + length;
  *delimiter_index = length;
  *next_paragraph_start = length;
  if (length == 0)
    return;

  while (p < end)
    {
      if (prev_sep == '\n' || prev_sep == '\xE2')
        {
          start = p;
          break;
        }
      else if (prev_sep == '\r' && *p != '\n')
        {
          start = p;
          break;
        }
      if (*p == '\n' || *p == '\r' ||
          (end - p >= 3 && memcmp (p, "\xE2\x80\xA9", 3) == 0))
        {
          if (!delimiter)
            delimiter = p;
          prev_sep = *p;
        }
      else
        prev_sep = 0;
      p = nt_utf8_next_char (p);
    }
  if (delimiter)
    *delimiter_index = (int) (delimiter - text);
  if (start)
    *next_paragraph_start = (int) (start - text);
}

static void
fill_embedding_levels (const char *text, int length, int n_chars, uint8_t *levels,
                       NsPangoDirection *pbase_dir)
{
  FriBidiParType base;
  FriBidiCharType *types = nt_new (FriBidiCharType, n_chars ? n_chars : 1);
  FriBidiBracketType *brackets = nt_new (FriBidiBracketType, n_chars ? n_chars : 1);
  FriBidiCharType ored = 0, anded_strongs = FRIBIDI_TYPE_RLE;
  const char *p;
  int i;

  switch (*pbase_dir)
    {
    case NS_PANGO_DIRECTION_LTR:
    case NS_PANGO_DIRECTION_TTB_RTL:
      base = FRIBIDI_PAR_LTR;
      break;
    case NS_PANGO_DIRECTION_RTL:
    case NS_PANGO_DIRECTION_TTB_LTR:
      base = FRIBIDI_PAR_RTL;
      break;
    case NS_PANGO_DIRECTION_WEAK_RTL:
      base = FRIBIDI_PAR_WRTL;
      break;
    case NS_PANGO_DIRECTION_WEAK_LTR:
    case NS_PANGO_DIRECTION_NEUTRAL:
    default:
      base = FRIBIDI_PAR_WLTR;
      break;
    }

  for (i = 0, p = text; p < text + length && i < n_chars; p = nt_utf8_next_char (p), i++)
    {
      nt_unichar ch = nt_utf8_get_char (p);
      FriBidiCharType t = fribidi_get_bidi_type (ch);

      types[i] = t;
      ored |= t;
      if (FRIBIDI_IS_STRONG (t))
        anded_strongs &= t;
      brackets[i] = t == FRIBIDI_TYPE_ON ? fribidi_get_bracket (ch) : FRIBIDI_NO_BRACKET;
    }

  if (!FRIBIDI_IS_ISOLATE (ored) && !FRIBIDI_IS_RTL (ored) && !FRIBIDI_IS_ARABIC (ored) &&
      (!FRIBIDI_IS_RTL (base) || (FRIBIDI_IS_WEAK (base) && FRIBIDI_IS_LETTER (ored))))
    {
      base = FRIBIDI_PAR_LTR;
      memset (levels, 0, (size_t) n_chars);
    }
  else if (!FRIBIDI_IS_ISOLATE (ored) && !FRIBIDI_IS_NUMBER (ored) &&
           FRIBIDI_IS_RTL (anded_strongs) &&
           (FRIBIDI_IS_RTL (base) || (FRIBIDI_IS_WEAK (base) && FRIBIDI_IS_LETTER (ored))))
    {
      base = FRIBIDI_PAR_RTL;
      memset (levels, 1, (size_t) n_chars);
    }
  else if (fribidi_get_par_embedding_levels_ex (types, brackets, n_chars, &base,
                                                (FriBidiLevel *) levels) == 0)
    memset (levels, 0, (size_t) n_chars);

  free (types);
  free (brackets);
  *pbase_dir = base == FRIBIDI_PAR_LTR ? NS_PANGO_DIRECTION_LTR : NS_PANGO_DIRECTION_RTL;
}

/* ---- itemization ------------------------------------------------------- */

/* Spaces and controls do not choose fonts (they take their neighbours'). */
static int
consider_as_space (nt_unichar wc)
{
  hb_unicode_general_category_t type = nt_unichar_type (wc);

  return type == HB_UNICODE_GENERAL_CATEGORY_CONTROL ||
         (type == HB_UNICODE_GENERAL_CATEGORY_FORMAT &&
          !((wc >= 0x600 && wc <= 0x06ff) || wc == 0x70f || wc == 0x8e2)) ||
         type == HB_UNICODE_GENERAL_CATEGORY_SURROGATE ||
         type == HB_UNICODE_GENERAL_CATEGORY_LINE_SEPARATOR ||
         type == HB_UNICODE_GENERAL_CATEGORY_PARAGRAPH_SEPARATOR ||
         (type == HB_UNICODE_GENERAL_CATEGORY_SPACE_SEPARATOR && wc != 0x1680u) ||
         (wc >= 0xfe00u && wc <= 0xfe0fu) || (wc >= 0xe0100u && wc <= 0xe01efu);
}

static NsPangoLanguage *
compute_derived_language (NsPangoLanguage *lang, hb_script_t script)
{
  NsPangoLanguage *derived;

  if (lang && nt_language_includes_script (lang, script))
    return lang;
  derived = nt_script_sample_language (script);
  return derived ? derived : ns_pango_language_from_string ("xx");
}

typedef struct
{
  NsPangoContext *context;
  const char     *text;
  NtItemList     *out;
  NtItem         *item;
  int             font_position;
  const char     *first_space;
  int             char_offset;
} State;

/* The extra attributes of a range: everything that is not a font field,
 * the innermost of each type, as Pango's attribute iterator gives them. */
static void
collect_extra (NtAttrIter *it, NtAttr ***out, int *n_out)
{
  int i, j;

  *out = NULL;
  *n_out = 0;
  if (!it)
    return;
  for (i = it->n_open - 1; i >= 0; i--)
    {
      const NtAttr *a = it->open[i];
      int dup = FALSE;

      switch ((NtAttrType) a->base.type)
        {
        case NT_ATTR_FONT_DESC:
        case NT_ATTR_FAMILY:
        case NT_ATTR_STYLE:
        case NT_ATTR_WEIGHT:
        case NT_ATTR_STRETCH:
        case NT_ATTR_ABSOLUTE_SIZE:
        case NT_ATTR_LANGUAGE:
          continue;
        default:
          break;
        }
      for (j = 0; j < *n_out; j++)
        if ((*out)[j]->base.type == a->base.type)
          dup = TRUE;
      if (dup)
        continue;
      *out = nt_renew (NtAttr *, *out, *n_out + 1);
      (*out)[(*n_out)++] = nt_attr_copy (a);
    }
  /* Back into the list's order. */
  for (i = 0, j = *n_out - 1; i < j; i++, j--)
    {
      NtAttr *t = (*out)[i];

      (*out)[i] = (*out)[j];
      (*out)[j] = t;
    }
}

static void
fill_font (State *st, NtFont *font)
{
  int i;

  for (i = st->out->n - 1; i >= 0; i--)
    {
      NtItem *item = st->out->items[i];

      if (item->font)
        break;
      if (font)
        item->font = nt_font_ref (font);
    }
}

static void
add_character (State *st, NtFont *font, int font_position, int force_break,
               const char *pos, int is_space, uint8_t level, hb_script_t script,
               NsPangoLanguage *lang, NtAttr **extra, int n_extra)
{
  const char *first_space = st->first_space;
  int n_spaces = 0;

  if (is_space)
    {
      if (!st->first_space)
        st->first_space = pos;
    }
  else
    st->first_space = NULL;

  if (st->item)
    {
      if (!st->item->font && font)
        {
          fill_font (st, font);
          st->font_position = font_position;
        }
      else if (st->item->font && !font)
        {
          font = st->item->font;
          font_position = st->font_position;
        }

      if (!force_break && st->item->font == font)
        {
          st->item->num_chars++;
          st->char_offset++;
          return;
        }

      /* Spaces at the end of an item go with the next one when its font is
       * less of a fallback (Pango issue 249). */
      if (first_space && st->text + st->item->offset < first_space &&
          font_position < st->font_position)
        {
          n_spaces = nt_utf8_strlen (first_space, (int) (pos - first_space));
          st->item->num_chars -= n_spaces;
          pos = first_space;
        }
      st->item->length = (int) (pos - st->text) - st->item->offset;
    }

  st->item = nt_item_new ();
  st->item->offset = (int) (pos - st->text);
  st->item->num_chars = n_spaces + 1;
  st->item->char_offset = st->char_offset - n_spaces;
  st->char_offset++;
  st->item->font = nt_font_ref (font);
  st->font_position = font_position;
  st->item->level = level;
  st->item->script = script;
  st->item->language = lang;
  if (n_extra)
    {
      int i;

      st->item->extra_attrs = nt_new (NtAttr *, n_extra);
      for (i = 0; i < n_extra; i++)
        st->item->extra_attrs[i] = nt_attr_copy (extra[i]);
      st->item->n_extra = n_extra;
    }
  nt_item_list_append (st->out, st->item);
}

void
nt_itemize (NsPangoContext *context, NsPangoDirection base_dir,
            const char *text, int start, int length,
            const NsPangoAttrList *itemize_attrs,
            const NsPangoFontDescription *desc,
            NtItemList *out)
{
  const char *p = text + start, *end = text + start + length;
  int n_chars = nt_utf8_strlen (p, length);
  uint8_t *levels;
  NtAttrIter attr_iter, *it = NULL;
  NtScriptIter script_iter;
  NtEmojiIter emoji_iter;
  State st;
  int ci = 0;
  int first_item = out->n;

  if (length <= 0)
    return;

  memset (&st, 0, sizeof st);
  st.context = context;
  st.text = text;
  st.out = out;
  st.font_position = 0xffff;
  st.char_offset = 0; /* relative; fixed up below */

  levels = nt_new (uint8_t, n_chars);
  fill_embedding_levels (p, length, n_chars, levels, &base_dir);

  if (itemize_attrs)
    {
      it = &attr_iter;
      nt_attr_iter_init (it, itemize_attrs);
      while (it->end <= (unsigned) start && nt_attr_iter_next (it))
        ;
    }
  nt_script_iter_init (&script_iter, p, length);
  nt_emoji_iter_init (&emoji_iter, p, length, (unsigned) n_chars);

  while (p < end)
    {
      /* The run: up to the next change of level, attributes, script or
       * emoji presentation. */
      const char *run_end = end, *q;
      uint8_t level = levels[ci];
      NsPangoFontDescription *font_desc;
      NsPangoLanguage *lang = NULL, *derived;
      NtAttr **extra = NULL;
      int n_extra = 0, k, run_ci;
      NtFontset *fontset;
      int last_was_forced_break = FALSE;

      if (it && it->end != (unsigned) -1 && text + it->end < run_end)
        run_end = text + it->end;
      if (script_iter.script_end < run_end)
        run_end = script_iter.script_end;
      if (emoji_iter.end < run_end)
        run_end = emoji_iter.end;
      for (q = p, k = ci; q < run_end; q = nt_utf8_next_char (q), k++)
        if (levels[k] != level)
          {
            run_end = q;
            break;
          }

      font_desc = ns_pango_font_description_copy (context->font_desc);
      if (desc)
        nt_font_description_merge (font_desc, desc, TRUE);
      if (it)
        {
          nt_attr_iter_get_font (it, font_desc, &lang);
          collect_extra (it, &extra, &n_extra);
        }
      if (lang)
        derived = lang;
      else
        derived = compute_derived_language (context->language, script_iter.script_code);

      /* Emoji presentation: the emoji family, in colour when a variation
         selector asks; a text presentation selector asks for no colour. */
      if (emoji_iter.is_emoji)
        {
          ns_pango_font_description_set_family (font_desc, "emoji");
          font_desc->color = emoji_iter.has_vs ? FcTrue : FcDontCare;
        }
      else if (emoji_iter.has_vs)
        {
          ns_pango_font_description_set_family (font_desc, "emoji");
          font_desc->color = FcFalse;
        }

      fontset = nt_context_load_fontset (context, font_desc, derived);
      ns_pango_font_description_free (font_desc);

      for (q = p, run_ci = ci; q < run_end; q = nt_utf8_next_char (q), run_ci++)
        {
          nt_unichar wc = nt_utf8_get_char (q);
          int is_forced_break = wc == '\t' || wc == 0x2028;
          NtFont *font;
          int position;
          int is_space;

          if (consider_as_space (wc))
            {
              font = NULL;
              position = 0xffff;
              is_space = TRUE;
            }
          else
            {
              /* No font has it: the first font's unknown glyph. */
              font = nt_fontset_get_font (fontset, wc, &position);
              if (!font)
                font = nt_fontset_get_font_at (fontset, 0);
              is_space = FALSE;
            }

          if (q == p)
            st.item = NULL; /* every run starts an item */
          add_character (&st, font, position, is_forced_break || last_was_forced_break,
                         q, is_space, level, script_iter.script_code, derived, extra, n_extra);
          last_was_forced_break = is_forced_break;
        }

      st.item->length = (int) (run_end - text) - st.item->offset;
      if (!st.item->font)
        {
          int position;
          NtFont *font = nt_fontset_get_font (fontset, ' ', &position);

          if (!font)
            font = nt_fontset_get_font_at (fontset, 0);
          fill_font (&st, font);
        }
      st.item = NULL;
      st.first_space = NULL;

      for (k = 0; k < n_extra; k++)
        ns_pango_attribute_destroy (&extra[k]->base);
      free (extra);
      nt_fontset_unref (fontset);

      ci = run_ci;
      p = run_end;
      if (it && it->end != (unsigned) -1 && p >= text + it->end)
        nt_attr_iter_next (it);
      if (p >= script_iter.script_end)
        nt_script_iter_next (&script_iter);
      if (p >= emoji_iter.end)
        nt_emoji_iter_next (&emoji_iter);
    }

  /* Character offsets from the paragraph's. */
  {
    int i, base = nt_utf8_pointer_to_offset (text, text + start);

    for (i = first_item; i < out->n; i++)
      out->items[i]->char_offset += base;
  }

  nt_emoji_iter_fini (&emoji_iter);
  if (it)
    nt_attr_iter_destroy (it);
  free (levels);
}

/* Adds the attributes covering each item to it (they do not split items:
 * features carry their own ranges, breaking reads the list). */
void
nt_apply_attrs_to_items (NtItemList *items, const NsPangoAttrList *attrs)
{
  int i, j;

  if (!attrs)
    return;
  for (i = 0; i < items->n; i++)
    {
      NtItem *item = items->items[i];
      unsigned istart = (unsigned) item->offset;
      unsigned iend = (unsigned) (item->offset + item->length);

      for (j = 0; j < attrs->n; j++)
        {
          const NtAttr *a = attrs->attrs[j];
          int k, dup = FALSE;

          if (a->base.start_index >= iend || a->base.end_index < istart ||
              a->base.start_index == a->base.end_index)
            continue;
          for (k = 0; k < item->n_extra; k++)
            if (nt_attr_equal (item->extra_attrs[k], a) &&
                item->extra_attrs[k]->base.start_index == a->base.start_index &&
                item->extra_attrs[k]->base.end_index == a->base.end_index)
              dup = TRUE;
          if (dup)
            continue;
          item->extra_attrs = nt_renew (NtAttr *, item->extra_attrs, item->n_extra + 1);
          item->extra_attrs[item->n_extra++] = nt_attr_copy (a);
        }
    }
}
