/* Weststar SDL Browser — gfx: the drawing context.
 *
 * Coordinates, as in Cairo: user space maps to device space by the current
 * transformation matrix (CTM); device space maps to the target's pixels by
 * the target's device scale and offset. Paths and clips are kept in pixels.
 *
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "gfx-private.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* A clip: a box of target pixels, with an 8-bit mask over it unless the
 * clip is exactly the box. Shared between graphics states; never changed
 * once made. */
typedef struct {
    int      ref;
    int      x0, y0, x1, y1;
    uint8_t *mask;
} gfx_clipst;

typedef struct gstate {
    struct gstate   *next;
    gfx_operator_t   op;
    double           tolerance;
    gfx_antialias_t  aa;
    gfx_fill_rule_t  fill_rule;
    double           line_width;
    gfx_line_cap_t   cap;
    gfx_line_join_t  join;
    double           miter_limit;
    double          *dashes;
    int              num_dashes;
    double           dash_offset;
    gfx_matrix_t     ctm;
    gfx_pattern_t   *source;
    gfx_matrix_t     source_ctm;   /* the CTM when the source was set */
    gfx_clipst        *clip;         /* NULL: the whole target */
    gfx_surface_t   *target;
    gfx_matrix_t     dev;          /* device -> target pixels */
    int              group;        /* pushed by push_group */
    double           group_dx, group_dy;   /* parent pixels -> group pixels */
} gstate;

struct gfx {
    int          ref;
    gfx_status_t status;
    gstate      *gs;
    gfx_pathbuf  path;
};

/* ---- clips ------------------------------------------------------------- */

static gfx_clipst *
clip_new(int x0, int y0, int x1, int y1, int with_mask)
{
    gfx_clipst *c = calloc(1, sizeof *c);
    if (!c)
        return NULL;
    c->ref = 1;
    if (x1 < x0) x1 = x0;
    if (y1 < y0) y1 = y0;
    c->x0 = x0; c->y0 = y0; c->x1 = x1; c->y1 = y1;
    if (with_mask && x1 > x0 && y1 > y0) {
        c->mask = calloc((size_t)(x1 - x0) * (size_t)(y1 - y0), 1);
        if (!c->mask) {
            free(c);
            return NULL;
        }
    }
    return c;
}

static gfx_clipst *
clip_ref(gfx_clipst *c)
{
    if (c)
        c->ref++;
    return c;
}

static void
clip_unref(gfx_clipst *c)
{
    if (!c || --c->ref > 0)
        return;
    free(c->mask);
    free(c);
}

/* The box drawing may touch: the clip's within the target. */
static void
clip_box(const gstate *gs, int *x0, int *y0, int *x1, int *y1)
{
    *x0 = 0;
    *y0 = 0;
    *x1 = gs->target->width;
    *y1 = gs->target->height;
    if (gs->clip) {
        if (gs->clip->x0 > *x0) *x0 = gs->clip->x0;
        if (gs->clip->y0 > *y0) *y0 = gs->clip->y0;
        if (gs->clip->x1 < *x1) *x1 = gs->clip->x1;
        if (gs->clip->y1 < *y1) *y1 = gs->clip->y1;
    }
}

/* ---- life cycle -------------------------------------------------------- */

static void
dev_matrix(const gfx_surface_t *s, gfx_matrix_t *m)
{
    gfx_matrix_init(m, s->dev_sx, 0, 0, s->dev_sy, s->dev_x, s->dev_y);
}

static gstate *
gstate_new(gfx_surface_t *target)
{
    gstate *gs = calloc(1, sizeof *gs);
    if (!gs)
        return NULL;
    gs->op = GFX_OPERATOR_OVER;
    gs->tolerance = 0.1;
    gs->aa = GFX_ANTIALIAS_DEFAULT;
    gs->fill_rule = GFX_FILL_RULE_WINDING;
    gs->line_width = 2.0;
    gs->cap = GFX_LINE_CAP_BUTT;
    gs->join = GFX_LINE_JOIN_MITER;
    gs->miter_limit = 10.0;
    gfx_matrix_init_identity(&gs->ctm);
    gfx_matrix_init_identity(&gs->source_ctm);
    gs->source = gfx_pattern_create_rgb(0, 0, 0);
    gs->target = gfx_surface_reference(target);
    dev_matrix(target, &gs->dev);
    return gs;
}

static void
gstate_free(gstate *gs)
{
    gfx_pattern_destroy(gs->source);
    gfx_surface_destroy(gs->target);
    clip_unref(gs->clip);
    free(gs->dashes);
    free(gs);
}

static gstate *
gstate_copy(const gstate *src)
{
    gstate *gs = malloc(sizeof *gs);
    if (!gs)
        return NULL;
    *gs = *src;
    gs->next = NULL;
    gs->group = 0;
    gs->dashes = NULL;
    if (src->num_dashes > 0) {
        gs->dashes = malloc((size_t)src->num_dashes * sizeof *gs->dashes);
        if (!gs->dashes) {
            free(gs);
            return NULL;
        }
        memcpy(gs->dashes, src->dashes, (size_t)src->num_dashes * sizeof *gs->dashes);
    }
    gfx_pattern_reference(gs->source);
    gfx_surface_reference(gs->target);
    clip_ref(gs->clip);
    return gs;
}

static gfx_t *
gfx_nil(gfx_status_t status)
{
    static gfx_t nil_memory = { .ref = -1, .status = GFX_STATUS_NO_MEMORY };
    static gfx_t nil_size = { .ref = -1, .status = GFX_STATUS_INVALID_SIZE };
    return status == GFX_STATUS_INVALID_SIZE ? &nil_size : &nil_memory;
}

gfx_t *
gfx_create(gfx_surface_t *target)
{
    if (!target || target->status != GFX_STATUS_SUCCESS)
        return gfx_nil(target ? target->status : GFX_STATUS_NO_MEMORY);
    gfx_t *g = calloc(1, sizeof *g);
    if (!g)
        return gfx_nil(GFX_STATUS_NO_MEMORY);
    g->ref = 1;
    g->gs = gstate_new(target);
    if (!g->gs) {
        free(g);
        return gfx_nil(GFX_STATUS_NO_MEMORY);
    }
    gfx_pathbuf_init(&g->path);
    return g;
}

gfx_t *
gfx_reference(gfx_t *g)
{
    if (g && g->ref > 0)
        g->ref++;
    return g;
}

void
gfx_destroy(gfx_t *g)
{
    if (!g || g->ref <= 0 || --g->ref > 0)
        return;
    while (g->gs) {
        gstate *next = g->gs->next;
        gstate_free(g->gs);
        g->gs = next;
    }
    gfx_pathbuf_free(&g->path);
    free(g);
}

