/* Weststar SDL Browser — nsfc test.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Checks patterns, character sets, the family rules and the order of
 * sorted lists against a handful of made-up fonts, so that it runs the
 * same on every machine. NSFC_TEST_FONT may name a font file to scan.
 */

#include "nsfc-private.h"

#include "plat/ns_env.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failed;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            g_failed++;                                                      \
        }                                                                    \
    } while (0)

static const char *
family_of(const FcPattern *p)
{
    FcChar8 *s = NULL;
    return FcPatternGetString(p, FC_FAMILY, 0, &s) == FcResultMatch
               ? (const char *)s : "";
}

static const char *
style_of(const FcPattern *p)
{
    FcChar8 *s = NULL;
    return FcPatternGetString(p, FC_STYLE, 0, &s) == FcResultMatch
               ? (const char *)s : "";
}

static bool
is_font(const FcPattern *p, const char *family, const char *style)
{
    return p && strcmp(family_of(p), family) == 0 &&
           (!style || strcmp(style_of(p), style) == 0);
}

static void
test_patterns(void)
{
    int i = 0;
    double d = 0;
    FcBool b = FcFalse;
    FcChar8 *s = NULL;
    FcMatrix *m = NULL;

    FcPattern *p = FcPatternBuild(NULL,
                                  FC_WEIGHT, FcTypeDouble, 80.5,
                                  FC_SLANT, FcTypeInteger, FC_SLANT_ITALIC,
                                  FC_COLOR, FcTypeBool, FcTrue,
                                  NULL);
    CHECK(p != NULL);
    CHECK(FcPatternAddString(p, FC_FAMILY, (const FcChar8 *)"One"));
    CHECK(FcPatternAddString(p, FC_FAMILY, (const FcChar8 *)"Two"));

    /* A number reads as either kind; other types do not convert. */
    CHECK(FcPatternGetInteger(p, FC_WEIGHT, 0, &i) == FcResultMatch && i == 80);
    CHECK(FcPatternGetDouble(p, FC_SLANT, 0, &d) == FcResultMatch && d == 100);
    CHECK(FcPatternGetBool(p, FC_COLOR, 0, &b) == FcResultMatch && b == FcTrue);
    CHECK(FcPatternGetInteger(p, FC_FAMILY, 0, &i) == FcResultTypeMismatch);
    CHECK(FcPatternGetString(p, FC_FAMILY, 1, &s) == FcResultMatch &&
          strcmp((const char *)s, "Two") == 0);
    CHECK(FcPatternGetString(p, FC_FAMILY, 2, &s) == FcResultNoId);
    CHECK(FcPatternGetString(p, FC_STYLE, 0, &s) == FcResultNoMatch);

    FcPattern *copy = FcPatternDuplicate(p);
    CHECK(FcPatternEqual(p, copy));
    CHECK(FcPatternHash(p) == FcPatternHash(copy));
    CHECK(FcPatternRemove(copy, FC_FAMILY, 0));
    CHECK(!FcPatternEqual(p, copy));
    CHECK(strcmp(family_of(copy), "Two") == 0);
    CHECK(FcPatternRemove(copy, FC_FAMILY, 0));
    CHECK(FcPatternGetString(copy, FC_FAMILY, 0, &s) == FcResultNoMatch);
    CHECK(FcPatternDel(copy, FC_COLOR));
    CHECK(!FcPatternDel(copy, FC_COLOR));

    /* An integer and the same number as a double are one value. */
    FcPattern *a = FcPatternBuild(NULL, FC_SIZE, FcTypeInteger, 12, NULL);
    FcPattern *c = FcPatternBuild(NULL, FC_SIZE, FcTypeDouble, 12.0, NULL);
    CHECK(FcPatternEqual(a, c) && FcPatternHash(a) == FcPatternHash(c));

    FcMatrix shear = { 1, 0.2, 0, 1 }, twice;
    FcMatrixMultiply(&twice, &shear, &shear);
    CHECK(fabs(twice.xy - 0.4) < 1e-9 && twice.xx == 1 && twice.yy == 1);
    CHECK(FcPatternAddMatrix(a, FC_MATRIX, &shear));
    CHECK(FcPatternGetMatrix(a, FC_MATRIX, 0, &m) == FcResultMatch && m->xy == 0.2);

    /* Through the cache's format and back. */
    NsFcBuf buf = { 0 };
    FcCharSet *chars = FcCharSetCreate();
    FcCharSetAddChar(chars, 'a');
    FcCharSetAddChar(chars, 0x1f600);
    FcPatternAddCharSet(p, FC_CHARSET, chars);
    FcCharSetDestroy(chars);
    nsfc_pattern_write(&buf, p);
    CHECK(!buf.failed);
    const Uint8 *cursor = buf.data;
    FcPattern *back = nsfc_pattern_read(&cursor, buf.data + buf.len);
    CHECK(back && FcPatternEqual(p, back) && cursor == buf.data + buf.len);
    FcPatternDestroy(back);
    /* A cut-off file is refused, wherever it ends. */
    for (size_t len = 0; len < buf.len; len++) {
        cursor = buf.data;
        back = nsfc_pattern_read(&cursor, buf.data + len);
        CHECK(back == NULL);
        FcPatternDestroy(back);
    }
    SDL_free(buf.data);

    FcPatternDestroy(a);
    FcPatternDestroy(c);
    FcPatternDestroy(copy);
    FcPatternDestroy(p);
}

