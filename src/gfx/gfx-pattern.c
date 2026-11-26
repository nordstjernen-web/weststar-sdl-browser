/* Weststar SDL Browser — gfx: patterns, and fetching their pixels.
 * Gradients interpolate premultiplied colours, as CSS and canvas require.
 * Radial gradients follow Cairo's (pixman's) two-circle definition.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "gfx-private.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#if defined(__SSE2__)
#include <emmintrin.h>
#define GFX_SSE2 1
#endif

static gfx_pattern_t *
pattern_new(gfx_pattern_type type)
{
    gfx_pattern_t *p = calloc(1, sizeof *p);
    if (!p)
        return NULL;
    p->ref = 1;
    p->type = type;
    gfx_matrix_init_identity(&p->matrix);
    p->extend = type == GFX_PATTERN_SURFACE ? GFX_EXTEND_NONE : GFX_EXTEND_PAD;
    p->filter = GFX_FILTER_GOOD;
    gfx_vec_init(&p->stops, sizeof(gfx_stop));
    gfx_vec_init(&p->patches, sizeof(gfx_patch));
    return p;
}

/* What a failed create returns: draws nothing, never freed. */
static gfx_pattern_t *
pattern_nil(void)
{
    static gfx_pattern_t nil = {
        .ref = -1, .status = GFX_STATUS_NO_MEMORY, .type = GFX_PATTERN_SOLID,
        .matrix = { 1, 0, 0, 1, 0, 0 },
    };
    return &nil;
}

gfx_pattern_t *
gfx_pattern_create_rgba(double r, double g, double b, double a)
{
    gfx_pattern_t *p = pattern_new(GFX_PATTERN_SOLID);
    if (!p)
        return pattern_nil();
    p->r = gfx_clampd(r, 0, 1);
    p->g = gfx_clampd(g, 0, 1);
    p->b = gfx_clampd(b, 0, 1);
    p->a = gfx_clampd(a, 0, 1);
    return p;
}

gfx_pattern_t *
gfx_pattern_create_rgb(double r, double g, double b)
{
    return gfx_pattern_create_rgba(r, g, b, 1);
}

gfx_pattern_t *
gfx_pattern_create_for_surface(gfx_surface_t *s)
{
    gfx_pattern_t *p = pattern_new(GFX_PATTERN_SURFACE);
    if (!p)
        return pattern_nil();
    p->surface = gfx_surface_reference(s);
    if (!s || s->status != GFX_STATUS_SUCCESS)
        p->status = s ? s->status : GFX_STATUS_NO_MEMORY;
    return p;
}

gfx_pattern_t *
gfx_pattern_create_linear(double x0, double y0, double x1, double y1)
{
    gfx_pattern_t *p = pattern_new(GFX_PATTERN_LINEAR);
    if (!p)
        return pattern_nil();
    p->p[0] = x0; p->p[1] = y0; p->p[2] = x1; p->p[3] = y1;
    return p;
}

gfx_pattern_t *
gfx_pattern_create_radial(double cx0, double cy0, double r0, double cx1,
                          double cy1, double r1)
{
    gfx_pattern_t *p = pattern_new(GFX_PATTERN_RADIAL);
    if (!p)
        return pattern_nil();
    p->p[0] = cx0; p->p[1] = cy0; p->p[2] = fabs(r0);
    p->p[3] = cx1; p->p[4] = cy1; p->p[5] = fabs(r1);
    return p;
}

gfx_pattern_t *
gfx_pattern_create_mesh(void)
{
    gfx_pattern_t *p = pattern_new(GFX_PATTERN_MESH);
    if (!p)
        return pattern_nil();
    p->extend = GFX_EXTEND_NONE;
    return p;
}

gfx_pattern_t *
gfx_pattern_reference(gfx_pattern_t *p)
{
    if (p && p->ref > 0)
        p->ref++;
    return p;
}

void
gfx_pattern_destroy(gfx_pattern_t *p)
{
    if (!p || p->ref <= 0)
        return;
    if (--p->ref > 0)
        return;
    gfx_surface_destroy(p->surface);
    gfx_vec_free(&p->stops);
    gfx_vec_free(&p->patches);
    free(p->current);
    free(p);
}

gfx_status_t
gfx_pattern_status(const gfx_pattern_t *p)
{
    return p ? p->status : GFX_STATUS_NO_MEMORY;
}

