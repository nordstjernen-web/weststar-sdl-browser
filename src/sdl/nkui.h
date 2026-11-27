/* Weststar SDL Browser — the Nuklear backend: SDL3 input in, chrome pixels out.
 * Text is measured and drawn with the engine's text stack; Nuklear's command list is
 * rendered into a window-sized layer, and only the part that changed is uploaded.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_SDL_NKUI_H
#define NS_SDL_NKUI_H

#include "plat/ns_sdl.h"
#include <glib.h>

#include "nk.h"
#include "ui.h"

G_BEGIN_DECLS

typedef struct NsNk NsNk;

typedef enum {
    NS_NK_FONT_NORMAL,
    NS_NK_FONT_SMALL,
    NS_NK_FONT_BOLD,
    NS_NK_FONT_COUNT
} NsNkFontRole;

NsNk                      *ns_nk_new(void);
void                       ns_nk_free(NsNk *nk);
struct nk_context         *ns_nk_context(NsNk *nk);
const struct nk_user_font *ns_nk_font(NsNk *nk, NsNkFontRole role);

/* Restyles every widget from the theme. */
void ns_nk_apply_theme(NsNk *nk, const NsUiTheme *theme);

/* An image handle for a bundled icon ("weststar-back"). */
struct nk_image ns_nk_icon(const char *name);

/* Input for one frame. to_ui turns window coordinates into UI units. */
void ns_nk_input_begin(NsNk *nk);
void ns_nk_input_event(NsNk *nk, const SDL_Event *ev, float to_ui);
void ns_nk_input_end(NsNk *nk);

/* Draws the frame just built at px_w x px_h pixels and ui_scale pixels per
 * UI unit; returns TRUE when the chrome's pixels changed. Clears the
 * context for the next frame. */
gboolean ns_nk_render(NsNk *nk, SDL_Renderer *renderer, int px_w, int px_h,
                      double ui_scale);
void     ns_nk_present(NsNk *nk, SDL_Renderer *renderer);
/* The renderer lost its textures. */
void     ns_nk_device_reset(NsNk *nk);

G_END_DECLS

#endif