static void
test_charsets(void)
{
    FcBool changed = FcFalse;
    FcCharSet *a = FcCharSetCreate(), *b = FcCharSetCreate();
    CHECK(FcCharSetAddChar(a, 'a') && FcCharSetAddChar(a, 0x4e00) &&
          FcCharSetAddChar(a, 0x10ffff));
    CHECK(!FcCharSetAddChar(a, 0x110000));
    CHECK(FcCharSetHasChar(a, 'a') && FcCharSetHasChar(a, 0x4e00) &&
          FcCharSetHasChar(a, 0x10ffff));
    CHECK(!FcCharSetHasChar(a, 'b') && !FcCharSetHasChar(a, 0x4e01));
    CHECK(FcCharSetCount(a) == 3);

    FcCharSetAddChar(b, 'a');
    CHECK(FcCharSetMerge(a, b, &changed) && !changed);
    FcCharSetAddChar(b, 0x3042);
    CHECK(FcCharSetMerge(a, b, &changed) && changed && FcCharSetCount(a) == 4);

    /* A copy changes on its own; a constant set does not change at all. */
    FcCharSet *copy = FcCharSetCopy(a);
    CHECK(copy != a && FcCharSetDelChar(copy, 'a'));
    CHECK(!FcCharSetHasChar(copy, 'a') && FcCharSetHasChar(a, 'a'));
    nsfc_charset_set_constant(a);
    FcCharSet *shared = FcCharSetCopy(a);
    CHECK(shared == a && !FcCharSetAddChar(a, 'z') && !FcCharSetHasChar(a, 'z'));

    FcCharSetDestroy(shared);
    FcCharSetDestroy(copy);
    FcCharSetDestroy(a);
    FcCharSetDestroy(b);
}

static void
test_weights(void)
{
    CHECK(FcWeightFromOpenTypeDouble(100) == FC_WEIGHT_THIN);
    CHECK(FcWeightFromOpenTypeDouble(400) == FC_WEIGHT_REGULAR);
    CHECK(FcWeightFromOpenTypeDouble(500) == FC_WEIGHT_MEDIUM);
    CHECK(FcWeightFromOpenTypeDouble(550) == 140);
    CHECK(FcWeightFromOpenTypeDouble(700) == FC_WEIGHT_BOLD);
    CHECK(FcWeightFromOpenTypeDouble(900) == FC_WEIGHT_BLACK);
    CHECK(FcWeightToOpenTypeDouble(FC_WEIGHT_THIN) == 100);
    CHECK(FcWeightToOpenTypeDouble(FC_WEIGHT_REGULAR) == 400);
    CHECK(FcWeightToOpenTypeDouble(FC_WEIGHT_BOLD) == 700);
    for (double ot = 100; ot <= 1000; ot += 25)
        CHECK(fabs(FcWeightToOpenTypeDouble(FcWeightFromOpenTypeDouble(ot)) - ot) < 1e-6);
}

