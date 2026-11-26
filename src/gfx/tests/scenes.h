/* Weststar SDL Browser — gfx test scenes, drawn once with gfx and once with Cairo.
 * Included twice by gfx-test.c with P(name) naming a function (gfx_name or
 * cairo_name), E(NAME) an enumerator (GFX_NAME or CAIRO_NAME), CTX the
 * context type, SURF the surface type and PAT the pattern type.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

static void
SCENE(fills)(CTX *cr)
{
    P(set_source_rgb)(cr, 0.9, 0.2, 0.1);
    P(rectangle)(cr, 10, 10, 80, 60);
    P(fill)(cr);
    P(set_source_rgba)(cr, 0.1, 0.4, 0.9, 0.5);
    P(rectangle)(cr, 40.5, 30.25, 90.3, 70.7);
    P(fill)(cr);
    /* A five-pointed star, both fill rules. */
    for (int k = 0; k < 2; k++) {
        P(new_path)(cr);
        for (int i = 0; i < 5; i++) {
            double a = -M_PI / 2 + i * 4 * M_PI / 5;
            double x = 60 + 140 * k + 45 * cos(a), y = 150 + 45 * sin(a);
            if (i == 0)
                P(move_to)(cr, x, y);
            else
                P(line_to)(cr, x, y);
        }
        P(close_path)(cr);
        P(set_fill_rule)(cr, k ? E(FILL_RULE_EVEN_ODD) : E(FILL_RULE_WINDING));
        P(set_source_rgb)(cr, 0.2, 0.6, 0.2);
        P(fill)(cr);
    }
    /* A triangle with a hole wound the other way. */
    P(move_to)(cr, 150, 10);
    P(line_to)(cr, 240, 10);
    P(line_to)(cr, 195, 100);
    P(close_path)(cr);
    P(move_to)(cr, 175, 25);
    P(line_to)(cr, 195, 65);
    P(line_to)(cr, 215, 25);
    P(close_path)(cr);
    P(set_fill_rule)(cr, E(FILL_RULE_WINDING));
    P(set_source_rgb)(cr, 0.5, 0.1, 0.6);
    P(fill)(cr);
}

