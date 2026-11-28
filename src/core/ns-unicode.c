/* Weststar SDL Browser — nscore: UTF-8, UTF-16 and Unicode properties.
 *
 * GLib's gutf8.c, guniprop.c, gunidecomp.c and gunicollate.c redone on
 * nscore's own tables (ns-unicode-tables.h), keeping GLib 2.80's results:
 * the same validation, the same quirks for invalid input, the same order
 * of decomposition and composition. See ns-unicode.h for where it does
 * not follow GLib (locale-specific case rules, collation).
 *
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ns-unicode.h"

#include <string.h>

#include "ns-unicode-tables.h"

#define NS_UC_MAX 0x110000u

/* Hangul syllables (Unicode 3.12). */
#define SBASE 0xAC00
#define LBASE 0x1100
#define VBASE 0x1161
#define TBASE 0x11A7
#define LCOUNT 19
#define VCOUNT 21
#define TCOUNT 28
#define NCOUNT (VCOUNT * TCOUNT)
#define SCOUNT (LCOUNT * NCOUNT)

#define UNICODE_VALID(c) ((c) < 0x110000 && (((c) & 0xFFFFF800) != 0xD800))

const char ns_utf8_skip[256] = {
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,4,4,4,4,4,4,4,4,5,5,5,5,6,6,1,1
};

/* ---- Property lookups ---------------------------------------------- */

#define TWO_STAGE(name, SHIFT, cp)                                      \
  name##_stage2[((size_t)name##_stage1[(cp) >> (SHIFT)] << (SHIFT)) +  \
                ((cp) & ((1u << (SHIFT)) - 1))]

static int
uc_type(ns_unichar c)
{
  if (c >= NS_UC_MAX)
    return NS_UNICODE_UNASSIGNED;
  return TWO_STAGE(ns_uc_type, NS_UC_TYPE_SHIFT, c);
}

static int
uc_ccc(ns_unichar c)
{
  if (c >= NS_UC_MAX)
    return 0;
  return TWO_STAGE(ns_uc_ccc, NS_UC_CCC_SHIFT, c);
}

/* Index of key in the sorted column 0 of rows of `width` uint32s, or -1. */
static long
find_row(const uint32_t *rows, size_t n, size_t width, uint32_t key)
{
  size_t lo = 0, hi = n;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    uint32_t v = rows[mid * width];
    if (v == key)
      return (long)mid;
    if (v < key)
      lo = mid + 1;
    else
      hi = mid;
  }
  return -1;
}

static ns_unichar
simple_case(ns_unichar c)
{
  long i = find_row(ns_uc_simple_cp, NS_UC_SIMPLE_N, 1, c);
  return i < 0 ? 0 : ns_uc_simple_map[i];
}

static const uint32_t *
title_row(ns_unichar c)
{
  for (size_t i = 0; i < NS_UC_TITLE_N; i++)
    if (ns_uc_title[i][0] == c)
      return ns_uc_title[i];
  return NULL;
}

static const uint32_t *
special_row(ns_unichar c)
{
  long i = find_row(&ns_uc_special[0][0], NS_UC_SPECIAL_N, 3, c);
  return i < 0 ? NULL : ns_uc_special[i];
}

#define IS_ALPHA_TYPE(t)                                               \
  ((t) == NS_UNICODE_LOWERCASE_LETTER || (t) == NS_UNICODE_UPPERCASE_LETTER || \
   (t) == NS_UNICODE_TITLECASE_LETTER || (t) == NS_UNICODE_MODIFIER_LETTER || \
   (t) == NS_UNICODE_OTHER_LETTER)
#define IS_DIGIT_TYPE(t)                                               \
  ((t) == NS_UNICODE_DECIMAL_NUMBER || (t) == NS_UNICODE_LETTER_NUMBER || \
   (t) == NS_UNICODE_OTHER_NUMBER)
#define IS_MARK_TYPE(t)                                                \
  ((t) == NS_UNICODE_NON_SPACING_MARK || (t) == NS_UNICODE_SPACING_MARK || \
   (t) == NS_UNICODE_ENCLOSING_MARK)

NsUnicodeType
ns_unichar_type(ns_unichar c)
{
  return (NsUnicodeType)uc_type(c);
}

NsUnicodeScript
ns_unichar_get_script(ns_unichar ch)
{
  if (ch >= NS_UC_MAX)
    return NS_UNICODE_SCRIPT_UNKNOWN;
  return (NsUnicodeScript)TWO_STAGE(ns_uc_script, NS_UC_SCRIPT_SHIFT, ch);
}

