/* Weststar SDL Browser — gfx: the scanline rasterizer.
 *
 * Each row accumulates, per pixel, the signed area the polygon's edges
 * sweep to its left (the accumulation technique of font-rs and stb_truetype);
 * a running sum over the row then gives each pixel's winding-weighted
 * coverage, to which the fill rule is applied. Coverage is exact for
 * pixels crossed by a single edge and a close approximation where several
 * edges meet in one pixel.
 *
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "gfx-private.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

void
gfx_polygon_init(gfx_polygon *poly)
{
    gfx_vec_init(&poly->edges, sizeof(gfx_edge));
    poly->minx = poly->miny = INFINITY;
    poly->maxx = poly->maxy = -INFINITY;
}

void
gfx_polygon_free(gfx_polygon *poly)
{
    gfx_vec_free(&poly->edges);
}

void
gfx_polygon_clear(gfx_polygon *poly)
{
    gfx_vec_clear(&poly->edges);
    poly->minx = poly->miny = INFINITY;
    poly->maxx = poly->maxy = -INFINITY;
}

void
gfx_polygon_add_line(gfx_polygon *poly, double x0, double y0, double x1,
                     double y1)
{
    if (y0 == y1 || !isfinite(x0) || !isfinite(y0) || !isfinite(x1) ||
        !isfinite(y1))
        return;
    gfx_edge *e = gfx_vec_push(&poly->edges);
    if (!e)
        return;
    if (y0 < y1) {
        *e = (gfx_edge){ x0, y0, x1, y1, 1 };
    } else {
        *e = (gfx_edge){ x1, y1, x0, y0, -1 };
    }
    poly->minx = fmin(poly->minx, fmin(x0, x1));
    poly->maxx = fmax(poly->maxx, fmax(x0, x1));
    poly->miny = fmin(poly->miny, fmin(y0, y1));
    poly->maxy = fmax(poly->maxy, fmax(y0, y1));
}

void
gfx_polygon_add_flat(gfx_polygon *poly, const gfx_flat *f)
{
    for (size_t s = 0; s < f->subs.len; s++) {
        const gfx_fsub *sub = &GFX_VEC_AT(&f->subs, gfx_fsub, s);
        if (sub->count < 2)
            continue;
        const gfx_fpoint *p = &GFX_VEC_AT(&f->pts, gfx_fpoint, sub->start);
        for (int i = 0; i < sub->count; i++) {
            const gfx_fpoint *a = &p[i];
            const gfx_fpoint *b = &p[(i + 1) % sub->count];
            gfx_polygon_add_line(poly, a->x, a->y, b->x, b->y);
        }
    }
}

int
gfx_polygon_contains(const gfx_polygon *poly, gfx_fill_rule_t rule, double x,
                     double y)
{
    double w = 0;
    for (size_t i = 0; i < poly->edges.len; i++) {
        const gfx_edge *e = &GFX_VEC_AT(&poly->edges, gfx_edge, i);
        if (y < e->y0 || y >= e->y1)
            continue;
        double ex = e->x0 + (y - e->y0) * (e->x1 - e->x0) / (e->y1 - e->y0);
        if (ex > x)
            w += e->dir;
    }
    if (rule == GFX_FILL_RULE_EVEN_ODD)
        return ((long)fabs(w)) % 2 == 1;
    return w != 0;
}

/* ---- rasterization ----------------------------------------------------- */

typedef struct {
    gfx_vec edges;   /* gfx_edge, x relative to the box */
} edge_list;

static void
push_edge(edge_list *l, double x0, double y0, double x1, double y1, double dir)
{
    if (y1 <= y0)
        return;
    gfx_edge *e = gfx_vec_push(&l->edges);
    if (e)
        *e = (gfx_edge){ x0, y0, x1, y1, dir };
}

/* Adds the part of e within x < width, moving whatever lies left of the box
 * onto its left side, where it still counts towards the winding. */
