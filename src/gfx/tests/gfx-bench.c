/* Weststar SDL Browser — gfx benchmark: page-like workloads, timed.
 * Prints the best of several runs of each workload in milliseconds. Run
 * with `meson test -C builddir --benchmark --verbose` or directly;
 * GFX_BENCH_RUNS=<n> sets the number of runs (default 5). Earlier results,
 * when gfx was timed against Cairo, are in docs/porting-to-sdl.md.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "gfx.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(_WIN32) && !defined(_UCRT)
#include <windows.h>
#endif

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

typedef struct {
    const char *name;
    void (*gfx)(gfx_t *, gfx_surface_t *);
} bench;

#define B(n) { #n, gfx_bench_##n }

static const bench benches[] = {
    B(fill_page), B(alpha_page), B(rects_aligned), B(rounded_boxes),
    B(thin_lines), B(circles), B(image_1to1), B(image_scaled),
    B(image_shrunk), B(gradients), B(groups), B(rounded_clips),
};

static double
now_ms(void)
{
#if defined(_WIN32) && !defined(_UCRT)
    /* msvcrt.dll, Windows XP's C library, has no timespec_get. */
    LARGE_INTEGER freq, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart * 1000.0 / (double)freq.QuadPart;
#else
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
#endif
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

    gfx_surface_t *img = gfx_image_surface_create(GFX_FORMAT_ARGB32, 512, 512);
    fill_image(gfx_image_surface_get_data(img), gfx_image_surface_get_stride(img));
    gfx_surface_mark_dirty(img);

    printf("gfx, %dx%d, best of %d runs\n", BW, BH, runs);
    printf("%-14s %10s\n", "workload", "ms");
    double total = 0;
    for (size_t i = 0; i < sizeof benches / sizeof benches[0]; i++) {
        double best = INFINITY;
        for (int r = 0; r < runs; r++) {
            gfx_surface_t *s = gfx_image_surface_create(GFX_FORMAT_ARGB32, BW, BH);
            gfx_t *g = gfx_create(s);
            double t0 = now_ms();
            benches[i].gfx(g, img);
            double t1 = now_ms();
            gfx_destroy(g);
            gfx_surface_destroy(s);
            best = fmin(best, t1 - t0);
        }
        total += best;
        printf("%-14s %10.2f\n", benches[i].name, best);
    }
    printf("%-14s %10.2f\n", "total", total);
    gfx_surface_destroy(img);
    return 0;
}
