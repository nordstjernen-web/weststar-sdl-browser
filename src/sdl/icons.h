/* Weststar SDL Browser — the bundled SVG icons, drawn by the engine's own SVG renderer.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_SDL_ICONS_H
#define NS_SDL_ICONS_H

#include <SDL3/SDL.h>
#include <cairo.h>
#include <glib.h>

G_BEGIN_DECLS

/* Draws icon "name" (e.g. "weststar-back") into the size x size box at (x, y);
 * FALSE when there is no such icon. */
gboolean ns_sdl_icon_draw(cairo_t *cr, const char *name, double x, double y,
                          double size);

/* The window icon: icon "name" at px x px, as a new SDL surface. */
SDL_Surface *ns_sdl_icon_surface(const char *name, int px);

G_END_DECLS

#endif
