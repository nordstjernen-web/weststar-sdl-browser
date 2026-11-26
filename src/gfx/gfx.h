/* Weststar SDL Browser — gfx, the in-tree 2D rasterizer that replaces Cairo.
 *
 * The API is the subset of Cairo's that the engine uses, with Cairo's
 * semantics, under a gfx_ prefix: paths in user space under a current
 * transformation matrix, fills (non-zero and even-odd), strokes with
 * joins, caps and dashes, clipping, solid, linear, radial, mesh and surface
 * patterns, the Porter-Duff and blend operators, groups and masks.
 *
 * Pixels are Cairo's: native-endian 32-bit words holding premultiplied
 * ARGB (GFX_FORMAT_ARGB32), the same words with the alpha byte ignored
 * (GFX_FORMAT_RGB24), or 8-bit alpha (GFX_FORMAT_A8). An ARGB32 surface is
 * SDL_PIXELFORMAT_ARGB8888 with premultiplied alpha, so it can be wrapped
 * as an SDL_Surface or uploaded to an SDL_Texture without conversion.
 *
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_GFX_H
#define NS_GFX_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int gfx_bool_t;

typedef enum {
    GFX_STATUS_SUCCESS = 0,
    GFX_STATUS_NO_MEMORY,
    GFX_STATUS_INVALID_RESTORE,
    GFX_STATUS_INVALID_POP_GROUP,
    GFX_STATUS_NO_CURRENT_POINT,
    GFX_STATUS_INVALID_MATRIX,
    GFX_STATUS_INVALID_SIZE,
    GFX_STATUS_INVALID_FORMAT,
    GFX_STATUS_WRITE_ERROR,
} gfx_status_t;

typedef enum {
    GFX_FORMAT_INVALID = -1,
    GFX_FORMAT_ARGB32 = 0,
    GFX_FORMAT_RGB24 = 1,
    GFX_FORMAT_A8 = 2,
} gfx_format_t;

/* In Cairo's order, so values can be passed straight through. */
typedef enum {
    GFX_OPERATOR_CLEAR,
    GFX_OPERATOR_SOURCE,
    GFX_OPERATOR_OVER,
    GFX_OPERATOR_IN,
    GFX_OPERATOR_OUT,
    GFX_OPERATOR_ATOP,
    GFX_OPERATOR_DEST,
    GFX_OPERATOR_DEST_OVER,
    GFX_OPERATOR_DEST_IN,
    GFX_OPERATOR_DEST_OUT,
    GFX_OPERATOR_DEST_ATOP,
    GFX_OPERATOR_XOR,
    GFX_OPERATOR_ADD,
    GFX_OPERATOR_SATURATE,
    GFX_OPERATOR_MULTIPLY,
    GFX_OPERATOR_SCREEN,
    GFX_OPERATOR_OVERLAY,
    GFX_OPERATOR_DARKEN,
    GFX_OPERATOR_LIGHTEN,
    GFX_OPERATOR_COLOR_DODGE,
    GFX_OPERATOR_COLOR_BURN,
    GFX_OPERATOR_HARD_LIGHT,
    GFX_OPERATOR_SOFT_LIGHT,
    GFX_OPERATOR_DIFFERENCE,
    GFX_OPERATOR_EXCLUSION,
    GFX_OPERATOR_HSL_HUE,
    GFX_OPERATOR_HSL_SATURATION,
    GFX_OPERATOR_HSL_COLOR,
    GFX_OPERATOR_HSL_LUMINOSITY,
} gfx_operator_t;

typedef enum {
    GFX_FILL_RULE_WINDING,
    GFX_FILL_RULE_EVEN_ODD,
} gfx_fill_rule_t;

typedef enum {
    GFX_LINE_CAP_BUTT,
    GFX_LINE_CAP_ROUND,
    GFX_LINE_CAP_SQUARE,
} gfx_line_cap_t;

