/* Weststar SDL Browser — nscore: tests of ns-unicode.
 *
 *   test-unicode [--ucd DIR] [CORPUS...]
 *
 * Checks known answers and, with --ucd, the conformance lines of the
 * UCD's NormalizationTest.txt in DIR. Built with -DNS_TEST_VS_GLIB and
 * linked with GLib (test-unicode-vs-glib), it also compares every function
 * with GLib's: each code point 0..0x10FFFF, random bytes and UTF-16 units,
 * the NormalizationTest.txt strings and the lines and whole contents of
 * each CORPUS file (directories are walked). Differences are printed;
 * the exit status is the number of failed checks (capped at 255).
 *
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ns-unicode.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef NS_TEST_VS_GLIB
#include <glib.h>
#endif

/* ---- Stand-ins for the rest of nscore ---------------------------------- */

struct _NsError {
  uint32_t domain;
  int code;
  char *message;
};

void *
ns_malloc(size_t n)
{
  void *p = malloc(n ? n : 1);
  if (!p)
    abort();
  return p;
}

void
ns_free(void *p)
{
  free(p);
}

uint32_t
ns_convert_error_quark(void)
{
#ifdef NS_TEST_VS_GLIB
  return g_convert_error_quark();
#else
  return 42;
#endif
}

NsError *
ns_error_new_literal(uint32_t domain, int code, const char *message)
{
  NsError *e = ns_malloc(sizeof *e);
  size_t n = strlen(message) + 1;
  e->domain = domain;
  e->code = code;
  e->message = ns_malloc(n);
  memcpy(e->message, message, n);
  return e;
}

static void
error_free(NsError *e)
{
  if (e) {
    ns_free(e->message);
    ns_free(e);
  }
}

/* ---- Reporting ---------------------------------------------------------- */

static long failures;
static long checks;

/* Failures by check name: each name's first few are printed, all counted. */
#define MAX_KINDS 128
static struct { const char *what; long count; } kinds[MAX_KINDS];

static void
fail(const char *what, const char *detail)
{
  int i;
  failures++;
  for (i = 0; i < MAX_KINDS && kinds[i].what; i++)
    if (strcmp(kinds[i].what, what) == 0)
      break;
  if (i < MAX_KINDS && !kinds[i].what)
    kinds[i].what = what;
  if (i < MAX_KINDS && kinds[i].count++ >= 8)
    return;
  fprintf(stderr, "FAIL %s: %s\n", what, detail);
}

static void
report_kinds(void)
{
  int i;
  for (i = 0; i < MAX_KINDS && kinds[i].what; i++)
    fprintf(stderr, "  %-36s %ld failures\n", kinds[i].what, kinds[i].count);
}

static void
hexstr(char *out, size_t cap, const char *s, ptrdiff_t len)
{
  size_t o = 0;
  ptrdiff_t i;
  if (!s) {
    snprintf(out, cap, "(null)");
    return;
  }
  if (len < 0)
    len = (ptrdiff_t)strlen(s);
  out[0] = '\0';
  for (i = 0; i < len && o + 4 < cap; i++)
    o += (size_t)snprintf(out + o, cap - o, "%02X ", (unsigned char)s[i]);
}

static void
check_str(const char *what, const char *input, ptrdiff_t inlen,
          const char *got, const char *want)
{
  checks++;
  if ((got == NULL) != (want == NULL) || (got && strcmp(got, want) != 0)) {
    char a[200], b[200], c[200], msg[700];
    hexstr(a, sizeof a, input, inlen);
    hexstr(b, sizeof b, got, -1);
    hexstr(c, sizeof c, want, -1);
    snprintf(msg, sizeof msg, "in [%s] got [%s] want [%s]", a, b, c);
    fail(what, msg);
  }
}

static void
check_int(const char *what, long long got, long long want)
{
  checks++;
  if (got != want) {
    char msg[100];
    snprintf(msg, sizeof msg, "got %lld want %lld", got, want);
    fail(what, msg);
  }
}

#ifdef NS_TEST_VS_GLIB
static void
check_cp_int(const char *what, uint32_t cp, long long got, long long want)
{
  checks++;
  if (got != want) {
    char msg[120];
    snprintf(msg, sizeof msg, "U+%04X: got %lld (0x%llX) want %lld (0x%llX)",
             cp, got, got, want, want);
    fail(what, msg);
  }
}

#endif

/* An ns_ function's result compared with an expected string; frees it. */
static void
expect(const char *what, const char *input, char *got, const char *want)
{
  check_str(what, input, -1, got, want);
  ns_free(got);
}

/* ---- Known answers ------------------------------------------------------ */

