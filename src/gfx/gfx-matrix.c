/* Weststar SDL Browser — gfx: affine matrices and a growable array.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "gfx-private.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

void
gfx_matrix_init(gfx_matrix_t *m, double xx, double yx, double xy, double yy,
                double x0, double y0)
{
    m->xx = xx; m->yx = yx;
    m->xy = xy; m->yy = yy;
    m->x0 = x0; m->y0 = y0;
}

void
gfx_matrix_init_identity(gfx_matrix_t *m)
{
    gfx_matrix_init(m, 1, 0, 0, 1, 0, 0);
}

void
gfx_matrix_init_translate(gfx_matrix_t *m, double tx, double ty)
{
    gfx_matrix_init(m, 1, 0, 0, 1, tx, ty);
}

void
gfx_matrix_init_scale(gfx_matrix_t *m, double sx, double sy)
{
    gfx_matrix_init(m, sx, 0, 0, sy, 0, 0);
}

void
gfx_matrix_init_rotate(gfx_matrix_t *m, double radians)
{
    double s = sin(radians), c = cos(radians);
    gfx_matrix_init(m, c, s, -s, c, 0, 0);
}

void
gfx_matrix_multiply(gfx_matrix_t *result, const gfx_matrix_t *a,
                    const gfx_matrix_t *b)
{
    gfx_matrix_t r;
    r.xx = a->xx * b->xx + a->yx * b->xy;
    r.yx = a->xx * b->yx + a->yx * b->yy;
    r.xy = a->xy * b->xx + a->yy * b->xy;
    r.yy = a->xy * b->yx + a->yy * b->yy;
    r.x0 = a->x0 * b->xx + a->y0 * b->xy + b->x0;
    r.y0 = a->x0 * b->yx + a->y0 * b->yy + b->y0;
    *result = r;
}

void
gfx_matrix_translate(gfx_matrix_t *m, double tx, double ty)
{
    gfx_matrix_t t;
    gfx_matrix_init_translate(&t, tx, ty);
    gfx_matrix_multiply(m, &t, m);
}

void
gfx_matrix_scale(gfx_matrix_t *m, double sx, double sy)
{
    gfx_matrix_t t;
    gfx_matrix_init_scale(&t, sx, sy);
    gfx_matrix_multiply(m, &t, m);
}

void
gfx_matrix_rotate(gfx_matrix_t *m, double radians)
{
    gfx_matrix_t t;
    gfx_matrix_init_rotate(&t, radians);
    gfx_matrix_multiply(m, &t, m);
}

gfx_status_t
gfx_matrix_invert(gfx_matrix_t *m)
{
    double det = m->xx * m->yy - m->yx * m->xy;
    if (det == 0 || !isfinite(det))
        return GFX_STATUS_INVALID_MATRIX;
    gfx_matrix_t r;
    r.xx = m->yy / det;
    r.yx = -m->yx / det;
    r.xy = -m->xy / det;
    r.yy = m->xx / det;
    r.x0 = -(r.xx * m->x0 + r.xy * m->y0);
    r.y0 = -(r.yx * m->x0 + r.yy * m->y0);
    *m = r;
    return GFX_STATUS_SUCCESS;
}

void
gfx_matrix_transform_distance(const gfx_matrix_t *m, double *dx, double *dy)
{
    double x = *dx, y = *dy;
    *dx = m->xx * x + m->xy * y;
    *dy = m->yx * x + m->yy * y;
}

void
gfx_matrix_transform_point(const gfx_matrix_t *m, double *x, double *y)
{
    gfx_matrix_transform_distance(m, x, y);
    *x += m->x0;
    *y += m->y0;
}

/* ---- gfx_vec ----------------------------------------------------------- */

void
gfx_vec_init(gfx_vec *v, size_t elem)
{
    v->data = NULL;
    v->len = v->cap = 0;
    v->elem = elem;
}

void
gfx_vec_free(gfx_vec *v)
{
    free(v->data);
    v->data = NULL;
    v->len = v->cap = 0;
}

void
gfx_vec_clear(gfx_vec *v)
{
    v->len = 0;
}

void *
gfx_vec_push(gfx_vec *v)
{
    if (v->len == v->cap) {
        size_t cap = v->cap ? v->cap * 2 : 16;
        void *d = realloc(v->data, cap * v->elem);
        if (!d)
            return NULL;
        v->data = d;
        v->cap = cap;
    }
    void *slot = (char *)v->data + v->len * v->elem;
    memset(slot, 0, v->elem);
    v->len++;
    return slot;
}
