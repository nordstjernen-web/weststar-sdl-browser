/* Weststar SDL Browser — nscore: tests of everything but the unicode part
 * (test-unicode.c has that).
 *
 * Alone, known answers and round trips: doubles through ns_ascii_dtostr
 * and back bit for bit, C99 printf formats, key files written and read
 * back, the hash table against a reference of what it holds. Built with
 * -DNS_TEST_VS_GLIB and linked with GLib (test-nscore-vs-glib), each
 * function is also run on the same input as GLib's and the answers must
 * be the same: the strings written byte for byte, the hash tables in the
 * same iteration order. The exit status is the number of failed checks
 * (capped at 255).
 *
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "nscore.h"

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef NS_TEST_VS_GLIB
#include <glib.h>
#endif

static long failures, checks;

static void
fail(const char *what, const char *got, const char *want)
{
    failures++;
    if (failures <= 200)
        fprintf(stderr, "FAIL %s:\n  got  [%s]\n  want [%s]\n", what, got ? got : "(null)",
                want ? want : "(null)");
}

static void
check_str(const char *what, const char *got, const char *want)
{
    checks++;
    if ((got && want) ? strcmp(got, want) != 0 : got != want)
        fail(what, got, want);
}

static void
check_mem(const char *what, const void *got, size_t got_len, const void *want, size_t want_len)
{
    checks++;
    if (got_len != want_len || (got_len && memcmp(got, want, got_len) != 0)) {
        char g[64], w[64];
        snprintf(g, sizeof g, "%zu bytes", got_len);
        snprintf(w, sizeof w, "%zu bytes", want_len);
        fail(what, g, w);
    }
}

static void
check_int(const char *what, long long got, long long want)
{
    checks++;
    if (got != want) {
        char g[32], w[32];
        snprintf(g, sizeof g, "%lld", got);
        snprintf(w, sizeof w, "%lld", want);
        fail(what, g, w);
    }
}

static uint64_t rng_state = 0x9e3779b97f4a7c15ULL;

static uint64_t
rng(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

/* ---- doubles -------------------------------------------------------------- */

static void
test_doubles(void)
{
    static const double fixed[] = {
        0.0, -0.0, 1.0, -1.0, 0.1, 0.5, 1.5, 3.141592653589793, 1e300, 1e-300,
        5e-324, 2.2250738585072014e-308, 1.7976931348623157e308, 123456789012345678.0,
        0.30000000000000004, 100.0, 1e21, 1e-7, 2.5, 1.0 / 3.0,
    };
    char buf[NS_ASCII_DTOSTR_BUF_SIZE];
    for (size_t i = 0; i < 20000; i++) {
        double d;
        if (i < sizeof fixed / sizeof fixed[0]) {
            d = fixed[i];
        } else {
            uint64_t bits = rng();
            memcpy(&d, &bits, sizeof d);
            if (isnan(d) || isinf(d)) continue;
        }
        ns_ascii_dtostr(buf, sizeof buf, d);
        char *end = NULL;
        double back = ns_ascii_strtod(buf, &end);
        checks++;
        if (memcmp(&back, &d, sizeof d) != 0 || *end != '\0') fail("dtostr round trip", buf, "");
#ifdef NS_TEST_VS_GLIB
        char gbuf[G_ASCII_DTOSTR_BUF_SIZE];
        g_ascii_dtostr(gbuf, sizeof gbuf, d);
        check_str("dtostr vs GLib", buf, gbuf);
        static const char *const fmts[] = { "%.1f", "%g", "%.3e", "%.10g", "%f", "%.0f" };
        for (size_t f = 0; f < 6; f++) {
            char a[400], b[400];
            ns_ascii_formatd(a, sizeof a, fmts[f], d);
            g_ascii_formatd(b, sizeof b, fmts[f], d);
            check_str("formatd vs GLib", a, b);
        }
#endif
    }
    check_str("dtostr 0.1", ns_ascii_dtostr(buf, sizeof buf, 0.1), "0.10000000000000001");
    check_str("dtostr 1e21", ns_ascii_dtostr(buf, sizeof buf, 1e21), "1e+21");
    check_str("dtostr 100", ns_ascii_dtostr(buf, sizeof buf, 100.0), "100");

    static const char *const inputs[] = {
        "1.5", "  -2.25e3x", "0x1.8p1", "inf", "-Infinity", "nan", ".5", "5.", "1e",
        "1e+", "abc", "", "+0", "1,5", "1e400", "1e-400", "0x", "  .e1", "12.34.56",
    };
    for (size_t i = 0; i < sizeof inputs / sizeof inputs[0]; i++) {
        char *end;
        double v = ns_ascii_strtod(inputs[i], &end);
        char got[128];
        snprintf(got, sizeof got, "%.17g @%d", v, (int)(end - inputs[i]));
#ifdef NS_TEST_VS_GLIB
        char *gend;
        double gv = g_ascii_strtod(inputs[i], &gend);
        char want[128];
        snprintf(want, sizeof want, "%.17g @%d", gv, (int)(gend - inputs[i]));
        check_str(inputs[i], got, want);
#else
        (void)got;
#endif
    }
    char *end;
    check_int("strtod 1.5", (long long)(ns_ascii_strtod("1.5", &end) * 10), 15);
}

