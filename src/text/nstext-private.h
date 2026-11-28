/* Weststar SDL Browser — nstext's internals.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_TEXT_PRIVATE_H
#define NS_TEXT_PRIVATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <hb.h>
#include <hb-ot.h>
#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include "nstext.h"
#include "plat/ns_plat.h"

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#define NT_MIN(a, b) ((a) < (b) ? (a) : (b))
#define NT_MAX(a, b) ((a) > (b) ? (a) : (b))
#define NT_N_ELEMENTS(a) (sizeof (a) / sizeof ((a)[0]))
#define NT_UNUSED __attribute__((unused))

/* ---- memory ------------------------------------------------------------ */

/* Allocation failure aborts, as GLib's does. */
void *nt_malloc  (size_t n);
void *nt_malloc0 (size_t n);
void *nt_realloc (void *p, size_t n);
char *nt_strdup  (const char *s);
char *nt_strndup (const char *s, size_t n);
#define nt_new(T, n)  ((T *) nt_malloc  (sizeof (T) * (size_t) (n)))
#define nt_new0(T, n) ((T *) nt_malloc0 (sizeof (T) * (size_t) (n)))
#define nt_renew(T, p, n) ((T *) nt_realloc ((p), sizeof (T) * (size_t) (n)))

int  nt_ascii_strcasecmp  (const char *a, const char *b);
int  nt_ascii_strncasecmp (const char *a, const char *b, size_t n);
char nt_ascii_tolower     (char c);

/* A growable byte string. */
typedef struct
{
  char  *str;
  size_t len, cap;
} NtString;

void nt_string_append_len (NtString *s, const char *p, size_t n);
void nt_string_append     (NtString *s, const char *p);
void nt_string_append_c   (NtString *s, char c);
void nt_string_printf     (NtString *s, const char *fmt, ...)
  __attribute__((format (printf, 2, 3)));

/* ---- UTF-8 and Unicode ------------------------------------------------- */

typedef uint32_t nt_unichar;

extern const unsigned char nt_utf8_skip[256];
#define nt_utf8_next_char(p) ((p) + nt_utf8_skip[*(const unsigned char *) (p)])

nt_unichar  nt_utf8_get_char          (const char *p);
const char *nt_utf8_prev_char         (const char *p);
int         nt_utf8_strlen            (const char *p, int max_bytes);
const char *nt_utf8_offset_to_pointer (const char *s, int offset);
int         nt_utf8_pointer_to_offset (const char *s, const char *p);
int         nt_unichar_to_utf8        (nt_unichar c, char *out);
/* Copies valid UTF-8 with bad bytes replaced by U+FFFD. */
char       *nt_utf8_make_valid        (const char *s, int len, int *out_len);

/* General category: GLib's GUnicodeType and HarfBuzz's categories are the
 * same enumeration. */
hb_unicode_general_category_t nt_unichar_type (nt_unichar c);
hb_script_t nt_unichar_script (nt_unichar c);
int         nt_unichar_combining_class (nt_unichar c);
nt_unichar  nt_unichar_tolower (nt_unichar c);
nt_unichar  nt_unichar_toupper (nt_unichar c);
nt_unichar  nt_unichar_mirror  (nt_unichar c);
int         nt_unichar_isspace (nt_unichar c);
int         nt_unichar_iswide  (nt_unichar c);
int         nt_unichar_iszerowidth (nt_unichar c);
int         nt_unichar_isalnum (nt_unichar c);

