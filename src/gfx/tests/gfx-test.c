/* Weststar SDL Browser — gfx tests.
 *
 * Every scene in scenes.h is drawn and compared with its reference image in
 * tests/ref/ (zlib-compressed ARGB). The references were made by gfx while
 * it was still checked against Cairo scene by scene, so a difference means
 * gfx's output changed. After an intended change, regenerate them with
 * GFX_TEST_WRITE_REF=1 and look at the new images (GFX_TEST_OUT=<dir>
 * writes each scene as a PNG). Queries (in_fill, extents, the current
 * point, copy_path), regions and PNG output are checked against fixed
 * values.
 *
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "gfx.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef GFX_REF_DIR
#error "GFX_REF_DIR must name the reference image directory"
#endif

#define W 250
#define H 250

#define P(name) gfx_##name
#define E(name) GFX_##name
#define CTX gfx_t
#define SURF gfx_surface_t
#define PAT gfx_pattern_t
#define PATH gfx_path_t
#define SCENE(name) gfx_scene_##name
#include "scenes.h"

typedef struct {
    const char *name;
    void (*draw)(gfx_t *);
} scene;

#define S(n) { #n, gfx_scene_##n }

static const scene scenes[] = {
    S(fills), S(arcs), S(strokes), S(hairlines), S(dashes), S(transforms),
    S(linear), S(radial), S(images), S(clips), S(groups), S(masks),
    S(operators), S(mesh), S(a8), S(paths),
};

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

/* ---- reference images -------------------------------------------------- */

/* "GFXR", width, height (little-endian 32-bit), then width x height ARGB
 * pixels as little-endian 32-bit words, the whole deflated. */
static void
put_le32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static uint32_t
get_le32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
           (uint32_t)p[3] << 24;
}

static char *
ref_path(const char *name)
{
    size_t n = strlen(GFX_REF_DIR) + strlen(name) + 16;
    char *p = malloc(n);
    if (p)
        snprintf(p, n, "%s/%s.argb.z", GFX_REF_DIR, name);
    return p;
}

static int
write_ref(const char *name, gfx_surface_t *s)
{
    size_t raw_len = 12 + (size_t)W * H * 4;
    unsigned char *raw = malloc(raw_len);
    uLongf zlen = compressBound((uLong)raw_len);
    unsigned char *z = malloc(zlen);
    char *path = ref_path(name);
    int ok = 0;
    if (raw && z && path) {
        memcpy(raw, "GFXR", 4);
        put_le32(raw + 4, W);
        put_le32(raw + 8, H);
        const unsigned char *d = gfx_image_surface_get_data(s);
        int stride = gfx_image_surface_get_stride(s);
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                uint32_t px;
                memcpy(&px, d + y * stride + x * 4, 4);
                put_le32(raw + 12 + ((size_t)y * W + x) * 4, px);
            }
        if (compress2(z, &zlen, raw, (uLong)raw_len, 9) == Z_OK) {
            FILE *f = fopen(path, "wb");
            if (f) {
                ok = fwrite(z, 1, zlen, f) == zlen;
                ok = fclose(f) == 0 && ok;
            }
        }
    }
    free(raw);
    free(z);
    free(path);
    return ok;
}

/* The reference pixels, W x H native words; NULL when missing. */
static uint32_t *
read_ref(const char *name)
{
    char *path = ref_path(name);
    FILE *f = path ? fopen(path, "rb") : NULL;
    free(path);
    if (!f)
        return NULL;
    unsigned char *z = NULL;
    size_t zlen = 0, cap = 0;
    for (;;) {
        if (zlen == cap) {
            cap = cap ? cap * 2 : 65536;
            unsigned char *nz = realloc(z, cap);
            if (!nz)
                break;
            z = nz;
        }
        size_t got = fread(z + zlen, 1, cap - zlen, f);
        if (got == 0)
            break;
        zlen += got;
    }
    fclose(f);
    uLongf raw_len = 12 + (uLongf)W * H * 4;
    unsigned char *raw = malloc(raw_len);
    uint32_t *px = malloc((size_t)W * H * 4);
    int ok = z && raw && px &&
             uncompress(raw, &raw_len, z, (uLong)zlen) == Z_OK &&
             raw_len == 12 + (uLongf)W * H * 4 && memcmp(raw, "GFXR", 4) == 0 &&
             get_le32(raw + 4) == W && get_le32(raw + 8) == H;
    if (ok)
        for (size_t i = 0; i < (size_t)W * H; i++)
            px[i] = get_le32(raw + 12 + i * 4);
    free(z);
    free(raw);
    if (!ok) {
        free(px);
        return NULL;
    }
    return px;
}

static void
write_png(const char *dir, const char *name, gfx_surface_t *s)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s.png", dir, name);
    gfx_surface_write_to_png(s, path);
}

/* Draws a scene and compares it with its reference. A pixel counts as
 * different when a channel is off by more than 2: room for floating-point
 * results that differ between compilers and CPUs, not for any visible
 * change. */