static void
test_known(void)
{
  const char *end;
  char buf[8];
  long rd, wr;
  NsError *err = NULL;
  ns_unichar2 *u16;
  char *s;

  /* Validation. */
  check_int("validate ascii", ns_utf8_validate("abc", -1, NULL), 1);
  check_int("validate 2-byte", ns_utf8_validate("\xc3\xa9", -1, NULL), 1);
  check_int("validate 4-byte max", ns_utf8_validate("\xf4\x8f\xbf\xbf", -1, NULL), 1);
  check_int("validate > U+10FFFF", ns_utf8_validate("\xf4\x90\x80\x80", -1, NULL), 0);
  check_int("validate surrogate", ns_utf8_validate("\xed\xa0\x80", -1, NULL), 0);
  check_int("validate overlong C0", ns_utf8_validate("\xc0\x80", -1, NULL), 0);
  check_int("validate overlong E0", ns_utf8_validate("\xe0\x80\xaf", -1, NULL), 0);
  check_int("validate overlong F0", ns_utf8_validate("\xf0\x80\x80\xaf", -1, NULL), 0);
  check_int("validate 5-byte", ns_utf8_validate("\xf8\x88\x80\x80\x80", -1, NULL), 0);
  check_int("validate lone cont", ns_utf8_validate("a\x80", -1, &end), 0);
  check_int("validate end", end - "a\x80" == 1, 1);
  check_int("validate NUL in len", ns_utf8_validate("a\0b", 3, &end), 0);
  check_int("validate NUL end", (int)(end - "a\0b"), 1);
  check_int("validate truncated by len", ns_utf8_validate("\xc3\xa9", 1, NULL), 0);
  check_int("validate_len", ns_utf8_validate_len("ab\xc3\xa9", 4, NULL), 1);
  check_int("unichar_validate D800", ns_unichar_validate(0xD800), 0);
  check_int("unichar_validate 10FFFF", ns_unichar_validate(0x10FFFF), 1);
  check_int("unichar_validate 110000", ns_unichar_validate(0x110000), 0);
  check_int("unichar_validate FFFE", ns_unichar_validate(0xFFFE), 1);

  /* make_valid: each byte where validity stops becomes U+FFFD. */
  expect("make_valid", "a\xe0\x80\x80z",
         ns_utf8_make_valid("a\xe0\x80\x80z", -1),
         "a\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbdz");
  expect("make_valid NUL", "a\0b", ns_utf8_make_valid("a\0b", 3),
         "a\xef\xbf\xbd" "b");
  expect("make_valid valid", "ok", ns_utf8_make_valid("ok", -1), "ok");

  /* get_char / get_char_validated. */
  check_int("get_char", ns_utf8_get_char("\xe2\x82\xac"), 0x20AC);
  check_int("get_char_validated", ns_utf8_get_char_validated("\xe2\x82\xac", -1), 0x20AC);
  check_int("get_char_validated partial",
            ns_utf8_get_char_validated("\xe2\x82", 2), (long long)(ns_unichar)-2);
  check_int("get_char_validated bad",
            ns_utf8_get_char_validated("\xe2(", -1), (long long)(ns_unichar)-1);
  check_int("get_char_validated surrogate",
            ns_utf8_get_char_validated("\xed\xa0\x80", -1), (long long)(ns_unichar)-1);
  check_int("get_char_validated NUL",
            ns_utf8_get_char_validated("", 1), (long long)(ns_unichar)-2);
  check_int("get_char_validated 0 len",
            ns_utf8_get_char_validated("a", 0), (long long)(ns_unichar)-2);

  /* Lengths and offsets. */
  check_int("strlen", ns_utf8_strlen("a\xc3\xa9\xe2\x82\xac", -1), 3);
  check_int("strlen max partial", ns_utf8_strlen("a\xc3\xa9\xe2\x82\xac", 5), 2);
  check_int("strlen max full", ns_utf8_strlen("a\xc3\xa9\xe2\x82\xac", 6), 3);
  {
    const char *t = "a\xc3\xa9\xe2\x82\xac" "b";
    check_int("offset_to_pointer", ns_utf8_offset_to_pointer(t, 2) - t, 3);
    check_int("offset_to_pointer back", ns_utf8_offset_to_pointer(t + 6, -2) - t, 1);
    check_int("pointer_to_offset", ns_utf8_pointer_to_offset(t, t + 6), 3);
    check_int("pointer_to_offset neg", ns_utf8_pointer_to_offset(t + 6, t + 1), -2);
    check_int("prev_char", ns_utf8_prev_char(t + 6) - t, 3);
    check_int("next_char", ns_utf8_next_char(t + 1) - t, 3);
  }

  check_int("to_utf8 len", ns_unichar_to_utf8(0x1F600, buf), 4);
  check_int("to_utf8 bytes", memcmp(buf, "\xf0\x9f\x98\x80", 4), 0);
  check_int("to_utf8 6-byte", ns_unichar_to_utf8(0x7FFFFFFF, NULL), 6);

  /* UTF-16. */
  u16 = ns_utf8_to_utf16("a\xf0\x9f\x98\x80", -1, &rd, &wr, &err);
  check_int("to_utf16 written", wr, 3);
  check_int("to_utf16 read", rd, 5);
  check_int("to_utf16 hi", u16[1], 0xD83D);
  check_int("to_utf16 lo", u16[2], 0xDE00);
  s = ns_utf16_to_utf8(u16, -1, NULL, &wr, NULL);
  expect("utf16 round trip", "", s, "a\xf0\x9f\x98\x80");
  ns_free(u16);
  u16 = ns_utf8_to_utf16("a\xed\xa0\x80", -1, NULL, NULL, &err);
  check_int("to_utf16 surrogate NULL", u16 == NULL, 1);
  check_int("to_utf16 error code", err ? err->code : -1, NS_CONVERT_ERROR_ILLEGAL_SEQUENCE);
  error_free(err);
  err = NULL;
  u16 = ns_utf8_to_utf16("a\xc3", -1, NULL, NULL, &err);
  check_int("to_utf16 partial code", err ? err->code : -1, NS_CONVERT_ERROR_PARTIAL_INPUT);
  error_free(err);
  err = NULL;
  u16 = ns_utf8_to_utf16("a\xc3", -1, &rd, NULL, &err);
  check_int("to_utf16 partial read", rd, 1);
  check_int("to_utf16 partial ok", u16 != NULL && err == NULL, 1);
  ns_free(u16);
  {
    static const ns_unichar2 lone[] = { 'a', 0xDC00, 0 };
    s = ns_utf16_to_utf8(lone, -1, NULL, NULL, &err);
    check_int("utf16 lone low", s == NULL && err != NULL, 1);
    error_free(err);
    err = NULL;
  }

  /* Properties. */
  check_int("type A", ns_unichar_type('A'), NS_UNICODE_UPPERCASE_LETTER);
  check_int("type 0301", ns_unichar_type(0x301), NS_UNICODE_NON_SPACING_MARK);
  check_int("type 0378", ns_unichar_type(0x378), NS_UNICODE_UNASSIGNED);
  check_int("type E000", ns_unichar_type(0xE000), NS_UNICODE_PRIVATE_USE);
  check_int("type 10FFFD", ns_unichar_type(0x10FFFD), NS_UNICODE_PRIVATE_USE);
  check_int("type D800", ns_unichar_type(0xD800), NS_UNICODE_SURROGATE);
  check_int("type 4E00", ns_unichar_type(0x4E00), NS_UNICODE_OTHER_LETTER);
  check_int("type 110000", ns_unichar_type(0x110000), NS_UNICODE_UNASSIGNED);
  check_int("script a", ns_unichar_get_script('a'), NS_UNICODE_SCRIPT_LATIN);
  check_int("script 0627", ns_unichar_get_script(0x627), NS_UNICODE_SCRIPT_ARABIC);
  check_int("script 0301", ns_unichar_get_script(0x301), NS_UNICODE_SCRIPT_INHERITED);
  check_int("script space", ns_unichar_get_script(' '), NS_UNICODE_SCRIPT_COMMON);
  check_int("script 0378", ns_unichar_get_script(0x378), NS_UNICODE_SCRIPT_UNKNOWN);
  check_int("script AC00", ns_unichar_get_script(0xAC00), NS_UNICODE_SCRIPT_HANGUL);
  check_int("isspace tab", ns_unichar_isspace('\t'), 1);
  check_int("isspace vt", ns_unichar_isspace('\v'), 0);
  check_int("isspace 00A0", ns_unichar_isspace(0xA0), 1);
  check_int("isspace 2028", ns_unichar_isspace(0x2028), 1);
  check_int("isprint E000", ns_unichar_isprint(0xE000), 1);
  check_int("isprint 200B", ns_unichar_isprint(0x200B), 0);
  check_int("isalnum 0661", ns_unichar_isalnum(0x661), 1);
  check_int("isalpha 01C5", ns_unichar_isalpha(0x1C5), 1);
  check_int("iscntrl 7F", ns_unichar_iscntrl(0x7F), 1);
  check_int("ismark 0903", ns_unichar_ismark(0x903) != 0, 1);
  check_int("totitle a", ns_unichar_totitle('a'), 'A');
  check_int("totitle 01C6", ns_unichar_totitle(0x1C6), 0x1C5);
  check_int("totitle 01C4", ns_unichar_totitle(0x1C4), 0x1C5);
  check_int("totitle 00DF", ns_unichar_totitle(0xDF), 0xDF);
  check_int("totitle 1F80", ns_unichar_totitle(0x1F80), 0x1F88);
  check_int("totitle 1", ns_unichar_totitle('1'), '1');

  /* Case conversion. */
  expect("strup", "stra\xc3\x9f" "e", ns_utf8_strup("stra\xc3\x9f" "e", -1), "STRASSE");
  expect("strup len", "abc", ns_utf8_strup("abc", 2), "AB");
  expect("strup ligature", "\xef\xac\x81", ns_utf8_strup("\xef\xac\x81", -1), "FI");
  expect("strup 0149", "\xc5\x89", ns_utf8_strup("\xc5\x89", -1), "\xca\xbcN");
  expect("strup ypogegrammeni", "\xce\xb1\xcd\x85\xcc\x81",
         ns_utf8_strup("\xce\xb1\xcd\x85\xcc\x81", -1),
         "\xce\x91\xcc\x81\xce\x99");
  expect("strdown sigma", "\xce\xa3\xce\x91\xce\xa3",
         ns_utf8_strdown("\xce\xa3\xce\x91\xce\xa3", -1),
         "\xcf\x83\xce\xb1\xcf\x82");
  expect("strdown 0130", "\xc4\xb0", ns_utf8_strdown("\xc4\xb0", -1), "i\xcc\x87");
  expect("strdown I", "I", ns_utf8_strdown("I", -1), "i");
  expect("strdown 01C5", "\xc7\x85", ns_utf8_strdown("\xc7\x85", -1), "\xc7\x86");
  expect("strdown 2160 (Nl)", "\xe2\x85\xa0", ns_utf8_strdown("\xe2\x85\xa0", -1),
         "\xe2\x85\xa0");
  expect("casefold", "Stra\xc3\x9f" "e", ns_utf8_casefold("Stra\xc3\x9f" "e", -1), "strasse");
  expect("casefold sigma", "\xcf\x82", ns_utf8_casefold("\xcf\x82", -1), "\xcf\x83");
  expect("casefold 24B6", "\xe2\x92\xb6", ns_utf8_casefold("\xe2\x92\xb6", -1),
         "\xe2\x93\x90");
  expect("casefold 0130", "\xc4\xb0", ns_utf8_casefold("\xc4\xb0", -1), "i\xcc\x87");

  /* Normalization. */
  expect("NFD e-acute", "\xc3\xa9", ns_utf8_normalize("\xc3\xa9", -1, NS_NORMALIZE_NFD),
         "e\xcc\x81");
  expect("NFC e-acute", "e\xcc\x81", ns_utf8_normalize("e\xcc\x81", -1, NS_NORMALIZE_NFC),
         "\xc3\xa9");
  expect("NFD order", "a\xcc\x81\xcc\xa3",
         ns_utf8_normalize("a\xcc\x81\xcc\xa3", -1, NS_NORMALIZE_NFD),
         "a\xcc\xa3\xcc\x81");
  expect("NFC order", "a\xcc\x81\xcc\xa3",
         ns_utf8_normalize("a\xcc\x81\xcc\xa3", -1, NS_NORMALIZE_NFC),
         "\xe1\xba\xa1\xcc\x81");
  expect("NFD Hangul", "\xea\xb0\x81",
         ns_utf8_normalize("\xea\xb0\x81", -1, NS_NORMALIZE_NFD),
         "\xe1\x84\x80\xe1\x85\xa1\xe1\x86\xa8");
  expect("NFC Hangul", "\xe1\x84\x80\xe1\x85\xa1\xe1\x86\xa8",
         ns_utf8_normalize("\xe1\x84\x80\xe1\x85\xa1\xe1\x86\xa8", -1, NS_NORMALIZE_NFC),
         "\xea\xb0\x81");
  expect("NFKD fi", "\xef\xac\x81", ns_utf8_normalize("\xef\xac\x81", -1, NS_NORMALIZE_NFKD),
         "fi");
  expect("NFKC 2075", "x\xe2\x81\xb5", ns_utf8_normalize("x\xe2\x81\xb5", -1, NS_NORMALIZE_NFKC),
         "x5");
  expect("NFC exclusion 0958", "\xe0\xa5\x98",
         ns_utf8_normalize("\xe0\xa5\x98", -1, NS_NORMALIZE_NFC),
         "\xe0\xa4\x95\xe0\xa4\xbc");
  expect("NFC singleton 212B", "\xe2\x84\xab",
         ns_utf8_normalize("\xe2\x84\xab", -1, NS_NORMALIZE_NFC), "\xc3\x85");
  expect("normalize invalid", "a\xff", ns_utf8_normalize("a\xff", -1, NS_NORMALIZE_NFC),
         NULL);

  /* Collation: base letters, then accents, then case (uppercase first). */
  {
    static const char *const order[] = {
      "", " ", "1", "A", "a", "\xc3\x81", "\xc3\xa1", "Ab", "ab", "b",
      "co", "c\xc3\xb4", "cote", "c\xc3\xb4te", "z", "\xce\xb1",
    };
    size_t i, j, n = sizeof order / sizeof order[0];
    for (i = 0; i < n; i++)
      for (j = 0; j < n; j++) {
        int want = i < j ? -1 : i > j ? 1 : 0;
        int got = ns_utf8_collate(order[i], order[j]);
        char *ki = ns_utf8_collate_key(order[i], -1);
        char *kj = ns_utf8_collate_key(order[j], -1);
        int kc = strcmp(ki, kj);
        char what[64];
        snprintf(what, sizeof what, "collate %zu %zu", i, j);
        check_int(what, got, want);
        snprintf(what, sizeof what, "collate_key %zu %zu", i, j);
        check_int(what, kc < 0 ? -1 : kc > 0 ? 1 : 0, want);
        ns_free(ki);
        ns_free(kj);
      }
    check_int("collate NFC=NFD", ns_utf8_collate("\xc3\xa9", "e\xcc\x81"), 0);
  }
}

