/* Weststar SDL Browser — nstext, the text layout library without Pango.
 *
 * The part of Pango's API the engine calls (under the ns_pango_* names of
 * the Pango fork it replaced, web-text-lib), implemented on HarfBuzz, FriBidi and
 * FreeType with fonts from fontconfig or nsfc and glyphs drawn with gfx.
 * There is no GObject and no GLib here: the types are plain C, and a
 * gboolean argument is an int as GLib's is. See docs/text-layout.md.
 *
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_TEXT_H
#define NS_TEXT_H

#include <stdint.h>
#include <stdio.h>
#include <gfx.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NS_PANGO_SCALE 1024
#define NS_PANGO_PIXELS(d) (((int) (d) + 512) >> 10)
#define NS_PANGO_PIXELS_FLOOR(d) (((int) (d)) >> 10)
#define NS_PANGO_PIXELS_CEIL(d) (((int) (d) + 1023) >> 10)
#define NS_PANGO_UNITS_FLOOR(d) ((d) & ~(NS_PANGO_SCALE - 1))
#define NS_PANGO_UNITS_CEIL(d) (((d) + (NS_PANGO_SCALE - 1)) & ~(NS_PANGO_SCALE - 1))
#define NS_PANGO_UNITS_ROUND(d) (((d) + (NS_PANGO_SCALE >> 1)) & ~(NS_PANGO_SCALE - 1))

/* ---- plain types ------------------------------------------------------ */

typedef struct
{
  int x, y, width, height;
} NsPangoRectangle;

/* Per character (and one past the last), as Pango's: the bit order is the
 * same. */
typedef struct
{
  unsigned int is_line_break               : 1;
  unsigned int is_mandatory_break          : 1;
  unsigned int is_char_break               : 1;
  unsigned int is_white                    : 1;
  unsigned int is_cursor_position          : 1;
  unsigned int is_word_start               : 1;
  unsigned int is_word_end                 : 1;
  unsigned int is_sentence_boundary        : 1;
  unsigned int is_sentence_start           : 1;
  unsigned int is_sentence_end             : 1;
  unsigned int backspace_deletes_character : 1;
  unsigned int is_expandable_space         : 1;
  unsigned int is_word_boundary            : 1;
  unsigned int break_inserts_hyphen        : 1;
  unsigned int break_removes_preceding     : 1;
  unsigned int reserved                    : 17;
} NsPangoLogAttr;

typedef enum
{
  NS_PANGO_STYLE_NORMAL,
  NS_PANGO_STYLE_OBLIQUE,
  NS_PANGO_STYLE_ITALIC
} NsPangoStyle;

typedef enum
{
  NS_PANGO_VARIANT_NORMAL,
  NS_PANGO_VARIANT_SMALL_CAPS,
  NS_PANGO_VARIANT_ALL_SMALL_CAPS,
  NS_PANGO_VARIANT_PETITE_CAPS,
  NS_PANGO_VARIANT_ALL_PETITE_CAPS,
  NS_PANGO_VARIANT_UNICASE,
  NS_PANGO_VARIANT_TITLE_CAPS
} NsPangoVariant;

typedef enum
{
  NS_PANGO_WEIGHT_THIN = 100,
  NS_PANGO_WEIGHT_ULTRALIGHT = 200,
  NS_PANGO_WEIGHT_LIGHT = 300,
  NS_PANGO_WEIGHT_SEMILIGHT = 350,
  NS_PANGO_WEIGHT_BOOK = 380,
  NS_PANGO_WEIGHT_NORMAL = 400,
  NS_PANGO_WEIGHT_MEDIUM = 500,
  NS_PANGO_WEIGHT_SEMIBOLD = 600,
  NS_PANGO_WEIGHT_BOLD = 700,
  NS_PANGO_WEIGHT_ULTRABOLD = 800,
  NS_PANGO_WEIGHT_HEAVY = 900,
  NS_PANGO_WEIGHT_ULTRAHEAVY = 1000
} NsPangoWeight;

typedef enum
{
  NS_PANGO_STRETCH_ULTRA_CONDENSED,
  NS_PANGO_STRETCH_EXTRA_CONDENSED,
  NS_PANGO_STRETCH_CONDENSED,
  NS_PANGO_STRETCH_SEMI_CONDENSED,
  NS_PANGO_STRETCH_NORMAL,
  NS_PANGO_STRETCH_SEMI_EXPANDED,
  NS_PANGO_STRETCH_EXPANDED,
  NS_PANGO_STRETCH_EXTRA_EXPANDED,
  NS_PANGO_STRETCH_ULTRA_EXPANDED
} NsPangoStretch;

