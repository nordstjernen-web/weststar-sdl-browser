/* Weststar SDL Browser — gfx: compositing operators.
 * The Porter-Duff operators and the separable and non-separable blend
 * modes of Compositing and Blending Level 1, on premultiplied colour, with
 * Cairo's rules for coverage: for operators a transparent source leaves
 * alone, dst' = lerp(dst, op(src, dst), coverage); for the others (IN, OUT,
 * DEST_IN, DEST_ATOP), dst' = op(src x coverage, dst).
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "gfx-private.h"

#include <math.h>
#include <string.h>

int
gfx_operator_bounded(gfx_operator_t op)
{
    switch (op) {
    case GFX_OPERATOR_IN:
    case GFX_OPERATOR_OUT:
    case GFX_OPERATOR_DEST_IN:
    case GFX_OPERATOR_DEST_ATOP:
        return 0;
    default:
        return 1;
    }
}

typedef struct {
    double r, g, b, a;
} rgba;

static inline rgba
unpack(uint32_t p)
{
    return (rgba){ ((p >> 16) & 0xff) / 255.0, ((p >> 8) & 0xff) / 255.0,
                   (p & 0xff) / 255.0, (p >> 24) / 255.0 };
}

/* ---- blend functions on straight colour -------------------------------- */

static double
blend_sep(gfx_operator_t op, double cs, double cb)
{
    switch (op) {
    case GFX_OPERATOR_MULTIPLY:
        return cs * cb;
    case GFX_OPERATOR_SCREEN:
        return cs + cb - cs * cb;
    case GFX_OPERATOR_OVERLAY:
        return cb <= 0.5 ? 2 * cs * cb : 1 - 2 * (1 - cs) * (1 - cb);
    case GFX_OPERATOR_DARKEN:
        return fmin(cs, cb);
    case GFX_OPERATOR_LIGHTEN:
        return fmax(cs, cb);
    case GFX_OPERATOR_COLOR_DODGE:
        if (cb == 0) return 0;
        if (cs >= 1) return 1;
        return fmin(1, cb / (1 - cs));
    case GFX_OPERATOR_COLOR_BURN:
        if (cb >= 1) return 1;
        if (cs <= 0) return 0;
        return 1 - fmin(1, (1 - cb) / cs);
    case GFX_OPERATOR_HARD_LIGHT:
        return cs <= 0.5 ? 2 * cs * cb : 1 - 2 * (1 - cs) * (1 - cb);
    case GFX_OPERATOR_SOFT_LIGHT: {
        if (cs <= 0.5)
            return cb - (1 - 2 * cs) * cb * (1 - cb);
        double d = cb <= 0.25 ? ((16 * cb - 12) * cb + 4) * cb : sqrt(cb);
        return cb + (2 * cs - 1) * (d - cb);
    }
    case GFX_OPERATOR_DIFFERENCE:
        return fabs(cs - cb);
    case GFX_OPERATOR_EXCLUSION:
        return cs + cb - 2 * cs * cb;
    default:
        return cs;
    }
}

static double
lum(rgba c)
{
    return 0.3 * c.r + 0.59 * c.g + 0.11 * c.b;
}

static rgba
clip_color(rgba c)
{
    double l = lum(c);
    double n = fmin(c.r, fmin(c.g, c.b)), x = fmax(c.r, fmax(c.g, c.b));
    if (n < 0) {
        c.r = l + (c.r - l) * l / (l - n);
        c.g = l + (c.g - l) * l / (l - n);
        c.b = l + (c.b - l) * l / (l - n);
    }
    if (x > 1) {
        c.r = l + (c.r - l) * (1 - l) / (x - l);
        c.g = l + (c.g - l) * (1 - l) / (x - l);
        c.b = l + (c.b - l) * (1 - l) / (x - l);
    }
    return c;
}

