/* Weststar SDL Browser — nstext: memory, strings, UTF-8, Unicode
 * properties, a hash table, languages and script runs.
 *
 * The script run iterator and the language tables follow Pango's
 * pango-script.c and pango-language.c (LGPL-2.0-or-later, Copyright (C)
 * 2002 Red Hat Software, (C) 2000 Tor Lillqvist); the rest is new.
 *
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "nstext-private.h"

#include <stdarg.h>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "nstext-linebreak-table.h"

/* ---- memory ------------------------------------------------------------ */

static void
nt_out_of_memory (size_t n)
{
  fprintf (stderr, "nstext: failed to allocate %zu bytes\n", n);
  abort ();
}

void *
nt_malloc (size_t n)
{
  void *p = malloc (n ? n : 1);

  if (!p)
    nt_out_of_memory (n);
  return p;
}

void *
nt_malloc0 (size_t n)
{
  void *p = calloc (1, n ? n : 1);

  if (!p)
    nt_out_of_memory (n);
  return p;
}

void *
nt_realloc (void *p, size_t n)
{
  void *q = realloc (p, n ? n : 1);

  if (!q)
    nt_out_of_memory (n);
  return q;
}

char *
nt_strdup (const char *s)
{
  return s ? nt_strndup (s, strlen (s)) : NULL;
}

char *
nt_strndup (const char *s, size_t n)
{
  char *d;

  if (!s)
    return NULL;
  d = nt_malloc (n + 1);
  memcpy (d, s, n);
  d[n] = '\0';
  return d;
}

char
nt_ascii_tolower (char c)
{
  return (c >= 'A' && c <= 'Z') ? (char) (c - 'A' + 'a') : c;
}

int
nt_ascii_strncasecmp (const char *a, const char *b, size_t n)
{
  while (n-- > 0)
    {
      int ca = (unsigned char) nt_ascii_tolower (*a++);
      int cb = (unsigned char) nt_ascii_tolower (*b++);

      if (ca != cb || !ca)
        return ca - cb;
    }
  return 0;
}

int
nt_ascii_strcasecmp (const char *a, const char *b)
{
  return nt_ascii_strncasecmp (a, b, (size_t) -1);
}

void
nt_string_append_len (NtString *s, const char *p, size_t n)
{
  if (s->len + n + 1 > s->cap)
    {
      size_t cap = s->cap ? s->cap : 32;

      while (cap < s->len + n + 1)
        cap *= 2;
      s->str = nt_realloc (s->str, cap);
      s->cap = cap;
    }
  memcpy (s->str + s->len, p, n);
  s->len += n;
  s->str[s->len] = '\0';
}

void
nt_string_append (NtString *s, const char *p)
{
  nt_string_append_len (s, p, strlen (p));
}

void
nt_string_append_c (NtString *s, char c)
{
  nt_string_append_len (s, &c, 1);
}

void
nt_string_printf (NtString *s, const char *fmt, ...)
{
  char buf[256];
  va_list ap;
  int n;

  va_start (ap, fmt);
  n = vsnprintf (buf, sizeof buf, fmt, ap);
  va_end (ap);
  if (n < 0)
    return;
  if ((size_t) n < sizeof buf)
    {
      nt_string_append_len (s, buf, (size_t) n);
      return;
    }
  {
    char *big = nt_malloc ((size_t) n + 1);

    va_start (ap, fmt);
    vsnprintf (big, (size_t) n + 1, fmt, ap);
    va_end (ap);
    nt_string_append_len (s, big, (size_t) n);
    free (big);
  }
}

/* ---- UTF-8 ------------------------------------------------------------- */

const unsigned char nt_utf8_skip[256] = {
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,4,4,4,4,4,4,4,4,5,5,5,5,6,6,1,1
};