typedef enum {
    GFX_LINE_JOIN_MITER,
    GFX_LINE_JOIN_ROUND,
    GFX_LINE_JOIN_BEVEL,
} gfx_line_join_t;

typedef enum {
    GFX_EXTEND_NONE,
    GFX_EXTEND_REPEAT,
    GFX_EXTEND_REFLECT,
    GFX_EXTEND_PAD,
} gfx_extend_t;

typedef enum {
    GFX_FILTER_FAST,
    GFX_FILTER_GOOD,
    GFX_FILTER_BEST,
    GFX_FILTER_NEAREST,
    GFX_FILTER_BILINEAR,
} gfx_filter_t;

typedef enum {
    GFX_ANTIALIAS_DEFAULT,
    GFX_ANTIALIAS_NONE,
    GFX_ANTIALIAS_GRAY,
    GFX_ANTIALIAS_SUBPIXEL,
    GFX_ANTIALIAS_FAST,
    GFX_ANTIALIAS_GOOD,
    GFX_ANTIALIAS_BEST,
} gfx_antialias_t;

/* Laid out as cairo_matrix_t: x' = xx*x + xy*y + x0, y' = yx*x + yy*y + y0. */
typedef struct {
    double xx, yx;
    double xy, yy;
    double x0, y0;
} gfx_matrix_t;

typedef struct {
    double x, y, width, height;
} gfx_rectangle_t;

typedef struct {
    int x, y, width, height;
} gfx_rectangle_int_t;

typedef enum {
    GFX_PATH_MOVE_TO,
    GFX_PATH_LINE_TO,
    GFX_PATH_CURVE_TO,
    GFX_PATH_CLOSE_PATH,
} gfx_path_data_type_t;

/* Laid out as cairo_path_data_t: a header, then header.length - 1 points. */
typedef union {
    struct {
        gfx_path_data_type_t type;
        int length;
    } header;
    struct {
        double x, y;
    } point;
} gfx_path_data_t;

typedef struct {
    gfx_status_t     status;
    gfx_path_data_t *data;
    int              num_data;
} gfx_path_t;

typedef struct gfx_surface gfx_surface_t;
typedef struct gfx_region  gfx_region_t;
typedef struct gfx_pattern gfx_pattern_t;
typedef struct gfx         gfx_t;

typedef gfx_status_t (*gfx_write_func_t)(void *closure,
                                         const unsigned char *data,
                                         unsigned int length);

/* ---- matrices ---------------------------------------------------------- */

void gfx_matrix_init(gfx_matrix_t *m, double xx, double yx, double xy,
                     double yy, double x0, double y0);
void gfx_matrix_init_identity(gfx_matrix_t *m);
void gfx_matrix_init_translate(gfx_matrix_t *m, double tx, double ty);
void gfx_matrix_init_scale(gfx_matrix_t *m, double sx, double sy);
void gfx_matrix_init_rotate(gfx_matrix_t *m, double radians);
void gfx_matrix_translate(gfx_matrix_t *m, double tx, double ty);
void gfx_matrix_scale(gfx_matrix_t *m, double sx, double sy);
void gfx_matrix_rotate(gfx_matrix_t *m, double radians);
/* result = a then b, as cairo_matrix_multiply. result may alias a or b. */
void gfx_matrix_multiply(gfx_matrix_t *result, const gfx_matrix_t *a,
                         const gfx_matrix_t *b);
gfx_status_t gfx_matrix_invert(gfx_matrix_t *m);
void gfx_matrix_transform_point(const gfx_matrix_t *m, double *x, double *y);
void gfx_matrix_transform_distance(const gfx_matrix_t *m, double *dx,
                                   double *dy);

/* ---- surfaces ---------------------------------------------------------- */

gfx_surface_t *gfx_image_surface_create(gfx_format_t format, int width,
                                        int height);
/* Wraps caller-owned pixels; they must outlive the surface. */
gfx_surface_t *gfx_image_surface_create_for_data(unsigned char *data,
                                                 gfx_format_t format,
                                                 int width, int height,
                                                 int stride);
