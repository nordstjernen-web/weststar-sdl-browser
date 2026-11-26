/* Weststar SDL Browser — chrome drawing helpers: the theme, chrome text, and layers.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ui.h"

#include "ns_pango.h"

#include <string.h>

#define NS_UI_FONT_FAMILY   "Sans"
#define NS_UI_FONT_FEATURES "kern 0, liga 0, clig 0"

static const NsUiTheme k_light = {
    .dark         = FALSE,
    .window       = { 1.00, 1.00, 1.00, 1 },
    .toolbar      = { 0.95, 0.95, 0.96, 1 },
    .border       = { 0.80, 0.81, 0.83, 1 },
    .text         = { 0.10, 0.11, 0.13, 1 },
    .text_dim     = { 0.42, 0.44, 0.48, 1 },
    .field        = { 1.00, 1.00, 1.00, 1 },
    .field_border = { 0.76, 0.77, 0.80, 1 },
    .focus        = { 0.21, 0.47, 0.86, 1 },
    .hover        = { 0.88, 0.89, 0.91, 1 },
    .pressed      = { 0.80, 0.81, 0.84, 1 },
    .selection    = { 0.70, 0.80, 0.96, 1 },
    .popup        = { 1.00, 1.00, 1.00, 1 },
    .secure       = { 0.16, 0.55, 0.27, 1 },
    .insecure     = { 0.78, 0.22, 0.18, 1 },
};

static const NsUiTheme k_dark = {
    .dark         = TRUE,
    .window       = { 0.12, 0.12, 0.13, 1 },
    .toolbar      = { 0.17, 0.17, 0.19, 1 },
    .border       = { 0.30, 0.30, 0.33, 1 },
    .text         = { 0.92, 0.92, 0.94, 1 },
    .text_dim     = { 0.62, 0.63, 0.67, 1 },
    .field        = { 0.11, 0.11, 0.12, 1 },
    .field_border = { 0.34, 0.34, 0.38, 1 },
    .focus        = { 0.38, 0.60, 0.96, 1 },
    .hover        = { 0.24, 0.24, 0.27, 1 },
    .pressed      = { 0.30, 0.30, 0.34, 1 },
    .selection    = { 0.25, 0.38, 0.62, 1 },
    .popup        = { 0.20, 0.20, 0.22, 1 },
    .secure       = { 0.42, 0.78, 0.48, 1 },
    .insecure     = { 0.95, 0.45, 0.40, 1 },
};

static const NsUiTheme *g_theme = &k_light;

const NsUiTheme *
ns_ui_theme(void)
{
    return g_theme;
}

void
ns_ui_set_dark(gboolean dark)
{
    g_theme = dark ? &k_dark : &k_light;
}

void
ns_ui_set_source(cairo_t *cr, NsColor c)
{
    cairo_set_source_rgba(cr, c.r, c.g, c.b, c.a);
}

/* A context for measuring text outside of drawing. */
static cairo_t *
measure_cr(void)
{
    static cairo_t *cr;
    if (!cr) {
        cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                                        1, 1);
        cr = cairo_create(s);
        cairo_surface_destroy(s);
    }
    return cr;
}

static NsPangoLayout *
ui_layout(cairo_t *cr, const char *text, int len, double size_px,
          gboolean bold)
{
    NsPangoLayout *layout = ns_pango_cairo_create_layout(cr);
    NsPangoFontDescription *fd = ns_pango_font_description_new();
    ns_pango_font_description_set_family(fd, NS_UI_FONT_FAMILY);
    ns_pango_font_description_set_absolute_size(fd, size_px * NS_PANGO_SCALE);
    if (bold)
        ns_pango_font_description_set_weight(fd, NS_PANGO_WEIGHT_BOLD);
    ns_pango_layout_set_font_description(layout, fd);
    ns_pango_font_description_free(fd);
    NsPangoAttrList *attrs = ns_pango_attr_list_new();
    ns_pango_attr_list_insert(attrs,
                              ns_pango_attr_font_features_new(NS_UI_FONT_FEATURES));
    ns_pango_layout_set_attributes(layout, attrs);
    ns_pango_attr_list_unref(attrs);
    ns_pango_layout_set_text(layout, text ? text : "", text ? len : 0);
    return layout;
}