nt_unichar
nt_utf8_get_char (const char *p)
{
  const unsigned char *s = (const unsigned char *) p;
  unsigned c = s[0];

  if (c < 0x80)
    return c;
  if (c < 0xE0)
    return ((c & 0x1F) << 6) | (s[1] & 0x3F);
  if (c < 0xF0)
    return ((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
  return ((c & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) |
         (s[3] & 0x3F);
}

const char *
nt_utf8_prev_char (const char *p)
{
  do
    p--;
  while ((*(const unsigned char *) p & 0xC0) == 0x80);
  return p;
}

int
nt_utf8_strlen (const char *p, int max_bytes)
{
  int n = 0;

  if (!p)
    return 0;
  if (max_bytes < 0)
    {
      while (*p)
        {
          p = nt_utf8_next_char (p);
          n++;
        }
      return n;
    }
  {
    const char *end = p + max_bytes;

    while (p < end)
      {
        p = nt_utf8_next_char (p);
        n++;
      }
  }
  return n;
}

const char *
nt_utf8_offset_to_pointer (const char *s, int offset)
{
  if (offset >= 0)
    while (offset-- > 0)
      s = nt_utf8_next_char (s);
  else
    while (offset++ < 0)
      s = nt_utf8_prev_char (s);
  return s;
}

int
nt_utf8_pointer_to_offset (const char *s, const char *p)
{
  int n = 0;

  if (p < s)
    return -nt_utf8_pointer_to_offset (p, s);
  while (s < p)
    {
      s = nt_utf8_next_char (s);
      n++;
    }
  return n;
}

int
nt_unichar_to_utf8 (nt_unichar c, char *out)
{
  if (c < 0x80)
    {
      out[0] = (char) c;
      return 1;
    }
  if (c < 0x800)
    {
      out[0] = (char) (0xC0 | (c >> 6));
      out[1] = (char) (0x80 | (c & 0x3F));
      return 2;
    }
  if (c < 0x10000)
    {
      out[0] = (char) (0xE0 | (c >> 12));
      out[1] = (char) (0x80 | ((c >> 6) & 0x3F));
      out[2] = (char) (0x80 | (c & 0x3F));
      return 3;
    }
  out[0] = (char) (0xF0 | (c >> 18));
  out[1] = (char) (0x80 | ((c >> 12) & 0x3F));
  out[2] = (char) (0x80 | ((c >> 6) & 0x3F));
  out[3] = (char) (0x80 | (c & 0x3F));
  return 4;
}

/* The length of the valid sequence at s (at most n bytes), 0 if invalid. */
static int
utf8_valid_len (const unsigned char *s, int n)
{
  unsigned c = s[0];
  int len, i;
  nt_unichar v;

  if (c < 0x80)
    return 1;
  if (c < 0xC2 || c > 0xF4)
    return 0;
  len = c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
  if (len > n)
    return 0;
  v = c & (0x3F >> (len - 1));
  for (i = 1; i < len; i++)
    {
      if ((s[i] & 0xC0) != 0x80)
        return 0;
      v = (v << 6) | (s[i] & 0x3F);
    }
  if ((len == 3 && v < 0x800) || (len == 4 && (v < 0x10000 || v > 0x10FFFF)) ||
      (v >= 0xD800 && v <= 0xDFFF))
    return 0;
  return len;
}

char *
nt_utf8_make_valid (const char *s, int len, int *out_len)
{
  NtString out = { 0 };
  const unsigned char *p = (const unsigned char *) s;
  const unsigned char *end = p + len;

  nt_string_append_len (&out, "", 0);
  while (p < end)
    {
      int n = *p ? utf8_valid_len (p, (int) (end - p)) : 0;

      if (n > 0)
        {
          nt_string_append_len (&out, (const char *) p, (size_t) n);
          p += n;
        }
      else
        {
          /* NUL and bad bytes, as Pango: U+FFFD. */
          nt_string_append_len (&out, "\xef\xbf\xbd", 3);
          p++;
        }
    }
  *out_len = (int) out.len;
  return out.str;
}

/* ---- Unicode properties ----------------------------------------------- */

static hb_unicode_funcs_t *
ufuncs (void)
{
  return hb_unicode_funcs_get_default ();
}

hb_unicode_general_category_t
nt_unichar_type (nt_unichar c)
{
  return hb_unicode_general_category (ufuncs (), c);
}

hb_script_t
nt_unichar_script (nt_unichar c)
{
  return hb_unicode_script (ufuncs (), c);
}

int
nt_unichar_combining_class (nt_unichar c)
{
  return (int) hb_unicode_combining_class (ufuncs (), c);
}

nt_unichar
nt_unichar_mirror (nt_unichar c)
{
  return hb_unicode_mirroring (ufuncs (), c);
}

/* Simple case mapping, enough for the breaking tailorings and small
 * caps: ASCII, Latin-1, Latin Extended-A, Greek and Cyrillic. */
nt_unichar
nt_unichar_tolower (nt_unichar c)
{
  if (c >= 'A' && c <= 'Z')
    return c + 32;
  if ((c >= 0xC0 && c <= 0xDE && c != 0xD7))
    return c + 32;
  if (c >= 0x100 && c <= 0x17F && c != 0x130 && c != 0x131 && c != 0x138 &&
      c != 0x149 && c != 0x17F)
    {
      if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E))
        return (c & 1) ? c + 1 : c;
      return (c & 1) ? c : c + 1;
    }
  if (c >= 0x391 && c <= 0x3AB && c != 0x3A2)
    return c + 32;
  if (c >= 0x410 && c <= 0x42F)
    return c + 32;
  if (c >= 0x400 && c <= 0x40F)
    return c + 80;
  return c;
}

nt_unichar
nt_unichar_toupper (nt_unichar c)
{
  if (c >= 'a' && c <= 'z')
    return c - 32;
  if (c >= 0xE0 && c <= 0xFE && c != 0xF7)
    return c - 32;
  if (c == 0xFF)
    return 0x178;
  if (c >= 0x100 && c <= 0x17F && c != 0x130 && c != 0x131 && c != 0x138 &&
      c != 0x149 && c != 0x17F)
    {
      if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E))
        return (c & 1) ? c : c - 1;
      return (c & 1) ? c - 1 : c;
    }
  if (c >= 0x3B1 && c <= 0x3CB && c != 0x3C2)
    return c - 32;
  if (c >= 0x430 && c <= 0x44F)
    return c - 32;
  if (c >= 0x450 && c <= 0x45F)
    return c - 80;
  return c;
}

