/* Weststar SDL Browser — compositing layers: which boxes the compositor moves and fades.
 * A box gets a layer when nothing the page paints after it can overlap it, so drawing
 * its buffer over the page gives the pixels painting it in place would.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "layers.h"

#include <math.h>
#include <string.h>

#include "css.h"
#include "dom.h"
#include "paint.h"
#include "plat/ns_env.h"

/* Room around a subtree's computed extent for antialiasing and ink the
 * extent misses, CSS px. */
#define NS_LAYER_PAD 4.0

/* NS_LAYERS: 0 off, "log" on and telling what it decides, else on. */
static int
layers_mode(void)
{
    static gint mode = -1;
    if (G_UNLIKELY(mode < 0)) {
        const char *v = ns_getenv("NS_LAYERS");
        mode = v && strcmp(v, "0") == 0 ? 0
             : v && strcmp(v, "log") == 0 ? 2 : 1;
    }
    return mode;
}

gboolean
ns_layers_enabled(void)
{
    return layers_mode() > 0;
}

static const char *
node_label(const ns_node *n)
{
    const char *id = n ? ns_element_get_attr(n, "id") : NULL;
    if (id && *id) return id;
    return n && n->name ? n->name : "?";
}

/* The root, html and body boxes, which paint the canvas and never clip. */
static gboolean
box_is_canvas_root(const ns_box *b)
{
    return !b->parent || ns_node_is_tag(b->dom, NS_TAG_HTML) ||
           ns_node_is_tag(b->dom, NS_TAG_BODY);
}

/* TRUE if the box is placed inside a line, as an inline-block or the
 * like, at an offset paint_inline applies. */
static gboolean
box_is_atomic(const ns_box *b)
{
    const ns_box *o = b->parent;
    for (guint i = 0; o && o->inline_atomics && i < o->inline_atomics->len; i++)
        if (g_array_index(o->inline_atomics, ns_inline_atomic, i).box == b)
            return TRUE;
    return FALSE;
}

static gboolean
box_clips_overflow(const ns_box *b)
{
    const ns_style *s = b->style;
    if (!s) return FALSE;
    return ns_overflow_keyword_clips(ns_style_keyword(s, NS_CSS_OVERFLOW_X)) ||
           ns_overflow_keyword_clips(ns_style_keyword(s, NS_CSS_OVERFLOW_Y)) ||
           ns_overflow_keyword_clips(ns_style_keyword(s, NS_CSS_OVERFLOW));
}

/* TRUE if the box's descendants are painted where it paints them and as
 * they are, so a layer below it composites straight onto the page: no
 * group, transform, filter, clip or scroller, no stacking context of its
 * own. A positioned box without a z-index or a float is fine, as long as
 * the layers below it are ordered with it (layer_order_cmp). An element
 * animating opacity or transform may become a group in any frame. */
static gboolean
box_passes_through(const ns_box *b, ns_anim *anim, GHashTable *animated)
{
    if (b->fragment_context || ns_paint_box_isolates(b, anim)) return FALSE;
    if (b->dom && g_hash_table_contains(animated, b->dom)) return FALSE;
    if (box_is_canvas_root(b)) return TRUE;
    int z;
    int k = ns_paint_box_stacking(b, &z);
    if (k == 3) return FALSE;
    if (k == 2) {
        const ns_css_value *zv = b->style ? b->style->values[NS_CSS_Z_INDEX] : NULL;
        if ((zv && zv->kind == NS_CSS_V_LENGTH) ||
            ns_paint_box_is_stacking_context(b))
            return FALSE;
    }
    return !ns_box_is_fixed(b) && !box_clips_overflow(b) && !box_is_atomic(b);
}

/* Scans a would-be layer's subtree: FALSE if a box in it paints before
 * the flow (negative z-index), or, unless the layer is a stacking context
 * however it is animated, if one is positioned and so painted from the
 * page's list of positioned boxes rather than with the layer. */
static gboolean
subtree_stays_inside(const ns_box *b, gboolean contains_positioned)
{
    for (const ns_box *c = b->first_child; c; c = c->next_sibling) {
        int z;
        int k = ns_paint_box_stacking(c, &z);
        if (k == 3 || (k == 2 && !contains_positioned)) return FALSE;
        if (ns_box_is_fixed(c)) return FALSE;
        if (!subtree_stays_inside(c, contains_positioned)) return FALSE;
    }
    for (guint i = 0; b->inline_atomics && i < b->inline_atomics->len; i++) {
        const ns_box *a = g_array_index(b->inline_atomics, ns_inline_atomic, i).box;
        if (!a) continue;
        int z;
        int k = ns_paint_box_stacking(a, &z);
        if (k == 3 || (k == 2 && !contains_positioned)) return FALSE;
        if (!subtree_stays_inside(a, contains_positioned)) return FALSE;
    }
    return TRUE;
}