int
ns_unichar_combining_class(ns_unichar uc)
{
  return uc_ccc(uc);
}

int ns_unichar_isalpha(ns_unichar c) { int t = uc_type(c); return IS_ALPHA_TYPE(t); }
int ns_unichar_isdigit(ns_unichar c) { return uc_type(c) == NS_UNICODE_DECIMAL_NUMBER; }
int ns_unichar_iscntrl(ns_unichar c) { return uc_type(c) == NS_UNICODE_CONTROL; }
int ns_unichar_isupper(ns_unichar c) { return uc_type(c) == NS_UNICODE_UPPERCASE_LETTER; }
int ns_unichar_islower(ns_unichar c) { return uc_type(c) == NS_UNICODE_LOWERCASE_LETTER; }
/* GLib returns the type's bit of its mark mask here, not 1. */
int ns_unichar_ismark(ns_unichar c) { int t = uc_type(c); return IS_MARK_TYPE(t) ? 1 << t : 0; }

int
ns_unichar_isalnum(ns_unichar c)
{
  int t = uc_type(c);
  return IS_ALPHA_TYPE(t) || IS_DIGIT_TYPE(t);
}

int
ns_unichar_isprint(ns_unichar c)
{
  int t = uc_type(c);
  return !(t == NS_UNICODE_CONTROL || t == NS_UNICODE_FORMAT ||
           t == NS_UNICODE_UNASSIGNED || t == NS_UNICODE_SURROGATE);
}

int
ns_unichar_isspace(ns_unichar c)
{
  int t;
  switch (c) {
  case '\t': case '\n': case '\r': case '\f':
    return 1;
  default:
    t = uc_type(c);
    return t == NS_UNICODE_SPACE_SEPARATOR || t == NS_UNICODE_LINE_SEPARATOR ||
           t == NS_UNICODE_PARAGRAPH_SEPARATOR;
  }
}

ns_unichar
ns_unichar_toupper(ns_unichar c)
{
  int t = uc_type(c);
  if (t == NS_UNICODE_LOWERCASE_LETTER) {
    ns_unichar v = simple_case(c);
    return v ? v : c;
  }
  if (t == NS_UNICODE_TITLECASE_LETTER) {
    const uint32_t *row = title_row(c);
    if (row)
      return row[1] ? row[1] : c;
  }
  return c;
}

ns_unichar
ns_unichar_tolower(ns_unichar c)
{
  int t = uc_type(c);
  if (t == NS_UNICODE_UPPERCASE_LETTER) {
    ns_unichar v = simple_case(c);
    return v ? v : c;
  }
  if (t == NS_UNICODE_TITLECASE_LETTER) {
    const uint32_t *row = title_row(c);
    if (row)
      return row[2];
  }
  return c;
}

ns_unichar
ns_unichar_totitle(ns_unichar c)
{
  if (c == 0)
    return c;
  for (size_t i = 0; i < NS_UC_TITLE_N; i++)
    if (ns_uc_title[i][0] == c || ns_uc_title[i][1] == c ||
        ns_uc_title[i][2] == c)
      return ns_uc_title[i][0];
  if (uc_type(c) == NS_UNICODE_LOWERCASE_LETTER)
    return ns_unichar_toupper(c);
  return c;
}

/* ---- UTF-8 ------------------------------------------------------------ */

int
ns_unichar_validate(ns_unichar ch)
{
  return UNICODE_VALID(ch);
}

int
ns_unichar_to_utf8(ns_unichar c, char *outbuf)
{
  unsigned first;
  int len, i;
  if (c < 0x80) { first = 0; len = 1; }
  else if (c < 0x800) { first = 0xc0; len = 2; }
  else if (c < 0x10000) { first = 0xe0; len = 3; }
  else if (c < 0x200000) { first = 0xf0; len = 4; }
  else if (c < 0x4000000) { first = 0xf8; len = 5; }
  else { first = 0xfc; len = 6; }
  if (outbuf) {
    for (i = len - 1; i > 0; --i) {
      outbuf[i] = (char)((c & 0x3f) | 0x80);
      c >>= 6;
    }
    outbuf[0] = (char)(c | first);
  }
  return len;
}

