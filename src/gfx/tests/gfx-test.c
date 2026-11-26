/* Weststar SDL Browser — gfx tests: every scene is drawn with gfx and with Cairo
 * and the pixels compared; queries (in_fill, extents, current point) are
 * compared too. GFX_TEST_OUT=<dir> writes each pair as PNGs.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "gfx.h"

#include <cairo.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define W 250
#define H 250

/* ---- the scenes, twice ------------------------------------------------- */

#define P(name) gfx_##name
#define E(name) GFX_##name
#define CTX gfx_t
#define SURF gfx_surface_t
#define PAT gfx_pattern_t
#define PATH gfx_path_t
#define SCENE(name) gfx_scene_##name
#include "scenes.h"
#undef P
#undef E
#undef CTX
#undef SURF
#undef PAT
#undef PATH
#undef SCENE

#define P(name) cairo_##name
#define E(name) CAIRO_##name
#define CTX cairo_t
#define SURF cairo_surface_t
#define PAT cairo_pattern_t
#define PATH cairo_path_t
#define SCENE(name) cairo_scene_##name
#include "scenes.h"
#undef P
#undef E
#undef CTX
#undef SURF
#undef PAT
#undef PATH
#undef SCENE

typedef struct {
    const char *name;
    void (*gfx)(gfx_t *);
    void (*cairo)(cairo_t *);
    /* Allowed: pixels differing by more than `big` in some channel, as a
     * fraction of all pixels, and the mean difference per channel. */
    double max_big_fraction;
    double max_mean;
    int    mesh;      /* compared only where Cairo draws meshes */
} scene;

#define S(n, frac, mean) { #n, gfx_scene_##n, cairo_scene_##n, frac, mean, 0 }
#define S_MESH(n, frac, mean) { #n, gfx_scene_##n, cairo_scene_##n, frac, mean, 1 }

static const scene scenes[] = {
    S(fills, 0.002, 0.3),
    S(arcs, 0.002, 0.3),
    S(strokes, 0.004, 0.4),
    S(hairlines, 0.004, 0.5),
    S(dashes, 0.004, 0.4),
    S(transforms, 0.004, 0.4),
    S(linear, 0.001, 0.5),
    S(radial, 0.001, 0.5),
    S(images, 0.01, 0.6),
    S(clips, 0.002, 0.3),
    S(groups, 0.002, 0.3),
    S(masks, 0.002, 0.5),
    S(operators, 0.01, 0.6),
    S_MESH(mesh, 0.01, 1.0),
    S(a8, 0.002, 0.4),
    S(paths, 0.004, 0.4),
};

static const int big = 24;

/* Some Cairo builds (1.18.4 on Ubuntu 26.04) draw only the outlines of
 * mesh patches. Such a Cairo is no reference: this draws the mesh scene
 * with Cairo and looks at a point well inside one of its patches. */
static int
cairo_draws_meshes(void)
{
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
    cairo_t *cr = cairo_create(s);
    cairo_scene_mesh(cr);
    cairo_destroy(cr);
    cairo_surface_flush(s);
    int x = 125 + 60, y = 125 + 3;   /* inside the first patch */
    uint32_t px;
    memcpy(&px, cairo_image_surface_get_data(s) +
                y * cairo_image_surface_get_stride(s) + x * 4, 4);
    cairo_surface_destroy(s);
    return (px >> 24) == 0xff;
}

static void
write_png(const char *dir, const char *name, const char *kind,
          cairo_surface_t *s)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s-%s.png", dir, name, kind);
    cairo_surface_write_to_png(s, path);
}

