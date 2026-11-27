/* Weststar SDL Browser — nsfc: choosing fonts for a request.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * fontconfig reads its rules from XML files; nsfc's are the tables below:
 * what the generic families stand for, and which families can stand in
 * for one another. A sorted list has the families the page named first,
 * then the stand-ins for its generic family, then the fonts for the text's
 * language, each group by slant, weight and width. (fontconfig puts the
 * language before the stand-ins, which gives Latin text on a Japanese page
 * the Japanese font's Latin letters; a browser wants the page's family for
 * every character it has.)
 */

#include "nsfc-private.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---- family rules ---- */

#define MAX_NAMES 16

typedef struct Alias {
    const char *names[4];           /* what a request may say */
    const char *families[MAX_NAMES]; /* the fonts to try, best first */
} Alias;

static const Alias generic_aliases[] = {
    { { "sans-serif", "sans", "ui-sans-serif", "ui-rounded" },
      { "DejaVu Sans", "Arial", "Liberation Sans", "Helvetica", "Noto Sans",
        "Segoe UI", "Tahoma", "Verdana", "Nimbus Sans", "FreeSans",
        "Bitstream Vera Sans", "Roboto", "Lucida Grande" } },
    { { "serif", "ui-serif" },
      { "DejaVu Serif", "Times New Roman", "Liberation Serif", "Times",
        "Noto Serif", "Georgia", "Nimbus Roman", "FreeSerif",
        "Bitstream Vera Serif" } },
    { { "monospace", "mono", "ui-monospace" },
      { "DejaVu Sans Mono", "Courier New", "Liberation Mono", "Consolas",
        "Menlo", "Noto Sans Mono", "Lucida Console", "Nimbus Mono PS",
        "FreeMono", "Courier", "Bitstream Vera Sans Mono" } },
    { { "system-ui" },
      { "Segoe UI", "Cantarell", "Inter", "Noto Sans", "DejaVu Sans", "Arial",
        "Liberation Sans", "Helvetica", "Tahoma", "Roboto" } },
    { { "cursive" },
      { "Comic Sans MS", "Comic Neue", "Apple Chancery", "Z003",
        "URW Chancery L", "Brush Script MT" } },
    { { "fantasy" },
      { "Impact", "Papyrus", "Copperplate", "Luminari" } },
    { { "emoji" },
      { "Noto Color Emoji", "Segoe UI Emoji", "Apple Color Emoji",
        "Twemoji Mozilla", "EmojiOne Color", "Noto Emoji", "Segoe UI Symbol" } },
    { { "math" },
      { "Cambria Math", "STIX Two Math", "Latin Modern Math",
        "DejaVu Math TeX Gyre", "Noto Sans Math" } },
    { { "fangsong" },
      { "FangSong", "STFangsong", "AR PL UKai CN" } },
};

/* Families with the same metrics: a page asking for one is as well served
 * by another of its group. */
static const char *const metric_groups[][MAX_NAMES] = {
    { "Arial", "Helvetica", "Liberation Sans", "Arimo", "Nimbus Sans",
      "Nimbus Sans L", "Albany", "FreeSans" },
    { "Times New Roman", "Times", "Liberation Serif", "Tinos", "Nimbus Roman",
      "Nimbus Roman No9 L", "Thorndale", "FreeSerif" },
    { "Courier New", "Courier", "Liberation Mono", "Cousine", "Nimbus Mono PS",
      "Nimbus Mono L", "Cumberland", "FreeMono" },
    { "Calibri", "Carlito" },
    { "Cambria", "Caladea" },
    { "Georgia", "Gelasio" },
    { "Arial Narrow", "Liberation Sans Narrow" },
    { "Symbol", "Standard Symbols PS", "Symbol Neu" },
};

static const Alias *
find_generic(const char *family)
{
    for (size_t i = 0; i < SDL_arraysize(generic_aliases); i++) {
        const Alias *a = &generic_aliases[i];
        for (size_t j = 0; j < SDL_arraysize(a->names) && a->names[j]; j++)
            if (nsfc_family_cmp(a->names[j], family) == 0)
                return a;
    }
    return NULL;
}