static int
run_scene(const scene *sc, int write, const char *out_dir)
{
    gfx_surface_t *s = gfx_image_surface_create(GFX_FORMAT_ARGB32, W, H);
    gfx_t *g = gfx_create(s);
    gfx_set_source_rgb(g, 1, 1, 1);
    gfx_paint(g);
    gfx_set_source_rgb(g, 0, 0, 0);
    sc->draw(g);
    int status = gfx_status(g);
    gfx_destroy(g);
    if (out_dir)
        write_png(out_dir, sc->name, s);
    if (write) {
        int ok = write_ref(sc->name, s);
        printf("%-4s %-11s reference written\n", ok ? "ok" : "FAIL", sc->name);
        gfx_surface_destroy(s);
        return ok;
    }
    uint32_t *ref = read_ref(sc->name);
    if (!ref) {
        printf("FAIL %-11s no reference image in %s\n", sc->name, GFX_REF_DIR);
        gfx_surface_destroy(s);
        return 0;
    }
    const unsigned char *d = gfx_image_surface_get_data(s);
    int stride = gfx_image_surface_get_stride(s);
    long ndiff = 0;
    int maxd = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            uint32_t a, b = ref[y * W + x];
            memcpy(&a, d + y * stride + x * 4, 4);
            int worst = 0;
            for (int sh = 0; sh < 32; sh += 8) {
                int dd = abs((int)((a >> sh) & 0xff) - (int)((b >> sh) & 0xff));
                if (dd > worst)
                    worst = dd;
            }
            if (worst > 2)
                ndiff++;
            if (worst > maxd)
                maxd = worst;
        }
    free(ref);
    /* A few pixels may land differently where rounding decides. */
    int ok = status == GFX_STATUS_SUCCESS && ndiff <= 20;
    printf("%-4s %-11s %ld px differ, max %d%s\n", ok ? "ok" : "FAIL",
           sc->name, ndiff, maxd, status ? "  (gfx status set)" : "");
    gfx_surface_destroy(s);
    return ok;
}

/* ---- queries ----------------------------------------------------------- */

static void
test_queries(void)
{
    gfx_surface_t *s = gfx_image_surface_create(GFX_FORMAT_ARGB32, 100, 100);
    gfx_t *g = gfx_create(s);

    gfx_translate(g, 10, 5);
    gfx_scale(g, 2, 3);
    gfx_rectangle(g, 1, 1, 10, 10);
    double x, y;
    gfx_get_current_point(g, &x, &y);
    check(near(x, 1, 1e-9) && near(y, 1, 1e-9), "current point after rectangle");
    check(gfx_in_fill(g, 5, 5), "in_fill inside");
    check(!gfx_in_fill(g, 12, 5), "in_fill outside");
    gfx_set_line_width(g, 2);
    check(gfx_in_stroke(g, 1.5, 5), "in_stroke on the edge");
    check(!gfx_in_stroke(g, 5, 5), "in_stroke inside");

    double a, b, c, d;
    gfx_fill_extents(g, &a, &b, &c, &d);
    check(near(a, 1, 1e-6) && near(b, 1, 1e-6) && near(c, 11, 1e-6) &&
          near(d, 11, 1e-6), "fill_extents");
    gfx_path_extents(g, &a, &b, &c, &d);
    check(near(a, 1, 1e-6) && near(b, 1, 1e-6) && near(c, 11, 1e-6) &&
          near(d, 11, 1e-6), "path_extents");

    /* As Cairo: move, three lines, close, and the move close_path adds. */
    static const struct { int type; double x, y; } want[] = {
        { GFX_PATH_MOVE_TO, 1, 1 },   { GFX_PATH_LINE_TO, 11, 1 },
        { GFX_PATH_LINE_TO, 11, 11 }, { GFX_PATH_LINE_TO, 1, 11 },
        { GFX_PATH_CLOSE_PATH, 0, 0 }, { GFX_PATH_MOVE_TO, 1, 1 },
    };
    gfx_path_t *p = gfx_copy_path(g);
    int same = 1, k = 0;
    for (int i = 0; i < p->num_data && same; i += p->data[i].header.length, k++) {
        same = k < 6 && (int)p->data[i].header.type == want[k].type;
        if (same && p->data[i].header.length > 1)
            same = near(p->data[i + 1].point.x, want[k].x, 1e-9) &&
                   near(p->data[i + 1].point.y, want[k].y, 1e-9);
    }
    check(same && k == 6, "copy_path");
    gfx_path_destroy(p);

    gfx_new_path(g);
    gfx_rectangle(g, 0, 0, 20, 10);
    gfx_clip(g);
    gfx_clip_extents(g, &a, &b, &c, &d);
    check(near(a, 0, 1e-6) && near(b, 0, 1e-6) && near(c, 20, 1e-6) &&
          near(d, 10, 1e-6), "clip_extents");

    gfx_matrix_t m;
    gfx_get_matrix(g, &m);
    check(m.xx == 2 && m.yy == 3 && m.x0 == 10 && m.y0 == 5 && m.xy == 0 &&
          m.yx == 0, "get_matrix");
    x = 3;
    y = 4;
    gfx_user_to_device(g, &x, &y);
    check(near(x, 16, 1e-9) && near(y, 17, 1e-9), "user_to_device");

    gfx_restore(g);
    check(gfx_status(g) == GFX_STATUS_INVALID_RESTORE,
          "unbalanced restore is an error");

    gfx_destroy(g);
    gfx_surface_destroy(s);
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
    const char *wr = getenv("GFX_TEST_WRITE_REF");
    int write = wr && *wr && strcmp(wr, "0") != 0;
    printf("gfx scenes against %s, %dx%d\n", GFX_REF_DIR, W, H);
    for (size_t i = 0; i < sizeof scenes / sizeof scenes[0]; i++)
        if (!run_scene(&scenes[i], write, out && *out ? out : NULL))
            failures++;
    test_queries();
    test_region();
    test_png();
    if (failures)
        printf("%d failure(s)\n", failures);
    else
        printf("all passed\n");
    return failures ? 1 : 0;
}