/* Line_Break classes, numbered as GLib's GUnicodeBreakType. */
typedef enum
{
  NT_LB_MANDATORY, NT_LB_CARRIAGE_RETURN, NT_LB_LINE_FEED, NT_LB_COMBINING_MARK,
  NT_LB_SURROGATE, NT_LB_ZERO_WIDTH_SPACE, NT_LB_INSEPARABLE,
  NT_LB_NON_BREAKING_GLUE, NT_LB_CONTINGENT, NT_LB_SPACE, NT_LB_AFTER,
  NT_LB_BEFORE, NT_LB_BEFORE_AND_AFTER, NT_LB_HYPHEN, NT_LB_NON_STARTER,
  NT_LB_OPEN_PUNCTUATION, NT_LB_CLOSE_PUNCTUATION, NT_LB_QUOTATION,
  NT_LB_EXCLAMATION, NT_LB_IDEOGRAPHIC, NT_LB_NUMERIC, NT_LB_INFIX_SEPARATOR,
  NT_LB_SYMBOL, NT_LB_ALPHABETIC, NT_LB_PREFIX, NT_LB_POSTFIX,
  NT_LB_COMPLEX_CONTEXT, NT_LB_AMBIGUOUS, NT_LB_UNKNOWN, NT_LB_NEXT_LINE,
  NT_LB_WORD_JOINER, NT_LB_HANGUL_L_JAMO, NT_LB_HANGUL_V_JAMO,
  NT_LB_HANGUL_T_JAMO, NT_LB_HANGUL_LV_SYLLABLE, NT_LB_HANGUL_LVT_SYLLABLE,
  NT_LB_CLOSE_PARENTHESIS, NT_LB_CONDITIONAL_JAPANESE_STARTER,
  NT_LB_HEBREW_LETTER, NT_LB_REGIONAL_INDICATOR, NT_LB_EMOJI_BASE,
  NT_LB_EMOJI_MODIFIER, NT_LB_ZERO_WIDTH_JOINER, NT_LB_AKSARA,
  NT_LB_AKSARA_PRE_BASE, NT_LB_AKSARA_START, NT_LB_VIRAMA_FINAL, NT_LB_VIRAMA,
  NT_LB_UNAMBIGUOUS_HYPHEN
} NtBreakType;

NtBreakType nt_unichar_break_type (nt_unichar c);

int nt_is_emoji_extended_pictographic (nt_unichar c);
int nt_is_emoji (nt_unichar c);
int nt_is_emoji_presentation (nt_unichar c);
int nt_is_emoji_modifier (nt_unichar c);
int nt_is_emoji_modifier_base (nt_unichar c);
int nt_is_regional_indicator (nt_unichar c);
int nt_is_emoji_keycap_base (nt_unichar c);
int nt_is_default_ignorable (nt_unichar c);

/* ---- languages --------------------------------------------------------- */

struct _PangoLanguage
{
  char tag[1]; /* lowercase, '-' separated; interned */
};

const char *nt_language_sample_string (NsPangoLanguage *lang);
int         nt_language_includes_script (NsPangoLanguage *lang, hb_script_t script);
NsPangoLanguage *nt_script_sample_language (hb_script_t script);

/* ---- attributes -------------------------------------------------------- */

typedef enum
{
  NT_ATTR_INVALID,
  NT_ATTR_LANGUAGE,
  NT_ATTR_FAMILY,
  NT_ATTR_STYLE,
  NT_ATTR_WEIGHT,
  NT_ATTR_VARIANT,
  NT_ATTR_STRETCH,
  NT_ATTR_ABSOLUTE_SIZE,
  NT_ATTR_FONT_DESC,
  NT_ATTR_FOREGROUND,
  NT_ATTR_BACKGROUND,
  NT_ATTR_UNDERLINE,
  NT_ATTR_STRIKETHROUGH,
  NT_ATTR_RISE,
  NT_ATTR_SHAPE,
  NT_ATTR_LETTER_SPACING,
  NT_ATTR_UNDERLINE_COLOR,
  NT_ATTR_STRIKETHROUGH_COLOR,
  NT_ATTR_FOREGROUND_ALPHA,
  NT_ATTR_BACKGROUND_ALPHA,
  NT_ATTR_ALLOW_BREAKS,
  NT_ATTR_INSERT_HYPHENS,
  NT_ATTR_OVERLINE,
  NT_ATTR_OVERLINE_COLOR,
  NT_ATTR_WORD_SPACING,
  NT_ATTR_FONT_FEATURES,
  NT_ATTR_N_TYPES
} NtAttrType;

typedef struct
{
  uint16_t red, green, blue;
} NtColor;