/* ---- NormalizationTest.txt ---------------------------------------------- */

static char *
read_file(const char *path, size_t *len)
{
  FILE *f = fopen(path, "rb");
  char *buf;
  long n;
  if (!f)
    return NULL;
  fseek(f, 0, SEEK_END);
  n = ftell(f);
  fseek(f, 0, SEEK_SET);
  buf = ns_malloc((size_t)n + 1);
  if (fread(buf, 1, (size_t)n, f) != (size_t)n)
    n = 0;
  buf[n] = '\0';
  fclose(f);
  *len = (size_t)n;
  return buf;
}

/* Parses a field of hex code points into UTF-8. */
static void
parse_field(const char *s, const char *e, char *out)
{
  size_t o = 0;
  while (s < e) {
    char *next;
    unsigned long cp = strtoul(s, &next, 16);
    if (next == s)
      break;
    o += (size_t)ns_unichar_to_utf8((ns_unichar)cp, out + o);
    s = next;
    while (s < e && *s == ' ')
      s++;
  }
  out[o] = '\0';
}

typedef void (*norm_line_fn)(char cols[5][256]);

static void
each_normalization_line(const char *ucd, norm_line_fn fn)
{
  char path[1024];
  size_t len;
  char *text, *line, *next;
  snprintf(path, sizeof path, "%s/NormalizationTest.txt", ucd);
  text = read_file(path, &len);
  if (!text) {
    fail("NormalizationTest.txt", path);
    return;
  }
  for (line = text; line && *line; line = next) {
    char cols[5][256];
    const char *p = line;
    int c;
    next = strchr(line, '\n');
    if (next)
      *next++ = '\0';
    if (*line == '#' || *line == '@' || *line == '\0' || *line == '\r')
      continue;
    for (c = 0; c < 5; c++) {
      const char *semi = strchr(p, ';');
      if (!semi)
        break;
      parse_field(p, semi, cols[c]);
      p = semi + 1;
    }
    if (c == 5)
      fn(cols);
  }
  ns_free(text);
}

