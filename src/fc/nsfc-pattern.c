/* Weststar SDL Browser — nsfc: patterns, font sets and character sets.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "nsfc-private.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---- values ---- */

void
nsfc_value_clear(NsFcValue *v)
{
    switch (v->type) {
    case FcTypeString:  SDL_free(v->u.s); break;
    case FcTypeMatrix:  SDL_free(v->u.m); break;
    case FcTypeCharSet: FcCharSetDestroy(v->u.c); break;
    default: break;
    }
    v->type = FcTypeVoid;
}

bool
nsfc_value_copy(NsFcValue *dst, const NsFcValue *src)
{
    *dst = *src;
    switch (src->type) {
    case FcTypeString:
        dst->u.s = SDL_strdup(src->u.s);
        return dst->u.s != NULL;
    case FcTypeMatrix:
        dst->u.m = SDL_malloc(sizeof *dst->u.m);
        if (!dst->u.m) return false;
        *dst->u.m = *src->u.m;
        return true;
    case FcTypeCharSet:
        dst->u.c = FcCharSetCopy(src->u.c);
        return dst->u.c != NULL;
    default:
        return true;
    }
}

static bool
value_number(const NsFcValue *v, double *d)
{
    if (v->type == FcTypeInteger) { *d = v->u.i; return true; }
    if (v->type == FcTypeDouble)  { *d = v->u.d; return true; }
    return false;
}

static bool charset_equal(const FcCharSet *a, const FcCharSet *b);

static bool
value_equal(const NsFcValue *a, const NsFcValue *b)
{
    double da, db;
    if (value_number(a, &da) && value_number(b, &db))
        return da == db;
    if (a->type != b->type)
        return false;
    switch (a->type) {
    case FcTypeString:  return strcmp(a->u.s, b->u.s) == 0;
    case FcTypeBool:    return a->u.b == b->u.b;
    case FcTypeMatrix:  return memcmp(a->u.m, b->u.m, sizeof *a->u.m) == 0;
    case FcTypeCharSet: return charset_equal(a->u.c, b->u.c);
    default:            return true;
    }
}

static Uint32
hash_bytes(Uint32 h, const void *data, size_t len)
{
    const Uint8 *p = data;
    for (size_t i = 0; i < len; i++)
        h = (h ^ p[i]) * 16777619u;
    return h;
}

static Uint32
value_hash(Uint32 h, const NsFcValue *v)
{
    double d;
    if (value_number(v, &d)) {
        /* An integer and the same number as a double are equal. */
        Sint32 i = (Sint32)d;
        return hash_bytes(h, &i, sizeof i);
    }
    switch (v->type) {
    case FcTypeString: return hash_bytes(h, v->u.s, strlen(v->u.s));
    case FcTypeBool:   return hash_bytes(h, &v->u.b, sizeof v->u.b);
    case FcTypeMatrix: return hash_bytes(h, v->u.m, sizeof *v->u.m);
    case FcTypeCharSet: {
        Uint32 count = FcCharSetCount(v->u.c);
        return hash_bytes(h, &count, sizeof count);
    }
    default:           return h;
    }
}

/* ---- patterns ---- */

FcPattern *
FcPatternCreate(void)
{
    FcPattern *p = SDL_calloc(1, sizeof *p);
    if (p) SDL_SetAtomicInt(&p->ref, 1);
    return p;
}

void
FcPatternReference(FcPattern *p)
{
    if (p) SDL_AtomicIncRef(&p->ref);
}

static void
elt_clear(NsFcElt *e)
{
    for (int i = 0; i < e->n; i++)
        nsfc_value_clear(&e->values[i]);
    SDL_free(e->values);
    SDL_free(e->object);
}

void
FcPatternDestroy(FcPattern *p)
{
    if (!p || !SDL_AtomicDecRef(&p->ref))
        return;
    for (int i = 0; i < p->n; i++)
        elt_clear(&p->elts[i]);
    SDL_free(p->elts);
    SDL_free(p);
}

/* The index of `object`'s element, or of where it would go. */
static int
elt_position(const FcPattern *p, const char *object, bool *found)
{
    int lo = 0, hi = p->n;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        int c = strcmp(p->elts[mid].object, object);
        if (c == 0) { *found = true; return mid; }
        if (c < 0) lo = mid + 1; else hi = mid;
    }
    *found = false;
    return lo;
}

const NsFcElt *
nsfc_pattern_find(const FcPattern *p, const char *object)
{
    bool found;
    if (!p || !object) return NULL;
    int at = elt_position(p, object, &found);
    return found ? &p->elts[at] : NULL;
}