typedef enum
{
  NS_PANGO_UNDERLINE_NONE,
  NS_PANGO_UNDERLINE_SINGLE,
  NS_PANGO_UNDERLINE_DOUBLE,
  NS_PANGO_UNDERLINE_LOW,
  NS_PANGO_UNDERLINE_ERROR,
  NS_PANGO_UNDERLINE_SINGLE_LINE,
  NS_PANGO_UNDERLINE_DOUBLE_LINE,
  NS_PANGO_UNDERLINE_ERROR_LINE
} NsPangoUnderline;

typedef enum
{
  NS_PANGO_OVERLINE_NONE,
  NS_PANGO_OVERLINE_SINGLE
} NsPangoOverline;

typedef enum
{
  NS_PANGO_ALIGN_LEFT,
  NS_PANGO_ALIGN_CENTER,
  NS_PANGO_ALIGN_RIGHT
} NsPangoAlignment;

typedef enum
{
  NS_PANGO_WRAP_WORD,
  NS_PANGO_WRAP_CHAR,
  NS_PANGO_WRAP_WORD_CHAR,
  NS_PANGO_WRAP_NONE
} NsPangoWrapMode;

typedef enum
{
  NS_PANGO_ELLIPSIZE_NONE,
  NS_PANGO_ELLIPSIZE_START,
  NS_PANGO_ELLIPSIZE_MIDDLE,
  NS_PANGO_ELLIPSIZE_END
} NsPangoEllipsizeMode;

typedef enum
{
  NS_PANGO_DIRECTION_LTR,
  NS_PANGO_DIRECTION_RTL,
  NS_PANGO_DIRECTION_TTB_LTR,
  NS_PANGO_DIRECTION_TTB_RTL,
  NS_PANGO_DIRECTION_WEAK_LTR,
  NS_PANGO_DIRECTION_WEAK_RTL,
  NS_PANGO_DIRECTION_NEUTRAL
} NsPangoDirection;

typedef enum
{
  NS_PANGO_TAB_LEFT,
  NS_PANGO_TAB_RIGHT,
  NS_PANGO_TAB_CENTER,
  NS_PANGO_TAB_DECIMAL
} NsPangoTabAlign;

/* ---- opaque types ----------------------------------------------------- */

typedef struct _PangoFontDescription NsPangoFontDescription;
typedef struct _PangoAttrList        NsPangoAttrList;
typedef struct _PangoLanguage        NsPangoLanguage;
typedef struct _PangoTabArray        NsPangoTabArray;
typedef struct _PangoContext         NsPangoContext;
typedef struct _PangoFontMap         NsPangoFontMap;
typedef struct _PangoFontFamily      NsPangoFontFamily;
typedef struct _PangoFontMetrics     NsPangoFontMetrics;
typedef struct _PangoLayout          NsPangoLayout;
typedef struct _PangoLayoutIter      NsPangoLayoutIter;

/* The engine sets an attribute's byte range itself. */
typedef struct _PangoAttribute
{
  int          type;
  unsigned int start_index;
  unsigned int end_index;
} NsPangoAttribute;

/* A line's start and length (bytes) are read by the engine; the rest of a
 * line is the library's. */
typedef struct _PangoLayoutLine
{
  NsPangoLayout *layout;
  int            start_index;
  int            length;
  unsigned int   is_paragraph_start : 1;
  unsigned int   resolved_dir       : 3;
} NsPangoLayoutLine;

/* There is one font map type: the fontconfig (or nsfc) one. */
typedef NsPangoFontMap NsPangoFcFontMap;
#define NS_PANGO_IS_FC_FONT_MAP(fm) ((fm) != NULL)
#define NS_PANGO_FC_FONT_MAP(fm) ((NsPangoFcFontMap *) (fm))

/* ---- languages, font descriptions ------------------------------------- */

NsPangoLanguage *ns_pango_language_from_string (const char *language);
const char      *ns_pango_language_to_string   (NsPangoLanguage *language);
NsPangoLanguage *ns_pango_language_get_default (void);