/* A made-up font with the characters from..to. */
static void
add_font(FcFontSet *set, const char *family, const char *style, int weight,
         int slant, FcChar32 from, FcChar32 to, bool color, bool mono)
{
    FcPattern *p = FcPatternCreate();
    FcCharSet *chars = FcCharSetCreate();
    char file[128];
    SDL_snprintf(file, sizeof file, "/fonts/%s-%s.ttf", family, style);
    FcPatternAddString(p, FC_FAMILY, (const FcChar8 *)family);
    FcPatternAddString(p, FC_STYLE, (const FcChar8 *)style);
    FcPatternAddString(p, FC_FILE, (const FcChar8 *)file);
    FcPatternAddInteger(p, FC_INDEX, 0);
    FcPatternAddInteger(p, FC_WEIGHT, weight);
    FcPatternAddInteger(p, FC_SLANT, slant);
    FcPatternAddInteger(p, FC_WIDTH, FC_WIDTH_NORMAL);
    FcPatternAddBool(p, FC_COLOR, color ? FcTrue : FcFalse);
    if (mono) FcPatternAddInteger(p, FC_SPACING, FC_MONO);
    for (FcChar32 c = from; c <= to; c++)
        FcCharSetAddChar(chars, c);
    nsfc_charset_set_constant(chars);
    FcPatternAddCharSet(p, FC_CHARSET, chars);
    FcCharSetDestroy(chars);
    FcFontSetAdd(set, p);
}

static FcFontSet *
make_fonts(void)
{
    FcFontSet *set = FcFontSetCreate();
    /* Deliberately not in the order any request wants them. */
    add_font(set, "Kana Mono", "Regular", 80, 0, 0x20, 0x30ff, false, true);
    add_font(set, "Emoji Color", "Regular", 80, 0, 0x1f600, 0x1f64f, true, false);
    add_font(set, "Liberation Sans", "Regular", 80, 0, 0x20, 0x24f, false, false);
    add_font(set, "DejaVu Serif", "Book", 80, 0, 0x20, 0x4ff, false, false);
    add_font(set, "DejaVu Sans", "Bold", 200, 0, 0x20, 0x3ff, false, false);
    add_font(set, "DejaVu Sans", "Book", 80, 0, 0x20, 0x4ff, false, false);
    add_font(set, "DejaVu Sans", "Oblique", 80, 110, 0x20, 0x4ff, false, false);
    add_font(set, "DejaVu Sans Mono", "Book", 80, 0, 0x20, 0x4ff, false, true);
    add_font(set, "Kana Text", "Regular", 80, 0, 0x20, 0x30ff, false, false);
    add_font(set, "Symbols", "Regular", 80, 0, 0x2190, 0x21ff, false, false);

    /* A variable font: one face, any weight from thin to black. */
    add_font(set, "Flex", "Regular", 80, 0, 0x20, 0x7f, false, false);
    FcPattern *flex = set->fonts[set->nfont - 1];
    FcPatternAddDouble(flex, NSFC_WEIGHT_MIN, FC_WEIGHT_THIN);
    FcPatternAddDouble(flex, NSFC_WEIGHT_MAX, FC_WEIGHT_BLACK);
    return set;
}

/* A request as nstext's font map builds it. */
static FcPattern *
make_request(const char *families, int weight, int slant, const char *lang,
             bool color)
{
    FcPattern *p = FcPatternBuild(NULL,
                                  FC_WEIGHT, FcTypeDouble, (double)weight,
                                  FC_SLANT, FcTypeInteger, slant,
                                  FC_WIDTH, FcTypeDouble, 100.0,
                                  FC_VARIABLE, FcTypeBool, FcDontCare,
                                  FC_DPI, FcTypeDouble, 96.0,
                                  FC_SIZE, FcTypeDouble, 12.0,
                                  FC_PIXEL_SIZE, FcTypeDouble, 16.0,
                                  FC_COLOR, FcTypeBool, color ? FcTrue : FcFalse,
                                  NULL);
    char *list = SDL_strdup(families), *save = NULL;
    for (char *f = SDL_strtok_r(list, ",", &save); f; f = SDL_strtok_r(NULL, ",", &save))
        FcPatternAddString(p, FC_FAMILY, (const FcChar8 *)f);
    SDL_free(list);
    if (lang)
        FcPatternAddString(p, FC_LANG, (const FcChar8 *)lang);
    FcConfigSubstitute(NULL, p, FcMatchPattern);
    FcDefaultSubstitute(p);
    return p;
}

