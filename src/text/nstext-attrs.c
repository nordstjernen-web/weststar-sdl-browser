/* Weststar SDL Browser — nstext: font descriptions, attributes, attribute
 * lists and their iterator, tab arrays.
 *
 * The semantics are Pango's (fonts.c, pango-attributes.c): attributes are
 * kept sorted by start, a later one of a type covering a range wins, and
 * a font description in an attribute fills only the fields not set by a
 * later attribute.
 *
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "nstext-private.h"

#include <math.h>
#include <stdio.h>

/* ---- font descriptions ------------------------------------------------- */

NsPangoFontDescription *
ns_pango_font_description_new (void)
{
  NsPangoFontDescription *d = nt_new0 (NsPangoFontDescription, 1);

  d->style = NS_PANGO_STYLE_NORMAL;
  d->variant = NS_PANGO_VARIANT_NORMAL;
  d->weight = NS_PANGO_WEIGHT_NORMAL;
  d->stretch = NS_PANGO_STRETCH_NORMAL;
  d->color = 2; /* FcDontCare */
  return d;
}

NsPangoFontDescription *
ns_pango_font_description_copy (const NsPangoFontDescription *desc)
{
  NsPangoFontDescription *d;

  if (!desc)
    return NULL;
  d = nt_new (NsPangoFontDescription, 1);
  *d = *desc;
  d->family = nt_strdup (desc->family);
  d->variations = nt_strdup (desc->variations);
  return d;
}

void
ns_pango_font_description_free (NsPangoFontDescription *desc)
{
  if (!desc)
    return;
  free (desc->family);
  free (desc->variations);
  free (desc);
}

void
ns_pango_font_description_set_family (NsPangoFontDescription *desc, const char *family)
{
  char *copy = nt_strdup (family);

  free (desc->family);
  desc->family = copy;
  if (family)
    desc->mask |= NT_DESC_FAMILY;
  else
    desc->mask &= ~(unsigned) NT_DESC_FAMILY;
}

void
ns_pango_font_description_set_family_static (NsPangoFontDescription *desc, const char *family)
{
  ns_pango_font_description_set_family (desc, family);
}

const char *
ns_pango_font_description_get_family (const NsPangoFontDescription *desc)
{
  return desc->family;
}

void
ns_pango_font_description_set_style (NsPangoFontDescription *desc, NsPangoStyle style)
{
  desc->style = style;
  desc->mask |= NT_DESC_STYLE;
}

void
ns_pango_font_description_set_weight (NsPangoFontDescription *desc, NsPangoWeight weight)
{
  desc->weight = weight;
  desc->mask |= NT_DESC_WEIGHT;
}

void
ns_pango_font_description_set_stretch (NsPangoFontDescription *desc, NsPangoStretch stretch)
{
  desc->stretch = stretch;
  desc->mask |= NT_DESC_STRETCH;
}

void
ns_pango_font_description_set_variant (NsPangoFontDescription *desc, NsPangoVariant variant)
{
  desc->variant = variant;
  desc->mask |= NT_DESC_VARIANT;
}

void
ns_pango_font_description_set_absolute_size (NsPangoFontDescription *desc, double size)
{
  desc->size = (int) lround (size);
  desc->size_is_absolute = TRUE;
  desc->mask |= NT_DESC_SIZE;
}

void
ns_pango_font_description_set_size (NsPangoFontDescription *desc, int size)
{
  desc->size = size;
  desc->size_is_absolute = FALSE;
  desc->mask |= NT_DESC_SIZE;
}

int
ns_pango_font_description_get_size (const NsPangoFontDescription *desc)
{
  return desc->size;
}

void
ns_pango_font_description_set_variations (NsPangoFontDescription *desc, const char *variations)
{
  char *copy = nt_strdup (variations);

  free (desc->variations);
  desc->variations = copy;
  if (variations)
    desc->mask |= NT_DESC_VARIATIONS;
  else
    desc->mask &= ~(unsigned) NT_DESC_VARIATIONS;
}