void
gfx_pattern_add_color_stop_rgba(gfx_pattern_t *p, double offset, double r,
                                double g, double b, double a)
{
    if (!p || p->ref <= 0 ||
        (p->type != GFX_PATTERN_LINEAR && p->type != GFX_PATTERN_RADIAL))
        return;
    offset = gfx_clampd(offset, 0, 1);
    /* Insert after any stop at the same offset: equal offsets keep the
     * order they were added in, which makes a hard edge. */
    size_t at = p->stops.len;
    while (at > 0 && GFX_VEC_AT(&p->stops, gfx_stop, at - 1).offset > offset)
        at--;
    if (!gfx_vec_push(&p->stops))
        return;
    gfx_stop *s = p->stops.data;
    memmove(&s[at + 1], &s[at], (p->stops.len - 1 - at) * sizeof *s);
    s[at] = (gfx_stop){ offset, gfx_clampd(r, 0, 1), gfx_clampd(g, 0, 1),
                        gfx_clampd(b, 0, 1), gfx_clampd(a, 0, 1) };
}

void
gfx_pattern_add_color_stop_rgb(gfx_pattern_t *p, double offset, double r,
                               double g, double b)
{
    gfx_pattern_add_color_stop_rgba(p, offset, r, g, b, 1);
}

void
gfx_pattern_set_matrix(gfx_pattern_t *p, const gfx_matrix_t *m)
{
    if (!p || p->ref <= 0)
        return;
    gfx_matrix_t inv = *m;
    if (gfx_matrix_invert(&inv) != GFX_STATUS_SUCCESS) {
        p->status = GFX_STATUS_INVALID_MATRIX;
        return;
    }
    p->matrix = *m;
}

void
gfx_pattern_get_matrix(const gfx_pattern_t *p, gfx_matrix_t *m)
{
    if (p)
        *m = p->matrix;
    else
        gfx_matrix_init_identity(m);
}

void
gfx_pattern_set_extend(gfx_pattern_t *p, gfx_extend_t extend)
{
    if (p && p->ref > 0)
        p->extend = extend;
}

gfx_extend_t
gfx_pattern_get_extend(const gfx_pattern_t *p)
{
    return p ? p->extend : GFX_EXTEND_NONE;
}

void
gfx_pattern_set_filter(gfx_pattern_t *p, gfx_filter_t filter)
{
    if (p && p->ref > 0)
        p->filter = filter;
}

gfx_filter_t
gfx_pattern_get_filter(const gfx_pattern_t *p)
{
    return p ? p->filter : GFX_FILTER_GOOD;
}

gfx_bool_t
gfx_pattern_get_rgba(const gfx_pattern_t *p, double *r, double *g, double *b,
                     double *a)
{
    if (!p || p->type != GFX_PATTERN_SOLID)
        return 0;
    if (r) *r = p->r;
    if (g) *g = p->g;
    if (b) *b = p->b;
    if (a) *a = p->a;
    return 1;
}

gfx_surface_t *
gfx_pattern_get_surface(const gfx_pattern_t *p)
{
    return p && p->type == GFX_PATTERN_SURFACE ? p->surface : NULL;
}

/* ---- mesh patches ------------------------------------------------------ */

void
gfx_mesh_pattern_begin_patch(gfx_pattern_t *p)
{
    if (!p || p->ref <= 0 || p->type != GFX_PATTERN_MESH || p->current)
        return;
    p->current = calloc(1, sizeof *p->current);
    if (p->current)
        p->current->nsides = -1;    /* waiting for move_to */
}

void
gfx_mesh_pattern_move_to(gfx_pattern_t *p, double x, double y)
{
    gfx_patch *pt = p ? p->current : NULL;
    if (!pt || pt->nsides > 0)
        return;
    pt->x[0] = x;
    pt->y[0] = y;
    pt->nsides = 0;
}

void
gfx_mesh_pattern_line_to(gfx_pattern_t *p, double x, double y)
{
    gfx_patch *pt = p ? p->current : NULL;
    if (!pt)
        return;
    if (pt->nsides < 0) {
        gfx_mesh_pattern_move_to(p, x, y);
        return;
    }
    if (pt->nsides >= 4)
        return;
    pt->nsides++;
    if (pt->nsides < 4) {
        pt->x[pt->nsides] = x;
        pt->y[pt->nsides] = y;
    }
}