/* Leaves a fixed or animated box in the page, saying why under NS_LAYERS=log. */
static gboolean
reject(const ns_box *b, const char *why)
{
    if (layers_mode() == 2)
        g_printerr("[layers] %s stays in the page: %s\n", node_label(b->dom),
                   why);
    return FALSE;
}

typedef struct plan_walk {
    ns_anim    *anim;
    GHashTable *composite;
    GArray     *layers;
    double      scale, max_w, max_h, max_area;
    guint       order;
} plan_walk;

/* Where the page paints a box relative to the others: in the flow (0), or
 * from its stacking context's list of positioned boxes (1) at a z-index
 * and in document order; a box in the flow of a positioned box paints
 * with it. */
typedef struct paint_slot {
    int   elevated;
    int   z;
    guint order;
} paint_slot;

/* Adds the box as a layer if it can be one. */
static gboolean
try_layer(plan_walk *w, const ns_box *b, const paint_slot *slot)
{
    if (!b->dom || b->dom->kind != NS_NODE_ELEMENT) return FALSE;
    if (b->kind == NS_BOX_INLINE || b->kind == NS_BOX_TEXT) return FALSE;
    gboolean fixed = ns_box_is_fixed(b);
    gboolean animated = g_hash_table_contains(w->composite, b->dom);
    if (!fixed && !animated) return FALSE;
    if (w->layers->len >= NS_LAYERS_MAX) return reject(b, "too many layers");
    if (b->fragment_context || box_is_atomic(b))
        return reject(b, "in a line or fragmented");
    int z;
    int k = ns_paint_box_stacking(b, &z);
    if (k == 1 || k == 3) return reject(b, "a float or below the flow");
    const ns_style *s = b->style;
    if (!s) return FALSE;
    /* What the compositor does not do: blend modes, filters, masks and
     * clips, 3D, sticky positioning. */
    if (ns_css_keyword_is(s->values[NS_CSS_POSITION], "sticky"))
        return reject(b, "sticky");
    const ns_css_value *v = s->values[NS_CSS_MIX_BLEND_MODE];
    if (v && !ns_css_keyword_is(v, "normal")) return reject(b, "blend mode");
    v = s->values[NS_CSS_FILTER];
    if (v && !ns_css_keyword_is(v, "none")) return reject(b, "filter");
    v = s->values[NS_CSS_MASK_IMAGE];
    if (v && !ns_css_keyword_is(v, "none")) return reject(b, "mask");
    v = s->values[NS_CSS_CLIP_PATH];
    if (v && !ns_css_keyword_is(v, "none")) return reject(b, "clip-path");
    if (s->values[NS_CSS_CLIP] && s->values[NS_CSS_CLIP]->kind == NS_CSS_V_RECT)
        return reject(b, "clip");
    gfx_matrix_t m;
    gboolean singular;
    if (!ns_paint_box_transform(b, w->anim, &m, &singular))
        return reject(b, "3D transform");
    /* A transform nothing animates stays painted in place: the GPU would
     * resample text that paint draws sharp. */
    if (!animated && ns_style_has_transform(s))
        return reject(b, "transform without animation");
    /* A fixed box, or a positioned one with a z-index, paints its
     * positioned descendants itself; any other box only while a transform
     * or an opacity below 1 makes it a stacking context. */
    const ns_css_value *zv = s->values[NS_CSS_Z_INDEX];
    gboolean own_scope = fixed ||
                         (k == 2 && zv && zv->kind == NS_CSS_V_LENGTH &&
                          ns_paint_box_is_stacking_context(b));
    if (!subtree_stays_inside(b, own_scope))
        return reject(b, "positioned or fixed boxes inside");
    if (!ns_paint_box_subtree_paints(b)) return reject(b, "paints nothing");
    ns_layer l;
    memset(&l, 0, sizeof l);
    if (!ns_damage_subtree_extent(b, w->anim, &l.x0, &l.y0, &l.x1, &l.y1))
        return reject(b, "no extent");
    l.x0 -= NS_LAYER_PAD;
    l.y0 -= NS_LAYER_PAD;
    l.x1 += NS_LAYER_PAD;
    l.y1 += NS_LAYER_PAD;
    double dw = (l.x1 - l.x0) * w->scale, dh = (l.y1 - l.y0) * w->scale;
    if (!(dw > 0 && dh > 0) || dw > w->max_w || dh > w->max_h ||
        dw * dh > w->max_area)
        return reject(b, "too large");
    l.box = b;
    l.node = b->dom;
    l.fixed = fixed;
    l.elevated = k == 2;
    l.z = k == 2 ? z : 0;
    l.order = w->order;
    if (k == 2) {
        l.slot_elevated = 1;
        l.slot_z = z;
        l.slot_order = w->order;
    } else {
        l.slot_elevated = slot->elevated;
        l.slot_z = slot->z;
        l.slot_order = slot->order;
    }
    l.above = g_array_new(FALSE, FALSE, sizeof(ns_damage_rect));
    l.ancestors = g_hash_table_new(g_direct_hash, g_direct_equal);
    for (const ns_box *p = b->parent; p; p = p->parent)
        g_hash_table_add(l.ancestors, (gpointer)p);
    g_array_append_val(w->layers, l);
    return TRUE;
}