void
nt_font_description_merge (NsPangoFontDescription *desc,
                           const NsPangoFontDescription *merge,
                           int replace_existing)
{
  unsigned new_mask;

  if (!merge)
    return;
  new_mask = replace_existing ? merge->mask : merge->mask & ~desc->mask;

  if (new_mask & NT_DESC_FAMILY)
    ns_pango_font_description_set_family (desc, merge->family);
  if (new_mask & NT_DESC_STYLE)
    desc->style = merge->style;
  if (new_mask & NT_DESC_VARIANT)
    desc->variant = merge->variant;
  if (new_mask & NT_DESC_WEIGHT)
    desc->weight = merge->weight;
  if (new_mask & NT_DESC_STRETCH)
    desc->stretch = merge->stretch;
  if (new_mask & NT_DESC_SIZE)
    {
      desc->size = merge->size;
      desc->size_is_absolute = merge->size_is_absolute;
    }
  if (new_mask & NT_DESC_VARIATIONS)
    ns_pango_font_description_set_variations (desc, merge->variations);
  desc->mask |= new_mask;
}

static int
str_equal (const char *a, const char *b)
{
  return a == b || (a && b && strcmp (a, b) == 0);
}

int
nt_font_description_equal (const NsPangoFontDescription *a,
                           const NsPangoFontDescription *b)
{
  return a->style == b->style && a->variant == b->variant &&
         a->weight == b->weight && a->stretch == b->stretch &&
         a->size == b->size && a->size_is_absolute == b->size_is_absolute &&
         ((a->mask ^ b->mask) & (NT_DESC_FAMILY | NT_DESC_VARIATIONS)) == 0 &&
         (!(a->mask & NT_DESC_FAMILY) || (a->family && b->family &&
                                          nt_ascii_strcasecmp (a->family, b->family) == 0)) &&
         str_equal (a->variations, b->variations);
}

/* ---- parsing ("Family, List Style Words Size") ------------------------- */

typedef struct
{
  int         value;
  const char *name;
} FieldMap;

static const FieldMap style_map[] = {
  { NS_PANGO_STYLE_NORMAL, "Roman" },
  { NS_PANGO_STYLE_OBLIQUE, "Oblique" },
  { NS_PANGO_STYLE_ITALIC, "Italic" },
};

static const FieldMap variant_map[] = {
  { NS_PANGO_VARIANT_SMALL_CAPS, "Small-Caps" },
  { NS_PANGO_VARIANT_ALL_SMALL_CAPS, "All-Small-Caps" },
  { NS_PANGO_VARIANT_PETITE_CAPS, "Petite-Caps" },
  { NS_PANGO_VARIANT_ALL_PETITE_CAPS, "All-Petite-Caps" },
  { NS_PANGO_VARIANT_UNICASE, "Unicase" },
  { NS_PANGO_VARIANT_TITLE_CAPS, "Title-Caps" },
};

