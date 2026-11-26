/* Weststar SDL Browser — gfx benchmark workloads.
 * Included by gfx-bench.c with the same macros as scenes.h. Each
 * workload draws something a web page's painting does a lot of, into a
 * BW x BH surface.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Page backgrounds: opaque solid fills of the whole surface. */
static void
BENCH(fill_page)(CTX *cr, SURF *img)
{
    (void)img;
    for (int i = 0; i < 20; i++) {
        P(set_source_rgb)(cr, 0.1 * (i % 10), 0.5, 0.9);
        P(paint)(cr);
    }
}

/* Translucent overlays over the whole surface. */
static void
BENCH(alpha_page)(CTX *cr, SURF *img)
{
    (void)img;
    for (int i = 0; i < 10; i++) {
        P(set_source_rgba)(cr, 0.2, 0.3, 0.1 * (i % 10), 0.4);
        P(paint)(cr);
    }
}

/* Block boxes: pixel-aligned rectangles of many sizes. */
static void
BENCH(rects_aligned)(CTX *cr, SURF *img)
{
    (void)img;
    for (int i = 0; i < 3000; i++) {
        int x = (i * 37) % (BW - 200), y = (i * 53) % (BH - 120);
        P(set_source_rgb)(cr, (i % 7) / 7.0, (i % 5) / 5.0, (i % 3) / 3.0);
        P(rectangle)(cr, x, y, 20 + i % 180, 10 + i % 110);
        P(fill)(cr);
    }
}

/* Buttons and cards: rounded rectangles with a one-pixel border. */
static void
BENCH(rounded_boxes)(CTX *cr, SURF *img)
{
    (void)img;
    P(set_line_width)(cr, 1);
    for (int i = 0; i < 800; i++) {
        double x = (i * 41) % (BW - 160) + 0.5, y = (i * 29) % (BH - 60) + 0.5;
        double w = 60 + i % 100, h = 24 + i % 30, r = 6;
        P(new_sub_path)(cr);
        P(arc)(cr, x + w - r, y + r, r, -M_PI / 2, 0);
        P(arc)(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
        P(arc)(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
        P(arc)(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
        P(close_path)(cr);
        P(set_source_rgb)(cr, 0.9, 0.92, 0.95);
        P(fill_preserve)(cr);
        P(set_source_rgb)(cr, 0.6, 0.62, 0.66);
        P(stroke)(cr);
    }
}

/* Underlines, rules and borders: short thin lines. */
static void
BENCH(thin_lines)(CTX *cr, SURF *img)
{
    (void)img;
    P(set_line_width)(cr, 1);
    P(set_source_rgb)(cr, 0.2, 0.2, 0.25);
    for (int i = 0; i < 4000; i++) {
        double x = (i * 13) % (BW - 100) + 0.5, y = (i * 7) % (BH - 10) + 0.5;
        P(move_to)(cr, x, y);
        P(line_to)(cr, x + 20 + i % 80, y + (i % 3));
        P(stroke)(cr);
    }
}

/* Antialiased circles (bullets, avatars, icons). */
static void
BENCH(circles)(CTX *cr, SURF *img)
{
    (void)img;
    for (int i = 0; i < 1500; i++) {
        double x = (i * 31) % BW, y = (i * 17) % BH;
        P(arc)(cr, x, y, 3 + i % 24, 0, 2 * M_PI);
        P(set_source_rgba)(cr, (i % 4) / 4.0, 0.3, 0.6, 0.8);
        P(fill)(cr);
    }
}

/* Images at their natural size, at whole-pixel positions. */
static void
BENCH(image_1to1)(CTX *cr, SURF *img)
{
    for (int i = 0; i < 30; i++) {
        P(set_source_surface)(cr, img, (i * 97) % (BW - 512), (i * 61) % (BH - 300));
        P(paint)(cr);
    }
}

/* Images scaled, bilinear (responsive images). */
static void
BENCH(image_scaled)(CTX *cr, SURF *img)
{
    for (int i = 0; i < 8; i++) {
        P(save)(cr);
        P(translate)(cr, (i * 50) % 300, (i * 30) % 100);
        P(scale)(cr, 1.37, 1.37);
        P(set_source_surface)(cr, img, 0, 0);
        P(pattern_set_filter)(P(get_source)(cr), E(FILTER_BILINEAR));
        P(rectangle)(cr, 0, 0, 512, 512);
        P(fill)(cr);
        P(restore)(cr);
    }
}

/* Images shrunk (thumbnails), with the good filter. */
static void
BENCH(image_shrunk)(CTX *cr, SURF *img)
{
    for (int i = 0; i < 40; i++) {
        P(save)(cr);
        P(translate)(cr, (i * 130) % (BW - 200), (i * 90) % (BH - 200));
        P(scale)(cr, 0.3, 0.3);
        P(set_source_surface)(cr, img, 0, 0);
        P(pattern_set_filter)(P(get_source)(cr), E(FILTER_GOOD));
        P(paint)(cr);
        P(restore)(cr);
    }
}

/* CSS gradients filling large boxes. */
static void
BENCH(gradients)(CTX *cr, SURF *img)
{
    (void)img;
    for (int i = 0; i < 6; i++) {
        PAT *p = i % 2 ? P(pattern_create_radial)(BW / 2.0, BH / 2.0, 10, BW / 2.0, BH / 2.0, BW / 2.0)
                       : P(pattern_create_linear)(0, 0, BW, BH);
        P(pattern_add_color_stop_rgb)(p, 0, 1, 0.8, 0.2);
        P(pattern_add_color_stop_rgb)(p, 0.5, 0.2, 0.6, 0.9);
        P(pattern_add_color_stop_rgb)(p, 1, 0.1, 0.1, 0.3);
        P(set_source)(cr, p);
        P(paint)(cr);
        P(pattern_destroy)(p);
    }
}

/* Opacity layers: a group drawn under a clip, painted back with alpha. */
static void
BENCH(groups)(CTX *cr, SURF *img)
{
    (void)img;
    for (int i = 0; i < 20; i++) {
        P(save)(cr);
        P(rectangle)(cr, (i * 40) % 600, (i * 25) % 300, 600, 400);
        P(clip)(cr);
        P(push_group)(cr);
        P(set_source_rgb)(cr, 0.3, 0.5, 0.2);
        P(paint)(cr);
        P(arc)(cr, 300 + i * 10, 300, 120, 0, 2 * M_PI);
        P(set_source_rgb)(cr, 0.9, 0.2, 0.2);
        P(fill)(cr);
        P(pop_group_to_source)(cr);
        P(paint_with_alpha)(cr, 0.6);
        P(restore)(cr);
    }
}

/* Rounded clips (border-radius with overflow: hidden) around fills. */
static void
BENCH(rounded_clips)(CTX *cr, SURF *img)
{
    (void)img;
    for (int i = 0; i < 60; i++) {
        double x = (i * 71) % (BW - 300), y = (i * 47) % (BH - 200);
        P(save)(cr);
        P(arc)(cr, x + 100, y + 100, 100, 0, 2 * M_PI);
        P(clip)(cr);
        P(set_source_rgb)(cr, 0.4, 0.4, (i % 5) / 5.0);
        P(rectangle)(cr, x, y, 300, 200);
        P(fill)(cr);
        P(restore)(cr);
    }
}