static FcFontSet *
sorted(FcFontSet *fonts, const char *families, int weight, int slant,
       const char *lang, bool color, FcBool trim)
{
    FcResult result;
    FcPattern *p = make_request(families, weight, slant, lang, color);
    FcFontSet *out = FcFontSetSort(NULL, &fonts, 1, p, trim, NULL, &result);
    CHECK(result == FcResultMatch);
    FcPatternDestroy(p);
    return out;
}

static int
position_of(const FcFontSet *set, const char *family)
{
    for (int i = 0; i < set->nfont; i++)
        if (strcmp(family_of(set->fonts[i]), family) == 0)
            return i;
    return -1;
}

static void
test_substitution(void)
{
    FcChar8 *s = NULL;
    int hint = -1;
    double pixels = 0;

    FcPattern *p = make_request(" Arial , serif", 80, 0, NULL, false);
    /* The named family (trimmed), its metric twins, then what serif stands
     * for, then the last resort. */
    CHECK(strcmp(family_of(p), "Arial") == 0);
    const NsFcElt *e = nsfc_pattern_find(p, FC_FAMILY);
    int liberation = -1, dejavu_serif = -1, dejavu_sans = -1;
    for (int i = 0; e && i < e->n; i++) {
        /* The first mention is the one that counts. */
        const char *name = e->values[i].u.s;
        if (liberation < 0 && !strcmp(name, "Liberation Sans")) liberation = i;
        if (dejavu_serif < 0 && !strcmp(name, "DejaVu Serif")) dejavu_serif = i;
        if (dejavu_sans < 0 && !strcmp(name, "DejaVu Sans")) dejavu_sans = i;
    }
    CHECK(liberation > 0 && dejavu_serif > liberation && dejavu_sans > dejavu_serif);
    CHECK(e && e->values[liberation].binding == NSFC_STRONG &&
          e->values[dejavu_serif].binding == NSFC_WEAK);
    CHECK(FcPatternGetInteger(p, FC_HINT_STYLE, 0, &hint) == FcResultMatch &&
          hint == FC_HINT_SLIGHT);
    FcPatternDestroy(p);

    /* A request without a size gets one. */
    p = FcPatternCreate();
    FcConfigSubstitute(NULL, p, FcMatchPattern);
    FcDefaultSubstitute(p);
    CHECK(FcPatternGetDouble(p, FC_PIXEL_SIZE, 0, &pixels) == FcResultMatch &&
          fabs(pixels - 12.5) < 1e-9);
    CHECK(FcPatternGetString(p, FC_FAMILY, 0, &s) == FcResultMatch);
    FcPatternDestroy(p);
}

