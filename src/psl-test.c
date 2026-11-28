/* Weststar SDL Browser — tests of the compiled-in Public Suffix List.
 *
 *   psl-test                 known answers
 *   psl-test RULES-FILE      also against libpsl (built with NS_PSL_TEST_LIBPSL)
 *
 * Against libpsl, the table is written out as a list (RULES-FILE) that
 * libpsl loads, so both answer from the same rules: every entry, names
 * around each and edge cases are asked of both, and every answer must be
 * the same. The known answers are asked of libpsl's own built-in list as
 * well, which may be of another date, so only rules that do not change.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "psl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef NS_PSL_TEST_LIBPSL
#include <libpsl.h>
#endif

static int failures;

/* name and its registrable domain (NULL: none). */
static const char *const known[][2] = {
    { "com", NULL },
    { "example.com", "example.com" },
    { "www.example.com", "example.com" },
    { "a.b.c.example.com", "example.com" },
    { "example", NULL },
    { "example.example", "example.example" },
    { "b.example.example", "example.example" },
    { "localhost", NULL },
    { "", NULL },
    { ".com", NULL },
    { ".example.com", NULL },
    { "example.com.", "example.com." },
    { "www.example.com.", "example.com." },
    { "co.uk", NULL },
    { "example.co.uk", "example.co.uk" },
    { "www.example.co.uk", "example.co.uk" },
    { "uk.com", NULL },
    { "example.uk.com", "example.uk.com" },
    { "b.example.uk.com", "example.uk.com" },
    /* *.ck and !www.ck */
    { "ck", NULL },
    { "test.ck", NULL },
    { "b.test.ck", "b.test.ck" },
    { "a.b.test.ck", "b.test.ck" },
    { "www.ck", "www.ck" },
    { "www.www.ck", "www.ck" },
    /* *.kawasaki.jp and !city.kawasaki.jp */
    { "kawasaki.jp", NULL },
    { "c.kawasaki.jp", NULL },
    { "b.c.kawasaki.jp", "b.c.kawasaki.jp" },
    { "a.b.c.kawasaki.jp", "b.c.kawasaki.jp" },
    { "city.kawasaki.jp", "city.kawasaki.jp" },
    { "www.city.kawasaki.jp", "city.kawasaki.jp" },
    { "us", NULL },
    { "test.us", "test.us" },
    { "www.test.us", "test.us" },
    { "ak.us", NULL },
    { "test.ak.us", "test.ak.us" },
    { "k12.ak.us", NULL },
    { "test.k12.ak.us", "test.k12.ak.us" },
    { "www.test.k12.ak.us", "test.k12.ak.us" },
    /* The private section. */
    { "github.io", NULL },
    { "example.github.io", "example.github.io" },
    { "www.example.github.io", "example.github.io" },
    { "blogspot.com", NULL },
    { "example.blogspot.com", "example.blogspot.com" },
    /* IDN, in UTF-8 and as A-labels. */
    { "\xe5\x85\xac\xe5\x8f\xb8.cn", NULL },
    { "\xe9\xa3\x9f\xe7\x8b\xae.com.cn", "\xe9\xa3\x9f\xe7\x8b\xae.com.cn" },
    { "\xe9\xa3\x9f\xe7\x8b\xae.\xe5\x85\xac\xe5\x8f\xb8.cn",
      "\xe9\xa3\x9f\xe7\x8b\xae.\xe5\x85\xac\xe5\x8f\xb8.cn" },
    { "www.\xe9\xa3\x9f\xe7\x8b\xae.\xe5\x85\xac\xe5\x8f\xb8.cn",
      "\xe9\xa3\x9f\xe7\x8b\xae.\xe5\x85\xac\xe5\x8f\xb8.cn" },
    { "shishi.\xe5\x85\xac\xe5\x8f\xb8.cn", "shishi.\xe5\x85\xac\xe5\x8f\xb8.cn" },
    { "xn--55qx5d.cn", NULL },
    { "xn--85x722f.xn--55qx5d.cn", "xn--85x722f.xn--55qx5d.cn" },
    { "www.xn--85x722f.xn--55qx5d.cn", "xn--85x722f.xn--55qx5d.cn" },
    { "shishi.xn--55qx5d.cn", "shishi.xn--55qx5d.cn" },
    { "\xe4\xb8\xad\xe5\x9b\xbd", NULL },
    { "www.\xe4\xb8\xad\xe5\x9b\xbd", "www.\xe4\xb8\xad\xe5\x9b\xbd" },
    { "xn--fiqs8s", NULL },
    { "www.xn--fiqs8s", "www.xn--fiqs8s" },
    /* No IP address is special to the list. */
    { "192.168.0.1", "0.1" },
};

static const char *
show(const char *s)
{
    return s ? s : "(none)";
}