static void
conformance_line(char cols[5][256])
{
  /* UAX #15: c2 == NFC(c1..c3), c4 == NFC(c4,c5); c3 == NFD(c1..c3),
   * c5 == NFD(c4,c5); c4 == NFKC(c1..c5); c5 == NFKD(c1..c5). */
  static const int nfc[5] = { 1, 1, 1, 3, 3 }, nfd[5] = { 2, 2, 2, 4, 4 };
  int i;
  for (i = 0; i < 5; i++) {
    expect("NormalizationTest NFC", cols[i],
           ns_utf8_normalize(cols[i], -1, NS_NORMALIZE_NFC), cols[nfc[i]]);
    expect("NormalizationTest NFD", cols[i],
           ns_utf8_normalize(cols[i], -1, NS_NORMALIZE_NFD), cols[nfd[i]]);
    expect("NormalizationTest NFKC", cols[i],
           ns_utf8_normalize(cols[i], -1, NS_NORMALIZE_NFKC), cols[3]);
    expect("NormalizationTest NFKD", cols[i],
           ns_utf8_normalize(cols[i], -1, NS_NORMALIZE_NFKD), cols[4]);
  }
}

#ifdef NS_TEST_VS_GLIB

/* ---- Comparison with GLib ----------------------------------------------- */