static void
test_sorting(void)
{
    FcFontSet *fonts = make_fonts();
    FcFontSet *list;

    /* A generic family: its first installed font, in the asked weight. */
    list = sorted(fonts, "sans-serif", 80, 0, "en", false, FcFalse);
    CHECK(list->nfont == fonts->nfont);
    CHECK(is_font(list->fonts[0], "DejaVu Sans", "Book"));
    FcFontSetDestroy(list);
    list = sorted(fonts, "sans-serif", 200, 0, "en", false, FcFalse);
    CHECK(is_font(list->fonts[0], "DejaVu Sans", "Bold"));
    FcFontSetDestroy(list);
    list = sorted(fonts, "sans-serif", 80, FC_SLANT_ITALIC, "en", false, FcFalse);
    CHECK(is_font(list->fonts[0], "DejaVu Sans", "Oblique"));
    FcFontSetDestroy(list);
    list = sorted(fonts, "monospace", 80, 0, "en", false, FcFalse);
    CHECK(is_font(list->fonts[0], "DejaVu Sans Mono", NULL));
    FcFontSetDestroy(list);

    /* A family that is not installed: its metric twin; failing that, a
     * font of its kind. Case and blanks do not matter. */
    list = sorted(fonts, "arial", 80, 0, "en", false, FcFalse);
    CHECK(is_font(list->fonts[0], "Liberation Sans", NULL));
    FcFontSetDestroy(list);
    list = sorted(fonts, "Nowhere Serif", 80, 0, "en", false, FcFalse);
    CHECK(is_font(list->fonts[0], "DejaVu Serif", NULL));
    FcFontSetDestroy(list);
    list = sorted(fonts, "dejavusans mono,serif", 80, 0, "en", false, FcFalse);
    CHECK(is_font(list->fonts[0], "DejaVu Sans Mono", NULL));
    CHECK(is_font(list->fonts[1], "DejaVu Serif", NULL));
    FcFontSetDestroy(list);

    /* The page's family first whatever the language; among the fonts it
     * did not name, those for the language first, proportional before
     * fixed-width, and symbol fonts last. */
    list = sorted(fonts, "sans-serif", 80, 0, "ja", false, FcFalse);
    CHECK(is_font(list->fonts[0], "DejaVu Sans", "Book"));
    int text = position_of(list, "Kana Text"), mono = position_of(list, "Kana Mono");
    CHECK(text > 0 && text < mono && mono < position_of(list, "DejaVu Serif"));
    FcFontSetDestroy(list);
    list = sorted(fonts, "sans-serif", 80, 0, "en", false, FcFalse);
    CHECK(position_of(list, "Symbols") > position_of(list, "Kana Mono"));
    CHECK(position_of(list, "Emoji Color") == list->nfont - 1);
    FcFontSetDestroy(list);

    /* Colour asked for: the colour font first. */
    list = sorted(fonts, "sans-serif", 80, 0, "en", true, FcFalse);
    CHECK(is_font(list->fonts[0], "Emoji Color", NULL));
    FcFontSetDestroy(list);

    /* Trimmed: the best font, then only fonts that add characters. */
    list = sorted(fonts, "sans-serif", 80, 0, "en", false, FcTrue);
    CHECK(is_font(list->fonts[0], "DejaVu Sans", "Book"));
    CHECK(position_of(list, "Liberation Sans") < 0);
    CHECK(position_of(list, "DejaVu Serif") < 0);
    CHECK(position_of(list, "Kana Text") > 0 && position_of(list, "Kana Mono") < 0);
    CHECK(position_of(list, "Emoji Color") > 0);
    FcFontSetDestroy(list);
    /* The best font stays even when it has the fewest characters. */
    list = sorted(fonts, "sans-serif", 200, 0, "en", false, FcTrue);
    CHECK(is_font(list->fonts[0], "DejaVu Sans", "Bold"));
    CHECK(is_font(list->fonts[1], "DejaVu Sans", "Book"));
    FcFontSetDestroy(list);

    FcFontSetDestroy(fonts);
}

