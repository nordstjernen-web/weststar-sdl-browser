/* Weststar SDL Browser — gfx: path storage and flattening.
 * Paths are kept in the target's pixels, as Cairo keeps them in backend
 * space, so changing the matrix after a segment leaves the segment alone.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "gfx-private.h"

#include <math.h>
#include <string.h>

void
gfx_pathbuf_init(gfx_pathbuf *p)
{
    memset(p, 0, sizeof *p);
    gfx_vec_init(&p->data, sizeof(gfx_path_data_t));
}

void
gfx_pathbuf_free(gfx_pathbuf *p)
{
    gfx_vec_free(&p->data);
}

void
gfx_pathbuf_clear(gfx_pathbuf *p)
{
    gfx_vec_clear(&p->data);
    p->has_current = p->has_move = p->need_move = p->tail_is_move = 0;
}

int
gfx_pathbuf_copy(gfx_pathbuf *dst, const gfx_pathbuf *src)
{
    gfx_pathbuf_clear(dst);
    for (size_t i = 0; i < src->data.len; i++) {
        gfx_path_data_t *d = gfx_vec_push(&dst->data);
        if (!d)
            return 0;
        *d = GFX_VEC_AT(&src->data, gfx_path_data_t, i);
    }
    dst->has_current = src->has_current;
    dst->cur_x = src->cur_x;
    dst->cur_y = src->cur_y;
    dst->has_move = src->has_move;
    dst->move_x = src->move_x;
    dst->move_y = src->move_y;
    dst->need_move = src->need_move;
    dst->tail_is_move = src->tail_is_move;
    return 1;
}

static void
push_op(gfx_pathbuf *p, gfx_path_data_type_t type, const double *xy, int n)
{
    gfx_path_data_t *h = gfx_vec_push(&p->data);
    if (!h)
        return;
    h->header.type = type;
    h->header.length = n + 1;
    p->tail_is_move = 0;
    for (int i = 0; i < n; i++) {
        gfx_path_data_t *pt = gfx_vec_push(&p->data);
        if (!pt)
            return;
        pt->point.x = xy[2 * i];
        pt->point.y = xy[2 * i + 1];
    }
}

void
gfx_pathbuf_move_to(gfx_pathbuf *p, double x, double y)
{
    /* A move_to after a move_to replaces it. */
    if (p->tail_is_move && p->data.len >= 2) {
        gfx_path_data_t *pt = &GFX_VEC_AT(&p->data, gfx_path_data_t, p->data.len - 1);
        pt->point.x = x;
        pt->point.y = y;
    } else {
        push_op(p, GFX_PATH_MOVE_TO, (double[]){ x, y }, 1);
    }
    p->tail_is_move = 1;
    p->has_current = p->has_move = 1;
    p->need_move = 0;
    p->cur_x = p->move_x = x;
    p->cur_y = p->move_y = y;
}

/* Emits the move_to a segment needs after close_path. */
static void
ensure_move(gfx_pathbuf *p)
{
    if (p->need_move) {
        double mx = p->move_x, my = p->move_y;
        gfx_pathbuf_move_to(p, mx, my);
    }
}

void
gfx_pathbuf_line_to(gfx_pathbuf *p, double x, double y)
{
    if (!p->has_current) {
        gfx_pathbuf_move_to(p, x, y);
        return;
    }
    ensure_move(p);
    push_op(p, GFX_PATH_LINE_TO, (double[]){ x, y }, 1);
    p->cur_x = x;
    p->cur_y = y;
}

void
gfx_pathbuf_curve_to(gfx_pathbuf *p, double x1, double y1, double x2,
                     double y2, double x3, double y3)
{
    if (!p->has_current)
        gfx_pathbuf_move_to(p, x1, y1);
    ensure_move(p);
    push_op(p, GFX_PATH_CURVE_TO, (double[]){ x1, y1, x2, y2, x3, y3 }, 3);
    p->cur_x = x3;
    p->cur_y = y3;
}

void
gfx_pathbuf_close(gfx_pathbuf *p)
{
    if (!p->has_current || p->need_move)
        return;
    push_op(p, GFX_PATH_CLOSE_PATH, NULL, 0);
    /* As Cairo (since 1.2.4) does, the sub-path's start follows as an
     * explicit move_to, visible to copy_path. */
    double mx = p->move_x, my = p->move_y;
    gfx_pathbuf_move_to(p, mx, my);
}

void
gfx_pathbuf_translate(gfx_pathbuf *p, double dx, double dy)
{
    size_t i = 0;
    while (i < p->data.len) {
        gfx_path_data_t *h = &GFX_VEC_AT(&p->data, gfx_path_data_t, i);
        for (int k = 1; k < h->header.length; k++) {
            h[k].point.x += dx;
            h[k].point.y += dy;
        }
        i += (size_t)h->header.length;
    }
    p->cur_x += dx;
    p->cur_y += dy;
    p->move_x += dx;
    p->move_y += dy;
}