static void
same_str(const char *what, const char *in, ptrdiff_t inlen, char *ns, char *gl)
{
  check_str(what, in, inlen, ns, gl);
  ns_free(ns);
  g_free(gl);
}

static void
same_error(const char *what, NsError *ns, GError *gl)
{
  checks++;
  if ((ns == NULL) != (gl == NULL) ||
      (ns && (ns->code != gl->code || ns->domain != gl->domain ||
              strcmp(ns->message, gl->message) != 0))) {
    char msg[300];
    snprintf(msg, sizeof msg, "ns %d \"%s\" glib %d \"%s\"",
             ns ? ns->code : -1, ns ? ns->message : "", gl ? gl->code : -1,
             gl ? gl->message : "");
    fail(what, msg);
  }
  error_free(ns);
  if (gl)
    g_error_free(gl);
}

/* Every string function on s (len bytes, NUL-terminated after them). */
static void
compare_string(const char *s, size_t len)
{
  static const ptrdiff_t modes[] = { G_NORMALIZE_NFD, G_NORMALIZE_NFC,
                                     G_NORMALIZE_NFKD, G_NORMALIZE_NFKC };
  ptrdiff_t lens[3];
  const char *e1, *e2;
  int k, m;
  lens[0] = -1;
  lens[1] = (ptrdiff_t)len;
  lens[2] = len > 1 ? (ptrdiff_t)len - 1 : (ptrdiff_t)len;

  for (k = 0; k < 3; k++) {
    ptrdiff_t l = lens[k];
    if (k == 2 && lens[2] == lens[1])
      break;
    check_int("validate", ns_utf8_validate(s, l, &e1), g_utf8_validate(s, l, &e2));
    check_int("validate end", e1 - s, e2 - s);
    if (l >= 0) {
      check_int("validate_len", ns_utf8_validate_len(s, (size_t)l, &e1),
                g_utf8_validate_len(s, (gsize)l, &e2));
      check_int("validate_len end", e1 - s, e2 - s);
      same_str("make_valid", s, l, ns_utf8_make_valid(s, l), g_utf8_make_valid(s, l));
      check_int("get_char_validated len",
                ns_utf8_get_char_validated(s, l), g_utf8_get_char_validated(s, l));
    }
    same_str("strup", s, l, ns_utf8_strup(s, l), g_utf8_strup(s, l));
    same_str("strdown", s, l, ns_utf8_strdown(s, l), g_utf8_strdown(s, l));
    same_str("casefold", s, l, ns_utf8_casefold(s, l), g_utf8_casefold(s, l));
    for (m = 0; m < 4; m++)
      same_str("normalize", s, l, ns_utf8_normalize(s, l, (NsNormalizeMode)modes[m]),
               g_utf8_normalize(s, l, (GNormalizeMode)modes[m]));
    {
      long r1 = -7, w1 = -7;
      glong r2 = -7, w2 = -7;
      NsError *ne = NULL;
      GError *ge = NULL;
      ns_unichar2 *a = ns_utf8_to_utf16(s, l, NULL, &w1, &ne);
      gunichar2 *b = g_utf8_to_utf16(s, l, NULL, &w2, &ge);
      checks++;
      if ((a == NULL) != (b == NULL) || (a && (w1 != w2 ||
          memcmp(a, b, (size_t)(w1 + 1) * 2) != 0)))
        fail("utf8_to_utf16", "result differs");
      same_error("utf8_to_utf16 error", ne, ge);
      ns_free(a);
      g_free(b);
      ne = NULL;
      ge = NULL;
      a = ns_utf8_to_utf16(s, l, &r1, &w1, &ne);
      b = g_utf8_to_utf16(s, l, &r2, &w2, &ge);
      checks++;
      if ((a == NULL) != (b == NULL) || r1 != r2 || (a && (w1 != w2 ||
          memcmp(a, b, (size_t)(w1 + 1) * 2) != 0)))
        fail("utf8_to_utf16 items_read", "result differs");
      same_error("utf8_to_utf16 items_read error", ne, ge);
      ns_free(a);
      g_free(b);
    }
  }
  if (g_utf8_validate(s, -1, NULL)) {
    const char *p;
    long n = ns_utf8_strlen(s, -1);
    check_int("strlen", n, g_utf8_strlen(s, -1));
    for (k = 0; k <= (int)len && k < 64; k++)
      check_int("strlen max", ns_utf8_strlen(s, k), g_utf8_strlen(s, k));
    long idx = 0, step = n / 64 + 1;
    for (p = s; *p; p = g_utf8_next_char(p), idx++) {
      check_int("get_char", ns_utf8_get_char(p), g_utf8_get_char(p));
      check_int("get_char_validated", ns_utf8_get_char_validated(p, -1),
                g_utf8_get_char_validated(p, -1));
      if (idx % step)
        continue;
      check_int("pointer_to_offset", ns_utf8_pointer_to_offset(s, p),
                g_utf8_pointer_to_offset(s, p));
      check_int("pointer_to_offset neg", ns_utf8_pointer_to_offset(p, s),
                g_utf8_pointer_to_offset(p, s));
      if (p > s)
        check_int("prev_char", ns_utf8_prev_char(p) - s, g_utf8_prev_char(p) - s);
    }
    for (k = 0; k <= n && k < 64; k++) {
      const char *q = g_utf8_offset_to_pointer(s, n);
      check_int("offset_to_pointer", ns_utf8_offset_to_pointer(s, k) - s,
                g_utf8_offset_to_pointer(s, k) - s);
      check_int("offset_to_pointer back", ns_utf8_offset_to_pointer(q, -k) - s,
                g_utf8_offset_to_pointer(q, -k) - s);
    }
    {
      glong w = 0;
      gunichar2 *u = g_utf8_to_utf16(s, -1, NULL, &w, NULL);
      if (u) {
        long r1 = -7, w1 = -7;
        glong r2 = -7, w2 = -7;
        NsError *ne = NULL;
        GError *ge = NULL;
        char *a = ns_utf16_to_utf8(u, -1, &r1, &w1, &ne);
        char *b = g_utf16_to_utf8(u, -1, &r2, &w2, &ge);
        check_int("utf16_to_utf8 read", r1, r2);
        check_int("utf16_to_utf8 written", w1, w2);
        same_error("utf16_to_utf8 error", ne, ge);
        same_str("utf16_to_utf8", s, -1, a, b);
        g_free(u);
      }
    }
  }
}

