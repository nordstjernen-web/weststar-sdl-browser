/* nstext — line, word, sentence and grapheme boundaries (UAX #14, #29).
 *
 * Pango's break.c and its script tailorings (break-arabic.c,
 * break-indic.c, break-thai.c, break-latin.c), ported off GLib: the
 * Unicode properties come from HarfBuzz (general category, script) and
 * from nstext-linebreak-table.h (Line_Break, generated from the UCD by
 * scripts/gen-linebreak-table.py); Thai dictionary breaks come from
 * libthai where it is installed, as in Pango.
 *
 * Copyright (C) 1999 Red Hat Software
 * Copyright 2026 Andreas Røsdal
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "nstext-private.h"
#include "nstext-config.h"
#include "nstext-break-table.h"

/* What the tailorings read of Pango's PangoAnalysis. */
typedef struct
{
  hb_script_t      script;
  NsPangoLanguage *language;
} NsPangoAnalysis;

#define NT_FALLTHROUGH __attribute__ ((fallthrough))


/* In Unicode 17, there are some test cases which contain
 *  the Unicode zero code point.
 */
/* #define ENABLE_UNICODE_ZERO_CODE_POINT_TEST_CASE 1 */

/* {{{ Unicode line breaking and segmentation */

#define PARAGRAPH_SEPARATOR 0x2029

/* See http://www.unicode.org/unicode/reports/tr14/ if you hope
 * to understand the line breaking code.
 */

typedef enum
{
  BREAK_ALREADY_HANDLED,   /* didn't use the table */
  BREAK_PROHIBITED, /* no break, even if spaces intervene */
  BREAK_IF_SPACES,  /* "indirect break" (only if there are spaces) */
  BREAK_ALLOWED     /* "direct break" (can always break here) */
  /* TR 14 has two more break-opportunity classes,
   * "indirect break opportunity for combining marks following a space"
   * and "prohibited break for combining marks"
   * but we handle that inline in the code.
   */
} BreakOpportunity;

#define FIX_BREAK_TYPE(bt, wc) do { } while (0)

/* need to sync the break range to glib/gunicode.h . */
#define LAST_BREAK_TYPE NT_LB_UNAMBIGUOUS_HYPHEN
#define BREAK_TYPE_SAFE(btype)            \
	 ((btype) <= LAST_BREAK_TYPE ? (btype) : NT_LB_UNKNOWN)


/*
 * Hangul Conjoining Jamo handling.
 *
 * The way we implement it is just a bit different from TR14,
 * but produces the same results.
 * The same algorithm is also used in TR29 for cluster boundaries.
 *
 */


/* An enum that works as the states of the Hangul syllables system.
 **/
typedef enum
{
  JAMO_L,	/* NT_LB_HANGUL_L_JAMO */
  JAMO_V,	/* NT_LB_HANGUL_V_JAMO */
  JAMO_T,	/* NT_LB_HANGUL_T_JAMO */
  JAMO_LV,	/* NT_LB_HANGUL_LV_SYLLABLE */
  JAMO_LVT,	/* NT_LB_HANGUL_LVT_SYLLABLE */
  NO_JAMO	/* Other */
} JamoType;

/* There are Hangul syllables encoded as characters, that act like a
 * sequence of Jamos. For each character we define a JamoType
 * that the character starts with, and one that it ends with.  This
 * decomposes JAMO_LV and JAMO_LVT to simple other JAMOs.  So for
 * example, a character with LineBreak type
 * NT_LB_HANGUL_LV_SYLLABLE has start=JAMO_L and end=JAMO_V.
 */
typedef struct _CharJamoProps
{
  JamoType start, end;
} CharJamoProps;

/* Map from JamoType to CharJamoProps that hold only simple
 * JamoTypes (no LV or LVT) or none.
 */
static const CharJamoProps HangulJamoProps[] = {
  {JAMO_L, JAMO_L},	/* JAMO_L */
  {JAMO_V, JAMO_V},	/* JAMO_V */
  {JAMO_T, JAMO_T},	/* JAMO_T */
  {JAMO_L, JAMO_V},	/* JAMO_LV */
  {JAMO_L, JAMO_T},	/* JAMO_LVT */
  {NO_JAMO, NO_JAMO}	/* NO_JAMO */
};

/* A character forms a syllable with the previous character if and only if:
 * JamoType(this) is not NO_JAMO and:
 *
 * HangulJamoProps[JamoType(prev)].end and
 * HangulJamoProps[JamoType(this)].start are equal,
 * or the former is one less than the latter.
 */

#define IS_JAMO(btype)              \
	((btype >= NT_LB_HANGUL_L_JAMO) && \
	 (btype <= NT_LB_HANGUL_LVT_SYLLABLE))
#define JAMO_TYPE(btype)      \
	(IS_JAMO(btype) ? (btype - NT_LB_HANGUL_L_JAMO) : NO_JAMO)

/* Types of Japanese characters */
#define JAPANESE(wc) ((wc) >= 0x2F00 && (wc) <= 0x30FF)
#define KANJI(wc)    ((wc) >= 0x2F00 && (wc) <= 0x2FDF)
#define HIRAGANA(wc) ((wc) >= 0x3040 && (wc) <= 0x309F)
#define KATAKANA(wc) ((wc) >= 0x30A0 && (wc) <= 0x30FF)

#define LATIN(wc) (((wc) >= 0x0020 && (wc) <= 0x02AF) || ((wc) >= 0x1E00 && (wc) <= 0x1EFF))
#define CYRILLIC(wc) (((wc) >= 0x0400 && (wc) <= 0x052F))
#define GREEK(wc) (((wc) >= 0x0370 && (wc) <= 0x3FF) || ((wc) >= 0x1F00 && (wc) <= 0x1FFF))
#define KANA(wc) ((wc) >= 0x3040 && (wc) <= 0x30FF)
#define HANGUL(wc) ((wc) >= 0xAC00 && (wc) <= 0xD7A3)
#define EMOJI(wc) (nt_is_emoji_base_character (wc))
#define MATH(wc) ((wc) >= 0x2200 && (wc) <= 0x22FF)
#define BACKSPACE_DELETES_CHARACTER(wc) (!LATIN (wc) && !CYRILLIC (wc) && !GREEK (wc) && !KANA (wc) && !HANGUL (wc) && !EMOJI (wc) && !MATH (wc))

/* Previously "123foo" was two words. But in UAX 29 of Unicode, 
 * we know don't break words between consecutive letters and numbers
 */
typedef enum
{
  WordNone,
  WordLetters,
  WordNumbers
} WordType;