/* Finds the layer boxes: below boxes whose descendants paint as they are. */
static void
find_layers(plan_walk *w, const ns_box *b, gboolean through, paint_slot slot)
{
    w->order++;
    if (through && !box_is_canvas_root(b) && try_layer(w, b, &slot)) return;
    gboolean child_through =
        through && box_passes_through(b, w->anim, w->composite);
    if (!child_through) return;
    int z;
    if (!box_is_canvas_root(b) && !slot.elevated &&
        ns_paint_box_stacking(b, &z) == 2) {
        slot.elevated = 1;
        slot.z = z;
        slot.order = w->order;
    }
    for (const ns_box *c = b->first_child; c; c = c->next_sibling)
        find_layers(w, c, child_through, slot);
}

typedef struct above_walk {
    ns_anim  *anim;
    GArray   *layers;
} above_walk;

/* Records that a subtree paints above a layer: its on-screen rectangle,
 * or if that cannot be had, its boxes' one by one. FALSE when neither can. */
static gboolean
add_above(above_walk *w, ns_layer *l, const ns_box *b)
{
    if (!ns_paint_box_subtree_paints(b)) return TRUE;
    ns_damage_rect r;
    if (ns_damage_box_visual_rect(b, w->anim, &r)) {
        if (r.w > 0 && r.h > 0) g_array_append_val(l->above, r);
        return TRUE;
    }
    if (ns_paint_box_paints_itself(b)) return FALSE;
    for (const ns_box *c = b->first_child; c; c = c->next_sibling)
        if (!add_above(w, l, c)) return FALSE;
    for (guint i = 0; b->inline_atomics && i < b->inline_atomics->len; i++) {
        const ns_box *a = g_array_index(b->inline_atomics, ns_inline_atomic, i).box;
        if (a && !add_above(w, l, a)) return FALSE;
    }
    return TRUE;
}

/* TRUE if box b, not inside layer l and not one of its ancestors, paints
 * after it. A layer in the flow is painted over by what follows it in the
 * document and by positioned boxes and floats; a positioned one by the
 * positioned boxes above it in z-index, or at its z-index and later in the
 * document, which are the ones visited once it was left. Counting a box
 * that paints before as after only keeps a layer in the page. */
static gboolean
paints_after(const ns_layer *l, const ns_box *b)
{
    int z;
    int k = ns_paint_box_stacking(b, &z);
    if (!l->elevated) return l->exited || k == 1 || k == 2;
    return k == 2 && (z > l->z || (z == l->z && l->exited));
}

/* Walks the page in the order find_layers did, collecting for each layer
 * the rectangles of what paints above it. pending has a bit for each
 * layer still looking inside this box. */
static void
walk_above(above_walk *w, const ns_box *b, guint32 pending, gboolean root)
{
    if (!pending) return;
    for (guint i = 0; i < w->layers->len; i++) {
        ns_layer *l = &g_array_index(w->layers, ns_layer, i);
        if (l->box != b) continue;
        /* A layer: composited in order with the others. */
        l->entered = l->exited = TRUE;
        return;
    }
    int z;
    if (!root && ns_paint_box_stacking(b, &z) == 3) return;  /* below the flow */
    guint32 below = 0;
    for (guint i = 0; i < w->layers->len; i++) {
        if (!(pending & (1u << i))) continue;
        ns_layer *l = &g_array_index(w->layers, ns_layer, i);
        if (l->rejected) continue;
        if (!root && !g_hash_table_contains(l->ancestors, b) &&
            paints_after(l, b)) {
            if (!add_above(w, l, b)) l->rejected = TRUE;
        } else {
            below |= 1u << i;
        }
    }
    if (!below) return;
    for (const ns_box *c = b->first_child; c; c = c->next_sibling)
        walk_above(w, c, below, FALSE);
    for (guint i = 0; b->inline_atomics && i < b->inline_atomics->len; i++) {
        const ns_box *a = g_array_index(b->inline_atomics, ns_inline_atomic, i).box;
        if (a) walk_above(w, a, below, FALSE);
    }
}