/* Adds a value, which the pattern takes over (also when it fails). */
bool
nsfc_pattern_add(FcPattern *p, const char *object, NsFcValue value, bool append)
{
    bool found;
    if (!p || !object) { nsfc_value_clear(&value); return false; }
    int at = elt_position(p, object, &found);
    if (!found) {
        if (p->n == p->cap) {
            int cap = p->cap ? p->cap * 2 : 16;
            NsFcElt *elts = SDL_realloc(p->elts, (size_t)cap * sizeof *elts);
            if (!elts) { nsfc_value_clear(&value); return false; }
            p->elts = elts;
            p->cap = cap;
        }
        char *name = SDL_strdup(object);
        if (!name) { nsfc_value_clear(&value); return false; }
        memmove(&p->elts[at + 1], &p->elts[at],
                (size_t)(p->n - at) * sizeof *p->elts);
        p->elts[at] = (NsFcElt){ .object = name };
        p->n++;
    }
    NsFcElt *e = &p->elts[at];
    if (e->n == e->cap) {
        int cap = e->cap ? e->cap * 2 : 1;
        NsFcValue *values = SDL_realloc(e->values, (size_t)cap * sizeof *values);
        if (!values) { nsfc_value_clear(&value); return false; }
        e->values = values;
        e->cap = cap;
    }
    if (append) {
        e->values[e->n] = value;
    } else {
        memmove(&e->values[1], &e->values[0], (size_t)e->n * sizeof *e->values);
        e->values[0] = value;
    }
    e->n++;
    return true;
}

FcPattern *
FcPatternDuplicate(const FcPattern *p)
{
    FcPattern *copy = FcPatternCreate();
    if (!copy || !p) return copy;
    for (int i = 0; i < p->n; i++) {
        const NsFcElt *e = &p->elts[i];
        for (int j = 0; j < e->n; j++) {
            NsFcValue v;
            if (!nsfc_value_copy(&v, &e->values[j]) ||
                !nsfc_pattern_add(copy, e->object, v, true)) {
                FcPatternDestroy(copy);
                return NULL;
            }
        }
    }
    return copy;
}

FcBool
FcPatternEqual(const FcPattern *pa, const FcPattern *pb)
{
    if (pa == pb) return FcTrue;
    if (!pa || !pb || pa->n != pb->n) return FcFalse;
    for (int i = 0; i < pa->n; i++) {
        const NsFcElt *a = &pa->elts[i], *b = &pb->elts[i];
        if (a->n != b->n || strcmp(a->object, b->object) != 0)
            return FcFalse;
        for (int j = 0; j < a->n; j++)
            if (!value_equal(&a->values[j], &b->values[j]))
                return FcFalse;
    }
    return FcTrue;
}

FcChar32
FcPatternHash(const FcPattern *p)
{
    Uint32 h = 2166136261u;
    if (!p) return h;
    for (int i = 0; i < p->n; i++) {
        const NsFcElt *e = &p->elts[i];
        h = hash_bytes(h, e->object, strlen(e->object));
        for (int j = 0; j < e->n; j++)
            h = value_hash(h, &e->values[j]);
    }
    return h;
}

FcBool
FcPatternDel(FcPattern *p, const char *object)
{
    bool found;
    if (!p || !object) return FcFalse;
    int at = elt_position(p, object, &found);
    if (!found) return FcFalse;
    elt_clear(&p->elts[at]);
    memmove(&p->elts[at], &p->elts[at + 1],
            (size_t)(p->n - at - 1) * sizeof *p->elts);
    p->n--;
    return FcTrue;
}

FcBool
FcPatternRemove(FcPattern *p, const char *object, int id)
{
    bool found;
    if (!p || !object) return FcFalse;
    int at = elt_position(p, object, &found);
    if (!found) return FcFalse;
    NsFcElt *e = &p->elts[at];
    if (id < 0 || id >= e->n) return FcFalse;
    nsfc_value_clear(&e->values[id]);
    memmove(&e->values[id], &e->values[id + 1],
            (size_t)(e->n - id - 1) * sizeof *e->values);
    if (--e->n == 0)
        FcPatternDel(p, object);
    return FcTrue;
}

FcBool
FcPatternAddInteger(FcPattern *p, const char *object, int i)
{
    return nsfc_pattern_add(p, object,
        (NsFcValue){ .type = FcTypeInteger, .u.i = i }, true);
}