ns_unichar
ns_utf8_get_char(const char *p)
{
  const unsigned char *s = (const unsigned char *)p;
  unsigned c = s[0], mask;
  int len, i;
  ns_unichar r;
  if (c < 0x80) return c;
  else if ((c & 0xe0) == 0xc0) { len = 2; mask = 0x1f; }
  else if ((c & 0xf0) == 0xe0) { len = 3; mask = 0x0f; }
  else if ((c & 0xf8) == 0xf0) { len = 4; mask = 0x07; }
  else if ((c & 0xfc) == 0xf8) { len = 5; mask = 0x03; }
  else if ((c & 0xfe) == 0xfc) { len = 6; mask = 0x01; }
  else return (ns_unichar)-1;
  r = c & mask;
  for (i = 1; i < len; i++) {
    if ((s[i] & 0xc0) != 0x80)
      return (ns_unichar)-1;
    r = (r << 6) | (s[i] & 0x3f);
  }
  return r;
}

/* GLib's g_utf8_get_char_extended: -1 malformed, -2 partial. */
static ns_unichar
get_char_extended(const char *p, ptrdiff_t max_len)
{
  const unsigned char *s = (const unsigned char *)p;
  size_t i, len;
  ns_unichar min_code, wc = s[0];
  if (wc < 0x80) return wc;
  else if (wc < 0xc0) return (ns_unichar)-1;
  else if (wc < 0xe0) { len = 2; wc &= 0x1f; min_code = 1u << 7; }
  else if (wc < 0xf0) { len = 3; wc &= 0x0f; min_code = 1u << 11; }
  else if (wc < 0xf8) { len = 4; wc &= 0x07; min_code = 1u << 16; }
  else if (wc < 0xfc) { len = 5; wc &= 0x03; min_code = 1u << 21; }
  else if (wc < 0xfe) { len = 6; wc &= 0x01; min_code = 1u << 26; }
  else return (ns_unichar)-1;

  if (max_len >= 0 && len > (size_t)max_len) {
    for (i = 1; i < (size_t)max_len; i++)
      if ((s[i] & 0xc0) != 0x80)
        return (ns_unichar)-1;
    return (ns_unichar)-2;
  }
  for (i = 1; i < len; ++i) {
    ns_unichar ch = s[i];
    if ((ch & 0xc0) != 0x80)
      return ch ? (ns_unichar)-1 : (ns_unichar)-2;
    wc = (wc << 6) | (ch & 0x3f);
  }
  if (wc < min_code)
    return (ns_unichar)-1;
  return wc;
}

ns_unichar
ns_utf8_get_char_validated(const char *p, ptrdiff_t max_len)
{
  ns_unichar r;
  if (max_len == 0)
    return (ns_unichar)-2;
  r = get_char_extended(p, max_len);
  /* U+0000 is a NUL byte: within max_len it is a partial input. */
  if (r == 0 && max_len > 0)
    return (ns_unichar)-2;
  if (r & 0x80000000u)
    return r;
  if (!UNICODE_VALID(r))
    return (ns_unichar)-1;
  return r;
}

char *
ns_utf8_prev_char(const char *p)
{
  for (;;) {
    p--;
    if ((*p & 0xc0) != 0x80)
      return (char *)p;
  }
}

long
ns_utf8_strlen(const char *p, ptrdiff_t max)
{
  long len = 0;
  const char *start = p;
  if (!p)
    return 0;
  if (max < 0) {
    while (*p) {
      p = ns_utf8_next_char(p);
      ++len;
    }
  } else {
    if (max == 0 || !*p)
      return 0;
    p = ns_utf8_next_char(p);
    while (p - start < max && *p) {
      ++len;
      p = ns_utf8_next_char(p);
    }
    /* Count the last character only when it is complete. */
    if (p - start <= max)
      ++len;
  }
  return len;
}

long
ns_utf8_pointer_to_offset(const char *str, const char *pos)
{
  const char *s = str;
  long offset = 0;
  if (pos < str)
    return -ns_utf8_pointer_to_offset(pos, str);
  while (s < pos) {
    s = ns_utf8_next_char(s);
    offset++;
  }
  return offset;
}

char *
ns_utf8_offset_to_pointer(const char *str, long offset)
{
  const char *s = str;
  if (offset > 0) {
    while (offset--)
      s = ns_utf8_next_char(s);
  } else {
    /* Step back by offset bytes, to a character start, and count what
     * is left (Larry Ewing's "stutter stepping", as GLib). */
    while (offset) {
      const char *s1 = s;
      s += offset;
      while ((*s & 0xc0) == 0x80)
        s--;
      offset += ns_utf8_pointer_to_offset(s, s1);
    }
  }
  return (char *)s;
}