int            gfx_format_stride_for_width(gfx_format_t format, int width);
gfx_surface_t *gfx_surface_reference(gfx_surface_t *s);
void           gfx_surface_destroy(gfx_surface_t *s);
gfx_status_t   gfx_surface_status(const gfx_surface_t *s);
unsigned char *gfx_image_surface_get_data(gfx_surface_t *s);
gfx_format_t   gfx_image_surface_get_format(const gfx_surface_t *s);
int            gfx_image_surface_get_width(const gfx_surface_t *s);
int            gfx_image_surface_get_height(const gfx_surface_t *s);
int            gfx_image_surface_get_stride(const gfx_surface_t *s);
/* No-ops: gfx draws straight into the pixels. Kept so call sites that
 * hand pixels to other code read as they did with Cairo. */
void           gfx_surface_flush(gfx_surface_t *s);
void           gfx_surface_mark_dirty(gfx_surface_t *s);
void           gfx_surface_set_device_offset(gfx_surface_t *s, double x,
                                             double y);
void           gfx_surface_get_device_offset(const gfx_surface_t *s,
                                             double *x, double *y);
void           gfx_surface_set_device_scale(gfx_surface_t *s, double sx,
                                            double sy);
void           gfx_surface_get_device_scale(const gfx_surface_t *s,
                                            double *sx, double *sy);
gfx_status_t   gfx_surface_write_to_png_stream(gfx_surface_t *s,
                                               gfx_write_func_t write,
                                               void *closure);
gfx_status_t   gfx_surface_write_to_png(gfx_surface_t *s,
                                        const char *filename);

/* ---- patterns ---------------------------------------------------------- */

gfx_pattern_t *gfx_pattern_create_rgb(double r, double g, double b);
gfx_pattern_t *gfx_pattern_create_rgba(double r, double g, double b,
                                       double a);
gfx_pattern_t *gfx_pattern_create_for_surface(gfx_surface_t *s);
gfx_pattern_t *gfx_pattern_create_linear(double x0, double y0, double x1,
                                         double y1);
gfx_pattern_t *gfx_pattern_create_radial(double cx0, double cy0, double r0,
                                         double cx1, double cy1, double r1);
gfx_pattern_t *gfx_pattern_create_mesh(void);
gfx_pattern_t *gfx_pattern_reference(gfx_pattern_t *p);
void           gfx_pattern_destroy(gfx_pattern_t *p);
gfx_status_t   gfx_pattern_status(const gfx_pattern_t *p);
void gfx_pattern_add_color_stop_rgb(gfx_pattern_t *p, double offset,
                                    double r, double g, double b);
void gfx_pattern_add_color_stop_rgba(gfx_pattern_t *p, double offset,
                                     double r, double g, double b, double a);
void gfx_pattern_set_matrix(gfx_pattern_t *p, const gfx_matrix_t *m);
void gfx_pattern_get_matrix(const gfx_pattern_t *p, gfx_matrix_t *m);
void gfx_pattern_set_extend(gfx_pattern_t *p, gfx_extend_t extend);
gfx_extend_t gfx_pattern_get_extend(const gfx_pattern_t *p);
void gfx_pattern_set_filter(gfx_pattern_t *p, gfx_filter_t filter);
gfx_filter_t gfx_pattern_get_filter(const gfx_pattern_t *p);
/* A solid pattern's colour; FALSE for other patterns. */
gfx_bool_t gfx_pattern_get_rgba(const gfx_pattern_t *p, double *r, double *g,
                                double *b, double *a);
/* The surface of a surface pattern (not referenced), else NULL. */
gfx_surface_t *gfx_pattern_get_surface(const gfx_pattern_t *p);

/* Mesh patches. Each patch has four sides; a side given with line_to is
 * straight. Sides given with curve_to are drawn as the straight line
 * between their end points: only straight-sided patches (the canvas conic
 * gradient) are used by the engine. */