void
gfx_mesh_pattern_curve_to(gfx_pattern_t *p, double x1, double y1, double x2,
                          double y2, double x3, double y3)
{
    (void)x1; (void)y1; (void)x2; (void)y2;
    gfx_mesh_pattern_line_to(p, x3, y3);
}

void
gfx_mesh_pattern_set_corner_color_rgba(gfx_pattern_t *p, unsigned int corner,
                                       double r, double g, double b, double a)
{
    gfx_patch *pt = p ? p->current : NULL;
    if (!pt || corner > 3)
        return;
    a = gfx_clampd(a, 0, 1);
    pt->c[corner][0] = gfx_clampd(r, 0, 1) * a;
    pt->c[corner][1] = gfx_clampd(g, 0, 1) * a;
    pt->c[corner][2] = gfx_clampd(b, 0, 1) * a;
    pt->c[corner][3] = a;
    pt->color_set[corner] = 1;
}

void
gfx_mesh_pattern_set_corner_color_rgb(gfx_pattern_t *p, unsigned int corner,
                                      double r, double g, double b)
{
    gfx_mesh_pattern_set_corner_color_rgba(p, corner, r, g, b, 1);
}

void
gfx_mesh_pattern_end_patch(gfx_pattern_t *p)
{
    gfx_patch *pt = p ? p->current : NULL;
    if (!pt)
        return;
    p->current = NULL;
    if (pt->nsides < 0) {
        free(pt);
        return;
    }
    /* Missing sides close straight back to the start. */
    for (int k = pt->nsides + 1; k < 4; k++) {
        pt->x[k] = pt->x[pt->nsides < 3 ? pt->nsides : 3];
        pt->y[k] = pt->y[pt->nsides < 3 ? pt->nsides : 3];
    }
    gfx_patch *slot = gfx_vec_push(&p->patches);
    if (slot)
        *slot = *pt;
    free(pt);
}

/* ---- sources ----------------------------------------------------------- */

void
gfx_source_init(gfx_source *src, const gfx_pattern_t *pat,
                const gfx_matrix_t *to_pattern, double alpha)
{
    memset(src, 0, sizeof *src);
    src->pat = pat;
    src->to_pattern = *to_pattern;
    src->alpha = gfx_clampd(alpha, 0, 1);
    if (pat->status != GFX_STATUS_SUCCESS) {
        src->solid = 0;
        return;
    }
    if (pat->type == GFX_PATTERN_SOLID) {
        double a = pat->a * src->alpha;
        src->solid = gfx_pack(pat->r * a, pat->g * a, pat->b * a, a);
        src->opaque = (src->solid >> 24) == 255;
        return;
    }
    if (pat->type == GFX_PATTERN_SURFACE && pat->surface) {
        const gfx_surface_t *s = pat->surface;
        gfx_matrix_t dev;
        gfx_matrix_init(&dev, s->dev_sx, 0, 0, s->dev_sy, s->dev_x, s->dev_y);
        gfx_matrix_multiply(&src->to_texel, to_pattern, &dev);
        const gfx_matrix_t *m = &src->to_texel;
        if (m->xx == 1 && m->yy == 1 && m->xy == 0 && m->yx == 0 &&
            m->x0 == floor(m->x0) && m->y0 == floor(m->y0) &&
            fabs(m->x0) < 1e9 && fabs(m->y0) < 1e9) {
            src->texel_int = 1;
            src->tx = (int)m->x0;
            src->ty = (int)m->y0;
        }
    }
}

int
gfx_source_extent(const gfx_source *src, int *x0, int *y0, int *x1, int *y1)
{
    const gfx_pattern_t *pat = src->pat;
    if (pat->status != GFX_STATUS_SUCCESS) {
        *x0 = *y0 = *x1 = *y1 = 0;
        return 1;
    }
    if (pat->type != GFX_PATTERN_SURFACE || pat->extend != GFX_EXTEND_NONE ||
        !pat->surface)
        return 0;
    gfx_matrix_t inv = src->to_texel;
    if (gfx_matrix_invert(&inv) != GFX_STATUS_SUCCESS)
        return 0;
    double xs[4] = { 0, pat->surface->width, pat->surface->width, 0 };
    double ys[4] = { 0, 0, pat->surface->height, pat->surface->height };
    double minx = INFINITY, miny = INFINITY, maxx = -INFINITY, maxy = -INFINITY;
    for (int i = 0; i < 4; i++) {
        gfx_matrix_transform_point(&inv, &xs[i], &ys[i]);
        minx = fmin(minx, xs[i]);
        miny = fmin(miny, ys[i]);
        maxx = fmax(maxx, xs[i]);
        maxy = fmax(maxy, ys[i]);
    }
    /* A pixel of margin for filtering. */
    if (!(fabs(minx) < 1e8 && fabs(maxx) < 1e8 && fabs(miny) < 1e8 &&
          fabs(maxy) < 1e8))
        return 0;
    *x0 = (int)floor(minx) - 1;
    *y0 = (int)floor(miny) - 1;
    *x1 = (int)ceil(maxx) + 1;
    *y1 = (int)ceil(maxy) + 1;
    return 1;
}