static void
default_break (const char    *text,
               int            length,
               NsPangoAnalysis *analysis NT_UNUSED,
               NsPangoLogAttr  *attrs,
               int            attrs_len NT_UNUSED)
{
  /* The rationale for all this is in section 5.15 of the Unicode 3.0 book,
   * the line breaking stuff is also in TR14 on unicode.org
   */

  /* This is a default break implementation that should work for nearly all
   * languages. Language engines can override it optionally.
   */

  /* FIXME one cheesy optimization here would be to memset attrs to 0
   * before we start, and then never assign %FALSE to anything
   */

  const char *next;
  const char *next_next;
  int i;

  nt_unichar prev_wc;
  nt_unichar prev_prev_wc;
  nt_unichar next_wc;
  nt_unichar next_next_wc;

  JamoType prev_jamo;

  NtBreakType next_break_type;
  NtBreakType next_next_break_type;
  NtBreakType prev_break_type;
  NtBreakType prev_prev_break_type;

  hb_script_t prev_script;

  /* See Grapheme_Cluster_Break Property Values table of UAX#29 */
  typedef enum
  {
    GB_Other,
    GB_ControlCRLF,
    GB_Extend,
    GB_ZWJ,
    GB_Prepend,
    GB_SpacingMark,
    GB_InHangulSyllable, /* Handles all of L, V, T, LV, LVT rules */
    /* Use state machine to handle emoji sequence */
    /* Rule GB12 and GB13 */
    GB_RI_Odd, /* Meets odd number of RI */
    GB_RI_Even, /* Meets even number of RI */
  } GraphemeBreakType;
  GraphemeBreakType prev_GB_type = GB_Other;
  int met_Extended_Pictographic = FALSE;

  /* Rule GB9c */
  int met_InCB_Consonant = FALSE;
  int met_InCB_Consonant_Linker = FALSE;

  /* See Word_Break Property Values table of UAX#29 */
  typedef enum
  {
    WB_Other,
    WB_NewlineCRLF,
    WB_ExtendFormat,
    WB_Katakana,
    WB_Hebrew_Letter,
    WB_ALetter,
    WB_MidNumLet,
    WB_MidLetter,
    WB_MidNum,
    WB_Numeric,
    WB_ExtendNumLet,
    WB_RI_Odd,
    WB_RI_Even,
    WB_WSegSpace,
  } WordBreakType;
  WordBreakType prev_prev_WB_type = WB_Other, prev_WB_type = WB_Other;
  int prev_WB_i = -1;

  /* See Sentence_Break Property Values table of UAX#29 */
  typedef enum
  {
    SB_Other,
    SB_ExtendFormat,
    SB_ParaSep,
    SB_Sp,
    SB_Lower,
    SB_Upper,
    SB_OLetter,
    SB_Numeric,
    SB_ATerm,
    SB_SContinue,
    SB_STerm,
    SB_Close,
    /* Rules SB8 and SB8a */
    SB_ATerm_Close_Sp,
    SB_STerm_Close_Sp,
  } SentenceBreakType;
  SentenceBreakType prev_prev_SB_type = SB_Other, prev_SB_type = SB_Other;
  int prev_SB_i = -1;

  /* Rule LB25 with Example 7 of Customization */
  typedef enum
  {
    LB_Other,
    LB_Numeric,
    LB_Numeric_Close,
    LB_RI_Odd,
    LB_RI_Even,
    LB_Dotted_Circle,
    LB_Hyphen,
  } LineBreakType;
  LineBreakType prev_prev_LB_type NT_UNUSED = LB_Other;
  LineBreakType prev_LB_type = LB_Other;
  int met_LB15a = FALSE;
  int prev_LB_i = -1;
  int met_Ext_Pict_Unassigned = FALSE;

  WordType current_word_type = WordNone;
  nt_unichar last_word_letter = 0;
  nt_unichar base_character = 0;

  int last_sentence_start = -1;
  int last_non_space = -1;

  int prev_space_or_hyphen;

  int almost_done = FALSE;
  int done = FALSE;


  next = text;
  next_next = NULL;

  prev_break_type = NT_LB_UNKNOWN;
  prev_prev_break_type = NT_LB_UNKNOWN;
  prev_wc = 0;
  prev_prev_wc = 0;
  prev_script = HB_SCRIPT_COMMON;
  prev_jamo = NO_JAMO;
  prev_space_or_hyphen = FALSE;

#ifdef ENABLE_UNICODE_ZERO_CODE_POINT_TEST_CASE
  if (length == 0 || (length == -1 && *text == '\0'))
#else
  if (length == 0 || *text == '\0')
#endif
    {
      next_wc = PARAGRAPH_SEPARATOR;
      almost_done = TRUE;
    }
  else
    next_wc = nt_utf8_get_char (next);

  next_break_type = nt_unichar_break_type (next_wc);
  FIX_BREAK_TYPE (next_break_type, next_wc);
  next_break_type = BREAK_TYPE_SAFE (next_break_type);

  for (i = 0; !done ; i++)
    {
      hb_unicode_general_category_t type;
      nt_unichar wc;
      NtBreakType break_type;
      NtBreakType row_break_type;
      BreakOpportunity break_op;
      JamoType jamo;
      int makes_hangul_syllable;

      /* UAX#29 boundaries */
      int is_grapheme_boundary;
      int is_word_boundary;
      int is_sentence_boundary;

      /* Emoji extended pictographics */
      int is_Extended_Pictographic;

      hb_script_t script;

      wc = next_wc;
      break_type = next_break_type;

      if (almost_done)
	{
	  /*
	   * If we have already reached the end of @text nt_utf8_next_char()
	   * may not increment next
	   */
	  next_wc = 0;
	  next_break_type = NT_LB_UNKNOWN;
	  done = TRUE;
	}
      else
	{
	  next = nt_utf8_next_char (next);

#ifdef ENABLE_UNICODE_ZERO_CODE_POINT_TEST_CASE
	  if ((length >= 0 && next >= text + length) || (length == -1 && *next == '\0'))
#else
	  if ((length >= 0 && next >= text + length) || *next == '\0')
#endif
	    {
	      /* This is how we fill in the last element (end position) of the
	       * attr array - assume there's a paragraph separators off the end
	       * of @text.
	       */
	      next_wc = PARAGRAPH_SEPARATOR;
	      next_next_wc = PARAGRAPH_SEPARATOR;
	      almost_done = TRUE;
	    }
	  else
	    {
	      next_wc = nt_utf8_get_char (next);
	      next_next = nt_utf8_next_char (next);

#ifdef ENABLE_UNICODE_ZERO_CODE_POINT_TEST_CASE
	      if ((length >= 0 && next_next >= text + length) || (length == -1 && *next_next == '\0'))
#else
	      if ((length >= 0 && next_next >= text + length) || *next_next == '\0')
#endif
	        next_next_wc = PARAGRAPH_SEPARATOR;
	      else
	        next_next_wc = nt_utf8_get_char (next_next);
	    }

	  next_break_type = nt_unichar_break_type (next_wc);
	  FIX_BREAK_TYPE (next_break_type, next_wc);
	  next_break_type = BREAK_TYPE_SAFE (next_break_type);

	  next_next_break_type = nt_unichar_break_type (next_next_wc);
	  FIX_BREAK_TYPE (next_next_break_type, next_next_wc);
	  next_next_break_type = BREAK_TYPE_SAFE (next_next_break_type);
	}

      type = nt_unichar_type (wc);
      jamo = JAMO_TYPE (break_type);

      /* Determine wheter this forms a Hangul syllable with prev. */
      if (jamo == NO_JAMO)
	makes_hangul_syllable = FALSE;
      else
	{
	  JamoType prev_end   = HangulJamoProps[prev_jamo].end  ;
	  JamoType this_start = HangulJamoProps[     jamo].start;

	  /* See comments before IS_JAMO */
	  makes_hangul_syllable = (prev_end == this_start) || (prev_end + 1 == this_start);
	}

      switch ((int)type)
        {
        case HB_UNICODE_GENERAL_CATEGORY_SPACE_SEPARATOR:
        case HB_UNICODE_GENERAL_CATEGORY_LINE_SEPARATOR:
        case HB_UNICODE_GENERAL_CATEGORY_PARAGRAPH_SEPARATOR:
          attrs[i].is_white = TRUE;
          break;
        case HB_UNICODE_GENERAL_CATEGORY_CONTROL:
          if (wc == '\t' || wc == '\n' || wc == '\r' || wc == '\f')
            attrs[i].is_white = TRUE;
          else
            attrs[i].is_white = FALSE;
          break;
        default:
          attrs[i].is_white = FALSE;
          break;
        }

      /* Just few spaces have variable width. So explicitly mark them.
       */
      attrs[i].is_expandable_space = (0x0020 == wc || 0x00A0 == wc);
      is_Extended_Pictographic =
	nt_is_emoji_extended_pictographic (wc);


      /* ---- UAX#29 Grapheme Boundaries ---- */
      {
	GraphemeBreakType GB_type;

        /* Find the GraphemeBreakType of wc */
	GB_type = GB_Other;
	switch ((int)type)
	  {
	  case HB_UNICODE_GENERAL_CATEGORY_FORMAT:
	    if (wc == 0x200C)
	      {
		GB_type = GB_Extend;
		break;
	      }
	    if (wc == 0x200D)
	      {
		GB_type = GB_ZWJ;
		break;
	      }
            if (((wc >= 0x600 && wc <= 0x605) ||
                            wc == 0x6DD ||
                            wc == 0x70F ||
                            wc == 0x8E2 ||
                            wc == 0x110BD ||
                            wc == 0x110CD))
              {
                GB_type = GB_Prepend;
                break;
              }
            /* Tag chars */
            if (wc >= 0xE0020 && wc <= 0xE00FF)
              {
                GB_type = GB_Extend;
                break;
              }
            NT_FALLTHROUGH;
	  case HB_UNICODE_GENERAL_CATEGORY_CONTROL:
	  case HB_UNICODE_GENERAL_CATEGORY_LINE_SEPARATOR:
	  case HB_UNICODE_GENERAL_CATEGORY_PARAGRAPH_SEPARATOR:
	  case HB_UNICODE_GENERAL_CATEGORY_SURROGATE:
	    GB_type = GB_ControlCRLF;
	    break;

	  case HB_UNICODE_GENERAL_CATEGORY_UNASSIGNED:
	    /* Unassigned default ignorables */
	    if ((wc >= 0xFFF0 && wc <= 0xFFF8) ||
		(wc >= 0xE0000 && wc <= 0xE0FFF))
	      {
		GB_type = GB_ControlCRLF;
		break;
	      }
            NT_FALLTHROUGH;

	  case HB_UNICODE_GENERAL_CATEGORY_OTHER_LETTER:
	    if (makes_hangul_syllable)
	      GB_type = GB_InHangulSyllable;

	    if (nt_is_Consonant_Preceding_Repha (wc) ||
		nt_is_Consonant_Prefixed (wc))
	      GB_type = GB_Prepend;
	    break;

	  case HB_UNICODE_GENERAL_CATEGORY_MODIFIER_LETTER:
	    if (wc >= 0xFF9E && wc <= 0xFF9F)
	      GB_type = GB_Extend; /* Other_Grapheme_Extend */
	    break;

	  case HB_UNICODE_GENERAL_CATEGORY_SPACING_MARK:
	    if (!(wc == 0x102B || wc == 0x102C || wc == 0x1038||
		  (wc >= 0x1062 && wc <= 0x1064) ||
		  (wc >= 0x1067 && wc <= 0x106D) ||
		  wc == 0x1083 || (wc >= 0x1087 && wc <= 0x108C) ||
		  wc == 0x108F || (wc >= 0x109A && wc <= 0x109C) ||
		  wc == 0x1A61 || wc == 0x1A63 || wc == 0x1A64 ||
		  wc == 0xAA7B || wc == 0xAA7D ||
		  wc == 0x11720 || wc == 0x11721))
	      GB_type = GB_SpacingMark; /* SpacingMark */
	    if (wc >= 0x0900)
	      {
		if (wc == 0x09BE || wc == 0x09D7 ||
		    wc == 0x0B3E || wc == 0x0B57 || wc == 0x0BBE || wc == 0x0BD7 ||
		    wc == 0x0CC2 || wc == 0x0CD5 || wc == 0x0CD6 ||
		    wc == 0x0D3E || wc == 0x0D57 || wc == 0x0DCF || wc == 0x0DDF ||
		    wc == 0x1D165 || (wc >= 0x1D16E && wc <= 0x1D172))
		  GB_type = GB_Extend; /* Other_Grapheme_Extend */
	      }
	    break;

	  case HB_UNICODE_GENERAL_CATEGORY_ENCLOSING_MARK:
	  case HB_UNICODE_GENERAL_CATEGORY_NON_SPACING_MARK:
	    GB_type = GB_Extend; /* Grapheme_Extend */
	    break;

          case HB_UNICODE_GENERAL_CATEGORY_OTHER_SYMBOL:
            if ((wc >=0x1F1E6 && wc <=0x1F1FF))
              {
                if (prev_GB_type == GB_RI_Odd)
                  GB_type = GB_RI_Even;
                else
                  GB_type = GB_RI_Odd;
                break;
              }
            break;

          case HB_UNICODE_GENERAL_CATEGORY_MODIFIER_SYMBOL:
            /* Fitzpatrick modifiers */
            if (wc >= 0x1F3FB && wc <= 0x1F3FF)
              GB_type = GB_Extend;
            break;

          default:
            break;
	  }

	/* Rule GB11 */
	if (met_Extended_Pictographic)
	  {
	    if (GB_type == GB_Extend)
	      met_Extended_Pictographic = TRUE;
	    else if (nt_is_emoji_extended_pictographic (prev_wc) &&
		     GB_type == GB_ZWJ)
	      met_Extended_Pictographic = TRUE;
	    else if (prev_GB_type == GB_Extend && GB_type == GB_ZWJ)
	      met_Extended_Pictographic = TRUE;
	    else if (prev_GB_type == GB_ZWJ && is_Extended_Pictographic)
	      met_Extended_Pictographic = TRUE;
	    else
	      met_Extended_Pictographic = FALSE;
	  }

	/* Grapheme Cluster Boundary Rules */
	is_grapheme_boundary = TRUE; /* Rule GB999 */

	if (prev_GB_type == GB_RI_Odd && GB_type == GB_RI_Even)
	  is_grapheme_boundary = FALSE; /* Rule GB12 and GB13 */

	if (is_Extended_Pictographic)
	  { /* Rule GB11 */
	    if (prev_GB_type == GB_ZWJ && met_Extended_Pictographic)
	      is_grapheme_boundary = FALSE;
	  }

	if (met_InCB_Consonant || met_InCB_Consonant_Linker)
	  { /* Rule GB9c */
	    if (met_InCB_Consonant_Linker &&
		nt_is_Indic_Conjunct_Break_Consonant(wc))
	      {
	        met_InCB_Consonant = TRUE;
		met_InCB_Consonant_Linker = FALSE;
		is_grapheme_boundary = FALSE;
	      }

	    if (!(nt_is_Indic_Conjunct_Break_Extend(wc) ||
		  nt_is_Indic_Conjunct_Break_Linker(wc)))
	      {
	        met_InCB_Consonant = FALSE;
		met_InCB_Consonant_Linker = FALSE;
	      }
	  }

	if (prev_GB_type == GB_Prepend)
	  is_grapheme_boundary = FALSE; /* Rule GB9b */

	if (GB_type == GB_SpacingMark)
	  is_grapheme_boundary = FALSE; /* Rule GB9a */

	if (GB_type == GB_Extend || GB_type == GB_ZWJ)
	  is_grapheme_boundary = FALSE; /* Rule GB9 */

	if (GB_type == GB_InHangulSyllable)
	  is_grapheme_boundary = FALSE; /* Rules GB6, GB7, GB8 */

	if (prev_GB_type == GB_ControlCRLF || GB_type == GB_ControlCRLF)
	  is_grapheme_boundary = TRUE; /* Rules GB4 and GB5 */

	if (wc == '\n' && prev_wc == '\r')
          is_grapheme_boundary = FALSE; /* Rule GB3 */

	/* We apply Rules GB1 and GB2 at the end of the function */

	if (is_Extended_Pictographic)
	  met_Extended_Pictographic = TRUE;

	if (met_InCB_Consonant && !met_InCB_Consonant_Linker &&
	    nt_is_Indic_Conjunct_Break_Linker(wc))
	      met_InCB_Consonant_Linker = TRUE; /* Rule GB9c */

	if (!met_InCB_Consonant && !met_InCB_Consonant_Linker &&
	    nt_is_Indic_Conjunct_Break_Consonant(wc))
	      met_InCB_Consonant = TRUE; /* Rule GB9c */

	attrs[i].is_cursor_position = is_grapheme_boundary;
	/* If this is a grapheme boundary, we have to decide if backspace
	 * deletes a character or the whole grapheme cluster */
	if (is_grapheme_boundary)
          {
	    attrs[i].backspace_deletes_character = BACKSPACE_DELETES_CHARACTER (base_character);

	    /* Dependent Vowels for Indic language */
	    if (nt_is_Virama (prev_wc) ||
		nt_is_Vowel_Dependent (prev_wc))
	      attrs[i].backspace_deletes_character = TRUE;
          }
	else
	  attrs[i].backspace_deletes_character = FALSE;

	prev_GB_type = GB_type;
      }

      script = (hb_script_t)nt_unichar_script (wc);
      /* ---- UAX#29 Word Boundaries ---- */
      {
	is_word_boundary = FALSE;
	if (is_grapheme_boundary ||
	    (wc >=0x1F1E6 && wc <=0x1F1FF)) /* Rules WB3 and WB4 */
	  {
	    WordBreakType WB_type;

	    /* Find the WordBreakType of wc */
	    WB_type = WB_Other;

	    if (script == HB_SCRIPT_KATAKANA)
	      WB_type = WB_Katakana;

	    if (script == HB_SCRIPT_HEBREW && type == HB_UNICODE_GENERAL_CATEGORY_OTHER_LETTER)
	      WB_type = WB_Hebrew_Letter;

	    if (WB_type == WB_Other)
	      switch (wc >> 8)
	        {
		case 0x00:
		  if (wc == 0x00b8)
		    WB_type = WB_ALetter; /* For Unicode 17.0.0 */
		  break;
		case 0x30:
		  if (wc == 0x3031 || wc == 0x3032 || wc == 0x3033 || wc == 0x3034 || wc == 0x3035 ||
		      wc == 0x309b || wc == 0x309c || wc == 0x30a0 || wc == 0x30fc)
		    WB_type = WB_Katakana; /* Katakana exceptions */
		  break;
		case 0xFF:
		  if (wc == 0xFF70)
		    WB_type = WB_Katakana; /* Katakana exceptions */
		  else if (wc >= 0xFF9E && wc <= 0xFF9F)
		    WB_type = WB_ExtendFormat; /* Other_Grapheme_Extend */
		  break;
		case 0x05:
		  if (wc == 0x058A)
		    WB_type = WB_ALetter; /* ALetter exceptions */
		  break;
		case 0x07:
		  if (wc == 0x070F)
		    WB_type = WB_ALetter; /* ALetter exceptions */
		  break;
                default:
                  break;
		}

	    if (WB_type == WB_Other)
	      switch ((int) break_type)
	        {
		case NT_LB_NUMERIC:
		  if (wc != 0x066C)
		    WB_type = WB_Numeric; /* Numeric */
		  break;
		case NT_LB_INFIX_SEPARATOR:
		  if (wc != 0x003A && wc != 0xFE13 && wc != 0x002E)
		    WB_type = WB_MidNum; /* MidNum */
		  break;
                default:
                  break;
		}

	    if (WB_type == WB_Other)
	      switch ((int) type)
		{
		case HB_UNICODE_GENERAL_CATEGORY_CONTROL:
		  if (wc != 0x000D && wc != 0x000A && wc != 0x000B && wc != 0x000C && wc != 0x0085)
		    break;
                  NT_FALLTHROUGH;
		case HB_UNICODE_GENERAL_CATEGORY_LINE_SEPARATOR:
		case HB_UNICODE_GENERAL_CATEGORY_PARAGRAPH_SEPARATOR:
		  WB_type = WB_NewlineCRLF; /* CR, LF, Newline */
		  break;

		case HB_UNICODE_GENERAL_CATEGORY_FORMAT:
		case HB_UNICODE_GENERAL_CATEGORY_SPACING_MARK:
		case HB_UNICODE_GENERAL_CATEGORY_ENCLOSING_MARK:
		case HB_UNICODE_GENERAL_CATEGORY_NON_SPACING_MARK:
		  WB_type = WB_ExtendFormat; /* Extend, Format */
		  break;

		case HB_UNICODE_GENERAL_CATEGORY_CONNECT_PUNCTUATION:
		  WB_type = WB_ExtendNumLet; /* ExtendNumLet */
		  break;

		case HB_UNICODE_GENERAL_CATEGORY_INITIAL_PUNCTUATION:
		case HB_UNICODE_GENERAL_CATEGORY_FINAL_PUNCTUATION:
		  if (wc == 0x2018 || wc == 0x2019)
		    WB_type = WB_MidNumLet; /* MidNumLet */
		  break;
		case HB_UNICODE_GENERAL_CATEGORY_OTHER_PUNCTUATION:
		  if ((wc >= 0x055a && wc <= 0x055c) ||
		      wc == 0x055e || wc == 0x05f3)
		    WB_type = WB_ALetter; /* ALetter */
		  else if (wc == 0x0027 || wc == 0x002e || wc == 0x2024 ||
		      wc == 0xfe52 || wc == 0xff07 || wc == 0xff0e)
		    WB_type = WB_MidNumLet; /* MidNumLet */
		  else if (wc == 0x00b7 || wc == 0x05f4 || wc == 0x2027 ||
			   wc == 0x003a || wc == 0x0387 || wc == 0x055f ||
			   wc == 0xfe13 || wc == 0xfe55 || wc == 0xff1a)
		    WB_type = WB_MidLetter; /* MidLetter */
		  else if (wc == 0x066c ||
			   wc == 0xfe50 || wc == 0xfe54 || wc == 0xff0c || wc == 0xff1b)
		    WB_type = WB_MidNum; /* MidNum */
		  break;

		case HB_UNICODE_GENERAL_CATEGORY_OTHER_SYMBOL:
		  if (wc >= 0x24B6 && wc <= 0x24E9) /* Other_Alphabetic */
		    goto Alphabetic;

		  if ((wc >= 0x1F1E6 && wc <= 0x1F1FF))
		    {
                      if (prev_WB_type == WB_RI_Odd)
                        WB_type = WB_RI_Even;
                      else
                        WB_type = WB_RI_Odd;
		    }

		  break;

		case HB_UNICODE_GENERAL_CATEGORY_OTHER_LETTER:
		case HB_UNICODE_GENERAL_CATEGORY_LETTER_NUMBER:
		  if (wc == 0x3006 || wc == 0x3007 ||
		      (wc >= 0x3021 && wc <= 0x3029) ||
		      (wc >= 0x3038 && wc <= 0x303A) ||
		      (wc >= 0x3400 && wc <= 0x4DB5) ||
		      (wc >= 0x4E00 && wc <= 0x9FC3) ||
		      (wc >= 0xF900 && wc <= 0xFA2D) ||
		      (wc >= 0xFA30 && wc <= 0xFA6A) ||
		      (wc >= 0xFA70 && wc <= 0xFAD9) ||
		      (wc >= 0x20000 && wc <= 0x2A6D6) ||
		      (wc >= 0x2F800 && wc <= 0x2FA1D))
		    break; /* ALetter exceptions: Ideographic */
		  goto Alphabetic;

		case HB_UNICODE_GENERAL_CATEGORY_LOWERCASE_LETTER:
		case HB_UNICODE_GENERAL_CATEGORY_MODIFIER_LETTER:
		case HB_UNICODE_GENERAL_CATEGORY_TITLECASE_LETTER:
		case HB_UNICODE_GENERAL_CATEGORY_UPPERCASE_LETTER:
		Alphabetic:
		  if (break_type != NT_LB_COMPLEX_CONTEXT && script != HB_SCRIPT_HIRAGANA)
		    WB_type = WB_ALetter; /* ALetter */
		  break;
                default:
                  break;
		}

	    if (WB_type == WB_Other)
	      {
		if (type == HB_UNICODE_GENERAL_CATEGORY_SPACE_SEPARATOR &&
		    break_type != NT_LB_NON_BREAKING_GLUE)
		  WB_type = WB_WSegSpace;
	      }

	    /* Word Cluster Boundary Rules */

	    /* We apply Rules WB1 and WB2 at the end of the function */

	    if (prev_WB_type == WB_NewlineCRLF && prev_WB_i + 1 == i)
	      {
	        /* The extra check for prev_WB_i is to correctly handle sequences like
		 * Newline ÷ Extend × Extend
		 * since we have not skipped ExtendFormat yet.
		 */
	        is_word_boundary = TRUE; /* Rule WB3a */
	      }
	    else if (WB_type == WB_NewlineCRLF)
	      is_word_boundary = TRUE; /* Rule WB3b */
	    else if (prev_wc == 0x200D && is_Extended_Pictographic)
	      is_word_boundary = FALSE; /* Rule WB3c */
	    else if (prev_WB_type == WB_WSegSpace &&
		     WB_type == WB_WSegSpace && prev_WB_i + 1 == i)
	      is_word_boundary = FALSE; /* Rule WB3d */
	    else if (WB_type == WB_ExtendFormat)
	      is_word_boundary = FALSE; /* Rules WB4? */
	    else if ((prev_WB_type == WB_ALetter  ||
                  prev_WB_type == WB_Hebrew_Letter ||
                  prev_WB_type == WB_Numeric) &&
                 (WB_type == WB_ALetter  ||
                  WB_type == WB_Hebrew_Letter ||
                  WB_type == WB_Numeric))
	      is_word_boundary = FALSE; /* Rules WB5, WB8, WB9, WB10 */
	    else if (prev_WB_type == WB_Katakana && WB_type == WB_Katakana)
	      is_word_boundary = FALSE; /* Rule WB13 */
	    else if ((prev_WB_type == WB_ALetter ||
                  prev_WB_type == WB_Hebrew_Letter ||
                  prev_WB_type == WB_Numeric ||
                  prev_WB_type == WB_Katakana ||
                  prev_WB_type == WB_ExtendNumLet) &&
                 WB_type == WB_ExtendNumLet)
	      is_word_boundary = FALSE; /* Rule WB13a */
	    else if (prev_WB_type == WB_ExtendNumLet &&
                 (WB_type == WB_ALetter ||
                  WB_type == WB_Hebrew_Letter ||
                  WB_type == WB_Numeric ||
                  WB_type == WB_Katakana))
	      is_word_boundary = FALSE; /* Rule WB13b */
	    else if (((prev_prev_WB_type == WB_ALetter ||
                   prev_prev_WB_type == WB_Hebrew_Letter) &&
                  (WB_type == WB_ALetter ||
                   WB_type == WB_Hebrew_Letter)) &&
		     (prev_WB_type == WB_MidLetter ||
              prev_WB_type == WB_MidNumLet ||
              prev_wc == 0x0027))
	      {
		attrs[prev_WB_i].is_word_boundary = FALSE; /* Rule WB6 */
		is_word_boundary = FALSE; /* Rule WB7 */
	      }
	    else if (prev_WB_type == WB_Hebrew_Letter && wc == 0x0027)
          is_word_boundary = FALSE; /* Rule WB7a */
	    else if (prev_prev_WB_type == WB_Hebrew_Letter && prev_wc == 0x0022 &&
                 WB_type == WB_Hebrew_Letter) {
          attrs[prev_WB_i].is_word_boundary = FALSE; /* Rule WB7b */
          is_word_boundary = FALSE; /* Rule WB7c */
        }
	    else if ((prev_prev_WB_type == WB_Numeric && WB_type == WB_Numeric) &&
                 (prev_WB_type == WB_MidNum || prev_WB_type == WB_MidNumLet ||
                  prev_wc == 0x0027))
	      {
		is_word_boundary = FALSE; /* Rule WB11 */
		attrs[prev_WB_i].is_word_boundary = FALSE; /* Rule WB12 */
	      }
	    else if (prev_WB_type == WB_RI_Odd && WB_type == WB_RI_Even)
	      is_word_boundary = FALSE; /* Rule WB15 and WB16 */
	    else
	      is_word_boundary = TRUE; /* Rule WB999 */

	    if (WB_type != WB_ExtendFormat)
	      {
		prev_prev_WB_type = prev_WB_type;
		prev_WB_type = WB_type;
		prev_WB_i = i;
	      }
	  }

	attrs[i].is_word_boundary = is_word_boundary;
      }

      /* ---- UAX#29 Sentence Boundaries ---- */
      {
	is_sentence_boundary = FALSE;
	if (is_word_boundary ||
	    wc == '\r' || wc == '\n') /* Rules SB3 and SB5 */
	  {
	    SentenceBreakType SB_type;

	    /* Find the SentenceBreakType of wc */
	    SB_type = SB_Other;

	    if (break_type == NT_LB_NUMERIC)
	      SB_type = SB_Numeric; /* Numeric */

	    if (SB_type == SB_Other)
	      switch ((int) type)
		{
		case HB_UNICODE_GENERAL_CATEGORY_CONTROL:
		  if (wc == '\r' || wc == '\n')
		    SB_type = SB_ParaSep;
		  else if (wc == 0x0009 || wc == 0x000B || wc == 0x000C)
		    SB_type = SB_Sp;
		  else if (wc == 0x0085)
		    SB_type = SB_ParaSep;
		  break;

		case HB_UNICODE_GENERAL_CATEGORY_SPACE_SEPARATOR:
		  if (wc == 0x0020 || wc == 0x00A0 || wc == 0x1680 ||
		      (wc >= 0x2000 && wc <= 0x200A) ||
		      wc == 0x202F || wc == 0x205F || wc == 0x3000)
		    SB_type = SB_Sp;
		  break;

		case HB_UNICODE_GENERAL_CATEGORY_LINE_SEPARATOR:
		case HB_UNICODE_GENERAL_CATEGORY_PARAGRAPH_SEPARATOR:
		  SB_type = SB_ParaSep;
		  break;

		case HB_UNICODE_GENERAL_CATEGORY_FORMAT:
		case HB_UNICODE_GENERAL_CATEGORY_SPACING_MARK:
		case HB_UNICODE_GENERAL_CATEGORY_ENCLOSING_MARK:
		case HB_UNICODE_GENERAL_CATEGORY_NON_SPACING_MARK:
		  SB_type = SB_ExtendFormat; /* Extend, Format */
		  break;

		case HB_UNICODE_GENERAL_CATEGORY_MODIFIER_LETTER:
		  if (wc >= 0xFF9E && wc <= 0xFF9F)
		    SB_type = SB_ExtendFormat; /* Other_Grapheme_Extend */
		  break;

		case HB_UNICODE_GENERAL_CATEGORY_TITLECASE_LETTER:
		  SB_type = SB_Upper;
		  break;

		case HB_UNICODE_GENERAL_CATEGORY_DASH_PUNCTUATION:
		  if (wc == 0x002D ||
		      wc == 0x003B ||
		      wc == 0x037E ||
		      (wc >= 0x2013 && wc <= 0x2014) ||
		      wc == 0xFE14 ||
		      (wc >= 0xFE31 && wc <= 0xFE32) ||
		      wc == 0xFE54 ||
		      wc == 0xFE58 ||
		      wc == 0xFE63 ||
		      wc == 0xFF0D ||
		      wc == 0xFF1A ||
		      wc == 0xFF1B ||
		      wc == 0xFF64)
		    SB_type = SB_SContinue;
		  break;

		case HB_UNICODE_GENERAL_CATEGORY_OTHER_PUNCTUATION:
		  if (wc == 0x05F3)
		    SB_type = SB_OLetter;
		  else if (wc == 0x002E || wc == 0x2024 ||
		      wc == 0xFE52 || wc == 0xFF0E)
		    SB_type = SB_ATerm;

		  if (wc == 0x002C ||
		      wc == 0x003A ||
		      wc == 0x055D ||
		      (wc >= 0x060C && wc <= 0x060D) ||
		      wc == 0x07F8 ||
		      wc == 0x1802 ||
		      wc == 0x1808 ||
		      wc == 0x3001 ||
		      (wc >= 0xFE10 && wc <= 0xFE11) ||
		      wc == 0xFE13 ||
		      (wc >= 0xFE50 && wc <= 0xFE51) ||
		      wc == 0xFE55 ||
		      wc == 0xFF0C ||
		      wc == 0xFF1A ||
		      wc == 0xFF64)
		    SB_type = SB_SContinue;

		  if (nt_is_STerm(wc))
		    SB_type = SB_STerm;

		  break;

                default:
                  break;
		}

	    if (SB_type == SB_Other)
	      {
                if (type == HB_UNICODE_GENERAL_CATEGORY_LOWERCASE_LETTER)
		  SB_type = SB_Lower;
                else if (type == HB_UNICODE_GENERAL_CATEGORY_UPPERCASE_LETTER)
		  SB_type = SB_Upper;
                else if (type == HB_UNICODE_GENERAL_CATEGORY_TITLECASE_LETTER ||
                         type == HB_UNICODE_GENERAL_CATEGORY_MODIFIER_LETTER ||
                         type == HB_UNICODE_GENERAL_CATEGORY_OTHER_LETTER)
		  SB_type = SB_OLetter;

		if (type == HB_UNICODE_GENERAL_CATEGORY_OPEN_PUNCTUATION ||
		    type == HB_UNICODE_GENERAL_CATEGORY_CLOSE_PUNCTUATION ||
		    break_type == NT_LB_QUOTATION)
		  SB_type = SB_Close;
	      }

	    /* Sentence Boundary Rules */

	    /* We apply Rules SB1 and SB2 at the end of the function */

#define IS_OTHER_TERM(SB_type)						\
	    /* not in (OLetter | Upper | Lower | ParaSep | SATerm) */	\
	      !(SB_type == SB_OLetter ||				\
		SB_type == SB_Upper || SB_type == SB_Lower ||		\
		SB_type == SB_ParaSep ||				\
		SB_type == SB_ATerm || SB_type == SB_STerm ||		\
		SB_type == SB_ATerm_Close_Sp ||				\
		SB_type == SB_STerm_Close_Sp)


	    if (wc == '\n' && prev_wc == '\r')
	      is_sentence_boundary = FALSE; /* Rule SB3 */
	    else if (prev_SB_type == SB_ParaSep && prev_SB_i + 1 == i)
	      {
		/* The extra check for prev_SB_i is to correctly handle sequences like
		 * ParaSep ÷ Extend × Extend
		 * since we have not skipped ExtendFormat yet.
		 */

		is_sentence_boundary = TRUE; /* Rule SB4 */
	      }
	    else if (SB_type == SB_ExtendFormat)
	      is_sentence_boundary = FALSE; /* Rule SB5? */
	    else if (prev_SB_type == SB_ATerm && SB_type == SB_Numeric)
	      is_sentence_boundary = FALSE; /* Rule SB6 */
	    else if ((prev_prev_SB_type == SB_Upper ||
		      prev_prev_SB_type == SB_Lower) &&
		     prev_SB_type == SB_ATerm &&
		     SB_type == SB_Upper)
	      is_sentence_boundary = FALSE; /* Rule SB7 */
	    else if (prev_SB_type == SB_ATerm && SB_type == SB_Close)
		SB_type = SB_ATerm;
	    else if (prev_SB_type == SB_STerm && SB_type == SB_Close)
	      SB_type = SB_STerm;
	    else if (prev_SB_type == SB_ATerm && SB_type == SB_Sp)
	      SB_type = SB_ATerm_Close_Sp;
	    else if (prev_SB_type == SB_STerm && SB_type == SB_Sp)
	      SB_type = SB_STerm_Close_Sp;
	    /* Rule SB8 */
	    else if ((prev_SB_type == SB_ATerm ||
		      prev_SB_type == SB_ATerm_Close_Sp) &&
		     SB_type == SB_Lower)
	      is_sentence_boundary = FALSE;
	    else if ((prev_prev_SB_type == SB_ATerm ||
		      prev_prev_SB_type == SB_ATerm_Close_Sp) &&
		     IS_OTHER_TERM(prev_SB_type) &&
		     SB_type == SB_Lower)
              {
	        attrs[prev_SB_i].is_sentence_boundary = FALSE;
	        attrs[prev_SB_i].is_sentence_end = FALSE;
                last_sentence_start = -1;
                for (int j = prev_SB_i - 1; j >= 0; j--)
                  {
                    attrs[j].is_sentence_end = FALSE;
                    if (attrs[j].is_sentence_boundary)
                      {
                        last_sentence_start = j;
                        break;
                      }
                  }
              }
	    else if ((prev_SB_type == SB_ATerm ||
		      prev_SB_type == SB_ATerm_Close_Sp ||
		      prev_SB_type == SB_STerm ||
		      prev_SB_type == SB_STerm_Close_Sp) &&
		     (SB_type == SB_SContinue ||
		      SB_type == SB_ATerm || SB_type == SB_STerm))
	      is_sentence_boundary = FALSE; /* Rule SB8a */
	    else if ((prev_SB_type == SB_ATerm ||
		      prev_SB_type == SB_STerm) &&
		     (SB_type == SB_Close || SB_type == SB_Sp ||
		      SB_type == SB_ParaSep))
	      is_sentence_boundary = FALSE; /* Rule SB9 */
	    else if ((prev_SB_type == SB_ATerm ||
		      prev_SB_type == SB_ATerm_Close_Sp ||
		      prev_SB_type == SB_STerm ||
		      prev_SB_type == SB_STerm_Close_Sp) &&
		     (SB_type == SB_Sp || SB_type == SB_ParaSep))
	      is_sentence_boundary = FALSE; /* Rule SB10 */
	    else if ((prev_SB_type == SB_ATerm ||
		      prev_SB_type == SB_ATerm_Close_Sp ||
		      prev_SB_type == SB_STerm ||
		      prev_SB_type == SB_STerm_Close_Sp) &&
		     SB_type != SB_ParaSep)
	      is_sentence_boundary = TRUE; /* Rule SB11 */
	    else
	      is_sentence_boundary = FALSE; /* Rule SB998 */

	    if (SB_type != SB_ExtendFormat &&
		!((prev_prev_SB_type == SB_ATerm ||
		   prev_prev_SB_type == SB_ATerm_Close_Sp) &&
		  IS_OTHER_TERM(prev_SB_type) &&
		  IS_OTHER_TERM(SB_type)))
              {
                prev_prev_SB_type = prev_SB_type;
                prev_SB_type = SB_type;
                prev_SB_i = i;
              }

#undef IS_OTHER_TERM

	  }

	if (i == 0 || done)
	  is_sentence_boundary = TRUE; /* Rules SB1 and SB2 */

	attrs[i].is_sentence_boundary = is_sentence_boundary;
      }

      /* ---- Line breaking ---- */

      break_op = BREAK_ALREADY_HANDLED;

      row_break_type = prev_break_type == NT_LB_SPACE ?
	prev_prev_break_type : prev_break_type;

      attrs[i].is_char_break = FALSE;
      attrs[i].is_line_break = FALSE;
      attrs[i].is_mandatory_break = FALSE;

      /* Rule LB1:
	 assign a line breaking class to each code point of the input. */
      switch ((int)break_type)
	{
	case NT_LB_AMBIGUOUS:
	case NT_LB_SURROGATE:
	case NT_LB_UNKNOWN:
	  break_type = NT_LB_ALPHABETIC;
	  break;

	case NT_LB_COMPLEX_CONTEXT:
	  if (type == HB_UNICODE_GENERAL_CATEGORY_NON_SPACING_MARK ||
	      type == HB_UNICODE_GENERAL_CATEGORY_SPACING_MARK)
	    break_type = NT_LB_COMBINING_MARK;
	  else
	    break_type = NT_LB_ALPHABETIC;
	  break;

	case NT_LB_CONDITIONAL_JAPANESE_STARTER:
	  break_type = NT_LB_NON_STARTER;
	  break;

	default:
          break;
	}

      /* If it's not a grapheme boundary, it's not a line break either */
      if (attrs[i].is_cursor_position ||
	  break_type == NT_LB_COMBINING_MARK ||
	  break_type == NT_LB_ZERO_WIDTH_JOINER ||
	  break_type == NT_LB_NON_BREAKING_GLUE ||
	  break_type == NT_LB_HANGUL_L_JAMO ||
	  break_type == NT_LB_HANGUL_V_JAMO ||
	  break_type == NT_LB_HANGUL_T_JAMO ||
	  break_type == NT_LB_HANGUL_LV_SYLLABLE ||
	  break_type == NT_LB_HANGUL_LVT_SYLLABLE ||
	  break_type == NT_LB_EMOJI_MODIFIER ||
	  break_type == NT_LB_REGIONAL_INDICATOR ||
	  break_type == NT_LB_VIRAMA ||
	  break_type == NT_LB_VIRAMA_FINAL ||
	  FALSE)
	{
	  LineBreakType LB_type;

	  /* Find the LineBreakType of wc */
	  LB_type = LB_Other;

	  /* Rule LB20a */
	  if (wc == 0x2010)
	    LB_type = LB_Hyphen;

	  /* Rule LB28a */
	  if (wc == 0x25CC)
	    LB_type = LB_Dotted_Circle;

	  if (break_type == NT_LB_NUMERIC)
	    LB_type = LB_Numeric;

	  if (break_type == NT_LB_SYMBOL ||
	      break_type == NT_LB_INFIX_SEPARATOR)
	    {
	      if (!(prev_LB_type == LB_Numeric))
		LB_type = LB_Other;
	    }

	  if (break_type == NT_LB_CLOSE_PUNCTUATION ||
	      break_type == NT_LB_CLOSE_PARENTHESIS)
	    {
	      if (prev_LB_type == LB_Numeric)
		LB_type = LB_Numeric_Close;
	      else
		LB_type = LB_Other;
	    }

	  if (break_type == NT_LB_REGIONAL_INDICATOR)
	    {
	      if (prev_LB_type == LB_RI_Odd)
		LB_type = LB_RI_Even;
	      else
		LB_type = LB_RI_Odd;
	    }

	  attrs[i].is_line_break = TRUE; /* Rule LB31 */
	  /* Unicode doesn't specify char wrap;
	     we wrap around all chars currently. */
	  if (attrs[i].is_cursor_position)
	    attrs[i].is_char_break = TRUE;

	  /* Make any necessary replacements first */
	  if (row_break_type == NT_LB_UNKNOWN)
	    row_break_type = NT_LB_ALPHABETIC;

	  /* add the line break rules in reverse order to override
	     the lower priority rules. */

	  /* Rule LB30b */
	  if (prev_break_type == NT_LB_EMOJI_BASE &&
	      break_type == NT_LB_EMOJI_MODIFIER)
	    break_op = BREAK_PROHIBITED;

	  if (met_Ext_Pict_Unassigned &&
	      break_type == NT_LB_EMOJI_MODIFIER)
	    {
	      met_Ext_Pict_Unassigned = FALSE;
	      break_op = BREAK_PROHIBITED;
	    }

	  /* Rule LB30a */
	  if (prev_LB_type == LB_RI_Odd && LB_type == LB_RI_Even)
	    break_op = BREAK_PROHIBITED;

	  /* Rule LB30 */
	  if ((prev_break_type == NT_LB_ALPHABETIC ||
	       prev_break_type == NT_LB_HEBREW_LETTER ||
	       prev_break_type == NT_LB_NUMERIC) &&
	      break_type == NT_LB_OPEN_PUNCTUATION &&
	      !nt_is_EastAsianWide (wc))
	    break_op = BREAK_PROHIBITED;

	  if (prev_break_type == NT_LB_CLOSE_PARENTHESIS &&
	      !nt_is_EastAsianWide (prev_wc)&&
	      (break_type == NT_LB_ALPHABETIC ||
	       break_type == NT_LB_HEBREW_LETTER ||
	       break_type == NT_LB_NUMERIC))
	    break_op = BREAK_PROHIBITED;

	  /* Rule LB29 */
	  if (prev_break_type == NT_LB_INFIX_SEPARATOR &&
	      (break_type == NT_LB_ALPHABETIC ||
	       break_type == NT_LB_HEBREW_LETTER))
	    break_op = BREAK_PROHIBITED;

	  /* Rule LB28a */
	  if (prev_break_type == NT_LB_AKSARA_PRE_BASE &&
	      (break_type == NT_LB_AKSARA ||
	       LB_type == LB_Dotted_Circle ||
	       break_type == NT_LB_AKSARA_START))
	    break_op = BREAK_PROHIBITED;

	  if ((prev_break_type == NT_LB_AKSARA ||
	       prev_LB_type == LB_Dotted_Circle ||
	       prev_break_type == NT_LB_AKSARA_START) &&
	      (break_type == NT_LB_VIRAMA_FINAL ||
	       break_type == NT_LB_VIRAMA))
	    break_op = BREAK_PROHIBITED;

	  if ((prev_prev_break_type == NT_LB_AKSARA ||
	       prev_prev_LB_type == LB_Dotted_Circle ||
	       prev_prev_break_type == NT_LB_AKSARA_START) &&
	      prev_break_type == NT_LB_VIRAMA &&
	      (break_type == NT_LB_AKSARA ||
	       LB_type == LB_Dotted_Circle))
	    break_op = BREAK_PROHIBITED;

	  if ((prev_break_type == NT_LB_AKSARA ||
	       prev_LB_type == LB_Dotted_Circle ||
	       prev_break_type == NT_LB_AKSARA_START) &&
	      (break_type == NT_LB_AKSARA ||
	       LB_type == LB_Dotted_Circle ||
	       break_type == NT_LB_AKSARA_START) &&
	      next_break_type == NT_LB_VIRAMA_FINAL)
	    break_op = BREAK_PROHIBITED;

	  /* Rule LB28 */
	  if ((prev_break_type == NT_LB_ALPHABETIC ||
	       prev_break_type == NT_LB_HEBREW_LETTER) &&
	      (break_type == NT_LB_ALPHABETIC ||
	       break_type == NT_LB_HEBREW_LETTER))
	    break_op = BREAK_PROHIBITED;

	  /* Rule LB27 */
	  if ((prev_break_type == NT_LB_HANGUL_L_JAMO ||
	       prev_break_type == NT_LB_HANGUL_V_JAMO ||
	       prev_break_type == NT_LB_HANGUL_T_JAMO ||
	       prev_break_type == NT_LB_HANGUL_LV_SYLLABLE ||
	       prev_break_type == NT_LB_HANGUL_LVT_SYLLABLE) &&
	      break_type == NT_LB_POSTFIX)
	    break_op = BREAK_PROHIBITED;

	  if (prev_break_type == NT_LB_PREFIX &&
	      (break_type == NT_LB_HANGUL_L_JAMO ||
	       break_type == NT_LB_HANGUL_V_JAMO ||
	       break_type == NT_LB_HANGUL_T_JAMO ||
	       break_type == NT_LB_HANGUL_LV_SYLLABLE ||
	       break_type == NT_LB_HANGUL_LVT_SYLLABLE))
	    break_op = BREAK_PROHIBITED;

	  /* Rule LB26 */
	  if (prev_break_type == NT_LB_HANGUL_L_JAMO &&
	      (break_type == NT_LB_HANGUL_L_JAMO ||
	       break_type == NT_LB_HANGUL_V_JAMO ||
	       break_type == NT_LB_HANGUL_LV_SYLLABLE ||
	       break_type == NT_LB_HANGUL_LVT_SYLLABLE))
	    break_op = BREAK_PROHIBITED;

	  if ((prev_break_type == NT_LB_HANGUL_V_JAMO ||
	       prev_break_type == NT_LB_HANGUL_LV_SYLLABLE) &&
	      (break_type == NT_LB_HANGUL_V_JAMO ||
	       break_type == NT_LB_HANGUL_T_JAMO))
	    break_op = BREAK_PROHIBITED;

	  if ((prev_break_type == NT_LB_HANGUL_T_JAMO ||
	       prev_break_type == NT_LB_HANGUL_LVT_SYLLABLE) &&
	      break_type == NT_LB_HANGUL_T_JAMO)
	    break_op = BREAK_PROHIBITED;

	  /* Rule LB25 */
	  if (prev_prev_LB_type == LB_Numeric &&
	      prev_break_type == NT_LB_CLOSE_PUNCTUATION &&
	      break_type == NT_LB_POSTFIX)
	    break_op = BREAK_PROHIBITED; /* NU ( SY | IS )* CL × PO */

	  if (prev_prev_LB_type == LB_Numeric &&
	      prev_break_type == NT_LB_CLOSE_PARENTHESIS &&
	      break_type == NT_LB_POSTFIX)
	    break_op = BREAK_PROHIBITED; /* NU ( SY | IS )* CP × PO */

	  if (prev_prev_LB_type == LB_Numeric &&
	      prev_break_type == NT_LB_CLOSE_PUNCTUATION &&
	      break_type == NT_LB_PREFIX)
	    break_op = BREAK_PROHIBITED; /* NU ( SY | IS )* CL × PR */

	  if (prev_prev_LB_type == LB_Numeric &&
	      prev_break_type == NT_LB_CLOSE_PARENTHESIS &&
	      break_type == NT_LB_PREFIX)
	    break_op = BREAK_PROHIBITED; /* NU ( SY | IS )* CP × PR */

	  if (prev_LB_type == LB_Numeric &&
	      break_type == NT_LB_POSTFIX)
	    break_op = BREAK_PROHIBITED; /* NU ( SY | IS )* × PO */

	  if (prev_LB_type == LB_Numeric &&
	      break_type == NT_LB_PREFIX)
	    break_op = BREAK_PROHIBITED; /* NU ( SY | IS )* × PR */

	  if (prev_break_type == NT_LB_POSTFIX &&
	      break_type == NT_LB_OPEN_PUNCTUATION &&
	      next_break_type == NT_LB_NUMERIC)
	    break_op = BREAK_PROHIBITED; /* PO × OP NU */

	  if (prev_break_type == NT_LB_POSTFIX &&
	      break_type == NT_LB_OPEN_PUNCTUATION &&
	      next_break_type == NT_LB_INFIX_SEPARATOR &&
	      next_next_break_type == NT_LB_NUMERIC)
	    break_op = BREAK_PROHIBITED; /* PO × OP IS NU */

	  if (prev_break_type == NT_LB_POSTFIX &&
	      break_type == NT_LB_NUMERIC)
	    break_op = BREAK_PROHIBITED; /* PO × NU */

	  if (prev_break_type == NT_LB_PREFIX &&
	      break_type == NT_LB_OPEN_PUNCTUATION &&
	      next_break_type == NT_LB_NUMERIC)
	    break_op = BREAK_PROHIBITED; /* PR × OP NU */

	  if (prev_break_type == NT_LB_PREFIX &&
	      break_type == NT_LB_OPEN_PUNCTUATION &&
	      next_break_type == NT_LB_INFIX_SEPARATOR &&
	      next_next_break_type == NT_LB_NUMERIC)
	    break_op = BREAK_PROHIBITED; /* PR × OP IS NU */

	  if (prev_break_type == NT_LB_PREFIX &&
	      break_type == NT_LB_NUMERIC)
	    break_op = BREAK_PROHIBITED; /* PR × NU */

	  if (prev_break_type == NT_LB_HYPHEN &&
	      break_type == NT_LB_NUMERIC)
	    break_op = BREAK_PROHIBITED; /* HY × NU */

	  if (prev_break_type == NT_LB_INFIX_SEPARATOR &&
	      break_type == NT_LB_NUMERIC)
	    break_op = BREAK_PROHIBITED; /* IS × NU */

	  if (prev_LB_type == LB_Numeric &&
	      break_type == NT_LB_NUMERIC)
	    break_op = BREAK_PROHIBITED; /* NU ( SY | IS )* × NU */

	  /* Rule LB24 */
	  if ((prev_break_type == NT_LB_PREFIX ||
	       prev_break_type == NT_LB_POSTFIX) &&
	      (break_type == NT_LB_ALPHABETIC ||
	       break_type == NT_LB_HEBREW_LETTER))
	    break_op = BREAK_PROHIBITED;

	  if ((prev_break_type == NT_LB_ALPHABETIC ||
	       prev_break_type == NT_LB_HEBREW_LETTER) &&
	      (break_type == NT_LB_PREFIX ||
	       break_type == NT_LB_POSTFIX))
	    break_op = BREAK_PROHIBITED;

	  /* Rule LB23a */
	  if (prev_break_type == NT_LB_PREFIX &&
	      (break_type == NT_LB_IDEOGRAPHIC ||
	       break_type == NT_LB_EMOJI_BASE ||
	       break_type == NT_LB_EMOJI_MODIFIER))
	    break_op = BREAK_PROHIBITED;

	  if ((prev_break_type == NT_LB_IDEOGRAPHIC ||
	       prev_break_type == NT_LB_EMOJI_BASE ||
	       prev_break_type == NT_LB_EMOJI_MODIFIER) &&
	      break_type == NT_LB_POSTFIX)
	    break_op = BREAK_PROHIBITED;

	  /* Rule LB23 */
	  if ((prev_break_type == NT_LB_ALPHABETIC ||
	       prev_break_type == NT_LB_HEBREW_LETTER) &&
	      break_type == NT_LB_NUMERIC)
	    break_op = BREAK_PROHIBITED;

	  if (prev_break_type == NT_LB_NUMERIC &&
	      (break_type == NT_LB_ALPHABETIC ||
	       break_type == NT_LB_HEBREW_LETTER))
	    break_op = BREAK_PROHIBITED;

	  /* Rule LB22 */
	  if (break_type == NT_LB_INSEPARABLE)
	    break_op = BREAK_PROHIBITED;

	  if (prev_break_type == NT_LB_SYMBOL &&
	      break_type == NT_LB_HEBREW_LETTER)
	    break_op = BREAK_PROHIBITED; /* Rule LB21b */

	  if (prev_prev_break_type == NT_LB_HEBREW_LETTER &&
	      (prev_break_type == NT_LB_HYPHEN ||
	       prev_break_type == NT_LB_UNAMBIGUOUS_HYPHEN) &&
	      break_type != NT_LB_HEBREW_LETTER)
	    break_op = BREAK_PROHIBITED; /* Rule LB21a */

	  if (break_type == NT_LB_AFTER ||
	      break_type == NT_LB_UNAMBIGUOUS_HYPHEN ||
	      break_type == NT_LB_HYPHEN ||
	      break_type == NT_LB_NON_STARTER ||
	      prev_break_type == NT_LB_BEFORE)
	    break_op = BREAK_PROHIBITED; /* Rule LB21 */

	  /* Rule LB20a */
	  if ((prev_LB_i == 0 ||
	       prev_prev_break_type == NT_LB_MANDATORY ||
	       prev_prev_break_type == NT_LB_CARRIAGE_RETURN ||
	       prev_prev_break_type == NT_LB_LINE_FEED ||
	       prev_prev_break_type == NT_LB_NEXT_LINE ||
	       prev_prev_break_type == NT_LB_SPACE ||
	       prev_prev_break_type == NT_LB_ZERO_WIDTH_SPACE ||
	       prev_prev_break_type == NT_LB_CONTINGENT ||
	       prev_prev_break_type == NT_LB_NON_BREAKING_GLUE) &&
	      (prev_break_type == NT_LB_HYPHEN ||
	       prev_break_type == NT_LB_UNAMBIGUOUS_HYPHEN) &&
	      (break_type == NT_LB_ALPHABETIC ||
	       break_type == NT_LB_HEBREW_LETTER))
	     break_op = BREAK_PROHIBITED;

	  if (prev_break_type == NT_LB_CONTINGENT ||
	      break_type == NT_LB_CONTINGENT)
	    break_op = BREAK_ALLOWED; /* Rule LB20 */

	  /* Rule LB19a */
	  if (!nt_is_EastAsianWide(prev_wc) &&
	      break_type == NT_LB_QUOTATION)
	    break_op = BREAK_PROHIBITED;

	  if (break_type == NT_LB_QUOTATION &&
	      (!nt_is_EastAsianWide(next_wc) || done))
	    break_op = BREAK_PROHIBITED;

	  if (prev_break_type == NT_LB_QUOTATION &&
	      !nt_is_EastAsianWide(wc))
	    break_op = BREAK_PROHIBITED;

	  if ((prev_LB_i == 0 || !nt_is_EastAsianWide(prev_prev_wc)) &&
	      prev_break_type == NT_LB_QUOTATION)
	    break_op = BREAK_PROHIBITED;

	  /* Rule LB19 */
	  if (break_type == NT_LB_QUOTATION &&
	      type != HB_UNICODE_GENERAL_CATEGORY_INITIAL_PUNCTUATION)
	    break_op = BREAK_PROHIBITED;

	  if (prev_break_type == NT_LB_QUOTATION &&
	      nt_unichar_type(prev_wc) != HB_UNICODE_GENERAL_CATEGORY_FINAL_PUNCTUATION)
	    break_op = BREAK_PROHIBITED;

	  /* handle related rules for Space as state machine here,
	     and override the pair table result. */
	  if (prev_break_type == NT_LB_SPACE) /* Rule LB18 */
	    break_op = BREAK_ALLOWED;

	  if (row_break_type == NT_LB_BEFORE_AND_AFTER &&
	      break_type == NT_LB_BEFORE_AND_AFTER)
	    break_op = BREAK_PROHIBITED; /* Rule LB17 */

	  if ((row_break_type == NT_LB_CLOSE_PUNCTUATION ||
	       row_break_type == NT_LB_CLOSE_PARENTHESIS) &&
	      break_type == NT_LB_NON_STARTER)
	    break_op = BREAK_PROHIBITED; /* Rule LB16 */

	  /* Rule LB15d */
	  if (break_type == NT_LB_INFIX_SEPARATOR)
	      break_op = BREAK_PROHIBITED;

	  /* Rule LB15c */
	  if (prev_break_type == NT_LB_SPACE &&
	      break_type == NT_LB_INFIX_SEPARATOR &&
	      next_break_type == NT_LB_NUMERIC)
	      break_op = BREAK_ALLOWED;

	  /* Rule LB15b */
	  if (type == HB_UNICODE_GENERAL_CATEGORY_FINAL_PUNCTUATION &&
	      break_type == NT_LB_QUOTATION)
	    {
	      if (next_wc == 0 ||
		  next_break_type == NT_LB_SPACE ||
		  next_break_type == NT_LB_NON_BREAKING_GLUE ||
		  next_break_type == NT_LB_WORD_JOINER ||
		  next_break_type == NT_LB_CLOSE_PUNCTUATION ||
		  next_break_type == NT_LB_QUOTATION ||
		  next_break_type == NT_LB_CLOSE_PARENTHESIS ||
		  next_break_type == NT_LB_EXCLAMATION ||
		  next_break_type == NT_LB_INFIX_SEPARATOR ||
		  next_break_type == NT_LB_SYMBOL ||
		  next_break_type == NT_LB_MANDATORY ||
		  next_break_type == NT_LB_CARRIAGE_RETURN ||
		  next_break_type == NT_LB_LINE_FEED ||
		  next_break_type == NT_LB_NEXT_LINE ||
		  next_break_type == NT_LB_ZERO_WIDTH_SPACE)
		break_op = BREAK_PROHIBITED;
	    }

	  /* Rule LB15a */
	  if (met_LB15a &&
	      break_type != NT_LB_SPACE &&
	      /* Rule LB9 */
	      break_type != NT_LB_COMBINING_MARK &&
	      break_type != NT_LB_ZERO_WIDTH_JOINER)
	    {
	      met_LB15a = FALSE;
	      break_op = BREAK_PROHIBITED;
	    }
	  else if (type == HB_UNICODE_GENERAL_CATEGORY_INITIAL_PUNCTUATION &&
	      break_type == NT_LB_QUOTATION)
	    {
	      if (i == 0 ||
		  prev_break_type == NT_LB_MANDATORY ||
		  prev_break_type == NT_LB_CARRIAGE_RETURN ||
		  prev_break_type == NT_LB_LINE_FEED ||
		  prev_break_type == NT_LB_NEXT_LINE ||
		  prev_break_type == NT_LB_OPEN_PUNCTUATION ||
		  prev_break_type == NT_LB_QUOTATION ||
		  prev_break_type == NT_LB_NON_BREAKING_GLUE ||
		  prev_break_type == NT_LB_SPACE ||
		  prev_break_type == NT_LB_ZERO_WIDTH_SPACE)
		met_LB15a = TRUE;
	    }

	  if (row_break_type == NT_LB_OPEN_PUNCTUATION)
	    break_op = BREAK_PROHIBITED; /* Rule LB14 */

	  if (break_type == NT_LB_CLOSE_PUNCTUATION ||
	      break_type == NT_LB_CLOSE_PARENTHESIS ||
	      break_type == NT_LB_EXCLAMATION ||
	      break_type == NT_LB_SYMBOL)
	    break_op = BREAK_PROHIBITED; /* Rule LB13 */

	  if (prev_wc == '/' && wc > ' ' && wc < 0x7f)
	    break_op = BREAK_PROHIBITED;

	  if (break_type == NT_LB_NON_BREAKING_GLUE &&
	      (prev_break_type != NT_LB_SPACE &&
	       prev_break_type != NT_LB_AFTER &&
	       prev_break_type != NT_LB_HYPHEN &&
	       prev_break_type != NT_LB_UNAMBIGUOUS_HYPHEN))
	    break_op = BREAK_PROHIBITED; /* Rule LB12a */

	  if (prev_break_type == NT_LB_NON_BREAKING_GLUE)
	    break_op = BREAK_PROHIBITED; /* Rule LB12 */

	  if (prev_break_type == NT_LB_WORD_JOINER ||
	      break_type == NT_LB_WORD_JOINER)
	    break_op = BREAK_PROHIBITED; /* Rule LB11 */

	  /* Rule LB9 */
	  if (break_type == NT_LB_COMBINING_MARK ||
              break_type == NT_LB_ZERO_WIDTH_JOINER)
	    {
	      if (!(prev_break_type == NT_LB_MANDATORY ||
		    prev_break_type == NT_LB_CARRIAGE_RETURN ||
		    prev_break_type == NT_LB_LINE_FEED ||
		    prev_break_type == NT_LB_NEXT_LINE ||
		    prev_break_type == NT_LB_SPACE ||
		    prev_break_type == NT_LB_ZERO_WIDTH_SPACE))
		break_op = BREAK_PROHIBITED;

	      if (met_LB15a)
		break_op = BREAK_PROHIBITED;
	    }
	  else
	    {
	      if (nt_is_emoji_extended_pictographic (wc) &&
		  nt_unichar_type (wc) == HB_UNICODE_GENERAL_CATEGORY_UNASSIGNED)
		met_Ext_Pict_Unassigned = TRUE;
	    }

	  if (prev_wc == 0x200D)
	    break_op = BREAK_PROHIBITED; /* Rule LB8a */

	  if (row_break_type == NT_LB_ZERO_WIDTH_SPACE)
	    break_op = BREAK_ALLOWED; /* Rule LB8 */

	  if (break_type == NT_LB_SPACE ||
	      break_type == NT_LB_ZERO_WIDTH_SPACE)
	    break_op = BREAK_PROHIBITED; /* Rule LB7 */

	  /* Rule LB6 */
	  if (break_type == NT_LB_MANDATORY ||
	      break_type == NT_LB_CARRIAGE_RETURN ||
	      break_type == NT_LB_LINE_FEED ||
	      break_type == NT_LB_NEXT_LINE)
	    break_op = BREAK_PROHIBITED;

	  /* Rules LB4 and LB5 */
	  if (prev_break_type == NT_LB_MANDATORY ||
	      (prev_break_type == NT_LB_CARRIAGE_RETURN &&
	       wc != '\n') ||
	      prev_break_type == NT_LB_LINE_FEED ||
	      prev_break_type == NT_LB_NEXT_LINE)
	    {
	      attrs[i].is_mandatory_break = TRUE;
	      break_op = BREAK_ALLOWED;
	    }

	  switch (break_op)
	    {
	    case BREAK_PROHIBITED:
	      /* can't break here */
	      attrs[i].is_line_break = FALSE;
	      break;

	    case BREAK_IF_SPACES:
	      /* break if prev char was space */
	      if (prev_break_type != NT_LB_SPACE)
		attrs[i].is_line_break = FALSE;
	      break;

	    case BREAK_ALLOWED:
	      attrs[i].is_line_break = TRUE;
	      break;

	    case BREAK_ALREADY_HANDLED:
	      break;

	    default:
	      break;
	      break;
	    }

	  /* Rule LB9 */
	  if (!(break_type == NT_LB_COMBINING_MARK ||
		break_type == NT_LB_ZERO_WIDTH_JOINER))
	    {
	      /* Rule LB25 without Example 7 of Customization */
	      if (break_type == NT_LB_SYMBOL ||
		    break_type == NT_LB_INFIX_SEPARATOR)
		{
		  if (prev_LB_type != LB_Numeric)
		    {
		      prev_prev_LB_type = prev_LB_type;
		      prev_LB_type = LB_type;
		    }
		  /* else don't change the prev_LB_type */
		}
	      else
		{
		  prev_prev_LB_type = prev_LB_type;
		  prev_LB_type = LB_type;
		}
	    }
	  /* else don't change the prev_LB_type for Rule LB9 */
	}

      if (break_type != NT_LB_SPACE)
	{
	  /* Rule LB9 */
	  if (break_type == NT_LB_COMBINING_MARK ||
	      break_type == NT_LB_ZERO_WIDTH_JOINER)
	    {
	      if (i == 0 /* start of text */ ||
		  prev_break_type == NT_LB_MANDATORY ||
		  prev_break_type == NT_LB_CARRIAGE_RETURN ||
		  prev_break_type == NT_LB_LINE_FEED ||
		  prev_break_type == NT_LB_NEXT_LINE ||
		  prev_break_type == NT_LB_SPACE ||
		  prev_break_type == NT_LB_ZERO_WIDTH_SPACE)
		prev_break_type = NT_LB_ALPHABETIC; /* Rule LB10 */
	      /* else don't change the prev_break_type for Rule LB9 */
	    }
	  else
	    {
	      prev_prev_break_type = prev_break_type;
	      prev_break_type = break_type;
	      prev_LB_i = i;
	    }

	  prev_jamo = jamo;
	}
      else
	{
	  if (prev_break_type != NT_LB_SPACE)
	    {
	      prev_prev_break_type = prev_break_type;
	      prev_break_type = break_type;
	      prev_LB_i = i;
	    }
	  /* else don't change the prev_break_type */
	}

      /* ---- Word breaks ---- */

      /* default to not a word start/end */
      attrs[i].is_word_start = FALSE;
      attrs[i].is_word_end = FALSE;

      if (current_word_type != WordNone)
	{
	  /* Check for a word end */
	  switch ((int) type)
	    {
	    case HB_UNICODE_GENERAL_CATEGORY_SPACING_MARK:
	    case HB_UNICODE_GENERAL_CATEGORY_ENCLOSING_MARK:
	    case HB_UNICODE_GENERAL_CATEGORY_NON_SPACING_MARK:
	    case HB_UNICODE_GENERAL_CATEGORY_FORMAT:
	      /* nothing, we just eat these up as part of the word */
	      break;

	    case HB_UNICODE_GENERAL_CATEGORY_LOWERCASE_LETTER:
	    case HB_UNICODE_GENERAL_CATEGORY_MODIFIER_LETTER:
	    case HB_UNICODE_GENERAL_CATEGORY_OTHER_LETTER:
	    case HB_UNICODE_GENERAL_CATEGORY_TITLECASE_LETTER:
	    case HB_UNICODE_GENERAL_CATEGORY_UPPERCASE_LETTER:
	      if (current_word_type == WordLetters)
		{
		  /* Japanese special cases for ending the word */
		  if (JAPANESE (last_word_letter) ||
		      JAPANESE (wc))
		    {
		      if ((HIRAGANA (last_word_letter) &&
			   !HIRAGANA (wc)) ||
			  (KATAKANA (last_word_letter) &&
			   !(KATAKANA (wc) || HIRAGANA (wc))) ||
			  (KANJI (last_word_letter) &&
			   !(HIRAGANA (wc) || KANJI (wc))) ||
			  (JAPANESE (last_word_letter) &&
			   !JAPANESE (wc)) ||
			  (!JAPANESE (last_word_letter) &&
			   JAPANESE (wc)))
			{
			  attrs[i].is_word_start = TRUE;
			  attrs[i].is_word_end = TRUE;
			}
		    }
		}
	      last_word_letter = wc;
	      break;

	    case HB_UNICODE_GENERAL_CATEGORY_DECIMAL_NUMBER:
	    case HB_UNICODE_GENERAL_CATEGORY_LETTER_NUMBER:
	    case HB_UNICODE_GENERAL_CATEGORY_OTHER_NUMBER:
	      last_word_letter = wc;
	      break;

	    default:
	      /* Punctuation, control/format chars, etc. all end a word. */
	      attrs[i].is_word_end = TRUE;
	      current_word_type = WordNone;
	      break;
	    }
	}
      else
	{
	  /* Check for a word start */
	  switch ((int) type)
	    {
	    case HB_UNICODE_GENERAL_CATEGORY_LOWERCASE_LETTER:
	    case HB_UNICODE_GENERAL_CATEGORY_MODIFIER_LETTER:
	    case HB_UNICODE_GENERAL_CATEGORY_OTHER_LETTER:
	    case HB_UNICODE_GENERAL_CATEGORY_TITLECASE_LETTER:
	    case HB_UNICODE_GENERAL_CATEGORY_UPPERCASE_LETTER:
	      current_word_type = WordLetters;
	      last_word_letter = wc;
	      attrs[i].is_word_start = TRUE;
	      break;

	    case HB_UNICODE_GENERAL_CATEGORY_DECIMAL_NUMBER:
	    case HB_UNICODE_GENERAL_CATEGORY_LETTER_NUMBER:
	    case HB_UNICODE_GENERAL_CATEGORY_OTHER_NUMBER:
	      current_word_type = WordNumbers;
	      last_word_letter = wc;
	      attrs[i].is_word_start = TRUE;
	      break;

	    default:
	      /* No word here */
	      break;
	    }
	}

      /* ---- Sentence breaks ---- */
      {

	/* default to not a sentence start/end */
	attrs[i].is_sentence_start = FALSE;
	attrs[i].is_sentence_end = FALSE;

	/* maybe start sentence */
	if (last_sentence_start == -1 && !is_sentence_boundary)
	  last_sentence_start = i - 1;

	/* remember last non space character position */
	if (i > 0 && !attrs[i - 1].is_white)
	  last_non_space = i;

	/* meets sentence end, mark both sentence start and end */
	if (last_sentence_start != -1 && is_sentence_boundary) {
	  if (last_non_space >= last_sentence_start) {
	    attrs[last_sentence_start].is_sentence_start = TRUE;
	    attrs[last_non_space].is_sentence_end = TRUE;
	  }

	  last_sentence_start = -1;
	  last_non_space = -1;
	}

	/* meets space character, move sentence start */
	if (last_sentence_start != -1 &&
	    last_sentence_start == i - 1 &&
	    attrs[i - 1].is_white) {
	    last_sentence_start++;
          }
      }

      /* --- Hyphens --- */

      {
        int insert_hyphens;
        int space_or_hyphen = FALSE;

        attrs[i].break_inserts_hyphen = FALSE;
        attrs[i].break_removes_preceding = FALSE;

        switch ((int)prev_script)
          {
          case HB_SCRIPT_COMMON:
            insert_hyphens = prev_wc == 0x00ad;
            break;
          case HB_SCRIPT_HAN:
          case HB_SCRIPT_HANGUL:
          case HB_SCRIPT_HIRAGANA:
          case HB_SCRIPT_KATAKANA:
            insert_hyphens = FALSE;
            break;
          default:
            insert_hyphens = TRUE;
            break;
          }

        switch ((int)type)
          {
          case HB_UNICODE_GENERAL_CATEGORY_SPACE_SEPARATOR:
          case HB_UNICODE_GENERAL_CATEGORY_LINE_SEPARATOR:
          case HB_UNICODE_GENERAL_CATEGORY_PARAGRAPH_SEPARATOR:
            space_or_hyphen = TRUE;
            break;
          case HB_UNICODE_GENERAL_CATEGORY_CONTROL:
            if (wc == '\t' || wc == '\n' || wc == '\r' || wc == '\f')
              space_or_hyphen = TRUE;
            break;
          default:
            break;
          }

        if (!space_or_hyphen)
          {
            if (wc == '-'    || /* Hyphen-minus */
                wc == 0x058a || /* Armenian hyphen */
                wc == 0x1400 || /* Canadian syllabics hyphen */
                wc == 0x1806 || /* Mongolian todo hyphen */
                wc == 0x2010 || /* Hyphen */
                wc == 0x2e17 || /* Double oblique hyphen */
                wc == 0x2e40 || /* Double hyphen */
                wc == 0x30a0 || /* Katakana-Hiragana double hyphen */
                wc == 0xfe63 || /* Small hyphen-minus */
                wc == 0xff0d)   /* Fullwidth hyphen-minus */
              space_or_hyphen = TRUE;
          }

        if (attrs[i].is_word_boundary)
          attrs[i].break_inserts_hyphen = FALSE;
        else if (prev_space_or_hyphen)
          attrs[i].break_inserts_hyphen = FALSE;
        else if (space_or_hyphen)
          attrs[i].break_inserts_hyphen = FALSE;
        else
          attrs[i].break_inserts_hyphen = insert_hyphens;

        if (prev_wc == 0x2027)     /* Hyphenation point */
          {
            attrs[i].break_inserts_hyphen = TRUE;
            attrs[i].break_removes_preceding = TRUE;
          }

        prev_space_or_hyphen = space_or_hyphen;
      }

      prev_prev_wc = prev_wc;
      prev_wc = wc;
      prev_script = script;

      /* wc might not be a valid Unicode base character, but really all we
       * need to know is the last non-combining character */
      if (type != HB_UNICODE_GENERAL_CATEGORY_SPACING_MARK &&
	  type != HB_UNICODE_GENERAL_CATEGORY_ENCLOSING_MARK &&
	  type != HB_UNICODE_GENERAL_CATEGORY_NON_SPACING_MARK)
	base_character = wc;
    }

  i--;

  attrs[0].is_cursor_position = TRUE;  /* Rule GB1 */
  attrs[i].is_cursor_position = TRUE;  /* Rule GB2 */

  attrs[0].is_word_boundary = TRUE;  /* Rule WB1 */
  attrs[i].is_word_boundary = TRUE;  /* Rule WB2 */

  attrs[0].is_line_break = FALSE; /* Rule LB2 */
  attrs[i].is_line_break = TRUE;  /* Rule LB3 */
  attrs[i].is_mandatory_break = TRUE;  /* Rule LB3 */
}