static const FieldMap weight_map[] = {
  { NS_PANGO_WEIGHT_THIN, "Thin" },
  { NS_PANGO_WEIGHT_ULTRALIGHT, "Ultra-Light" },
  { NS_PANGO_WEIGHT_ULTRALIGHT, "Extra-Light" },
  { NS_PANGO_WEIGHT_LIGHT, "Light" },
  { NS_PANGO_WEIGHT_SEMILIGHT, "Semi-Light" },
  { NS_PANGO_WEIGHT_SEMILIGHT, "Demi-Light" },
  { NS_PANGO_WEIGHT_BOOK, "Book" },
  { NS_PANGO_WEIGHT_NORMAL, "Regular" },
  { NS_PANGO_WEIGHT_MEDIUM, "Medium" },
  { NS_PANGO_WEIGHT_SEMIBOLD, "Semi-Bold" },
  { NS_PANGO_WEIGHT_SEMIBOLD, "Demi-Bold" },
  { NS_PANGO_WEIGHT_BOLD, "Bold" },
  { NS_PANGO_WEIGHT_ULTRABOLD, "Ultra-Bold" },
  { NS_PANGO_WEIGHT_ULTRABOLD, "Extra-Bold" },
  { NS_PANGO_WEIGHT_HEAVY, "Heavy" },
  { NS_PANGO_WEIGHT_HEAVY, "Black" },
  { NS_PANGO_WEIGHT_ULTRAHEAVY, "Ultra-Heavy" },
  { NS_PANGO_WEIGHT_ULTRAHEAVY, "Extra-Heavy" },
  { NS_PANGO_WEIGHT_ULTRAHEAVY, "Ultra-Black" },
  { NS_PANGO_WEIGHT_ULTRAHEAVY, "Extra-Black" },
};

static const FieldMap stretch_map[] = {
  { NS_PANGO_STRETCH_ULTRA_CONDENSED, "Ultra-Condensed" },
  { NS_PANGO_STRETCH_EXTRA_CONDENSED, "Extra-Condensed" },
  { NS_PANGO_STRETCH_CONDENSED, "Condensed" },
  { NS_PANGO_STRETCH_SEMI_CONDENSED, "Semi-Condensed" },
  { NS_PANGO_STRETCH_SEMI_EXPANDED, "Semi-Expanded" },
  { NS_PANGO_STRETCH_EXPANDED, "Expanded" },
  { NS_PANGO_STRETCH_EXTRA_EXPANDED, "Extra-Expanded" },
  { NS_PANGO_STRETCH_ULTRA_EXPANDED, "Ultra-Expanded" },
};

/* Words compare without case, '-' and ' ' alike ("semibold" too). */
static int
field_matches (const char *s1, const char *s2, size_t n)
{
  size_t i = 0;

  while (i < n && *s2)
    {
      char c1 = nt_ascii_tolower (s1[i]);
      char c2 = nt_ascii_tolower (*s2);

      if (c1 == c2)
        {
          i++;
          s2++;
        }
      else if (c2 == '-')
        s2++;
      else if (c1 == '-' || c1 == ' ')
        i++;
      else
        return FALSE;
    }
  return i == n && *s2 == '\0';
}

static int
find_in_map (const FieldMap *map, size_t n_map, const char *word, size_t len, int *value)
{
  size_t i;

  for (i = 0; i < n_map; i++)
    if (field_matches (word, map[i].name, len))
      {
        *value = map[i].value;
        return TRUE;
      }
  return FALSE;
}

static int
find_field_any (const char *word, size_t len, NsPangoFontDescription *desc)
{
  int v;

  if (field_matches (word, "Normal", len))
    return TRUE;
  if (find_in_map (style_map, NT_N_ELEMENTS (style_map), word, len, &v))
    {
      desc->style = (NsPangoStyle) v;
      return TRUE;
    }
  if (find_in_map (variant_map, NT_N_ELEMENTS (variant_map), word, len, &v))
    {
      desc->variant = (NsPangoVariant) v;
      return TRUE;
    }
  if (find_in_map (weight_map, NT_N_ELEMENTS (weight_map), word, len, &v))
    {
      desc->weight = v;
      return TRUE;
    }
  if (find_in_map (stretch_map, NT_N_ELEMENTS (stretch_map), word, len, &v))
    {
      desc->stretch = (NsPangoStretch) v;
      return TRUE;
    }
  /* "weight=650"-like numeric weights, as Pango. */
  if (len > 0 && word[0] >= '0' && word[0] <= '9')
    {
      size_t i;
      int w = 0;

      for (i = 0; i < len && word[i] >= '0' && word[i] <= '9'; i++)
        w = w * 10 + (word[i] - '0');
      if (i == len && w >= 100 && w <= 1000)
        {
          desc->weight = w;
          return TRUE;
        }
    }
  return FALSE;
}