/* Where valid UTF-8 stops: at a NUL (max_len < 0) or at max_len, else at
 * the first byte of the invalid or incomplete character. */
static const char *
validate_stop(const char *str, ptrdiff_t max_len)
{
  const unsigned char *p = (const unsigned char *)str;
  const unsigned char *end = max_len < 0 ? NULL : p + max_len;
  while (!end || p < end) {
    unsigned c = p[0];
    size_t need, avail, k;
    unsigned lo = 0x80, hi = 0xbf;
    if (c < 0x80) {
      if (c == 0)
        break;
      p++;
      continue;
    }
    if (c >= 0xc2 && c <= 0xdf) need = 2;
    else if (c >= 0xe0 && c <= 0xef) {
      need = 3;
      if (c == 0xe0) lo = 0xa0;
      else if (c == 0xed) hi = 0x9f;
    } else if (c >= 0xf0 && c <= 0xf4) {
      need = 4;
      if (c == 0xf0) lo = 0x90;
      else if (c == 0xf4) hi = 0x8f;
    } else
      break;
    avail = end ? (size_t)(end - p) : need;
    if (avail < need)
      break;
    if (p[1] < lo || p[1] > hi)
      break;
    for (k = 2; k < need; k++)
      if ((p[k] & 0xc0) != 0x80)
        break;
    if (k < need)
      break;
    p += need;
  }
  return (const char *)p;
}

int
ns_utf8_validate(const char *str, ptrdiff_t max_len, const char **end)
{
  const char *p = validate_stop(str, max_len);
  if (end)
    *end = p;
  if (max_len >= 0)
    return p == str + max_len;
  return *p == '\0';
}

int
ns_utf8_validate_len(const char *str, size_t max_len, const char **end)
{
  const char *p = validate_stop(str, (ptrdiff_t)max_len);
  if (end)
    *end = p;
  return p == str + max_len;
}

char *
ns_utf8_make_valid(const char *str, ptrdiff_t len)
{
  static const char fffd[3] = { '\xef', '\xbf', '\xbd' };
  size_t total = 0, n;
  int pass;
  char *out = NULL;
  if (len < 0)
    len = (ptrdiff_t)strlen(str);
  /* Two passes: the length, then the bytes. Each byte where valid UTF-8
   * stops becomes U+FFFD. */
  for (pass = 0; pass < 2; pass++) {
    const char *rem = str, *bad;
    size_t remaining = (size_t)len;
    total = 0;
    while (remaining != 0) {
      if (ns_utf8_validate(rem, (ptrdiff_t)remaining, &bad))
        break;
      n = (size_t)(bad - rem);
      if (out) {
        memcpy(out + total, rem, n);
        memcpy(out + total + n, fffd, 3);
      }
      total += n + 3;
      remaining -= n + 1;
      rem = bad + 1;
    }
    if (out)
      memcpy(out + total, rem, remaining);
    total += remaining;
    if (!out)
      out = ns_malloc(total + 1);
  }
  out[total] = '\0';
  return out;
}

/* ---- UTF-16 ------------------------------------------------------------ */

static void
set_error(NsError **error, int code, const char *message)
{
  if (error && !*error)
    *error = ns_error_new_literal(ns_convert_error_quark(), code, message);
}

