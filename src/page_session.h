/* Weststar SDL Browser — one page host session: open page, back/forward cache, last frame.
 * API used by the shell's engine-thread view and headless driver for rendering, input and navigation.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_PAGE_SESSION_H
#define NS_PAGE_SESSION_H

#include <glib.h>


typedef struct ns_page_session ns_page_session;

typedef void (*ns_page_session_wake_cb)(gpointer user_data);

typedef struct {
    int   ok;
    int   page_width;
    int   page_height;
    char *title;
    char *url;
    char *nav;
    int   security;
    char *remote_ip;
} ns_page_info;

/* A compositing layer: a premultiplied ARGB32 buffer the shell draws over
 * the page, w by h pixels whose top left pixel is at (x, y) in the frame
 * before the transform m (xx, yx, xy, yy, x0, y0, device pixels), at an
 * opacity. Its pixels are the session's until the next render. */
typedef struct {
    int                  id;
    int                  changed;    /* new pixels since the last frame */
    const unsigned char *pixels;
    int                  w, h, stride;
    int                  x, y;
    double               m[6];
    double               opacity;
    int                  visible;
} ns_page_layer;

typedef struct {
    int                  ok;
    int                  width;
    int                  height;
    int                  stride;
    int                  animating;
    int                  caret_blinking;
    int                  page_w;
    int                  page_h;
    int                  scroll_y;
    int                  scroll_x;
    int                  unchanged;
    int                  n_damage;
    int                 *damage;
    /* A scrolled frame painted from the one before it: that frame's pixels
     * moved up by blit_dy and left by blit_dx device pixels, and then the
     * damage rectangles (the exposed strips among them) were painted. */
    int                  blit_dx;
    int                  blit_dy;
    /* With layers on, the layers over the page in the order to draw them,
     * and whether any came, went, moved or changed since the last frame:
     * then the frame is new even if the page is unchanged. */
    int                  n_layers;
    ns_page_layer       *layers;
    int                  layers_changed;
    /* What the frame cost to paint, page and layers, in microseconds. */
    gint64               paint_us;
    gint64               layers_paint_us;
    int                  layers_painted;
    guint64              gen;
    guint64              base_gen;
    const unsigned char *pixels;
    char                *nav;
    char                *url;
    int                  url_pushed;
    char                *title;
    char                *camera;
    char                *download;
    int                  clipboard;
} ns_page_frame;

ns_page_session *ns_page_session_new(int max_width, int max_height);
void ns_page_session_free(ns_page_session *s);
void ns_page_session_set_wake(ns_page_session *s, ns_page_session_wake_cb cb,
                              gpointer user_data);
void ns_page_session_set_frame_time(ns_page_session *s, gint64 frame_time_us);
/* Lets the session hand fixed boxes and elements animating only transform
 * and opacity over as layers, for a shell that composites them; off by
 * default, so a frame's pixels are the whole page. NS_LAYERS=0 keeps it off.
 * With NS_PAGE_LAYERS_AXIS_ONLY the shell can only move and scale a layer:
 * one turned, skewed or mirrored is painted into the page. */
#define NS_PAGE_LAYERS           1
#define NS_PAGE_LAYERS_AXIS_ONLY 2
void ns_page_session_set_layers(ns_page_session *s, int flags);

int  ns_page_session_open(ns_page_session *s, const char *url, int width,
                          int height, int settle_ms, int history,
                          int user_activated, ns_page_info *out);
int  ns_page_session_set_viewport(ns_page_session *s, int width, int height,
                                  ns_page_info *out);
void ns_page_info_clear(ns_page_info *info);

int  ns_page_session_render(ns_page_session *s, int width, int height,
                            int scroll_x, int scroll_y, double scale,
                            int caret_active, ns_page_frame *out);
void ns_page_frame_clear(ns_page_frame *frame);
void ns_page_session_invalidate_frame(ns_page_session *s);

char *ns_page_session_link_at(ns_page_session *s, int x, int y,
                              char **out_cursor);
int   ns_page_session_hover(ns_page_session *s, int x, int y,
                            char **out_href, char **out_cursor);
char *ns_page_session_click(ns_page_session *s, int x, int y, int mods);
char *ns_page_session_release(ns_page_session *s, int *out_changed);
char *ns_page_session_release_at(ns_page_session *s, int x, int y,
                                 int *out_changed);
char *ns_page_session_select(ns_page_session *s, int kind, int x, int y);
char *ns_page_session_key(ns_page_session *s, int kind, const char *key,
                          const char *code, int keycode, int mods,
                          int *out_prevented);
int   ns_page_session_contextmenu(ns_page_session *s, int x, int y);
int   ns_page_session_scroll(ns_page_session *s, int x, int y, int dx,
                             int dy);
int   ns_page_session_scrollbar(ns_page_session *s, int kind, int x, int y);
int   ns_page_session_drop_files(ns_page_session *s, int x, int y,
                                 const char *const *paths, int n_paths);
int   ns_page_session_find(ns_page_session *s, const char *query,
                           int case_sensitive, int direction, int from_y,
                           int *out_total, int *out_current,
                           int *out_scroll_y);
char *ns_page_session_clipboard(ns_page_session *s);
char *ns_page_session_media_at(ns_page_session *s, int x, int y,
                               int *out_is_video, int *out_stream);
void  ns_page_session_resolve_camera(ns_page_session *s, const char *origin,
                                     int allow);
char *ns_page_session_eval(ns_page_session *s, const char *src);
char *ns_page_session_dump(ns_page_session *s, const char *kind);
char *ns_page_session_console(ns_page_session *s);
int   ns_page_session_export(ns_page_session *s, const char *path);

#endif