int
nt_unichar_isspace (nt_unichar c)
{
  switch (c)
    {
    case '\t': case '\n': case '\r': case '\f':
      return TRUE;
    default:
      {
        hb_unicode_general_category_t t = nt_unichar_type (c);

        return t == HB_UNICODE_GENERAL_CATEGORY_SPACE_SEPARATOR ||
               t == HB_UNICODE_GENERAL_CATEGORY_LINE_SEPARATOR ||
               t == HB_UNICODE_GENERAL_CATEGORY_PARAGRAPH_SEPARATOR;
      }
    }
}

int
nt_unichar_isalnum (nt_unichar c)
{
  switch (nt_unichar_type (c))
    {
    case HB_UNICODE_GENERAL_CATEGORY_LOWERCASE_LETTER:
    case HB_UNICODE_GENERAL_CATEGORY_MODIFIER_LETTER:
    case HB_UNICODE_GENERAL_CATEGORY_OTHER_LETTER:
    case HB_UNICODE_GENERAL_CATEGORY_TITLECASE_LETTER:
    case HB_UNICODE_GENERAL_CATEGORY_UPPERCASE_LETTER:
    case HB_UNICODE_GENERAL_CATEGORY_DECIMAL_NUMBER:
    case HB_UNICODE_GENERAL_CATEGORY_LETTER_NUMBER:
    case HB_UNICODE_GENERAL_CATEGORY_OTHER_NUMBER:
      return TRUE;
    default:
      return FALSE;
    }
}

/* East Asian Wide and Fullwidth, as GLib's g_unichar_iswide (for the
 * column counts of the metrics' sample strings). */