/* ---- flattened paths --------------------------------------------------- */

void
gfx_flat_init(gfx_flat *f)
{
    gfx_vec_init(&f->pts, sizeof(gfx_fpoint));
    gfx_vec_init(&f->subs, sizeof(gfx_fsub));
}

void
gfx_flat_free(gfx_flat *f)
{
    gfx_vec_free(&f->pts);
    gfx_vec_free(&f->subs);
}

void
gfx_flat_clear(gfx_flat *f)
{
    gfx_vec_clear(&f->pts);
    gfx_vec_clear(&f->subs);
}

int
gfx_flat_begin(gfx_flat *f)
{
    gfx_fsub *s = gfx_vec_push(&f->subs);
    if (!s)
        return 0;
    s->start = (int)f->pts.len;
    return 1;
}

int
gfx_flat_add(gfx_flat *f, double x, double y, int corner)
{
    if (!f->subs.len)
        return 0;
    gfx_fpoint *pt = gfx_vec_push(&f->pts);
    if (!pt)
        return 0;
    pt->x = x;
    pt->y = y;
    pt->corner = corner;
    GFX_VEC_AT(&f->subs, gfx_fsub, f->subs.len - 1).count++;
    return 1;
}

void
gfx_flat_end(gfx_flat *f, int closed)
{
    if (f->subs.len)
        GFX_VEC_AT(&f->subs, gfx_fsub, f->subs.len - 1).closed = closed;
}

/* Uniform subdivision: with n segments a cubic strays from its chords by at
 * most |B''|max / (8 n^2), and |B''| <= 6 max(|p0 - 2p1 + p2|, |p1 - 2p2 + p3|). */
static int
curve_segments(double x0, double y0, double x1, double y1, double x2,
               double y2, double x3, double y3, double tolerance)
{
    double ax = x0 - 2 * x1 + x2, ay = y0 - 2 * y1 + y2;
    double bx = x1 - 2 * x2 + x3, by = y1 - 2 * y2 + y3;
    double m = fmax(sqrt(ax * ax + ay * ay), sqrt(bx * bx + by * by));
    double n = ceil(sqrt(0.75 * m / tolerance));
    if (!(n >= 1))
        return 1;
    return n > 1000 ? 1000 : (int)n;
}

int
gfx_flatten(const gfx_pathbuf *p, double tolerance, gfx_flat *out)
{
    if (!(tolerance > 0.001))
        tolerance = 0.001;
    gfx_flat_clear(out);
    int open = 0;
    double cx = 0, cy = 0;
    size_t i = 0;
    while (i < p->data.len) {
        const gfx_path_data_t *h = &GFX_VEC_AT(&p->data, gfx_path_data_t, i);
        switch (h->header.type) {
        case GFX_PATH_MOVE_TO:
            if (!gfx_flat_begin(out))
                return 0;
            open = 1;
            cx = h[1].point.x;
            cy = h[1].point.y;
            if (!gfx_flat_add(out, cx, cy, 1))
                return 0;
            break;
        case GFX_PATH_LINE_TO:
            if (!open)
                break;
            cx = h[1].point.x;
            cy = h[1].point.y;
            if (!gfx_flat_add(out, cx, cy, 1))
                return 0;
            break;
        case GFX_PATH_CURVE_TO: {
            if (!open)
                break;
            double x1 = h[1].point.x, y1 = h[1].point.y;
            double x2 = h[2].point.x, y2 = h[2].point.y;
            double x3 = h[3].point.x, y3 = h[3].point.y;
            int n = curve_segments(cx, cy, x1, y1, x2, y2, x3, y3, tolerance);
            for (int k = 1; k <= n; k++) {
                double t = (double)k / n, u = 1 - t;
                double b0 = u * u * u, b1 = 3 * u * u * t, b2 = 3 * u * t * t,
                       b3 = t * t * t;
                double x = b0 * cx + b1 * x1 + b2 * x2 + b3 * x3;
                double y = b0 * cy + b1 * y1 + b2 * y2 + b3 * y3;
                if (k == n) {
                    x = x3;
                    y = y3;
                }
                if (!gfx_flat_add(out, x, y, k == n))
                    return 0;
            }
            cx = x3;
            cy = y3;
            break;
        }
        case GFX_PATH_CLOSE_PATH:
            if (open)
                gfx_flat_end(out, 1);
            open = 0;
            break;
        }
        i += (size_t)h->header.length;
    }
    return 1;
}