ns_unichar2 *
ns_utf8_to_utf16(const char *str, long len, long *items_read,
                 long *items_written, NsError **error)
{
  ns_unichar2 *result = NULL;
  size_t n16 = 0, i;
  const char *in = str;

  while ((len < 0 || str + len - in > 0) && *in) {
    ns_unichar wc = get_char_extended(in, len < 0 ? 6 : (ptrdiff_t)(str + len - in));
    if (wc & 0x80000000u) {
      if (wc == (ns_unichar)-2) {
        if (items_read)
          break;
        set_error(error, NS_CONVERT_ERROR_PARTIAL_INPUT,
                  "Partial character sequence at end of input");
      } else
        set_error(error, NS_CONVERT_ERROR_ILLEGAL_SEQUENCE,
                  "Invalid byte sequence in conversion input");
      goto out;
    }
    if (wc < 0xd800)
      n16 += 1;
    else if (wc < 0xe000) {
      set_error(error, NS_CONVERT_ERROR_ILLEGAL_SEQUENCE,
                "Invalid sequence in conversion input");
      goto out;
    } else if (wc < 0x10000)
      n16 += 1;
    else if (wc < 0x110000)
      n16 += 2;
    else {
      set_error(error, NS_CONVERT_ERROR_ILLEGAL_SEQUENCE,
                "Character out of range for UTF-16");
      goto out;
    }
    in = ns_utf8_next_char(in);
  }

  result = ns_malloc((n16 + 1) * sizeof(ns_unichar2));
  in = str;
  for (i = 0; i < n16;) {
    ns_unichar wc = ns_utf8_get_char(in);
    if (wc < 0x10000)
      result[i++] = (ns_unichar2)wc;
    else {
      result[i++] = (ns_unichar2)((wc - 0x10000) / 0x400 + 0xd800);
      result[i++] = (ns_unichar2)((wc - 0x10000) % 0x400 + 0xdc00);
    }
    in = ns_utf8_next_char(in);
  }
  result[i] = 0;
  if (items_written)
    *items_written = (long)n16;
out:
  if (items_read)
    *items_read = (long)(in - str);
  return result;
}

#define SURROGATE_VALUE(h, l) (((h) - 0xd800) * 0x400 + (l) - 0xdc00 + 0x10000)

char *
ns_utf16_to_utf8(const ns_unichar2 *str, long len, long *items_read,
                 long *items_written, NsError **error)
{
  const ns_unichar2 *in = str;
  char *result = NULL, *out;
  size_t n_bytes = 0;
  ns_unichar high = 0;

  while ((len < 0 || in - str < len) && *in) {
    ns_unichar c = *in, wc;
    if (c >= 0xdc00 && c < 0xe000) {
      if (!high) {
        set_error(error, NS_CONVERT_ERROR_ILLEGAL_SEQUENCE,
                  "Invalid sequence in conversion input");
        goto err;
      }
      wc = SURROGATE_VALUE(high, c);
      high = 0;
    } else {
      if (high) {
        set_error(error, NS_CONVERT_ERROR_ILLEGAL_SEQUENCE,
                  "Invalid sequence in conversion input");
        goto err;
      }
      if (c >= 0xd800 && c < 0xdc00) {
        high = c;
        in++;
        continue;
      }
      wc = c;
    }
    n_bytes += (size_t)ns_unichar_to_utf8(wc, NULL);
    in++;
  }
  if (high && !items_read) {
    set_error(error, NS_CONVERT_ERROR_PARTIAL_INPUT,
              "Partial character sequence at end of input");
    goto err;
  }

  result = ns_malloc(n_bytes + 1);
  high = 0;
  out = result;
  in = str;
  while (out < result + n_bytes) {
    ns_unichar c = *in, wc;
    if (c >= 0xdc00 && c < 0xe000) {
      wc = SURROGATE_VALUE(high, c);
      high = 0;
    } else if (c >= 0xd800 && c < 0xdc00) {
      high = c;
      in++;
      continue;
    } else
      wc = c;
    out += ns_unichar_to_utf8(wc, out);
    in++;
  }
  *out = '\0';
  if (items_written)
    *items_written = (long)(out - result);
err:
  if (items_read)
    *items_read = (long)(in - str);
  return result;
}

/* ---- Case conversion --------------------------------------------------- */

/* Writes the code points of pool sequence `off` as UTF-8 (out may be
 * NULL); returns the byte count. */
static size_t
put_pool(uint32_t off, char *out)
{
  size_t len = 0;
  for (const uint32_t *s = &ns_uc_pool[off]; *s; s++)
    len += (size_t)ns_unichar_to_utf8(*s, out ? out + len : NULL);
  return len;
}

/* After U+0345, GLib moves the marks that follow it before the capital
 * iota: those wholly before end (NULL: the NUL). */
static size_t
output_marks(const char **pp, const char *end, char *out)
{
  const char *p = *pp;
  size_t len = 0;
  while (*p && (!end || ns_utf8_next_char(p) <= end)) {
    ns_unichar c = ns_utf8_get_char(p);
    int t = uc_type(c);
    if (!IS_MARK_TYPE(t))
      break;
    len += (size_t)ns_unichar_to_utf8(c, out ? out + len : NULL);
    p = ns_utf8_next_char(p);
  }
  *pp = p;
  return len;
}