void gfx_mesh_pattern_begin_patch(gfx_pattern_t *p);
void gfx_mesh_pattern_end_patch(gfx_pattern_t *p);
void gfx_mesh_pattern_move_to(gfx_pattern_t *p, double x, double y);
void gfx_mesh_pattern_line_to(gfx_pattern_t *p, double x, double y);
void gfx_mesh_pattern_curve_to(gfx_pattern_t *p, double x1, double y1,
                               double x2, double y2, double x3, double y3);
void gfx_mesh_pattern_set_corner_color_rgb(gfx_pattern_t *p,
                                           unsigned int corner, double r,
                                           double g, double b);
void gfx_mesh_pattern_set_corner_color_rgba(gfx_pattern_t *p,
                                            unsigned int corner, double r,
                                            double g, double b, double a);

/* ---- contexts ---------------------------------------------------------- */

gfx_t         *gfx_create(gfx_surface_t *target);
gfx_t         *gfx_reference(gfx_t *g);
void           gfx_destroy(gfx_t *g);
gfx_status_t   gfx_status(const gfx_t *g);
const char    *gfx_status_to_string(gfx_status_t status);
gfx_surface_t *gfx_get_target(gfx_t *g);
gfx_surface_t *gfx_get_group_target(gfx_t *g);

void gfx_save(gfx_t *g);
void gfx_restore(gfx_t *g);

/* Groups draw into an intermediate ARGB32 surface the size of the current
 * clip. */
void           gfx_push_group(gfx_t *g);
gfx_pattern_t *gfx_pop_group(gfx_t *g);
void           gfx_pop_group_to_source(gfx_t *g);

void gfx_set_source_rgb(gfx_t *g, double r, double g_, double b);
void gfx_set_source_rgba(gfx_t *g, double r, double g_, double b, double a);
/* The pattern is locked to the user space in effect now, as in Cairo. */
void gfx_set_source(gfx_t *g, gfx_pattern_t *p);
void gfx_set_source_surface(gfx_t *g, gfx_surface_t *s, double x, double y);
gfx_pattern_t *gfx_get_source(gfx_t *g);

void            gfx_set_operator(gfx_t *g, gfx_operator_t op);
gfx_operator_t  gfx_get_operator(const gfx_t *g);
void            gfx_set_tolerance(gfx_t *g, double tolerance);
void            gfx_set_antialias(gfx_t *g, gfx_antialias_t aa);
gfx_antialias_t gfx_get_antialias(const gfx_t *g);
void            gfx_set_fill_rule(gfx_t *g, gfx_fill_rule_t rule);
gfx_fill_rule_t gfx_get_fill_rule(const gfx_t *g);
void            gfx_set_line_width(gfx_t *g, double width);
double          gfx_get_line_width(const gfx_t *g);
void            gfx_set_line_cap(gfx_t *g, gfx_line_cap_t cap);
gfx_line_cap_t  gfx_get_line_cap(const gfx_t *g);
void            gfx_set_line_join(gfx_t *g, gfx_line_join_t join);
gfx_line_join_t gfx_get_line_join(const gfx_t *g);
void            gfx_set_miter_limit(gfx_t *g, double limit);
void            gfx_set_dash(gfx_t *g, const double *dashes, int num_dashes,
                             double offset);

/* ---- transformation ---------------------------------------------------- */

void gfx_translate(gfx_t *g, double tx, double ty);
void gfx_scale(gfx_t *g, double sx, double sy);
void gfx_rotate(gfx_t *g, double radians);
void gfx_transform(gfx_t *g, const gfx_matrix_t *m);
void gfx_set_matrix(gfx_t *g, const gfx_matrix_t *m);
void gfx_get_matrix(const gfx_t *g, gfx_matrix_t *m);
void gfx_identity_matrix(gfx_t *g);
void gfx_user_to_device(const gfx_t *g, double *x, double *y);
void gfx_user_to_device_distance(const gfx_t *g, double *dx, double *dy);
void gfx_device_to_user(const gfx_t *g, double *x, double *y);
void gfx_device_to_user_distance(const gfx_t *g, double *dx, double *dy);