const char *
gfx_status_to_string(gfx_status_t status)
{
    switch (status) {
    case GFX_STATUS_SUCCESS:           return "no error";
    case GFX_STATUS_NO_MEMORY:         return "out of memory";
    case GFX_STATUS_INVALID_RESTORE:   return "restore without a matching save";
    case GFX_STATUS_INVALID_POP_GROUP: return "pop_group without a matching push_group";
    case GFX_STATUS_NO_CURRENT_POINT:  return "no current point";
    case GFX_STATUS_INVALID_MATRIX:    return "invalid matrix";
    case GFX_STATUS_INVALID_SIZE:      return "invalid size";
    case GFX_STATUS_INVALID_FORMAT:    return "invalid format";
    case GFX_STATUS_WRITE_ERROR:       return "error while writing";
    }
    return "unknown error";
}

gfx_status_t
gfx_status(const gfx_t *g)
{
    return g ? g->status : GFX_STATUS_NO_MEMORY;
}

static int
usable(const gfx_t *g)
{
    return g && g->ref > 0 && g->status == GFX_STATUS_SUCCESS;
}

static void
set_error(gfx_t *g, gfx_status_t st)
{
    if (g && g->ref > 0 && g->status == GFX_STATUS_SUCCESS)
        g->status = st;
}

gfx_surface_t *
gfx_get_target(gfx_t *g)
{
    if (!g || !g->gs)
        return gfx_surface_nil(GFX_STATUS_NO_MEMORY);
    /* The bottom state's target, below any groups. */
    gstate *gs = g->gs;
    while (gs->next)
        gs = gs->next;
    return gs->target;
}

gfx_surface_t *
gfx_get_group_target(gfx_t *g)
{
    if (!g || !g->gs)
        return gfx_surface_nil(GFX_STATUS_NO_MEMORY);
    return g->gs->target;
}

void
gfx_save(gfx_t *g)
{
    if (!usable(g))
        return;
    gstate *gs = gstate_copy(g->gs);
    if (!gs) {
        set_error(g, GFX_STATUS_NO_MEMORY);
        return;
    }
    gs->next = g->gs;
    g->gs = gs;
}

void
gfx_restore(gfx_t *g)
{
    if (!usable(g))
        return;
    if (!g->gs->next || g->gs->group) {
        set_error(g, GFX_STATUS_INVALID_RESTORE);
        return;
    }
    gstate *top = g->gs;
    g->gs = top->next;
    gstate_free(top);
}

/* ---- groups ------------------------------------------------------------ */

void
gfx_push_group(gfx_t *g)
{
    if (!usable(g))
        return;
    int x0, y0, x1, y1;
    clip_box(g->gs, &x0, &y0, &x1, &y1);
    if (x1 <= x0 || y1 <= y0) {
        x1 = x0 + 1;
        y1 = y0 + 1;
    }
    gfx_surface_t *s = gfx_image_surface_create(GFX_FORMAT_ARGB32, x1 - x0,
                                                y1 - y0);
    if (s->status != GFX_STATUS_SUCCESS) {
        set_error(g, s->status);
        return;
    }
    const gfx_surface_t *parent = g->gs->target;
    s->dev_sx = parent->dev_sx;
    s->dev_sy = parent->dev_sy;
    s->dev_x = parent->dev_x - x0;
    s->dev_y = parent->dev_y - y0;
    gstate *gs = gstate_copy(g->gs);
    if (!gs) {
        gfx_surface_destroy(s);
        set_error(g, GFX_STATUS_NO_MEMORY);
        return;
    }
    gfx_surface_destroy(gs->target);
    gs->target = s;
    dev_matrix(s, &gs->dev);
    clip_unref(gs->clip);
    gs->clip = NULL;
    /* An empty parent clip stays empty inside the group. */
    {
        int px0, py0, px1, py1;
        clip_box(g->gs, &px0, &py0, &px1, &py1);
        if (px1 <= px0 || py1 <= py0)
            gs->clip = clip_new(0, 0, 0, 0, 0);
    }
    gs->group = 1;
    gs->group_dx = -x0;
    gs->group_dy = -y0;
    gs->next = g->gs;
    g->gs = gs;
    gfx_pathbuf_translate(&g->path, -x0, -y0);
}

gfx_pattern_t *
gfx_pop_group(gfx_t *g)
{
    if (!usable(g))
        return gfx_pattern_create_rgba(0, 0, 0, 0);
    gstate *top = g->gs;
    if (!top->group || !top->next) {
        set_error(g, GFX_STATUS_INVALID_POP_GROUP);
        return gfx_pattern_create_rgba(0, 0, 0, 0);
    }
    gfx_pattern_t *p = gfx_pattern_create_for_surface(top->target);
    /* Pattern space is device space: the group's pixels sit where they were
     * drawn. */
    p->matrix = top->ctm;
    gfx_pathbuf_translate(&g->path, -top->group_dx, -top->group_dy);
    g->gs = top->next;
    gstate_free(top);
    return p;
}

void
gfx_pop_group_to_source(gfx_t *g)
{
    gfx_pattern_t *p = gfx_pop_group(g);
    gfx_set_source(g, p);
    gfx_pattern_destroy(p);
}

/* ---- state ------------------------------------------------------------- */

void
gfx_set_source(gfx_t *g, gfx_pattern_t *p)
{
    if (!usable(g) || !p)
        return;
    gfx_pattern_reference(p);
    gfx_pattern_destroy(g->gs->source);
    g->gs->source = p;
    g->gs->source_ctm = g->gs->ctm;
}

void
gfx_set_source_rgba(gfx_t *g, double r, double g_, double b, double a)
{
    if (!usable(g))
        return;
    gfx_pattern_t *p = gfx_pattern_create_rgba(r, g_, b, a);
    gfx_set_source(g, p);
    gfx_pattern_destroy(p);
}

void
gfx_set_source_rgb(gfx_t *g, double r, double g_, double b)
{
    gfx_set_source_rgba(g, r, g_, b, 1);
}

void
gfx_set_source_surface(gfx_t *g, gfx_surface_t *s, double x, double y)
{
    if (!usable(g))
        return;
    gfx_pattern_t *p = gfx_pattern_create_for_surface(s);
    gfx_matrix_init_translate(&p->matrix, -x, -y);
    gfx_set_source(g, p);
    gfx_pattern_destroy(p);
}

gfx_pattern_t *
gfx_get_source(gfx_t *g)
{
    return g && g->gs ? g->gs->source : NULL;
}

void gfx_set_operator(gfx_t *g, gfx_operator_t op) { if (usable(g)) g->gs->op = op; }
gfx_operator_t gfx_get_operator(const gfx_t *g) { return g && g->gs ? g->gs->op : GFX_OPERATOR_OVER; }

void
gfx_set_tolerance(gfx_t *g, double tolerance)
{
    if (usable(g))
        g->gs->tolerance = tolerance < 1e-6 ? 1e-6 : tolerance;
}

