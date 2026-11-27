/* Weststar SDL Browser — nsfc: the part of the fontconfig API the text stack
 * uses, implemented in the tree for builds without fontconfig.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This header stands in for <fontconfig/fontconfig.h> when the build is
 * configured with -Dfontconfig=disabled (or fontconfig is not installed):
 * web-text-lib's font map and the engine keep calling Fc* functions, and
 * get nsfc's. The names, types and constants are fontconfig's; only what
 * Weststar calls is here. See docs/porting-to-sdl.md.
 */

#ifndef NSFC_FONTCONFIG_H
#define NSFC_FONTCONFIG_H

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Tells callers which implementation they got. */
#define NSFC_INTREE 1

typedef unsigned char FcChar8;
typedef unsigned int  FcChar32;
typedef int           FcBool;

#define FcFalse    0
#define FcTrue     1
#define FcDontCare 2

/* Properties of a pattern. */
#define FC_FAMILY          "family"         /* String */
#define FC_STYLE           "style"          /* String */
#define FC_SLANT           "slant"          /* Int */
#define FC_WEIGHT          "weight"         /* Int or Double */
#define FC_SIZE            "size"           /* Double */
#define FC_PIXEL_SIZE      "pixelsize"      /* Double */
#define FC_SPACING         "spacing"        /* Int */
#define FC_WIDTH           "width"          /* Int */
#define FC_FILE            "file"           /* String */
#define FC_INDEX           "index"          /* Int */
#define FC_HINTING         "hinting"        /* Bool */
#define FC_HINT_STYLE      "hintstyle"      /* Int */
#define FC_AUTOHINT        "autohint"       /* Bool */
#define FC_VERTICAL_LAYOUT "verticallayout" /* Bool */
#define FC_ANTIALIAS       "antialias"      /* Bool */
#define FC_RASTERIZER      "rasterizer"     /* String */
#define FC_OUTLINE         "outline"        /* Bool */
#define FC_SCALABLE        "scalable"       /* Bool */
#define FC_COLOR           "color"          /* Bool */
#define FC_VARIABLE        "variable"       /* Bool */
#define FC_DPI             "dpi"            /* Double */
#define FC_MATRIX          "matrix"         /* FcMatrix */
#define FC_CHARSET         "charset"        /* CharSet */
#define FC_LANG            "lang"           /* String or LangSet */
#define FC_FAMILYLANG      "familylang"     /* String */
#define FC_EMBOLDEN        "embolden"       /* Bool */
#define FC_EMBEDDED_BITMAP "embeddedbitmap" /* Bool */
#define FC_FONTFORMAT      "fontformat"     /* String */
#define FC_FONT_FEATURES   "fontfeatures"   /* String */
#define FC_FONT_VARIATIONS "fontvariations" /* String */
#define FC_PRGNAME         "prgname"        /* String */
#define FC_ORDER           "order"          /* Int */
#define FC_FONT_WRAPPER    "fontwrapper"    /* String */

#define FC_WEIGHT_THIN       0
#define FC_WEIGHT_EXTRALIGHT 40
#define FC_WEIGHT_LIGHT      50
#define FC_WEIGHT_DEMILIGHT  55
#define FC_WEIGHT_SEMILIGHT  FC_WEIGHT_DEMILIGHT
#define FC_WEIGHT_BOOK       75
#define FC_WEIGHT_REGULAR    80
#define FC_WEIGHT_NORMAL     FC_WEIGHT_REGULAR
#define FC_WEIGHT_MEDIUM     100
#define FC_WEIGHT_DEMIBOLD   180
#define FC_WEIGHT_SEMIBOLD   FC_WEIGHT_DEMIBOLD
#define FC_WEIGHT_BOLD       200
#define FC_WEIGHT_EXTRABOLD  205
#define FC_WEIGHT_BLACK      210
#define FC_WEIGHT_EXTRABLACK 215

#define FC_SLANT_ROMAN   0
#define FC_SLANT_ITALIC  100
#define FC_SLANT_OBLIQUE 110

