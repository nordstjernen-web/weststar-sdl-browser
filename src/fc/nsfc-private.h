/* Weststar SDL Browser — nsfc internals.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NSFC_PRIVATE_H
#define NSFC_PRIVATE_H

#include <fontconfig/fontconfig.h>
#include <fontconfig/fcfreetype.h>

#include <SDL3/SDL_atomic.h>
#include <SDL3/SDL_stdinc.h>
#include <stdbool.h>
#include <stddef.h>

/* Properties only nsfc sets on a font: the range of the font's weight axis
 * (a variable font), in fontconfig's weight units. */
#define NSFC_WEIGHT_MIN "nsfcweightmin"
#define NSFC_WEIGHT_MAX "nsfcweightmax"

/* How firmly a family name in a request was asked for: named by the
 * caller, or added by nsfc as a stand-in for a generic name. */
typedef enum {
    NSFC_STRONG = 0,
    NSFC_WEAK = 1,
} NsFcBinding;

typedef struct NsFcValue {
    FcType      type;
    NsFcBinding binding;
    union {
        int        i;
        double     d;
        FcBool     b;
        char      *s;
        FcMatrix  *m;
        FcCharSet *c;
    } u;
} NsFcValue;

typedef struct NsFcElt {
    char      *object;
    int        n, cap;
    NsFcValue *values;
} NsFcElt;

struct _FcPattern {
    SDL_AtomicInt ref;
    int           n, cap;
    NsFcElt      *elts; /* sorted by object name */
};

/* A set of code points: 256-bit pages, sorted by their number (the code
 * point without its low byte). A constant set, one that belongs to a
 * scanned font, is shared by reference and never changes. */
struct _FcCharSet {
    SDL_AtomicInt ref;
    bool          constant;
    int           n, cap;
    Uint32       *keys;
    Uint32      (*pages)[8];
};

struct _FcStrSet {
    int    n, cap;
    char **strs;
};

struct _FcStrList {
    FcStrSet *set;
    int       pos;
};

struct _FcLangSet {
    FcStrSet langs;
};

struct _FcConfig {
    SDL_AtomicInt ref;
    FcFontSet    *sets[2];
};

/* nsfc-pattern.c */
const NsFcElt *nsfc_pattern_find(const FcPattern *p, const char *object);
bool nsfc_pattern_add(FcPattern *p, const char *object, NsFcValue value,
                      bool append);
bool nsfc_value_copy(NsFcValue *dst, const NsFcValue *src);
void nsfc_value_clear(NsFcValue *v);
int  nsfc_family_cmp(const char *a, const char *b);
void nsfc_charset_set_constant(FcCharSet *fcs);
bool nsfc_str_set_add(FcStrSet *set, const char *s);

/* nsfc-scan.c */
/* The patterns of the faces in a font file, appended to `set`; returns how
 * many. `only_face` is a face's index, or -1 for all of them. */
int nsfc_scan_file(const char *path, int only_face, int *n_faces,
                   FcFontSet *set);

/* A serialized pattern, for the cache. */
typedef struct NsFcBuf {
    Uint8 *data;
    size_t len, cap;
    bool   failed;
} NsFcBuf;
void nsfc_pattern_write(NsFcBuf *buf, const FcPattern *p);
FcPattern *nsfc_pattern_read(const Uint8 **cursor, const Uint8 *end);
void nsfc_buf_put(NsFcBuf *buf, const void *data, size_t len);
void nsfc_buf_put_u32(NsFcBuf *buf, Uint32 v);
void nsfc_buf_put_str(NsFcBuf *buf, const char *s);
bool nsfc_read_u32(const Uint8 **cursor, const Uint8 *end, Uint32 *v);
const char *nsfc_read_str(const Uint8 **cursor, const Uint8 *end);

#endif /* NSFC_PRIVATE_H */