static void
test_matching(void)
{
    FcFontSet *fonts = make_fonts();
    FcResult result;
    FcBool embolden = FcFalse;
    FcMatrix *m = NULL;
    FcChar8 *s = NULL;
    double d = 0;
    int slant = 0;

    /* The match carries the request's size; an existing face needs no
     * making up. */
    FcPattern *p = make_request("DejaVu Sans", 200, 0, "en", false);
    FcPattern *match = FcFontSetMatch(NULL, &fonts, 1, p, &result);
    CHECK(result == FcResultMatch && is_font(match, "DejaVu Sans", "Bold"));
    CHECK(FcPatternGetDouble(match, FC_PIXEL_SIZE, 0, &d) == FcResultMatch && d == 16);
    CHECK(FcPatternGetBool(match, FC_EMBOLDEN, 0, &embolden) == FcResultNoMatch);
    CHECK(FcPatternGetMatrix(match, FC_MATRIX, 0, &m) == FcResultNoMatch);
    CHECK(FcPatternGetString(match, FC_LANG, 0, &s) == FcResultNoMatch);
    FcPatternDestroy(match);
    FcPatternDestroy(p);

    /* No bold, no italic in the family: both are made. */
    p = make_request("DejaVu Serif", 200, FC_SLANT_ITALIC, "en", false);
    match = FcFontSetMatch(NULL, &fonts, 1, p, &result);
    CHECK(is_font(match, "DejaVu Serif", NULL));
    CHECK(FcPatternGetBool(match, FC_EMBOLDEN, 0, &embolden) == FcResultMatch && embolden);
    CHECK(FcPatternGetMatrix(match, FC_MATRIX, 0, &m) == FcResultMatch &&
          m->xx == 1 && m->xy == 0.2 && m->yx == 0 && m->yy == 1);
    CHECK(FcPatternGetInteger(match, FC_SLANT, 0, &slant) == FcResultMatch &&
          slant == FC_SLANT_OBLIQUE);
    FcPatternDestroy(match);
    FcPatternDestroy(p);

    /* A variable font is set to the weight instead, before any variation
     * settings of the request, which then win. */
    p = make_request("Flex", 200, 0, "en", false);
    FcPatternAddString(p, FC_FONT_VARIATIONS, (const FcChar8 *)"opsz=14");
    match = FcFontSetMatch(NULL, &fonts, 1, p, &result);
    CHECK(is_font(match, "Flex", NULL));
    CHECK(FcPatternGetString(match, FC_FONT_VARIATIONS, 0, &s) == FcResultMatch &&
          strcmp((const char *)s, "wght=700,opsz=14") == 0);
    CHECK(FcPatternGetBool(match, FC_EMBOLDEN, 0, &embolden) == FcResultNoMatch);
    CHECK(FcPatternGetDouble(match, FC_WEIGHT, 0, &d) == FcResultMatch && d == 200);
    FcPatternDestroy(match);
    FcPatternDestroy(p);

    /* No fonts at all: no match, and nothing breaks. */
    FcFontSet *none = FcFontSetCreate();
    p = make_request("sans-serif", 80, 0, "en", false);
    match = FcFontSetMatch(NULL, &none, 1, p, &result);
    CHECK(match == NULL && result == FcResultNoMatch);
    FcFontSet *list = FcFontSetSort(NULL, &none, 1, p, FcTrue, NULL, &result);
    CHECK(list && list->nfont == 0 && result == FcResultNoMatch);
    FcFontSetDestroy(list);
    FcFontSetDestroy(none);
    FcPatternDestroy(p);

    FcFontSetDestroy(fonts);
}

static void
test_listing(void)
{
    FcFontSet *fonts = make_fonts();

    /* Every family once. */
    FcObjectSet *os = FcObjectSetBuild(FC_FAMILY, NULL);
    FcPattern *any = FcPatternCreate();
    FcFontSet *list = FcFontSetList(NULL, &fonts, 1, any, os);
    CHECK(list->nfont == 9);
    FcFontSetDestroy(list);
    FcObjectSetDestroy(os);

    /* The faces of one family. */
    os = FcObjectSetBuild(FC_FAMILY, FC_STYLE, FC_WEIGHT, NULL);
    FcPatternAddString(any, FC_FAMILY, (const FcChar8 *)"dejavu sans");
    list = FcFontSetList(NULL, &fonts, 1, any, os);
    CHECK(list->nfont == 3);
    for (int i = 0; i < list->nfont; i++)
        CHECK(is_font(list->fonts[i], "DejaVu Sans", NULL));
    FcFontSetDestroy(list);
    FcObjectSetDestroy(os);
    FcPatternDestroy(any);

    FcFontSetDestroy(fonts);
}

/* Marks the last font of the set as one compiled into the browser. */
static void
mark_bundled(FcFontSet *set)
{
    FcPatternAddBool(set->fonts[set->nfont - 1], NSFC_BUNDLED, FcTrue);
}

/* The bundled fonts come after the system's of their kind, and before the
 * system's of another kind. */