static const char *const *
find_metric_group(const char *family)
{
    for (size_t i = 0; i < SDL_arraysize(metric_groups); i++)
        for (size_t j = 0; j < MAX_NAMES && metric_groups[i][j]; j++)
            if (nsfc_family_cmp(metric_groups[i][j], family) == 0)
                return metric_groups[i];
    return NULL;
}

/* The generic family a named family belongs to, for when it is not
 * installed: by the usual names, else sans-serif. */
static const Alias *
classify_family(const char *family)
{
    static const char *const mono[] = {
        "mono", "courier", "consol", "menlo", "monaco", "terminal", "fixed",
        "typewriter", "andale",
    };
    static const char *const serif[] = {
        "serif", "times", "georgia", "garamond", "roman", "palatino",
        "bookman", "cambria", "charter", "baskerville", "didot", "minion",
        "century", "caslon", "bodoni", "mincho", "song", "ming",
    };
    for (size_t i = 0; i < SDL_arraysize(mono); i++)
        if (SDL_strcasestr(family, mono[i]))
            return find_generic("monospace");
    if (!SDL_strcasestr(family, "sans")) {
        for (size_t i = 0; i < SDL_arraysize(serif); i++)
            if (SDL_strcasestr(family, serif[i]))
                return find_generic("serif");
    }
    return find_generic("sans-serif");
}

static void
add_family_value(FcPattern *p, const char *family, NsFcBinding binding)
{
    char *copy = SDL_strdup(family);
    if (!copy) return;
    nsfc_pattern_add(p, FC_FAMILY,
        (NsFcValue){ .type = FcTypeString, .binding = binding, .u.s = copy },
        true);
}

static void
add_alias_families(FcPattern *p, const Alias *alias)
{
    for (size_t i = 0; i < MAX_NAMES && alias->families[i]; i++)
        add_family_value(p, alias->families[i], NSFC_WEAK);
}

/* Rewrites the request's families: a generic name becomes the fonts it
 * stands for, a named family is followed by its metric-compatible
 * stand-ins, and the list ends with the fonts of the request's generic
 * family, so that a page naming only fonts that are not installed still
 * gets a font of the right kind. */
static void
substitute_families(FcPattern *p)
{
    const NsFcElt *e = nsfc_pattern_find(p, FC_FAMILY);
    int n = e ? e->n : 0;
    char **families = n ? SDL_calloc((size_t)n, sizeof *families) : NULL;
    const Alias *last_resort = find_generic("sans-serif");
    const Alias *seen_generic = NULL;
    const Alias *first_kind = NULL;

    if (n && !families) return;
    for (int i = 0; i < n; i++) {
        char *s = e->values[i].type == FcTypeString ? e->values[i].u.s : NULL;
        /* Without the blanks around a name in a comma-separated list. */
        while (s && *s == ' ') s++;
        families[i] = s ? SDL_strdup(s) : NULL;
        if (families[i]) {
            size_t len = strlen(families[i]);
            while (len && families[i][len - 1] == ' ')
                families[i][--len] = 0;
        }
    }
    FcPatternDel(p, FC_FAMILY);

    for (int i = 0; i < n; i++) {
        const char *family = families[i];
        if (!family || !*family) continue;
        const Alias *generic = find_generic(family);
        if (generic) {
            add_alias_families(p, generic);
            seen_generic = generic;
            continue;
        }
        add_family_value(p, family, NSFC_STRONG);
        const char *const *group = find_metric_group(family);
        for (size_t j = 0; group && j < MAX_NAMES && group[j]; j++)
            if (nsfc_family_cmp(group[j], family) != 0)
                add_family_value(p, group[j], NSFC_STRONG);
        if (!first_kind)
            first_kind = classify_family(family);
    }
    if (!seen_generic && first_kind)
        add_alias_families(p, first_kind);
    if (seen_generic != last_resort && (seen_generic || first_kind != last_resort))
        add_alias_families(p, last_resort);

    for (int i = 0; i < n; i++)
        SDL_free(families[i]);
    SDL_free(families);
}