static rgba
set_lum(rgba c, double l)
{
    double d = l - lum(c);
    c.r += d;
    c.g += d;
    c.b += d;
    return clip_color(c);
}

static double
sat(rgba c)
{
    return fmax(c.r, fmax(c.g, c.b)) - fmin(c.r, fmin(c.g, c.b));
}

static rgba
set_sat(rgba c, double s)
{
    double *v[3] = { &c.r, &c.g, &c.b };
    /* Sort pointers: min, mid, max. */
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2 - i; j++)
            if (*v[j] > *v[j + 1]) {
                double *t = v[j];
                v[j] = v[j + 1];
                v[j + 1] = t;
            }
    if (*v[2] > *v[0]) {
        *v[1] = (*v[1] - *v[0]) * s / (*v[2] - *v[0]);
        *v[2] = s;
    } else {
        *v[1] = *v[2] = 0;
    }
    *v[0] = 0;
    return c;
}

static rgba
blend_nonsep(gfx_operator_t op, rgba cs, rgba cb)
{
    switch (op) {
    case GFX_OPERATOR_HSL_HUE:
        return set_lum(set_sat(cs, sat(cb)), lum(cb));
    case GFX_OPERATOR_HSL_SATURATION:
        return set_lum(set_sat(cb, sat(cs)), lum(cb));
    case GFX_OPERATOR_HSL_COLOR:
        return set_lum(cs, lum(cb));
    default: /* LUMINOSITY */
        return set_lum(cb, lum(cs));
    }
}

/* op(s, d) on premultiplied colour. */
static rgba
apply_op(gfx_operator_t op, rgba s, rgba d)
{
    double fa, fb;   /* Porter-Duff factors */
    switch (op) {
    case GFX_OPERATOR_CLEAR:     return (rgba){ 0, 0, 0, 0 };
    case GFX_OPERATOR_SOURCE:    return s;
    case GFX_OPERATOR_DEST:      return d;
    case GFX_OPERATOR_OVER:      fa = 1;       fb = 1 - s.a; break;
    case GFX_OPERATOR_IN:        fa = d.a;     fb = 0;       break;
    case GFX_OPERATOR_OUT:       fa = 1 - d.a; fb = 0;       break;
    case GFX_OPERATOR_ATOP:      fa = d.a;     fb = 1 - s.a; break;
    case GFX_OPERATOR_DEST_OVER: fa = 1 - d.a; fb = 1;       break;
    case GFX_OPERATOR_DEST_IN:   fa = 0;       fb = s.a;     break;
    case GFX_OPERATOR_DEST_OUT:  fa = 0;       fb = 1 - s.a; break;
    case GFX_OPERATOR_DEST_ATOP: fa = 1 - d.a; fb = s.a;     break;
    case GFX_OPERATOR_XOR:       fa = 1 - d.a; fb = 1 - s.a; break;
    case GFX_OPERATOR_ADD:       fa = 1;       fb = 1;       break;
    case GFX_OPERATOR_SATURATE:
        fa = s.a > 0 ? fmin(1, (1 - d.a) / s.a) : 1;
        fb = 1;
        break;
    default: {
        /* Blend modes: co = cs(1 - ab) + cb(1 - as) + as ab B(Cs, Cb). */
        rgba cs = s, cb = d;
        if (s.a > 0) { cs.r /= s.a; cs.g /= s.a; cs.b /= s.a; }
        if (d.a > 0) { cb.r /= d.a; cb.g /= d.a; cb.b /= d.a; }
        rgba bl;
        if (op >= GFX_OPERATOR_HSL_HUE) {
            bl = blend_nonsep(op, cs, cb);
        } else {
            bl.r = blend_sep(op, cs.r, cb.r);
            bl.g = blend_sep(op, cs.g, cb.g);
            bl.b = blend_sep(op, cs.b, cb.b);
        }
        double sa_da = s.a * d.a;
        rgba o;
        o.r = s.r * (1 - d.a) + d.r * (1 - s.a) + sa_da * bl.r;
        o.g = s.g * (1 - d.a) + d.g * (1 - s.a) + sa_da * bl.g;
        o.b = s.b * (1 - d.a) + d.b * (1 - s.a) + sa_da * bl.b;
        o.a = s.a + d.a - sa_da;
        return o;
    }
    }
    return (rgba){ s.r * fa + d.r * fb, s.g * fa + d.g * fb,
                   s.b * fa + d.b * fb, s.a * fa + d.a * fb };
}