int
nt_unichar_iswide (nt_unichar c)
{
  return (c >= 0x1100 && c <= 0x115F) || (c >= 0x2329 && c <= 0x232A) ||
         (c >= 0x2E80 && c <= 0x303E) || (c >= 0x3041 && c <= 0x33FF) ||
         (c >= 0x3400 && c <= 0x4DBF) || (c >= 0x4E00 && c <= 0x9FFF) ||
         (c >= 0xA000 && c <= 0xA4CF) || (c >= 0xAC00 && c <= 0xD7A3) ||
         (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFE30 && c <= 0xFE4F) ||
         (c >= 0xFF00 && c <= 0xFF60) || (c >= 0xFFE0 && c <= 0xFFE6) ||
         (c >= 0x1F300 && c <= 0x1F64F) || (c >= 0x1F900 && c <= 0x1F9FF) ||
         (c >= 0x20000 && c <= 0x2FFFD) || (c >= 0x30000 && c <= 0x3FFFD);
}

int
nt_unichar_iszerowidth (nt_unichar c)
{
  hb_unicode_general_category_t t;

  if (c == 0x00AD)
    return FALSE;
  t = nt_unichar_type (c);
  if (t == HB_UNICODE_GENERAL_CATEGORY_NON_SPACING_MARK ||
      t == HB_UNICODE_GENERAL_CATEGORY_ENCLOSING_MARK ||
      t == HB_UNICODE_GENERAL_CATEGORY_FORMAT)
    return TRUE;
  return (c >= 0x1160 && c < 0x1200) || c == 0x200B;
}

int
nt_is_default_ignorable (nt_unichar c)
{
  return c == 0x00AD || c == 0x034F || c == 0x061C ||
         (c >= 0x115F && c <= 0x1160) || (c >= 0x17B4 && c <= 0x17B5) ||
         (c >= 0x180B && c <= 0x180F) || (c >= 0x200B && c <= 0x200F) ||
         (c >= 0x202A && c <= 0x202E) || (c >= 0x2060 && c <= 0x206F) ||
         c == 0x3164 || (c >= 0xFE00 && c <= 0xFE0F) || c == 0xFEFF ||
         c == 0xFFA0 || (c >= 0xFFF0 && c <= 0xFFF8) ||
         (c >= 0x1BCA0 && c <= 0x1BCA3) || (c >= 0x1D173 && c <= 0x1D17A) ||
         (c >= 0xE0000 && c <= 0xE0FFF);
}

NtBreakType
nt_unichar_break_type (nt_unichar c)
{
  int lo = 0, hi = NS_TEXT_LB_RUNS - 1;

  if (c > 0x10FFFF)
    return NT_LB_UNKNOWN;
  while (lo < hi)
    {
      int mid = (lo + hi + 1) / 2;

      if (ns_text_lb_starts[mid] <= c)
        lo = mid;
      else
        hi = mid - 1;
    }
  return (NtBreakType) ns_text_lb_classes[lo];
}

/* ---- a hash table ------------------------------------------------------ */

typedef struct
{
  char    *key;
  size_t   key_len;
  uint32_t hash;
  void    *value;
} NtHashEntry;

struct NtHash
{
  NtHashEntry *entries;
  size_t       cap, n;
  void       (*free_value) (void *);
};

uint32_t
nt_hash_bytes (const void *p, size_t n, uint32_t seed)
{
  const unsigned char *s = p;
  uint32_t h = 2166136261u ^ seed;

  while (n--)
    {
      h ^= *s++;
      h *= 16777619u;
    }
  return h;
}

NtHash *
nt_hash_new (void (*free_value) (void *))
{
  NtHash *h = nt_new0 (NtHash, 1);

  h->free_value = free_value;
  return h;
}

void
nt_hash_remove_all (NtHash *h)
{
  size_t i;

  for (i = 0; i < h->cap; i++)
    if (h->entries[i].key)
      {
        free (h->entries[i].key);
        if (h->free_value)
          h->free_value (h->entries[i].value);
      }
  free (h->entries);
  h->entries = NULL;
  h->cap = h->n = 0;
}

void
nt_hash_free (NtHash *h)
{
  if (!h)
    return;
  nt_hash_remove_all (h);
  free (h);
}

size_t
nt_hash_size (NtHash *h)
{
  return h->n;
}