/* }}} */
/* {{{ Script-specific tailoring */


#define ALEF_WITH_MADDA_ABOVE	0x0622
#define YEH_WITH_HAMZA_ABOVE	0x0626
#define ALEF			0x0627
#define WAW			0x0648
#define YEH			0x064A

#define MADDAH_ABOVE		0x0653
#define HAMZA_ABOVE		0x0654
#define HAMZA_BELOW		0x0655

/*
 * Arabic characters with canonical decompositions that are not just
 * ligatures.  The characters U+06C0, U+06C2, and U+06D3 are intentionally
 * excluded as they are marked as "not an independent letter" in Unicode
 * Character Database's NamesList.txt
 */
#define IS_COMPOSITE(c) (ALEF_WITH_MADDA_ABOVE <= (c) && (c) <= YEH_WITH_HAMZA_ABOVE)

/* If a character is the second part of a composite Arabic character with an Alef */
#define IS_COMPOSITE_WITH_ALEF(c) (MADDAH_ABOVE <= (c) && (c) <= HAMZA_BELOW)

static void
break_arabic (const char          *text,
	      int                  length,
	      const NsPangoAnalysis *analysis NT_UNUSED,
	      NsPangoLogAttr        *attrs,
	      int                  attrs_len NT_UNUSED)
{
  int i;
  const char *p;
  nt_unichar prev_wc, this_wc;

  /* See http://bugzilla.gnome.org/show_bug.cgi?id=350132 for issues this
   * module tries to solve.
   */

  for (p = text, i = 0, prev_wc = 0;
       p < text + length;
       p = nt_utf8_next_char (p), i++, prev_wc = this_wc)
    {
      this_wc = nt_utf8_get_char (p);

      /*
       * Unset backspace_deletes_character for various combinations.
       *
       * A few more combinations may need to be handled here, but are not
       * handled yet, as expectations of users is not known or may differ
       * among different languages or users:
       * some letters combined with U+0658 ARABIC MARK NOON GHUNNA;
       * combinations considered one letter in Azerbaijani (WAW+SUKUN and
       * FARSI_YEH+HAMZA_ABOVE); combinations of YEH and ALEF_MAKSURA with
       * HAMZA_BELOW (Qur'anic); TATWEEL+HAMZA_ABOVE (Qur'anic).
       *
       * FIXME: Ordering these in some other way may lower the time spent here, or not.
       */
      if ( (
	   IS_COMPOSITE (this_wc) ||
	  (prev_wc == ALEF && IS_COMPOSITE_WITH_ALEF (this_wc)) ||
	  (this_wc == HAMZA_ABOVE && (prev_wc == WAW || prev_wc == YEH))
	 ))
	attrs[i+1].backspace_deletes_character = FALSE;
    }
}