static int
run_scene(const scene *sc, const char *out_dir)
{
    gfx_surface_t *gs = gfx_image_surface_create(GFX_FORMAT_ARGB32, W, H);
    gfx_t *g = gfx_create(gs);
    gfx_set_source_rgb(g, 1, 1, 1);
    gfx_paint(g);
    gfx_set_source_rgb(g, 0, 0, 0);
    sc->gfx(g);
    int gstatus = gfx_status(g);
    gfx_destroy(g);

    cairo_surface_t *cs = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
    cairo_t *cr = cairo_create(cs);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    cairo_set_source_rgb(cr, 0, 0, 0);
    sc->cairo(cr);
    cairo_destroy(cr);
    cairo_surface_flush(cs);

    const unsigned char *a = gfx_image_surface_get_data(gs);
    const unsigned char *b = cairo_image_surface_get_data(cs);
    int sa = gfx_image_surface_get_stride(gs), sb = cairo_image_surface_get_stride(cs);
    long nbig = 0;
    double sum = 0;
    int maxd = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int worst = 0;
            for (int c = 0; c < 4; c++) {
                int d = abs((int)a[y * sa + x * 4 + c] - (int)b[y * sb + x * 4 + c]);
                sum += d;
                if (d > worst)
                    worst = d;
            }
            if (worst > big)
                nbig++;
            if (worst > maxd)
                maxd = worst;
        }
    double frac = (double)nbig / (W * H), mean = sum / (W * H * 4.0);
    int ok = gstatus == GFX_STATUS_SUCCESS && frac <= sc->max_big_fraction &&
             mean <= sc->max_mean;
    printf("%-4s %-11s mean %.3f  over %d: %.4f%%  max %d%s\n", ok ? "ok" : "FAIL",
           sc->name, mean, big, frac * 100, maxd,
           gstatus ? "  (gfx status set)" : "");
    if (out_dir) {
        cairo_surface_t *wrap = cairo_image_surface_create_for_data(
            gfx_image_surface_get_data(gs), CAIRO_FORMAT_ARGB32, W, H, sa);
        write_png(out_dir, sc->name, "gfx", wrap);
        cairo_surface_destroy(wrap);
        write_png(out_dir, sc->name, "cairo", cs);
    }
    cairo_surface_destroy(cs);
    gfx_surface_destroy(gs);
    return ok;
}

/* ---- queries ----------------------------------------------------------- */

static int failures;

static void
check(int cond, const char *what)
{
    if (!cond) {
        printf("FAIL %s\n", what);
        failures++;
    }
}

static int
near(double a, double b, double tol)
{
    return fabs(a - b) <= tol;
}

static void
test_queries(void)
{
    gfx_surface_t *gs = gfx_image_surface_create(GFX_FORMAT_ARGB32, 100, 100);
    gfx_t *g = gfx_create(gs);
    cairo_surface_t *cs = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 100, 100);
    cairo_t *cr = cairo_create(cs);

    gfx_translate(g, 10, 5);
    cairo_translate(cr, 10, 5);
    gfx_scale(g, 2, 3);
    cairo_scale(cr, 2, 3);
    gfx_rectangle(g, 1, 1, 10, 10);
    cairo_rectangle(cr, 1, 1, 10, 10);
    double gx, gy, cx, cy;
    gfx_get_current_point(g, &gx, &gy);
    cairo_get_current_point(cr, &cx, &cy);
    check(near(gx, cx, 1e-9) && near(gy, cy, 1e-9), "current point after rectangle");
    check(gfx_in_fill(g, 5, 5) == cairo_in_fill(cr, 5, 5), "in_fill inside");
    check(gfx_in_fill(g, 12, 5) == cairo_in_fill(cr, 12, 5), "in_fill outside");
    gfx_set_line_width(g, 2);
    cairo_set_line_width(cr, 2);
    check(gfx_in_stroke(g, 1.5, 5) == cairo_in_stroke(cr, 1.5, 5), "in_stroke on edge");
    check(gfx_in_stroke(g, 5, 5) == cairo_in_stroke(cr, 5, 5), "in_stroke inside");

    double g1, g2, g3, g4, c1, c2, c3, c4;
    gfx_fill_extents(g, &g1, &g2, &g3, &g4);
    cairo_fill_extents(cr, &c1, &c2, &c3, &c4);
    check(near(g1, c1, 1e-6) && near(g2, c2, 1e-6) && near(g3, c3, 1e-6) &&
          near(g4, c4, 1e-6), "fill_extents");
    gfx_path_extents(g, &g1, &g2, &g3, &g4);
    cairo_path_extents(cr, &c1, &c2, &c3, &c4);
    check(near(g1, c1, 1e-6) && near(g2, c2, 1e-6) && near(g3, c3, 1e-6) &&
          near(g4, c4, 1e-6), "path_extents");

    gfx_path_t *gp = gfx_copy_path(g);
    cairo_path_t *cp = cairo_copy_path(cr);
    int same = gp->num_data == cp->num_data;
    for (int i = 0; same && i < gp->num_data;) {
        same = (int)gp->data[i].header.type == (int)cp->data[i].header.type &&
               gp->data[i].header.length == cp->data[i].header.length;
        for (int k = 1; same && k < gp->data[i].header.length; k++)
            same = near(gp->data[i + k].point.x, cp->data[i + k].point.x, 1e-9) &&
                   near(gp->data[i + k].point.y, cp->data[i + k].point.y, 1e-9);
        i += gp->data[i].header.length;
    }
    check(same, "copy_path");
    gfx_path_destroy(gp);
    cairo_path_destroy(cp);

    gfx_new_path(g);
    cairo_new_path(cr);
    gfx_rectangle(g, 0, 0, 20, 10);
    cairo_rectangle(cr, 0, 0, 20, 10);
    gfx_clip(g);
    cairo_clip(cr);
    gfx_clip_extents(g, &g1, &g2, &g3, &g4);
    cairo_clip_extents(cr, &c1, &c2, &c3, &c4);
    check(near(g1, c1, 1e-6) && near(g2, c2, 1e-6) && near(g3, c3, 1e-6) &&
          near(g4, c4, 1e-6), "clip_extents");

    gfx_matrix_t gm;
    cairo_matrix_t cm;
    gfx_get_matrix(g, &gm);
    cairo_get_matrix(cr, &cm);
    check(near(gm.xx, cm.xx, 1e-12) && near(gm.yy, cm.yy, 1e-12) &&
          near(gm.x0, cm.x0, 1e-12) && near(gm.y0, cm.y0, 1e-12), "get_matrix");
    gx = 3; gy = 4; cx = 3; cy = 4;
    gfx_user_to_device(g, &gx, &gy);
    cairo_user_to_device(cr, &cx, &cy);
    check(near(gx, cx, 1e-9) && near(gy, cy, 1e-9), "user_to_device");

    gfx_restore(g);
    check(gfx_status(g) == GFX_STATUS_INVALID_RESTORE, "unbalanced restore is an error");

    gfx_destroy(g);
    gfx_surface_destroy(gs);
    cairo_destroy(cr);
    cairo_surface_destroy(cs);
}