static NtHashEntry *
hash_find (NtHash *h, const char *key, size_t len, uint32_t hash)
{
  size_t i;

  if (!h->cap)
    return NULL;
  for (i = hash & (h->cap - 1); ; i = (i + 1) & (h->cap - 1))
    {
      NtHashEntry *e = &h->entries[i];

      if (!e->key)
        return e;
      if (e->hash == hash && e->key_len == len && memcmp (e->key, key, len) == 0)
        return e;
    }
}

void *
nt_hash_lookup (NtHash *h, const char *key, size_t len)
{
  NtHashEntry *e = hash_find (h, key, len, nt_hash_bytes (key, len, 0));

  return e && e->key ? e->value : NULL;
}

static void
hash_grow (NtHash *h)
{
  NtHashEntry *old = h->entries;
  size_t old_cap = h->cap, i;

  h->cap = old_cap ? old_cap * 2 : 16;
  h->entries = nt_new0 (NtHashEntry, h->cap);
  for (i = 0; i < old_cap; i++)
    if (old[i].key)
      {
        NtHashEntry *e = hash_find (h, old[i].key, old[i].key_len, old[i].hash);

        if (e)
          *e = old[i];
      }
  free (old);
}

void
nt_hash_insert (NtHash *h, const char *key, size_t len, void *value)
{
  uint32_t hash = nt_hash_bytes (key, len, 0);
  NtHashEntry *e;

  if ((h->n + 1) * 4 > h->cap * 3)
    hash_grow (h);
  e = hash_find (h, key, len, hash);
  if (!e)
    return; /* not reached: the table has room after growing */
  if (e->key)
    {
      if (h->free_value && e->value != value)
        h->free_value (e->value);
      e->value = value;
      return;
    }
  e->key = nt_strndup (key, len);
  e->key_len = len;
  e->hash = hash;
  e->value = value;
  h->n++;
}

/* ---- languages --------------------------------------------------------- */

static NsMutex lang_lock;
static NtHash *lang_table;

NsPangoLanguage *
ns_pango_language_from_string (const char *language)
{
  char tag[64];
  size_t n = 0;
  NsPangoLanguage *lang;

  if (!language)
    return NULL;

  /* As Pango canonicalizes: lowercase, '_' to '-', up to a character that
   * cannot be in a tag ("en_US.UTF-8" is en-us). */
  for (; *language && n + 1 < sizeof tag; language++)
    {
      char c = *language;

      if (c == '_' || c == '-')
        c = '-';
      else if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
               (c >= '0' && c <= '9'))
        c = nt_ascii_tolower (c);
      else
        break;
      tag[n++] = c;
    }
  tag[n] = '\0';

  ns_mutex_lock (&lang_lock);
  if (!lang_table)
    lang_table = nt_hash_new (free);
  lang = nt_hash_lookup (lang_table, tag, n);
  if (!lang)
    {
      lang = nt_malloc (sizeof (NsPangoLanguage) + n);
      memcpy (lang->tag, tag, n + 1);
      nt_hash_insert (lang_table, tag, n, lang);
    }
  ns_mutex_unlock (&lang_lock);

  return lang;
}

const char *
ns_pango_language_to_string (NsPangoLanguage *language)
{
  return language ? language->tag : NULL;
}

static char *
lc_ctype (void)
{
  const char *p;

  p = getenv ("LC_ALL");
  if (p && *p)
    return nt_strdup (p);
  p = getenv ("LC_CTYPE");
  if (p && *p)
    return nt_strdup (p);
  p = getenv ("LANG");
  if (p && *p)
    return nt_strdup (p);
#ifdef _WIN32
  {
    /* As GLib's g_win32_getlocale: the thread locale's ISO names. */
    LCID lcid = GetThreadLocale ();
    char iso639[10], iso3166[10];

    if (GetLocaleInfoA (lcid, LOCALE_SISO639LANGNAME, iso639, sizeof iso639) &&
        GetLocaleInfoA (lcid, LOCALE_SISO3166CTRYNAME, iso3166, sizeof iso3166))
      {
        char buf[24];

        snprintf (buf, sizeof buf, "%s_%s", iso639, iso3166);
        return nt_strdup (buf);
      }
    return nt_strdup ("C");
  }
#else
  return nt_strdup ("C");
#endif
}

