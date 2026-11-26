/* Weststar SDL Browser — gfx: strokes and dashes.
 *
 * A stroke is built in user space, so that a non-uniform transformation
 * shapes the pen as in Cairo, as a set of small polygons: one quadrilateral
 * per segment, plus joins and caps. Every polygon is wound the same way,
 * so filling them all with the non-zero rule gives their union.
 *
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "gfx-private.h"

#include <math.h>
#include <stdlib.h>

typedef struct {
    double x, y;
    int    corner;
} upt;

typedef struct {
    gfx_polygon         *out;
    const gfx_matrix_t  *m;          /* user -> pixels */
    double               scale;      /* the matrix's largest stretch */
    const gfx_stroke_style *st;
    double               hw;         /* half the line width */
    gfx_vec              buf;        /* double[2], pixels */
} stroker;

/* ---- polygon emission -------------------------------------------------- */

static void
em_begin(stroker *s)
{
    gfx_vec_clear(&s->buf);
}

static void
em_pt(stroker *s, double x, double y)
{
    double *p = gfx_vec_push(&s->buf);
    if (!p)
        return;
    gfx_matrix_transform_point(s->m, &x, &y);
    p[0] = x;
    p[1] = y;
}

static void
em_end(stroker *s)
{
    size_t n = s->buf.len;
    if (n < 3)
        return;
    const double *p = s->buf.data;
    double area = 0;
    for (size_t i = 0; i < n; i++) {
        const double *a = p + 2 * i, *b = p + 2 * ((i + 1) % n);
        area += a[0] * b[1] - b[0] * a[1];
    }
    if (area == 0)
        return;
    for (size_t i = 0; i < n; i++) {
        size_t j = (i + 1) % n;
        if (area > 0)
            gfx_polygon_add_line(s->out, p[2 * i], p[2 * i + 1], p[2 * j],
                                 p[2 * j + 1]);
        else
            gfx_polygon_add_line(s->out, p[2 * j], p[2 * j + 1], p[2 * i],
                                 p[2 * i + 1]);
    }
}

/* Steps for an arc of the pen's outline sweeping angle radians. */
static int
arc_steps(const stroker *s, double angle)
{
    double r = s->hw * s->scale;
    double tol = s->st->tolerance;
    double step = tol < r ? 2 * acos(1 - tol / r) : GFX_PI / 2;
    if (step > GFX_PI / 2)
        step = GFX_PI / 2;
    if (!(step > 1e-4))
        step = 1e-4;
    int n = (int)ceil(fabs(angle) / step);
    return n < 1 ? 1 : n > 1024 ? 1024 : n;
}

/* Arc points around (cx, cy) from angle a0 to a1, both ends included. */
static void
em_arc(stroker *s, double cx, double cy, double a0, double a1)
{
    int n = arc_steps(s, a1 - a0);
    for (int i = 0; i <= n; i++) {
        double a = a0 + (a1 - a0) * i / n;
        em_pt(s, cx + s->hw * cos(a), cy + s->hw * sin(a));
    }
}

static void
em_circle(stroker *s, double cx, double cy)
{
    em_begin(s);
    int n = arc_steps(s, 2 * GFX_PI);
    if (n < 8)
        n = 8;
    for (int i = 0; i < n; i++) {
        double a = 2 * GFX_PI * i / n;
        em_pt(s, cx + s->hw * cos(a), cy + s->hw * sin(a));
    }
    em_end(s);
}

/* ---- caps and joins ---------------------------------------------------- */

/* A cap at p for a line leaving p along -(ux, uy): it extends backwards. */
static void
cap(stroker *s, double px, double py, double ux, double uy)
{
    double hw = s->hw;
    double nx = -uy * hw, ny = ux * hw;
    switch (s->st->cap) {
    case GFX_LINE_CAP_BUTT:
        return;
    case GFX_LINE_CAP_SQUARE:
        em_begin(s);
        em_pt(s, px + nx, py + ny);
        em_pt(s, px + nx - ux * hw, py + ny - uy * hw);
        em_pt(s, px - nx - ux * hw, py - ny - uy * hw);
        em_pt(s, px - nx, py - ny);
        em_end(s);
        return;
    case GFX_LINE_CAP_ROUND: {
        double a = atan2(ny, nx);
        em_begin(s);
        em_arc(s, px, py, a, a + GFX_PI);
        em_end(s);
        return;
    }
    }
}

