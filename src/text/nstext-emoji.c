/* nstext (ported from Pango's pango-emoji.c)
 * pango-emoji.c: Emoji handling
 *
 * Copyright (C) 2017 Google, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Implementation of ns_pango_emoji_iter is based on Chromium's Ragel-based
 * parser:
 *
 * https://chromium-review.googlesource.com/c/chromium/src/+/1264577
 *
 * The grammar file emoji_presentation_scanner.rl was just modified to
 * adapt the function signature and variables to our usecase.  The
 * grammar itself was NOT modified:
 *
 * https://chromium-review.googlesource.com/c/chromium/src/+/1264577/3/third_party/blink/renderer/platform/fonts/emoji_presentation_scanner.rl
 *
 * The emoji_presentation_scanner.c is generated from .rl file by
 * running ragel on it.
 *
 * The categorization is also based on:
 *
 * https://chromium-review.googlesource.com/c/chromium/src/+/1264577/3/third_party/blink/renderer/platform/fonts/utf16_ragel_iterator.h
 *
 * The iterator next() is based on:
 *
 * https://chromium-review.googlesource.com/c/chromium/src/+/1264577/3/third_party/blink/renderer/platform/fonts/symbols_iterator.cc
 *
 * // Copyright 2015 The Chromium Authors. All rights reserved.
 * // Use of this source code is governed by a BSD-style license that can be
 * // found in the LICENSE file.
 */


#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "nstext-private.h"
#include "nstext-emoji-table.h"

static inline int
bsearch_interval (nt_unichar              c,
                  const struct Interval table[],
                  unsigned int                 n)
{
  unsigned int lower = 0;
  unsigned int upper = n - 1;

  while (lower <= upper)
    {
      int mid = (lower + upper) / 2;

      if (c < table[mid].start)
        upper = mid - 1;
      else if (c > table[mid].end)
        lower = mid + 1;
      else
        return TRUE;
    }

  return FALSE;
}