#define FC_WIDTH_ULTRACONDENSED 50
#define FC_WIDTH_EXTRACONDENSED 63
#define FC_WIDTH_CONDENSED      75
#define FC_WIDTH_SEMICONDENSED  87
#define FC_WIDTH_NORMAL         100
#define FC_WIDTH_SEMIEXPANDED   113
#define FC_WIDTH_EXPANDED       125
#define FC_WIDTH_EXTRAEXPANDED  150
#define FC_WIDTH_ULTRAEXPANDED  200

#define FC_PROPORTIONAL 0
#define FC_DUAL         90
#define FC_MONO         100
#define FC_CHARCELL     110

#define FC_HINT_NONE   0
#define FC_HINT_SLIGHT 1
#define FC_HINT_MEDIUM 2
#define FC_HINT_FULL   3

typedef enum _FcType {
    FcTypeUnknown = -1,
    FcTypeVoid,
    FcTypeInteger,
    FcTypeDouble,
    FcTypeString,
    FcTypeBool,
    FcTypeMatrix,
    FcTypeCharSet,
    FcTypeFTFace,
    FcTypeLangSet,
    FcTypeRange
} FcType;

typedef struct _FcMatrix {
    double xx, xy, yx, yy;
} FcMatrix;

#define FcMatrixInit(m) ((m)->xx = (m)->yy = 1, (m)->xy = (m)->yx = 0)

typedef enum _FcResult {
    FcResultMatch,
    FcResultNoMatch,
    FcResultTypeMismatch,
    FcResultNoId,
    FcResultOutOfMemory
} FcResult;

typedef struct _FcPattern FcPattern;
typedef struct _FcCharSet FcCharSet;
typedef struct _FcLangSet FcLangSet;
typedef struct _FcConfig  FcConfig;
typedef struct _FcStrSet  FcStrSet;
typedef struct _FcStrList FcStrList;
typedef struct _FcBlanks  FcBlanks;

typedef struct _FcFontSet {
    int         nfont;
    int         sfont;
    FcPattern **fonts;
} FcFontSet;

typedef struct _FcObjectSet {
    int          nobject;
    int          sobject;
    const char **objects;
} FcObjectSet;

typedef enum _FcMatchKind {
    FcMatchPattern,
    FcMatchFont,
    FcMatchScan
} FcMatchKind;

typedef enum _FcSetName {
    FcSetSystem = 0,
    FcSetApplication = 1
} FcSetName;

/* Configuration: the fonts found on the system, and those the application
 * added. A NULL config means the default one everywhere. */
FcBool     FcInit(void);
FcConfig  *FcConfigReference(FcConfig *config);
void       FcConfigDestroy(FcConfig *config);
FcFontSet *FcConfigGetFonts(FcConfig *config, FcSetName set);
FcBool     FcConfigAppFontAddFile(FcConfig *config, const FcChar8 *file);
FcBool     FcConfigSubstitute(FcConfig *config, FcPattern *p, FcMatchKind kind);
void       FcDefaultSubstitute(FcPattern *pattern);