#define DEV_RRA 0x0931 /* 0930 + 093c */
#define DEV_QA 0x0958 /* 0915 + 093c */
#define DEV_YA 0x095F /* 092f + 003c */
#define DEV_KHHA 0x0959
#define DEV_GHHA 0x095A
#define DEV_ZA 0x095B
#define DEV_DDDHA 0x095C
#define DEV_RHA 0x095D
#define DEV_FA 0x095E
#define DEV_YYA 0x095F

/* Bengali */
/* for split matras in all brahmi based script */
#define BENGALI_SIGN_O 0x09CB  /* 09c7 + 09be */
#define BENGALI_SIGN_AU 0x09CC /* 09c7 + 09d7 */
#define BENGALI_RRA 0x09DC
#define BENGALI_RHA 0x09DD
#define BENGALI_YYA 0x09DF

/* Gurumukhi */
#define GURUMUKHI_LLA 0x0A33
#define GURUMUKHI_SHA 0x0A36
#define GURUMUKHI_KHHA 0x0A59
#define GURUMUKHI_GHHA 0x0A5A
#define GURUMUKHI_ZA 0x0A5B
#define GURUMUKHI_RRA 0x0A5C
#define GURUMUKHI_FA 0x0A5E

/* Oriya */
#define ORIYA_AI 0x0B48
#define ORIYA_O 0x0B4B
#define ORIYA_AU 0x0B4C

