/* Weststar SDL Browser — gfx benchmark: page-like workloads timed with gfx and with Cairo.
 * Prints the best of several runs of each workload, in milliseconds, and
 * the ratio gfx / Cairo. Run with `meson test -C builddir --benchmark`
 * or directly; GFX_BENCH_RUNS=<n> sets the number of runs (default 5).
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
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define BW 1280
#define BH 800

#define P(name) gfx_##name
#define E(name) GFX_##name
#define CTX gfx_t
#define SURF gfx_surface_t
#define PAT gfx_pattern_t
#define BENCH(name) gfx_bench_##name
#include "bench.h"
#undef P
#undef E
#undef CTX
#undef SURF
#undef PAT
#undef BENCH

#define P(name) cairo_##name
#define E(name) CAIRO_##name
#define CTX cairo_t
#define SURF cairo_surface_t
#define PAT cairo_pattern_t
#define BENCH(name) cairo_bench_##name
#include "bench.h"
#undef P
#undef E
#undef CTX
#undef SURF
#undef PAT
#undef BENCH

typedef struct {
    const char *name;
    void (*gfx)(gfx_t *, gfx_surface_t *);
    void (*cairo)(cairo_t *, cairo_surface_t *);
} bench;

#define B(n) { #n, gfx_bench_##n, cairo_bench_##n }

static const bench benches[] = {
    B(fill_page), B(alpha_page), B(rects_aligned), B(rounded_boxes),
    B(thin_lines), B(circles), B(image_1to1), B(image_scaled),
    B(image_shrunk), B(gradients), B(groups), B(rounded_clips),
};

static double
now_ms(void)
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

/* A 512 x 512 photo-like test image: gradients with some noise. */
static void
fill_image(unsigned char *data, int stride)
{
    uint32_t seed = 12345;
    for (int y = 0; y < 512; y++)
        for (int x = 0; x < 512; x++) {
            seed = seed * 1103515245u + 12345u;
            unsigned n = (seed >> 16) & 31;
            unsigned r = (x / 2 + n) & 255, g = (y / 2 + n) & 255,
                     b = ((x + y) / 4) & 255;
            uint32_t v = 0xff000000u | r << 16 | g << 8 | b;
            memcpy(data + (size_t)y * stride + (size_t)x * 4, &v, 4);
        }
}

int
main(void)
{
    const char *env = getenv("GFX_BENCH_RUNS");
    int runs = env ? atoi(env) : 5;
    if (runs < 1)
        runs = 1;

    gfx_surface_t *gimg = gfx_image_surface_create(GFX_FORMAT_ARGB32, 512, 512);
    fill_image(gfx_image_surface_get_data(gimg), gfx_image_surface_get_stride(gimg));
    cairo_surface_t *cimg = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 512, 512);
    cairo_surface_flush(cimg);
    fill_image(cairo_image_surface_get_data(cimg), cairo_image_surface_get_stride(cimg));
    cairo_surface_mark_dirty(cimg);

    printf("gfx against Cairo %s, %dx%d, best of %d runs\n",
           cairo_version_string(), BW, BH, runs);
    printf("%-14s %10s %10s %8s\n", "workload", "gfx ms", "cairo ms", "ratio");
    double tg = 0, tc = 0;
    for (size_t i = 0; i < sizeof benches / sizeof benches[0]; i++) {
        double bg = INFINITY, bc = INFINITY;
        for (int r = 0; r < runs; r++) {
            gfx_surface_t *gs = gfx_image_surface_create(GFX_FORMAT_ARGB32, BW, BH);
            gfx_t *g = gfx_create(gs);
            double t0 = now_ms();
            benches[i].gfx(g, gimg);
            double t1 = now_ms();
            gfx_destroy(g);
            gfx_surface_destroy(gs);
            bg = fmin(bg, t1 - t0);

            cairo_surface_t *cs = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, BW, BH);
            cairo_t *cr = cairo_create(cs);
            t0 = now_ms();
            benches[i].cairo(cr, cimg);
            cairo_surface_flush(cs);
            t1 = now_ms();
            cairo_destroy(cr);
            cairo_surface_destroy(cs);
            bc = fmin(bc, t1 - t0);
        }
        tg += bg;
        tc += bc;
        printf("%-14s %10.2f %10.2f %7.2fx\n", benches[i].name, bg, bc,
               bc > 0 ? bg / bc : 0);
    }
    printf("%-14s %10.2f %10.2f %7.2fx\n", "total", tg, tc, tc > 0 ? tg / tc : 0);
    gfx_surface_destroy(gimg);
    cairo_surface_destroy(cimg);
    return 0;
}