static void
test_integers(void)
{
    static const char *const inputs[] = {
        "0", "42", "-42", "  +17z", "0x1F", "0X", "0x", "077", "-0", "9223372036854775807",
        "9223372036854775808", "-9223372036854775808", "-9223372036854775809",
        "18446744073709551615", "18446744073709551616", "z", "", "  ", "1e3", "-1",
    };
    static const unsigned int bases[] = { 0, 10, 16, 8, 36 };
    for (size_t i = 0; i < sizeof inputs / sizeof inputs[0]; i++) {
        for (size_t b = 0; b < 5; b++) {
            char *e1;
            errno = 0;
            int64_t v = ns_ascii_strtoll(inputs[i], &e1, bases[b]);
            int err1 = errno;
            char *e2;
            errno = 0;
            uint64_t u = ns_ascii_strtoull(inputs[i], &e2, bases[b]);
            int err2 = errno;
            char got[160];
            snprintf(got, sizeof got, "%lld @%d e%d / %llu @%d e%d", (long long)v,
                     (int)(e1 - inputs[i]), err1, (unsigned long long)u, (int)(e2 - inputs[i]),
                     err2);
#ifdef NS_TEST_VS_GLIB
            errno = 0;
            gint64 gv = g_ascii_strtoll(inputs[i], &e1, bases[b]);
            err1 = errno;
            errno = 0;
            guint64 gu = g_ascii_strtoull(inputs[i], &e2, bases[b]);
            err2 = errno;
            char want[160];
            snprintf(want, sizeof want, "%lld @%d e%d / %llu @%d e%d", (long long)gv,
                     (int)(e1 - inputs[i]), err1, (unsigned long long)gu,
                     (int)(e2 - inputs[i]), err2);
            check_str(inputs[i], got, want);
#else
            (void)got;
#endif
        }
    }
    check_int("strtoll 0x1F", ns_ascii_strtoll("0x1F", NULL, 16), 31);
    check_int("strtoll -42", ns_ascii_strtoll("-42", NULL, 10), -42);
}

/* ---- printf --------------------------------------------------------------- */

static void
test_printf(void)
{
    /* SIZE_MAX, so that %zu is checked at the width size_t has. */
    size_t z = SIZE_MAX;
    char zbuf[32];
    snprintf(zbuf, sizeof zbuf, "%llu", (unsigned long long)z);
    long long ll = -1234567890123LL;
    int64_t i64 = INT64_MIN;
    char *s = ns_strdup_printf("%zu|%lld|%" PRId64 "|%5.2f|%-4s|%x|%c|%%", z, ll, i64, 3.14159,
                               "ab", 255u, 'q');
    char *want = ns_strdup_printf("%s|-1234567890123|-9223372036854775808| 3.14|ab  |ff|q|%%",
                                  zbuf);
    check_str("printf", s, want);
    ns_free(want);
    ns_free(s);
    char buf[8];
    check_int("snprintf length", ns_snprintf(buf, sizeof buf, "%s", "0123456789"), 10);
    check_str("snprintf truncates", buf, "0123456");
    s = ns_strdup_printf("%e|%g|%a", 1e20, 1e-5, 1.0);
    check_str("printf exponent", s, "1.000000e+20|1e-05|0x1p+0");
    ns_free(s);
    NsString *str = ns_string_new("x");
    ns_string_append_printf(str, "%llu-%zd", 18446744073709551615ULL, (ptrdiff_t)-5);
    check_str("string printf", str->str, "x18446744073709551615--5");
    ns_string_free(str, 1);
}

/* ---- strings ----------------------------------------------------------- */

static char *
join(char **v)
{
    char *r = ns_strjoinv("|", v);
    char *q = ns_strdup_printf("%u:%s", ns_strv_length(v), r);
    ns_free(r);
    return q;
}