#define DEFINE_pango_Is_(name) \
static inline int \
nt_Is_##name (nt_unichar ch) \
{ \
  return ch >= nt_##name##_table[0].start && \
         bsearch_interval (ch, \
                           nt_##name##_table, \
                           NT_N_ELEMENTS (nt_##name##_table)); \
}

DEFINE_pango_Is_(Emoji)
DEFINE_pango_Is_(Emoji_Presentation)
DEFINE_pango_Is_(Emoji_Modifier)
DEFINE_pango_Is_(Emoji_Modifier_Base)
DEFINE_pango_Is_(Extended_Pictographic)

int
nt_is_emoji_base_character (nt_unichar ch)
{
	return nt_Is_Emoji (ch);
}

int
nt_is_emoji_extended_pictographic (nt_unichar ch)
{
	return nt_Is_Extended_Pictographic (ch);
}

static inline int
nt_Is_Emoji_Emoji_Default (nt_unichar ch)
{
  return nt_Is_Emoji_Presentation (ch);
}

static inline int
nt_Is_Emoji_Keycap_Base (nt_unichar ch)
{
  return (ch >= '0' && ch <= '9') || ch == '#' || ch == '*';
}

static inline int
nt_Is_Regional_Indicator (nt_unichar ch)
{
  return (ch >= 0x1F1E6 && ch <= 0x1F1FF);
}


#define kCombiningEnclosingCircleBackslashCharacter 0x20E0
#define kCombiningEnclosingKeycapCharacter 0x20E3
#define kVariationSelector15Character 0xFE0E
#define kVariationSelector16Character 0xFE0F
#define kZeroWidthJoinerCharacter 0x200D

enum NsPangoEmojiScannerCategory {
  EMOJI = 0,
  EMOJI_TEXT_PRESENTATION = 1,
  EMOJI_EMOJI_PRESENTATION = 2,
  EMOJI_MODIFIER_BASE = 3,
  EMOJI_MODIFIER = 4,
  EMOJI_VS_BASE = 5,
  REGIONAL_INDICATOR = 6,
  KEYCAP_BASE = 7,
  COMBINING_ENCLOSING_KEYCAP = 8,
  COMBINING_ENCLOSING_CIRCLE_BACKSLASH = 9,
  ZWJ = 10,
  VS15 = 11,
  VS16 = 12,
  TAG_BASE = 13,
  TAG_SEQUENCE = 14,
  TAG_TERM = 15,
  kMaxEmojiScannerCategory = 16
};

static inline unsigned char
nt_EmojiSegmentationCategory (nt_unichar codepoint)
{
  /* Specific ones first. */
  if (('a' <= codepoint && codepoint <= 'z') ||
      ('A' <= codepoint && codepoint <= 'Z') ||
      codepoint == ' ')
    return kMaxEmojiScannerCategory;

  if ('0' <= codepoint && codepoint <= '9')
    return KEYCAP_BASE;

  /* Below U+0080 the tables below can only match the keycap bases, which are
   * the digits above plus these two. Deciding that here spares every other
   * ASCII character -- punctuation and the controls, which the rest of this
   * function would otherwise put through seven interval searches -- the whole
   * of it.
   */
  if (codepoint < 0x80)
    return (codepoint == '#' || codepoint == '*') ? KEYCAP_BASE
                                                  : kMaxEmojiScannerCategory;

  switch (codepoint)
    {
    case kCombiningEnclosingKeycapCharacter:
      return COMBINING_ENCLOSING_KEYCAP;
    case kCombiningEnclosingCircleBackslashCharacter:
      return COMBINING_ENCLOSING_CIRCLE_BACKSLASH;
    case kZeroWidthJoinerCharacter:
      return ZWJ;
    case kVariationSelector15Character:
      return VS15;
    case kVariationSelector16Character:
      return VS16;
    case 0x1F3F4:
      return TAG_BASE;
    case 0xE007F:
      return TAG_TERM;
    default: ;
    }

  if ((0xE0030 <= codepoint && codepoint <= 0xE0039) ||
      (0xE0061 <= codepoint && codepoint <= 0xE007A))
    return TAG_SEQUENCE;

  if (nt_Is_Emoji_Modifier_Base (codepoint))
    return EMOJI_MODIFIER_BASE;
  if (nt_Is_Emoji_Modifier (codepoint))
    return EMOJI_MODIFIER;
  if (nt_Is_Regional_Indicator (codepoint))
    return REGIONAL_INDICATOR;
  if (nt_Is_Emoji_Keycap_Base (codepoint))
    return KEYCAP_BASE;
  if (nt_Is_Emoji_Emoji_Default (codepoint))
    return EMOJI_EMOJI_PRESENTATION;
  if (nt_Is_Emoji (codepoint))
    return EMOJI_TEXT_PRESENTATION;

  /* Ragel state machine will interpret unknown category as "any". */
  return kMaxEmojiScannerCategory;
}


typedef unsigned char *emoji_text_iter_t;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-default"
#include "nstext-emoji-scanner.c"
#pragma GCC diagnostic pop


NtEmojiIter *
nt_emoji_iter_init (NtEmojiIter *iter,
                        const char     *text,
                        int             length,
                        unsigned int    n_chars)
{
  unsigned char *types;
  unsigned int i;
  const char *p;
  const char *text_end;

  iter->text_start = iter->start = iter->end = text;
  if (length >= 0)
    text_end = text + length;
  else
    text_end = text + strlen (text);
  iter->text_end = text_end;
  iter->is_emoji = FALSE;
  iter->has_vs = FALSE;
  iter->n_chars = n_chars;

  /* Every sequence the scanner recognises needs a character above U+007F --
   * a pictograph, a regional indicator, a variation selector, a zero-width
   * joiner or the enclosing keycap. So ASCII is one non-emoji run by
   * construction, and saying so directly skips both the per-character
   * classification and the scanner over the text most pages are made of.
   */
  for (p = text; p < text_end; p++)
    if ((unsigned char) *p >= 0x80)
      break;

  if (p == text_end)
    {
      iter->types = iter->types_;
      iter->cursor = n_chars;
      iter->end = text_end;

      return iter;
    }

  if (n_chars < 64)
    types = iter->types_;
  else
    types = nt_malloc (n_chars);

  p = text;
  for (i = 0; i < n_chars; i++)
  {
    types[i] = nt_EmojiSegmentationCategory (nt_utf8_get_char (p));
    p = nt_utf8_next_char (p);
  }

  iter->types = types;
  iter->cursor = 0;

  nt_emoji_iter_next (iter);

  return iter;
}

void
nt_emoji_iter_fini (NtEmojiIter *iter)
{
  if (iter->types != iter->types_)
    free (iter->types);
}

int
nt_emoji_iter_next (NtEmojiIter *iter)
{
  unsigned int old_cursor, cursor;
  bool is_emoji;
  bool has_vs;

  if (iter->end >= iter->text_end)
    return FALSE;

  iter->start = iter->end;

  old_cursor = cursor = iter->cursor;
  cursor = scan_emoji_presentation (iter->types + cursor,
				    iter->types + iter->n_chars,
				    &is_emoji,
                                    &has_vs) - iter->types;
  do
  {
    iter->cursor = cursor;
    iter->is_emoji = is_emoji;
    iter->has_vs = has_vs;

    if (cursor == iter->n_chars)
      break;

    cursor = scan_emoji_presentation (iter->types + cursor,
				      iter->types + iter->n_chars,
				      &is_emoji,
                                      &has_vs) - iter->types;
  }
  while (iter->is_emoji == is_emoji &&
         iter->has_vs == has_vs);

  iter->end = nt_utf8_offset_to_pointer (iter->start, iter->cursor - old_cursor);

  return TRUE;
}


/**********************************************************
 * End of code from Chromium
 **********************************************************/
