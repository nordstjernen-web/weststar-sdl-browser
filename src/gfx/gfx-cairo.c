/* Weststar SDL Browser — gfx: the bridge to Cairo for call sites not ported yet.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "gfx-cairo.h"

#include "gfx-private.h"

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