/* ---- loading and storing destination pixels ---------------------------- */

static inline uint32_t
load(gfx_format_t fmt, const unsigned char *p)
{
    if (fmt == GFX_FORMAT_A8)
        return (uint32_t)*p << 24;
    uint32_t v;
    memcpy(&v, p, 4);
    return fmt == GFX_FORMAT_RGB24 ? v | 0xff000000u : v;
}

static inline void
store(gfx_format_t fmt, unsigned char *p, uint32_t v)
{
    if (fmt == GFX_FORMAT_A8) {
        *p = (unsigned char)(v >> 24);
        return;
    }
    if (fmt == GFX_FORMAT_RGB24) {
        /* Cairo stores RGB24 as if composited onto black. */
        v &= 0x00ffffffu;
    }
    memcpy(p, &v, 4);
}

/* Two channels per multiply: x's four 8-bit channels times a / 255. */
static inline uint32_t
mul4(uint32_t x, unsigned a)
{
    uint32_t rb = (x & 0x00ff00ffu) * a + 0x00800080u;
    rb = ((rb + ((rb >> 8) & 0x00ff00ffu)) >> 8) & 0x00ff00ffu;
    uint32_t ag = ((x >> 8) & 0x00ff00ffu) * a + 0x00800080u;
    ag = (ag + ((ag >> 8) & 0x00ff00ffu)) & 0xff00ff00u;
    return rb | ag;
}

/* x + y per channel, saturating at 255. */
static inline uint32_t
add4(uint32_t x, uint32_t y)
{
    uint32_t rb = (x & 0x00ff00ffu) + (y & 0x00ff00ffu);
    rb = (rb | (0x01000100u - ((rb >> 8) & 0x00ff00ffu))) & 0x00ff00ffu;
    uint32_t ag = ((x >> 8) & 0x00ff00ffu) + ((y >> 8) & 0x00ff00ffu);
    ag = (ag | (0x01000100u - ((ag >> 8) & 0x00ff00ffu))) & 0x00ff00ffu;
    return rb | (ag << 8);
}

/* lerp(d, s, c), c in 0..255. */
static inline uint32_t
lerp8(uint32_t d, uint32_t s, unsigned c)
{
    return add4(mul4(s, c), mul4(d, 255 - c));
}

static inline uint32_t
scale8(uint32_t s, unsigned c)
{
    return mul4(s, c);
}

/* s OVER d. */
static inline uint32_t
over8(uint32_t s, uint32_t d)
{
    unsigned ia = 255 - (s >> 24);
    return ia == 0 ? s : add4(s, mul4(d, ia));
}

static inline uint32_t
add8(uint32_t s, uint32_t d)
{
    return add4(s, d);
}