/* Paint order: the flow, then positioned boxes by z-index and document
 * order, each with the flow inside it. */
static gint
layer_order_cmp(gconstpointer pa, gconstpointer pb)
{
    const ns_layer *a = pa, *b = pb;
    if (a->slot_elevated != b->slot_elevated)
        return a->slot_elevated - b->slot_elevated;
    if (a->slot_z != b->slot_z) return a->slot_z < b->slot_z ? -1 : 1;
    if (a->slot_order != b->slot_order)
        return a->slot_order < b->slot_order ? -1 : 1;
    return a->order < b->order ? -1 : a->order > b->order;
}

void
ns_layers_plan_clear(ns_layer_plan *plan)
{
    if (!plan) return;
    if (plan->layers) {
        for (guint i = 0; i < plan->layers->len; i++) {
            ns_layer *l = &g_array_index(plan->layers, ns_layer, i);
            g_array_free(l->above, TRUE);
            g_hash_table_destroy(l->ancestors);
        }
        g_array_free(plan->layers, TRUE);
    }
    if (plan->composite) g_hash_table_destroy(plan->composite);
    memset(plan, 0, sizeof *plan);
}

void
ns_layers_plan_build(ns_layer_plan *plan, const ns_box *root, ns_anim *anim,
                     GPtrArray *composite_nodes, double scale, double max_w,
                     double max_h, double max_area)
{
    ns_layers_plan_clear(plan);
    plan->layers = g_array_new(FALSE, FALSE, sizeof(ns_layer));
    plan->composite = g_hash_table_new(g_direct_hash, g_direct_equal);
    for (guint i = 0; composite_nodes && i < composite_nodes->len; i++)
        g_hash_table_add(plan->composite, g_ptr_array_index(composite_nodes, i));
    if (!root || ns_paint_has_top_layer(root)) return;
    plan_walk pw = {
        .anim = anim, .composite = plan->composite, .layers = plan->layers,
        .scale = scale > 0 ? scale : 1.0, .max_w = max_w, .max_h = max_h,
        .max_area = max_area,
    };
    paint_slot slot = { 0 };
    find_layers(&pw, root, TRUE, slot);
    if (plan->layers->len == 0) return;
    /* The walk below visits boxes in the order find_layers did; entering a
     * layer box marks it, so what comes later is known to be later. */
    above_walk aw = { .anim = anim, .layers = plan->layers };
    guint32 all = plan->layers->len >= 32 ? 0xffffffffu
                                          : (1u << plan->layers->len) - 1;
    walk_above(&aw, root, all, TRUE);
    for (guint i = plan->layers->len; i > 0; i--) {
        ns_layer *l = &g_array_index(plan->layers, ns_layer, i - 1);
        if (layers_mode() == 2)
            g_printerr("[layers] plan %s%s: %s, %.0fx%.0f, %u above\n",
                       node_label(l->node), l->fixed ? " (fixed)" : "",
                       l->rejected ? "painted in the page" : "layer",
                       l->x1 - l->x0, l->y1 - l->y0, l->above->len);
        if (!l->rejected) continue;
        g_array_free(l->above, TRUE);
        g_hash_table_destroy(l->ancestors);
        g_array_remove_index(plan->layers, i - 1);
    }
    g_array_sort(plan->layers, layer_order_cmp);
}

gboolean
ns_layers_plan_same_nodes(const ns_layer_plan *plan, GPtrArray *nodes)
{
    if (!plan->composite) return FALSE;
    if (g_hash_table_size(plan->composite) != (nodes ? nodes->len : 0))
        return FALSE;
    for (guint i = 0; nodes && i < nodes->len; i++)
        if (!g_hash_table_contains(plan->composite,
                                   g_ptr_array_index(nodes, i)))
            return FALSE;
    return TRUE;
}