/* The last word of [str, last), separated by spaces (or stop chars). */
static const char *
getword (const char *str, const char *last, size_t *wordlen, const char *stop)
{
  const char *result;

  while (last > str && (last[-1] == ' ' || last[-1] == '\t'))
    last--;
  result = last;
  while (result > str && result[-1] != ' ' && result[-1] != '\t' &&
         !strchr (stop, result[-1]))
    result--;
  *wordlen = (size_t) (last - result);
  return result;
}

static int
parse_size (const char *word, size_t len, int *size, int *is_absolute)
{
  char buf[32];
  char *end;
  double v;

  if (len == 0 || len >= sizeof buf)
    return FALSE;
  memcpy (buf, word, len);
  buf[len] = '\0';
  v = strtod (buf, &end);
  if (end == buf || v < 0 || v > 1000000)
    return FALSE;
  if (*end == '\0')
    *is_absolute = FALSE;
  else if (strcmp (end, "px") == 0)
    *is_absolute = TRUE;
  else
    return FALSE;
  *size = (int) (v * NS_PANGO_SCALE + 0.5);
  return TRUE;
}

NsPangoFontDescription *
ns_pango_font_description_from_string (const char *str)
{
  NsPangoFontDescription *desc = ns_pango_font_description_new ();
  const char *p, *last;
  size_t wordlen;

  if (!str)
    return desc;

  desc->mask = NT_DESC_STYLE | NT_DESC_WEIGHT | NT_DESC_VARIANT | NT_DESC_STRETCH;
  last = str + strlen (str);

  p = getword (str, last, &wordlen, ",");
  if (wordlen != 0)
    {
      int absolute;

      if (parse_size (p, wordlen, &desc->size, &absolute))
        {
          desc->size_is_absolute = absolute;
          desc->mask |= NT_DESC_SIZE;
          last = p;
        }
    }

  p = getword (str, last, &wordlen, ",");
  while (wordlen != 0)
    {
      if (!find_field_any (p, wordlen, desc))
        break;
      last = p;
      p = getword (str, last, &wordlen, ",");
    }

  while (last > str && (last[-1] == ' ' || last[-1] == '\t'))
    last--;
  if (last > str && last[-1] == ',')
    last--;
  while (str < last && (*str == ' ' || *str == '\t'))
    str++;

  if (last > str)
    {
      /* Each family trimmed, as Pango does. */
      NtString fam = { 0 };
      const char *q = str;

      while (q < last)
        {
          const char *e = memchr (q, ',', (size_t) (last - q));
          const char *a = q, *b;

          if (!e)
            e = last;
          b = e;
          while (a < b && (*a == ' ' || *a == '\t'))
            a++;
          while (b > a && (b[-1] == ' ' || b[-1] == '\t'))
            b--;
          if (b > a)
            {
              if (fam.len)
                nt_string_append_c (&fam, ',');
              nt_string_append_len (&fam, a, (size_t) (b - a));
            }
          q = e < last ? e + 1 : last;
        }
      if (fam.len)
        {
          desc->family = fam.str;
          desc->mask |= NT_DESC_FAMILY;
        }
      else
        free (fam.str);
    }

  return desc;
}

uint32_t
nt_font_description_hash (const NsPangoFontDescription *d)
{
  uint32_t h = 0;
  int v[6] = { d->style, d->variant, d->weight, d->stretch, d->size,
               d->size_is_absolute };

  h = nt_hash_bytes (v, sizeof v, h);
  if (d->family)
    h = nt_hash_bytes (d->family, strlen (d->family), h);
  if (d->variations)
    h = nt_hash_bytes (d->variations, strlen (d->variations), h);
  return h;
}

/* ---- attributes -------------------------------------------------------- */