/* A region covers exactly the pixels of the rectangles added, once each. */
static void
test_region(void)
{
    enum { RW = 64, RH = 64 };
    static unsigned char want[RH][RW], got[RH][RW];
    memset(want, 0, sizeof want);
    memset(got, 0, sizeof got);
    gfx_region_t *r = gfx_region_create();
    uint32_t seed = 7;
    for (int i = 0; i < 40; i++) {
        seed = seed * 1103515245u + 12345u;
        int x = (int)(seed >> 8) % 50, y = (int)(seed >> 16) % 50;
        int w = 1 + (int)(seed >> 4) % 20, h = 1 + (int)(seed >> 20) % 20;
        gfx_rectangle_int_t rect = { x, y, w, h };
        gfx_region_union_rectangle(r, &rect);
        for (int yy = y; yy < y + h && yy < RH; yy++)
            for (int xx = x; xx < x + w && xx < RW; xx++)
                want[yy][xx] = 1;
    }
    int overlap = 0;
    for (int i = 0; i < gfx_region_num_rectangles(r); i++) {
        gfx_rectangle_int_t q;
        gfx_region_get_rectangle(r, i, &q);
        for (int yy = q.y; yy < q.y + q.height; yy++)
            for (int xx = q.x; xx < q.x + q.width; xx++)
                if (got[yy][xx]++)
                    overlap = 1;
    }
    check(!overlap, "region rectangles do not overlap");
    check(memcmp(want, got, sizeof want) == 0, "region covers the union");
    gfx_rectangle_int_t ext;
    gfx_region_get_extents(r, &ext);
    check(ext.width > 0 && !gfx_region_is_empty(r), "region extents");
    gfx_region_destroy(r);
}

static gfx_status_t
count_bytes(void *closure, const unsigned char *data, unsigned int len)
{
    (void)data;
    *(size_t *)closure += len;
    return GFX_STATUS_SUCCESS;
}

static void
test_png(void)
{
    gfx_surface_t *s = gfx_image_surface_create(GFX_FORMAT_ARGB32, 30, 20);
    gfx_t *g = gfx_create(s);
    gfx_set_source_rgba(g, 0.2, 0.4, 0.6, 0.5);
    gfx_paint(g);
    gfx_destroy(g);
    size_t n = 0;
    check(gfx_surface_write_to_png_stream(s, count_bytes, &n) == GFX_STATUS_SUCCESS &&
          n > 57, "PNG stream");
    gfx_surface_destroy(s);
}

int
main(void)
{
    const char *out = getenv("GFX_TEST_OUT");
    printf("gfx against Cairo %s, %dx%d\n", cairo_version_string(), W, H);
    int meshes = cairo_draws_meshes();
    for (size_t i = 0; i < sizeof scenes / sizeof scenes[0]; i++) {
        if (scenes[i].mesh && !meshes) {
            printf("skip %-11s this Cairo does not fill mesh patches\n",
                   scenes[i].name);
            continue;
        }
        if (!run_scene(&scenes[i], out && *out ? out : NULL))
            failures++;
    }
    test_queries();
    test_region();
    test_png();
    if (failures)
        printf("%d failure(s)\n", failures);
    else
        printf("all passed\n");
    return failures ? 1 : 0;
}
