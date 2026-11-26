/* Weststar SDL Browser — gfx: the bridge to Cairo for call sites not ported yet.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "gfx-cairo.h"

#include "gfx-private.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

cairo_surface_t *
gfx_cairo_surface_wrap(gfx_surface_t *s)
{
    cairo_format_t fmt;
    switch (gfx_image_surface_get_format(s)) {
    case GFX_FORMAT_ARGB32: fmt = CAIRO_FORMAT_ARGB32; break;
    case GFX_FORMAT_RGB24:  fmt = CAIRO_FORMAT_RGB24; break;
    case GFX_FORMAT_A8:     fmt = CAIRO_FORMAT_A8; break;
    default:
        return cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 0, 0);
    }
    return cairo_image_surface_create_for_data(gfx_image_surface_get_data(s),
                                               fmt, s->width, s->height,
                                               s->stride);
}

static const cairo_user_data_key_t k_masked;

cairo_t *
gfx_cairo_begin(gfx_t *g)
{
    gfx_surface_t *t = gfx_get_group_target(g);
    cairo_surface_t *cs = gfx_cairo_surface_wrap(t);
    cairo_t *cr = cairo_create(cs);
    cairo_surface_destroy(cs);
    if (gfx_status(g) != GFX_STATUS_SUCCESS)
        return cr;

    gfx_rectangle_int_t box;
    gfx_bool_t is_rect = 1;
    if (!gfx_clip_pixel_box(g, &box, &is_rect)) {
        cairo_rectangle(cr, 0, 0, 0, 0);
    } else {
        cairo_rectangle(cr, box.x, box.y, box.width, box.height);
    }
    cairo_clip(cr);
    if (!is_rect) {
        /* The clip is a mask: draw into a group, masked by it at the end. */
        cairo_push_group(cr);
        cairo_set_user_data(cr, &k_masked, (void *)&k_masked, NULL);
    }
    gfx_matrix_t m;
    gfx_user_to_pixels_matrix(g, &m);
    cairo_matrix_t cm;
    cairo_matrix_init(&cm, m.xx, m.yx, m.xy, m.yy, m.x0, m.y0);
    cairo_set_matrix(cr, &cm);
    cairo_set_operator(cr, (cairo_operator_t)gfx_get_operator(g));
    double r, gg, b, a;
    if (gfx_pattern_get_rgba(gfx_get_source(g), &r, &gg, &b, &a))
        cairo_set_source_rgba(cr, r, gg, b, a);
    return cr;
}