static NtAttr *
attr_new (NtAttrType type)
{
  NtAttr *a = nt_new0 (NtAttr, 1);

  a->base.type = type;
  a->base.start_index = 0;
  a->base.end_index = (unsigned) -1;
  return a;
}

static NsPangoAttribute *
attr_int (NtAttrType type, int value)
{
  NtAttr *a = attr_new (type);

  a->v.i = value;
  return &a->base;
}

static NsPangoAttribute *
attr_color (NtAttrType type, uint16_t r, uint16_t g, uint16_t b)
{
  NtAttr *a = attr_new (type);

  a->v.color.red = r;
  a->v.color.green = g;
  a->v.color.blue = b;
  return &a->base;
}

static NsPangoAttribute *
attr_str (NtAttrType type, const char *s)
{
  NtAttr *a = attr_new (type);

  a->v.str = nt_strdup (s ? s : "");
  return &a->base;
}

NsPangoAttribute *ns_pango_attr_family_new (const char *family)
{ return attr_str (NT_ATTR_FAMILY, family); }
NsPangoAttribute *ns_pango_attr_font_features_new (const char *features)
{ return attr_str (NT_ATTR_FONT_FEATURES, features); }
NsPangoAttribute *ns_pango_attr_size_new_absolute (int size)
{ return attr_int (NT_ATTR_ABSOLUTE_SIZE, size); }
NsPangoAttribute *ns_pango_attr_weight_new (NsPangoWeight weight)
{ return attr_int (NT_ATTR_WEIGHT, weight); }
NsPangoAttribute *ns_pango_attr_style_new (NsPangoStyle style)
{ return attr_int (NT_ATTR_STYLE, style); }
NsPangoAttribute *ns_pango_attr_stretch_new (NsPangoStretch stretch)
{ return attr_int (NT_ATTR_STRETCH, stretch); }
NsPangoAttribute *ns_pango_attr_variant_new (NsPangoVariant variant)
{ return attr_int (NT_ATTR_VARIANT, variant); }
NsPangoAttribute *ns_pango_attr_foreground_new (uint16_t r, uint16_t g, uint16_t b)
{ return attr_color (NT_ATTR_FOREGROUND, r, g, b); }
NsPangoAttribute *ns_pango_attr_background_new (uint16_t r, uint16_t g, uint16_t b)
{ return attr_color (NT_ATTR_BACKGROUND, r, g, b); }
NsPangoAttribute *ns_pango_attr_foreground_alpha_new (uint16_t alpha)
{ return attr_int (NT_ATTR_FOREGROUND_ALPHA, alpha); }
NsPangoAttribute *ns_pango_attr_background_alpha_new (uint16_t alpha)
{ return attr_int (NT_ATTR_BACKGROUND_ALPHA, alpha); }
NsPangoAttribute *ns_pango_attr_underline_new (NsPangoUnderline underline)
{ return attr_int (NT_ATTR_UNDERLINE, underline); }
NsPangoAttribute *ns_pango_attr_underline_color_new (uint16_t r, uint16_t g, uint16_t b)
{ return attr_color (NT_ATTR_UNDERLINE_COLOR, r, g, b); }
NsPangoAttribute *ns_pango_attr_overline_new (NsPangoOverline overline)
{ return attr_int (NT_ATTR_OVERLINE, overline); }
NsPangoAttribute *ns_pango_attr_overline_color_new (uint16_t r, uint16_t g, uint16_t b)
{ return attr_color (NT_ATTR_OVERLINE_COLOR, r, g, b); }
NsPangoAttribute *ns_pango_attr_strikethrough_new (int strikethrough)
{ return attr_int (NT_ATTR_STRIKETHROUGH, strikethrough != 0); }
NsPangoAttribute *ns_pango_attr_strikethrough_color_new (uint16_t r, uint16_t g, uint16_t b)
{ return attr_color (NT_ATTR_STRIKETHROUGH_COLOR, r, g, b); }
NsPangoAttribute *ns_pango_attr_rise_new (int rise)
{ return attr_int (NT_ATTR_RISE, rise); }
NsPangoAttribute *ns_pango_attr_letter_spacing_new (int letter_spacing)
{ return attr_int (NT_ATTR_LETTER_SPACING, letter_spacing); }
NsPangoAttribute *ns_pango_attr_word_spacing_new (int word_spacing)
{ return attr_int (NT_ATTR_WORD_SPACING, word_spacing); }
NsPangoAttribute *ns_pango_attr_allow_breaks_new (int allow_breaks)
{ return attr_int (NT_ATTR_ALLOW_BREAKS, allow_breaks != 0); }
NsPangoAttribute *ns_pango_attr_insert_hyphens_new (int insert_hyphens)
{ return attr_int (NT_ATTR_INSERT_HYPHENS, insert_hyphens != 0); }