static void
test_bundled_order(void)
{
    FcFontSet *fonts = FcFontSetCreate();
    FcFontSet *list;
    add_font(fonts, "DejaVu Sans", "Book", 80, 0, 0x20, 0x52f, false, false);
    mark_bundled(fonts);
    add_font(fonts, "DejaVu Sans Mono", "Book", 80, 0, 0x20, 0x52f, false, true);
    mark_bundled(fonts);
    add_font(fonts, "DejaVu Serif", "Book", 80, 0, 0x20, 0x52f, false, false);
    mark_bundled(fonts);
    add_font(fonts, "Tahoma", "Regular", 80, 0, 0x20, 0x24f, false, false);
    add_font(fonts, "Greek Text", "Regular", 80, 0, 0x370, 0x3ff, false, false);

    /* DejaVu Sans is the first stand-in for sans-serif, but the system's
     * Tahoma, the seventh, comes first; after it every system font, and
     * then the bundled ones. */
    list = sorted(fonts, "sans-serif", 80, 0, "en", false, FcFalse);
    CHECK(is_font(list->fonts[0], "Tahoma", NULL));
    CHECK(is_font(list->fonts[1], "Greek Text", NULL));
    CHECK(is_font(list->fonts[2], "DejaVu Sans", NULL));
    FcFontSetDestroy(list);
    /* With no monospace font on the system, the bundled one, not Tahoma. */
    list = sorted(fonts, "monospace", 80, 0, "en", false, FcFalse);
    CHECK(is_font(list->fonts[0], "DejaVu Sans Mono", NULL));
    CHECK(is_font(list->fonts[1], "Tahoma", NULL));
    FcFontSetDestroy(list);
    list = sorted(fonts, "serif", 80, 0, "en", false, FcFalse);
    CHECK(is_font(list->fonts[0], "DejaVu Serif", NULL));
    FcFontSetDestroy(list);
    /* Trimmed: Greek from the system's Greek font, not the bundled sans,
     * which then adds only what no system font has. */
    list = sorted(fonts, "sans-serif", 80, 0, "en", false, FcTrue);
    CHECK(list->nfont == 3);
    CHECK(is_font(list->fonts[0], "Tahoma", NULL));
    CHECK(is_font(list->fonts[1], "Greek Text", NULL));
    CHECK(is_font(list->fonts[2], "DejaVu Sans", NULL));
    FcFontSetDestroy(list);

    /* A family the page names: the system's copy before the bundled one,
     * which still comes before a family named after it. */
    add_font(fonts, "DejaVu Sans", "Book", 80, 0, 0x20, 0x24f, false, false);
    list = sorted(fonts, "DejaVu Sans", 80, 0, "en", false, FcFalse);
    CHECK(is_font(list->fonts[0], "DejaVu Sans", NULL) &&
          FcPatternGetBool(list->fonts[0], NSFC_BUNDLED, 0, &(FcBool){ 0 }) ==
              FcResultNoMatch);
    FcFontSetDestroy(list);
    list = sorted(fonts, "DejaVu Sans Mono,Tahoma", 80, 0, "en", false, FcFalse);
    CHECK(is_font(list->fonts[0], "DejaVu Sans Mono", NULL));
    FcFontSetDestroy(list);
    FcFontSetDestroy(fonts);
}

/* The fonts compiled into this build, if any, found with the system's
 * folders turned off, as on a system without fonts. */