FcBool
FcPatternAddDouble(FcPattern *p, const char *object, double d)
{
    return nsfc_pattern_add(p, object,
        (NsFcValue){ .type = FcTypeDouble, .u.d = d }, true);
}

FcBool
FcPatternAddBool(FcPattern *p, const char *object, FcBool b)
{
    return nsfc_pattern_add(p, object,
        (NsFcValue){ .type = FcTypeBool, .u.b = b }, true);
}

FcBool
FcPatternAddString(FcPattern *p, const char *object, const FcChar8 *s)
{
    if (!s) return FcFalse;
    char *copy = SDL_strdup((const char *)s);
    if (!copy) return FcFalse;
    return nsfc_pattern_add(p, object,
        (NsFcValue){ .type = FcTypeString, .u.s = copy }, true);
}

FcBool
FcPatternAddMatrix(FcPattern *p, const char *object, const FcMatrix *m)
{
    if (!m) return FcFalse;
    FcMatrix *copy = SDL_malloc(sizeof *copy);
    if (!copy) return FcFalse;
    *copy = *m;
    return nsfc_pattern_add(p, object,
        (NsFcValue){ .type = FcTypeMatrix, .u.m = copy }, true);
}

FcBool
FcPatternAddCharSet(FcPattern *p, const char *object, const FcCharSet *c)
{
    if (!c) return FcFalse;
    /* Copying takes a reference to a constant set and nothing else. */
    FcCharSet *copy = FcCharSetCopy((FcCharSet *)(uintptr_t)c);
    if (!copy) return FcFalse;
    return nsfc_pattern_add(p, object,
        (NsFcValue){ .type = FcTypeCharSet, .u.c = copy }, true);
}

static FcResult
pattern_get(const FcPattern *p, const char *object, int n, const NsFcValue **v)
{
    const NsFcElt *e = nsfc_pattern_find(p, object);
    if (!e) return FcResultNoMatch;
    if (n < 0 || n >= e->n) return FcResultNoId;
    *v = &e->values[n];
    return FcResultMatch;
}

FcResult
FcPatternGetInteger(const FcPattern *p, const char *object, int n, int *i)
{
    const NsFcValue *v;
    FcResult r = pattern_get(p, object, n, &v);
    if (r != FcResultMatch) return r;
    if (v->type == FcTypeInteger) *i = v->u.i;
    else if (v->type == FcTypeDouble) *i = (int)v->u.d;
    else return FcResultTypeMismatch;
    return FcResultMatch;
}

FcResult
FcPatternGetDouble(const FcPattern *p, const char *object, int n, double *d)
{
    const NsFcValue *v;
    FcResult r = pattern_get(p, object, n, &v);
    if (r != FcResultMatch) return r;
    return value_number(v, d) ? FcResultMatch : FcResultTypeMismatch;
}

FcResult
FcPatternGetString(const FcPattern *p, const char *object, int n, FcChar8 **s)
{
    const NsFcValue *v;
    FcResult r = pattern_get(p, object, n, &v);
    if (r != FcResultMatch) return r;
    if (v->type != FcTypeString) return FcResultTypeMismatch;
    *s = (FcChar8 *)v->u.s;
    return FcResultMatch;
}

FcResult
FcPatternGetBool(const FcPattern *p, const char *object, int n, FcBool *b)
{
    const NsFcValue *v;
    FcResult r = pattern_get(p, object, n, &v);
    if (r != FcResultMatch) return r;
    if (v->type != FcTypeBool) return FcResultTypeMismatch;
    *b = v->u.b;
    return FcResultMatch;
}

FcResult
FcPatternGetMatrix(const FcPattern *p, const char *object, int n, FcMatrix **m)
{
    const NsFcValue *v;
    FcResult r = pattern_get(p, object, n, &v);
    if (r != FcResultMatch) return r;
    if (v->type != FcTypeMatrix) return FcResultTypeMismatch;
    *m = v->u.m;
    return FcResultMatch;
}

FcResult
FcPatternGetCharSet(const FcPattern *p, const char *object, int n, FcCharSet **c)
{
    const NsFcValue *v;
    FcResult r = pattern_get(p, object, n, &v);
    if (r != FcResultMatch) return r;
    if (v->type != FcTypeCharSet) return FcResultTypeMismatch;
    *c = v->u.c;
    return FcResultMatch;
}