/* Bounds of a rectangle mapped through m. */
static void
matrix_bounds(const gfx_matrix_t *m, double x0, double y0, double x1,
              double y1, double *ox0, double *oy0, double *ox1, double *oy1)
{
    double xs[4] = { x0, x1, x0, x1 }, ys[4] = { y0, y0, y1, y1 };
    *ox0 = *oy0 = G_MAXDOUBLE;
    *ox1 = *oy1 = -G_MAXDOUBLE;
    for (int i = 0; i < 4; i++) {
        double x = xs[i], y = ys[i];
        gfx_matrix_transform_point(m, &x, &y);
        *ox0 = MIN(*ox0, x);
        *oy0 = MIN(*oy0, y);
        *ox1 = MAX(*ox1, x);
        *oy1 = MAX(*oy1, y);
    }
}

/* TRUE if a rectangle, CSS px, overlaps the layer's bounds this frame;
 * fixed says whose coordinates it is in, the viewport's or the page's. */
static gboolean
overlaps(const ns_layer *l, double x, double y, double w, double h,
         gboolean fixed, double sx, double sy)
{
    if (fixed != l->fixed) {
        double d = l->fixed ? -1 : 1;
        x += d * sx;
        y += d * sy;
    }
    return x < l->bx1 && x + w > l->bx0 && y < l->by1 && y + h > l->by0;
}

int
ns_layers_plan_resolve(ns_layer_plan *plan, ns_anim *anim, double sx,
                       double sy, gboolean axis_only)
{
    if (!plan->layers) return 0;
    for (guint i = 0; i < plan->layers->len; i++) {
        ns_layer *l = &g_array_index(plan->layers, ns_layer, i);
        gboolean singular = FALSE;
        l->active = ns_paint_box_transform(l->box, anim, &l->m, &singular);
        l->opacity = ns_paint_box_opacity(l->box, anim);
        l->visible = !singular && l->opacity > 0;
        matrix_bounds(&l->m, l->x0, l->y0, l->x1, l->y1, &l->bx0, &l->by0,
                      &l->bx1, &l->by1);
        if (axis_only && (l->m.xy != 0 || l->m.yx != 0 || l->m.xx <= 0 ||
                          l->m.yy <= 0))
            l->active = FALSE;
        for (guint j = 0; l->active && j < l->above->len; j++) {
            const ns_damage_rect *r = &g_array_index(l->above, ns_damage_rect, j);
            if (overlaps(l, r->x, r->y, r->w, r->h, r->fixed, sx, sy))
                l->active = FALSE;
        }
    }
    /* A layer the page paints is under every layer before it: one of
     * those it overlaps must be painted into the page as well. */
    int n = 0;
    for (guint i = plan->layers->len; i > 0; i--) {
        ns_layer *l = &g_array_index(plan->layers, ns_layer, i - 1);
        for (guint j = i; l->active && j < plan->layers->len; j++) {
            const ns_layer *o = &g_array_index(plan->layers, ns_layer, j);
            if (!o->active &&
                overlaps(l, o->bx0, o->by0, o->bx1 - o->bx0, o->by1 - o->by0,
                         o->fixed, sx, sy))
                l->active = FALSE;
        }
        if (l->active) n++;
    }
    if (layers_mode() == 2) {
        GString *line = g_string_new("[layers] frame:");
        for (guint i = 0; i < plan->layers->len; i++) {
            const ns_layer *l = &g_array_index(plan->layers, ns_layer, i);
            g_string_append_printf(line, " %s%s", node_label(l->node),
                                   l->active ? "" : "(in page)");
        }
        g_printerr("%s\n", line->str);
        g_string_free(line, TRUE);
    }
    return n;
}

void
ns_layer_device_box(const ns_layer *l, double scale, double sx, double sy,
                    int *x, int *y, int *w, int *h)
{
    double ox = l->fixed ? 0 : sx, oy = l->fixed ? 0 : sy;
    double x0 = floor((l->x0 - ox) * scale), y0 = floor((l->y0 - oy) * scale);
    double x1 = ceil((l->x1 - ox) * scale), y1 = ceil((l->y1 - oy) * scale);
    *x = (int)x0;
    *y = (int)y0;
    *w = (int)(x1 - x0);
    *h = (int)(y1 - y0);
}