/* Every attribute is one of these; type says which member is set. */
typedef struct
{
  NsPangoAttribute base;
  union
  {
    int                     i;
    NtColor                 color;
    char                   *str;   /* family, features */
    NsPangoLanguage        *lang;
    NsPangoFontDescription *desc;
    struct
    {
      NsPangoRectangle ink, logical;
    } shape;
  } v;
} NtAttr;

/* Sorted by start index, as Pango keeps them (inserted after the last with
 * the same start). */
struct _PangoAttrList
{
  int      ref_count;
  NtAttr **attrs;
  int      n, cap;
};

NtAttr *nt_attr_copy  (const NtAttr *attr);
int     nt_attr_equal (const NtAttr *a, const NtAttr *b);

/* Walks the attribute list in ranges where the set of applying attributes
 * stays the same, as Pango's attribute iterator. */
typedef struct
{
  const NsPangoAttrList *list;
  int       next_attr;  /* next attribute (by start) not yet open */
  NtAttr  **open;       /* attributes covering [start, end) */
  int       n_open, cap_open;
  unsigned  start, end;
} NtAttrIter;

void    nt_attr_iter_init    (NtAttrIter *it, const NsPangoAttrList *list);
void    nt_attr_iter_destroy (NtAttrIter *it);
int     nt_attr_iter_next    (NtAttrIter *it);
/* The last opened (innermost) attribute of a type, or NULL. */
NtAttr *nt_attr_iter_get     (NtAttrIter *it, NtAttrType type);
/* Merges the font attributes into desc (which it owns), as Pango's
 * get_font: the language is returned separately. */
void    nt_attr_iter_get_font (NtAttrIter *it, NsPangoFontDescription *desc,
                               NsPangoLanguage **language);

/* ---- font descriptions ------------------------------------------------- */

enum
{
  NT_DESC_FAMILY     = 1 << 0,
  NT_DESC_STYLE      = 1 << 1,
  NT_DESC_VARIANT    = 1 << 2,
  NT_DESC_WEIGHT     = 1 << 3,
  NT_DESC_STRETCH    = 1 << 4,
  NT_DESC_SIZE       = 1 << 5,
  NT_DESC_VARIATIONS = 1 << 6
};

struct _PangoFontDescription
{
  char          *family;      /* comma-separated list, owned */
  NsPangoStyle   style;
  NsPangoVariant variant;
  int            weight;
  NsPangoStretch stretch;
  int            size;        /* Pango units */
  int            size_is_absolute;
  char          *variations;  /* owned */
  int            color;       /* FcFalse, FcTrue or FcDontCare (emoji) */
  unsigned       mask;
};

void nt_font_description_merge (NsPangoFontDescription *desc,
                                const NsPangoFontDescription *merge,
                                int replace_existing);
int  nt_font_description_equal (const NsPangoFontDescription *a,
                                const NsPangoFontDescription *b);
uint32_t nt_font_description_hash (const NsPangoFontDescription *d);

/* ---- tabs -------------------------------------------------------------- */

typedef struct
{
  NsPangoTabAlign align;
  int             location;
} NtTab;

struct _PangoTabArray
{
  NtTab *tabs;
  int    n;
  int    in_pixels;
};

/* ---- fonts ------------------------------------------------------------- */

typedef struct _NtFont    NtFont;
typedef struct _NtFontset NtFontset;

/* Pango's metrics, all in Pango units. */
struct _PangoFontMetrics
{
  int ref_count;
  int ascent, descent, height;
  int approximate_char_width, approximate_digit_width;
  int underline_position, underline_thickness;
  int strikethrough_position, strikethrough_thickness;
};

NsPangoFontMetrics *nt_font_metrics_new (void);

struct _PangoFontFamily
{
  char *name;
};

struct _PangoFontMap
{
  unsigned int       serial;
  double             dpi;
  struct NtHash     *fontsets;   /* key -> NtFontset */
  struct NtHash     *fonts;      /* key -> NtFont */
  NsPangoFontFamily **families;
  int                 n_families;
  FcFontSet          *all_fonts; /* the SFNT fonts of both of fontconfig's sets */
  struct NtHash      *faces;     /* file#index -> hb_face_t */
  NsMutex             lock;
};