static size_t
real_toupper(const char *str, ptrdiff_t max_len, char *out)
{
  const char *p = str;
  size_t len = 0;
  while ((max_len < 0 || p < str + max_len) && *p) {
    const char *last = p;
    ns_unichar c = ns_utf8_get_char(p);
    int t = uc_type(c);
    p = ns_utf8_next_char(p);
    if (c == 0x0345) {
      len += output_marks(&p, max_len < 0 ? NULL : str + max_len,
                          out ? out + len : NULL);
      len += (size_t)ns_unichar_to_utf8(0x399, out ? out + len : NULL);
    } else if (t == NS_UNICODE_LOWERCASE_LETTER ||
               t == NS_UNICODE_TITLECASE_LETTER) {
      const uint32_t *sp = special_row(c);
      if (sp && sp[1]) {
        len += put_pool(sp[1], out ? out + len : NULL);
      } else {
        ns_unichar v;
        if (t == NS_UNICODE_TITLECASE_LETTER) {
          const uint32_t *row = title_row(c);
          v = row ? row[1] : 0;
        } else
          v = simple_case(c);
        len += (size_t)ns_unichar_to_utf8(v ? v : c, out ? out + len : NULL);
      }
    } else {
      size_t n = (size_t)ns_utf8_skip[*(const unsigned char *)last];
      if (out)
        memcpy(out + len, last, n);
      len += n;
    }
  }
  return len;
}

static size_t
real_tolower(const char *str, ptrdiff_t max_len, char *out)
{
  const char *p = str;
  size_t len = 0;
  while ((max_len < 0 || p < str + max_len) && *p) {
    const char *last = p;
    ns_unichar c = ns_utf8_get_char(p);
    int t = uc_type(c);
    p = ns_utf8_next_char(p);
    if (c == 0x03A3) {
      /* Capital sigma: final unless a letter follows (GLib's and ICU's
       * simplified test). */
      ns_unichar v = 0x3c2;
      if ((max_len < 0 || p < str + max_len) && *p) {
        int nt = uc_type(ns_utf8_get_char(p));
        if (IS_ALPHA_TYPE(nt))
          v = 0x3c3;
      }
      len += (size_t)ns_unichar_to_utf8(v, out ? out + len : NULL);
    } else if (t == NS_UNICODE_UPPERCASE_LETTER ||
               t == NS_UNICODE_TITLECASE_LETTER) {
      const uint32_t *sp = special_row(c);
      if (sp && sp[2]) {
        len += put_pool(sp[2], out ? out + len : NULL);
      } else {
        ns_unichar v;
        if (t == NS_UNICODE_TITLECASE_LETTER) {
          const uint32_t *row = title_row(c);
          v = row ? row[2] : 0;
        } else
          v = simple_case(c);
        len += (size_t)ns_unichar_to_utf8(v ? v : c, out ? out + len : NULL);
      }
    } else {
      size_t n = (size_t)ns_utf8_skip[*(const unsigned char *)last];
      if (out)
        memcpy(out + len, last, n);
      len += n;
    }
  }
  return len;
}

char *
ns_utf8_strup(const char *str, ptrdiff_t len)
{
  size_t n = real_toupper(str, len, NULL);
  char *r = ns_malloc(n + 1);
  real_toupper(str, len, r);
  r[n] = '\0';
  return r;
}

char *
ns_utf8_strdown(const char *str, ptrdiff_t len)
{
  size_t n = real_tolower(str, len, NULL);
  char *r = ns_malloc(n + 1);
  real_tolower(str, len, r);
  r[n] = '\0';
  return r;
}

/* Full case folding of one code point as UTF-8 (out may be NULL). */
static size_t
fold_char(ns_unichar ch, char *out)
{
  long i = find_row(&ns_uc_fold[0][0], NS_UC_FOLD_N, 2, ch);
  if (i >= 0)
    return put_pool(ns_uc_fold[i][1], out);
  return (size_t)ns_unichar_to_utf8(ns_unichar_tolower(ch), out);
}

static size_t
real_casefold(const char *str, ptrdiff_t len, char *out)
{
  const char *p = str;
  size_t n = 0;
  while ((len < 0 || p < str + len) && *p) {
    n += fold_char(ns_utf8_get_char(p), out ? out + n : NULL);
    p = ns_utf8_next_char(p);
  }
  return n;
}