static void
join(stroker *s, double px, double py, double u1x, double u1y, double u2x,
     double u2y, int corner)
{
    double cross = u1x * u2y - u1y * u2x;
    double dot = u1x * u2x + u1y * u2y;
    if (fabs(cross) < 1e-12 && dot > 0)
        return;                         /* straight on */
    double hw = s->hw;
    double sgn = cross > 0 ? -1 : 1;    /* the outer side */
    double n1x = -u1y * hw * sgn, n1y = u1x * hw * sgn;
    double n2x = -u2y * hw * sgn, n2y = u2x * hw * sgn;
    gfx_line_join_t kind = corner ? s->st->join : GFX_LINE_JOIN_ROUND;
    if (kind == GFX_LINE_JOIN_MITER) {
        double ml = s->st->miter_limit;
        if (dot > -1 + 1e-12 && 2 <= ml * ml * (1 + dot)) {
            double k = 1 / (1 + dot);
            em_begin(s);
            em_pt(s, px, py);
            em_pt(s, px + n1x, py + n1y);
            em_pt(s, px + (n1x + n2x) * k, py + (n1y + n2y) * k);
            em_pt(s, px + n2x, py + n2y);
            em_end(s);
            return;
        }
        kind = GFX_LINE_JOIN_BEVEL;
    }
    if (kind == GFX_LINE_JOIN_BEVEL) {
        em_begin(s);
        em_pt(s, px, py);
        em_pt(s, px + n1x, py + n1y);
        em_pt(s, px + n2x, py + n2y);
        em_end(s);
        return;
    }
    double a1 = atan2(n1y, n1x), a2 = atan2(n2y, n2x);
    double d = a2 - a1;
    while (d > GFX_PI) d -= 2 * GFX_PI;
    while (d < -GFX_PI) d += 2 * GFX_PI;
    if (fabs(cross) < 1e-12) {
        /* A U-turn: the half circle ahead of the line. */
        d = GFX_PI;
        if (cos(a1 + d / 2) * u1x + sin(a1 + d / 2) * u1y < 0)
            d = -GFX_PI;
    }
    em_begin(s);
    em_pt(s, px, py);
    em_arc(s, px, py, a1, a1 + d);
    em_end(s);
}

/* ---- polylines --------------------------------------------------------- */

static void
dir_of(const upt *a, const upt *b, double *ux, double *uy)
{
    double dx = b->x - a->x, dy = b->y - a->y;
    double len = sqrt(dx * dx + dy * dy);
    *ux = dx / len;
    *uy = dy / len;
}

/* Strokes n points (consecutive duplicates removed). A single point is a
 * zero-length line: drawn only for round and square caps, with the square
 * along (hx, hy). */