FcBool
FcConfigSubstitute(FcConfig *config, FcPattern *p, FcMatchKind kind)
{
    (void)config;
    if (!p) return FcFalse;
    if (kind != FcMatchPattern) return FcTrue;
    substitute_families(p);
    /* Light hinting, the default of today's desktops. */
    if (!nsfc_pattern_find(p, FC_HINT_STYLE))
        FcPatternAddInteger(p, FC_HINT_STYLE, FC_HINT_SLIGHT);
    return FcTrue;
}

void
FcDefaultSubstitute(FcPattern *pattern)
{
    double size, dpi, d;
    if (!pattern) return;
    if (!nsfc_pattern_find(pattern, FC_WEIGHT))
        FcPatternAddInteger(pattern, FC_WEIGHT, FC_WEIGHT_NORMAL);
    if (!nsfc_pattern_find(pattern, FC_SLANT))
        FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ROMAN);
    if (!nsfc_pattern_find(pattern, FC_WIDTH))
        FcPatternAddInteger(pattern, FC_WIDTH, FC_WIDTH_NORMAL);
    if (!nsfc_pattern_find(pattern, FC_HINTING))
        FcPatternAddBool(pattern, FC_HINTING, FcTrue);
    if (!nsfc_pattern_find(pattern, FC_AUTOHINT))
        FcPatternAddBool(pattern, FC_AUTOHINT, FcFalse);
    if (!nsfc_pattern_find(pattern, FC_ANTIALIAS))
        FcPatternAddBool(pattern, FC_ANTIALIAS, FcTrue);
    if (!nsfc_pattern_find(pattern, FC_EMBEDDED_BITMAP))
        FcPatternAddBool(pattern, FC_EMBEDDED_BITMAP, FcTrue);
    if (!nsfc_pattern_find(pattern, FC_VERTICAL_LAYOUT))
        FcPatternAddBool(pattern, FC_VERTICAL_LAYOUT, FcFalse);
    if (!nsfc_pattern_find(pattern, FC_HINT_STYLE))
        FcPatternAddInteger(pattern, FC_HINT_STYLE, FC_HINT_FULL);

    if (FcPatternGetDouble(pattern, FC_DPI, 0, &dpi) != FcResultMatch) {
        dpi = 75.0;
        FcPatternAddDouble(pattern, FC_DPI, dpi);
    }
    if (FcPatternGetDouble(pattern, FC_PIXEL_SIZE, 0, &d) != FcResultMatch) {
        if (FcPatternGetDouble(pattern, FC_SIZE, 0, &size) != FcResultMatch) {
            size = 12.0;
            FcPatternAddDouble(pattern, FC_SIZE, size);
        }
        FcPatternAddDouble(pattern, FC_PIXEL_SIZE, size * dpi / 72.0);
    } else if (FcPatternGetDouble(pattern, FC_SIZE, 0, &size) != FcResultMatch) {
        FcPatternAddDouble(pattern, FC_SIZE, d * 72.0 / dpi);
    }
}

/* ---- scoring ---- */

enum {
    SCORE_COLOR,
    SCORE_FAMILY_STRONG,
    SCORE_FAMILY_WEAK,
    SCORE_LANG,
    SCORE_SPACING,
    SCORE_SLANT,
    SCORE_WEIGHT,
    SCORE_WIDTH,
    SCORE_ORDER,
    N_SCORES
};

typedef struct Scored {
    double     score[N_SCORES];
    int        index;
    FcPattern *font;
} Scored;

/* What a request asks for, read out of its pattern once. */
typedef struct Request {
    const NsFcElt *families;
    FcChar32       exemplar;
    bool           has_color, has_slant, has_weight, has_width, has_spacing,
                   has_order;
    FcBool         color;
    double         slant, weight, width, spacing, order;
} Request;

/* A letter every font for the language has: enough to put a Japanese font
 * before a Chinese one for Japanese text, and a text font before a symbol
 * font everywhere. */