static inline uint32_t
texel(const gfx_surface_t *s, int x, int y)
{
    const unsigned char *row = s->data + (size_t)y * (size_t)s->stride;
    switch (s->format) {
    case GFX_FORMAT_A8:
        return (uint32_t)row[x] << 24;
    case GFX_FORMAT_RGB24: {
        uint32_t p;
        memcpy(&p, row + (size_t)x * 4, 4);
        return p | 0xff000000u;
    }
    default: {
        uint32_t p;
        memcpy(&p, row + (size_t)x * 4, 4);
        return p;
    }
    }
}

/* Maps a texel index into [0, n) by the extend mode; -1 when outside. */
static inline int
wrap(int i, int n, gfx_extend_t extend)
{
    if (i >= 0 && i < n)
        return i;
    switch (extend) {
    case GFX_EXTEND_NONE:
        return -1;
    case GFX_EXTEND_PAD:
        return i < 0 ? 0 : n - 1;
    case GFX_EXTEND_REPEAT: {
        int r = i % n;
        return r < 0 ? r + n : r;
    }
    case GFX_EXTEND_REFLECT: {
        int r = i % (2 * n);
        if (r < 0)
            r += 2 * n;
        return r < n ? r : 2 * n - 1 - r;
    }
    }
    return -1;
}

static inline uint32_t
texel_at(const gfx_surface_t *s, int x, int y, gfx_extend_t extend)
{
    x = wrap(x, s->width, extend);
    y = wrap(y, s->height, extend);
    if (x < 0 || y < 0)
        return 0;
    return texel(s, x, y);
}

/* Bilinear blend of four premultiplied pixels with weights in 1/256. */
static inline uint32_t
blend4(uint32_t p00, uint32_t p10, uint32_t p01, uint32_t p11, unsigned fx,
       unsigned fy)
{
    unsigned w00 = (256 - fx) * (256 - fy), w10 = fx * (256 - fy);
    unsigned w01 = (256 - fx) * fy, w11 = fx * fy;
    uint32_t out = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        unsigned c = ((p00 >> sh) & 0xff) * w00 + ((p10 >> sh) & 0xff) * w10 +
                     ((p01 >> sh) & 0xff) * w01 + ((p11 >> sh) & 0xff) * w11;
        out |= (uint32_t)((c + 32768) >> 16) << sh;
    }
    return out;
}

static uint32_t
sample_bilinear(const gfx_surface_t *s, double u, double v, gfx_extend_t ext)
{
    u -= 0.5;
    v -= 0.5;
    double fu = floor(u), fv = floor(v);
    if (fabs(fu) > 1e9 || fabs(fv) > 1e9)
        return 0;
    int x = (int)fu, y = (int)fv;
    unsigned fx = (unsigned)((u - fu) * 256 + 0.5);
    unsigned fy = (unsigned)((v - fv) * 256 + 0.5);
    if (fx >= 256) { fx = 0; x++; }
    if (fy >= 256) { fy = 0; y++; }
    return blend4(texel_at(s, x, y, ext), texel_at(s, x + 1, y, ext),
                  texel_at(s, x, y + 1, ext), texel_at(s, x + 1, y + 1, ext),
                  fx, fy);
}