/* A font: a face at a pixel size, as fontconfig rendered its pattern. */
struct _NtFont
{
  int             ref_count;
  NsPangoFontMap *fontmap;     /* not referenced: fonts die with the map */
  char           *key;
  FcPattern      *pattern;
  FcCharSet      *charset;     /* the pattern's, not owned */
  hb_face_t      *hb_face;
  hb_font_t      *hb_font;
  double          size;        /* the em in pixels */
  double          ctm_sx, ctm_sy; /* the context's scale it is hinted for */
  int             embolden;
  int             has_shape;
  FT_Matrix       shape;       /* fontconfig's matrix (synthetic oblique) */
  double          slant;
  int             is_color;
  FT_Int32        load_flags;

  NsMutex         lock;        /* the FreeType face and the caches */
  FT_Face         face;
  int             face_failed;
  FT_Size         metric_size;
  double          metric_scale;
  FT_Size         render_size;
  FT_F26Dot6      render_px;
  double          render_scale;

  int              extents_ready;
  NsPangoRectangle font_extents; /* the logical box of a glyph */
  struct NtGlyphExtents *extents_cache;

  NsPangoFontMetrics *metrics;   /* language independent here */
  struct NtHexBox    *hexbox;
  struct NtHash      *images;
  size_t              image_bytes;
};

NtFont *nt_font_ref   (NtFont *font);
void    nt_font_unref (NtFont *font);
int     nt_font_has_char (NtFont *font, nt_unichar wc);
/* The glyph's ink and logical rectangles, Pango units, y down. */
void    nt_font_get_glyph_extents (NtFont *font, uint32_t glyph,
                                   NsPangoRectangle *ink, NsPangoRectangle *logical);
NsPangoFontMetrics *nt_font_get_metrics (NtFont *font);
double  nt_font_get_size (NtFont *font);

/* Fontsets: the fonts FcFontSort orders for a description and language. */
NtFontset *nt_font_map_load_fontset (NsPangoFontMap *fontmap,
                                     const NsPangoFontDescription *desc,
                                     NsPangoLanguage *language);
void       nt_fontset_unref   (NtFontset *fontset);
NtFontset *nt_fontset_ref     (NtFontset *fontset);
/* The i-th font (loaded on demand), NULL past the end. */
NtFont    *nt_fontset_get_font_at (NtFontset *fontset, int i);
/* The first font covering wc and its position; the first font if none. */
NtFont    *nt_fontset_get_font (NtFontset *fontset, nt_unichar wc, int *position);
NtFont    *nt_font_map_load_font (NsPangoFontMap *fontmap,
                                  const NsPangoFontDescription *desc,
                                  NsPangoLanguage *language);

/* Glyph flags, as Pango's. */
#define NT_GLYPH_EMPTY         0x0FFFFFFFu
#define NT_GLYPH_INVALID_INPUT 0xFFFFFFFFu
#define NT_GLYPH_UNKNOWN_FLAG  0x10000000u
#define NT_GET_UNKNOWN_GLYPH(wc) ((uint32_t) (wc) | NT_GLYPH_UNKNOWN_FLAG)

/* ---- rasterizing ------------------------------------------------------- */

typedef struct
{
  gfx_surface_t *surface; /* referenced; NULL for a blank glyph */
  int            x, y;
  int            color;
  double         scale;
} NtGlyphImage;

int  nt_font_device_size   (NtFont *font, const gfx_matrix_t *user_to_device,
                            double *device_size);
int  nt_font_get_glyph_image (NtFont *font, uint32_t glyph, double device_size,
                              int xphase, int yphase, NtGlyphImage *image);
void nt_font_append_glyph_path (NtFont *font, gfx_t *g, uint32_t glyph,
                                double x, double y);

typedef struct NtHexBox
{
  NtFont *font;
  int     rows;
  double  digit_width, digit_height;
  double  pad_x, pad_y;
  double  line_width;
  double  box_descent, box_height;
} NtHexBox;