/* Telugu */
#define TELUGU_EE 0x0C47
#define TELUGU_AI 0x0C48

/* Tamil */
#define TAMIL_O 0x0BCA
#define TAMIL_OO 0x0BCB
#define TAMIL_AU 0x0BCC

/* Kannada */
#define KNDA_EE 0x0CC7
#define KNDA_AI 0x0CC8
#define KNDA_O 0x0CCA
#define KNDA_OO 0x0CCB

/* Malayalam */
#define MLYM_O 0x0D4A
#define MLYM_OO 0x0D4B
#define MLYM_AU 0x0D4C

#define IS_COMPOSITE_WITH_BRAHMI_NUKTA(c) ( \
	(c >= BENGALI_RRA  && c <= BENGALI_YYA) || \
	(c >= DEV_QA  && c <= DEV_YA) || (c == DEV_RRA) || (c >= DEV_KHHA  && c <= DEV_YYA) || \
	(c >= KNDA_EE  && c <= KNDA_AI) ||(c >= KNDA_O  && c <= KNDA_OO) || \
	(c == TAMIL_O) || (c == TAMIL_OO) || (c == TAMIL_AU) || \
	(c == TELUGU_EE) || (c == TELUGU_AI) || \
	(c == ORIYA_AI) || (c == ORIYA_O) || (c == ORIYA_AU) || \
	(c >= GURUMUKHI_KHHA  && c <= GURUMUKHI_RRA) || (c == GURUMUKHI_FA)|| (c == GURUMUKHI_LLA)|| (c == GURUMUKHI_SHA) || \
	FALSE)