static void
stroke_polyline(stroker *s, const upt *p, int n, int closed, double hx,
                double hy)
{
    if (n <= 0)
        return;
    if (n == 1 || (closed && n == 2 && p[0].x == p[1].x && p[0].y == p[1].y)) {
        if (s->st->cap == GFX_LINE_CAP_ROUND) {
            em_circle(s, p[0].x, p[0].y);
        } else if (s->st->cap == GFX_LINE_CAP_SQUARE) {
            double ux = hx, uy = hy;
            if (ux == 0 && uy == 0)
                ux = 1;
            cap(s, p[0].x, p[0].y, ux, uy);
            cap(s, p[0].x, p[0].y, -ux, -uy);
        }
        return;
    }
    int nseg = closed ? n : n - 1;
    double *u = malloc((size_t)nseg * 3 * sizeof *u);   /* ux, uy, length */
    char *smooth = calloc((size_t)n, 1);
    if (!u || !smooth) {
        free(u);
        free(smooth);
        return;
    }
    for (int i = 0; i < nseg; i++) {
        const upt *a = &p[i], *b = &p[(i + 1) % n];
        dir_of(a, b, &u[3 * i], &u[3 * i + 1]);
        u[3 * i + 2] = hypot(b->x - a->x, b->y - a->y);
    }
    /* A vertex inside a curve turning gently enough that the outline's
     * offset points stay apart can be part of one outline polygon; the
     * others get joins. */
    for (int v = 0; v < n; v++) {
        int in = v - 1, out = v;
        if (!closed && (v == 0 || v == n - 1))
            continue;
        if (in < 0)
            in = nseg - 1;
        if (p[v].corner)
            continue;
        double dot = u[3 * in] * u[3 * out] + u[3 * in + 1] * u[3 * out + 1];
        if (dot <= 0)
            continue;
        double half_tan = sqrt((1 - dot) / (1 + dot));   /* tan(turn / 2) */
        double room = 0.5 * fmin(u[3 * in + 2], u[3 * out + 2]);
        smooth[v] = s->hw * half_tan < room;
    }
    /* A closed path starts at a vertex that is not smooth, if any. */
    int off = 0;
    if (closed)
        for (int v = 0; v < n; v++)
            if (!smooth[v]) {
                off = v;
                break;
            }
    if (closed)
        smooth[off] = 0;

    /* Outline polygons over runs of segments joined at smooth vertices. */
    double hw = s->hw;
    for (int i = 0; i < nseg;) {
        int j = i;
        while (j + 1 < nseg && smooth[(off + j + 1) % n])
            j++;
        em_begin(s);
        for (int side = 0; side < 2; side++) {
            double sg = side ? -1 : 1;
            for (int k = 0; k <= j - i + 1; k++) {
                int kk = side ? (j + 1) - k : i + k;     /* vertex, run-relative */
                const upt *pt = &p[(off + kk) % n];
                int sin_ = (off + kk - 1 + n) % n, sout = (off + kk) % n;
                double ox, oy;
                if (kk == i || kk == j + 1) {
                    int sg_i = kk == i ? sout : sin_;
                    if (!closed) sg_i = kk == i ? off + i : off + j;
                    sg_i %= nseg;
                    ox = -u[3 * sg_i + 1] * hw;
                    oy = u[3 * sg_i] * hw;
                } else {
                    int a1 = sin_ % nseg, a2 = sout % nseg;
                    double n1x = -u[3 * a1 + 1] * hw, n1y = u[3 * a1] * hw;
                    double n2x = -u[3 * a2 + 1] * hw, n2y = u[3 * a2] * hw;
                    double dot = u[3 * a1] * u[3 * a2] + u[3 * a1 + 1] * u[3 * a2 + 1];
                    ox = (n1x + n2x) / (1 + dot);
                    oy = (n1y + n2y) / (1 + dot);
                }
                em_pt(s, pt->x + sg * ox, pt->y + sg * oy);
            }
        }
        em_end(s);
        i = j + 1;
    }

    /* Joins where runs meet. */
    int first = closed ? 0 : 1, last = closed ? n : n - 1;
    for (int v = first; v < last; v++) {
        if (smooth[v])
            continue;
        int in = (v - 1 + nseg) % nseg, out = v % nseg;
        join(s, p[v].x, p[v].y, u[3 * in], u[3 * in + 1], u[3 * out],
             u[3 * out + 1], p[v].corner);
    }
    if (!closed) {
        cap(s, p[0].x, p[0].y, u[0], u[1]);
        cap(s, p[n - 1].x, p[n - 1].y, -u[3 * (nseg - 1)], -u[3 * (nseg - 1) + 1]);
    }
    free(u);
    free(smooth);
}

/* ---- dashes ------------------------------------------------------------ */

typedef struct {
    gfx_vec pts;      /* upt */
    double  hx, hy;   /* direction, for a zero-length dash */
} piece;

typedef struct {
    gfx_vec pieces;   /* piece */
} piece_list;

static piece *
piece_new(piece_list *l)
{
    piece *pc = gfx_vec_push(&l->pieces);
    if (pc)
        gfx_vec_init(&pc->pts, sizeof(upt));
    return pc;
}

static void
piece_add(piece *pc, double x, double y, int corner)
{
    if (pc->pts.len) {
        const upt *last = &GFX_VEC_AT(&pc->pts, upt, pc->pts.len - 1);
        if (last->x == x && last->y == y)
            return;
    }
    upt *u = gfx_vec_push(&pc->pts);
    if (u)
        *u = (upt){ x, y, corner };
}