NtHexBox *nt_font_get_hex_box (NtFont *font);

/* ---- a small hash table ------------------------------------------------ */

typedef struct NtHash NtHash;

NtHash *nt_hash_new     (void (*free_value) (void *));
void    nt_hash_free    (NtHash *h);
void   *nt_hash_lookup  (NtHash *h, const char *key, size_t key_len);
/* Copies the key; replaces (and frees) an existing value. */
void    nt_hash_insert  (NtHash *h, const char *key, size_t key_len, void *value);
void    nt_hash_remove_all (NtHash *h);
/* Drops (and frees) the values keep() says no to; returns how many. */
size_t  nt_hash_filter  (NtHash *h, int (*keep) (void *value, void *user), void *user);
size_t  nt_hash_size    (NtHash *h);
uint32_t nt_hash_bytes  (const void *p, size_t n, uint32_t seed);

/* ---- contexts ---------------------------------------------------------- */

struct _PangoContext
{
  int                     ref_count;
  NsPangoFontMap         *fontmap;
  NsPangoFontDescription *font_desc;
  NsPangoLanguage        *language;
  NsPangoDirection        base_dir;
  double                  scale_x, scale_y; /* the matrix, for hinting */
  unsigned int            serial;
  unsigned int            fontmap_serial;
  NtHash                 *metrics;          /* desc+lang -> metrics */
};

void nt_context_check_fontmap (NsPangoContext *context);

/* ---- items, glyphs ----------------------------------------------------- */

typedef struct
{
  uint32_t glyph;
  int      width;
  int      x_offset, y_offset;
  unsigned is_cluster_start : 1;
  unsigned is_color         : 1;
} NtGlyph;

typedef struct
{
  int      num_glyphs;
  NtGlyph *glyphs;
  int     *log_clusters; /* byte offset in the item of each glyph's cluster */
  int      space;
} NtGlyphString;

NtGlyphString *nt_glyph_string_new  (void);
void           nt_glyph_string_free (NtGlyphString *gs);
void           nt_glyph_string_set_size (NtGlyphString *gs, int n);
NtGlyphString *nt_glyph_string_copy (const NtGlyphString *gs);
int            nt_glyph_string_get_width (const NtGlyphString *gs);
void           nt_glyph_string_extents (const NtGlyphString *gs, NtFont *font,
                                        NsPangoRectangle *ink, NsPangoRectangle *logical);
void           nt_glyph_string_extents_range (const NtGlyphString *gs, int start, int end,
                                              NtFont *font, NsPangoRectangle *ink,
                                              NsPangoRectangle *logical);

enum
{
  NT_ITEM_NEED_HYPHEN = 1 << 0,
  NT_ITEM_IS_ELLIPSIS = 1 << 1
};

typedef struct
{
  int              offset;     /* bytes, in the layout's text */
  int              length;     /* bytes */
  int              num_chars;
  int              char_offset;/* characters, in the layout's text */
  NtFont          *font;       /* referenced */
  uint8_t          level;      /* bidi embedding level */
  hb_script_t      script;
  NsPangoLanguage *language;
  unsigned         flags;
  NtAttr         **extra_attrs;/* copies, owned */
  int              n_extra;
} NtItem;

NtItem *nt_item_new   (void);
NtItem *nt_item_copy  (const NtItem *item);
void    nt_item_free  (NtItem *item);
/* Splits off the first split_index bytes (split_offset chars) into a new
 * item, which is returned; the item keeps the rest. */
NtItem *nt_item_split (NtItem *item, int split_index, int split_offset);
NtAttr *nt_item_find_attr (const NtItem *item, NtAttrType type);

/* Itemizes text[start, start + length) with the attributes that affect
 * fonts (and runs): a list of items in logical order. */
typedef struct
{
  NtItem **items;
  int      n, cap;
} NtItemList;

void nt_item_list_append (NtItemList *l, NtItem *item);
void nt_item_list_clear  (NtItemList *l);

