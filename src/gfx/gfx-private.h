/* Weststar SDL Browser — gfx internals shared between its source files.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_GFX_PRIVATE_H
#define NS_GFX_PRIVATE_H

#include "gfx.h"

#include <stdint.h>

#define GFX_PI 3.14159265358979323846

/* ---- surfaces ---------------------------------------------------------- */

struct gfx_surface {
    int            ref;
    gfx_status_t   status;
    gfx_format_t   format;
    int            width, height, stride;
    unsigned char *data;
    int            owns_data;
    double         dev_x, dev_y;          /* device offset */
    double         dev_sx, dev_sy;        /* device scale */
};

gfx_surface_t *gfx_surface_nil(gfx_status_t status);

/* ---- a growable array -------------------------------------------------- */

typedef struct {
    void  *data;
    size_t len, cap, elem;
} gfx_vec;

void  gfx_vec_init(gfx_vec *v, size_t elem);
void  gfx_vec_free(gfx_vec *v);
void *gfx_vec_push(gfx_vec *v);        /* zeroed slot; NULL on no memory */
void  gfx_vec_clear(gfx_vec *v);
#define GFX_VEC_AT(v, type, i) (((type *)(v)->data)[i])

/* ---- paths: device-pixel coordinates ----------------------------------- */

typedef struct {
    gfx_vec data;                 /* gfx_path_data_t, Cairo's layout */
    int     has_current;
    double  cur_x, cur_y;         /* current point, pixels */
    int     has_move;
    double  move_x, move_y;       /* start of the current sub-path */
    int     need_move;            /* after close_path */
    int     tail_is_move;         /* the last op is a move_to */
} gfx_pathbuf;

void gfx_pathbuf_init(gfx_pathbuf *p);
void gfx_pathbuf_free(gfx_pathbuf *p);
void gfx_pathbuf_clear(gfx_pathbuf *p);
int  gfx_pathbuf_copy(gfx_pathbuf *dst, const gfx_pathbuf *src);
void gfx_pathbuf_move_to(gfx_pathbuf *p, double x, double y);
void gfx_pathbuf_line_to(gfx_pathbuf *p, double x, double y);
void gfx_pathbuf_curve_to(gfx_pathbuf *p, double x1, double y1, double x2,
                          double y2, double x3, double y3);
void gfx_pathbuf_close(gfx_pathbuf *p);
void gfx_pathbuf_translate(gfx_pathbuf *p, double dx, double dy);

/* A flattened path: points grouped into sub-paths. */
typedef struct {
    double x, y;
    int    corner;   /* an end point of a path segment, not inside a curve */
} gfx_fpoint;

typedef struct {
    int start, count;
    int closed;
} gfx_fsub;

typedef struct {
    gfx_vec pts;     /* gfx_fpoint */
    gfx_vec subs;    /* gfx_fsub */
} gfx_flat;

void gfx_flat_init(gfx_flat *f);
void gfx_flat_free(gfx_flat *f);
void gfx_flat_clear(gfx_flat *f);
int  gfx_flat_begin(gfx_flat *f);
int  gfx_flat_add(gfx_flat *f, double x, double y, int corner);
void gfx_flat_end(gfx_flat *f, int closed);
/* Flattens curves to within tolerance pixels. */
int  gfx_flatten(const gfx_pathbuf *p, double tolerance, gfx_flat *out);

/* ---- the rasterizer ---------------------------------------------------- */

typedef struct {
    double x0, y0, x1, y1;   /* y0 < y1 */
    double dir;              /* +1 downwards, -1 upwards */
} gfx_edge;

typedef struct {
    gfx_vec edges;           /* gfx_edge, pixel coordinates */
    double  minx, miny, maxx, maxy;
} gfx_polygon;

void gfx_polygon_init(gfx_polygon *poly);
void gfx_polygon_free(gfx_polygon *poly);
void gfx_polygon_clear(gfx_polygon *poly);
void gfx_polygon_add_line(gfx_polygon *poly, double x0, double y0,
                          double x1, double y1);
/* Adds every sub-path of f, each closed. */
void gfx_polygon_add_flat(gfx_polygon *poly, const gfx_flat *f);

/* Receives a row of coverage, 0..255, for pixels x .. x + len - 1. */
typedef void (*gfx_span_func)(void *closure, int y, int x, int len,
                              const uint8_t *coverage);

/* Rasterizes the polygon within the pixel box [bx0, bx1) x [by0, by1),
 * calling span for each row it touches. */
void gfx_rasterize(const gfx_polygon *poly, gfx_fill_rule_t rule,
                   gfx_antialias_t aa, int bx0, int by0, int bx1, int by1,
                   gfx_span_func span, void *closure);

/* Point in polygon by its winding number. */
int gfx_polygon_contains(const gfx_polygon *poly, gfx_fill_rule_t rule,
                         double x, double y);