void
gfx_cairo_end(gfx_t *g, cairo_t *cr)
{
    if (!cr)
        return;
    if (cairo_get_user_data(cr, &k_masked)) {
        cairo_pop_group_to_source(cr);
        cairo_identity_matrix(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
        int x0, y0, w, h;
        const uint8_t *mask;
        if (gfx_clip_mask(g, &x0, &y0, &w, &h, &mask) && w > 0 && h > 0) {
            int stride = cairo_format_stride_for_width(CAIRO_FORMAT_A8, w);
            unsigned char *buf = malloc((size_t)stride * (size_t)h);
            if (buf) {
                for (int y = 0; y < h; y++)
                    memcpy(buf + (size_t)y * (size_t)stride,
                           mask + (size_t)y * (size_t)w, (size_t)w);
                cairo_surface_t *ms = cairo_image_surface_create_for_data(
                    buf, CAIRO_FORMAT_A8, w, h, stride);
                cairo_mask_surface(cr, ms, x0, y0);
                cairo_surface_destroy(ms);
                free(buf);
            }
        }
    }
    cairo_surface_flush(cairo_get_target(cr));
    cairo_destroy(cr);
}

/* ---- the other way: gfx drawing into a cairo_t's target ---------------- */

struct gfx_from_cairo {
    gfx_surface_t   *surface;
    cairo_surface_t *image;     /* vector targets: the image drawn into */
    int              grouped;   /* drawn into a Cairo group */
    double           x0, y0, k; /* vector targets: where the image goes */
};

static gfx_format_t
format_of(cairo_format_t f)
{
    switch (f) {
    case CAIRO_FORMAT_ARGB32: return GFX_FORMAT_ARGB32;
    case CAIRO_FORMAT_RGB24:  return GFX_FORMAT_RGB24;
    case CAIRO_FORMAT_A8:     return GFX_FORMAT_A8;
    default:                  return GFX_FORMAT_INVALID;
    }
}

static int
is_gfx_image(cairo_surface_t *s)
{
    return cairo_surface_get_type(s) == CAIRO_SURFACE_TYPE_IMAGE &&
           format_of(cairo_image_surface_get_format(s)) != GFX_FORMAT_INVALID;
}

static gfx_surface_t *
wrap_cairo_image(cairo_surface_t *s)
{
    cairo_surface_flush(s);
    gfx_surface_t *g = gfx_image_surface_create_for_data(
        cairo_image_surface_get_data(s),
        format_of(cairo_image_surface_get_format(s)),
        cairo_image_surface_get_width(s), cairo_image_surface_get_height(s),
        cairo_image_surface_get_stride(s));
    double dx, dy, sx, sy;
    cairo_surface_get_device_offset(s, &dx, &dy);
    cairo_surface_get_device_scale(s, &sx, &sy);
    gfx_surface_set_device_offset(g, dx, dy);
    gfx_surface_set_device_scale(g, sx, sy);
    return g;
}

gfx_t *
gfx_from_cairo_begin(cairo_t *cr, gfx_from_cairo **state)
{
    gfx_from_cairo *st = calloc(1, sizeof *st);
    *state = st;
    cairo_matrix_t cm;
    cairo_get_matrix(cr, &cm);
    gfx_matrix_t m = { cm.xx, cm.yx, cm.xy, cm.yy, cm.x0, cm.y0 };

    /* The clip in device space, as rectangles when it is that. */
    cairo_save(cr);
    cairo_identity_matrix(cr);
    cairo_rectangle_list_t *rects = cairo_copy_clip_rectangle_list(cr);
    double ex1, ey1, ex2, ey2;
    cairo_clip_extents(cr, &ex1, &ey1, &ex2, &ey2);
    cairo_restore(cr);

    gfx_t *g;
    if (!st || !is_gfx_image(cairo_get_group_target(cr))) {
        /* A vector target: draw an image of the clipped area. */
        double k = 2;
        int w = (int)ceil((ex2 - ex1) * k), h = (int)ceil((ey2 - ey1) * k);
        if (w < 1) w = 1;
        if (h < 1) h = 1;
        if (st) {
            st->image = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
            st->x0 = ex1;
            st->y0 = ey1;
            st->k = k;
            st->surface = wrap_cairo_image(st->image);
            gfx_surface_set_device_scale(st->surface, k, k);
            gfx_surface_set_device_offset(st->surface, -ex1 * k, -ey1 * k);
            g = gfx_create(st->surface);
        } else {
            gfx_surface_t *none = gfx_image_surface_create(GFX_FORMAT_ARGB32, 0, 0);
            g = gfx_create(none);
            gfx_surface_destroy(none);
        }
        gfx_set_matrix(g, &m);
        cairo_rectangle_list_destroy(rects);
        return g;
    }

    int representable = rects->status == CAIRO_STATUS_SUCCESS;
    if (!representable) {
        /* Cairo clips the group when it is painted. */
        cairo_push_group(cr);
        st->grouped = 1;
    }
    st->surface = wrap_cairo_image(cairo_get_group_target(cr));
    g = gfx_create(st->surface);
    if (representable) {
        /* Device space = gfx user space under the identity matrix. */
        for (int i = 0; i < rects->num_rectangles; i++) {
            const cairo_rectangle_t *r = &rects->rectangles[i];
            gfx_rectangle(g, r->x, r->y, r->width, r->height);
        }
        if (rects->num_rectangles == 0)
            gfx_rectangle(g, 0, 0, 0, 0);
        gfx_clip(g);
    }
    cairo_rectangle_list_destroy(rects);
    gfx_set_matrix(g, &m);
    return g;
}

void
gfx_from_cairo_end(cairo_t *cr, gfx_t *g, gfx_from_cairo *st)
{
    gfx_destroy(g);
    if (!st)
        return;
    gfx_surface_destroy(st->surface);
    if (st->image) {
        cairo_surface_mark_dirty(st->image);
        cairo_save(cr);
        cairo_identity_matrix(cr);
        cairo_translate(cr, st->x0, st->y0);
        cairo_scale(cr, 1 / st->k, 1 / st->k);
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
        cairo_set_source_surface(cr, st->image, 0, 0);
        cairo_paint(cr);
        cairo_restore(cr);
        cairo_surface_destroy(st->image);
    } else {
        cairo_surface_mark_dirty(cairo_get_group_target(cr));
        if (st->grouped) {
            cairo_pop_group_to_source(cr);
            cairo_paint(cr);
        }
    }
    free(st);
}

void
gfx_append_cairo_path(gfx_t *g, const cairo_path_t *path)
{
    if (!path || path->status != CAIRO_STATUS_SUCCESS)
        return;
    for (int i = 0; i < path->num_data;) {
        const cairo_path_data_t *h = &path->data[i];
        int len = h->header.length;
        if (len < 1 || i + len > path->num_data)
            break;
        switch (h->header.type) {
        case CAIRO_PATH_MOVE_TO:
            if (len >= 2)
                gfx_move_to(g, h[1].point.x, h[1].point.y);
            break;
        case CAIRO_PATH_LINE_TO:
            if (len >= 2)
                gfx_line_to(g, h[1].point.x, h[1].point.y);
            break;
        case CAIRO_PATH_CURVE_TO:
            if (len >= 4)
                gfx_curve_to(g, h[1].point.x, h[1].point.y, h[2].point.x,
                             h[2].point.y, h[3].point.x, h[3].point.y);
            break;
        case CAIRO_PATH_CLOSE_PATH:
            gfx_close_path(g);
            break;
        }
        i += len;
    }
}