static FcChar32
lang_exemplar(const char *lang)
{
    static const struct { const char *prefix; FcChar32 c; } table[] = {
        { "ja", 0x3042 }, { "ko", 0xac00 }, { "zh", 0x4e00 },
        { "th", 0x0e01 }, { "ar", 0x0627 }, { "fa", 0x0627 },
        { "ur", 0x0627 }, { "he", 0x05d0 }, { "yi", 0x05d0 },
        { "hi", 0x0915 }, { "mr", 0x0915 }, { "ne", 0x0915 },
        { "bn", 0x0995 }, { "ta", 0x0b95 }, { "te", 0x0c15 },
        { "kn", 0x0c95 }, { "ml", 0x0d15 }, { "gu", 0x0a95 },
        { "pa", 0x0a15 }, { "si", 0x0d9a }, { "km", 0x1780 },
        { "lo", 0x0e81 }, { "my", 0x1000 }, { "ka", 0x10d0 },
        { "hy", 0x0561 }, { "am", 0x1200 }, { "el", 0x03b1 },
        { "ru", 0x0430 }, { "uk", 0x0430 }, { "bg", 0x0430 },
        { "sr", 0x0430 }, { "be", 0x0430 }, { "mk", 0x0430 },
        { "kk", 0x0430 }, { "mn", 0x0430 },
    };
    if (!lang) return 0;
    for (size_t i = 0; i < SDL_arraysize(table); i++) {
        if (SDL_strncasecmp(lang, table[i].prefix, 2) == 0 &&
            (lang[2] == 0 || lang[2] == '-' || lang[2] == '_'))
            return table[i].c;
    }
    return 'a';
}

static void
request_read(Request *r, const FcPattern *p)
{
    FcChar8 *lang;
    memset(r, 0, sizeof *r);
    r->families = nsfc_pattern_find(p, FC_FAMILY);
    if (FcPatternGetString(p, FC_LANG, 0, &lang) == FcResultMatch)
        r->exemplar = lang_exemplar((const char *)lang);
    r->has_color = FcPatternGetBool(p, FC_COLOR, 0, &r->color) == FcResultMatch &&
                   r->color != FcDontCare;
    r->has_slant = FcPatternGetDouble(p, FC_SLANT, 0, &r->slant) == FcResultMatch;
    r->has_weight = FcPatternGetDouble(p, FC_WEIGHT, 0, &r->weight) == FcResultMatch;
    r->has_width = FcPatternGetDouble(p, FC_WIDTH, 0, &r->width) == FcResultMatch;
    r->has_spacing = FcPatternGetDouble(p, FC_SPACING, 0, &r->spacing) == FcResultMatch;
    r->has_order = FcPatternGetDouble(p, FC_ORDER, 0, &r->order) == FcResultMatch;
}

#define NO_FAMILY 1e9

static void
score_font(Scored *s, const Request *r, FcPattern *font)
{
    double d, lo, hi;
    FcBool b;
    FcCharSet *chars;

    for (int i = 0; i < N_SCORES; i++)
        s->score[i] = 0;
    s->font = font;

    if (r->has_color && FcPatternGetBool(font, FC_COLOR, 0, &b) == FcResultMatch)
        s->score[SCORE_COLOR] = (b != FcFalse) != (r->color != FcFalse);

    s->score[SCORE_FAMILY_STRONG] = s->score[SCORE_FAMILY_WEAK] = NO_FAMILY;
    const NsFcElt *names = nsfc_pattern_find(font, FC_FAMILY);
    for (int i = 0; r->families && names && i < r->families->n; i++) {
        const NsFcValue *want = &r->families->values[i];
        int which = want->binding == NSFC_STRONG ? SCORE_FAMILY_STRONG
                                                 : SCORE_FAMILY_WEAK;
        if (want->type != FcTypeString || s->score[which] != NO_FAMILY)
            continue;
        for (int j = 0; j < names->n; j++) {
            if (names->values[j].type == FcTypeString &&
                nsfc_family_cmp(want->u.s, names->values[j].u.s) == 0) {
                s->score[which] = i;
                break;
            }
        }
    }

    if (r->exemplar &&
        (FcPatternGetCharSet(font, FC_CHARSET, 0, &chars) != FcResultMatch ||
         !FcCharSetHasChar(chars, r->exemplar)))
        s->score[SCORE_LANG] = 1;

    /* Unless the request says otherwise, a proportional font before a
     * fixed-width one: this decides among the fonts the request did not
     * name, where a typewriter face is a poor stand-in. */
    if (FcPatternGetDouble(font, FC_SPACING, 0, &d) != FcResultMatch)
        d = FC_PROPORTIONAL;
    s->score[SCORE_SPACING] = r->has_spacing ? fabs(d - r->spacing) : d;
    if (r->has_slant && FcPatternGetDouble(font, FC_SLANT, 0, &d) == FcResultMatch)
        s->score[SCORE_SLANT] = fabs(d - r->slant);
    if (r->has_weight) {
        if (FcPatternGetDouble(font, NSFC_WEIGHT_MIN, 0, &lo) == FcResultMatch &&
            FcPatternGetDouble(font, NSFC_WEIGHT_MAX, 0, &hi) == FcResultMatch)
            s->score[SCORE_WEIGHT] = r->weight < lo ? lo - r->weight
                                   : r->weight > hi ? r->weight - hi : 0;
        else if (FcPatternGetDouble(font, FC_WEIGHT, 0, &d) == FcResultMatch)
            s->score[SCORE_WEIGHT] = fabs(d - r->weight);
    }
    if (r->has_width && FcPatternGetDouble(font, FC_WIDTH, 0, &d) == FcResultMatch)
        s->score[SCORE_WIDTH] = fabs(d - r->width);
    if (r->has_order && FcPatternGetDouble(font, FC_ORDER, 0, &d) == FcResultMatch)
        s->score[SCORE_ORDER] = fabs(d - r->order);
}

