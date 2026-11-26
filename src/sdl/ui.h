/* Weststar SDL Browser — chrome drawing helpers: the theme, chrome text, and layers.
 * Chrome is drawn in UI units, scaled to pixels, and uploaded to SDL textures.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_SDL_UI_H
#define NS_SDL_UI_H

#include <SDL3/SDL.h>
#include <cairo.h>
#include <glib.h>

G_BEGIN_DECLS

typedef struct {
    double r, g, b, a;
} NsColor;

typedef struct {
    gboolean dark;
    NsColor  window;       /* behind the page, before its first frame */
    NsColor  toolbar;
    NsColor  border;
    NsColor  text;
    NsColor  text_dim;
    NsColor  field;
    NsColor  field_border;
    NsColor  focus;
    NsColor  hover;
    NsColor  pressed;
    NsColor  selection;
    NsColor  popup;
    NsColor  secure;
    NsColor  insecure;
} NsUiTheme;

const NsUiTheme *ns_ui_theme(void);
void             ns_ui_set_dark(gboolean dark);

void ns_ui_set_source(cairo_t *cr, NsColor c);

/* Chrome text, in the chrome font at size_px UI units. Kerning and
 * ligatures are off, so a string is as wide as its characters together:
 * Nuklear places the caret by adding up character widths. */
double ns_ui_text_width(const char *text, int len, double size_px,
                        gboolean bold);
/* Draws text vertically centred in the box; clipped to w with an ellipsis. */
void   ns_ui_text_draw(cairo_t *cr, double x, double y, double w, double h,
                       const char *text, int len, double size_px,
                       gboolean bold, double r, double g, double b, double a);

/* A cairo surface uploaded to an SDL texture. */
typedef struct {
    cairo_surface_t *surface;
    SDL_Texture     *texture;
    int              px_w, px_h;
} NsUiLayer;

/* Makes the layer px_w x px_h pixels, keeping its pixels when the size is
 * unchanged; returns TRUE when it was (re)created. */
gboolean ns_ui_layer_size(NsUiLayer *layer, int px_w, int px_h);
/* Uploads a rectangle of the layer's pixels (all of it when rect is NULL). */
void     ns_ui_layer_upload(NsUiLayer *layer, SDL_Renderer *renderer,
                            const SDL_Rect *rect);
/* Draws the layer with its top-left corner at (x, y) in pixels. */
void     ns_ui_layer_present(NsUiLayer *layer, SDL_Renderer *renderer,
                             float x, float y);
/* Drops the texture only, after the renderer lost its textures. */
void     ns_ui_layer_lose_texture(NsUiLayer *layer);
void     ns_ui_layer_clear(NsUiLayer *layer);

G_END_DECLS

#endif