#define IS_SPLIT_MATRA_BRAHMI(c) ( \
	(c == BENGALI_SIGN_O) || (c == BENGALI_SIGN_AU) || \
	(c >= MLYM_O  && c <= MLYM_AU) || \
	FALSE)

static void
not_cursor_position (NsPangoLogAttr *attr)
{
  if (!attr->is_mandatory_break)
    {
      attr->is_cursor_position = FALSE;
      attr->is_char_break = FALSE;
      attr->is_line_break = FALSE;
      attr->is_mandatory_break = FALSE;
    }
}

static void
break_indic (const char          *text,
	     int                  length,
	     const NsPangoAnalysis *analysis,
	     NsPangoLogAttr        *attrs,
	     int                  attrs_len NT_UNUSED)
{
  const char *p, *next = NULL, *next_next;
  nt_unichar prev_wc, this_wc, next_wc, next_next_wc;
  int is_conjunct = FALSE;
  int i;

  for (p = text, prev_wc = 0, i = 0;
       p != NULL && p < (text + length);
       p = next, prev_wc = this_wc, i++)
    {
      this_wc = nt_utf8_get_char (p);
      next = nt_utf8_next_char (p);

    if ( (
               IS_COMPOSITE_WITH_BRAHMI_NUKTA(this_wc) || IS_SPLIT_MATRA_BRAHMI(this_wc))) {
         attrs[i+1].backspace_deletes_character = FALSE;
      }

      if (next != NULL && next < (text + length))
	{
	  next_wc = nt_utf8_get_char (next);
	  next_next = nt_utf8_next_char (next);
	}
      else
	{
	  next_wc = 0;
	  next_next = NULL;
	}
      if (next_next != NULL && next_next < (text + length))
	next_next_wc = nt_utf8_get_char (next_next);
      else
	next_next_wc = 0;

      switch (analysis->script)
      {
        case HB_SCRIPT_SINHALA:
	  /*
	   * TODO: The cursor position should be based on the state table.
	   *       This is the wrong place to be doing this.
	   */

	  /*
	   * The cursor should treat as a single glyph:
	   * SINHALA CONS + 0x0DCA + 0x200D + SINHALA CONS
	   * SINHALA CONS + 0x200D + 0x0DCA + SINHALA CONS
	   */
	  if ((this_wc == 0x0DCA && next_wc == 0x200D)
	      || (this_wc == 0x200D && next_wc == 0x0DCA))
	    {
	      not_cursor_position(&attrs[i]);
	      not_cursor_position(&attrs[i + 1]);
	      is_conjunct = TRUE;
	    }
	  else if (is_conjunct
		   && (prev_wc == 0x200D || prev_wc == 0x0DCA)
		   && this_wc >= 0x0D9A
		   && this_wc <= 0x0DC6)
	    {
	      not_cursor_position(&attrs[i]);
	      is_conjunct = FALSE;
	    }
	  /*
	   * Consonant clusters do NOT result in implicit conjuncts
	   * in SINHALA orthography.
	   */
	  else if (!is_conjunct && prev_wc == 0x0DCA && this_wc != 0x200D)
	    {
	      attrs[i].is_cursor_position = TRUE;
	    }

	  break;

	default:

	  if (prev_wc != 0 && (this_wc == 0x200D || this_wc == 0x200C))
	    {
	      not_cursor_position(&attrs[i]);
	      if (next_wc != 0)
		{
		  not_cursor_position(&attrs[i+1]);
		  if ((next_next_wc != 0) &&
		       (next_wc == 0x09CD ||	/* Bengali */
			next_wc == 0x0ACD ||	/* Gujarati */
			next_wc == 0x094D ||	/* Hindi */
			next_wc == 0x0CCD ||	/* Kannada */
			next_wc == 0x0D4D ||	/* Malayalam */
			next_wc == 0x0B4D ||	/* Oriya */
			next_wc == 0x0A4D ||	/* Punjabi */
			next_wc == 0x0BCD ||	/* Tamil */
			next_wc == 0x0C4D))	/* Telugu */
		    {
		      not_cursor_position(&attrs[i+2]);
		    }
		}
	    }

	  break;
      }
    }
}