NsPangoLanguage *
ns_pango_language_get_default (void)
{
  static NsPangoLanguage *result;
  static NsOnce once;

  if (ns_once_enter (&once))
    {
      char *s = lc_ctype ();

      result = ns_pango_language_from_string (s);
      free (s);
      ns_once_leave (&once);
    }
  return result;
}

typedef struct
{
  const char *lang;
  const char *sample;
} NtLangSample;

static const NtLangSample lang_samples[] = {
#define LANGUAGE(id, source, sample) { #id, sample },
#include "nstext-language-sample-table.h"
#undef LANGUAGE
};

#include "nstext-script-lang-table.h"

/* The first '-' separated component of a tag, compared. */
static int
first_component_cmp (const char *tag, const char *rec)
{
  for (;; tag++, rec++)
    {
      char a = (*tag == '-') ? '\0' : *tag;
      char b = (*rec == '-') ? '\0' : *rec;

      if (a != b || !a)
        return (unsigned char) a - (unsigned char) b;
    }
}

/* A tag matches a record when the record is the tag or a prefix of it at
 * a '-' (en-us matches en), as Pango's pango_language_matches. */
static int
tag_matches (const char *tag, const char *rec)
{
  size_t n = strlen (rec);

  return strncmp (tag, rec, n) == 0 && (tag[n] == '\0' || tag[n] == '-');
}

const char *
nt_language_sample_string (NsPangoLanguage *lang)
{
  size_t i, best = (size_t) -1;

  if (!lang)
    lang = ns_pango_language_get_default ();
  if (lang)
    for (i = 0; i < NT_N_ELEMENTS (lang_samples); i++)
      if (first_component_cmp (lang->tag, lang_samples[i].lang) == 0 &&
          tag_matches (lang->tag, lang_samples[i].lang))
        best = i;
  if (best != (size_t) -1)
    return lang_samples[best].sample;
  return "The quick brown fox jumps over the lazy dog.";
}

static int
real_script (hb_script_t s)
{
  return s != HB_SCRIPT_COMMON && s != HB_SCRIPT_INHERITED &&
         s != HB_SCRIPT_UNKNOWN && s != HB_SCRIPT_INVALID;
}

int
nt_language_includes_script (NsPangoLanguage *lang, hb_script_t script)
{
  const NtScriptForLang *best = NULL;
  size_t i;
  int j;

  if (!real_script (script))
    return TRUE;
  if (!lang)
    return TRUE;
  for (i = 0; i < NT_N_ELEMENTS (nt_script_for_lang); i++)
    if (first_component_cmp (lang->tag, nt_script_for_lang[i].lang) == 0 &&
        tag_matches (lang->tag, nt_script_for_lang[i].lang))
      best = &nt_script_for_lang[i];
  if (!best || !best->scripts[0])
    return TRUE;
  for (j = 0; j < (int) NT_N_ELEMENTS (best->scripts); j++)
    if (best->scripts[j] == script)
      return TRUE;
  return FALSE;
}