static void
test_strings(void)
{
    static const char *const split_in[] = { "", "a", "a,b", ",a,,b,", "a,,", ",,,", "abc" };
    static const char *const delims[] = { ",", ",,", "ab" };
    static const int max_tokens[] = { -1, 0, 1, 2, 3 };
    for (size_t i = 0; i < 7; i++) {
        for (size_t d = 0; d < 3; d++) {
            for (size_t m = 0; m < 5; m++) {
                char **v = ns_strsplit(split_in[i], delims[d], max_tokens[m]);
                char **w = ns_strsplit_set(split_in[i], delims[d], max_tokens[m]);
                char *a = join(v), *b = join(w);
#ifdef NS_TEST_VS_GLIB
                char **gv = g_strsplit(split_in[i], delims[d], max_tokens[m]);
                char **gw = g_strsplit_set(split_in[i], delims[d], max_tokens[m]);
                char *ga = g_strjoinv("|", gv), *gb = g_strjoinv("|", gw);
                char *wa = g_strdup_printf("%u:%s", g_strv_length(gv), ga);
                char *wb = g_strdup_printf("%u:%s", g_strv_length(gw), gb);
                check_str("strsplit", a, wa);
                check_str("strsplit_set", b, wb);
                g_free(ga); g_free(gb); g_free(wa); g_free(wb);
                g_strfreev(gv); g_strfreev(gw);
#endif
                ns_free(a); ns_free(b);
                ns_strfreev(v); ns_strfreev(w);
            }
        }
    }
    char **v = ns_strsplit(",a,,b,", ",", -1);
    char *j = join(v);
    check_str("strsplit known", j, "5:|a||b|");
    ns_free(j);
    ns_strfreev(v);

    char s1[] = "  \t hi there \n ";
    check_str("strstrip", ns_strstrip(s1), "hi there");
    check_int("strcmp0 NULL", ns_strcmp0(NULL, "a"), -1);
    check_int("strcasecmp", ns_ascii_strcasecmp("HeLLo", "hello"), 0);
    check_str("strstr_len", ns_strstr_len("hello world", 8, "wo"), "world");
    check_str("strstr_len cut", ns_strstr_len("hello world", 7, "wo"), NULL);
    check_str("strrstr_len", ns_strrstr_len("abcabc", -1, "bc"), "bc");
    check_str("strrstr_len cut", ns_strrstr_len("abcabc", 4, "bc"), "bcabc");

    unsigned char all[256];
    for (int i = 1; i < 256; i++) all[i - 1] = (unsigned char)i;
    all[255] = 0;
    char *esc = ns_strescape((const char *)all, "\t\xe9");
#ifdef NS_TEST_VS_GLIB
    char *gesc = g_strescape((const char *)all, "\t\xe9");
    check_str("strescape", esc, gesc);
    g_free(gesc);
#endif
    ns_free(esc);
    esc = ns_strescape("a\"b\\c\n\x01\xff", NULL);
    check_str("strescape known", esc, "a\\\"b\\\\c\\n\\001\\377");
    ns_free(esc);

    static const uint64_t sizes[] = { 0, 1, 2, 999, 1000, 1049, 1050, 999999, 1000000,
                                      123456789, 1000000000000000000ULL, UINT64_MAX };
    for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        char *f = ns_format_size(sizes[i]);
#ifdef NS_TEST_VS_GLIB
        char *g = g_format_size(sizes[i]);
        /* Older GLib put a plain space between number and unit where newer
         * ones (2.90 here, and nscore) put a no-break space. */
        char *nb = strstr(g, " ");
        if (nb && sizes[i] >= 1000) {
            char *fixed = g_strdup_printf("%.*s\xc2\xa0%s", (int)(nb - g), g, nb + 1);
            g_free(g);
            g = fixed;
        }
        check_str("format_size", f, g);
        g_free(g);
#endif
        ns_free(f);
    }
    char *fs = ns_format_size(1500);
    check_str("format_size known", fs, "1.5\xc2\xa0kB");
    ns_free(fs);

    NsString *gs = ns_string_new("hello");
    ns_string_insert_len(gs, 2, gs->str + 1, 3);  /* from inside itself */
    ns_string_append_unichar(gs, 0x20ac);
    ns_string_append_unichar(gs, 0x1f600);
    ns_string_erase(gs, 0, 1);
    ns_string_prepend_c(gs, '<');
    ns_string_insert_c(gs, -1, '>');
#ifdef NS_TEST_VS_GLIB
    GString *g = g_string_new("hello");
    g_string_insert_len(g, 2, g->str + 1, 3);
    g_string_append_unichar(g, 0x20ac);
    g_string_append_unichar(g, 0x1f600);
    g_string_erase(g, 0, 1);
    g_string_prepend_c(g, '<');
    g_string_insert_c(g, -1, '>');
    check_str("GString edits", gs->str, g->str);
    g_string_free(g, TRUE);
#endif
    check_str("GString known", gs->str, "<eellllo\xe2\x82\xac\xf0\x9f\x98\x80>");
    ns_string_free(gs, 1);
}

/* ---- hash tables ---------------------------------------------------------- */

static char *
ns_order(NsHashTable *t)
{
    NsString *s = ns_string_new(NULL);
    NsHashTableIter it;
    void *k, *v;
    ns_hash_table_iter_init(&it, t);
    while (ns_hash_table_iter_next(&it, &k, &v)) ns_string_append_printf(s, "%s,", (char *)k);
    ns_string_append(s, "/");
    NsList *keys = ns_hash_table_get_keys(t);
    for (NsList *l = keys; l; l = l->next) ns_string_append_printf(s, "%s,", (char *)l->data);
    ns_list_free(keys);
    return ns_string_free(s, 0);
}

#ifdef NS_TEST_VS_GLIB
static char *
g_order(GHashTable *t)
{
    GString *s = g_string_new(NULL);
    GHashTableIter it;
    gpointer k, v;
    g_hash_table_iter_init(&it, t);
    while (g_hash_table_iter_next(&it, &k, &v)) g_string_append_printf(s, "%s,", (char *)k);
    g_string_append(s, "/");
    GList *keys = g_hash_table_get_keys(t);
    for (GList *l = keys; l; l = l->next) g_string_append_printf(s, "%s,", (char *)l->data);
    g_list_free(keys);
    return g_string_free(s, FALSE);
}
#endif