FcResult
FcPatternGetLangSet(const FcPattern *p, const char *object, int n, FcLangSet **ls)
{
    /* No pattern holds a language set: a font's has no FC_LANG, and a
     * request's is a string. */
    const NsFcValue *v;
    FcResult r = pattern_get(p, object, n, &v);
    (void)ls;
    return r != FcResultMatch ? r : FcResultTypeMismatch;
}

FcPattern *
FcPatternBuild(FcPattern *p, ...)
{
    va_list va;
    bool ok = true;
    FcPattern *created = NULL;
    if (!p) {
        p = created = FcPatternCreate();
        if (!p) return NULL;
    }
    va_start(va, p);
    for (;;) {
        const char *object = va_arg(va, const char *);
        if (!object) break;
        FcType type = (FcType)va_arg(va, int);
        switch (type) {
        case FcTypeInteger:
            ok = FcPatternAddInteger(p, object, va_arg(va, int)) && ok;
            break;
        case FcTypeDouble:
            ok = FcPatternAddDouble(p, object, va_arg(va, double)) && ok;
            break;
        case FcTypeBool:
            ok = FcPatternAddBool(p, object, va_arg(va, FcBool)) && ok;
            break;
        case FcTypeString:
            ok = FcPatternAddString(p, object, va_arg(va, const FcChar8 *)) && ok;
            break;
        case FcTypeMatrix:
            ok = FcPatternAddMatrix(p, object, va_arg(va, const FcMatrix *)) && ok;
            break;
        case FcTypeCharSet:
            ok = FcPatternAddCharSet(p, object, va_arg(va, const FcCharSet *)) && ok;
            break;
        default:
            /* A type whose argument size is unknown: stop reading. */
            ok = false;
            break;
        }
        if (!ok) break;
    }
    va_end(va);
    if (!ok && created) {
        FcPatternDestroy(created);
        return NULL;
    }
    return p;
}

/* Family names compare as fontconfig compares them: without case and
 * without blanks. Only ASCII letters are folded. */
int
nsfc_family_cmp(const char *a, const char *b)
{
    for (;;) {
        while (*a == ' ') a++;
        while (*b == ' ') b++;
        unsigned char ca = (unsigned char)*a, cb = (unsigned char)*b;
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb + 32);
        if (ca != cb) return ca < cb ? -1 : 1;
        if (!ca) return 0;
        a++; b++;
    }
}

/* ---- serialized patterns (the font cache) ---- */

void
nsfc_buf_put(NsFcBuf *buf, const void *data, size_t len)
{
    if (buf->failed) return;
    if (buf->len + len > buf->cap) {
        size_t cap = buf->cap ? buf->cap * 2 : 65536;
        while (cap < buf->len + len) cap *= 2;
        Uint8 *grown = SDL_realloc(buf->data, cap);
        if (!grown) { buf->failed = true; return; }
        buf->data = grown;
        buf->cap = cap;
    }
    memcpy(buf->data + buf->len, data, len);
    buf->len += len;
}

void
nsfc_buf_put_u32(NsFcBuf *buf, Uint32 v)
{
    nsfc_buf_put(buf, &v, sizeof v);
}

void
nsfc_buf_put_str(NsFcBuf *buf, const char *s)
{
    Uint32 len = (Uint32)strlen(s);
    nsfc_buf_put_u32(buf, len);
    nsfc_buf_put(buf, s, (size_t)len + 1);
}

bool
nsfc_read_u32(const Uint8 **cursor, const Uint8 *end, Uint32 *v)
{
    if ((size_t)(end - *cursor) < sizeof *v) return false;
    memcpy(v, *cursor, sizeof *v);
    *cursor += sizeof *v;
    return true;
}

/* A string in the buffer, checked to end with its NUL inside it. */
const char *
nsfc_read_str(const Uint8 **cursor, const Uint8 *end)
{
    Uint32 len;
    if (!nsfc_read_u32(cursor, end, &len)) return NULL;
    if ((size_t)(end - *cursor) <= len || (*cursor)[len] != 0) return NULL;
    const char *s = (const char *)*cursor;
    if (strlen(s) != len) return NULL;
    *cursor += (size_t)len + 1;
    return s;
}