/* Threads, locks and queues from the SDL layer; see the header. */

#ifdef HAVE_LIBTHAI
#include <thai/thwchar.h>
#include <thai/thbrk.h>

static NsMutex thai_brk_lock;

#ifdef HAVE_TH_BRK_FIND_BREAKS
static ThBrk *thai_brk = NULL;
#endif

/*
 * tis_text is assumed to be large enough to hold the converted string,
 * i.e. it must be at least nt_utf8_strlen(text, len)+1 bytes.
 */
static thchar_t *
utf8_to_tis (const char *text, int len, thchar_t *tis_text, int *tis_cnt)
{
  thchar_t   *tis_p;
  const char *text_p;

  tis_p = tis_text;
  for (text_p = text; text_p < text + len; text_p = nt_utf8_next_char (text_p))
    *tis_p++ = th_uni2tis (nt_utf8_get_char (text_p));
  *tis_p++ = '\0';

  *tis_cnt = tis_p - tis_text;
  return tis_text;
}

#endif
static void
break_thai (const char          *text,
	    int                  len,
	    const NsPangoAnalysis *analysis NT_UNUSED,
	    NsPangoLogAttr        *attrs,
	    int                  attrs_len NT_UNUSED)
{
#ifdef HAVE_LIBTHAI
  thchar_t tis_stack[512];
  int brk_stack[512];
  thchar_t *tis_text;
  int *brk_pnts;
  int cnt;

  cnt = nt_utf8_strlen (text, len) + 1;

  tis_text = tis_stack;
  if (cnt > (int) NT_N_ELEMENTS (tis_stack))
    tis_text = nt_new (thchar_t, cnt);

  utf8_to_tis (text, len, tis_text, &cnt);

  brk_pnts = brk_stack;
  if (cnt > (int) NT_N_ELEMENTS (brk_stack))
    brk_pnts = nt_new (int, cnt);

  /* find line break positions */

  ns_mutex_lock (&thai_brk_lock);
#ifdef HAVE_TH_BRK_FIND_BREAKS
  if (thai_brk == NULL)
    thai_brk = th_brk_new (NULL);
  len = th_brk_find_breaks (thai_brk, tis_text, brk_pnts, cnt);
#else
  len = th_brk (tis_text, brk_pnts, cnt);
#endif
  ns_mutex_unlock (&thai_brk_lock);

  for (cnt = 0; cnt < len; cnt++)
    {
      if (!attrs[brk_pnts[cnt]].is_line_break)
        {
          /* Insert line breaks where there wasn't one.
           * Satisfy invariants by marking it as char break too.
           */
          attrs[brk_pnts[cnt]].is_char_break = TRUE;
          attrs[brk_pnts[cnt]].is_line_break = TRUE;
        }
      if (!(attrs[brk_pnts[cnt]].is_word_start ||
            attrs[brk_pnts[cnt]].is_word_end))
        {
          /* If we find a break in the middle of a sequence
           * of characters, end and start a word. We must
           * be careful only to do that if default_break
           * did not already find a word start or end,
           * otherwise we mess up the sequence.
           */
          attrs[brk_pnts[cnt]].is_word_start = TRUE;
          attrs[brk_pnts[cnt]].is_word_end = TRUE;
        }
    }

  if (brk_pnts != brk_stack)
    free (brk_pnts);

  if (tis_text != tis_stack)
    free (tis_text);
#endif
}