char *
ns_utf8_casefold(const char *str, ptrdiff_t len)
{
  size_t n = real_casefold(str, len, NULL);
  char *r = ns_malloc(n + 1);
  real_casefold(str, len, r);
  r[n] = '\0';
  return r;
}

/* ---- Normalization ----------------------------------------------------- */

static const uint32_t *
find_decomposition(ns_unichar ch, int compat)
{
  long i = find_row(&ns_uc_decomp[0][0], NS_UC_DECOMP_N, 3, ch);
  uint32_t off;
  if (i < 0)
    return NULL;
  off = compat ? ns_uc_decomp[i][2] : ns_uc_decomp[i][1];
  return off ? &ns_uc_pool[off] : NULL;
}

static size_t
decompose_hangul(ns_unichar s, ns_unichar *r)
{
  int32_t si = (int32_t)(s - SBASE);
  int32_t ti = si % TCOUNT;
  if (r) {
    r[0] = (ns_unichar)(LBASE + si / NCOUNT);
    r[1] = (ns_unichar)(VBASE + (si % NCOUNT) / TCOUNT);
    if (ti)
      r[2] = (ns_unichar)(TBASE + ti);
  }
  return ti ? 3 : 2;
}

static int
combine(ns_unichar a, ns_unichar b, ns_unichar *result)
{
  int32_t li = (int32_t)(a - LBASE), si = (int32_t)(a - SBASE);
  int32_t vi = (int32_t)(b - VBASE), ti = (int32_t)(b - TBASE);
  size_t lo = 0, hi = NS_UC_COMPOSE_N;

  if (0 <= li && li < LCOUNT && 0 <= vi && vi < VCOUNT) {
    *result = (ns_unichar)(SBASE + (li * VCOUNT + vi) * TCOUNT);
    return 1;
  }
  if (0 <= si && si < SCOUNT && si % TCOUNT == 0 && 0 < ti && ti < TCOUNT) {
    *result = a + (ns_unichar)ti;
    return 1;
  }
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    const uint32_t *row = ns_uc_compose[mid];
    if (row[0] == a && row[1] == b) {
      *result = row[2];
      return 1;
    }
    if (row[0] < a || (row[0] == a && row[1] < b))
      lo = mid + 1;
    else
      hi = mid;
  }
  return 0;
}

/* Stable sort by combining class of each run of non-starters, as GLib's
 * g_unicode_canonical_ordering. */
static void
canonical_ordering(ns_unichar *s, size_t len)
{
  size_t i, j;
  for (i = 1; i < len; i++) {
    int cc = uc_ccc(s[i]);
    ns_unichar t = s[i];
    if (cc == 0)
      continue;
    for (j = i; j > 0 && uc_ccc(s[j - 1]) > cc; j--)
      s[j] = s[j - 1];
    s[j] = t;
  }
}

/* GLib's _g_utf8_normalize_wc: the code points of the normalized form,
 * ended by 0, and their count; NULL if str is not valid UTF-8. */
static ns_unichar *
normalize_wc(const char *str, ptrdiff_t max_len, NsNormalizeMode mode,
             size_t *count)
{
  int do_compat = mode == NS_NORMALIZE_NFKC || mode == NS_NORMALIZE_NFKD;
  int do_compose = mode == NS_NORMALIZE_NFC || mode == NS_NORMALIZE_NFKC;
  size_t n_wc = 0;
  ns_unichar *buf;
  const char *p;
  int pass;

  for (pass = 0; pass < 2; pass++) {
    if (pass == 1) {
      buf = ns_malloc((n_wc + 1) * sizeof(ns_unichar));
      n_wc = 0;
    } else
      buf = NULL;
    p = str;
    while ((max_len < 0 || p < str + max_len) && *p) {
      const char *next = ns_utf8_next_char(p);
      /* Invalid UTF-8, or a character cut short by the NUL or by
       * max_len: no result. */
      ns_unichar wc = ns_utf8_get_char_validated(
          p, max_len < 0 ? -1 : (ptrdiff_t)(str + max_len - p));
      if (wc & 0x80000000u) {
        ns_free(buf);
        return NULL;
      }
      if (wc >= SBASE && wc < SBASE + SCOUNT)
        n_wc += decompose_hangul(wc, buf ? buf + n_wc : NULL);
      else {
        const uint32_t *d = find_decomposition(wc, do_compat);
        if (d) {
          for (; *d; d++) {
            if (buf)
              buf[n_wc] = *d;
            n_wc++;
          }
        } else {
          if (buf)
            buf[n_wc] = wc;
          n_wc++;
        }
      }
      p = next;
    }
  }
  canonical_ordering(buf, n_wc);

  if (do_compose && n_wc > 0) {
    size_t i, j, last_start = 0;
    int last_cc = 0;
    for (i = 0; i < n_wc; i++) {
      int cc = uc_ccc(buf[i]);
      if (i > 0 && (last_cc == 0 || last_cc < cc) &&
          combine(buf[last_start], buf[i], &buf[last_start])) {
        for (j = i + 1; j < n_wc; j++)
          buf[j - 1] = buf[j];
        n_wc--;
        i--;
        last_cc = i == last_start ? 0 : uc_ccc(buf[i - 1]);
        continue;
      }
      if (cc == 0)
        last_start = i;
      last_cc = cc;
    }
  }
  buf[n_wc] = 0;
  *count = n_wc;
  return buf;
}