static void
SCENE(arcs)(CTX *cr)
{
    P(set_source_rgb)(cr, 0.1, 0.3, 0.7);
    P(arc)(cr, 60, 60, 45, 0, 2 * M_PI);
    P(fill)(cr);
    P(set_source_rgb)(cr, 0.8, 0.5, 0.1);
    P(move_to)(cr, 180, 60);
    P(arc)(cr, 180, 60, 50, 0.3, 2.2);
    P(close_path)(cr);
    P(fill)(cr);
    P(set_source_rgb)(cr, 0.3, 0.6, 0.3);
    P(arc_negative)(cr, 60, 180, 40, 0, -2.5);
    P(close_path)(cr);
    P(fill)(cr);
    /* A rounded rectangle, as the chrome draws them. */
    double x = 130, y = 140, w = 100, h = 70, r = 12;
    P(new_sub_path)(cr);
    P(arc)(cr, x + w - r, y + r, r, -M_PI / 2, 0);
    P(arc)(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    P(arc)(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    P(arc)(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
    P(close_path)(cr);
    P(set_source_rgba)(cr, 0.6, 0.1, 0.4, 0.8);
    P(fill)(cr);
}

static void
SCENE(strokes)(CTX *cr)
{
    int joins[3] = { E(LINE_JOIN_MITER), E(LINE_JOIN_ROUND), E(LINE_JOIN_BEVEL) };
    int caps[3] = { E(LINE_CAP_BUTT), E(LINE_CAP_ROUND), E(LINE_CAP_SQUARE) };
    P(set_line_width)(cr, 12);
    for (int i = 0; i < 3; i++) {
        P(set_line_join)(cr, joins[i]);
        P(set_line_cap)(cr, caps[i]);
        P(move_to)(cr, 20 + i * 80, 100);
        P(line_to)(cr, 45 + i * 80, 25);
        P(line_to)(cr, 70 + i * 80, 100);
        P(set_source_rgb)(cr, 0.2 * i, 0.3, 0.8 - 0.2 * i);
        P(stroke)(cr);
    }
    /* A closed zig-zag with sharp turns past the miter limit. */
    P(set_line_join)(cr, E(LINE_JOIN_MITER));
    P(set_miter_limit)(cr, 4);
    P(set_line_width)(cr, 6);
    P(move_to)(cr, 20, 150);
    P(line_to)(cr, 120, 140);
    P(line_to)(cr, 30, 175);
    P(line_to)(cr, 140, 220);
    P(close_path)(cr);
    P(set_source_rgb)(cr, 0.7, 0.2, 0.2);
    P(stroke)(cr);
    /* Curves, thin and thick. */
    P(set_line_cap)(cr, E(LINE_CAP_ROUND));
    P(set_line_width)(cr, 1);
    P(move_to)(cr, 150, 130);
    P(curve_to)(cr, 260, 120, 140, 240, 250, 240);
    P(set_source_rgb)(cr, 0, 0, 0);
    P(stroke)(cr);
    P(set_line_width)(cr, 9);
    P(move_to)(cr, 160, 150);
    P(curve_to)(cr, 240, 150, 170, 220, 235, 200);
    P(set_source_rgba)(cr, 0.1, 0.5, 0.2, 0.7);
    P(stroke)(cr);
}

static void
SCENE(hairlines)(CTX *cr)
{
    /* The chrome's one-pixel lines and borders at half-pixel positions. */
    P(set_line_width)(cr, 1);
    P(set_source_rgb)(cr, 0.3, 0.3, 0.35);
    for (int i = 0; i < 10; i++) {
        P(move_to)(cr, 10.5, 10.5 + i * 7);
        P(line_to)(cr, 240.5, 10.5 + i * 7 + i * 0.7);
    }
    P(stroke)(cr);
    P(rectangle)(cr, 20.5, 100.5, 100, 40);
    P(stroke)(cr);
    P(set_line_width)(cr, 2);
    P(rectangle)(cr, 140, 100, 90, 40);
    P(stroke)(cr);
    P(set_line_width)(cr, 0.5);
    P(arc)(cr, 70, 200, 40, 0, 2 * M_PI);
    P(stroke)(cr);
}

static void
SCENE(dashes)(CTX *cr)
{
    double d1[] = { 20, 10 };
    double d2[] = { 0, 14 };
    double d3[] = { 30, 6, 6, 6 };
    P(set_line_width)(cr, 8);
    P(set_source_rgb)(cr, 0.2, 0.2, 0.6);
    P(set_dash)(cr, d1, 2, 0);
    P(move_to)(cr, 20, 30);
    P(line_to)(cr, 230, 30);
    P(stroke)(cr);
    P(set_line_cap)(cr, E(LINE_CAP_ROUND));
    P(set_dash)(cr, d2, 2, 0);
    P(move_to)(cr, 20, 70);
    P(line_to)(cr, 230, 70);
    P(stroke)(cr);
    P(set_line_cap)(cr, E(LINE_CAP_BUTT));
    P(set_dash)(cr, d3, 4, 11);
    P(rectangle)(cr, 30, 110, 190, 110);
    P(stroke)(cr);
    P(set_dash)(cr, d1, 1, 5);
    P(set_line_width)(cr, 3);
    P(arc)(cr, 125, 165, 30, 0, 2 * M_PI);
    P(stroke)(cr);
}

static void
SCENE(transforms)(CTX *cr)
{
    P(save)(cr);
    P(translate)(cr, 70, 70);
    P(scale)(cr, 3, 1);
    P(arc)(cr, 0, 0, 18, 0, 2 * M_PI);
    P(restore)(cr);
    P(set_line_width)(cr, 6);
    P(set_source_rgb)(cr, 0.2, 0.5, 0.8);
    P(stroke)(cr);
    /* The pen itself stretched: stroke under a non-uniform scale. */
    P(save)(cr);
    P(translate)(cr, 180, 70);
    P(scale)(cr, 1, 3);
    P(arc)(cr, 0, 0, 15, 0, 2 * M_PI);
    P(set_line_width)(cr, 4);
    P(set_source_rgb)(cr, 0.8, 0.3, 0.2);
    P(stroke)(cr);
    P(restore)(cr);
    P(save)(cr);
    P(translate)(cr, 125, 180);
    P(rotate)(cr, 0.5);
    P(rectangle)(cr, -50, -25, 100, 50);
    P(set_source_rgb)(cr, 0.3, 0.7, 0.3);
    P(fill_preserve)(cr);
    P(set_source_rgb)(cr, 0, 0, 0);
    P(set_line_width)(cr, 3);
    P(stroke)(cr);
    P(restore)(cr);
}

static void
SCENE(linear)(CTX *cr)
{
    PAT *p = P(pattern_create_linear)(20, 0, 230, 0);
    P(pattern_add_color_stop_rgb)(p, 0, 1, 0, 0);
    P(pattern_add_color_stop_rgb)(p, 0.5, 0, 1, 0);
    P(pattern_add_color_stop_rgb)(p, 1, 0, 0, 1);
    P(set_source)(cr, p);
    P(rectangle)(cr, 0, 0, 250, 60);
    P(fill)(cr);
    P(pattern_destroy)(p);
    /* Off the pixel grid: exact ties at the seams may go either way. */
    p = P(pattern_create_linear)(40.3, 80.1, 80.3, 120.1);
    P(pattern_add_color_stop_rgb)(p, 0, 1, 1, 0);
    P(pattern_add_color_stop_rgb)(p, 1, 0, 0.5, 0.5);
    P(pattern_set_extend)(p, E(EXTEND_REPEAT));
    P(set_source)(cr, p);
    P(rectangle)(cr, 0, 70, 120, 180);
    P(fill)(cr);
    P(pattern_set_extend)(p, E(EXTEND_REFLECT));
    P(rectangle)(cr, 130, 70, 120, 180);
    P(fill)(cr);
    P(pattern_destroy)(p);
}

static void
SCENE(radial)(CTX *cr)
{
    PAT *p = P(pattern_create_radial)(70, 70, 5, 70, 70, 60);
    P(pattern_add_color_stop_rgb)(p, 0, 1, 1, 1);
    P(pattern_add_color_stop_rgb)(p, 1, 0.1, 0.2, 0.6);
    P(set_source)(cr, p);
    P(rectangle)(cr, 0, 0, 140, 140);
    P(fill)(cr);
    P(pattern_destroy)(p);
    p = P(pattern_create_radial)(170, 160, 0, 190, 180, 70);
    P(pattern_add_color_stop_rgb)(p, 0, 1, 0.8, 0);
    P(pattern_add_color_stop_rgb)(p, 0.6, 0.8, 0.1, 0.1);
    P(pattern_add_color_stop_rgb)(p, 1, 0.1, 0, 0.2);
    P(set_source)(cr, p);
    P(rectangle)(cr, 110, 100, 140, 150);
    P(fill)(cr);
    P(pattern_destroy)(p);
}

/* A 16 x 16 checkerboard with a translucent corner, as an image. */
static SURF *
SCENE(checker)(void)
{
    SURF *s = P(image_surface_create)(E(FORMAT_ARGB32), 16, 16);
    unsigned char *d = P(image_surface_get_data)(s);
    int stride = P(image_surface_get_stride)(s);
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            uint32_t v = ((x / 4 + y / 4) % 2) ? 0xff2060c0u : 0xfff0e0a0u;
            if (x >= 12 && y >= 12)
                v = 0x80400000u;
            memcpy(d + y * stride + x * 4, &v, 4);
        }
    P(surface_mark_dirty)(s);
    return s;
}

static void
SCENE(images)(CTX *cr)
{
    SURF *s = SCENE(checker)();
    P(set_source_surface)(cr, s, 10, 10);
    P(paint)(cr);
    P(save)(cr);
    P(translate)(cr, 40, 10);
    P(scale)(cr, 4, 4);
    P(set_source_surface)(cr, s, 0, 0);
    P(pattern_set_filter)(P(get_source)(cr), E(FILTER_NEAREST));
    P(rectangle)(cr, 0, 0, 16, 16);
    P(fill)(cr);
    P(restore)(cr);
    P(save)(cr);
    P(translate)(cr, 120, 10);
    P(scale)(cr, 4, 4);
    P(set_source_surface)(cr, s, 0, 0);
    P(pattern_set_filter)(P(get_source)(cr), E(FILTER_BILINEAR));
    P(rectangle)(cr, 0, 0, 16, 16);
    P(fill)(cr);
    P(restore)(cr);
    P(save)(cr);
    P(translate)(cr, 70, 150);
    P(rotate)(cr, 0.4);
    P(scale)(cr, 3, 3);
    P(set_source_surface)(cr, s, -8, -8);
    P(pattern_set_extend)(P(get_source)(cr), E(EXTEND_REPEAT));
    P(pattern_set_filter)(P(get_source)(cr), E(FILTER_BILINEAR));
    P(rectangle)(cr, -16, -16, 32, 32);
    P(fill)(cr);
    P(restore)(cr);
    P(surface_destroy)(s);
}

static void
SCENE(clips)(CTX *cr)
{
    P(save)(cr);
    P(rectangle)(cr, 20, 20, 120, 120);
    P(clip)(cr);
    P(arc)(cr, 100, 100, 70, 0, 2 * M_PI);
    P(clip)(cr);
    P(set_source_rgb)(cr, 0.8, 0.2, 0.3);
    P(paint)(cr);
    P(restore)(cr);
    P(save)(cr);
    P(rectangle)(cr, 150.5, 30.5, 80, 180);
    P(clip)(cr);
    P(set_source_rgb)(cr, 0.2, 0.5, 0.8);
    P(rectangle)(cr, 100, 0, 200, 100);
    P(fill)(cr);
    P(set_source_rgb)(cr, 0.2, 0.7, 0.3);
    P(arc)(cr, 190, 150, 50, 0, 2 * M_PI);
    P(fill)(cr);
    P(restore)(cr);
    P(save)(cr);
    P(rectangle)(cr, 0, 160, 140, 90);
    P(clip)(cr);
    P(reset_clip)(cr);
    P(rectangle)(cr, 10, 170, 60, 60);
    P(clip)(cr);
    P(set_source_rgb)(cr, 0.4, 0.4, 0.1);
    P(paint)(cr);
    P(restore)(cr);
}

static void
SCENE(groups)(CTX *cr)
{
    P(set_source_rgb)(cr, 0.95, 0.95, 0.9);
    P(paint)(cr);
    P(push_group)(cr);
    P(set_source_rgb)(cr, 0.9, 0.1, 0.1);
    P(rectangle)(cr, 30, 30, 120, 120);
    P(fill)(cr);
    P(set_source_rgb)(cr, 0.1, 0.1, 0.9);
    P(arc)(cr, 150, 150, 60, 0, 2 * M_PI);
    P(fill)(cr);
    P(pop_group_to_source)(cr);
    P(paint_with_alpha)(cr, 0.5);
    /* A group under a clip and a translation. */
    P(save)(cr);
    P(rectangle)(cr, 160, 10, 80, 80);
    P(clip)(cr);
    P(translate)(cr, 5, 5);
    P(push_group)(cr);
    P(set_source_rgb)(cr, 0.2, 0.6, 0.2);
    P(arc)(cr, 200, 50, 45, 0, 2 * M_PI);
    P(fill)(cr);
    P(pop_group_to_source)(cr);
    P(paint_with_alpha)(cr, 0.7);
    P(restore)(cr);
}

static void
SCENE(masks)(CTX *cr)
{
    PAT *m = P(pattern_create_linear)(0, 0, 250, 0);
    P(pattern_add_color_stop_rgba)(m, 0, 0, 0, 0, 1);
    P(pattern_add_color_stop_rgba)(m, 1, 0, 0, 0, 0);
    P(set_source_rgb)(cr, 0.1, 0.4, 0.8);
    P(mask)(cr, m);
    P(pattern_destroy)(m);
    SURF *a = P(image_surface_create)(E(FORMAT_A8), 64, 64);
    CTX *c2 = P(create)(a);
    P(arc)(c2, 32, 32, 28, 0, 2 * M_PI);
    P(fill)(c2);
    P(destroy)(c2);
    P(set_source_rgb)(cr, 0.9, 0.5, 0.1);
    P(mask_surface)(cr, a, 150, 150);
    P(surface_destroy)(a);
}

static void
SCENE(operators)(CTX *cr)
{
    /* Every operator, a translucent blue circle onto a translucent red
     * square, each in its own clipped cell. */
    for (int op = 0; op <= E(OPERATOR_HSL_LUMINOSITY); op++) {
        int cx = (op % 6) * 41, cy = (op / 6) * 41;
        P(save)(cr);
        P(rectangle)(cr, cx, cy, 40, 40);
        P(clip)(cr);
        P(set_source_rgba)(cr, 0.8, 0.2, 0.1, 0.8);
        P(rectangle)(cr, cx + 4, cy + 4, 22, 22);
        P(fill)(cr);
        P(set_operator)(cr, op);
        P(set_source_rgba)(cr, 0.1, 0.3, 0.9, 0.6);
        P(arc)(cr, cx + 25, cy + 25, 12, 0, 2 * M_PI);
        P(fill)(cr);
        P(restore)(cr);
    }
}

static void
SCENE(mesh)(CTX *cr)
{
    /* The canvas conic gradient: a fan of straight-sided patches. */
    PAT *p = P(pattern_create_mesh)();
    int n = 36;
    for (int i = 0; i < n; i++) {
        double a0 = 2 * M_PI * i / n, a1 = 2 * M_PI * (i + 1) / n;
        double t0 = (double)i / n, t1 = (double)(i + 1) / n;
        P(mesh_pattern_begin_patch)(p);
        P(mesh_pattern_move_to)(p, 125, 125);
        P(mesh_pattern_line_to)(p, 125 + 200 * cos(a0), 125 + 200 * sin(a0));
        P(mesh_pattern_line_to)(p, 125 + 200 * cos(a1), 125 + 200 * sin(a1));
        P(mesh_pattern_line_to)(p, 125, 125);
        P(mesh_pattern_set_corner_color_rgba)(p, 0, t0, 0.2, 1 - t0, 1);
        P(mesh_pattern_set_corner_color_rgba)(p, 1, t0, 0.2, 1 - t0, 1);
        P(mesh_pattern_set_corner_color_rgba)(p, 2, t1, 0.2, 1 - t1, 1);
        P(mesh_pattern_set_corner_color_rgba)(p, 3, t1, 0.2, 1 - t1, 1);
        P(mesh_pattern_end_patch)(p);
    }
    P(set_source)(cr, p);
    P(arc)(cr, 125, 125, 100, 0, 2 * M_PI);
    P(fill)(cr);
    P(pattern_destroy)(p);
}

static void
SCENE(a8)(CTX *cr)
{
    /* Drawn into an A8 surface, then used as a mask. */
    SURF *a = P(image_surface_create)(E(FORMAT_A8), 250, 250);
    CTX *c2 = P(create)(a);
    P(set_source_rgba)(c2, 0, 0, 0, 0.7);
    P(rectangle)(c2, 20, 20, 150, 100);
    P(fill)(c2);
    P(set_line_width)(c2, 10);
    P(set_source_rgba)(c2, 0, 0, 0, 1);
    P(arc)(c2, 150, 150, 60, 0, 2 * M_PI);
    P(stroke)(c2);
    P(destroy)(c2);
    P(set_source_rgb)(cr, 0.3, 0.1, 0.5);
    P(mask_surface)(cr, a, 0, 0);
    P(surface_destroy)(a);
}

static void
SCENE(paths)(CTX *cr)
{
    /* Relative segments, copy_path and append_path under a new matrix. */
    P(move_to)(cr, 20, 20);
    P(rel_line_to)(cr, 60, 0);
    P(rel_curve_to)(cr, 20, 0, 20, 40, 0, 40);
    P(rel_line_to)(cr, -60, 0);
    P(close_path)(cr);
    P(rel_move_to)(cr, 10, 10);
    P(rel_line_to)(cr, 20, 10);
    PATH *path = P(copy_path)(cr);
    P(set_line_width)(cr, 3);
    P(set_source_rgb)(cr, 0.1, 0.1, 0.1);
    P(stroke)(cr);
    P(save)(cr);
    P(translate)(cr, 120, 100);
    P(scale)(cr, 1.2, 1.5);
    P(append_path)(cr, path);
    P(set_source_rgb)(cr, 0.7, 0.3, 0.1);
    P(fill)(cr);
    P(restore)(cr);
    P(path_destroy)(path);
}