NsPangoAttribute *
ns_pango_attr_language_new (NsPangoLanguage *language)
{
  NtAttr *a = attr_new (NT_ATTR_LANGUAGE);

  a->v.lang = language;
  return &a->base;
}

NsPangoAttribute *
ns_pango_attr_font_desc_new (const NsPangoFontDescription *desc)
{
  NtAttr *a = attr_new (NT_ATTR_FONT_DESC);

  a->v.desc = ns_pango_font_description_copy (desc);
  return &a->base;
}

NsPangoAttribute *
ns_pango_attr_shape_new (const NsPangoRectangle *ink_rect,
                         const NsPangoRectangle *logical_rect)
{
  NtAttr *a = attr_new (NT_ATTR_SHAPE);

  a->v.shape.ink = *ink_rect;
  a->v.shape.logical = *logical_rect;
  return &a->base;
}

void
ns_pango_attribute_destroy (NsPangoAttribute *attr)
{
  NtAttr *a = (NtAttr *) (void *) attr;

  if (!a)
    return;
  switch ((NtAttrType) a->base.type)
    {
    case NT_ATTR_FAMILY:
    case NT_ATTR_FONT_FEATURES:
      free (a->v.str);
      break;
    case NT_ATTR_FONT_DESC:
      ns_pango_font_description_free (a->v.desc);
      break;
    default:
      break;
    }
  free (a);
}

NtAttr *
nt_attr_copy (const NtAttr *attr)
{
  NtAttr *a = nt_new (NtAttr, 1);

  *a = *attr;
  switch ((NtAttrType) a->base.type)
    {
    case NT_ATTR_FAMILY:
    case NT_ATTR_FONT_FEATURES:
      a->v.str = nt_strdup (attr->v.str);
      break;
    case NT_ATTR_FONT_DESC:
      a->v.desc = ns_pango_font_description_copy (attr->v.desc);
      break;
    default:
      break;
    }
  return a;
}

int
nt_attr_equal (const NtAttr *a, const NtAttr *b)
{
  if (a->base.type != b->base.type)
    return FALSE;
  switch ((NtAttrType) a->base.type)
    {
    case NT_ATTR_FAMILY:
    case NT_ATTR_FONT_FEATURES:
      return strcmp (a->v.str, b->v.str) == 0;
    case NT_ATTR_FONT_DESC:
      return nt_font_description_equal (a->v.desc, b->v.desc);
    case NT_ATTR_LANGUAGE:
      return a->v.lang == b->v.lang;
    case NT_ATTR_FOREGROUND:
    case NT_ATTR_BACKGROUND:
    case NT_ATTR_UNDERLINE_COLOR:
    case NT_ATTR_STRIKETHROUGH_COLOR:
    case NT_ATTR_OVERLINE_COLOR:
      return a->v.color.red == b->v.color.red &&
             a->v.color.green == b->v.color.green &&
             a->v.color.blue == b->v.color.blue;
    case NT_ATTR_SHAPE:
      return memcmp (&a->v.shape, &b->v.shape, sizeof a->v.shape) == 0;
    default:
      return a->v.i == b->v.i;
    }
}