NsPangoFontDescription *ns_pango_font_description_new         (void);
NsPangoFontDescription *ns_pango_font_description_copy        (const NsPangoFontDescription *desc);
void                    ns_pango_font_description_free        (NsPangoFontDescription *desc);
NsPangoFontDescription *ns_pango_font_description_from_string (const char *str);
void ns_pango_font_description_set_family        (NsPangoFontDescription *desc, const char *family);
void ns_pango_font_description_set_family_static (NsPangoFontDescription *desc, const char *family);
void ns_pango_font_description_set_style         (NsPangoFontDescription *desc, NsPangoStyle style);
void ns_pango_font_description_set_weight        (NsPangoFontDescription *desc, NsPangoWeight weight);
void ns_pango_font_description_set_stretch       (NsPangoFontDescription *desc, NsPangoStretch stretch);
void ns_pango_font_description_set_variant       (NsPangoFontDescription *desc, NsPangoVariant variant);
void ns_pango_font_description_set_absolute_size (NsPangoFontDescription *desc, double size);
void ns_pango_font_description_set_size          (NsPangoFontDescription *desc, int size);
void ns_pango_font_description_set_variations    (NsPangoFontDescription *desc, const char *variations);
const char *ns_pango_font_description_get_family (const NsPangoFontDescription *desc);
int  ns_pango_font_description_get_size          (const NsPangoFontDescription *desc);

/* ---- attributes ------------------------------------------------------- */

NsPangoAttribute *ns_pango_attr_family_new         (const char *family);
NsPangoAttribute *ns_pango_attr_font_desc_new      (const NsPangoFontDescription *desc);
NsPangoAttribute *ns_pango_attr_size_new_absolute  (int size);
NsPangoAttribute *ns_pango_attr_weight_new         (NsPangoWeight weight);
NsPangoAttribute *ns_pango_attr_style_new          (NsPangoStyle style);
NsPangoAttribute *ns_pango_attr_stretch_new        (NsPangoStretch stretch);
NsPangoAttribute *ns_pango_attr_variant_new        (NsPangoVariant variant);
NsPangoAttribute *ns_pango_attr_font_features_new  (const char *features);
NsPangoAttribute *ns_pango_attr_language_new       (NsPangoLanguage *language);
NsPangoAttribute *ns_pango_attr_foreground_new     (uint16_t red, uint16_t green, uint16_t blue);
NsPangoAttribute *ns_pango_attr_background_new     (uint16_t red, uint16_t green, uint16_t blue);
NsPangoAttribute *ns_pango_attr_foreground_alpha_new (uint16_t alpha);
NsPangoAttribute *ns_pango_attr_background_alpha_new (uint16_t alpha);
NsPangoAttribute *ns_pango_attr_underline_new      (NsPangoUnderline underline);
NsPangoAttribute *ns_pango_attr_underline_color_new (uint16_t red, uint16_t green, uint16_t blue);
NsPangoAttribute *ns_pango_attr_overline_new       (NsPangoOverline overline);
NsPangoAttribute *ns_pango_attr_overline_color_new (uint16_t red, uint16_t green, uint16_t blue);
NsPangoAttribute *ns_pango_attr_strikethrough_new  (int strikethrough);
NsPangoAttribute *ns_pango_attr_strikethrough_color_new (uint16_t red, uint16_t green, uint16_t blue);
NsPangoAttribute *ns_pango_attr_rise_new           (int rise);
NsPangoAttribute *ns_pango_attr_letter_spacing_new (int letter_spacing);
NsPangoAttribute *ns_pango_attr_word_spacing_new   (int word_spacing);
NsPangoAttribute *ns_pango_attr_shape_new          (const NsPangoRectangle *ink_rect,
                                                    const NsPangoRectangle *logical_rect);
NsPangoAttribute *ns_pango_attr_allow_breaks_new   (int allow_breaks);
NsPangoAttribute *ns_pango_attr_insert_hyphens_new (int insert_hyphens);
void              ns_pango_attribute_destroy       (NsPangoAttribute *attr);

NsPangoAttrList *ns_pango_attr_list_new    (void);
NsPangoAttrList *ns_pango_attr_list_ref    (NsPangoAttrList *list);
void             ns_pango_attr_list_unref  (NsPangoAttrList *list);
void             ns_pango_attr_list_insert (NsPangoAttrList *list, NsPangoAttribute *attr);

/* ---- tabs ------------------------------------------------------------- */

NsPangoTabArray *ns_pango_tab_array_new     (int initial_size, int positions_in_pixels);
void             ns_pango_tab_array_free    (NsPangoTabArray *tabs);
void             ns_pango_tab_array_set_tab (NsPangoTabArray *tabs, int tab_index,
                                             NsPangoTabAlign alignment, int location);

/* ---- fonts ------------------------------------------------------------ */