static void
fetch_surface(const gfx_source *src, int y, int x, int len, uint32_t *out)
{
    const gfx_pattern_t *pat = src->pat;
    const gfx_surface_t *s = pat->surface;
    gfx_extend_t ext = pat->extend;
    if (!s || !s->data || s->width <= 0 || s->height <= 0) {
        memset(out, 0, (size_t)len * sizeof *out);
        return;
    }
    if (src->texel_int) {
        int ty = y + src->ty;
        int tx = x + src->tx;
        if (s->format == GFX_FORMAT_ARGB32 && ty >= 0 && ty < s->height &&
            tx >= 0 && tx + len <= s->width) {
            memcpy(out, s->data + (size_t)ty * (size_t)s->stride + (size_t)tx * 4,
                   (size_t)len * 4);
            return;
        }
        int wy = wrap(ty, s->height, ext);
        for (int i = 0; i < len; i++) {
            int wx = wrap(x + i + src->tx, s->width, ext);
            out[i] = (wx < 0 || wy < 0) ? 0 : texel(s, wx, wy);
        }
        return;
    }
    const gfx_matrix_t *m = &src->to_texel;
    int nearest = pat->filter == GFX_FILTER_NEAREST ||
                  pat->filter == GFX_FILTER_FAST;
    /* Texels per pixel along each pixel axis: when shrinking a lot, GOOD
     * and BEST average several taps across the pixel's footprint. */
    double sx = hypot(m->xx, m->yx), sy = hypot(m->xy, m->yy);
    int nx = 1, ny = 1;
    if (!nearest && (pat->filter == GFX_FILTER_GOOD ||
                     pat->filter == GFX_FILTER_BEST)) {
        if (sx > 1.5) nx = (int)fmin(8, ceil(sx));
        if (sy > 1.5) ny = (int)fmin(8, ceil(sy));
    }
    /* Texture coordinates are affine along the row: step them. */
    double u0 = x + 0.5, v0 = y + 0.5;
    gfx_matrix_transform_point(m, &u0, &v0);
    double du = m->xx, dv = m->yx;
    if (nx == 1 && ny == 1 && !nearest && s->format == GFX_FORMAT_ARGB32 &&
        fabs(u0) < 1e6 && fabs(v0) < 1e6 && fabs(du) < 1e4 && fabs(dv) < 1e4) {
        /* Bilinear in 16.16 fixed point; texels inside the image are read
         * directly, the rest (edges, extend modes) per pixel. */
        int64_t U = (int64_t)llround((u0 - 0.5) * 65536.0);
        int64_t V = (int64_t)llround((v0 - 0.5) * 65536.0);
        int64_t DU = (int64_t)llround(du * 65536.0);
        int64_t DV = (int64_t)llround(dv * 65536.0);
        int w1 = s->width - 1, h1 = s->height - 1;
        for (int i = 0; i < len; i++, U += DU, V += DV) {
            int64_t tx = U >> 16, ty = V >> 16;
            if (tx >= 0 && ty >= 0 && tx < w1 && ty < h1) {
                unsigned fx = (unsigned)((U >> 8) & 0xff);
                unsigned fy = (unsigned)((V >> 8) & 0xff);
                const uint32_t *r0 = (const uint32_t *)(const void *)
                    (s->data + (size_t)ty * (size_t)s->stride) + tx;
                const uint32_t *r1 = (const uint32_t *)(const void *)
                    ((const unsigned char *)r0 + s->stride);
#ifdef GFX_SSE2
                const __m128i z = _mm_setzero_si128();
                __m128i top = _mm_unpacklo_epi8(
                    _mm_unpacklo_epi32(_mm_cvtsi32_si128((int)r0[0]),
                                       _mm_cvtsi32_si128((int)r0[1])), z);
                __m128i bot = _mm_unpacklo_epi8(
                    _mm_unpacklo_epi32(_mm_cvtsi32_si128((int)r1[0]),
                                       _mm_cvtsi32_si128((int)r1[1])), z);
                __m128i wx = _mm_set_epi16((short)fx, (short)fx, (short)fx,
                                           (short)fx, (short)(256 - fx),
                                           (short)(256 - fx), (short)(256 - fx),
                                           (short)(256 - fx));
                top = _mm_mullo_epi16(top, wx);
                bot = _mm_mullo_epi16(bot, wx);
                top = _mm_srli_epi16(_mm_add_epi16(_mm_add_epi16(top, _mm_srli_si128(top, 8)),
                                                   _mm_set1_epi16(128)), 8);
                bot = _mm_srli_epi16(_mm_add_epi16(_mm_add_epi16(bot, _mm_srli_si128(bot, 8)),
                                                   _mm_set1_epi16(128)), 8);
                __m128i v = _mm_add_epi16(
                    _mm_add_epi16(_mm_mullo_epi16(top, _mm_set1_epi16((short)(256 - fy))),
                                  _mm_mullo_epi16(bot, _mm_set1_epi16((short)fy))),
                    _mm_set1_epi16(128));
                v = _mm_srli_epi16(v, 8);
                out[i] = (uint32_t)_mm_cvtsi128_si32(_mm_packus_epi16(v, z));
#else
                out[i] = blend4(r0[0], r0[1], r1[0], r1[1], fx, fy);
#endif
            } else {
                out[i] = sample_bilinear(s, u0 + du * i, v0 + dv * i, ext);
            }
        }
        return;
    }
    if (nx == 1 && ny == 1) {
        int argb = s->format == GFX_FORMAT_ARGB32;
        for (int i = 0; i < len; i++) {
            double u = u0 + du * i, v = v0 + dv * i;
            if (nearest) {
                double fu = floor(u), fv = floor(v);
                out[i] = (fabs(fu) > 1e9 || fabs(fv) > 1e9) ? 0 :
                         texel_at(s, (int)fu, (int)fv, ext);
                continue;
            }
            double bu = u - 0.5, bv = v - 0.5;
            double fu = floor(bu), fv = floor(bv);
            if (argb && fu >= 0 && fv >= 0 && fu < s->width - 1 &&
                fv < s->height - 1) {
                /* All four texels inside: read them directly. */
                int tx = (int)fu, ty = (int)fv;
                unsigned fx = (unsigned)((bu - fu) * 256 + 0.5);
                unsigned fy = (unsigned)((bv - fv) * 256 + 0.5);
                if (fx > 256) fx = 256;
                if (fy > 256) fy = 256;
                const uint32_t *r0 = (const uint32_t *)(const void *)
                    (s->data + (size_t)ty * (size_t)s->stride) + tx;
                const uint32_t *r1 = (const uint32_t *)(const void *)
                    ((const unsigned char *)r0 + s->stride);
                out[i] = blend4(r0[0], r0[1], r1[0], r1[1], fx, fy);
            } else {
                out[i] = sample_bilinear(s, u, v, ext);
            }
        }
        return;
    }
    /* Shrinking: the average of nx x ny point samples spread over the
     * pixel's footprint. */
    for (int i = 0; i < len; i++) {
        unsigned acc[4] = { 0, 0, 0, 0 };
        for (int j = 0; j < ny; j++) {
            for (int k = 0; k < nx; k++) {
                double ox = (k + 0.5) / nx - 0.5, oy = (j + 0.5) / ny - 0.5;
                double u = u0 + du * i + m->xx * ox + m->xy * oy;
                double v = v0 + dv * i + m->yx * ox + m->yy * oy;
                double fu = floor(u), fv = floor(v);
                uint32_t t = (fabs(fu) > 1e9 || fabs(fv) > 1e9) ? 0 :
                             texel_at(s, (int)fu, (int)fv, ext);
                acc[0] += t & 0xff;
                acc[1] += (t >> 8) & 0xff;
                acc[2] += (t >> 16) & 0xff;
                acc[3] += t >> 24;
            }
        }
        unsigned taps = (unsigned)(nx * ny);
        out[i] = ((acc[3] + taps / 2) / taps) << 24 |
                 ((acc[2] + taps / 2) / taps) << 16 |
                 ((acc[1] + taps / 2) / taps) << 8 |
                 ((acc[0] + taps / 2) / taps);
    }
}