void gfx_set_antialias(gfx_t *g, gfx_antialias_t aa) { if (usable(g)) g->gs->aa = aa; }
gfx_antialias_t gfx_get_antialias(const gfx_t *g) { return g && g->gs ? g->gs->aa : GFX_ANTIALIAS_DEFAULT; }
void gfx_set_fill_rule(gfx_t *g, gfx_fill_rule_t r) { if (usable(g)) g->gs->fill_rule = r; }
gfx_fill_rule_t gfx_get_fill_rule(const gfx_t *g) { return g && g->gs ? g->gs->fill_rule : GFX_FILL_RULE_WINDING; }

void
gfx_set_line_width(gfx_t *g, double width)
{
    if (usable(g))
        g->gs->line_width = width < 0 ? 0 : width;
}

double gfx_get_line_width(const gfx_t *g) { return g && g->gs ? g->gs->line_width : 2.0; }
void gfx_set_line_cap(gfx_t *g, gfx_line_cap_t cap) { if (usable(g)) g->gs->cap = cap; }
gfx_line_cap_t gfx_get_line_cap(const gfx_t *g) { return g && g->gs ? g->gs->cap : GFX_LINE_CAP_BUTT; }
void gfx_set_line_join(gfx_t *g, gfx_line_join_t join) { if (usable(g)) g->gs->join = join; }
gfx_line_join_t gfx_get_line_join(const gfx_t *g) { return g && g->gs ? g->gs->join : GFX_LINE_JOIN_MITER; }
void gfx_set_miter_limit(gfx_t *g, double limit) { if (usable(g)) g->gs->miter_limit = limit; }

void
gfx_set_dash(gfx_t *g, const double *dashes, int num_dashes, double offset)
{
    if (!usable(g))
        return;
    gstate *gs = g->gs;
    free(gs->dashes);
    gs->dashes = NULL;
    gs->num_dashes = 0;
    gs->dash_offset = offset;
    if (num_dashes <= 0 || !dashes)
        return;
    /* An odd-length array repeats, as in Cairo. */
    int n = num_dashes % 2 ? num_dashes * 2 : num_dashes;
    gs->dashes = malloc((size_t)n * sizeof *gs->dashes);
    if (!gs->dashes) {
        set_error(g, GFX_STATUS_NO_MEMORY);
        return;
    }
    for (int i = 0; i < n; i++)
        gs->dashes[i] = dashes[i % num_dashes];
    gs->num_dashes = n;
}

/* ---- transformation ---------------------------------------------------- */

static void
set_ctm(gfx_t *g, const gfx_matrix_t *m)
{
    gfx_matrix_t inv = *m;
    if (gfx_matrix_invert(&inv) != GFX_STATUS_SUCCESS) {
        set_error(g, GFX_STATUS_INVALID_MATRIX);
        return;
    }
    g->gs->ctm = *m;
}

void
gfx_transform(gfx_t *g, const gfx_matrix_t *m)
{
    if (!usable(g))
        return;
    gfx_matrix_t r;
    gfx_matrix_multiply(&r, m, &g->gs->ctm);
    set_ctm(g, &r);
}

void
gfx_translate(gfx_t *g, double tx, double ty)
{
    gfx_matrix_t m;
    gfx_matrix_init_translate(&m, tx, ty);
    gfx_transform(g, &m);
}

void
gfx_scale(gfx_t *g, double sx, double sy)
{
    gfx_matrix_t m;
    gfx_matrix_init_scale(&m, sx, sy);
    gfx_transform(g, &m);
}

void
gfx_rotate(gfx_t *g, double radians)
{
    gfx_matrix_t m;
    gfx_matrix_init_rotate(&m, radians);
    gfx_transform(g, &m);
}

void
gfx_set_matrix(gfx_t *g, const gfx_matrix_t *m)
{
    if (usable(g))
        set_ctm(g, m);
}

void
gfx_get_matrix(const gfx_t *g, gfx_matrix_t *m)
{
    if (g && g->gs)
        *m = g->gs->ctm;
    else
        gfx_matrix_init_identity(m);
}

void
gfx_identity_matrix(gfx_t *g)
{
    if (usable(g))
        gfx_matrix_init_identity(&g->gs->ctm);
}

void
gfx_user_to_device(const gfx_t *g, double *x, double *y)
{
    if (g && g->gs)
        gfx_matrix_transform_point(&g->gs->ctm, x, y);
}

void
gfx_user_to_device_distance(const gfx_t *g, double *dx, double *dy)
{
    if (g && g->gs)
        gfx_matrix_transform_distance(&g->gs->ctm, dx, dy);
}

void
gfx_device_to_user(const gfx_t *g, double *x, double *y)
{
    if (!g || !g->gs)
        return;
    gfx_matrix_t inv = g->gs->ctm;
    if (gfx_matrix_invert(&inv) == GFX_STATUS_SUCCESS)
        gfx_matrix_transform_point(&inv, x, y);
}

void
gfx_device_to_user_distance(const gfx_t *g, double *dx, double *dy)
{
    if (!g || !g->gs)
        return;
    gfx_matrix_t inv = g->gs->ctm;
    if (gfx_matrix_invert(&inv) == GFX_STATUS_SUCCESS)
        gfx_matrix_transform_distance(&inv, dx, dy);
}

/* User space -> target pixels. */
static void
u2p(const gstate *gs, gfx_matrix_t *m)
{
    gfx_matrix_multiply(m, &gs->ctm, &gs->dev);
}

static int
p2u(const gstate *gs, gfx_matrix_t *m)
{
    u2p(gs, m);
    return gfx_matrix_invert(m) == GFX_STATUS_SUCCESS;
}

/* ---- paths ------------------------------------------------------------- */

static void
to_pixels(const gfx_t *g, double *x, double *y)
{
    gfx_matrix_t m;
    u2p(g->gs, &m);
    gfx_matrix_transform_point(&m, x, y);
}

static void
from_pixels(const gfx_t *g, double *x, double *y)
{
    gfx_matrix_t m;
    if (p2u(g->gs, &m))
        gfx_matrix_transform_point(&m, x, y);
}

void
gfx_new_path(gfx_t *g)
{
    if (usable(g))
        gfx_pathbuf_clear(&g->path);
}

void
gfx_new_sub_path(gfx_t *g)
{
    if (usable(g)) {
        g->path.has_current = 0;
        g->path.need_move = 0;
    }
}

void
gfx_move_to(gfx_t *g, double x, double y)
{
    if (!usable(g))
        return;
    to_pixels(g, &x, &y);
    gfx_pathbuf_move_to(&g->path, x, y);
}

void
gfx_line_to(gfx_t *g, double x, double y)
{
    if (!usable(g))
        return;
    to_pixels(g, &x, &y);
    gfx_pathbuf_line_to(&g->path, x, y);
}

void
gfx_curve_to(gfx_t *g, double x1, double y1, double x2, double y2, double x3,
             double y3)
{
    if (!usable(g))
        return;
    to_pixels(g, &x1, &y1);
    to_pixels(g, &x2, &y2);
    to_pixels(g, &x3, &y3);
    gfx_pathbuf_curve_to(&g->path, x1, y1, x2, y2, x3, y3);
}