static void
compare_utf16(const ns_unichar2 *u, long n)
{
  long lens[2];
  int k, withread;
  lens[0] = -1;
  lens[1] = n;
  for (k = 0; k < 2; k++)
    for (withread = 0; withread < 2; withread++) {
      long r1 = -7, w1 = -7;
      glong r2 = -7, w2 = -7;
      NsError *ne = NULL;
      GError *ge = NULL;
      char *a = ns_utf16_to_utf8(u, lens[k], withread ? &r1 : NULL, &w1, &ne);
      char *b = g_utf16_to_utf8((const gunichar2 *)u, lens[k],
                                withread ? &r2 : NULL, &w2, &ge);
      if (withread)
        check_int("utf16_to_utf8 read", r1, r2);
      if (a && b)
        check_int("utf16_to_utf8 written", w1, w2);
      same_error("utf16_to_utf8 error", ne, ge);
      same_str("utf16_to_utf8", "", 0, a, b);
    }
}

static void
compare_code_points(void)
{
  ns_unichar c;
  static const ns_unichar big[] = { 0x110000, 0x1FFFFF, 0x200000, 0x3FFFFFF,
                                    0x4000000, 0x7FFFFFFF, 0x80000000u,
                                    0xFFFFFFFEu, 0xFFFFFFFFu };
  size_t i;
  for (c = 0; c < 0x110000 + sizeof big / sizeof big[0]; c++) {
    ns_unichar cp = c < 0x110000 ? c : big[c - 0x110000];
    char a[8], b[8], s[16];
    int la, lb;
    check_cp_int("type", cp, ns_unichar_type(cp), g_unichar_type(cp));
    check_cp_int("script", cp, ns_unichar_get_script(cp), g_unichar_get_script(cp));
    check_cp_int("totitle", cp, ns_unichar_totitle(cp), g_unichar_totitle(cp));
    check_cp_int("toupper", cp, ns_unichar_toupper(cp), g_unichar_toupper(cp));
    check_cp_int("tolower", cp, ns_unichar_tolower(cp), g_unichar_tolower(cp));
    check_cp_int("combining_class", cp, ns_unichar_combining_class(cp),
                 g_unichar_combining_class(cp));
    check_cp_int("isalnum", cp, ns_unichar_isalnum(cp), g_unichar_isalnum(cp));
    check_cp_int("isalpha", cp, ns_unichar_isalpha(cp), g_unichar_isalpha(cp));
    check_cp_int("iscntrl", cp, ns_unichar_iscntrl(cp), g_unichar_iscntrl(cp));
    check_cp_int("isdigit", cp, ns_unichar_isdigit(cp), g_unichar_isdigit(cp));
    check_cp_int("ismark", cp, ns_unichar_ismark(cp), g_unichar_ismark(cp));
    check_cp_int("isprint", cp, ns_unichar_isprint(cp), g_unichar_isprint(cp));
    check_cp_int("isspace", cp, ns_unichar_isspace(cp), g_unichar_isspace(cp));
    check_cp_int("isupper", cp, ns_unichar_isupper(cp), g_unichar_isupper(cp));
    check_cp_int("islower", cp, ns_unichar_islower(cp), g_unichar_islower(cp));
    check_cp_int("validate", cp, ns_unichar_validate(cp), g_unichar_validate(cp));
    la = ns_unichar_to_utf8(cp, a);
    lb = g_unichar_to_utf8(cp, b);
    check_cp_int("to_utf8 len", cp, la, lb);
    check_cp_int("to_utf8 bytes", cp, memcmp(a, b, (size_t)la), 0);
    if (cp >= 0x110000 || cp == 0)
      continue;
    /* The character alone, before a letter, after a letter and before
     * a mark, for the context rules (final sigma, ypogegrammeni). */
    memcpy(s, a, (size_t)la);
    s[la] = '\0';
    compare_string(s, (size_t)la);
    if ((cp & 0xFF) == 0 || ns_unichar_type(cp) != NS_UNICODE_UNASSIGNED) {
      size_t n = 0;
      s[n++] = 'A';
      memcpy(s + n, a, (size_t)la);
      n += (size_t)la;
      memcpy(s + n, "\xcc\x81" "b", 3);
      n += 3;
      s[n] = '\0';
      compare_string(s, n);
      compare_string(s + 1, n - 1);
    }
  }
  for (i = 0; i < 1; i++) {
    /* g_utf8_skip. */
    int k;
    for (k = 0; k < 256; k++)
      check_cp_int("utf8_skip", (uint32_t)k, ns_utf8_skip[k], g_utf8_skip[k]);
  }
}