/* Patterns. */
FcPattern *FcPatternCreate(void);
FcPattern *FcPatternDuplicate(const FcPattern *p);
void       FcPatternReference(FcPattern *p);
void       FcPatternDestroy(FcPattern *p);
FcBool     FcPatternEqual(const FcPattern *pa, const FcPattern *pb);
FcChar32   FcPatternHash(const FcPattern *p);
FcBool     FcPatternDel(FcPattern *p, const char *object);
FcBool     FcPatternRemove(FcPattern *p, const char *object, int id);
FcBool     FcPatternAddInteger(FcPattern *p, const char *object, int i);
FcBool     FcPatternAddDouble(FcPattern *p, const char *object, double d);
FcBool     FcPatternAddString(FcPattern *p, const char *object, const FcChar8 *s);
FcBool     FcPatternAddBool(FcPattern *p, const char *object, FcBool b);
FcBool     FcPatternAddMatrix(FcPattern *p, const char *object, const FcMatrix *m);
FcBool     FcPatternAddCharSet(FcPattern *p, const char *object, const FcCharSet *c);
FcResult   FcPatternGetInteger(const FcPattern *p, const char *object, int n, int *i);
FcResult   FcPatternGetDouble(const FcPattern *p, const char *object, int n, double *d);
FcResult   FcPatternGetString(const FcPattern *p, const char *object, int n, FcChar8 **s);
FcResult   FcPatternGetBool(const FcPattern *p, const char *object, int n, FcBool *b);
FcResult   FcPatternGetMatrix(const FcPattern *p, const char *object, int n, FcMatrix **m);
FcResult   FcPatternGetCharSet(const FcPattern *p, const char *object, int n, FcCharSet **c);
FcResult   FcPatternGetLangSet(const FcPattern *p, const char *object, int n, FcLangSet **ls);
/* object, FcType, value, … ending with a NULL object. */
FcPattern *FcPatternBuild(FcPattern *p, ...);

/* Font sets and object sets. */
FcFontSet   *FcFontSetCreate(void);
void         FcFontSetDestroy(FcFontSet *s);
FcBool       FcFontSetAdd(FcFontSet *s, FcPattern *font);
FcObjectSet *FcObjectSetCreate(void);
FcBool       FcObjectSetAdd(FcObjectSet *os, const char *object);
void         FcObjectSetDestroy(FcObjectSet *os);
FcObjectSet *FcObjectSetBuild(const char *first, ...);

/* Matching. */
FcFontSet *FcFontSetList(FcConfig *config, FcFontSet **sets, int nsets,
                         FcPattern *p, FcObjectSet *os);
FcPattern *FcFontSetMatch(FcConfig *config, FcFontSet **sets, int nsets,
                          FcPattern *p, FcResult *result);
FcPattern *FcFontMatch(FcConfig *config, FcPattern *p, FcResult *result);
FcFontSet *FcFontSetSort(FcConfig *config, FcFontSet **sets, int nsets,
                         FcPattern *p, FcBool trim, FcCharSet **csp,
                         FcResult *result);
FcFontSet *FcFontSort(FcConfig *config, FcPattern *p, FcBool trim,
                      FcCharSet **csp, FcResult *result);
FcPattern *FcFontRenderPrepare(FcConfig *config, FcPattern *pat, FcPattern *font);

/* Character sets. */
FcCharSet *FcCharSetCreate(void);
void       FcCharSetDestroy(FcCharSet *fcs);
FcCharSet *FcCharSetCopy(FcCharSet *src);
FcBool     FcCharSetAddChar(FcCharSet *fcs, FcChar32 ucs4);
FcBool     FcCharSetDelChar(FcCharSet *fcs, FcChar32 ucs4);
FcBool     FcCharSetHasChar(const FcCharSet *fcs, FcChar32 ucs4);
FcBool     FcCharSetMerge(FcCharSet *a, const FcCharSet *b, FcBool *changed);
FcChar32   FcCharSetCount(const FcCharSet *a);

/* Language sets and string lists. nsfc records no languages for a font, so
 * a font's pattern has no FC_LANG; these exist for the callers. */
FcStrSet  *FcLangSetGetLangs(const FcLangSet *ls);
void       FcStrSetDestroy(FcStrSet *set);
FcStrList *FcStrListCreate(FcStrSet *set);
void       FcStrListFirst(FcStrList *list);
FcChar8   *FcStrListNext(FcStrList *list);
void       FcStrListDone(FcStrList *list);

void   FcMatrixMultiply(FcMatrix *result, const FcMatrix *a, const FcMatrix *b);
double FcWeightFromOpenTypeDouble(double ot_weight);
double FcWeightToOpenTypeDouble(double fc_weight);

#ifdef __cplusplus
}
#endif

#endif /* NSFC_FONTCONFIG_H */