void nt_itemize (NsPangoContext *context, NsPangoDirection base_dir,
                 const char *text, int start, int length,
                 const NsPangoAttrList *itemize_attrs,
                 const NsPangoFontDescription *desc,
                 NtItemList *out);
/* Attaches the copies of the other attributes (colours, decorations,
 * breaking, features) covering each item to it, splitting items where
 * they change. */
void nt_apply_attrs_to_items (NtItemList *items, const NsPangoAttrList *attrs);

/* Shapes the item's text (the paragraph around it is context). */
void nt_shape_item (const char *paragraph_text, int paragraph_length,
                    const NtItem *item, const NsPangoLogAttr *log_attrs,
                    NtGlyphString *glyphs);
/* Without rounding the positions to whole pixels (what Pango's
 * pango_shape_full gives, for the context's metrics). */
void nt_shape_item_full (const char *paragraph_text, int paragraph_length,
                         const NtItem *item, const NsPangoLogAttr *log_attrs,
                         int round_positions, NtGlyphString *glyphs);
void nt_shape_shape_attr (const char *text, const NtItem *item, const NtAttr *shape,
                          NtGlyphString *glyphs);
void nt_glyph_letter_space (const char *text, const NtItem *item,
                            const NsPangoLogAttr *log_attrs, int letter_spacing,
                            NtGlyphString *glyphs);
void nt_glyph_word_space (const char *text, const NtItem *item, int word_spacing,
                          NtGlyphString *glyphs);
/* Each character's share of the glyphs' width (clusters divided evenly). */
void nt_glyph_logical_widths (const NtGlyphString *gs, const char *text,
                              const NtItem *item, int *widths);
void nt_glyph_index_to_x (const NtGlyphString *gs, const char *text, const NtItem *item,
                          const NsPangoLogAttr *log_attrs, int index, int trailing,
                          int *x_pos);
void nt_glyph_x_to_index (const NtGlyphString *gs, const char *text, const NtItem *item,
                          int x_pos, int *index, int *trailing);

void nt_shape_cache_stats (uint64_t *hits, uint64_t *misses, uint64_t *skips,
                          uint64_t *entries);

/* ---- breaking ---------------------------------------------------------- */

void nt_default_break (const char *text, int length, NsPangoLogAttr *attrs, int attrs_len);
void nt_tailor_break  (const char *text, int length, hb_script_t script,
                       NsPangoLanguage *language, NsPangoLogAttr *attrs, int attrs_len);
/* allow_breaks and insert_hyphens attributes, offset the paragraph's start. */
void nt_attr_break    (const char *text, int length, const NsPangoAttrList *attrs,
                       int offset, NsPangoLogAttr *log_attrs, int attrs_len);

/* ---- bidi -------------------------------------------------------------- */

NsPangoDirection nt_find_base_dir (const char *text, int length);
void nt_find_paragraph_boundary (const char *text, int length,
                                 int *delimiter_index, int *next_paragraph_start);

/* ---- emoji runs -------------------------------------------------------- */

typedef struct
{
  const char *text_start, *text_end;
  const char *start, *end;
  int         is_emoji;
  int         has_vs;
  unsigned char *types;
  unsigned char  types_[64];
  unsigned int   n_chars;
  unsigned int   cursor;
} NtEmojiIter;

NtEmojiIter *nt_emoji_iter_init (NtEmojiIter *iter, const char *text, int length,
                                 unsigned int n_chars);
int  nt_emoji_iter_next (NtEmojiIter *iter);
void nt_emoji_iter_fini (NtEmojiIter *iter);
int  nt_is_emoji_base_character (nt_unichar c);

/* ---- script runs ------------------------------------------------------- */

typedef struct
{
  const char *text_start, *text_end;
  const char *script_start, *script_end;
  hb_script_t script_code;
  struct { int pair_index; hb_script_t script_code; } paren_stack[128];
  int paren_sp;
} NtScriptIter;

void nt_script_iter_init (NtScriptIter *iter, const char *text, int length);
int  nt_script_iter_next (NtScriptIter *iter);

#endif /* NS_TEXT_PRIVATE_H */