static void
test_bundled(void)
{
    FcResult result;
    FcChar8 *file = NULL;
    FcCharSet *chars = NULL;
    size_t len = 0;
    int n = 0;
    if (!nsfc_bundled_fonts[0].name) {
        printf("no bundled fonts in this build\n");
        return;
    }
    ns_setenv("NS_NO_SYSTEM_FONTS", "1", true);
    ns_unsetenv("NS_FONT_PATH");
    Uint64 start = SDL_GetPerformanceCounter();
    FcFontSet *system = FcConfigGetFonts(NULL, FcSetSystem);
    double ms = (double)(SDL_GetPerformanceCounter() - start) * 1000.0 /
                (double)SDL_GetPerformanceFrequency();
    CHECK(system && system->nfont > 0);
    for (int i = 0; system && i < system->nfont; i++) {
        FcBool bundled = FcFalse;
        CHECK(FcPatternGetBool(system->fonts[i], NSFC_BUNDLED, 0, &bundled) ==
                  FcResultMatch && bundled);
        CHECK(FcPatternGetString(system->fonts[i], FC_FILE, 0, &file) ==
                  FcResultMatch &&
              NsFcBundledFontData(file, &len) != NULL && len > 1000);
        n++;
    }
    CHECK(NsFcBundledFontData((const FcChar8 *)"nsfc-bundled:nothing.ttf", &len) == NULL);
    CHECK(NsFcBundledFontData((const FcChar8 *)"/fonts/DejaVuSans.ttf", &len) == NULL);

    static const struct { const char *request; int weight; const char *family; }
    expect[] = {
        { "sans-serif", 80, "DejaVu Sans" },
        { "Nowhere Sans", 200, "DejaVu Sans" },
        { "serif", 80, "DejaVu Serif" },
        { "monospace", 80, "DejaVu Sans Mono" },
        { "system-ui", 80, "DejaVu Sans" },
    };
    for (size_t i = 0; i < SDL_arraysize(expect); i++) {
        FcPattern *p = make_request(expect[i].request, expect[i].weight, 0, "en", false);
        FcPattern *match = FcFontMatch(NULL, p, &result);
        CHECK(result == FcResultMatch && is_font(match, expect[i].family, NULL));
        if (match && expect[i].weight == 200)
            CHECK(strcmp(style_of(match), "Bold") == 0);
        if (match && FcPatternGetCharSet(match, FC_CHARSET, 0, &chars) == FcResultMatch)
            CHECK(FcCharSetHasChar(chars, 'A') && FcCharSetHasChar(chars, 0x3b1) &&
                  FcCharSetHasChar(chars, 0x430) && FcCharSetHasChar(chars, 0x2500));
        FcPatternDestroy(match);
        FcPatternDestroy(p);
    }
    printf("bundled: %d face(s), found in %.1f ms\n", n, ms);
}

/* A real font file, when the environment names one. */
static void
test_scan(const char *path)
{
    int faces = 0, weight = -1;
    FcChar8 *s = NULL;
    FcCharSet *chars = NULL;
    FcPattern *p = FcFreeTypeQuery((const FcChar8 *)path, 0, NULL, &faces);
    CHECK(p != NULL && faces >= 1);
    if (!p) return;
    CHECK(FcPatternGetString(p, FC_FAMILY, 0, &s) == FcResultMatch && *s);
    printf("scanned %s: family \"%s\"", path, (const char *)s);
    CHECK(FcPatternGetString(p, FC_STYLE, 0, &s) == FcResultMatch);
    CHECK(FcPatternGetString(p, FC_FILE, 0, &s) == FcResultMatch &&
          strcmp((const char *)s, path) == 0);
    CHECK(FcPatternGetInteger(p, FC_WEIGHT, 0, &weight) == FcResultMatch &&
          weight >= 0 && weight <= FC_WEIGHT_EXTRABLACK);
    CHECK(FcPatternGetString(p, FC_FONT_WRAPPER, 0, &s) == FcResultMatch &&
          strcmp((const char *)s, "SFNT") == 0);
    CHECK(FcPatternGetCharSet(p, FC_CHARSET, 0, &chars) == FcResultMatch &&
          FcCharSetCount(chars) > 0);
    printf(", weight %d, %u characters, %d face(s)\n", weight,
           (unsigned)FcCharSetCount(chars), faces);

    FcFontSet *set = FcFontSetCreate();
    CHECK(FcFreeTypeQueryAll((const FcChar8 *)path, (unsigned int)-1, NULL,
                             &faces, set) >= 1);
    CHECK(set->nfont >= 1 && FcPatternEqual(set->fonts[0], p));
    FcFontSetDestroy(set);
    FcPatternDestroy(p);

    /* Not a font, and not there. */
    CHECK(FcFreeTypeQuery((const FcChar8 *)"/nonexistent/font.ttf", 0, NULL,
                          &faces) == NULL);
}

int
main(void)
{
    test_patterns();
    test_charsets();
    test_weights();
    test_substitution();
    test_sorting();
    test_matching();
    test_listing();
    test_bundled_order();
    test_bundled();
    const char *font = getenv("NSFC_TEST_FONT");
    if (font && *font)
        test_scan(font);
    if (g_failed) {
        fprintf(stderr, "%d check(s) failed\n", g_failed);
        return 1;
    }
    printf("nsfc: all checks passed\n");
    return 0;
}