NsPangoLanguage *
nt_script_sample_language (hb_script_t script)
{
  static const struct
  {
    hb_script_t script;
    const char *lang;
  } samples[] = {
    { HB_SCRIPT_ARABIC, "ar" }, { HB_SCRIPT_ARMENIAN, "hy" },
    { HB_SCRIPT_BENGALI, "bn" }, { HB_SCRIPT_CHEROKEE, "chr" },
    { HB_SCRIPT_COPTIC, "cop" }, { HB_SCRIPT_CYRILLIC, "ru" },
    { HB_SCRIPT_DEVANAGARI, "hi" }, { HB_SCRIPT_ETHIOPIC, "am" },
    { HB_SCRIPT_GEORGIAN, "ka" }, { HB_SCRIPT_GREEK, "el" },
    { HB_SCRIPT_GUJARATI, "gu" }, { HB_SCRIPT_GURMUKHI, "pa" },
    { HB_SCRIPT_HANGUL, "ko" }, { HB_SCRIPT_HEBREW, "he" },
    { HB_SCRIPT_HIRAGANA, "ja" }, { HB_SCRIPT_KANNADA, "kn" },
    { HB_SCRIPT_KATAKANA, "ja" }, { HB_SCRIPT_KHMER, "km" },
    { HB_SCRIPT_LAO, "lo" }, { HB_SCRIPT_LATIN, "en" },
    { HB_SCRIPT_MALAYALAM, "ml" }, { HB_SCRIPT_MONGOLIAN, "mn" },
    { HB_SCRIPT_MYANMAR, "my" }, { HB_SCRIPT_ORIYA, "or" },
    { HB_SCRIPT_SINHALA, "si" }, { HB_SCRIPT_SYRIAC, "syr" },
    { HB_SCRIPT_TAMIL, "ta" }, { HB_SCRIPT_TELUGU, "te" },
    { HB_SCRIPT_THAANA, "dv" }, { HB_SCRIPT_THAI, "th" },
    { HB_SCRIPT_TIBETAN, "bo" }, { HB_SCRIPT_CANADIAN_SYLLABICS, "iu" },
    { HB_SCRIPT_TAGALOG, "tl" }, { HB_SCRIPT_HANUNOO, "hnn" },
    { HB_SCRIPT_BUHID, "bku" }, { HB_SCRIPT_TAGBANWA, "tbw" },
    { HB_SCRIPT_TAI_LE, "tdd" }, { HB_SCRIPT_NKO, "nqo" },
    { HB_SCRIPT_TIFINAGH, "ber" }, { HB_SCRIPT_VAI, "vai" },
  };
  size_t i;

  for (i = 0; i < NT_N_ELEMENTS (samples); i++)
    if (samples[i].script == script)
      return ns_pango_language_from_string (samples[i].lang);
  return NULL;
}

/* ---- script runs (Pango's pango-script.c) ------------------------------ */

static const nt_unichar paired_chars[] = {
  0x0028, 0x0029, 0x003c, 0x003e, 0x005b, 0x005d, 0x007b, 0x007d,
  0x00ab, 0x00bb, 0x0f3a, 0x0f3b, 0x0f3c, 0x0f3d, 0x169b, 0x169c,
  0x2018, 0x2019, 0x201c, 0x201d, 0x2039, 0x203a, 0x2045, 0x2046,
  0x207d, 0x207e, 0x208d, 0x208e, 0x27e6, 0x27e7, 0x27e8, 0x27e9,
  0x27ea, 0x27eb, 0x27ec, 0x27ed, 0x27ee, 0x27ef, 0x2983, 0x2984,
  0x2985, 0x2986, 0x2987, 0x2988, 0x2989, 0x298a, 0x298b, 0x298c,
  0x298d, 0x298e, 0x298f, 0x2990, 0x2991, 0x2992, 0x2993, 0x2994,
  0x2995, 0x2996, 0x2997, 0x2998, 0x29fc, 0x29fd, 0x2e02, 0x2e03,
  0x2e04, 0x2e05, 0x2e09, 0x2e0a, 0x2e0c, 0x2e0d, 0x2e1c, 0x2e1d,
  0x2e20, 0x2e21, 0x2e22, 0x2e23, 0x2e24, 0x2e25, 0x2e26, 0x2e27,
  0x2e28, 0x2e29, 0x3008, 0x3009, 0x300a, 0x300b, 0x300c, 0x300d,
  0x300e, 0x300f, 0x3010, 0x3011, 0x3014, 0x3015, 0x3016, 0x3017,
  0x3018, 0x3019, 0x301a, 0x301b, 0xfe59, 0xfe5a, 0xfe5b, 0xfe5c,
  0xfe5d, 0xfe5e, 0xff08, 0xff09, 0xff3b, 0xff3d, 0xff5b, 0xff5d,
  0xff5f, 0xff60, 0xff62, 0xff63
};

static int
get_pair_index (nt_unichar ch)
{
  int lower = 0, upper = (int) NT_N_ELEMENTS (paired_chars) - 1;

  while (lower <= upper)
    {
      int mid = (lower + upper) / 2;

      if (ch < paired_chars[mid])
        upper = mid - 1;
      else if (ch > paired_chars[mid])
        lower = mid + 1;
      else
        return mid;
    }
  return -1;
}