static void
check_known(void)
{
    for (size_t i = 0; i < sizeof(known) / sizeof(known[0]); i++) {
        const char *name = known[i][0], *want = known[i][1];
        const char *got = ns_psl_registrable_domain(name);
        if ((got == NULL) != (want == NULL) || (got && strcmp(got, want))) {
            fprintf(stderr, "FAIL registrable(%s) = %s, want %s\n",
                    name, show(got), show(want));
            failures++;
        }
    }
    static const char *const suffixes[] = {
        "com", "co.uk", "co.uk.", "uk.com", "test.ck", "ck", "github.io",
        "example", "", ".", ".com", "\xe5\x85\xac\xe5\x8f\xb8.cn",
        "xn--55qx5d.cn", "c.kawasaki.jp",
    };
    static const char *const not_suffixes[] = {
        "example.com", "www.ck", "city.kawasaki.jp", "example.github.io",
        "foo.test.ck.", "example.co.uk",
    };
    for (size_t i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); i++)
        if (!ns_psl_is_public_suffix(suffixes[i])) {
            fprintf(stderr, "FAIL is_public_suffix(%s) false\n", suffixes[i]);
            failures++;
        }
    for (size_t i = 0; i < sizeof(not_suffixes) / sizeof(not_suffixes[0]); i++)
        if (ns_psl_is_public_suffix(not_suffixes[i])) {
            fprintf(stderr, "FAIL is_public_suffix(%s) true\n", not_suffixes[i]);
            failures++;
        }
    if (!ns_psl_is_public_suffix(NULL) || ns_psl_registrable_domain(NULL)) {
        fprintf(stderr, "FAIL NULL\n");
        failures++;
    }
}

#ifdef NS_PSL_TEST_LIBPSL

static unsigned long long rng_state = 0x9e3779b97f4a7c15ull;

static unsigned
rng(unsigned n)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (unsigned)(rng_state % n);
}

typedef struct {
    char **names;
    size_t len, cap;
    FILE *out;
} entries;

static void
collect(const char *name, int flags, void *data)
{
    entries *e = data;
    if (e->len == e->cap) {
        e->cap = e->cap ? e->cap * 2 : 1024;
        e->names = realloc(e->names, e->cap * sizeof(char *));
        if (!e->names) abort();
    }
    size_t n = strlen(name) + 1;
    e->names[e->len] = malloc(n);
    if (!e->names[e->len]) abort();
    memcpy(e->names[e->len++], name, n);
    if (flags & NS_PSL_EXCEPTION) fprintf(e->out, "!%s\n", name);
    if (flags & NS_PSL_WILDCARD) fprintf(e->out, "*.%s\n", name);
    if (!flags) fprintf(e->out, "%s\n", name);
}

static size_t compared, skipped;

/* libpsl before 0.23 took a name's trailing dot for an empty last label,
 * so that "com." was no public suffix and the registrable domain of
 * "example.com." was "com." (curl's CVE-2026-8924); the table answers as
 * 0.23 does, and such names are not compared with an older libpsl. */
static int libpsl_old_trailing_dot;

/* Asks the table and a libpsl context the same; complains if they differ. */
static void
compare(const psl_ctx_t *psl, const char *name, const char *what)
{
    size_t n = strlen(name);
    if (libpsl_old_trailing_dot && n && name[n - 1] == '.') {
        skipped++;
        return;
    }
    compared++;
    int a = ns_psl_is_public_suffix(name) ? 1 : 0;
    int b = psl_is_public_suffix(psl, name) ? 1 : 0;
    if (a != b) {
        fprintf(stderr, "FAIL %s: is_public_suffix(\"%s\") %d, libpsl %d\n",
                what, name, a, b);
        failures++;
    }
    const char *ra = ns_psl_registrable_domain(name);
    const char *rb = psl_registrable_domain(psl, name);
    if (ra != rb) {
        fprintf(stderr, "FAIL %s: registrable(\"%s\") %s, libpsl %s\n",
                what, name, show(ra), show(rb));
        failures++;
    }
}

static void
compare_around(const psl_ctx_t *psl, const char *name, const char *what)
{
    char buf[512];
    compare(psl, name, what);
    static const char *const before[] = { "x.", "www.", "a.b.", "city.", "." };
    for (size_t i = 0; i < sizeof(before) / sizeof(before[0]); i++) {
        snprintf(buf, sizeof(buf), "%s%s", before[i], name);
        compare(psl, buf, what);
    }
    snprintf(buf, sizeof(buf), "%s.", name);
    compare(psl, buf, what);
    snprintf(buf, sizeof(buf), "x.%s.", name);
    compare(psl, buf, what);
    snprintf(buf, sizeof(buf), "a.b.c.d.e.f.g.h.i.%s", name);
    compare(psl, buf, what);
    size_t n = strlen(name);
    for (size_t i = 0; i < n && i < sizeof(buf) - 1; i++)
        buf[i] = (name[i] >= 'a' && name[i] <= 'z') ? (char)(name[i] - 32) : name[i];
    buf[n < sizeof(buf) - 1 ? n : sizeof(buf) - 1] = '\0';
    compare(psl, buf, what);
    const char *dot = strchr(name, '.');
    if (dot) compare(psl, dot + 1, what);
}

