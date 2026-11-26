/* Weststar SDL Browser — NsPageView: one page shown in an SDL window, driven on the engine thread.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_SDL_PAGEVIEW_H
#define NS_SDL_PAGEVIEW_H

#include <SDL3/SDL.h>
#include <glib.h>

G_BEGIN_DECLS

typedef struct NsPageView NsPageView;

typedef enum {
    NS_VIEW_EVT_TITLE,
    NS_VIEW_EVT_URL,
    NS_VIEW_EVT_STATUS,
    NS_VIEW_EVT_HISTORY,
    NS_VIEW_EVT_LOADING,      /* text "1" or "0" */
    NS_VIEW_EVT_DOWNLOAD,     /* "url\tsuggested-name" */
    NS_VIEW_EVT_ZOOM,         /* percent */
    NS_VIEW_EVT_CURSOR,       /* CSS cursor name, NULL for the default */
    NS_VIEW_EVT_CONTEXT_MENU, /* link under the pointer, or NULL */
    NS_VIEW_EVT_FIND,         /* "current/total", "" or "none" */
    NS_VIEW_EVT_REDRAW,       /* the page area has new pixels */
} NsViewEvent;

typedef void (*NsViewNotify)(NsPageView *view, NsViewEvent evt,
                             const char *text, gpointer user_data);

NsPageView *ns_page_view_new(SDL_Renderer *renderer);
/* Detaches the view from its window and frees the page on the engine thread. */
void        ns_page_view_close(NsPageView *view);
void        ns_page_view_set_notify(NsPageView *view, NsViewNotify cb,
                                    gpointer user_data);
void        ns_page_view_set_private(NsPageView *view, gboolean private_mode);
gboolean    ns_page_view_is_private(NsPageView *view);

/* The page area in window pixels, and the pixels per CSS pixel at 100% zoom. */
void ns_page_view_set_area(NsPageView *view, int x, int y, int w, int h,
                           double ui_scale);
/* Draws the latest frame and the viewport scrollbars into the area. */
void ns_page_view_present(NsPageView *view);
/* The renderer lost its textures: the next frame is fetched whole. */
void ns_page_view_device_reset(NsPageView *view);

/* Input, in window pixels. */
void     ns_page_view_mouse_down(NsPageView *view, float x, float y,
                                 int button, int clicks, SDL_Keymod mod);
void     ns_page_view_mouse_up(NsPageView *view, float x, float y,
                               int button);
void     ns_page_view_mouse_move(NsPageView *view, float x, float y,
                                 gboolean left_held);
void     ns_page_view_mouse_leave(NsPageView *view);
void     ns_page_view_wheel(NsPageView *view, float dx, float dy,
                            SDL_Keymod mod);
/* TRUE when the page took the key. */
gboolean ns_page_view_key_down(NsPageView *view, const SDL_KeyboardEvent *key);
void     ns_page_view_key_up(NsPageView *view, const SDL_KeyboardEvent *key);
void     ns_page_view_text_input(NsPageView *view, const char *text);
void     ns_page_view_set_focus(NsPageView *view, gboolean focused);
gboolean ns_page_view_has_focus(NsPageView *view);
/* Paths separated by newlines; ownership of paths is taken. */
void     ns_page_view_drop_files(NsPageView *view, float x, float y,
                                 char *paths);

/* Navigation. */
void        ns_page_view_load(NsPageView *view, const char *url);
void        ns_page_view_back(NsPageView *view);
void        ns_page_view_forward(NsPageView *view);
void        ns_page_view_reload(NsPageView *view);
void        ns_page_view_stop(NsPageView *view);
gboolean    ns_page_view_can_back(NsPageView *view);
gboolean    ns_page_view_can_forward(NsPageView *view);
const char *ns_page_view_url(NsPageView *view);
const char *ns_page_view_title(NsPageView *view);
gboolean    ns_page_view_is_loading(NsPageView *view);
int         ns_page_view_security(NsPageView *view);

void ns_page_view_zoom_in(NsPageView *view);
void ns_page_view_zoom_out(NsPageView *view);
void ns_page_view_zoom_reset(NsPageView *view);
int  ns_page_view_zoom_percent(NsPageView *view);

/* Find in page: direction 0 refreshes, 1 next, 2 previous; "" clears. */
void ns_page_view_find(NsPageView *view, const char *query, int direction,
                       gboolean case_sensitive);

void        ns_page_view_select_all(NsPageView *view);
void        ns_page_view_copy_selection(NsPageView *view);
gboolean    ns_page_view_has_selection(NsPageView *view);
/* Exports the page as a PNG to dest. */
void        ns_page_view_save_image(NsPageView *view, const char *dest);
/* Fetches the document again and writes its bytes to dest. */
void        ns_page_view_save_page(NsPageView *view, const char *dest);
/* Suggested file names for the two. */
char       *ns_page_view_save_page_name(NsPageView *view);
char       *ns_page_view_save_image_name(NsPageView *view);

G_END_DECLS

#endif