/* The font map of the calling thread, made on first use. */
NsPangoFontMap *ns_pango_gfx_font_map_get_default (void);
unsigned int    ns_pango_font_map_get_serial      (NsPangoFontMap *fontmap);
/* The families, in a newly allocated array the caller g_free()s (or
 * free()s: it is malloc'ed); the families stay the font map's. */
void            ns_pango_font_map_list_families   (NsPangoFontMap *fontmap,
                                                   NsPangoFontFamily ***families,
                                                   int *n_families);
const char     *ns_pango_font_family_get_name     (NsPangoFontFamily *family);
/* fontconfig's fonts changed (a web font was added): fontsets are rebuilt
 * and the serial moves on. */
void            ns_pango_fc_font_map_config_changed (NsPangoFcFontMap *fontmap);

int  ns_pango_font_metrics_get_ascent               (NsPangoFontMetrics *metrics);
int  ns_pango_font_metrics_get_descent              (NsPangoFontMetrics *metrics);
int  ns_pango_font_metrics_get_height               (NsPangoFontMetrics *metrics);
int  ns_pango_font_metrics_get_approximate_char_width (NsPangoFontMetrics *metrics);
int  ns_pango_font_metrics_get_approximate_digit_width (NsPangoFontMetrics *metrics);
int  ns_pango_font_metrics_get_underline_position   (NsPangoFontMetrics *metrics);
int  ns_pango_font_metrics_get_underline_thickness  (NsPangoFontMetrics *metrics);
int  ns_pango_font_metrics_get_strikethrough_position  (NsPangoFontMetrics *metrics);
int  ns_pango_font_metrics_get_strikethrough_thickness (NsPangoFontMetrics *metrics);
NsPangoFontMetrics *ns_pango_font_metrics_ref       (NsPangoFontMetrics *metrics);
void ns_pango_font_metrics_unref                    (NsPangoFontMetrics *metrics);

/* ---- contexts --------------------------------------------------------- */

NsPangoContext *ns_pango_gfx_create_context (gfx_t *g);
void            ns_pango_gfx_update_context (gfx_t *g, NsPangoContext *context);
NsPangoLayout  *ns_pango_gfx_create_layout  (gfx_t *g);
NsPangoContext *ns_pango_context_ref        (NsPangoContext *context);
void            ns_pango_context_unref      (NsPangoContext *context);
void            ns_pango_context_set_base_dir (NsPangoContext *context, NsPangoDirection direction);
NsPangoDirection ns_pango_context_get_base_dir (NsPangoContext *context);
NsPangoFontDescription *ns_pango_context_get_font_description (NsPangoContext *context);
NsPangoFontMetrics *ns_pango_context_get_metrics (NsPangoContext *context,
                                                  const NsPangoFontDescription *desc,
                                                  NsPangoLanguage *language);

/* ---- layouts ---------------------------------------------------------- */

NsPangoLayout *ns_pango_layout_new   (NsPangoContext *context);
NsPangoLayout *ns_pango_layout_ref   (NsPangoLayout *layout);
void           ns_pango_layout_unref (NsPangoLayout *layout);
NsPangoContext *ns_pango_layout_get_context (NsPangoLayout *layout);
void ns_pango_layout_context_changed (NsPangoLayout *layout);

void ns_pango_layout_set_text        (NsPangoLayout *layout, const char *text, int length);
const char *ns_pango_layout_get_text (NsPangoLayout *layout);
void ns_pango_layout_set_attributes  (NsPangoLayout *layout, NsPangoAttrList *attrs);
void ns_pango_layout_set_font_description (NsPangoLayout *layout,
                                           const NsPangoFontDescription *desc);
const NsPangoFontDescription *ns_pango_layout_get_font_description (NsPangoLayout *layout);
void ns_pango_layout_set_width       (NsPangoLayout *layout, int width);
int  ns_pango_layout_get_width       (NsPangoLayout *layout);
void ns_pango_layout_set_height      (NsPangoLayout *layout, int height);
void ns_pango_layout_set_wrap        (NsPangoLayout *layout, NsPangoWrapMode wrap);
void ns_pango_layout_set_ellipsize   (NsPangoLayout *layout, NsPangoEllipsizeMode ellipsize);
void ns_pango_layout_set_indent      (NsPangoLayout *layout, int indent);
void ns_pango_layout_set_line_spacing (NsPangoLayout *layout, float factor);
void ns_pango_layout_set_justify     (NsPangoLayout *layout, int justify);
int  ns_pango_layout_get_justify     (NsPangoLayout *layout);
void ns_pango_layout_set_alignment   (NsPangoLayout *layout, NsPangoAlignment alignment);
NsPangoAlignment ns_pango_layout_get_alignment (NsPangoLayout *layout);
void ns_pango_layout_set_auto_dir    (NsPangoLayout *layout, int auto_dir);
void ns_pango_layout_set_tabs        (NsPangoLayout *layout, NsPangoTabArray *tabs);

