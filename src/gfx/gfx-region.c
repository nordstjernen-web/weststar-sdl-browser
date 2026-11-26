/* Weststar SDL Browser — gfx: regions, sets of pixels as non-overlapping rectangles.
 * Enough of cairo_region_t for damage tracking: a union only ever adds the
 * parts of a rectangle not already covered, so no pixel is listed twice.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "gfx-private.h"

#include <stdlib.h>
#include <string.h>

struct gfx_region {
    gfx_vec rects;   /* gfx_rectangle_int_t, pairwise disjoint, non-empty */
};

gfx_region_t *
gfx_region_create(void)
{
    gfx_region_t *r = calloc(1, sizeof *r);
    if (r)
        gfx_vec_init(&r->rects, sizeof(gfx_rectangle_int_t));
    return r;
}

gfx_region_t *
gfx_region_create_rectangle(const gfx_rectangle_int_t *rect)
{
    gfx_region_t *r = gfx_region_create();
    if (r && rect)
        gfx_region_union_rectangle(r, rect);
    return r;
}

void
gfx_region_destroy(gfx_region_t *r)
{
    if (!r)
        return;
    gfx_vec_free(&r->rects);
    free(r);
}

int
gfx_region_num_rectangles(const gfx_region_t *r)
{
    return r ? (int)r->rects.len : 0;
}

void
gfx_region_get_rectangle(const gfx_region_t *r, int i, gfx_rectangle_int_t *out)
{
    if (r && i >= 0 && (size_t)i < r->rects.len)
        *out = GFX_VEC_AT(&r->rects, gfx_rectangle_int_t, i);
    else
        *out = (gfx_rectangle_int_t){ 0, 0, 0, 0 };
}

gfx_bool_t
gfx_region_is_empty(const gfx_region_t *r)
{
    return !r || r->rects.len == 0;
}

void
gfx_region_get_extents(const gfx_region_t *r, gfx_rectangle_int_t *out)
{
    if (gfx_region_is_empty(r)) {
        *out = (gfx_rectangle_int_t){ 0, 0, 0, 0 };
        return;
    }
    const gfx_rectangle_int_t *a = r->rects.data;
    int x0 = a[0].x, y0 = a[0].y;
    int x1 = a[0].x + a[0].width, y1 = a[0].y + a[0].height;
    for (size_t i = 1; i < r->rects.len; i++) {
        if (a[i].x < x0) x0 = a[i].x;
        if (a[i].y < y0) y0 = a[i].y;
        if (a[i].x + a[i].width > x1) x1 = a[i].x + a[i].width;
        if (a[i].y + a[i].height > y1) y1 = a[i].y + a[i].height;
    }
    *out = (gfx_rectangle_int_t){ x0, y0, x1 - x0, y1 - y0 };
}

static int
push_rect(gfx_vec *v, int x0, int y0, int x1, int y1)
{
    if (x1 <= x0 || y1 <= y0)
        return 1;
    gfx_rectangle_int_t *slot = gfx_vec_push(v);
    if (!slot)
        return 0;
    *slot = (gfx_rectangle_int_t){ x0, y0, x1 - x0, y1 - y0 };
    return 1;
}

/* Adds the parts of rect no existing rectangle covers. */
void
gfx_region_union_rectangle(gfx_region_t *r, const gfx_rectangle_int_t *rect)
{
    if (!r || !rect || rect->width <= 0 || rect->height <= 0)
        return;
    /* Pieces of the new rectangle still to place; each existing rectangle
     * carves them up into at most four parts. */
    gfx_vec todo, next;
    gfx_vec_init(&todo, sizeof(gfx_rectangle_int_t));
    gfx_vec_init(&next, sizeof(gfx_rectangle_int_t));
    if (!push_rect(&todo, rect->x, rect->y, rect->x + rect->width,
                   rect->y + rect->height))
        goto out;
    for (size_t e = 0; e < r->rects.len && todo.len; e++) {
        const gfx_rectangle_int_t *b = &GFX_VEC_AT(&r->rects, gfx_rectangle_int_t, e);
        int bx0 = b->x, by0 = b->y, bx1 = b->x + b->width, by1 = b->y + b->height;
        gfx_vec_clear(&next);
        for (size_t t = 0; t < todo.len; t++) {
            gfx_rectangle_int_t p = GFX_VEC_AT(&todo, gfx_rectangle_int_t, t);
            int px0 = p.x, py0 = p.y, px1 = p.x + p.width, py1 = p.y + p.height;
            if (px1 <= bx0 || px0 >= bx1 || py1 <= by0 || py0 >= by1) {
                if (!push_rect(&next, px0, py0, px1, py1))
                    goto out;
                continue;
            }
            /* Above, below, then left and right of the overlap. */
            int iy0 = py0 > by0 ? py0 : by0, iy1 = py1 < by1 ? py1 : by1;
            if (!push_rect(&next, px0, py0, px1, iy0) ||
                !push_rect(&next, px0, iy1, px1, py1) ||
                !push_rect(&next, px0, iy0, px0 > bx0 ? px0 : bx0, iy1) ||
                !push_rect(&next, px1 < bx1 ? px1 : bx1, iy0, px1, iy1))
                goto out;
        }
        gfx_vec tmp = todo;
        todo = next;
        next = tmp;
    }
    for (size_t t = 0; t < todo.len; t++) {
        gfx_rectangle_int_t *slot = gfx_vec_push(&r->rects);
        if (!slot)
            break;
        *slot = GFX_VEC_AT(&todo, gfx_rectangle_int_t, t);
    }
out:
    gfx_vec_free(&todo);
    gfx_vec_free(&next);
}