static void
test_hash(void)
{
    check_int("str_hash", ns_str_hash("hello"), 261238937);
#ifdef NS_TEST_VS_GLIB
    static const char *const hs[] = { "", "a", "hello", "\xc3\xa9t\xc3\xa9", "\xff\x80~" };
    for (size_t i = 0; i < 5; i++) check_int(hs[i], ns_str_hash(hs[i]), g_str_hash(hs[i]));
#endif

    NsHashTable *t = ns_hash_table_new_full(ns_str_hash, ns_str_equal, ns_free, NULL);
#ifdef NS_TEST_VS_GLIB
    GHashTable *g = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
#endif
    for (int round = 0; round < 6000; round++) {
        char key[32];
        snprintf(key, sizeof key, "k%u", (unsigned int)(rng() % 3000));
        unsigned int op = (unsigned int)(rng() % 10);
        if (op < 6) {
            ns_hash_table_replace(t, ns_strdup(key), NULL);
#ifdef NS_TEST_VS_GLIB
            g_hash_table_replace(g, g_strdup(key), NULL);
#endif
        } else if (op < 9) {
            ns_hash_table_remove(t, key);
#ifdef NS_TEST_VS_GLIB
            g_hash_table_remove(g, key);
#endif
        } else if (rng() % 50 == 0) {
            ns_hash_table_remove_all(t);
#ifdef NS_TEST_VS_GLIB
            g_hash_table_remove_all(g);
#endif
        }
        if (round % 500 == 0 || round == 5999) {
            char *a = ns_order(t);
#ifdef NS_TEST_VS_GLIB
            char *b = g_order(g);
            check_str("hash table order", a, b);
            g_free(b);
            check_int("hash size", ns_hash_table_size(t), g_hash_table_size(g));
#endif
            ns_free(a);
        }
    }
    /* Iterating and removing every other one. */
    NsHashTableIter it;
    void *k;
    int n = 0;
    ns_hash_table_iter_init(&it, t);
    while (ns_hash_table_iter_next(&it, &k, NULL))
        if (n++ % 2) ns_hash_table_iter_remove(&it);
#ifdef NS_TEST_VS_GLIB
    GHashTableIter git;
    gpointer gk;
    n = 0;
    g_hash_table_iter_init(&git, g);
    while (g_hash_table_iter_next(&git, &gk, NULL))
        if (n++ % 2) g_hash_table_iter_remove(&git);
    for (int i = 0; i < 40; i++) {
        char key[16];
        snprintf(key, sizeof key, "n%d", i);
        ns_hash_table_insert(t, ns_strdup(key), NULL);
        g_hash_table_insert(g, g_strdup(key), NULL);
    }
    char *a = ns_order(t), *b = g_order(g);
    check_str("hash order after iter_remove", a, b);
    ns_free(a);
    g_free(b);
    g_hash_table_destroy(g);
#endif
    ns_hash_table_destroy(t);

    /* Direct keys. */
    NsHashTable *d = ns_hash_table_new(NULL, NULL);
#ifdef NS_TEST_VS_GLIB
    GHashTable *gd = g_hash_table_new(NULL, NULL);
#endif
    for (unsigned int i = 0; i < 2000; i++) {
        uintptr_t key = (uintptr_t)(rng() % 100000) * 8;
        ns_hash_table_add(d, (void *)key);
#ifdef NS_TEST_VS_GLIB
        g_hash_table_add(gd, (gpointer)key);
#endif
    }
    NsString *sa = ns_string_new(NULL);
    ns_hash_table_iter_init(&it, d);
    while (ns_hash_table_iter_next(&it, &k, NULL))
        ns_string_append_printf(sa, "%lu,", (unsigned long)(uintptr_t)k);
#ifdef NS_TEST_VS_GLIB
    GString *sb = g_string_new(NULL);
    g_hash_table_iter_init(&git, gd);
    while (g_hash_table_iter_next(&git, &gk, NULL))
        g_string_append_printf(sb, "%lu,", (unsigned long)(uintptr_t)gk);
    check_str("direct hash order", sa->str, sb->str);
    g_string_free(sb, TRUE);
    g_hash_table_destroy(gd);
#endif
    ns_string_free(sa, 1);
    ns_hash_table_destroy(d);
}

/* ---- sorting (stable, as GLib's) ----------------------------------------- */

static int
cmp_mod(const void *a, const void *b)
{
    int x = *(const int *)a % 7, y = *(const int *)b % 7;
    return (x > y) - (x < y);
}

static int
cmp_ptr_mod(const void *a, const void *b)
{
    int x = (int)(intptr_t) * (void *const *)a % 5, y = (int)(intptr_t) * (void *const *)b % 5;
    return (x > y) - (x < y);
}

static int
cmp_list_mod(const void *a, const void *b)
{
    int x = (int)(intptr_t)a % 3, y = (int)(intptr_t)b % 3;
    return (x > y) - (x < y);
}

static void
test_sort(void)
{
    NsArray *a = ns_array_new(0, 0, sizeof(int));
    NsPtrArray *p = ns_ptr_array_new();
    NsList *l = NULL;
    for (int i = 0; i < 500; i++) {
        int v = (int)(rng() % 1000);
        ns_array_append_val(a, v);
        ns_ptr_array_add(p, (void *)(intptr_t)v);
        l = ns_list_prepend(l, (void *)(intptr_t)v);
    }
    int *copy = ns_memdup2(a->data, a->len * sizeof(int));
    ns_array_sort(a, cmp_mod);
    ns_ptr_array_sort(p, cmp_ptr_mod);
    l = ns_list_sort(l, cmp_list_mod);
    /* Stable: equal keys keep their order. */
    int stable = 1;
    for (unsigned int i = 1; i < a->len; i++) {
        int x = ns_array_index(a, int, i - 1), y = ns_array_index(a, int, i);
        if (x % 7 > y % 7) stable = 0;
        if (x % 7 == y % 7) {
            int fx = -1, fy = -1;
            for (int j = 0; j < 500; j++) {
                if (copy[j] == x && fx < 0) fx = j;
                if (copy[j] == y) fy = j;
            }
            if (fx > fy) stable = 0;
        }
    }
    check_int("array sort stable", stable, 1);
    ns_free(copy);
    ns_array_free(a, 1);
    ns_ptr_array_free(p, 1);
    ns_list_free(l);
}