/* ---- gradients --------------------------------------------------------- */

static uint32_t
gradient_color(const gfx_pattern_t *pat, double t)
{
    size_t n = pat->stops.len;
    if (n == 0 || !isfinite(t))
        return 0;
    switch (pat->extend) {
    case GFX_EXTEND_NONE:
        if (t < 0 || t > 1)
            return 0;
        break;
    case GFX_EXTEND_PAD:
        t = gfx_clampd(t, 0, 1);
        break;
    case GFX_EXTEND_REPEAT:
        t -= floor(t);
        break;
    case GFX_EXTEND_REFLECT:
        t = fmod(fabs(t), 2.0);
        if (t > 1)
            t = 2 - t;
        break;
    }
    const gfx_stop *s = pat->stops.data;
    const gfx_stop *a, *b;
    if (t <= s[0].offset) {
        a = b = &s[0];
    } else if (t >= s[n - 1].offset) {
        a = b = &s[n - 1];
    } else {
        size_t lo = 0, hi = n - 1;   /* s[lo].offset < t <= s[hi].offset */
        while (hi - lo > 1) {
            size_t mid = (lo + hi) / 2;
            if (s[mid].offset < t)
                lo = mid;
            else
                hi = mid;
        }
        a = &s[lo];
        b = &s[hi];
    }
    double f = b->offset > a->offset ? (t - a->offset) / (b->offset - a->offset) : 0;
    double aa = a->a + (b->a - a->a) * f;
    double r = a->r * a->a + (b->r * b->a - a->r * a->a) * f;
    double g = a->g * a->a + (b->g * b->a - a->g * a->a) * f;
    double bb = a->b * a->a + (b->b * b->a - a->b * a->a) * f;
    return gfx_pack(r, g, bb, aa);
}