static int
cmp_scored(const void *pa, const void *pb)
{
    const Scored *a = pa, *b = pb;
    for (int i = 0; i < N_SCORES; i++) {
        if (a->score[i] < b->score[i]) return -1;
        if (a->score[i] > b->score[i]) return 1;
    }
    return a->index < b->index ? -1 : a->index > b->index;
}

/* Every font of the sets, scored against the request; *n_out of them. */
static Scored *
score_sets(FcFontSet **sets, int nsets, const FcPattern *p, int *n_out)
{
    Request request;
    int total = 0, n = 0;
    for (int i = 0; i < nsets; i++)
        if (sets[i]) total += sets[i]->nfont;
    *n_out = 0;
    if (!total) return NULL;
    Scored *scored = SDL_malloc((size_t)total * sizeof *scored);
    if (!scored) return NULL;
    request_read(&request, p);
    for (int i = 0; i < nsets; i++) {
        for (int j = 0; sets[i] && j < sets[i]->nfont; j++) {
            score_font(&scored[n], &request, sets[i]->fonts[j]);
            scored[n].index = n;
            n++;
        }
    }
    *n_out = n;
    return scored;
}

/* ---- the queries ---- */

static FcFontSet **
config_sets(FcConfig *config, FcFontSet *sets[2])
{
    sets[0] = FcConfigGetFonts(config, FcSetSystem);
    sets[1] = FcConfigGetFonts(config, FcSetApplication);
    return sets;
}

FcFontSet *
FcFontSetSort(FcConfig *config, FcFontSet **sets, int nsets, FcPattern *p,
              FcBool trim, FcCharSet **csp, FcResult *result)
{
    int n;
    (void)config;
    if (csp) *csp = NULL;
    if (result) *result = FcResultNoMatch;
    FcFontSet *out = FcFontSetCreate();
    if (!out || !p) return out;
    Scored *scored = score_sets(sets, nsets, p, &n);
    if (!scored) return out;
    qsort(scored, (size_t)n, sizeof *scored, cmp_scored);

    /* Trimmed, the list keeps the best font and after it only fonts with
     * characters none before them has. */
    FcCharSet *covered = trim ? FcCharSetCreate() : NULL;
    for (int i = 0; i < n; i++) {
        FcPattern *font = scored[i].font;
        if (covered) {
            FcCharSet *chars;
            FcBool adds = FcFalse;
            if (FcPatternGetCharSet(font, FC_CHARSET, 0, &chars) == FcResultMatch)
                FcCharSetMerge(covered, chars, &adds);
            if (!adds && i > 0)
                continue;
        }
        FcPatternReference(font);
        if (!FcFontSetAdd(out, font))
            FcPatternDestroy(font);
    }
    FcCharSetDestroy(covered);
    SDL_free(scored);
    if (result && out->nfont) *result = FcResultMatch;
    return out;
}