void
gfx_close_path(gfx_t *g)
{
    if (usable(g))
        gfx_pathbuf_close(&g->path);
}

gfx_bool_t
gfx_has_current_point(const gfx_t *g)
{
    return g && g->ref > 0 && g->path.has_current;
}

void
gfx_get_current_point(const gfx_t *g, double *x, double *y)
{
    double cx = 0, cy = 0;
    if (g && g->ref > 0 && g->gs && g->path.has_current) {
        cx = g->path.cur_x;
        cy = g->path.cur_y;
        from_pixels(g, &cx, &cy);
    }
    if (x) *x = cx;
    if (y) *y = cy;
}

void
gfx_rel_move_to(gfx_t *g, double dx, double dy)
{
    if (!usable(g))
        return;
    if (!g->path.has_current) {
        set_error(g, GFX_STATUS_NO_CURRENT_POINT);
        return;
    }
    double x, y;
    gfx_get_current_point(g, &x, &y);
    gfx_move_to(g, x + dx, y + dy);
}

void
gfx_rel_line_to(gfx_t *g, double dx, double dy)
{
    if (!usable(g))
        return;
    if (!g->path.has_current) {
        set_error(g, GFX_STATUS_NO_CURRENT_POINT);
        return;
    }
    double x, y;
    gfx_get_current_point(g, &x, &y);
    gfx_line_to(g, x + dx, y + dy);
}

void
gfx_rel_curve_to(gfx_t *g, double dx1, double dy1, double dx2, double dy2,
                 double dx3, double dy3)
{
    if (!usable(g))
        return;
    if (!g->path.has_current) {
        set_error(g, GFX_STATUS_NO_CURRENT_POINT);
        return;
    }
    double x, y;
    gfx_get_current_point(g, &x, &y);
    gfx_curve_to(g, x + dx1, y + dy1, x + dx2, y + dy2, x + dx3, y + dy3);
}

void
gfx_rectangle(gfx_t *g, double x, double y, double w, double h)
{
    if (!usable(g))
        return;
    gfx_move_to(g, x, y);
    gfx_line_to(g, x + w, y);
    gfx_line_to(g, x + w, y + h);
    gfx_line_to(g, x, y + h);
    gfx_close_path(g);
}

/* Appends an arc as Bézier segments of at most a quarter turn each. */
static void
arc_segments(gfx_t *g, double xc, double yc, double r, double a1, double a2)
{
    double sweep = a2 - a1;
    int n = (int)ceil(fabs(sweep) / (GFX_PI / 2) - 1e-9);
    if (n < 1)
        n = 1;
    double step = sweep / n;
    double k = 4.0 / 3.0 * tan(step / 4);
    double a = a1;
    for (int i = 0; i < n; i++) {
        double b = a + step;
        double ca = cos(a), sa = sin(a), cb = cos(b), sb = sin(b);
        gfx_curve_to(g, xc + r * (ca - k * sa), yc + r * (sa + k * ca),
                     xc + r * (cb + k * sb), yc + r * (sb - k * cb),
                     xc + r * cb, yc + r * sb);
        a = b;
    }
}

static void
arc_start(gfx_t *g, double xc, double yc, double r, double a1)
{
    double x = xc + r * cos(a1), y = yc + r * sin(a1);
    if (g->path.has_current)
        gfx_line_to(g, x, y);
    else
        gfx_move_to(g, x, y);
}

void
gfx_arc(gfx_t *g, double xc, double yc, double radius, double angle1,
        double angle2)
{
    if (!usable(g))
        return;
    if (!(radius > 0)) {
        gfx_line_to(g, xc, yc);
        return;
    }
    if (!isfinite(angle1) || !isfinite(angle2))
        return;
    if (angle2 < angle1) {
        double turns = ceil((angle1 - angle2) / (2 * GFX_PI));
        angle2 += turns * 2 * GFX_PI;
    }
    arc_start(g, xc, yc, radius, angle1);
    if (angle2 > angle1)
        arc_segments(g, xc, yc, radius, angle1, angle2);
}

void
gfx_arc_negative(gfx_t *g, double xc, double yc, double radius,
                 double angle1, double angle2)
{
    if (!usable(g))
        return;
    if (!(radius > 0)) {
        gfx_line_to(g, xc, yc);
        return;
    }
    if (!isfinite(angle1) || !isfinite(angle2))
        return;
    if (angle2 > angle1) {
        double turns = ceil((angle2 - angle1) / (2 * GFX_PI));
        angle2 -= turns * 2 * GFX_PI;
    }
    arc_start(g, xc, yc, radius, angle1);
    if (angle2 < angle1)
        arc_segments(g, xc, yc, radius, angle1, angle2);
}

static gfx_path_t *
path_new(int n)
{
    gfx_path_t *p = calloc(1, sizeof *p);
    if (!p)
        return NULL;
    if (n > 0) {
        p->data = malloc((size_t)n * sizeof *p->data);
        if (!p->data) {
            free(p);
            return NULL;
        }
    }
    p->num_data = n;
    return p;
}

static gfx_path_t *
path_nil(void)
{
    static gfx_path_t nil = { GFX_STATUS_NO_MEMORY, NULL, 0 };
    return &nil;
}

gfx_path_t *
gfx_copy_path(gfx_t *g)
{
    if (!g || g->ref <= 0)
        return path_nil();
    int n = (int)g->path.data.len;
    gfx_path_t *p = path_new(n);
    if (!p)
        return path_nil();
    gfx_matrix_t inv;
    int ok = p2u(g->gs, &inv);
    for (int i = 0; i < n;) {
        const gfx_path_data_t *h = &GFX_VEC_AT(&g->path.data, gfx_path_data_t, i);
        p->data[i] = *h;
        for (int k = 1; k < h->header.length; k++) {
            double x = h[k].point.x, y = h[k].point.y;
            if (ok)
                gfx_matrix_transform_point(&inv, &x, &y);
            p->data[i + k].point.x = x;
            p->data[i + k].point.y = y;
        }
        i += h->header.length;
    }
    return p;
}

gfx_path_t *
gfx_copy_path_flat(gfx_t *g)
{
    if (!g || g->ref <= 0)
        return path_nil();
    gfx_flat f;
    gfx_flat_init(&f);
    gfx_flatten(&g->path, g->gs->tolerance, &f);
    int n = 0;
    for (size_t s = 0; s < f.subs.len; s++) {
        const gfx_fsub *sub = &GFX_VEC_AT(&f.subs, gfx_fsub, s);
        n += 2 * sub->count + (sub->closed ? 1 : 0);
    }
    gfx_path_t *p = path_new(n);
    if (!p) {
        gfx_flat_free(&f);
        return path_nil();
    }
    gfx_matrix_t inv;
    int ok = p2u(g->gs, &inv);
    int i = 0;
    for (size_t s = 0; s < f.subs.len; s++) {
        const gfx_fsub *sub = &GFX_VEC_AT(&f.subs, gfx_fsub, s);
        for (int k = 0; k < sub->count; k++) {
            const gfx_fpoint *fp = &GFX_VEC_AT(&f.pts, gfx_fpoint, sub->start + k);
            double x = fp->x, y = fp->y;
            if (ok)
                gfx_matrix_transform_point(&inv, &x, &y);
            p->data[i].header.type = k == 0 ? GFX_PATH_MOVE_TO : GFX_PATH_LINE_TO;
            p->data[i].header.length = 2;
            p->data[i + 1].point.x = x;
            p->data[i + 1].point.y = y;
            i += 2;
        }
        if (sub->closed) {
            p->data[i].header.type = GFX_PATH_CLOSE_PATH;
            p->data[i].header.length = 1;
            i++;
        }
    }
    gfx_flat_free(&f);
    return p;
}