/* ---- strokes ----------------------------------------------------------- */

typedef struct {
    double          width;
    gfx_line_cap_t  cap;
    gfx_line_join_t join;
    double          miter_limit;
    const double   *dashes;
    int             num_dashes;
    double          dash_offset;
    double          tolerance;
} gfx_stroke_style;

/* Strokes a flattened path given in pixels, with the pen in the user space
 * that to_pixels maps from. Polygons are added to out in pixels, all wound
 * the same way, to be filled with the non-zero rule. */
void gfx_stroke_to_polygon(const gfx_flat *pixels,
                           const gfx_matrix_t *to_pixels,
                           const gfx_stroke_style *style, gfx_polygon *out);

/* ---- patterns ---------------------------------------------------------- */

typedef enum {
    GFX_PATTERN_SOLID,
    GFX_PATTERN_SURFACE,
    GFX_PATTERN_LINEAR,
    GFX_PATTERN_RADIAL,
    GFX_PATTERN_MESH,
} gfx_pattern_type;

typedef struct {
    double offset;
    double r, g, b, a;
} gfx_stop;

typedef struct {
    double x[4], y[4];       /* corners */
    double c[4][4];          /* corner colours, premultiplied r g b a */
    int    nsides;           /* sides given so far */
    int    color_set[4];
} gfx_patch;

struct gfx_pattern {
    int              ref;
    gfx_status_t     status;
    gfx_pattern_type type;
    gfx_matrix_t     matrix;     /* user space -> pattern space */
    gfx_extend_t     extend;
    gfx_filter_t     filter;
    double           r, g, b, a; /* solid, unpremultiplied */
    gfx_surface_t   *surface;
    double           p[6];       /* linear x0 y0 x1 y1; radial cx0 cy0 r0 cx1 cy1 r1 */
    gfx_vec          stops;      /* gfx_stop, sorted by offset */
    gfx_vec          patches;    /* gfx_patch */
    gfx_patch       *current;    /* the patch being defined */
};

/* A pattern prepared for drawing: pixels -> pattern space. */
typedef struct {
    const gfx_pattern_t *pat;
    gfx_matrix_t         to_pattern;
    int                  opaque;      /* every pixel it yields has alpha 1 */
    uint32_t             solid;       /* premultiplied, for SOLID */
    /* Surface sampling: pattern space -> surface pixels. */
    gfx_matrix_t         to_texel;
    int                  texel_int;   /* to_texel is an integer translation */
    int                  tx, ty;
    double               alpha;       /* multiplies every colour */
} gfx_source;

/* The pixels a source can be non-transparent in, for an image that does
 * not extend; FALSE when unbounded. */
int  gfx_source_extent(const gfx_source *src, int *x0, int *y0, int *x1,
                       int *y1);
void gfx_source_init(gfx_source *src, const gfx_pattern_t *pat,
                     const gfx_matrix_t *to_pattern, double alpha);
/* Fills out[0 .. len) with premultiplied ARGB for row y, pixels x .. */
void gfx_source_fetch(const gfx_source *src, int y, int x, int len,
                      uint32_t *out);

/* ---- compositing ------------------------------------------------------- */

/* Operators for which a transparent source leaves the destination
 * unchanged; the others clear what lies outside the shape. */
int gfx_operator_bounded(gfx_operator_t op);

/* Composites len pixels of src into dst row with coverage (NULL: all 255).
 * dst points at the first pixel of the run in a surface of format fmt. */
/* The same for a solid premultiplied colour s. */
void gfx_composite_solid(gfx_operator_t op, gfx_format_t fmt,
                         unsigned char *dst, uint32_t s,
                         const uint8_t *coverage, int len);
void gfx_composite_row(gfx_operator_t op, gfx_format_t fmt,
                       unsigned char *dst, const uint32_t *src,
                       const uint8_t *coverage, int len);

static inline uint8_t
gfx_mul8(unsigned a, unsigned b)
{
    unsigned t = a * b + 128;
    return (uint8_t)((t + (t >> 8)) >> 8);
}

static inline double
gfx_clampd(double v, double lo, double hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* r g b a premultiplied, 0..1, to a pixel. */
static inline uint32_t
gfx_pack(double r, double g, double b, double a)
{
    if (!(a > 0))
        return 0;
    a = gfx_clampd(a, 0, 1);
    r = gfx_clampd(r, 0, a);
    g = gfx_clampd(g, 0, a);
    b = gfx_clampd(b, 0, a);
    return ((uint32_t)(a * 255 + 0.5) << 24) | ((uint32_t)(r * 255 + 0.5) << 16) |
           ((uint32_t)(g * 255 + 0.5) << 8) | (uint32_t)(b * 255 + 0.5);
}

#endif