static void
fetch_linear(const gfx_source *src, int y, int x, int len, uint32_t *out)
{
    const gfx_pattern_t *pat = src->pat;
    double x0 = pat->p[0], y0 = pat->p[1];
    double dx = pat->p[2] - x0, dy = pat->p[3] - y0;
    double den = dx * dx + dy * dy;
    if (den == 0) {
        /* A degenerate line: the last stop, if the gradient extends. */
        uint32_t c = pat->extend == GFX_EXTEND_NONE ? 0 : gradient_color(pat, 1);
        for (int i = 0; i < len; i++)
            out[i] = c;
        return;
    }
    const gfx_matrix_t *m = &src->to_pattern;
    double u = x + 0.5, v = y + 0.5;
    gfx_matrix_transform_point(m, &u, &v);
    /* t is affine along the row. */
    double t = ((u - x0) * dx + (v - y0) * dy) / den;
    double dt = (m->xx * dx + m->yx * dy) / den;
    for (int i = 0; i < len; i++)
        out[i] = gradient_color(pat, t + dt * i);
}

static void
fetch_radial(const gfx_source *src, int y, int x, int len, uint32_t *out)
{
    const gfx_pattern_t *pat = src->pat;
    double cx0 = pat->p[0], cy0 = pat->p[1], r0 = pat->p[2];
    double cdx = pat->p[3] - cx0, cdy = pat->p[4] - cy0, dr = pat->p[5] - r0;
    double a = cdx * cdx + cdy * cdy - dr * dr;
    double mindr = -r0;
    int none = pat->extend == GFX_EXTEND_NONE;
    for (int i = 0; i < len; i++) {
        double u = x + i + 0.5, v = y + 0.5;
        gfx_matrix_transform_point(&src->to_pattern, &u, &v);
        double pdx = u - cx0, pdy = v - cy0;
        double b = pdx * cdx + pdy * cdy + r0 * dr;
        double c = pdx * pdx + pdy * pdy - r0 * r0;
        uint32_t col = 0;
        if (fabs(a) < 1e-12) {
            if (b != 0) {
                double t = 0.5 * c / b;
                if (none ? (t >= 0 && t <= 1) : (t * dr >= mindr))
                    col = gradient_color(pat, t);
            }
        } else {
            double disc = b * b - a * c;
            if (disc >= 0) {
                double sq = sqrt(disc);
                double t0 = (b + sq) / a, t1 = (b - sq) / a;
                if (none) {
                    if (t0 >= 0 && t0 <= 1)
                        col = gradient_color(pat, t0);
                    else if (t1 >= 0 && t1 <= 1)
                        col = gradient_color(pat, t1);
                } else {
                    if (t0 * dr >= mindr)
                        col = gradient_color(pat, t0);
                    else if (t1 * dr >= mindr)
                        col = gradient_color(pat, t1);
                }
            }
        }
        out[i] = col;
    }
}

/* ---- mesh -------------------------------------------------------------- */

static double
cross2(double ax, double ay, double bx, double by)
{
    return ax * by - ay * bx;
}

