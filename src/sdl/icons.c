/* Weststar SDL Browser — the bundled SVG icons, drawn by the engine's own SVG renderer.
 * The icons (data/icons/hicolor/scalable/apps/) are compiled into the binary
 * as C arrays, found by name in ns_icons.h (see ns_icon_names in src/meson.build).
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "icons.h"

#include "dom.h"
#include "html.h"
#include "svg.h"

#include "ns_icons.h"

#include <glib.h>
#include <string.h>

typedef struct {
    ns_node       *doc;
    const ns_node *root;
} Icon;

static GHashTable *g_icons;

static void
icon_free(gpointer data)
{
    Icon *icon = data;
    if (icon->doc)
        ns_node_free(icon->doc);
    g_free(icon);
}

static const Icon *
icon_lookup(const char *name)
{
    if (!name || !*name)
        return NULL;
    if (!g_icons)
        g_icons = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                        icon_free);
    Icon *icon = g_hash_table_lookup(g_icons, name);
    if (icon)
        return icon->root ? icon : NULL;
    icon = g_new0(Icon, 1);
    g_hash_table_insert(g_icons, g_strdup(name), icon);
    const ns_icon_file *file = NULL;
    for (gsize i = 0; i < G_N_ELEMENTS(ns_icon_files); i++)
        if (!strcmp(ns_icon_files[i].name, name))
            file = &ns_icon_files[i];
    if (!file)
        return NULL;
    icon->doc = ns_html_parse(file->svg, (gssize)file->len);
    icon->root = icon->doc ? ns_svg_document_root(icon->doc) : NULL;
    return icon->root ? icon : NULL;
}

gboolean
ns_sdl_icon_draw(gfx_t *g, const char *name, double x, double y,
                 double size)
{
    const Icon *icon = icon_lookup(name);
    if (!icon)
        return FALSE;
    gfx_save(g);
    gfx_translate(g, x, y);
    ns_svg_render_node(g, icon->root, size, size, NULL, NULL);
    gfx_restore(g);
    return TRUE;
}

SDL_Surface *
ns_sdl_icon_surface(const char *name, int px)
{
    gfx_surface_t *s = gfx_image_surface_create(GFX_FORMAT_ARGB32, px, px);
    gfx_t *ctx = gfx_create(s);
    gboolean ok = ns_sdl_icon_draw(ctx, name, 0, 0, px);
    gfx_destroy(ctx);
    if (!ok) {
        gfx_surface_destroy(s);
        return NULL;
    }
    SDL_Surface *out = SDL_CreateSurface(px, px, SDL_PIXELFORMAT_ARGB8888);
    if (out) {
        /* gfx stores premultiplied alpha, SDL surfaces straight alpha. */
        const guint8 *src = gfx_image_surface_get_data(s);
        int sstride = gfx_image_surface_get_stride(s);
        for (int y = 0; y < px; y++) {
            const guint32 *in = (const guint32 *)(const void *)(src + (gsize)y * sstride);
            guint32 *row = (guint32 *)(void *)((guint8 *)out->pixels +
                                               (gsize)y * out->pitch);
            for (int x = 0; x < px; x++) {
                guint32 p = in[x];
                guint32 a = p >> 24;
                if (a == 0) {
                    row[x] = 0;
                    continue;
                }
                guint32 r = ((p >> 16) & 0xff) * 255 / a;
                guint32 g = ((p >> 8) & 0xff) * 255 / a;
                guint32 b = (p & 0xff) * 255 / a;
                row[x] = (a << 24) | (MIN(r, 255u) << 16) |
                         (MIN(g, 255u) << 8) | MIN(b, 255u);
            }
        }
    }
    gfx_surface_destroy(s);
    return out;
}