double
ns_ui_text_width(const char *text, int len, double size_px, gboolean bold)
{
    if (!text || len <= 0)
        return 0;
    NsPangoLayout *layout = ui_layout(measure_cr(), text, len, size_px, bold);
    int w = 0, h = 0;
    ns_pango_layout_get_size(layout, &w, &h);
    g_object_unref(layout);
    return (double)w / NS_PANGO_SCALE;
}

void
ns_ui_text_draw(cairo_t *cr, double x, double y, double w, double h,
                const char *text, int len, double size_px, gboolean bold,
                double r, double g, double b, double a)
{
    if (!text || len <= 0)
        return;
    NsPangoLayout *layout = ui_layout(cr, text, len, size_px, bold);
    if (w > 0) {
        ns_pango_layout_set_width(layout, (int)((w + 1) * NS_PANGO_SCALE));
        ns_pango_layout_set_ellipsize(layout, NS_PANGO_ELLIPSIZE_END);
    }
    int lw = 0, lh = 0;
    ns_pango_layout_get_size(layout, &lw, &lh);
    double th = (double)lh / NS_PANGO_SCALE;
    cairo_set_source_rgba(cr, r, g, b, a);
    cairo_move_to(cr, x, y + (h - th) / 2);
    ns_pango_cairo_show_layout(cr, layout);
    g_object_unref(layout);
}

/* ---- layers ------------------------------------------------------------ */

gboolean
ns_ui_layer_size(NsUiLayer *layer, int px_w, int px_h)
{
    if (px_w < 1) px_w = 1;
    if (px_h < 1) px_h = 1;
    if (layer->surface && layer->px_w == px_w && layer->px_h == px_h)
        return FALSE;
    if (layer->surface)
        cairo_surface_destroy(layer->surface);
    layer->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, px_w, px_h);
    if (layer->texture) {
        SDL_DestroyTexture(layer->texture);
        layer->texture = NULL;
    }
    layer->px_w = px_w;
    layer->px_h = px_h;
    return TRUE;
}

void
ns_ui_layer_upload(NsUiLayer *layer, SDL_Renderer *renderer,
                   const SDL_Rect *rect)
{
    if (!layer->surface)
        return;
    cairo_surface_flush(layer->surface);
    gboolean fresh = FALSE;
    if (!layer->texture) {
        layer->texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                           SDL_TEXTUREACCESS_STREAMING,
                                           layer->px_w, layer->px_h);
        if (!layer->texture)
            return;
        /* Cairo's pixels are premultiplied. */
        SDL_SetTextureBlendMode(layer->texture,
                                SDL_BLENDMODE_BLEND_PREMULTIPLIED);
        SDL_SetTextureScaleMode(layer->texture, SDL_SCALEMODE_NEAREST);
        fresh = TRUE;
    }
    const guint8 *data = cairo_image_surface_get_data(layer->surface);
    int stride = cairo_image_surface_get_stride(layer->surface);
    if (!rect || fresh) {
        SDL_UpdateTexture(layer->texture, NULL, data, stride);
        return;
    }
    SDL_Rect r = *rect;
    SDL_Rect all = { 0, 0, layer->px_w, layer->px_h };
    if (!SDL_GetRectIntersection(&r, &all, &r))
        return;
    SDL_UpdateTexture(layer->texture, &r,
                      data + (gsize)r.y * stride + (gsize)r.x * 4u, stride);
}

void
ns_ui_layer_present(NsUiLayer *layer, SDL_Renderer *renderer, float x, float y)
{
    if (!layer->texture)
        return;
    SDL_FRect dst = { x, y, (float)layer->px_w, (float)layer->px_h };
    SDL_RenderTexture(renderer, layer->texture, NULL, &dst);
}

void
ns_ui_layer_lose_texture(NsUiLayer *layer)
{
    if (layer->texture)
        SDL_DestroyTexture(layer->texture);
    layer->texture = NULL;
}

void
ns_ui_layer_clear(NsUiLayer *layer)
{
    if (layer->texture)
        SDL_DestroyTexture(layer->texture);
    if (layer->surface)
        cairo_surface_destroy(layer->surface);
    memset(layer, 0, sizeof *layer);
}