/* ---- key files ----------------------------------------------------------- */

static void
test_keyfile(void)
{
    static const char *const values[] = {
        "plain", " leading space", "\ttab", "a\nb\r\nc", "back\\slash", "trailing ",
        "", "semi;colon", "caf\xc3\xa9", "  \\ x", "=equals=", "#hash",
    };
    NsKeyFile *kf = ns_key_file_new();
#ifdef NS_TEST_VS_GLIB
    GKeyFile *gk = g_key_file_new();
#endif
    for (int i = 0; i < 40; i++) {
        char group[16], key[32];
        snprintf(group, sizeof group, "g%u", (unsigned int)(rng() % 4));
        snprintf(key, sizeof key, "k:%u key", (unsigned int)(rng() % 6));
        const char *v = values[rng() % (sizeof values / sizeof values[0])];
        ns_key_file_set_string(kf, group, key, v);
#ifdef NS_TEST_VS_GLIB
        g_key_file_set_string(gk, group, key, v);
#endif
        if (i == 25) {
            ns_key_file_remove_group(kf, "g1", NULL);
#ifdef NS_TEST_VS_GLIB
            g_key_file_remove_group(gk, "g1", NULL);
#endif
        }
    }
    size_t len;
    char *data = ns_key_file_to_data(kf, &len, NULL);
#ifdef NS_TEST_VS_GLIB
    gsize glen;
    char *gdata = g_key_file_to_data(gk, &glen, NULL);
    check_mem("key file written as GLib writes it", data, len, gdata, glen);
    if (len != glen || memcmp(data, gdata, len)) fprintf(stderr, "ns:\n%s\nglib:\n%s\n", data, gdata);

    /* What GLib wrote, read by nscore and written again; and the reverse. */
    NsKeyFile *back = ns_key_file_new();
    check_int("load GLib's", ns_key_file_load_from_data(back, gdata, glen, NS_KEY_FILE_NONE, NULL), 1);
    size_t blen;
    char *bdata = ns_key_file_to_data(back, &blen, NULL);
    check_mem("GLib's file round trip", bdata, blen, gdata, glen);
    GKeyFile *gback = g_key_file_new();
    g_key_file_load_from_data(gback, data, len, G_KEY_FILE_NONE, NULL);
    size_t n_groups;
    char **groups = ns_key_file_get_groups(back, &n_groups);
    gsize gn;
    char **ggroups = g_key_file_get_groups(gback, &gn);
    char *a = join(groups), *b = g_strjoinv("|", ggroups);
    char *wb = g_strdup_printf("%u:%s", g_strv_length(ggroups), b);
    check_str("groups", a, wb);
    ns_free(a); g_free(b); g_free(wb);
    for (size_t i = 0; groups[i]; i++) {
        char **keys = ns_key_file_get_keys(back, groups[i], NULL, NULL);
        char **gkeys = g_key_file_get_keys(gback, groups[i], NULL, NULL);
        a = join(keys);
        b = g_strjoinv("|", gkeys);
        wb = g_strdup_printf("%u:%s", g_strv_length(gkeys), b);
        check_str("keys", a, wb);
        ns_free(a); g_free(b); g_free(wb);
        for (size_t k = 0; keys[k]; k++) {
            char *v = ns_key_file_get_string(back, groups[i], keys[k], NULL);
            char *gv = g_key_file_get_string(gback, groups[i], keys[k], NULL);
            check_str("value", v, gv);
            ns_free(v);
            g_free(gv);
        }
        ns_strfreev(keys);
        g_strfreev(gkeys);
    }
    ns_strfreev(groups);
    g_strfreev(ggroups);
    ns_free(bdata);
    ns_key_file_free(back);
    g_key_file_free(gback);
    g_free(gdata);

    /* Hand-written files, odd ones included: each loaded by both. */
    static const char *const files[] = {
        "[a]\nx=1\n\n[b]\ny = 2 \n",
        "# comment\n\n[a]\r\nk=v\r\n  [b]  \nk2=v2\n[a]\nk3=v3\n",
        "[a]\nk=1\nk=2\n",
        "[a]\nName[de]=x\nName[C]=y\nName=z\n",
        "k=v\n",
        "[a]\nnot a pair\n",
        "[a b]\n=x\n",
        "[a]\nbad\\q=1\nv=\\q\n",
        "[a]\nk=v\\",
        "",
        "\n\n",
        "[Desktop Entry]\nEncoding=Latin1\n",
        "[a]\n[b]\n[c]\nk=1\n",
    };
    for (size_t f = 0; f < sizeof files / sizeof files[0]; f++) {
        NsKeyFile *x = ns_key_file_new();
        GKeyFile *gx = g_key_file_new();
        NsError *e = NULL;
        GError *ge = NULL;
        int ok = ns_key_file_load_from_data(x, files[f], (size_t)-1, NS_KEY_FILE_NONE, &e);
        int gok = g_key_file_load_from_data(gx, files[f], (gsize)-1, G_KEY_FILE_NONE, &ge);
        check_int(files[f], ok, gok);
        check_int("error code", e ? e->code : -1, ge ? ge->code : -1);
        ns_key_file_set_string(x, "a", "added", "new");
        g_key_file_set_string(gx, "a", "added", "new");
        size_t xl;
        gsize gxl;
        char *xd = ns_key_file_to_data(x, &xl, NULL);
        char *gxd = g_key_file_to_data(gx, &gxl, NULL);
        check_mem(files[f], xd, xl, gxd, gxl);
        char *v = ns_key_file_get_string(x, "a", "k", NULL);
        char *gv = g_key_file_get_string(gx, "a", "k", NULL);
        check_str("get_string", v, gv);
        ns_free(v); g_free(gv);
        ns_free(xd); g_free(gxd);
        ns_clear_error(&e);
        g_clear_error(&ge);
        ns_key_file_free(x);
        g_key_file_free(gx);
    }
    g_key_file_free(gk);
#endif
    /* Round trip of every value through nscore alone. */
    NsKeyFile *rt = ns_key_file_new();
    ns_key_file_load_from_data(rt, data, len, NS_KEY_FILE_NONE, NULL);
    size_t rlen;
    char *rdata = ns_key_file_to_data(rt, &rlen, NULL);
    check_mem("key file round trip", rdata, rlen, data, len);
    ns_free(rdata);
    ns_key_file_free(rt);
    ns_free(data);
    ns_key_file_free(kf);

    NsKeyFile *k2 = ns_key_file_new();
    ns_key_file_set_string(k2, "meta", "origin", "https://example.com");
    ns_key_file_set_string(k2, "items", "k:a%20b", " x\ty\n");
    data = ns_key_file_to_data(k2, &len, NULL);
    check_str("key file known", data,
              "[meta]\norigin=https://example.com\n\n[items]\nk:a%20b=\\sx\ty\\n\n");
    ns_free(data);
    char *v = ns_key_file_get_string(k2, "items", "k:a%20b", NULL);
    check_str("key file value", v, " x\ty\n");
    ns_free(v);
    ns_key_file_free(k2);
}