void
nsfc_pattern_write(NsFcBuf *buf, const FcPattern *p)
{
    nsfc_buf_put_u32(buf, (Uint32)p->n);
    for (int i = 0; i < p->n; i++) {
        const NsFcElt *e = &p->elts[i];
        nsfc_buf_put_str(buf, e->object);
        nsfc_buf_put_u32(buf, (Uint32)e->n);
        for (int j = 0; j < e->n; j++) {
            const NsFcValue *v = &e->values[j];
            nsfc_buf_put_u32(buf, (Uint32)v->type);
            switch (v->type) {
            case FcTypeInteger: nsfc_buf_put(buf, &v->u.i, sizeof v->u.i); break;
            case FcTypeDouble:  nsfc_buf_put(buf, &v->u.d, sizeof v->u.d); break;
            case FcTypeBool:    nsfc_buf_put(buf, &v->u.b, sizeof v->u.b); break;
            case FcTypeString:  nsfc_buf_put_str(buf, v->u.s); break;
            case FcTypeMatrix:  nsfc_buf_put(buf, v->u.m, sizeof *v->u.m); break;
            case FcTypeCharSet:
                nsfc_buf_put_u32(buf, (Uint32)v->u.c->n);
                nsfc_buf_put(buf, v->u.c->keys,
                             (size_t)v->u.c->n * sizeof *v->u.c->keys);
                nsfc_buf_put(buf, v->u.c->pages,
                             (size_t)v->u.c->n * sizeof *v->u.c->pages);
                break;
            default: break;
            }
        }
    }
}

static bool
read_bytes(const Uint8 **cursor, const Uint8 *end, void *out, size_t len)
{
    if ((size_t)(end - *cursor) < len) return false;
    memcpy(out, *cursor, len);
    *cursor += len;
    return true;
}

static FcCharSet *
charset_read(const Uint8 **cursor, const Uint8 *end)
{
    Uint32 n;
    if (!nsfc_read_u32(cursor, end, &n) || n > 0x1100) return NULL;
    FcCharSet *fcs = FcCharSetCreate();
    if (!fcs) return NULL;
    if (n) {
        fcs->keys = SDL_malloc(n * sizeof *fcs->keys);
        fcs->pages = SDL_malloc(n * sizeof *fcs->pages);
        if (!fcs->keys || !fcs->pages ||
            !read_bytes(cursor, end, fcs->keys, n * sizeof *fcs->keys) ||
            !read_bytes(cursor, end, fcs->pages, n * sizeof *fcs->pages)) {
            FcCharSetDestroy(fcs);
            return NULL;
        }
        fcs->n = fcs->cap = (int)n;
        /* The lookups rely on the order. */
        for (Uint32 i = 0; i < n; i++) {
            if (fcs->keys[i] > 0x10ff || (i && fcs->keys[i - 1] >= fcs->keys[i])) {
                FcCharSetDestroy(fcs);
                return NULL;
            }
        }
    }
    fcs->constant = true;
    return fcs;
}

/* Reads a pattern written by nsfc_pattern_write. The data is a cache file
 * that anything may have damaged, so every length is checked. */
FcPattern *
nsfc_pattern_read(const Uint8 **cursor, const Uint8 *end)
{
    Uint32 n_elts;
    if (!nsfc_read_u32(cursor, end, &n_elts) || n_elts > 256) return NULL;
    FcPattern *p = FcPatternCreate();
    if (!p) return NULL;
    for (Uint32 i = 0; i < n_elts; i++) {
        Uint32 n_values;
        const char *object = nsfc_read_str(cursor, end);
        if (!object || !nsfc_read_u32(cursor, end, &n_values) || n_values > 4096)
            goto fail;
        for (Uint32 j = 0; j < n_values; j++) {
            Uint32 type;
            NsFcValue v = { .type = FcTypeVoid };
            if (!nsfc_read_u32(cursor, end, &type)) goto fail;
            switch ((FcType)type) {
            case FcTypeInteger:
                if (!read_bytes(cursor, end, &v.u.i, sizeof v.u.i)) goto fail;
                break;
            case FcTypeDouble:
                if (!read_bytes(cursor, end, &v.u.d, sizeof v.u.d)) goto fail;
                break;
            case FcTypeBool:
                if (!read_bytes(cursor, end, &v.u.b, sizeof v.u.b)) goto fail;
                break;
            case FcTypeString: {
                const char *s = nsfc_read_str(cursor, end);
                if (!s || !(v.u.s = SDL_strdup(s))) goto fail;
                break;
            }
            case FcTypeMatrix:
                if (!(v.u.m = SDL_malloc(sizeof *v.u.m))) goto fail;
                if (!read_bytes(cursor, end, v.u.m, sizeof *v.u.m)) {
                    SDL_free(v.u.m);
                    goto fail;
                }
                break;
            case FcTypeCharSet:
                if (!(v.u.c = charset_read(cursor, end))) goto fail;
                break;
            default:
                goto fail;
            }
            v.type = (FcType)type;
            if (!nsfc_pattern_add(p, object, v, true)) goto fail;
        }
    }
    return p;
fail:
    FcPatternDestroy(p);
    return NULL;
}