static void
break_latin (const char          *text,
             int                  length,
             const NsPangoAnalysis *analysis,
             NsPangoLogAttr        *attrs,
             int                  attrs_len NT_UNUSED)
{
  int i;
  const char *p, *next;
  nt_unichar wc, prev_wc;

  if (!analysis || !analysis->language ||
      nt_ascii_strncasecmp (ns_pango_language_to_string (analysis->language), "ca-", 3) != 0)
    return;

  for (p = text, i = 0, prev_wc = 0;
       p < text + length;
       p = next, i++, prev_wc = wc)
    {
      wc = nt_utf8_get_char (p);
      next = nt_utf8_next_char (p);

      /* Catalan middle dot does not break words */
      if (wc == 0x00b7)
        {
          nt_unichar middle_next = nt_utf8_get_char (next);
          if (nt_unichar_tolower (middle_next) == 'l' && nt_unichar_tolower (prev_wc) == 'l')
            {
              attrs[i].is_word_end = FALSE;
              attrs[i+1].is_word_start = FALSE;
            }
        }
    }
}

static int
break_script (const char          *item_text,
	      unsigned int         item_length,
	      const NsPangoAnalysis *analysis,
	      NsPangoLogAttr        *attrs,
	      int                  attrs_len)
{
  switch (analysis->script)
    {
    case HB_SCRIPT_ARABIC:
      break_arabic (item_text, item_length, analysis, attrs, attrs_len);
      break;

    case HB_SCRIPT_DEVANAGARI:
    case HB_SCRIPT_BENGALI:
    case HB_SCRIPT_GURMUKHI:
    case HB_SCRIPT_GUJARATI:
    case HB_SCRIPT_ORIYA:
    case HB_SCRIPT_TAMIL:
    case HB_SCRIPT_TELUGU:
    case HB_SCRIPT_KANNADA:
    case HB_SCRIPT_MALAYALAM:
    case HB_SCRIPT_SINHALA:
      break_indic (item_text, item_length, analysis, attrs, attrs_len);
      break;

    case HB_SCRIPT_THAI:
      break_thai (item_text, item_length, analysis, attrs, attrs_len);
      break;

    case HB_SCRIPT_LATIN:
      break_latin (item_text, item_length, analysis, attrs, attrs_len);
      break;

    default:
      return FALSE;
    }

  return TRUE;
}

/* }}} */
/* {{{ Attribute-based customization */

/* We allow customizing log attrs in two ways:
 *
 * - You can directly remove breaks from a range, using allow_breaks=false.
 *   Only mandatory breaks remain, as for CSS white-space: nowrap, which
 *   also suppresses the opportunities after ZWS, hyphens and visible word
 *   dividers.
 *
 * - You can tweak the segmentation by marking ranges as word or sentence.
 *   When doing so, we split adjacent segments to preserve alternating
 *   starts and ends. We add a line break opportunity before each word that
 *   is created in this way, and we remove line break opportunities inside
 *   the word in the same way as for a range marked as allow_breaks=false,
 *   except that we don't remove char break opportunities.
 *
 *   Note that UAX #14 does not guarantee that words fall neatly into
 *   sentences, so we don't do extra work to enforce that.
 */

static int
attr_range_to_pos (const char *text, int length, int offset, unsigned start, unsigned end,
                   int log_attrs_len, int *start_pos, int *end_pos)
{
  if ((int) start < offset || start == (unsigned) -1)
    *start_pos = 0;
  else
    *start_pos = nt_utf8_pointer_to_offset (text, text + start - offset);
  if (end == (unsigned) -1 || (long) end >= (long) offset + length)
    *end_pos = log_attrs_len;
  else
    *end_pos = nt_utf8_pointer_to_offset (text, text + end - offset);
  return TRUE;
}

/* allow_breaks=false keeps only the mandatory breaks; insert_hyphens=false
 * drops the hyphens a break would insert, as Pango's attr_break. */
void
nt_attr_break (const char *text, int length, const NsPangoAttrList *attrs, int offset,
               NsPangoLogAttr *log_attrs, int log_attrs_len)
{
  NsPangoLogAttr before;
  int tailored = FALSE;
  int i;

  if (!attrs || log_attrs_len <= 0)
    return;
  if (length < 0)
    length = (int) strlen (text);
  before = log_attrs[0];

  for (i = 0; i < attrs->n; i++)
    {
      const NtAttr *a = attrs->attrs[i];
      int start_pos, end_pos, pos;

      if (a->base.type == NT_ATTR_INSERT_HYPHENS && a->v.i == 0)
        {
          attr_range_to_pos (text, length, offset, a->base.start_index, a->base.end_index,
                             log_attrs_len, &start_pos, &end_pos);
          for (pos = start_pos + 1; pos < end_pos && pos < log_attrs_len; pos++)
            if (!log_attrs[pos].break_removes_preceding)
              {
                log_attrs[pos].break_inserts_hyphen = FALSE;
                tailored = TRUE;
              }
        }
    }
  for (i = 0; i < attrs->n; i++)
    {
      const NtAttr *a = attrs->attrs[i];
      int start_pos, end_pos, pos;

      if (a->base.type == NT_ATTR_ALLOW_BREAKS && a->v.i == 0)
        {
          attr_range_to_pos (text, length, offset, a->base.start_index, a->base.end_index,
                             log_attrs_len, &start_pos, &end_pos);
          for (pos = start_pos + 1; pos < end_pos && pos < log_attrs_len; pos++)
            {
              log_attrs[pos].is_char_break = FALSE;
              if (!log_attrs[pos].is_mandatory_break)
                log_attrs[pos].is_line_break = FALSE;
            }
          tailored = TRUE;
        }
    }

  if (tailored)
    {
      log_attrs[0].backspace_deletes_character = before.backspace_deletes_character;
      log_attrs[0].is_line_break |= before.is_line_break;
      log_attrs[0].is_mandatory_break |= before.is_mandatory_break;
      log_attrs[0].is_cursor_position |= before.is_cursor_position;
    }
}

/* }}} */
/* {{{ Public API */

void
nt_default_break (const char *text, int length, NsPangoLogAttr *attrs, int attrs_len)
{
  NsPangoLogAttr before = *attrs;

  default_break (text, length, NULL, attrs, attrs_len);
  attrs->is_line_break      |= before.is_line_break;
  attrs->is_mandatory_break |= before.is_mandatory_break;
  attrs->is_cursor_position |= before.is_cursor_position;
}

void
nt_tailor_break (const char *text, int length, hb_script_t script,
                 NsPangoLanguage *language, NsPangoLogAttr *attrs, int attrs_len)
{
  NsPangoAnalysis analysis = { script, language };
  NsPangoLogAttr before = *attrs;

  if (length < 0)
    length = (int) strlen (text);
  if (break_script (text, (unsigned) length, &analysis, attrs, attrs_len))
    {
      attrs->backspace_deletes_character = before.backspace_deletes_character;
      attrs->is_line_break      |= before.is_line_break;
      attrs->is_mandatory_break |= before.is_mandatory_break;
      attrs->is_cursor_position |= before.is_cursor_position;
    }
}

void
ns_pango_get_log_attrs (const char *text, int length, int level NT_UNUSED,
                        NsPangoLanguage *language, NsPangoLogAttr *attrs, int attrs_len)
{
  NtScriptIter iter;
  int chars_broken = 0;

  if (!attrs || attrs_len <= 0)
    return;
  if (length < 0)
    length = text ? (int) strlen (text) : 0;
  memset (attrs, 0, sizeof *attrs * (size_t) attrs_len);
  nt_default_break (text ? text : "", length, attrs, attrs_len);
  if (length == 0)
    return;

  nt_script_iter_init (&iter, text, length);
  do
    {
      int chars_in_range = nt_utf8_strlen (iter.script_start,
                                           (int) (iter.script_end - iter.script_start));

      if (chars_broken + chars_in_range + 1 > attrs_len)
        break;
      nt_tailor_break (iter.script_start, (int) (iter.script_end - iter.script_start),
                       iter.script_code, language, attrs + chars_broken, chars_in_range + 1);
      chars_broken += chars_in_range;
    }
  while (nt_script_iter_next (&iter));
}