void
gfx_append_path(gfx_t *g, const gfx_path_t *path)
{
    if (!usable(g) || !path || path->status != GFX_STATUS_SUCCESS)
        return;
    for (int i = 0; i < path->num_data;) {
        const gfx_path_data_t *h = &path->data[i];
        int len = h->header.length;
        if (len < 1 || i + len > path->num_data)
            break;
        switch (h->header.type) {
        case GFX_PATH_MOVE_TO:
            if (len >= 2)
                gfx_move_to(g, h[1].point.x, h[1].point.y);
            break;
        case GFX_PATH_LINE_TO:
            if (len >= 2)
                gfx_line_to(g, h[1].point.x, h[1].point.y);
            break;
        case GFX_PATH_CURVE_TO:
            if (len >= 4)
                gfx_curve_to(g, h[1].point.x, h[1].point.y, h[2].point.x,
                             h[2].point.y, h[3].point.x, h[3].point.y);
            break;
        case GFX_PATH_CLOSE_PATH:
            gfx_close_path(g);
            break;
        }
        i += len;
    }
}

void
gfx_path_destroy(gfx_path_t *path)
{
    if (!path || path == path_nil())
        return;
    free(path->data);
    free(path);
}

/* A pixel-space box -> user-space bounds. */
static void
box_to_user(const gfx_t *g, double x0, double y0, double x1, double y1,
            double *ux1, double *uy1, double *ux2, double *uy2)
{
    gfx_matrix_t inv;
    if (!p2u(g->gs, &inv)) {
        *ux1 = *uy1 = *ux2 = *uy2 = 0;
        return;
    }
    double xs[4] = { x0, x1, x1, x0 }, ys[4] = { y0, y0, y1, y1 };
    double minx = INFINITY, miny = INFINITY, maxx = -INFINITY, maxy = -INFINITY;
    for (int i = 0; i < 4; i++) {
        gfx_matrix_transform_point(&inv, &xs[i], &ys[i]);
        minx = fmin(minx, xs[i]);
        miny = fmin(miny, ys[i]);
        maxx = fmax(maxx, xs[i]);
        maxy = fmax(maxy, ys[i]);
    }
    *ux1 = minx; *uy1 = miny; *ux2 = maxx; *uy2 = maxy;
}

static void
put_extents(double *x1, double *y1, double *x2, double *y2, double a,
            double b, double c, double d)
{
    if (x1) *x1 = a;
    if (y1) *y1 = b;
    if (x2) *x2 = c;
    if (y2) *y2 = d;
}

void
gfx_path_extents(gfx_t *g, double *x1, double *y1, double *x2, double *y2)
{
    if (!g || g->ref <= 0 || !g->gs) {
        put_extents(x1, y1, x2, y2, 0, 0, 0, 0);
        return;
    }
    gfx_flat f;
    gfx_flat_init(&f);
    gfx_flatten(&g->path, g->gs->tolerance, &f);
    gfx_matrix_t inv;
    int ok = p2u(g->gs, &inv);
    double minx = INFINITY, miny = INFINITY, maxx = -INFINITY, maxy = -INFINITY;
    for (size_t i = 0; i < f.pts.len; i++) {
        const gfx_fpoint *fp = &GFX_VEC_AT(&f.pts, gfx_fpoint, i);
        double x = fp->x, y = fp->y;
        if (ok)
            gfx_matrix_transform_point(&inv, &x, &y);
        minx = fmin(minx, x);
        miny = fmin(miny, y);
        maxx = fmax(maxx, x);
        maxy = fmax(maxy, y);
    }
    gfx_flat_free(&f);
    if (minx > maxx)
        put_extents(x1, y1, x2, y2, 0, 0, 0, 0);
    else
        put_extents(x1, y1, x2, y2, minx, miny, maxx, maxy);
}

/* ---- the drawing pipeline ---------------------------------------------- */

typedef struct {
    gstate        *gs;
    gfx_surface_t *t;
    gfx_operator_t op;
    int            bounded;
    gfx_source     src;
    gfx_source     mask;
    int            have_mask;
    unsigned       alpha;          /* constant coverage, 0..255 */
    const gfx_clipst *clip;
    uint32_t      *srcbuf, *maskbuf;
    uint8_t       *covbuf, *clipbuf;
    unsigned char *tmp;
} drawer;

static void
source_for(const gstate *gs, const gfx_pattern_t *p, const gfx_matrix_t *ctm,
           double alpha, gfx_source *out)
{
    /* pixels -> device -> user (as when set) -> pattern space */
    gfx_matrix_t inv_dev = gs->dev, inv_ctm = *ctm, m;
    if (gfx_matrix_invert(&inv_dev) != GFX_STATUS_SUCCESS ||
        gfx_matrix_invert(&inv_ctm) != GFX_STATUS_SUCCESS)
        gfx_matrix_init_identity(&inv_ctm);
    gfx_matrix_multiply(&m, &inv_dev, &inv_ctm);
    gfx_matrix_multiply(&m, &m, &p->matrix);
    gfx_source_init(out, p, &m, alpha);
}

static int
drawer_init(drawer *d, gfx_t *g, int width)
{
    memset(d, 0, sizeof *d);
    d->gs = g->gs;
    d->t = g->gs->target;
    d->op = g->gs->op;
    d->bounded = gfx_operator_bounded(d->op);
    d->alpha = 255;
    d->clip = g->gs->clip;
    source_for(g->gs, g->gs->source, &g->gs->source_ctm, 1, &d->src);
    size_t w = (size_t)(width > 0 ? width : 1);
    d->srcbuf = malloc(w * sizeof *d->srcbuf);
    d->maskbuf = malloc(w * sizeof *d->maskbuf);
    d->covbuf = malloc(w);
    d->clipbuf = malloc(w);
    d->tmp = malloc(w * 4);
    if (!d->srcbuf || !d->maskbuf || !d->covbuf || !d->clipbuf || !d->tmp) {
        set_error(g, GFX_STATUS_NO_MEMORY);
        return 0;
    }
    return 1;
}