void
gfx_composite_solid(gfx_operator_t op, gfx_format_t fmt, unsigned char *dst,
                    uint32_t s, const uint8_t *coverage, int len)
{
    if (fmt == GFX_FORMAT_ARGB32 &&
        (op == GFX_OPERATOR_OVER || op == GFX_OPERATOR_SOURCE)) {
        uint32_t *p = (uint32_t *)(void *)dst;
        int opaque = (s >> 24) == 255;
        if (!coverage && (op == GFX_OPERATOR_SOURCE || opaque)) {
            for (int i = 0; i < len; i++)
                p[i] = s;
            return;
        }
        if (op == GFX_OPERATOR_OVER) {
            if (s == 0)
                return;
            if (!coverage) {
                unsigned ia = 255 - (s >> 24);
                for (int i = 0; i < len; i++)
                    p[i] = add4(s, mul4(p[i], ia));
                return;
            }
            for (int i = 0; i < len; i++) {
                unsigned c = coverage[i];
                if (c == 0)
                    continue;
                p[i] = (c == 255 && opaque) ? s : over8(c == 255 ? s : mul4(s, c), p[i]);
            }
            return;
        }
        for (int i = 0; i < len; i++) {
            unsigned c = coverage[i];
            if (c)
                p[i] = c == 255 ? s : lerp8(p[i], s, c);
        }
        return;
    }
    /* Everything else goes through the general path, in chunks. */
    uint32_t buf[256];
    int bpp = fmt == GFX_FORMAT_A8 ? 1 : 4;
    for (int i = 0; i < 256; i++)
        buf[i] = s;
    for (int o = 0; o < len; o += 256) {
        int n = len - o < 256 ? len - o : 256;
        gfx_composite_row(op, fmt, dst + (size_t)o * (size_t)bpp, buf,
                          coverage ? coverage + o : NULL, n);
    }
}

void
gfx_composite_row(gfx_operator_t op, gfx_format_t fmt, unsigned char *dst,
                  const uint32_t *src, const uint8_t *coverage, int len)
{
    int bpp = fmt == GFX_FORMAT_A8 ? 1 : 4;
    if (fmt == GFX_FORMAT_ARGB32 && op == GFX_OPERATOR_OVER) {
        uint32_t *p = (uint32_t *)(void *)dst;
        for (int i = 0; i < len; i++) {
            uint32_t s = src[i];
            unsigned c = coverage ? coverage[i] : 255;
            if (c == 0 || s == 0)
                continue;
            if (c != 255)
                s = mul4(s, c);
            p[i] = (s >> 24) == 255 ? s : over8(s, p[i]);
        }
        return;
    }
    int bounded = gfx_operator_bounded(op);
    for (int i = 0; i < len; i++, dst += bpp) {
        unsigned c = coverage ? coverage[i] : 255;
        if (c == 0 && bounded)
            continue;
        uint32_t s = src[i];
        switch (op) {
        case GFX_OPERATOR_DEST:
            continue;
        case GFX_OPERATOR_OVER: {
            if (s == 0)
                continue;
            if (c == 255 && (s >> 24) == 255) {
                store(fmt, dst, s);
                continue;
            }
            uint32_t d = load(fmt, dst);
            store(fmt, dst, over8(c == 255 ? s : scale8(s, c), d));
            continue;
        }
        case GFX_OPERATOR_SOURCE: {
            uint32_t d = c == 255 ? 0 : load(fmt, dst);
            store(fmt, dst, c == 255 ? s : lerp8(d, s, c));
            continue;
        }
        case GFX_OPERATOR_CLEAR: {
            uint32_t d = c == 255 ? 0 : load(fmt, dst);
            store(fmt, dst, c == 255 ? 0 : lerp8(d, 0, c));
            continue;
        }
        case GFX_OPERATOR_ADD: {
            uint32_t d = load(fmt, dst);
            store(fmt, dst, add8(c == 255 ? s : scale8(s, c), d));
            continue;
        }
        default:
            break;
        }
        uint32_t d = load(fmt, dst);
        rgba dd = unpack(d);
        rgba r;
        if (bounded) {
            r = apply_op(op, unpack(s), dd);
            uint32_t full = gfx_pack(r.r, r.g, r.b, r.a);
            store(fmt, dst, c == 255 ? full : lerp8(d, full, c));
        } else {
            rgba ss = unpack(c == 255 ? s : scale8(s, c));
            r = apply_op(op, ss, dd);
            store(fmt, dst, gfx_pack(r.r, r.g, r.b, r.a));
        }
    }
}