static uint32_t rng_state = 12345;

static uint32_t
rng(void)
{
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 17;
  rng_state ^= rng_state << 5;
  return rng_state;
}

static void
compare_random(void)
{
  static const unsigned char alphabet[] = {
    'a', 'I', 'i', ' ', 0x00, 0x7f, 0x80, 0x81, 0x8f, 0x90, 0x9f, 0xa0, 0xa3,
    0xbf, 0xc0, 0xc1, 0xc2, 0xc3, 0xcc, 0xcd, 0xce, 0xcf, 0xdf, 0xe0, 0xe1,
    0xe2, 0xea, 0xed, 0xee, 0xef, 0xf0, 0xf1, 0xf3, 0xf4, 0xf5, 0xf8, 0xfc,
    0xfe, 0xff, 0x85, 0xa9, 0x84, 0x86,
  };
  int i, j;
  for (i = 0; i < 300000; i++) {
    char s[24];
    int n = (int)(rng() % 12) + 1;
    for (j = 0; j < n; j++)
      s[j] = (char)alphabet[rng() % sizeof alphabet];
    s[n] = '\0';
    compare_string(s, (size_t)n);
  }
  for (i = 0; i < 200000; i++) {
    ns_unichar2 u[12];
    int n = (int)(rng() % 10) + 1;
    for (j = 0; j < n; j++) {
      uint32_t r = rng();
      switch (r % 4) {
      case 0: u[j] = (ns_unichar2)(0xD800 + (r >> 8) % 0x800); break;
      case 1: u[j] = (ns_unichar2)('a' + (r >> 8) % 3); break;
      case 2: u[j] = (ns_unichar2)(r >> 8); break;
      default: u[j] = (ns_unichar2)((r >> 8) % 0x100); break;
      }
    }
    u[n] = 0;
    compare_utf16(u, n);
  }
}