static void
drawer_free(drawer *d)
{
    gfx_source_fini(&d->src);
    gfx_source_fini(&d->mask);
    free(d->srcbuf);
    free(d->maskbuf);
    free(d->covbuf);
    free(d->clipbuf);
    free(d->tmp);
}

/* Composites one run of pixels with the shape's coverage (NULL: full). */
static void
draw_span(void *closure, int y, int x, int len, const uint8_t *cov)
{
    drawer *d = closure;
    if (len <= 0 || y < 0 || y >= d->t->height)
        return;
    /* Clip the run to the clip box. */
    if (d->clip) {
        int cx0 = d->clip->x0, cx1 = d->clip->x1;
        if (y < d->clip->y0 || y >= d->clip->y1)
            return;
        if (x < cx0) {
            if (cov) cov += cx0 - x;
            len -= cx0 - x;
            x = cx0;
        }
        if (x + len > cx1)
            len = cx1 - x;
        if (len <= 0)
            return;
    }
    /* Shape coverage: the rasterizer's, the constant alpha, the mask. */
    const uint8_t *shape = cov;
    if (d->alpha != 255 || d->have_mask) {
        if (d->have_mask)
            gfx_source_fetch(&d->mask, y, x, len, d->maskbuf);
        for (int i = 0; i < len; i++) {
            unsigned c = cov ? cov[i] : 255;
            c = gfx_mul8(c, d->alpha);
            if (d->have_mask)
                c = gfx_mul8(c, d->maskbuf[i] >> 24);
            d->covbuf[i] = (uint8_t)c;
        }
        shape = d->covbuf;
    }
    const uint8_t *clipm = NULL;
    if (d->clip && d->clip->mask) {
        int cw = d->clip->x1 - d->clip->x0;
        clipm = d->clip->mask + (size_t)(y - d->clip->y0) * (size_t)cw +
                (size_t)(x - d->clip->x0);
    }
    int bpp = d->t->format == GFX_FORMAT_A8 ? 1 : 4;
    unsigned char *dst = d->t->data + (size_t)y * (size_t)d->t->stride +
                         (size_t)x * (size_t)bpp;
    if (d->bounded) {
        const uint8_t *total = shape;
        if (clipm) {
            for (int i = 0; i < len; i++)
                d->clipbuf[i] = gfx_mul8(shape ? shape[i] : 255, clipm[i]);
            total = d->clipbuf;
        }
        if (d->src.pat->type == GFX_PATTERN_SOLID &&
            d->src.pat->status == GFX_STATUS_SUCCESS) {
            gfx_composite_solid(d->op, d->t->format, dst, d->src.solid, total,
                                len);
            return;
        }
        gfx_source_fetch(&d->src, y, x, len, d->srcbuf);
        gfx_composite_row(d->op, d->t->format, dst, d->srcbuf, total, len);
        return;
    }
    gfx_source_fetch(&d->src, y, x, len, d->srcbuf);
    /* Unbounded: op(src x shape, dst), then the clip blends the result. */
    if (!clipm) {
        gfx_composite_row(d->op, d->t->format, dst, d->srcbuf, shape, len);
        return;
    }
    memcpy(d->tmp, dst, (size_t)len * (size_t)bpp);
    gfx_composite_row(d->op, d->t->format, d->tmp, d->srcbuf, shape, len);
    for (int i = 0; i < len; i++) {
        unsigned c = clipm[i];
        for (int b = 0; b < bpp; b++) {
            unsigned o = dst[i * bpp + b], n = d->tmp[i * bpp + b];
            dst[i * bpp + b] = (unsigned char)(o + (((int)n - (int)o) * (int)c +
                                                    (n >= o ? 127 : -127)) / 255);
        }
    }
}

/* Paints the clip box, or for a bounded operator the part of it a
 * non-extending image source (and mask) can reach. */
static void
draw_box(gfx_t *g, drawer *d)
{
    int x0, y0, x1, y1;
    clip_box(g->gs, &x0, &y0, &x1, &y1);
    if (d->bounded) {
        int e[4];
        if (gfx_source_extent(&d->src, &e[0], &e[1], &e[2], &e[3])) {
            x0 = x0 > e[0] ? x0 : e[0];
            y0 = y0 > e[1] ? y0 : e[1];
            x1 = x1 < e[2] ? x1 : e[2];
            y1 = y1 < e[3] ? y1 : e[3];
        }
        if (d->have_mask &&
            gfx_source_extent(&d->mask, &e[0], &e[1], &e[2], &e[3])) {
            x0 = x0 > e[0] ? x0 : e[0];
            y0 = y0 > e[1] ? y0 : e[1];
            x1 = x1 < e[2] ? x1 : e[2];
            y1 = y1 < e[3] ? y1 : e[3];
        }
    }
    for (int y = y0; y < y1 && x1 > x0; y++)
        draw_span(d, y, x0, x1 - x0, NULL);
}

/* Fills a box of whole pixels: no rasterizing, full coverage. */
static void
draw_pixel_rect(gfx_t *g, int rx0, int ry0, int rx1, int ry1)
{
    int x0, y0, x1, y1;
    clip_box(g->gs, &x0, &y0, &x1, &y1);
    x0 = x0 > rx0 ? x0 : rx0;
    y0 = y0 > ry0 ? y0 : ry0;
    x1 = x1 < rx1 ? x1 : rx1;
    y1 = y1 < ry1 ? y1 : ry1;
    if (x1 <= x0 || y1 <= y0)
        return;
    drawer d;
    if (drawer_init(&d, g, x1 - x0)) {
        for (int y = y0; y < y1; y++)
            draw_span(&d, y, x0, x1 - x0, NULL);
    }
    drawer_free(&d);
}

typedef struct {
    uint8_t *mask;
    int      x0, y0, w;
} mask_target;

static void
mask_span(void *closure, int y, int x, int len, const uint8_t *cov)
{
    mask_target *m = closure;
    memcpy(m->mask + (size_t)(y - m->y0) * (size_t)m->w + (size_t)(x - m->x0),
           cov, (size_t)len);
}

/* Draws the source through a polygon. */
static void
draw_polygon(gfx_t *g, const gfx_polygon *poly, gfx_fill_rule_t rule)
{
    int x0, y0, x1, y1;
    clip_box(g->gs, &x0, &y0, &x1, &y1);
    if (x1 <= x0 || y1 <= y0)
        return;
    drawer d;
    if (!drawer_init(&d, g, x1 - x0)) {
        drawer_free(&d);
        return;
    }
    if (d.bounded) {
        gfx_rasterize(poly, rule, g->gs->aa, x0, y0, x1, y1, draw_span, &d);
    } else {
        /* Pixels outside the shape are affected too: rasterize into a mask
         * over the whole clip box, then composite all of it. */
        int w = x1 - x0, h = y1 - y0;
        mask_target m = { calloc((size_t)w * (size_t)h, 1), x0, y0, w };
        if (m.mask) {
            gfx_rasterize(poly, rule, g->gs->aa, x0, y0, x1, y1, mask_span, &m);
            for (int y = y0; y < y1; y++)
                draw_span(&d, y, x0, w, m.mask + (size_t)(y - y0) * (size_t)w);
            free(m.mask);
        } else {
            set_error(g, GFX_STATUS_NO_MEMORY);
        }
    }
    drawer_free(&d);
}