static void
stroke_dashed(stroker *s, const upt *p, int n, int closed)
{
    const gfx_stroke_style *st = s->st;
    double period = 0;
    for (int i = 0; i < st->num_dashes; i++)
        period += st->dashes[i];
    /* Where the offset puts the start of the path within the pattern. */
    int di = 0;
    double off = fmod(st->dash_offset, period);
    if (off < 0)
        off += period;
    while (off > 0 && off >= st->dashes[di]) {
        off -= st->dashes[di];
        di = (di + 1) % st->num_dashes;
    }
    double rem = st->dashes[di] - off;
    int on = (di % 2) == 0;
    int start_on = on;

    piece_list l;
    gfx_vec_init(&l.pieces, sizeof(piece));
    piece *cur = NULL;
    if (on) {
        cur = piece_new(&l);
        if (cur)
            piece_add(cur, p[0].x, p[0].y, 1);
    }
    int nseg = closed ? n : n - 1;
    for (int i = 0; i < nseg; i++) {
        const upt *a = &p[i], *b = &p[(i + 1) % n];
        double ux, uy;
        dir_of(a, b, &ux, &uy);
        double dx = b->x - a->x, dy = b->y - a->y;
        double len = sqrt(dx * dx + dy * dy), pos = 0;
        while (len - pos >= rem) {
            pos += rem;
            double x = a->x + ux * pos, y = a->y + uy * pos;
            if (on) {
                if (cur) {
                    piece_add(cur, x, y, 1);
                    cur->hx = ux;
                    cur->hy = uy;
                }
                cur = NULL;
            } else {
                cur = piece_new(&l);
                if (cur) {
                    piece_add(cur, x, y, 1);
                    cur->hx = ux;
                    cur->hy = uy;
                }
            }
            on = !on;
            di = (di + 1) % st->num_dashes;
            rem = st->dashes[di];
        }
        rem -= len - pos;
        if (on && cur) {
            piece_add(cur, b->x, b->y, b->corner);
            cur->hx = ux;
            cur->hy = uy;
        }
    }
    /* A closed path whose dash runs over its start joins up. */
    size_t np = l.pieces.len;
    int merge = closed && on && start_on && cur && np >= 2;
    for (size_t k = merge ? 1 : 0; k < np; k++) {
        piece *pc = &GFX_VEC_AT(&l.pieces, piece, k);
        if (merge && k == np - 1) {
            piece *first = &GFX_VEC_AT(&l.pieces, piece, 0);
            for (size_t j = 0; j < first->pts.len; j++) {
                const upt *u = &GFX_VEC_AT(&first->pts, upt, j);
                piece_add(pc, u->x, u->y, u->corner);
            }
        }
        stroke_polyline(s, pc->pts.data, (int)pc->pts.len, 0, pc->hx, pc->hy);
    }
    for (size_t k = 0; k < np; k++)
        gfx_vec_free(&GFX_VEC_AT(&l.pieces, piece, k).pts);
    gfx_vec_free(&l.pieces);
}

/* ---- entry point ------------------------------------------------------- */

void
gfx_stroke_to_polygon(const gfx_flat *pixels, const gfx_matrix_t *to_pixels,
                      const gfx_stroke_style *style, gfx_polygon *out)
{
    if (!(style->width > 0))
        return;
    gfx_matrix_t inv = *to_pixels;
    if (gfx_matrix_invert(&inv) != GFX_STATUS_SUCCESS)
        return;
    stroker s = { .out = out, .m = to_pixels, .st = style,
                  .hw = style->width / 2 };
    double cx = hypot(to_pixels->xx, to_pixels->yx);
    double cy = hypot(to_pixels->xy, to_pixels->yy);
    s.scale = fmax(cx, cy);
    gfx_vec_init(&s.buf, 2 * sizeof(double));

    int dashed = 0;
    if (style->num_dashes > 0 && style->dashes) {
        double period = 0;
        int bad = 0;
        for (int i = 0; i < style->num_dashes; i++) {
            if (style->dashes[i] < 0)
                bad = 1;
            period += style->dashes[i];
        }
        dashed = !bad && period > 0;
    }

    gfx_vec u;
    gfx_vec_init(&u, sizeof(upt));
    for (size_t si = 0; si < pixels->subs.len; si++) {
        const gfx_fsub *sub = &GFX_VEC_AT(&pixels->subs, gfx_fsub, si);
        if (sub->count < 1 || (sub->count == 1 && !sub->closed))
            continue;
        gfx_vec_clear(&u);
        const gfx_fpoint *fp = &GFX_VEC_AT(&pixels->pts, gfx_fpoint, sub->start);
        for (int i = 0; i < sub->count; i++) {
            double x = fp[i].x, y = fp[i].y;
            gfx_matrix_transform_point(&inv, &x, &y);
            if (u.len) {
                upt *last = &GFX_VEC_AT(&u, upt, u.len - 1);
                if (fabs(last->x - x) < 1e-9 && fabs(last->y - y) < 1e-9) {
                    last->corner |= fp[i].corner;
                    continue;
                }
            }
            upt *q = gfx_vec_push(&u);
            if (q)
                *q = (upt){ x, y, fp[i].corner };
        }
        int n = (int)u.len;
        const upt *pts = u.data;
        int closed = sub->closed;
        /* A closed path ending where it began has that point once. */
        if (closed && n >= 2 && fabs(pts[0].x - pts[n - 1].x) < 1e-9 &&
            fabs(pts[0].y - pts[n - 1].y) < 1e-9)
            n--;
        if (dashed && n >= 2)
            stroke_dashed(&s, pts, n, closed);
        else
            stroke_polyline(&s, pts, n, closed && n >= 2, 0, 0);
    }
    gfx_vec_free(&u);
    gfx_vec_free(&s.buf);
}