/* ---- font sets and object sets ---- */

FcFontSet *
FcFontSetCreate(void)
{
    return SDL_calloc(1, sizeof(FcFontSet));
}

void
FcFontSetDestroy(FcFontSet *s)
{
    if (!s) return;
    for (int i = 0; i < s->nfont; i++)
        FcPatternDestroy(s->fonts[i]);
    SDL_free(s->fonts);
    SDL_free(s);
}

/* Takes over the caller's reference to the font. */
FcBool
FcFontSetAdd(FcFontSet *s, FcPattern *font)
{
    if (!s || !font) return FcFalse;
    if (s->nfont == s->sfont) {
        int cap = s->sfont ? s->sfont * 2 : 16;
        FcPattern **fonts = SDL_realloc(s->fonts, (size_t)cap * sizeof *fonts);
        if (!fonts) return FcFalse;
        s->fonts = fonts;
        s->sfont = cap;
    }
    s->fonts[s->nfont++] = font;
    return FcTrue;
}

FcObjectSet *
FcObjectSetCreate(void)
{
    return SDL_calloc(1, sizeof(FcObjectSet));
}

FcBool
FcObjectSetAdd(FcObjectSet *os, const char *object)
{
    if (!os || !object) return FcFalse;
    if (os->nobject == os->sobject) {
        int cap = os->sobject ? os->sobject * 2 : 8;
        const char **objects = SDL_realloc(os->objects, (size_t)cap * sizeof *objects);
        if (!objects) return FcFalse;
        os->objects = objects;
        os->sobject = cap;
    }
    char *copy = SDL_strdup(object);
    if (!copy) return FcFalse;
    os->objects[os->nobject++] = copy;
    return FcTrue;
}

void
FcObjectSetDestroy(FcObjectSet *os)
{
    if (!os) return;
    for (int i = 0; i < os->nobject; i++)
        SDL_free((char *)(uintptr_t)os->objects[i]);
    SDL_free(os->objects);
    SDL_free(os);
}

FcObjectSet *
FcObjectSetBuild(const char *first, ...)
{
    va_list va;
    FcObjectSet *os = FcObjectSetCreate();
    if (!os) return NULL;
    va_start(va, first);
    for (const char *object = first; object; object = va_arg(va, const char *)) {
        if (!FcObjectSetAdd(os, object)) {
            FcObjectSetDestroy(os);
            os = NULL;
            break;
        }
    }
    va_end(va);
    return os;
}

/* ---- character sets ---- */

FcCharSet *
FcCharSetCreate(void)
{
    FcCharSet *fcs = SDL_calloc(1, sizeof *fcs);
    if (fcs) SDL_SetAtomicInt(&fcs->ref, 1);
    return fcs;
}

void
FcCharSetDestroy(FcCharSet *fcs)
{
    if (!fcs || !SDL_AtomicDecRef(&fcs->ref))
        return;
    SDL_free(fcs->keys);
    SDL_free(fcs->pages);
    SDL_free(fcs);
}

void
nsfc_charset_set_constant(FcCharSet *fcs)
{
    fcs->constant = true;
}

/* A constant set is shared; any other is copied, so that the copy can
 * change on its own. */
FcCharSet *
FcCharSetCopy(FcCharSet *src)
{
    if (!src) return NULL;
    if (src->constant) {
        SDL_AtomicIncRef(&src->ref);
        return src;
    }
    FcCharSet *copy = FcCharSetCreate();
    if (!copy || !src->n) return copy;
    copy->keys = SDL_malloc((size_t)src->n * sizeof *copy->keys);
    copy->pages = SDL_malloc((size_t)src->n * sizeof *copy->pages);
    if (!copy->keys || !copy->pages) {
        FcCharSetDestroy(copy);
        return NULL;
    }
    memcpy(copy->keys, src->keys, (size_t)src->n * sizeof *copy->keys);
    memcpy(copy->pages, src->pages, (size_t)src->n * sizeof *copy->pages);
    copy->n = copy->cap = src->n;
    return copy;
}

static int
page_position(const FcCharSet *fcs, Uint32 key, bool *found)
{
    int lo = 0, hi = fcs->n;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (fcs->keys[mid] == key) { *found = true; return mid; }
        if (fcs->keys[mid] < key) lo = mid + 1; else hi = mid;
    }
    *found = false;
    return lo;
}