/* ---- encodings, URIs, paths, checksums ---------------------------------------- */

static void
test_encodings(void)
{
    for (int round = 0; round < 300; round++) {
        size_t n = (size_t)(rng() % 70);
        unsigned char buf[80];
        for (size_t i = 0; i < n; i++) buf[i] = (unsigned char)rng();
        char *e = ns_base64_encode(buf, n);
        size_t dl;
        unsigned char *d = ns_base64_decode(e, &dl);
        check_mem("base64 round trip", d, dl, buf, n);
        ns_free(d);
#ifdef NS_TEST_VS_GLIB
        char *ge = g_base64_encode(buf, n);
        check_str("base64 vs GLib", e, ge);
        g_free(ge);
        /* Junk mixed in. */
        char junk[120];
        size_t jn = 0;
        for (size_t i = 0; e[i] && jn < sizeof junk - 2; i++) {
            if (rng() % 5 == 0) junk[jn++] = " \n=*-_"[rng() % 6];
            junk[jn++] = e[i];
        }
        junk[jn] = '\0';
        d = ns_base64_decode(junk, &dl);
        gsize gdl;
        guchar *gd = g_base64_decode(junk, &gdl);
        check_mem("base64 decode junk", d, dl, gd, gdl);
        g_free(gd);
        ns_free(d);

        char text[40];
        for (size_t i = 0; i < 39; i++) text[i] = (char)(rng() % 255 + 1);
        text[39] = '\0';
        char *m = ns_markup_escape_text(text, -1);
        char *gm = g_markup_escape_text(text, -1);
        check_str("markup", m, gm);
        ns_free(m); g_free(gm);
        for (int u = 0; u < 2; u++) {
            char *x = ns_uri_escape_string(text, u ? "/:" : NULL, u);
            char *gx = g_uri_escape_string(text, u ? "/:" : NULL, u);
            check_str("uri escape", x, gx);
            char *y = ns_uri_unescape_string(x, NULL);
            char *gy = g_uri_unescape_string(gx, NULL);
            check_str("uri unescape", y, gy);
            ns_free(x); g_free(gx); ns_free(y); g_free(gy);
        }
        unsigned char data[100];
        size_t dn = (size_t)(rng() % 100);
        for (size_t i = 0; i < dn; i++) data[i] = (unsigned char)rng();
        static const NsChecksumType kinds[] = { NS_CHECKSUM_SHA256, NS_CHECKSUM_SHA384,
                                                NS_CHECKSUM_SHA512 };
        for (int k = 0; k < 3; k++) {
            char *c = ns_compute_checksum_for_data(kinds[k], data, dn);
            char *gc = g_compute_checksum_for_data((GChecksumType)kinds[k], data, dn);
            check_str("checksum", c, gc);
            ns_free(c); g_free(gc);
        }
#endif
        ns_free(e);
    }
    char *c = ns_compute_checksum_for_string(NS_CHECKSUM_SHA256, "abc", -1);
    check_str("sha256 abc", c, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    ns_free(c);
    NsChecksum *ck = ns_checksum_new(NS_CHECKSUM_SHA512);
    for (int i = 0; i < 1000; i++) ns_checksum_update(ck, (const unsigned char *)"aaaaaaaaaa", 10);
    unsigned char dig[64];
    size_t dlen = sizeof dig;
    ns_checksum_get_digest(ck, dig, &dlen);
    check_int("sha512 digest length", (long long)dlen, 64);
    check_str("sha512 10000 a", ns_checksum_get_string(ck),
              "0593036f4f479d2eb8078ca26b1d59321a86bdfcb04cb40043694f1eb0301b8a"
              "cd20b936db3c916ebcc1b609400ffcf3fa8d569d7e39293855668645094baf0e");
    ns_checksum_free(ck);

    static const char *const uris[] = {
        "a=1&b=2", "a=1&&b=2", "a+b=c%20d&e=%zz", "x=%C3%A9", "x=%FF", "=v", "k=", "",
        "a=1;b=2",
    };
    for (size_t i = 0; i < sizeof uris / sizeof uris[0]; i++) {
        NsHashTable *t = ns_uri_parse_params(uris[i], -1, "&", NS_URI_PARAMS_WWW_FORM, NULL);
        NsString *s = ns_string_new(t ? "" : "NULL");
        if (t) {
            NsHashTableIter it;
            void *k, *v;
            ns_hash_table_iter_init(&it, t);
            while (ns_hash_table_iter_next(&it, &k, &v))
                ns_string_append_printf(s, "[%s]=[%s];", (char *)k, (char *)v);
            ns_hash_table_destroy(t);
        }
#ifdef NS_TEST_VS_GLIB
        GHashTable *gt = g_uri_parse_params(uris[i], -1, "&", G_URI_PARAMS_WWW_FORM, NULL);
        GString *gs = g_string_new(gt ? "" : "NULL");
        if (gt) {
            GHashTableIter it;
            gpointer k, v;
            g_hash_table_iter_init(&it, gt);
            while (g_hash_table_iter_next(&it, &k, &v))
                g_string_append_printf(gs, "[%s]=[%s];", (char *)k, (char *)v);
            g_hash_table_destroy(gt);
        }
        check_str(uris[i], s->str, gs->str);
        g_string_free(gs, TRUE);
#endif
        ns_string_free(s, 1);
    }
    static const char *const unesc[] = { "a%20b", "%", "%4", "%41", "%00", "a%2Fb", "%e9" };
    for (size_t i = 0; i < sizeof unesc / sizeof unesc[0]; i++) {
        for (int ill = 0; ill < 2; ill++) {
            char *x = ns_uri_unescape_string(unesc[i], ill ? "/" : NULL);
#ifdef NS_TEST_VS_GLIB
            char *gx = g_uri_unescape_string(unesc[i], ill ? "/" : NULL);
            check_str(unesc[i], x, gx);
            g_free(gx);
#endif
            ns_free(x);
        }
    }
}

static void
test_paths(void)
{
#ifdef _WIN32
    static const char *const names[] = {
        "C:\\a\\b.txt", "C:/a/b/", "C:\\", "C:", "C:foo", "\\\\server\\share",
        "\\\\server\\share\\x", "//server/share/x/y", "a\\b/c", "/", "", ".", "..",
        "a", "C:\\a\\..\\..\\b", "C:\\a\\.\\b\\\\c\\", "\\a\\b", "a//b", "C:\\x y\\%#?\xc3\xa9",
    };
#else
    static const char *const names[] = {
        "/a/b.txt", "/a/b/", "/", "//", "///x", "//x/y", "a/b", "", ".", "..", "a",
        "/a/../../b", "/a/./b//c/", "a//b", "/x y/%#?\xc3\xa9", "/a/b/..", "/.",
    };
#endif
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        const char *n = names[i];
        char *r[6];
        r[0] = ns_path_get_basename(n);
        r[1] = ns_path_get_dirname(n);
        r[2] = ns_build_filename(n, "x", NULL);
        r[3] = ns_build_filename("base", n, "", n, NULL);
#ifdef _WIN32
        r[4] = ns_canonicalize_filename(n, "C:\\base\\dir");
#else
        r[4] = ns_canonicalize_filename(n, "/base/dir");
#endif
        r[5] = ns_path_is_absolute(n) ? ns_filename_to_uri(n, NULL, NULL) : ns_strdup("rel");
#ifdef NS_TEST_VS_GLIB
        char *g[6];
        g[0] = g_path_get_basename(n);
        g[1] = g_path_get_dirname(n);
        g[2] = g_build_filename(n, "x", NULL);
        g[3] = g_build_filename("base", n, "", n, NULL);
#ifdef _WIN32
        g[4] = g_canonicalize_filename(n, "C:\\base\\dir");
#else
        g[4] = g_canonicalize_filename(n, "/base/dir");
#endif
        g[5] = g_path_is_absolute(n) ? g_filename_to_uri(n, NULL, NULL) : g_strdup("rel");
        static const char *const what[] = { "basename", "dirname", "build_filename",
                                            "build_filename 4", "canonicalize", "to_uri" };
        for (int k = 0; k < 6; k++) {
            char label[128];
            snprintf(label, sizeof label, "%s(%s)", what[k], n);
            check_str(label, r[k], g[k]);
            g_free(g[k]);
        }
        if (ns_path_is_absolute(n)) {
            char *back = ns_filename_from_uri(r[5], NULL, NULL);
            char *gback = g_filename_from_uri(r[5], NULL, NULL);
            check_str("from_uri", back, gback);
            ns_free(back);
            g_free(gback);
        }
#endif
        for (int k = 0; k < 6; k++) ns_free(r[k]);
    }
    static const char *const uris[] = {
        "file:///a/b%20c", "file://localhost/x", "file://host/x", "file:/x", "FILE:///X?q#f",
        "file:///C:/x", "file:///c|/x", "http://x/", "file://bad_host/x", "file:///a%2Fb",
        "file:///a%00b", "file://ex%41mple.com/x",
    };
    for (size_t i = 0; i < sizeof uris / sizeof uris[0]; i++) {
        char *host = NULL;
        char *f = ns_filename_from_uri(uris[i], &host, NULL);
        char *got = ns_strdup_printf("%s|%s", f ? f : "NULL", host ? host : "NULL");
#ifdef NS_TEST_VS_GLIB
        char *ghost = NULL;
        char *gf = g_filename_from_uri(uris[i], &ghost, NULL);
        char *want = g_strdup_printf("%s|%s", gf ? gf : "NULL", ghost ? ghost : "NULL");
        check_str(uris[i], got, want);
        g_free(want); g_free(gf); g_free(ghost);
#endif
        ns_free(got); ns_free(f); ns_free(host);
    }
#ifdef NS_TEST_VS_GLIB
    static const char *const programs[] = { "sh", "cmd", "cmd.exe", "no-such-program-x",
                                            "./no-such", "/bin/sh", "C:\\Windows\\notepad.exe" };
    for (size_t i = 0; i < sizeof programs / sizeof programs[0]; i++) {
        char *p = ns_find_program_in_path(programs[i]);
        char *gp = g_find_program_in_path(programs[i]);
        check_str(programs[i], p, gp);
        ns_free(p);
        g_free(gp);
    }
#endif
#ifdef _WIN32
    char *u = ns_filename_to_uri("C:\\x y\\a.html", NULL, NULL);
    check_str("to_uri known", u, "file:///C:/x%20y/a.html");
#else
    char *u = ns_filename_to_uri("/x y/a.html", NULL, NULL);
    check_str("to_uri known", u, "file:///x%20y/a.html");
#endif
    ns_free(u);
}