static void
compare_normalization_line(char cols[5][256])
{
  int i;
  for (i = 0; i < 5; i++)
    compare_string(cols[i], strlen(cols[i]));
}

static void
compare_text(char *text, size_t len)
{
  char *line = text, *end = text + len;
  /* The whole text (up to its first NUL), then line by line. */
  compare_string(text, strlen(text));
  while (line < end) {
    char *nl = memchr(line, '\n', (size_t)(end - line));
    char save;
    if (!nl)
      nl = end;
    save = *nl;
    *nl = '\0';
    compare_string(line, strlen(line));
    *nl = save;
    line = nl + 1;
  }
}

static void
compare_path(const char *path)
{
  struct stat st;
  if (stat(path, &st) != 0) {
    fail("corpus", path);
    return;
  }
  if (S_ISDIR(st.st_mode)) {
    DIR *d = opendir(path);
    struct dirent *e;
    if (!d)
      return;
    while ((e = readdir(d))) {
      char sub[2048];
      if (e->d_name[0] == '.')
        continue;
      snprintf(sub, sizeof sub, "%s/%s", path, e->d_name);
      compare_path(sub);
    }
    closedir(d);
  } else {
    size_t len;
    char *text = read_file(path, &len);
    if (text && len <= (8u << 20))
      compare_text(text, len);
    ns_free(text);
  }
}

#endif /* NS_TEST_VS_GLIB */

int
main(int argc, char **argv)
{
  const char *ucd = NULL;
  int i;
  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--ucd") == 0 && i + 1 < argc) {
      ucd = argv[++i];
      continue;
    }
  }
  if (!ucd)
    ucd = getenv("NS_UCD_DIR");

#ifdef NS_TEST_VS_GLIB
  /* GLib's messages untranslated, to compare with nscore's. */
  g_setenv("LANGUAGE", "C", TRUE);
  g_setenv("LC_ALL", "C", TRUE);
#endif
  test_known();
  if (ucd)
    each_normalization_line(ucd, conformance_line);
#ifdef NS_TEST_VS_GLIB
  compare_code_points();
  compare_random();
  if (ucd)
    each_normalization_line(ucd, compare_normalization_line);
  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--ucd") == 0) {
      i++;
      continue;
    }
    compare_path(argv[i]);
  }
#endif
  report_kinds();
  printf("%ld checks, %ld failures%s\n", checks, failures,
         ucd ? "" : " (no --ucd: NormalizationTest.txt skipped)");
  return failures > 255 ? 255 : (int)failures;
}