static void
clip_edge(edge_list *l, const gfx_edge *e, double ox, double width)
{
    double x0 = e->x0 - ox, x1 = e->x1 - ox;
    double y0 = e->y0, y1 = e->y1;
    /* Split where the edge crosses x = 0 and x = width. */
    double ts[4] = { 0, 0, 0, 1 };
    int n = 1;
    double dx = x1 - x0;
    if (dx != 0) {
        double t = (0 - x0) / dx;
        if (t > 0 && t < 1)
            ts[n++] = t;
        t = (width - x0) / dx;
        if (t > 0 && t < 1)
            ts[n++] = t;
        if (n == 3 && ts[1] > ts[2]) {
            double tmp = ts[1];
            ts[1] = ts[2];
            ts[2] = tmp;
        }
    }
    ts[n] = 1;
    for (int i = 0; i < n; i++) {
        double ta = ts[i], tb = ts[i + 1];
        double ax = x0 + dx * ta, bx = x0 + dx * tb;
        double ay = y0 + (y1 - y0) * ta, by = y0 + (y1 - y0) * tb;
        double mx = 0.5 * (ax + bx);
        if (mx >= width)
            continue;               /* right of the box: no effect */
        if (mx <= 0) {
            ax = bx = 0;            /* left of the box: full winding */
        } else {
            ax = gfx_clampd(ax, 0, width);
            bx = gfx_clampd(bx, 0, width);
        }
        push_edge(l, ax, ay, bx, by, e->dir);
    }
}

static int
cmp_edge_y0(const void *a, const void *b)
{
    double ya = ((const gfx_edge *)a)->y0, yb = ((const gfx_edge *)b)->y0;
    return ya < yb ? -1 : ya > yb;
}

/* The signed area a straight piece of edge from (x, ...) to (xnext, ...)
 * covering d of the row's height adds to each pixel. Adapted from
 * font-rs's accumulate (Copyright 2015 Google Inc., Apache-2.0; see
 * THIRD-PARTY-LICENSES.md). */
static void
accumulate(double *acc, double x, double xnext, double d, int *lo, int *hi)
{
    double x0 = x < xnext ? x : xnext;
    double x1 = x < xnext ? xnext : x;
    double x0floor = floor(x0);
    int x0i = (int)x0floor;
    double x1ceil = ceil(x1);
    int x1i = (int)x1ceil;
    if (x0i < *lo)
        *lo = x0i;
    if (x1i + 1 > *hi)
        *hi = x1i + 1;
    if (x1i <= x0i + 1) {
        double xmf = 0.5 * (x + xnext) - x0floor;
        acc[x0i] += d - d * xmf;
        acc[x0i + 1] += d * xmf;
        return;
    }
    double s = 1.0 / (x1 - x0);
    double x0f = x0 - x0floor;
    double a0 = 0.5 * s * (1 - x0f) * (1 - x0f);
    double x1f = x1 - x1ceil + 1;
    double am = 0.5 * s * x1f * x1f;
    acc[x0i] += d * a0;
    if (x1i == x0i + 2) {
        acc[x0i + 1] += d * (1 - a0 - am);
    } else {
        double a1 = s * (1.5 - x0f);
        acc[x0i + 1] += d * (a1 - a0);
        for (int xi = x0i + 2; xi < x1i - 1; xi++)
            acc[xi] += d * s;
        double a2 = a1 + (x1i - x0i - 3) * s;
        acc[x1i - 1] += d * (1 - a2 - am);
    }
    acc[x1i] += d * am;
}

static uint8_t
coverage_of(double sum, gfx_fill_rule_t rule, gfx_antialias_t aa)
{
    double c = fabs(sum);
    if (rule == GFX_FILL_RULE_EVEN_ODD) {
        c = fmod(c, 2.0);
        if (c > 1)
            c = 2 - c;
    } else if (c > 1) {
        c = 1;
    }
    if (aa == GFX_ANTIALIAS_NONE)
        return c >= 0.5 ? 255 : 0;
    int v = (int)(c * 255 + 0.5);
    return (uint8_t)(v > 255 ? 255 : v);
}

