/* Weststar SDL Browser — compositing layers: which boxes the compositor moves and fades.
 * Fixed boxes and elements animating only transform or opacity are painted once into
 * buffers of their own and composited over the page, on the GPU in the shell or by gfx.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_LAYERS_H
#define NS_LAYERS_H

#include <glib.h>

#include "anim.h"
#include "damage.h"
#include "gfx.h"
#include "layout.h"

G_BEGIN_DECLS

/* At most this many layers on a page; more candidates are painted into it. */
#define NS_LAYERS_MAX 32

typedef struct ns_layer {
    const ns_box  *box;
    const ns_node *node;
    /* A fixed layer stays where it is in the viewport while the page
     * scrolls; the others move with the page. */
    gboolean       fixed;
    /* The subtree's paint extent before its own transform, CSS px, in the
     * box's coordinates (the viewport's for a fixed box). */
    double         x0, y0, x1, y1;
    /* This frame: the box's transform in the same space, its opacity, and
     * the transformed extent. */
    gfx_matrix_t   m;
    double         opacity;
    gboolean       visible;
    double         bx0, by0, bx1, by1;
    /* Whether the compositor shows it this frame, or the page paints it. */
    gboolean       active;
    /* Planning: the stacking order, and what paints above the box. */
    int            elevated;
    int            z;
    guint          order;
    int            slot_elevated, slot_z;
    guint          slot_order;
    gboolean       entered, exited, rejected;
    GArray        *above;      /* ns_damage_rect, CSS px */
    GHashTable    *ancestors;  /* the boxes it is inside */
} ns_layer;

typedef struct ns_layer_plan {
    GArray     *layers;        /* ns_layer, in paint order */
    GHashTable *composite;     /* the animated elements the plan saw */
} ns_layer_plan;

/* FALSE when NS_LAYERS=0: everything is painted into the page. */
gboolean ns_layers_enabled(void);

/* Finds the layers of a laid-out page. max_w and max_h bound a layer's
 * size in device pixels, max_area its area. */
void ns_layers_plan_build(ns_layer_plan *plan, const ns_box *root,
                          ns_anim *anim, GPtrArray *composite_nodes,
                          double scale, double max_w, double max_h,
                          double max_area);
void ns_layers_plan_clear(ns_layer_plan *plan);

/* TRUE if the plan was built for exactly these animated elements. */
gboolean ns_layers_plan_same_nodes(const ns_layer_plan *plan,
                                   GPtrArray *composite_nodes);

/* Updates every layer's transform, opacity and bounds for this frame and
 * decides which are active: those that nothing painted above them in the
 * page overlaps at this scroll offset (CSS px), and with axis_only those
 * not turned, skewed or mirrored. Returns how many are. */
int ns_layers_plan_resolve(ns_layer_plan *plan, ns_anim *anim,
                           double scroll_x, double scroll_y,
                           gboolean axis_only);

/* The layer's pixel buffer: its untransformed extent in device pixels of
 * the viewport, at this scale and scroll offset, grown to whole pixels. */
void ns_layer_device_box(const ns_layer *l, double scale, double scroll_x,
                         double scroll_y, int *x, int *y, int *w, int *h);

/* The layer's transform in device pixels of the viewport: where a point
 * of its untransformed buffer is shown. */
void ns_layer_device_matrix(const ns_layer *l, double scale, double scroll_x,
                            double scroll_y, gfx_matrix_t *out);

/* Draws a layer's premultiplied ARGB32 buffer, whose top left pixel is at
 * (x, y) untransformed, through the device matrix and with an opacity. */
void ns_layers_composite(gfx_t *cr, unsigned char *pixels, int w, int h,
                         int stride, int x, int y, const gfx_matrix_t *m,
                         double opacity);

/* Paints a whole page as ns_paint does, but with its layers painted on
 * their own and composited by gfx: the CPU path of the compositor, for
 * --dump=png. FALSE if the page has no layers (nothing was painted). */
gboolean ns_layers_paint_page(gfx_t *cr, const ns_box *root, ns_anim *anim,
                              int width, int height);

G_END_DECLS

#endif