/* ---- paths ------------------------------------------------------------- */

void gfx_new_path(gfx_t *g);
void gfx_new_sub_path(gfx_t *g);
void gfx_move_to(gfx_t *g, double x, double y);
void gfx_line_to(gfx_t *g, double x, double y);
void gfx_curve_to(gfx_t *g, double x1, double y1, double x2, double y2,
                  double x3, double y3);
void gfx_rel_move_to(gfx_t *g, double dx, double dy);
void gfx_rel_line_to(gfx_t *g, double dx, double dy);
void gfx_rel_curve_to(gfx_t *g, double dx1, double dy1, double dx2,
                      double dy2, double dx3, double dy3);
void gfx_arc(gfx_t *g, double xc, double yc, double radius, double angle1,
             double angle2);
void gfx_arc_negative(gfx_t *g, double xc, double yc, double radius,
                      double angle1, double angle2);
void gfx_rectangle(gfx_t *g, double x, double y, double w, double h);
void gfx_close_path(gfx_t *g);
gfx_bool_t  gfx_has_current_point(const gfx_t *g);
void        gfx_get_current_point(const gfx_t *g, double *x, double *y);
gfx_path_t *gfx_copy_path(gfx_t *g);
gfx_path_t *gfx_copy_path_flat(gfx_t *g);
void        gfx_append_path(gfx_t *g, const gfx_path_t *path);
void        gfx_path_destroy(gfx_path_t *path);
void        gfx_path_extents(gfx_t *g, double *x1, double *y1, double *x2,
                             double *y2);

/* ---- drawing ----------------------------------------------------------- */

void gfx_paint(gfx_t *g);
void gfx_paint_with_alpha(gfx_t *g, double alpha);
void gfx_mask(gfx_t *g, gfx_pattern_t *mask);
void gfx_mask_surface(gfx_t *g, gfx_surface_t *mask, double x, double y);
void gfx_fill(gfx_t *g);
void gfx_fill_preserve(gfx_t *g);
void gfx_fill_extents(gfx_t *g, double *x1, double *y1, double *x2,
                      double *y2);
void gfx_stroke(gfx_t *g);
void gfx_stroke_preserve(gfx_t *g);
void gfx_stroke_extents(gfx_t *g, double *x1, double *y1, double *x2,
                        double *y2);
gfx_bool_t gfx_in_fill(gfx_t *g, double x, double y);
gfx_bool_t gfx_in_stroke(gfx_t *g, double x, double y);

void gfx_clip(gfx_t *g);
void gfx_clip_preserve(gfx_t *g);
void gfx_reset_clip(gfx_t *g);
/* The clip's bounding box in user space. */
void gfx_clip_extents(gfx_t *g, double *x1, double *y1, double *x2,
                      double *y2);
/* The clip's bounding box in the target's pixels; FALSE when the clip is
 * empty. *is_rect is set when the clip is exactly that box. */
gfx_bool_t gfx_clip_pixel_box(gfx_t *g, gfx_rectangle_int_t *box,
                              gfx_bool_t *is_rect);

/* ---- regions: sets of pixels, as non-overlapping rectangles ------------ */

gfx_region_t *gfx_region_create(void);
gfx_region_t *gfx_region_create_rectangle(const gfx_rectangle_int_t *rect);
void          gfx_region_destroy(gfx_region_t *r);
void          gfx_region_union_rectangle(gfx_region_t *r,
                                         const gfx_rectangle_int_t *rect);
int           gfx_region_num_rectangles(const gfx_region_t *r);
void          gfx_region_get_rectangle(const gfx_region_t *r, int i,
                                       gfx_rectangle_int_t *out);
void          gfx_region_get_extents(const gfx_region_t *r,
                                     gfx_rectangle_int_t *out);
gfx_bool_t    gfx_region_is_empty(const gfx_region_t *r);

#ifdef __cplusplus
}
#endif

#endif