void
gfx_rasterize(const gfx_polygon *poly, gfx_fill_rule_t rule,
              gfx_antialias_t aa, int bx0, int by0, int bx1, int by1,
              gfx_span_func span, void *closure)
{
    if (!poly->edges.len)
        return;
    /* Edges left of the box matter (they wind); the box itself is cut down
     * to what the polygon can touch. */
    int x0 = bx0, x1 = bx1;
    if (poly->minx > x0) x0 = (int)fmax(x0, floor(poly->minx));
    if (poly->maxx < x1) x1 = (int)fmin(x1, ceil(poly->maxx) + 1);
    int y0 = (int)fmax(by0, floor(poly->miny));
    int y1 = (int)fmin(by1, ceil(poly->maxy));
    if (x0 >= x1 || y0 >= y1)
        return;
    int width = x1 - x0;

    edge_list l;
    gfx_vec_init(&l.edges, sizeof(gfx_edge));
    for (size_t i = 0; i < poly->edges.len; i++) {
        const gfx_edge *e = &GFX_VEC_AT(&poly->edges, gfx_edge, i);
        if (e->y1 <= y0 || e->y0 >= y1)
            continue;
        clip_edge(&l, e, x0, width);
    }
    size_t n = l.edges.len;
    if (!n) {
        gfx_vec_free(&l.edges);
        return;
    }
    gfx_edge *edges = l.edges.data;
    qsort(edges, n, sizeof *edges, cmp_edge_y0);

    double *acc = calloc((size_t)width + 3, sizeof *acc);
    uint8_t *cov = malloc((size_t)width + 1);
    size_t *active = malloc(n * sizeof *active);
    if (!acc || !cov || !active)
        goto out;

    size_t next = 0, nactive = 0;
    for (int y = y0; y < y1; y++) {
        double top = y, bot = y + 1.0;
        while (next < n && edges[next].y0 < bot)
            active[nactive++] = next++;
        int lo = width + 2, hi = -1;
        for (size_t k = 0; k < nactive;) {
            const gfx_edge *e = &edges[active[k]];
            if (e->y1 <= top) {
                active[k] = active[--nactive];
                continue;
            }
            k++;
            double ya = fmax(e->y0, top), yb = fmin(e->y1, bot);
            if (yb <= ya)
                continue;
            double slope = (e->x1 - e->x0) / (e->y1 - e->y0);
            double xa = e->x0 + (ya - e->y0) * slope;
            double xb = e->x0 + (yb - e->y0) * slope;
            xa = gfx_clampd(xa, 0, width);
            xb = gfx_clampd(xb, 0, width);
            accumulate(acc, xa, xb, e->dir * (yb - ya), &lo, &hi);
        }
        if (hi < 0)
            continue;
        if (lo < 0)
            lo = 0;
        /* Past the last touched pixel the sum stays constant: emit up to
         * the box edge only while it is non-zero. */
        double sum = 0;
        int end = lo;
        for (int x = lo; x < width; x++) {
            sum += acc[x];
            acc[x] = 0;
            if (x >= hi && fabs(sum) < 1e-6)
                break;
            cov[x - lo] = coverage_of(sum, rule, aa);
            end = x + 1;
        }
        for (int x = end; x <= width + 2 && x < hi; x++)
            acc[x] = 0;
        acc[width] = acc[width + 1] = acc[width + 2] = 0;
        /* Trim fully transparent ends. */
        int s = lo, e = end;
        while (s < e && cov[s - lo] == 0)
            s++;
        while (e > s && cov[e - 1 - lo] == 0)
            e--;
        if (e > s)
            span(closure, y, x0 + s, e - s, cov + (s - lo));
    }
out:
    free(acc);
    free(cov);
    free(active);
    gfx_vec_free(&l.edges);
}