/* (u, v) of p in the bilinear patch, or FALSE when p lies outside. */
static int
inv_bilinear(const gfx_patch *pt, double px, double py, double *u, double *v)
{
    double ax = pt->x[0], ay = pt->y[0];
    double ex = pt->x[1] - ax, ey = pt->y[1] - ay;
    double fx = pt->x[3] - ax, fy = pt->y[3] - ay;
    double gx = ax - pt->x[1] + pt->x[2] - pt->x[3];
    double gy = ay - pt->y[1] + pt->y[2] - pt->y[3];
    double hx = px - ax, hy = py - ay;
    double k2 = cross2(gx, gy, fx, fy);
    double k1 = cross2(ex, ey, fx, fy) + cross2(hx, hy, gx, gy);
    double k0 = cross2(hx, hy, ex, ey);
    const double eps = 1e-9;
    double vs[2];
    int nv = 0;
    if (fabs(k2) < eps) {
        if (fabs(k1) < eps) {
            if (fabs(hx) < eps && fabs(hy) < eps) {
                *u = *v = 0;
                return 1;
            }
            return 0;
        }
        vs[nv++] = -k0 / k1;
    } else {
        double w = k1 * k1 - 4 * k0 * k2;
        if (w < 0)
            return 0;
        w = sqrt(w);
        vs[nv++] = (-k1 - w) / (2 * k2);
        vs[nv++] = (-k1 + w) / (2 * k2);
    }
    for (int i = 0; i < nv; i++) {
        double vv = vs[i];
        if (vv < -eps || vv > 1 + eps)
            continue;
        double denx = ex + gx * vv, deny = ey + gy * vv;
        double uu;
        if (fabs(denx) >= fabs(deny)) {
            if (fabs(denx) < eps)
                uu = 0;
            else
                uu = (hx - fx * vv) / denx;
        } else {
            uu = (hy - fy * vv) / deny;
        }
        if (uu < -eps || uu > 1 + eps)
            continue;
        *u = gfx_clampd(uu, 0, 1);
        *v = gfx_clampd(vv, 0, 1);
        return 1;
    }
    return 0;
}

static void
fetch_mesh(const gfx_source *src, int y, int x, int len, uint32_t *out)
{
    const gfx_pattern_t *pat = src->pat;
    size_t n = pat->patches.len;
    const gfx_patch *patches = pat->patches.data;
    for (int i = 0; i < len; i++) {
        double px = x + i + 0.5, py = y + 0.5;
        gfx_matrix_transform_point(&src->to_pattern, &px, &py);
        uint32_t col = 0;
        /* Later patches lie on top. */
        for (size_t k = n; k-- > 0;) {
            const gfx_patch *pt = &patches[k];
            double u, v;
            if (!inv_bilinear(pt, px, py, &u, &v))
                continue;
            double w0 = (1 - u) * (1 - v), w1 = u * (1 - v), w2 = u * v,
                   w3 = (1 - u) * v;
            double c[4];
            for (int j = 0; j < 4; j++)
                c[j] = w0 * pt->c[0][j] + w1 * pt->c[1][j] + w2 * pt->c[2][j] +
                       w3 * pt->c[3][j];
            col = gfx_pack(c[0], c[1], c[2], c[3]);
            break;
        }
        out[i] = col;
    }
}

/* ---- dispatch ---------------------------------------------------------- */

static void
apply_alpha(uint32_t *out, int len, double alpha)
{
    unsigned a = (unsigned)(alpha * 255 + 0.5);
    for (int i = 0; i < len; i++) {
        uint32_t p = out[i];
        out[i] = (uint32_t)gfx_mul8(p >> 24, a) << 24 |
                 (uint32_t)gfx_mul8((p >> 16) & 0xff, a) << 16 |
                 (uint32_t)gfx_mul8((p >> 8) & 0xff, a) << 8 |
                 gfx_mul8(p & 0xff, a);
    }
}

void
gfx_source_fetch(const gfx_source *src, int y, int x, int len, uint32_t *out)
{
    const gfx_pattern_t *pat = src->pat;
    if (pat->status != GFX_STATUS_SUCCESS) {
        memset(out, 0, (size_t)len * sizeof *out);
        return;
    }
    switch (pat->type) {
    case GFX_PATTERN_SOLID:
        for (int i = 0; i < len; i++)
            out[i] = src->solid;
        return;     /* alpha already applied */
    case GFX_PATTERN_SURFACE:
        fetch_surface(src, y, x, len, out);
        break;
    case GFX_PATTERN_LINEAR:
        fetch_linear(src, y, x, len, out);
        break;
    case GFX_PATTERN_RADIAL:
        fetch_radial(src, y, x, len, out);
        break;
    case GFX_PATTERN_MESH:
        fetch_mesh(src, y, x, len, out);
        break;
    }
    if (src->alpha < 1)
        apply_alpha(out, len, src->alpha);
}