/* g_ucs4_to_utf8 (len -1): NULL when a code point is >= 0x80000000. */
static char *
ucs4_to_utf8(const ns_unichar *s)
{
  size_t n = 0, i;
  char *r, *o;
  for (i = 0; s[i]; i++) {
    if (s[i] >= 0x80000000u)
      return NULL;
    n += (size_t)ns_unichar_to_utf8(s[i], NULL);
  }
  r = o = ns_malloc(n + 1);
  for (i = 0; s[i]; i++)
    o += ns_unichar_to_utf8(s[i], o);
  *o = '\0';
  return r;
}

char *
ns_utf8_normalize(const char *str, ptrdiff_t len, NsNormalizeMode mode)
{
  size_t n;
  ns_unichar *wc = normalize_wc(str, len, mode, &n);
  char *r;
  if (!wc)
    return NULL;
  r = ucs4_to_utf8(wc);
  ns_free(wc);
  return r;
}

/* ---- Collation --------------------------------------------------------- */

/* A key byte string whose strcmp order is the collation order: three
 * levels separated by 0x01, each a sequence of code points + 1 in UTF-8
 * form (so no 0x00 or 0x01 byte within a level):
 *   1. the NFKD form without marks, case folded;
 *   2. the NFKD form case folded;
 *   3. the NFKD form. */
static size_t
put_key_cp(ns_unichar c, char *out)
{
  return (size_t)ns_unichar_to_utf8(c + 1, out);
}

static size_t
put_key_folded(ns_unichar c, char *out)
{
  char tmp[24];
  size_t n = fold_char(c, tmp), len = 0;
  const char *p = tmp;
  while (p < tmp + n) {
    len += put_key_cp(ns_utf8_get_char(p), out ? out + len : NULL);
    p = ns_utf8_next_char(p);
  }
  return len;
}

static size_t
build_key(const ns_unichar *wc, size_t n, char *out)
{
  size_t len = 0, i;
  int level;
  for (level = 1; level <= 3; level++) {
    if (level > 1) {
      if (out)
        out[len] = '\x01';
      len++;
    }
    for (i = 0; i < n; i++) {
      char *o = out ? out + len : NULL;
      if (level == 1) {
        int t = uc_type(wc[i]);
        if (!IS_MARK_TYPE(t))
          len += put_key_folded(wc[i], o);
      } else if (level == 2)
        len += put_key_folded(wc[i], o);
      else
        len += put_key_cp(wc[i], o);
    }
  }
  return len;
}

char *
ns_utf8_collate_key(const char *str, ptrdiff_t len)
{
  size_t n, klen;
  ns_unichar *wc;
  char *valid = ns_utf8_make_valid(str, len < 0 ? (ptrdiff_t)strlen(str) : len);
  char *key;
  wc = normalize_wc(valid, -1, NS_NORMALIZE_NFKD, &n);
  ns_free(valid);
  klen = build_key(wc, n, NULL);
  key = ns_malloc(klen + 1);
  build_key(wc, n, key);
  key[klen] = '\0';
  ns_free(wc);
  return key;
}

int
ns_utf8_collate(const char *str1, const char *str2)
{
  char *k1 = ns_utf8_collate_key(str1, -1);
  char *k2 = ns_utf8_collate_key(str2, -1);
  int r = strcmp(k1, k2);
  ns_free(k1);
  ns_free(k2);
  return r < 0 ? -1 : r > 0 ? 1 : 0;
}