/* The page for `key`, added (empty) at `at` when missing. */
static Uint32 *
page_insert(FcCharSet *fcs, int at, Uint32 key)
{
    if (fcs->n == fcs->cap) {
        int cap = fcs->cap ? fcs->cap * 2 : 8;
        Uint32 *keys = SDL_realloc(fcs->keys, (size_t)cap * sizeof *keys);
        if (!keys) return NULL;
        fcs->keys = keys;
        Uint32 (*pages)[8] = SDL_realloc(fcs->pages, (size_t)cap * sizeof *pages);
        if (!pages) return NULL;
        fcs->pages = pages;
        fcs->cap = cap;
    }
    memmove(&fcs->keys[at + 1], &fcs->keys[at],
            (size_t)(fcs->n - at) * sizeof *fcs->keys);
    memmove(&fcs->pages[at + 1], &fcs->pages[at],
            (size_t)(fcs->n - at) * sizeof *fcs->pages);
    fcs->keys[at] = key;
    memset(fcs->pages[at], 0, sizeof fcs->pages[at]);
    fcs->n++;
    return fcs->pages[at];
}

FcBool
FcCharSetAddChar(FcCharSet *fcs, FcChar32 ucs4)
{
    bool found;
    if (!fcs || fcs->constant || ucs4 > 0x10ffff) return FcFalse;
    int at = page_position(fcs, ucs4 >> 8, &found);
    Uint32 *page = found ? fcs->pages[at] : page_insert(fcs, at, ucs4 >> 8);
    if (!page) return FcFalse;
    page[(ucs4 & 0xff) >> 5] |= 1u << (ucs4 & 0x1f);
    return FcTrue;
}

FcBool
FcCharSetDelChar(FcCharSet *fcs, FcChar32 ucs4)
{
    bool found;
    if (!fcs || fcs->constant) return FcFalse;
    int at = page_position(fcs, ucs4 >> 8, &found);
    if (found)
        fcs->pages[at][(ucs4 & 0xff) >> 5] &= ~(1u << (ucs4 & 0x1f));
    return FcTrue;
}

FcBool
FcCharSetHasChar(const FcCharSet *fcs, FcChar32 ucs4)
{
    bool found;
    if (!fcs) return FcFalse;
    int at = page_position(fcs, ucs4 >> 8, &found);
    return found && (fcs->pages[at][(ucs4 & 0xff) >> 5] >> (ucs4 & 0x1f) & 1);
}

/* a |= b; *changed says whether that added anything to a. */
FcBool
FcCharSetMerge(FcCharSet *a, const FcCharSet *b, FcBool *changed)
{
    if (changed) *changed = FcFalse;
    if (!a || !b) return FcFalse;
    if (a->constant) return FcFalse;
    int at = 0;
    for (int i = 0; i < b->n; i++) {
        const Uint32 *src = b->pages[i];
        while (at < a->n && a->keys[at] < b->keys[i]) at++;
        Uint32 *dst;
        if (at < a->n && a->keys[at] == b->keys[i]) {
            dst = a->pages[at];
        } else {
            bool any = false;
            for (int w = 0; w < 8; w++) any = any || src[w];
            if (!any) continue;
            dst = page_insert(a, at, b->keys[i]);
            if (!dst) return FcFalse;
        }
        for (int w = 0; w < 8; w++) {
            if (src[w] & ~dst[w]) {
                if (changed) *changed = FcTrue;
                dst[w] |= src[w];
            }
        }
    }
    return FcTrue;
}

FcChar32
FcCharSetCount(const FcCharSet *a)
{
    FcChar32 count = 0;
    if (!a) return 0;
    for (int i = 0; i < a->n; i++) {
        for (int w = 0; w < 8; w++) {
            Uint32 bits = a->pages[i][w];
            for (; bits; bits &= bits - 1) count++;
        }
    }
    return count;
}

static bool
page_empty(const Uint32 *page)
{
    for (int w = 0; w < 8; w++)
        if (page[w]) return false;
    return true;
}

/* Equal as sets: a page with nothing in it is the same as no page. */
static bool
charset_equal(const FcCharSet *a, const FcCharSet *b)
{
    int i = 0, j = 0;
    if (a == b) return true;
    while (i < a->n || j < b->n) {
        if (i < a->n && page_empty(a->pages[i])) { i++; continue; }
        if (j < b->n && page_empty(b->pages[j])) { j++; continue; }
        if (i == a->n || j == b->n || a->keys[i] != b->keys[j] ||
            memcmp(a->pages[i], b->pages[j], sizeof a->pages[i]) != 0)
            return false;
        i++; j++;
    }
    return true;
}