void
ns_layer_device_matrix(const ns_layer *l, double scale, double sx, double sy,
                       gfx_matrix_t *out)
{
    /* Device = scale * (user - scroll), the scroll left out for a fixed
     * box, whose user space is the viewport's: conjugate the box's user
     * space transform by that. */
    double ox = l->fixed ? 0 : sx, oy = l->fixed ? 0 : sy;
    gfx_matrix_t to_user, to_device;
    gfx_matrix_init(&to_user, 1.0 / scale, 0, 0, 1.0 / scale, ox, oy);
    gfx_matrix_init(&to_device, scale, 0, 0, scale, -ox * scale, -oy * scale);
    gfx_matrix_multiply(out, &to_user, &l->m);
    gfx_matrix_multiply(out, out, &to_device);
}

void
ns_layers_composite(gfx_t *cr, unsigned char *pixels, int w, int h,
                    int stride, int x, int y, const gfx_matrix_t *m,
                    double opacity)
{
    gfx_surface_t *src = gfx_image_surface_create_for_data(
        pixels, GFX_FORMAT_ARGB32, w, h, stride);
    if (gfx_surface_status(src) != GFX_STATUS_SUCCESS) {
        gfx_surface_destroy(src);
        return;
    }
    gfx_save(cr);
    gfx_transform(cr, m);
    gfx_set_source_surface(cr, src, x, y);
    /* Moved by whole pixels the buffer is copied as it is; otherwise it
     * is resampled, as the GPU does. */
    gboolean whole = m->xx == 1 && m->yy == 1 && m->xy == 0 && m->yx == 0 &&
                     m->x0 == floor(m->x0) && m->y0 == floor(m->y0);
    gfx_pattern_set_filter(gfx_get_source(cr),
                           whole ? GFX_FILTER_NEAREST : GFX_FILTER_BILINEAR);
    gfx_rectangle(cr, x, y, w, h);
    gfx_clip(cr);
    if (opacity >= 1.0) gfx_paint(cr);
    else                gfx_paint_with_alpha(cr, opacity);
    gfx_restore(cr);
    gfx_surface_destroy(src);
}

gboolean
ns_layers_paint_page(gfx_t *cr, const ns_box *root, ns_anim *anim, int width,
                     int height)
{
    GPtrArray *nodes = g_ptr_array_new();
    ns_anim_composite_nodes(anim, nodes);
    ns_layer_plan plan = { 0 };
    ns_layers_plan_build(&plan, root, anim, nodes, 1.0, 4096, 4096,
                         2.0 * width * height);
    g_ptr_array_free(nodes, TRUE);
    int n = ns_layers_plan_resolve(&plan, anim, 0, 0, FALSE);
    if (n == 0) {
        ns_layers_plan_clear(&plan);
        return FALSE;
    }
    GHashTable *skip = g_hash_table_new(g_direct_hash, g_direct_equal);
    for (guint i = 0; i < plan.layers->len; i++) {
        const ns_layer *l = &g_array_index(plan.layers, ns_layer, i);
        if (l->active) g_hash_table_add(skip, (gpointer)l->box);
    }
    ns_paint_set_device_viewport(0, 0, width, height);
    ns_paint_set_layer_skip(skip);
    ns_paint(cr, root, NULL);
    ns_paint_set_layer_skip(NULL);
    for (guint i = 0; i < plan.layers->len; i++) {
        const ns_layer *l = &g_array_index(plan.layers, ns_layer, i);
        if (!l->active || !l->visible) continue;
        int x, y, w, h;
        ns_layer_device_box(l, 1.0, 0, 0, &x, &y, &w, &h);
        gfx_surface_t *surf = gfx_image_surface_create(GFX_FORMAT_ARGB32, w, h);
        if (gfx_surface_status(surf) != GFX_STATUS_SUCCESS) {
            gfx_surface_destroy(surf);
            continue;
        }
        gfx_surface_set_device_offset(surf, -x, -y);
        gfx_t *lc = gfx_create(surf);
        gfx_rectangle(lc, x, y, w, h);
        gfx_clip(lc);
        ns_paint_layer(lc, root, l->box, NULL, NULL);
        gfx_destroy(lc);
        gfx_surface_flush(surf);
        gfx_surface_set_device_offset(surf, 0, 0);
        gfx_matrix_t m;
        ns_layer_device_matrix(l, 1.0, 0, 0, &m);
        ns_layers_composite(cr, gfx_image_surface_get_data(surf), w, h,
                            gfx_image_surface_get_stride(surf), x, y, &m,
                            l->opacity);
        gfx_surface_destroy(surf);
    }
    ns_paint_clear_device_viewport();
    g_hash_table_destroy(skip);
    ns_layers_plan_clear(&plan);
    return TRUE;
}