void ns_pango_layout_get_extents       (NsPangoLayout *layout, NsPangoRectangle *ink_rect,
                                        NsPangoRectangle *logical_rect);
void ns_pango_layout_get_pixel_extents (NsPangoLayout *layout, NsPangoRectangle *ink_rect,
                                        NsPangoRectangle *logical_rect);
void ns_pango_layout_get_size          (NsPangoLayout *layout, int *width, int *height);
void ns_pango_layout_get_pixel_size    (NsPangoLayout *layout, int *width, int *height);
int  ns_pango_layout_get_baseline      (NsPangoLayout *layout);
int  ns_pango_layout_get_line_count    (NsPangoLayout *layout);
NsPangoLayoutLine *ns_pango_layout_get_line_readonly (NsPangoLayout *layout, int line);
const NsPangoLogAttr *ns_pango_layout_get_log_attrs_readonly (NsPangoLayout *layout,
                                                              int *n_attrs);

void ns_pango_layout_index_to_pos    (NsPangoLayout *layout, int index, NsPangoRectangle *pos);
void ns_pango_layout_index_to_line_x (NsPangoLayout *layout, int index, int trailing,
                                      int *line, int *x_pos);
int  ns_pango_layout_xy_to_index     (NsPangoLayout *layout, int x, int y,
                                      int *index, int *trailing);

void ns_pango_layout_line_get_extents  (NsPangoLayoutLine *line, NsPangoRectangle *ink_rect,
                                        NsPangoRectangle *logical_rect);
void ns_pango_layout_line_index_to_x   (NsPangoLayoutLine *line, int index, int trailing,
                                        int *x_pos);
int  ns_pango_layout_line_x_to_index   (NsPangoLayoutLine *line, int x_pos,
                                        int *index, int *trailing);
/* *ranges is malloc'ed: g_free() or free() it. */
void ns_pango_layout_line_get_x_ranges (NsPangoLayoutLine *line, int start_index, int end_index,
                                        int **ranges, int *n_ranges);

NsPangoLayoutIter *ns_pango_layout_get_iter (NsPangoLayout *layout);
void ns_pango_layout_iter_free              (NsPangoLayoutIter *iter);
int  ns_pango_layout_iter_next_line         (NsPangoLayoutIter *iter);
NsPangoLayoutLine *ns_pango_layout_iter_get_line_readonly (NsPangoLayoutIter *iter);
void ns_pango_layout_iter_get_line_extents  (NsPangoLayoutIter *iter, NsPangoRectangle *ink_rect,
                                             NsPangoRectangle *logical_rect);
int  ns_pango_layout_iter_get_baseline      (NsPangoLayoutIter *iter);

/* ---- breaking --------------------------------------------------------- */

/* Log attrs for a paragraph of text (length bytes, or -1), attrs having
 * room for its characters plus one; level is ignored, as Pango does. */
void ns_pango_get_log_attrs (const char *text, int length, int level,
                             NsPangoLanguage *language,
                             NsPangoLogAttr *attrs, int attrs_len);

/* ---- drawing ---------------------------------------------------------- */

/* A layout (its top-left corner) or a line (its baseline's left end) at
 * gfx's current point, or at the origin when there is none, in gfx's
 * current source and operator. */
void ns_pango_gfx_show_layout      (gfx_t *g, NsPangoLayout *layout);
void ns_pango_gfx_show_layout_line (gfx_t *g, NsPangoLayoutLine *line);
/* Appends the layout's glyph outlines to gfx's path. */
void ns_pango_gfx_layout_path      (gfx_t *g, NsPangoLayout *layout);

/* ---- diagnostics ------------------------------------------------------ */

const char *ns_pango_version_string (void);
void ns_pango_cache_get_stats   (uint64_t *hits, uint64_t *misses, uint64_t *skips,
                                 uint64_t *entries);
void ns_pango_break_cache_stats (uint64_t *hits, uint64_t *misses, uint64_t *skips,
                                 uint64_t *entries);
void ns_pango_item_cache_stats  (uint64_t *hits, uint64_t *misses, uint64_t *skips,
                                 uint64_t *entries);

#ifdef __cplusplus
}
#endif

#endif /* NS_TEXT_H */