/* ---- languages and string lists ---- */

bool
nsfc_str_set_add(FcStrSet *set, const char *s)
{
    if (set->n == set->cap) {
        int cap = set->cap ? set->cap * 2 : 8;
        char **strs = SDL_realloc(set->strs, (size_t)cap * sizeof *strs);
        if (!strs) return false;
        set->strs = strs;
        set->cap = cap;
    }
    char *copy = SDL_strdup(s);
    if (!copy) return false;
    set->strs[set->n++] = copy;
    return true;
}

FcStrSet *
FcLangSetGetLangs(const FcLangSet *ls)
{
    FcStrSet *set = SDL_calloc(1, sizeof *set);
    if (!set || !ls) return set;
    for (int i = 0; i < ls->langs.n; i++)
        nsfc_str_set_add(set, ls->langs.strs[i]);
    return set;
}

void
FcStrSetDestroy(FcStrSet *set)
{
    if (!set) return;
    for (int i = 0; i < set->n; i++)
        SDL_free(set->strs[i]);
    SDL_free(set->strs);
    SDL_free(set);
}

/* The list reads the set, which must outlive it. */
FcStrList *
FcStrListCreate(FcStrSet *set)
{
    FcStrList *list = SDL_calloc(1, sizeof *list);
    if (list) list->set = set;
    return list;
}

void
FcStrListFirst(FcStrList *list)
{
    if (list) list->pos = 0;
}

FcChar8 *
FcStrListNext(FcStrList *list)
{
    if (!list || !list->set || list->pos >= list->set->n) return NULL;
    return (FcChar8 *)list->set->strs[list->pos++];
}

void
FcStrListDone(FcStrList *list)
{
    SDL_free(list);
}

/* ---- matrices and weights ---- */

void
FcMatrixMultiply(FcMatrix *result, const FcMatrix *a, const FcMatrix *b)
{
    FcMatrix r;
    r.xx = a->xx * b->xx + a->xy * b->yx;
    r.xy = a->xx * b->xy + a->xy * b->yy;
    r.yx = a->yx * b->xx + a->yy * b->yx;
    r.yy = a->yx * b->xy + a->yy * b->yy;
    *result = r;
}

/* fontconfig's weights against OpenType's (usWeightClass), linear between
 * the named points. */
static const struct { double ot, fc; } weight_map[] = {
    { 0, FC_WEIGHT_THIN }, { 100, FC_WEIGHT_THIN },
    { 200, FC_WEIGHT_EXTRALIGHT }, { 300, FC_WEIGHT_LIGHT },
    { 350, FC_WEIGHT_DEMILIGHT }, { 380, FC_WEIGHT_BOOK },
    { 400, FC_WEIGHT_REGULAR }, { 500, FC_WEIGHT_MEDIUM },
    { 600, FC_WEIGHT_DEMIBOLD }, { 700, FC_WEIGHT_BOLD },
    { 800, FC_WEIGHT_EXTRABOLD }, { 900, FC_WEIGHT_BLACK },
    { 1000, FC_WEIGHT_EXTRABLACK },
};
#define N_WEIGHTS ((int)(sizeof weight_map / sizeof weight_map[0]))

double
FcWeightFromOpenTypeDouble(double ot_weight)
{
    if (ot_weight < 0) return -1;
    if (ot_weight > 1000) ot_weight = 1000;
    int i = 1;
    while (i < N_WEIGHTS - 1 && ot_weight > weight_map[i].ot) i++;
    double span = weight_map[i].ot - weight_map[i - 1].ot;
    return weight_map[i - 1].fc + (ot_weight - weight_map[i - 1].ot) *
           (weight_map[i].fc - weight_map[i - 1].fc) / span;
}

double
FcWeightToOpenTypeDouble(double fc_weight)
{
    if (fc_weight < 0) return -1;
    if (fc_weight > FC_WEIGHT_EXTRABLACK) fc_weight = FC_WEIGHT_EXTRABLACK;
    /* The first two points share a weight; start past them. */
    int i = 2;
    while (i < N_WEIGHTS - 1 && fc_weight > weight_map[i].fc) i++;
    double span = weight_map[i].fc - weight_map[i - 1].fc;
    return weight_map[i - 1].ot + (fc_weight - weight_map[i - 1].fc) *
           (weight_map[i].ot - weight_map[i - 1].ot) / span;
}