void
gfx_paint_with_alpha(gfx_t *g, double alpha)
{
    if (!usable(g))
        return;
    int x0, y0, x1, y1;
    clip_box(g->gs, &x0, &y0, &x1, &y1);
    if (x1 <= x0 || y1 <= y0)
        return;
    drawer d;
    if (drawer_init(&d, g, x1 - x0)) {
        d.alpha = (unsigned)(gfx_clampd(alpha, 0, 1) * 255 + 0.5);
        if (d.alpha > 0 || !d.bounded)
            draw_box(g, &d);
    }
    drawer_free(&d);
}

void
gfx_paint(gfx_t *g)
{
    gfx_paint_with_alpha(g, 1);
}

void
gfx_mask(gfx_t *g, gfx_pattern_t *mask)
{
    if (!usable(g) || !mask)
        return;
    int x0, y0, x1, y1;
    clip_box(g->gs, &x0, &y0, &x1, &y1);
    if (x1 <= x0 || y1 <= y0)
        return;
    drawer d;
    if (drawer_init(&d, g, x1 - x0)) {
        source_for(g->gs, mask, &g->gs->ctm, 1, &d.mask);
        d.have_mask = 1;
        draw_box(g, &d);
    }
    drawer_free(&d);
}

void
gfx_mask_surface(gfx_t *g, gfx_surface_t *mask, double x, double y)
{
    if (!usable(g))
        return;
    gfx_pattern_t *p = gfx_pattern_create_for_surface(mask);
    gfx_matrix_init_translate(&p->matrix, -x, -y);
    gfx_mask(g, p);
    gfx_pattern_destroy(p);
}

static void
fill_polygon(gfx_t *g, gfx_polygon *poly)
{
    gfx_flat f;
    gfx_flat_init(&f);
    gfx_flatten(&g->path, g->gs->tolerance, &f);
    gfx_polygon_add_flat(poly, &f);
    gfx_flat_free(&f);
}

static void
stroke_polygon(gfx_t *g, gfx_polygon *poly)
{
    const gstate *gs = g->gs;
    gfx_flat f;
    gfx_flat_init(&f);
    gfx_flatten(&g->path, gs->tolerance, &f);
    gfx_matrix_t m;
    u2p(gs, &m);
    gfx_stroke_style st = {
        .width = gs->line_width, .cap = gs->cap, .join = gs->join,
        .miter_limit = gs->miter_limit, .dashes = gs->dashes,
        .num_dashes = gs->num_dashes, .dash_offset = gs->dash_offset,
        .tolerance = gs->tolerance,
    };
    gfx_stroke_to_polygon(&f, &m, &st, poly);
    gfx_flat_free(&f);
}

static int path_pixel_rect(const gfx_flat *f, int *x0, int *y0, int *x1,
                           int *y1);

void
gfx_fill_preserve(gfx_t *g)
{
    if (!usable(g))
        return;
    /* Most boxes on a page are rectangles on whole pixels. */
    if (gfx_operator_bounded(g->gs->op)) {
        gfx_flat f;
        gfx_flat_init(&f);
        gfx_flatten(&g->path, g->gs->tolerance, &f);
        int rx0, ry0, rx1, ry1;
        int rect = path_pixel_rect(&f, &rx0, &ry0, &rx1, &ry1);
        gfx_flat_free(&f);
        if (rect) {
            draw_pixel_rect(g, rx0, ry0, rx1, ry1);
            return;
        }
    }
    gfx_polygon poly;
    gfx_polygon_init(&poly);
    fill_polygon(g, &poly);
    draw_polygon(g, &poly, g->gs->fill_rule);
    gfx_polygon_free(&poly);
}

void
gfx_fill(gfx_t *g)
{
    gfx_fill_preserve(g);
    gfx_new_path(g);
}

void
gfx_stroke_preserve(gfx_t *g)
{
    if (!usable(g))
        return;
    gfx_polygon poly;
    gfx_polygon_init(&poly);
    stroke_polygon(g, &poly);
    draw_polygon(g, &poly, GFX_FILL_RULE_WINDING);
    gfx_polygon_free(&poly);
}

void
gfx_stroke(gfx_t *g)
{
    gfx_stroke_preserve(g);
    gfx_new_path(g);
}

static void
polygon_extents(gfx_t *g, const gfx_polygon *poly, double *x1, double *y1,
                double *x2, double *y2)
{
    if (!poly->edges.len) {
        put_extents(x1, y1, x2, y2, 0, 0, 0, 0);
        return;
    }
    double a, b, c, d;
    box_to_user(g, poly->minx, poly->miny, poly->maxx, poly->maxy, &a, &b, &c, &d);
    put_extents(x1, y1, x2, y2, a, b, c, d);
}

void
gfx_fill_extents(gfx_t *g, double *x1, double *y1, double *x2, double *y2)
{
    if (!g || g->ref <= 0 || !g->gs) {
        put_extents(x1, y1, x2, y2, 0, 0, 0, 0);
        return;
    }
    gfx_polygon poly;
    gfx_polygon_init(&poly);
    fill_polygon(g, &poly);
    polygon_extents(g, &poly, x1, y1, x2, y2);
    gfx_polygon_free(&poly);
}

void
gfx_stroke_extents(gfx_t *g, double *x1, double *y1, double *x2, double *y2)
{
    if (!g || g->ref <= 0 || !g->gs) {
        put_extents(x1, y1, x2, y2, 0, 0, 0, 0);
        return;
    }
    gfx_polygon poly;
    gfx_polygon_init(&poly);
    stroke_polygon(g, &poly);
    polygon_extents(g, &poly, x1, y1, x2, y2);
    gfx_polygon_free(&poly);
}

gfx_bool_t
gfx_in_fill(gfx_t *g, double x, double y)
{
    if (!g || g->ref <= 0 || !g->gs)
        return 0;
    gfx_polygon poly;
    gfx_polygon_init(&poly);
    fill_polygon(g, &poly);
    to_pixels(g, &x, &y);
    int in = gfx_polygon_contains(&poly, g->gs->fill_rule, x, y);
    gfx_polygon_free(&poly);
    return in;
}

gfx_bool_t
gfx_in_stroke(gfx_t *g, double x, double y)
{
    if (!g || g->ref <= 0 || !g->gs)
        return 0;
    gfx_polygon poly;
    gfx_polygon_init(&poly);
    stroke_polygon(g, &poly);
    to_pixels(g, &x, &y);
    int in = gfx_polygon_contains(&poly, GFX_FILL_RULE_WINDING, x, y);
    gfx_polygon_free(&poly);
    return in;
}

/* ---- clipping ---------------------------------------------------------- */