/* ---- logging ---------------------------------------------------------- */

/* The formatted line without its time of day (hh:mm:ss.mmm). */
static void
strip_time(char *s)
{
    for (char *p = s; *p; p++) {
        if (p[0] >= '0' && p[0] <= '9' && p[2] == ':' && p[5] == ':' && p[8] == '.') {
            memmove(p, p + 12, strlen(p + 12) + 1);
            return;
        }
    }
}

static void
test_log(void)
{
    static const char *const messages[] = { "plain", "tab\there\nnew", "ctl\x01\x7f",
                                            "bad \xff utf8", "cr\rx", "crlf\r\n", "\xc2\x85" };
    static const int levels[] = { NS_LOG_LEVEL_WARNING, NS_LOG_LEVEL_CRITICAL,
                                  NS_LOG_LEVEL_MESSAGE, NS_LOG_LEVEL_INFO,
                                  NS_LOG_LEVEL_DEBUG, NS_LOG_LEVEL_ERROR, 1 << 9 };
    for (size_t m = 0; m < sizeof messages / sizeof messages[0]; m++) {
        for (size_t l = 0; l < sizeof levels / sizeof levels[0]; l++) {
            for (int dom = 0; dom < 2; dom++) {
                NsLogField f[4] = {
                    { "GLIB_OLD_LOG_API", "1", -1 },
                    { "MESSAGE", messages[m], -1 },
                    { "PRIORITY", "4", -1 },
                    { "GLIB_DOMAIN", "dom", -1 },
                };
                char *s = ns_log_writer_format_fields((NsLogLevelFlags)levels[l], f,
                                                      dom ? 4 : 3, 0);
                strip_time(s);
#ifdef NS_TEST_VS_GLIB
                GLogField gf[4] = {
                    { "GLIB_OLD_LOG_API", "1", -1 },
                    { "MESSAGE", messages[m], -1 },
                    { "PRIORITY", "4", -1 },
                    { "GLIB_DOMAIN", "dom", -1 },
                };
                char *g = g_log_writer_format_fields((GLogLevelFlags)levels[l], gf,
                                                     dom ? 4 : 3, FALSE);
                strip_time(g);
                check_str("log format", s, g);
                g_free(g);
#endif
                ns_free(s);
            }
        }
    }
}

int
main(void)
{
    test_doubles();
    test_integers();
    test_printf();
    test_strings();
    test_hash();
    test_sort();
    test_keyfile();
    test_encodings();
    test_paths();
    test_log();
    printf("%ld checks, %ld failed\n", checks, failures);
    return failures > 255 ? 255 : (int)failures;
}