/* ---- attribute lists --------------------------------------------------- */

NsPangoAttrList *
ns_pango_attr_list_new (void)
{
  NsPangoAttrList *l = nt_new0 (NsPangoAttrList, 1);

  l->ref_count = 1;
  return l;
}

NsPangoAttrList *
ns_pango_attr_list_ref (NsPangoAttrList *list)
{
  if (list)
    list->ref_count++;
  return list;
}

void
ns_pango_attr_list_unref (NsPangoAttrList *list)
{
  int i;

  if (!list || --list->ref_count > 0)
    return;
  for (i = 0; i < list->n; i++)
    ns_pango_attribute_destroy (&list->attrs[i]->base);
  free (list->attrs);
  free (list);
}

/* After every attribute starting at or before its start, as Pango. */
void
ns_pango_attr_list_insert (NsPangoAttrList *list, NsPangoAttribute *attr)
{
  int lo = 0, hi;

  if (!list || !attr)
    return;
  hi = list->n;
  while (lo < hi)
    {
      int mid = (lo + hi) / 2;

      if (list->attrs[mid]->base.start_index <= attr->start_index)
        lo = mid + 1;
      else
        hi = mid;
    }
  if (list->n == list->cap)
    {
      list->cap = list->cap ? list->cap * 2 : 8;
      list->attrs = nt_renew (NtAttr *, list->attrs, list->cap);
    }
  memmove (list->attrs + lo + 1, list->attrs + lo,
           (size_t) (list->n - lo) * sizeof *list->attrs);
  list->attrs[lo] = (NtAttr *) (void *) attr;
  list->n++;
}

/* ---- the iterator ------------------------------------------------------ */

static int
iter_advance (NtAttrIter *it)
{
  int i;
  unsigned end = (unsigned) -1;

  if (it->next_attr >= (it->list ? it->list->n : 0) && it->n_open == 0)
    return FALSE;

  it->start = it->end;

  for (i = 0; i < it->n_open; )
    if (it->open[i]->base.end_index == it->start)
      {
        memmove (it->open + i, it->open + i + 1,
                 (size_t) (it->n_open - i - 1) * sizeof *it->open);
        it->n_open--;
      }
    else
      i++;

  while (it->list && it->next_attr < it->list->n &&
         it->list->attrs[it->next_attr]->base.start_index == it->start)
    {
      NtAttr *a = it->list->attrs[it->next_attr++];

      if (a->base.end_index == it->start)
        continue; /* empty: never applies */
      if (it->n_open == it->cap_open)
        {
          it->cap_open = it->cap_open ? it->cap_open * 2 : 8;
          it->open = nt_renew (NtAttr *, it->open, it->cap_open);
        }
      it->open[it->n_open++] = a;
    }

  for (i = 0; i < it->n_open; i++)
    end = NT_MIN (end, it->open[i]->base.end_index);
  if (it->list && it->next_attr < it->list->n)
    end = NT_MIN (end, it->list->attrs[it->next_attr]->base.start_index);
  it->end = end;
  return TRUE;
}

void
nt_attr_iter_init (NtAttrIter *it, const NsPangoAttrList *list)
{
  memset (it, 0, sizeof *it);
  it->list = list;
  if (!iter_advance (it))
    it->end = (unsigned) -1;
}

void
nt_attr_iter_destroy (NtAttrIter *it)
{
  free (it->open);
  it->open = NULL;
}

int
nt_attr_iter_next (NtAttrIter *it)
{
  return iter_advance (it);
}

NtAttr *
nt_attr_iter_get (NtAttrIter *it, NtAttrType type)
{
  int i;

  for (i = it->n_open - 1; i >= 0; i--)
    if (it->open[i]->base.type == (int) type)
      return it->open[i];
  return NULL;
}