static int
is_cluster_extender (nt_unichar ch)
{
  hb_unicode_general_category_t type = nt_unichar_type (ch);

  return type == HB_UNICODE_GENERAL_CATEGORY_SPACING_MARK ||
         type == HB_UNICODE_GENERAL_CATEGORY_ENCLOSING_MARK ||
         type == HB_UNICODE_GENERAL_CATEGORY_NON_SPACING_MARK ||
         (ch >= 0x200C && ch <= 0x200D) || (ch >= 0xFF9E && ch <= 0xFF9F) ||
         (ch >= 0x1F3FB && ch <= 0x1F3FF) || (ch >= 0xE0020 && ch <= 0xE007F);
}

#define SAME_SCRIPT(s1, s2, ch) \
  (!real_script (s1) || !real_script (s2) || (s1) == (s2) || is_cluster_extender (ch))
#define IS_OPEN(pair_index) (((pair_index) & 1) == 0)
#define PAREN_STACK_DEPTH ((int) NT_N_ELEMENTS (((NtScriptIter *) 0)->paren_stack))

void
nt_script_iter_init (NtScriptIter *iter, const char *text, int length)
{
  iter->text_start = text;
  iter->text_end = text + (length >= 0 ? (size_t) length : strlen (text));
  iter->script_start = text;
  iter->script_end = text;
  iter->script_code = HB_SCRIPT_COMMON;
  iter->paren_sp = -1;
  nt_script_iter_next (iter);
}

int
nt_script_iter_next (NtScriptIter *iter)
{
  int start_sp;

  if (iter->script_end == iter->text_end)
    return FALSE;

  start_sp = iter->paren_sp;
  iter->script_code = HB_SCRIPT_COMMON;
  iter->script_start = iter->script_end;

  for (; iter->script_end < iter->text_end;
       iter->script_end = nt_utf8_next_char (iter->script_end))
    {
      nt_unichar ch = nt_utf8_get_char (iter->script_end);
      hb_script_t sc = nt_unichar_script (ch);
      int pair_index = sc != HB_SCRIPT_COMMON ? -1 : get_pair_index (ch);

      if (pair_index >= 0)
        {
          if (IS_OPEN (pair_index))
            {
              if (++iter->paren_sp >= PAREN_STACK_DEPTH)
                iter->paren_sp = 0;
              iter->paren_stack[iter->paren_sp].pair_index = pair_index;
              iter->paren_stack[iter->paren_sp].script_code = iter->script_code;
            }
          else if (iter->paren_sp >= 0)
            {
              int pi = pair_index & ~1;

              while (iter->paren_sp >= 0 &&
                     iter->paren_stack[iter->paren_sp].pair_index != pi)
                iter->paren_sp--;
              if (iter->paren_sp < start_sp)
                start_sp = iter->paren_sp;
              if (iter->paren_sp >= 0)
                sc = iter->paren_stack[iter->paren_sp].script_code;
            }
        }

      if (SAME_SCRIPT (iter->script_code, sc, ch))
        {
          if (!real_script (iter->script_code) && real_script (sc))
            {
              iter->script_code = sc;
              while (start_sp < iter->paren_sp)
                iter->paren_stack[++start_sp].script_code = iter->script_code;
            }
          if (pair_index >= 0 && !IS_OPEN (pair_index) && iter->paren_sp >= 0)
            {
              iter->paren_sp--;
              if (iter->paren_sp < start_sp)
                start_sp = iter->paren_sp;
            }
        }
      else
        break;
    }

  return TRUE;
}

/* ---- version and statistics ------------------------------------------- */

const char *
ns_pango_version_string (void)
{
  static char version[96];
  static NsOnce once;

  if (ns_once_enter (&once))
    {
      snprintf (version, sizeof version, "nstext (HarfBuzz %s, Unicode %s)",
                hb_version_string (), NS_TEXT_UCD_VERSION);
      ns_once_leave (&once);
    }
  return version;
}