FcFontSet *
FcFontSort(FcConfig *config, FcPattern *p, FcBool trim, FcCharSet **csp,
           FcResult *result)
{
    FcFontSet *sets[2];
    return FcFontSetSort(config, config_sets(config, sets), 2, p, trim, csp, result);
}

FcPattern *
FcFontSetMatch(FcConfig *config, FcFontSet **sets, int nsets, FcPattern *p,
               FcResult *result)
{
    int n, best = 0;
    if (result) *result = FcResultNoMatch;
    if (!p) return NULL;
    Scored *scored = score_sets(sets, nsets, p, &n);
    if (!scored) return NULL;
    for (int i = 1; i < n; i++)
        if (cmp_scored(&scored[i], &scored[best]) < 0)
            best = i;
    FcPattern *match = FcFontRenderPrepare(config, p, scored[best].font);
    SDL_free(scored);
    if (match && result) *result = FcResultMatch;
    return match;
}

FcPattern *
FcFontMatch(FcConfig *config, FcPattern *p, FcResult *result)
{
    FcFontSet *sets[2];
    return FcFontSetMatch(config, config_sets(config, sets), 2, p, result);
}

/* The pattern a font is opened with: the font's own properties, the
 * request's where the font says nothing (size, matrix, features, hinting),
 * and what makes up for a face the family lacks: a variable font set to
 * the requested weight, else a regular face emboldened, and an upright
 * face slanted. */
FcPattern *
FcFontRenderPrepare(FcConfig *config, FcPattern *pat, FcPattern *font)
{
    double want, have, lo, hi;
    (void)config;
    FcPattern *out = FcPatternDuplicate(font);
    if (!out || !pat) return out;

    for (int i = 0; i < pat->n; i++) {
        const NsFcElt *e = &pat->elts[i];
        if (nsfc_pattern_find(font, e->object) ||
            strcmp(e->object, FC_LANG) == 0 || strcmp(e->object, FC_CHARSET) == 0)
            continue;
        for (int j = 0; j < e->n; j++) {
            NsFcValue v;
            if (nsfc_value_copy(&v, &e->values[j]))
                nsfc_pattern_add(out, e->object, v, true);
        }
    }

    if (FcPatternGetDouble(pat, FC_WEIGHT, 0, &want) == FcResultMatch &&
        FcPatternGetDouble(font, FC_WEIGHT, 0, &have) == FcResultMatch) {
        if (FcPatternGetDouble(font, NSFC_WEIGHT_MIN, 0, &lo) == FcResultMatch &&
            FcPatternGetDouble(font, NSFC_WEIGHT_MAX, 0, &hi) == FcResultMatch) {
            double weight = want < lo ? lo : want > hi ? hi : want;
            FcChar8 *others = NULL;
            char *variations = NULL;
            /* Before the request's own settings, which then win. */
            FcPatternGetString(out, FC_FONT_VARIATIONS, 0, &others);
            if (SDL_asprintf(&variations, "wght=%d%s%s",
                             (int)lround(FcWeightToOpenTypeDouble(weight)),
                             others ? "," : "",
                             others ? (const char *)others : "") >= 0) {
                FcPatternDel(out, FC_FONT_VARIATIONS);
                FcPatternAddString(out, FC_FONT_VARIATIONS,
                                   (const FcChar8 *)variations);
                SDL_free(variations);
                FcPatternDel(out, FC_WEIGHT);
                FcPatternAddDouble(out, FC_WEIGHT, weight);
            }
        } else if (want >= FC_WEIGHT_BOLD && have <= FC_WEIGHT_MEDIUM) {
            FcPatternDel(out, FC_EMBOLDEN);
            FcPatternAddBool(out, FC_EMBOLDEN, FcTrue);
            FcPatternDel(out, FC_WEIGHT);
            FcPatternAddInteger(out, FC_WEIGHT, FC_WEIGHT_BOLD);
        }
    }

    if (FcPatternGetDouble(pat, FC_SLANT, 0, &want) == FcResultMatch &&
        FcPatternGetDouble(font, FC_SLANT, 0, &have) == FcResultMatch &&
        want != FC_SLANT_ROMAN && have == FC_SLANT_ROMAN) {
        FcMatrix shear = { 1, 0.2, 0, 1 }, m = shear, *given;
        if (FcPatternGetMatrix(out, FC_MATRIX, 0, &given) == FcResultMatch)
            FcMatrixMultiply(&m, given, &shear);
        FcPatternDel(out, FC_MATRIX);
        FcPatternAddMatrix(out, FC_MATRIX, &m);
        FcPatternDel(out, FC_SLANT);
        FcPatternAddInteger(out, FC_SLANT, FC_SLANT_OBLIQUE);
    }
    return out;
}