void
nt_attr_iter_get_font (NtAttrIter *it, NsPangoFontDescription *desc,
                       NsPangoLanguage **language)
{
  unsigned mask = 0;
  int have_language = FALSE;
  int i;

  if (language)
    *language = NULL;

  for (i = it->n_open - 1; i >= 0; i--)
    {
      const NtAttr *a = it->open[i];

      switch ((NtAttrType) a->base.type)
        {
        case NT_ATTR_FONT_DESC:
          {
            unsigned new_mask = a->v.desc->mask & ~mask;

            mask |= new_mask;
            /* What the attribute sets and no later attribute did. */
            {
              NsPangoFontDescription tmp = *a->v.desc;

              tmp.mask = new_mask;
              nt_font_description_merge (desc, &tmp, TRUE);
            }
          }
          break;
        case NT_ATTR_FAMILY:
          if (!(mask & NT_DESC_FAMILY))
            {
              mask |= NT_DESC_FAMILY;
              ns_pango_font_description_set_family (desc, a->v.str);
            }
          break;
        case NT_ATTR_STYLE:
          if (!(mask & NT_DESC_STYLE))
            {
              mask |= NT_DESC_STYLE;
              ns_pango_font_description_set_style (desc, (NsPangoStyle) a->v.i);
            }
          break;
        case NT_ATTR_VARIANT:
          if (!(mask & NT_DESC_VARIANT))
            {
              mask |= NT_DESC_VARIANT;
              ns_pango_font_description_set_variant (desc, (NsPangoVariant) a->v.i);
            }
          break;
        case NT_ATTR_WEIGHT:
          if (!(mask & NT_DESC_WEIGHT))
            {
              mask |= NT_DESC_WEIGHT;
              ns_pango_font_description_set_weight (desc, (NsPangoWeight) a->v.i);
            }
          break;
        case NT_ATTR_STRETCH:
          if (!(mask & NT_DESC_STRETCH))
            {
              mask |= NT_DESC_STRETCH;
              ns_pango_font_description_set_stretch (desc, (NsPangoStretch) a->v.i);
            }
          break;
        case NT_ATTR_ABSOLUTE_SIZE:
          if (!(mask & NT_DESC_SIZE))
            {
              mask |= NT_DESC_SIZE;
              ns_pango_font_description_set_absolute_size (desc, a->v.i);
            }
          break;
        case NT_ATTR_LANGUAGE:
          if (language && !have_language)
            {
              have_language = TRUE;
              *language = a->v.lang;
            }
          break;
        default:
          break;
        }
    }
}

/* ---- tabs -------------------------------------------------------------- */

NsPangoTabArray *
ns_pango_tab_array_new (int initial_size, int positions_in_pixels)
{
  NsPangoTabArray *t = nt_new0 (NsPangoTabArray, 1);

  t->n = initial_size > 0 ? initial_size : 0;
  t->tabs = nt_new0 (NtTab, t->n ? t->n : 1);
  t->in_pixels = positions_in_pixels != 0;
  return t;
}

void
ns_pango_tab_array_free (NsPangoTabArray *tabs)
{
  if (!tabs)
    return;
  free (tabs->tabs);
  free (tabs);
}

void
ns_pango_tab_array_set_tab (NsPangoTabArray *tabs, int tab_index,
                            NsPangoTabAlign alignment, int location)
{
  if (!tabs || tab_index < 0)
    return;
  if (tab_index >= tabs->n)
    {
      int i;

      tabs->tabs = nt_renew (NtTab, tabs->tabs, tab_index + 1);
      for (i = tabs->n; i <= tab_index; i++)
        {
          tabs->tabs[i].align = NS_PANGO_TAB_LEFT;
          tabs->tabs[i].location = 0;
        }
      tabs->n = tab_index + 1;
    }
  tabs->tabs[tab_index].align = alignment;
  tabs->tabs[tab_index].location = location;
}