static void
check_libpsl(const char *rules_file)
{
    entries e = { 0 };
    e.out = fopen(rules_file, "wb");
    if (!e.out) {
        perror(rules_file);
        failures++;
        return;
    }
    libpsl_old_trailing_dot = psl_check_version_number(0) < 0x001700;
    size_t count = ns_psl_foreach(collect, &e);
    fclose(e.out);
    psl_ctx_t *psl = psl_load_file(rules_file);
    if (!psl) {
        fprintf(stderr, "FAIL libpsl cannot load %s\n", rules_file);
        failures++;
        return;
    }
    printf("table: %zu entries, list %s; libpsl %s loaded %d suffixes, "
           "%d exceptions\n", count, ns_psl_version(), psl_get_version(),
           psl_suffix_count(psl), psl_suffix_exception_count(psl));

    for (size_t i = 0; i < e.len; i++)
        compare_around(psl, e.names[i], "rule");

    static const char *const edges[] = {
        "", ".", "..", "...", "com", ".com", "com.", "com..", "..com",
        "a..com", "a..example.com", "example..com", ".example.com.",
        "1.2.3.4", "192.168.0.1", "10.0.0.1.", "127.0.0.1", "[::1]", "::1",
        "[2001:db8::1]", "localhost", "localhost.", "a.localhost",
        "EXAMPLE.COM", "Example.Co.Uk", "WWW.CK", "Www.Ck",
        "a.b.c.d.e.f.g.h.i.j.k.example.com",
        "a.b.c.d.e.f.g.h.i.j.k.l.m.n.o.p.q.r.s.t.u.v.w.x.y.z",
        "a.b.c.d.e.f.g.h.city.kawasaki.jp", "a.b.c.d.e.f.g.h.i.city.kawasaki.jp",
        "x.y.z.test.ck", "foo-.com", "-foo.com", "foo_bar.com", "*.com",
        "*.ck", "!www.ck", "xn--", "xn--.com", "\xff.com", "\xc3.com",
        "www.\xe4\xb8\xad\xe5\x9b\xbd.", "\xe4\xb8\xad\xe5\x9b\xbd.",
    };
    for (size_t i = 0; i < sizeof(edges) / sizeof(edges[0]); i++)
        compare(psl, edges[i], "edge");

    /* Names made of the rules' labels, at random. */
    char buf[600];
    for (int round = 0; round < 200000; round++) {
        buf[0] = '\0';
        unsigned parts = 1 + rng(4);
        for (unsigned j = 0; j < parts; j++) {
            const char *label = e.names[rng((unsigned)e.len)];
            if (rng(3) == 0) {
                const char *dot = strchr(label, '.');
                if (dot && rng(2)) label = dot + 1;
            }
            if (strlen(buf) + strlen(label) + 2 >= sizeof(buf)) break;
            if (j) strcat(buf, ".");
            strcat(buf, label);
        }
        compare(psl, buf, "random");
    }

    /* The known answers of libpsl's own built-in list. */
    const psl_ctx_t *builtin = psl_builtin();
    if (builtin) {
        for (size_t i = 0; i < sizeof(known) / sizeof(known[0]); i++)
            compare(builtin, known[i][0], "builtin");
        size_t differ = 0;
        for (size_t i = 0; i < e.len; i++)
            if (ns_psl_is_public_suffix(e.names[i]) !=
                (psl_is_public_suffix(builtin, e.names[i]) != 0))
                differ++;
        printf("libpsl's built-in list (%s) answers %zu of the %zu rules "
               "differently (lists of other dates)\n",
               psl_builtin_file_time() ? "dated" : "undated", differ, e.len);
    }
    printf("%zu names compared with libpsl", compared);
    if (skipped)
        printf(", %zu with a trailing dot not (libpsl before 0.23)", skipped);
    printf("\n");

    psl_free(psl);
    for (size_t i = 0; i < e.len; i++) free(e.names[i]);
    free(e.names);
}

#endif

int
main(int argc, char **argv)
{
    check_known();
#ifdef NS_PSL_TEST_LIBPSL
    if (argc > 1) check_libpsl(argv[1]);
#else
    (void)argc;
    (void)argv;
#endif
    if (failures) {
        fprintf(stderr, "%d failures\n", failures);
        return 1;
    }
    printf("ok\n");
    return 0;
}