/* The path as a pixel-aligned rectangle, if it is exactly one. */
static int
path_pixel_rect(const gfx_flat *f, int *x0, int *y0, int *x1, int *y1)
{
    /* One sub-path, plus perhaps the lone move_to close_path leaves. */
    size_t nsub = f->subs.len;
    if (nsub == 2) {
        const gfx_fsub *tail = &GFX_VEC_AT(&f->subs, gfx_fsub, 1);
        if (tail->count == 1 && !tail->closed)
            nsub = 1;
    }
    if (nsub != 1)
        return 0;
    const gfx_fsub *sub = &GFX_VEC_AT(&f->subs, gfx_fsub, 0);
    int n = sub->count;
    const gfx_fpoint *p = &GFX_VEC_AT(&f->pts, gfx_fpoint, sub->start);
    if (n == 5 && p[4].x == p[0].x && p[4].y == p[0].y)
        n = 4;
    if (n != 4)
        return 0;
    for (int i = 0; i < 4; i++) {
        if (p[i].x != floor(p[i].x) || p[i].y != floor(p[i].y) ||
            fabs(p[i].x) > 1e8 || fabs(p[i].y) > 1e8)
            return 0;
        const gfx_fpoint *q = &p[(i + 1) % 4];
        if (p[i].x != q->x && p[i].y != q->y)
            return 0;
    }
    double minx = fmin(fmin(p[0].x, p[1].x), fmin(p[2].x, p[3].x));
    double maxx = fmax(fmax(p[0].x, p[1].x), fmax(p[2].x, p[3].x));
    double miny = fmin(fmin(p[0].y, p[1].y), fmin(p[2].y, p[3].y));
    double maxy = fmax(fmax(p[0].y, p[1].y), fmax(p[2].y, p[3].y));
    /* Four axis-aligned sides with integer corners: a rectangle, unless
     * two corners coincide (then it is empty or a line, also exact). */
    *x0 = (int)minx; *x1 = (int)maxx;
    *y0 = (int)miny; *y1 = (int)maxy;
    return 1;
}

void
gfx_clip_preserve(gfx_t *g)
{
    if (!usable(g))
        return;
    gstate *gs = g->gs;
    int bx0, by0, bx1, by1;
    clip_box(gs, &bx0, &by0, &bx1, &by1);
    gfx_flat f;
    gfx_flat_init(&f);
    gfx_flatten(&g->path, gs->tolerance, &f);
    int rx0, ry0, rx1, ry1;
    gfx_clipst *c = NULL;
    if (path_pixel_rect(&f, &rx0, &ry0, &rx1, &ry1)) {
        int x0 = rx0 > bx0 ? rx0 : bx0, y0 = ry0 > by0 ? ry0 : by0;
        int x1 = rx1 < bx1 ? rx1 : bx1, y1 = ry1 < by1 ? ry1 : by1;
        int keep_mask = gs->clip && gs->clip->mask && x1 > x0 && y1 > y0;
        c = clip_new(x0, y0, x1, y1, keep_mask);
        if (c && keep_mask) {
            const gfx_clipst *o = gs->clip;
            int ow = o->x1 - o->x0, w = x1 - x0;
            for (int y = y0; y < y1; y++)
                memcpy(c->mask + (size_t)(y - y0) * (size_t)w,
                       o->mask + (size_t)(y - o->y0) * (size_t)ow +
                       (size_t)(x0 - o->x0), (size_t)w);
        }
    } else {
        gfx_polygon poly;
        gfx_polygon_init(&poly);
        gfx_polygon_add_flat(&poly, &f);
        int x0 = bx0, y0 = by0, x1 = bx1, y1 = by1;
        if (poly.edges.len) {
            x0 = (int)fmax(x0, floor(poly.minx));
            y0 = (int)fmax(y0, floor(poly.miny));
            x1 = (int)fmin(x1, ceil(poly.maxx));
            y1 = (int)fmin(y1, ceil(poly.maxy));
        } else {
            x1 = x0;
            y1 = y0;
        }
        c = clip_new(x0, y0, x1, y1, 1);
        if (c && c->mask) {
            mask_target m = { c->mask, c->x0, c->y0, c->x1 - c->x0 };
            gfx_rasterize(&poly, gs->fill_rule, gs->aa, c->x0, c->y0, c->x1,
                          c->y1, mask_span, &m);
            if (gs->clip && gs->clip->mask) {
                const gfx_clipst *o = gs->clip;
                int ow = o->x1 - o->x0, w = c->x1 - c->x0;
                for (int y = c->y0; y < c->y1; y++)
                    for (int x = c->x0; x < c->x1; x++) {
                        uint8_t *v = &c->mask[(size_t)(y - c->y0) * (size_t)w +
                                              (size_t)(x - c->x0)];
                        *v = gfx_mul8(*v, o->mask[(size_t)(y - o->y0) * (size_t)ow +
                                                  (size_t)(x - o->x0)]);
                    }
            }
        }
        gfx_polygon_free(&poly);
    }
    gfx_flat_free(&f);
    if (!c) {
        set_error(g, GFX_STATUS_NO_MEMORY);
        return;
    }
    clip_unref(gs->clip);
    gs->clip = c;
}

void
gfx_clip(gfx_t *g)
{
    gfx_clip_preserve(g);
    gfx_new_path(g);
}

void
gfx_reset_clip(gfx_t *g)
{
    if (!usable(g))
        return;
    clip_unref(g->gs->clip);
    g->gs->clip = NULL;
}

gfx_bool_t
gfx_clip_pixel_box(gfx_t *g, gfx_rectangle_int_t *box, gfx_bool_t *is_rect)
{
    if (!g || g->ref <= 0 || !g->gs) {
        if (box) *box = (gfx_rectangle_int_t){ 0, 0, 0, 0 };
        if (is_rect) *is_rect = 1;
        return 0;
    }
    int x0, y0, x1, y1;
    clip_box(g->gs, &x0, &y0, &x1, &y1);
    if (box)
        *box = (gfx_rectangle_int_t){ x0, y0, x1 > x0 ? x1 - x0 : 0,
                                      y1 > y0 ? y1 - y0 : 0 };
    if (is_rect)
        *is_rect = !g->gs->clip || !g->gs->clip->mask;
    return x1 > x0 && y1 > y0;
}

void
gfx_clip_extents(gfx_t *g, double *x1, double *y1, double *x2, double *y2)
{
    if (!g || g->ref <= 0 || !g->gs) {
        put_extents(x1, y1, x2, y2, 0, 0, 0, 0);
        return;
    }
    int bx0, by0, bx1, by1;
    clip_box(g->gs, &bx0, &by0, &bx1, &by1);
    if (bx1 <= bx0 || by1 <= by0) {
        put_extents(x1, y1, x2, y2, 0, 0, 0, 0);
        return;
    }
    double a, b, c, d;
    box_to_user(g, bx0, by0, bx1, by1, &a, &b, &c, &d);
    put_extents(x1, y1, x2, y2, a, b, c, d);
}