/* ---- listing ---- */

static bool
value_matches(const NsFcValue *a, const NsFcValue *b)
{
    double da, db;
    if (a->type == FcTypeString && b->type == FcTypeString)
        return nsfc_family_cmp(a->u.s, b->u.s) == 0;
    if (a->type == FcTypeBool && b->type == FcTypeBool)
        return a->u.b == FcDontCare || b->u.b == FcDontCare || a->u.b == b->u.b;
    bool na = a->type == FcTypeInteger || a->type == FcTypeDouble;
    bool nb = b->type == FcTypeInteger || b->type == FcTypeDouble;
    if (na && nb) {
        da = a->type == FcTypeInteger ? a->u.i : a->u.d;
        db = b->type == FcTypeInteger ? b->u.i : b->u.d;
        return da == db;
    }
    return false;
}

/* Whether a font has, for every property of the pattern, one of the
 * pattern's values. */
static bool
font_matches(const FcPattern *font, const FcPattern *p)
{
    for (int i = 0; p && i < p->n; i++) {
        const NsFcElt *want = &p->elts[i];
        const NsFcElt *have = nsfc_pattern_find(font, want->object);
        bool any = false;
        if (!have) return false;
        for (int j = 0; j < want->n && !any; j++)
            for (int k = 0; k < have->n && !any; k++)
                any = value_matches(&want->values[j], &have->values[k]);
        if (!any) return false;
    }
    return true;
}

/* The fonts matching a pattern, each reduced to the properties of the
 * object set, without duplicates. */
FcFontSet *
FcFontSetList(FcConfig *config, FcFontSet **sets, int nsets, FcPattern *p,
              FcObjectSet *os)
{
    (void)config;
    FcFontSet *out = FcFontSetCreate();
    size_t n_slots = 64;
    int *slots = NULL; /* an open-addressed table of indexes into out, +1 */
    if (!out) return NULL;
    for (int i = 0; i < nsets; i++)
        if (sets[i]) n_slots += (size_t)sets[i]->nfont * 2;
    slots = SDL_calloc(n_slots, sizeof *slots);
    if (!slots) return out;

    for (int i = 0; i < nsets; i++) {
        for (int j = 0; sets[i] && j < sets[i]->nfont; j++) {
            const FcPattern *font = sets[i]->fonts[j];
            if (!font_matches(font, p))
                continue;
            FcPattern *reduced;
            if (os) {
                reduced = FcPatternCreate();
                for (int k = 0; reduced && k < os->nobject; k++) {
                    const NsFcElt *e = nsfc_pattern_find(font, os->objects[k]);
                    for (int v = 0; e && v < e->n; v++) {
                        NsFcValue copy;
                        if (nsfc_value_copy(&copy, &e->values[v]))
                            nsfc_pattern_add(reduced, e->object, copy, true);
                    }
                }
            } else {
                reduced = FcPatternDuplicate(font);
            }
            if (!reduced) continue;

            size_t slot = FcPatternHash(reduced) % n_slots;
            bool duplicate = false;
            while (slots[slot]) {
                if (FcPatternEqual(out->fonts[slots[slot] - 1], reduced)) {
                    duplicate = true;
                    break;
                }
                slot = (slot + 1) % n_slots;
            }
            if (duplicate || !FcFontSetAdd(out, reduced))
                FcPatternDestroy(reduced);
            else
                slots[slot] = out->nfont;
        }
    }
    SDL_free(slots);
    return out;
}
