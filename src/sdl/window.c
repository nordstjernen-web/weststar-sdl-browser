/* Weststar SDL Browser — the browser application: SDL windows, each with chrome and one page.
 * The chrome (toolbar, address bar, find bar, menus, status bubble) is built with Nuklear
 * every turn of the loop that brought input or a state change; the page is an NsPageView.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "window.h"
#include "plat/ns_plat.h"
#include "plat/ns_env.h"
#include "plat/ns_fs.h"

#include "icons.h"
#include "mainloop.h"
#include "nkui.h"
#include "pageview.h"
#include "ui.h"

#include "audio/audio.h"
#include "bookmarks.h"
#include "brand.h"
#include "config.h"
#include "css.h"
#include "download_name.h"
#include "history.h"
#include "i18n.h"
#include "net.h"
#include "security.h"
#include "version.h"

#include <SDL3/SDL.h>
#include <locale.h>
#include <math.h>
#include <string.h>

/* Chrome geometry, in UI units. */
#define TB_H          40.0f
#define TB_ROW        28.0f
#define TB_BUTTON     30.0f
#define FIND_W        420.0f
#define FIND_H        40.0f
#define MENU_W        250.0f
#define MENU_ROW      26.0f
#define STATUS_H      22.0f
#define ADDRESS_MAX   8192
#define FIND_MAX      512
#define MENU_BOOKMARKS 10

typedef enum {
    MENU_NONE,
    MENU_APP,
    MENU_CONTEXT,
} MenuKind;

typedef enum {
    ACT_NONE = 0,
    ACT_BACK, ACT_FORWARD, ACT_RELOAD, ACT_STOP, ACT_HOME,
    ACT_NEW_WINDOW, ACT_CLOSE_WINDOW, ACT_QUIT,
    ACT_FIND, ACT_FIND_NEXT, ACT_FIND_PREV,
    ACT_ZOOM_IN, ACT_ZOOM_OUT, ACT_ZOOM_RESET,
    ACT_SAVE_PAGE, ACT_SAVE_IMAGE,
    ACT_BOOKMARK, ACT_HISTORY, ACT_SETTINGS, ACT_ABOUT,
    ACT_FULLSCREEN, ACT_FOCUS_ADDRESS,
    ACT_OPEN_LINK, ACT_COPY_LINK, ACT_COPY, ACT_SELECT_ALL, ACT_COPY_URL,
    ACT_PRINT, ACT_DEVTOOLS,
    ACT_OPEN_BOOKMARK,        /* + index */
} Action;

typedef struct NsWindow NsWindow;

typedef struct {
    GList        *windows;
    ns_bookmarks *bookmarks;
    char         *home_url;
    gboolean      private_mode;
    char         *session_path;
    guint         session_timer;
    int           init_w, init_h;
} App;

struct NsWindow {
    SDL_Window    *win;
    SDL_WindowID   id;
    SDL_Renderer  *ren;
    NsPageView    *view;
    NsNk          *nk;
    gboolean       private_mode;

    int            px_w, px_h;       /* window in pixels */
    double         ui_scale;         /* pixels per UI unit */
    double         density;          /* pixels per window coordinate */

    gboolean       nk_open;          /* nk_input_begin called this turn */
    gboolean       nk_had_press;     /* a key or button went down this frame */
    gboolean       chrome_dirty;     /* rebuild the chrome this turn */
    gboolean       needs_present;

    char           address[ADDRESS_MAX];
    int            address_len;
    struct nk_rect address_rect;     /* UI units, from the last frame */
    gboolean       address_active;
    gboolean       address_focus_request;
    gboolean       address_unfocus_request;

    gboolean       find_open;
    char           find[FIND_MAX];
    int            find_len;
    char           find_last[FIND_MAX];
    gboolean       find_active;
    gboolean       find_focus_request;
    char           find_label[64];

    MenuKind       menu;
    float          menu_x, menu_y;   /* UI units */
    struct nk_rect menu_rect;
    char          *ctx_link;
    gboolean       ctx_selection;

    char          *status;           /* transient status, e.g. a hovered link */
    char          *status_note;      /* "Done", "Saved to ..." */
    guint          note_timer;
    gboolean       loading;
    gboolean       page_focus_wanted;

    SDL_Cursor    *cursor;
    char          *cursor_name;

    GString       *drop_paths;
    gboolean       closing;
};

static App g_app;

static void build_chrome(NsWindow *w);

static void window_close(NsWindow *w);
static NsWindow *window_new(const char *url, gboolean private_mode);

void
ns_sdl_app_set_window_size(int width, int height)
{
    g_app.init_w = width;
    g_app.init_h = height;
}

static const char *
brand_versioned(void)
{
    static char brand[128];
    if (!brand[0])
        g_snprintf(brand, sizeof brand, "%s %s", ns_i18n(NS_BRAND_NAME),
                   NS_VERSION);
    return brand;
}

/* ---- small helpers ------------------------------------------------------- */

static void
mark_dirty(NsWindow *w)
{
    w->chrome_dirty = TRUE;
    ns_sdl_wake();
}

static void
set_address(NsWindow *w, const char *text)
{
    g_strlcpy(w->address, text ? text : "", sizeof w->address);
    w->address_len = (int)strlen(w->address);
    mark_dirty(w);
}

/* The URL without any userinfo ("user:pass@"). */
static char *
address_display_url(const char *url)
{
    if (!url || !*url)
        return g_strdup("");
    const char *scheme_end = strstr(url, "://");
    if (!scheme_end)
        return g_strdup(url);
    const char *authority = scheme_end + 3;
    const char *authority_end = authority + strcspn(authority, "/?#");
    const char *at = NULL;
    for (const char *p = authority; p < authority_end; p++)
        if (*p == '@')
            at = p;
    if (!at)
        return g_strdup(url);
    return g_strdup_printf("%.*s%s", (int)(authority - url), url, at + 1);
}

/* Address-bar input as a URL: schemes kept, local paths mapped, searches, else https. */
static char *
normalize_url(const char *input)
{
    char *trimmed = g_strstrip(g_strdup(input ? input : ""));
    if (!*trimmed)
        return trimmed;
    if (g_str_has_prefix(trimmed, "about:") || g_str_has_prefix(trimmed, "file:") ||
        g_str_has_prefix(trimmed, "data:") ||
        g_str_has_prefix(trimmed, "view-source:") || strstr(trimmed, "://"))
        return trimmed;
    char *local = ns_url_from_local_path(trimmed);
    if (local) {
        g_free(trimmed);
        return local;
    }
    if (ns_address_is_search(trimmed)) {
        char *out = ns_search_url_for(trimmed);
        g_free(trimmed);
        return out;
    }
    char *out = g_strconcat("https://", trimmed, NULL);
    g_free(trimmed);
    return out;
}

static void
window_load(NsWindow *w, const char *input)
{
    char *url = normalize_url(input);
    if (*url) {
        set_address(w, url);
        ns_page_view_load(w->view, url);
    }
    g_free(url);
}

static void
update_title(NsWindow *w)
{
    const char *title = ns_page_view_title(w->view);
    const char *brand = brand_versioned();
    char *t = title && *title ? g_strdup_printf("%s — %s%s", title, brand,
                                                w->private_mode ? " (Private)" : "")
                              : g_strdup_printf("%s%s", brand,
                                                w->private_mode ? " (Private)" : "");
    SDL_SetWindowTitle(w->win, t);
    g_free(t);
}

static gboolean
note_expire(gpointer data)
{
    NsWindow *w = data;
    w->note_timer = 0;
    g_clear_pointer(&w->status_note, g_free);
    mark_dirty(w);
    return NS_SOURCE_REMOVE;
}

/* A status message that stays for a few seconds. */
static void
set_note(NsWindow *w, const char *text)
{
    g_free(w->status_note);
    w->status_note = text && *text ? g_strdup(text) : NULL;
    if (w->note_timer)
        ns_loop_source_remove(w->note_timer);
    w->note_timer = w->status_note ? ns_loop_timeout_add_seconds(NULL, 5, note_expire, w) : 0;
    mark_dirty(w);
}

static void
set_status(NsWindow *w, const char *text)
{
    if (g_strcmp0(w->status ? w->status : "", text ? text : "") == 0)
        return;
    g_free(w->status);
    w->status = text && *text ? g_strdup(text) : NULL;
    mark_dirty(w);
}

static SDL_SystemCursor
system_cursor_for(const char *css)
{
    static const struct { const char *name; SDL_SystemCursor cursor; } map[] = {
        { "pointer",       SDL_SYSTEM_CURSOR_POINTER },
        { "text",          SDL_SYSTEM_CURSOR_TEXT },
        { "vertical-text", SDL_SYSTEM_CURSOR_TEXT },
        { "wait",          SDL_SYSTEM_CURSOR_WAIT },
        { "progress",      SDL_SYSTEM_CURSOR_PROGRESS },
        { "crosshair",     SDL_SYSTEM_CURSOR_CROSSHAIR },
        { "cell",          SDL_SYSTEM_CURSOR_CROSSHAIR },
        { "move",          SDL_SYSTEM_CURSOR_MOVE },
        { "all-scroll",    SDL_SYSTEM_CURSOR_MOVE },
        { "grab",          SDL_SYSTEM_CURSOR_MOVE },
        { "grabbing",      SDL_SYSTEM_CURSOR_MOVE },
        { "not-allowed",   SDL_SYSTEM_CURSOR_NOT_ALLOWED },
        { "no-drop",       SDL_SYSTEM_CURSOR_NOT_ALLOWED },
        { "ew-resize",     SDL_SYSTEM_CURSOR_EW_RESIZE },
        { "col-resize",    SDL_SYSTEM_CURSOR_EW_RESIZE },
        { "e-resize",      SDL_SYSTEM_CURSOR_E_RESIZE },
        { "w-resize",      SDL_SYSTEM_CURSOR_W_RESIZE },
        { "ns-resize",     SDL_SYSTEM_CURSOR_NS_RESIZE },
        { "row-resize",    SDL_SYSTEM_CURSOR_NS_RESIZE },
        { "n-resize",      SDL_SYSTEM_CURSOR_N_RESIZE },
        { "s-resize",      SDL_SYSTEM_CURSOR_S_RESIZE },
        { "nwse-resize",   SDL_SYSTEM_CURSOR_NWSE_RESIZE },
        { "nw-resize",     SDL_SYSTEM_CURSOR_NW_RESIZE },
        { "se-resize",     SDL_SYSTEM_CURSOR_SE_RESIZE },
        { "nesw-resize",   SDL_SYSTEM_CURSOR_NESW_RESIZE },
        { "ne-resize",     SDL_SYSTEM_CURSOR_NE_RESIZE },
        { "sw-resize",     SDL_SYSTEM_CURSOR_SW_RESIZE },
    };
    for (gsize i = 0; css && i < G_N_ELEMENTS(map); i++)
        if (strcmp(css, map[i].name) == 0)
            return map[i].cursor;
    return SDL_SYSTEM_CURSOR_DEFAULT;
}

static void
set_cursor(NsWindow *w, const char *css)
{
    if (w->loading && (!css || !*css || strcmp(css, "default") == 0 ||
                       strcmp(css, "auto") == 0))
        css = "progress";
    if (g_strcmp0(w->cursor_name, css) == 0 && w->cursor)
        return;
    g_free(w->cursor_name);
    w->cursor_name = g_strdup(css);
    if (css && strcmp(css, "none") == 0) {
        SDL_HideCursor();
        return;
    }
    SDL_ShowCursor();
    SDL_Cursor *c = SDL_CreateSystemCursor(system_cursor_for(css));
    if (c) {
        SDL_SetCursor(c);
        if (w->cursor)
            SDL_DestroyCursor(w->cursor);
        w->cursor = c;
    }
}

/* ---- downloads ----------------------------------------------------------- */

typedef struct {
    NsWindow *window;
    char     *url;
    char     *path;
    char     *name;
    gboolean  ok;
    gsize     size;
} Download;

static gboolean
download_url_allowed(const char *url)
{
    static const char *const ok[] = {
        "http://", "https://", "ftp://", "data:", "blob:", NULL,
    };
    for (gsize i = 0; url && ok[i]; i++)
        if (g_ascii_strncasecmp(url, ok[i], strlen(ok[i])) == 0)
            return TRUE;
    return FALSE;
}

static gboolean
download_done(gpointer data)
{
    Download *d = data;
    if (g_list_find(g_app.windows, d->window)) {
        char *msg;
        if (d->ok) {
            char *sz = g_format_size((guint64)d->size);
            msg = g_strdup_printf("%s %s (%s)", ns_i18n("Downloaded"), d->name,
                                  sz);
            g_free(sz);
        } else {
            msg = g_strdup_printf("%s: %s", ns_i18n("Download failed"), d->name);
        }
        set_note(d->window, msg);
        g_free(msg);
    }
    g_free(d->url);
    g_free(d->path);
    g_free(d->name);
    g_free(d);
    return NS_SOURCE_REMOVE;
}

static gpointer
download_worker(gpointer data)
{
    Download *d = data;
    ns_security_deny_exec_memory(FALSE, FALSE);
    GError *err = NULL;
    ns_response *resp = ns_net_fetch_blocking(d->url, NULL, &err);
    if (resp && resp->final_url && !download_url_allowed(resp->final_url)) {
        ns_response_free(resp);
        resp = NULL;
    }
    if (resp && !resp->error && resp->body &&
        ns_fs_set_contents(d->path, (const char *)resp->body->data,
                           (gssize)resp->body->len)) {
        d->ok = TRUE;
        d->size = resp->body->len;
        ns_security_mark_download_origin(d->path,
                                         resp->final_url ? resp->final_url : d->url);
    }
    if (resp)
        ns_response_free(resp);
    g_clear_error(&err);
    ns_sdl_idle_add(download_done, d);
    return NULL;
}

static char *
download_name_from(const char *candidate)
{
    if (!candidate || !*candidate)
        return NULL;
    g_autofree char *base = g_path_get_basename(candidate);
    if (!base || strcmp(base, ".") == 0 || strcmp(base, G_DIR_SEPARATOR_S) == 0)
        return NULL;
    return ns_download_file_name(base);
}

static void
start_download(NsWindow *w, const char *url, const char *suggested)
{
    if (!url || !*url || !download_url_allowed(url))
        return;
    char *name = download_name_from(suggested);
    if (!name) {
        char *path_part = g_strdup(url);
        char *q = strpbrk(path_part, "?#");
        if (q)
            *q = '\0';
        name = download_name_from(path_part);
        g_free(path_part);
    }
    if (!name)
        name = g_strdup("download");
    const char *dir = ns_security_downloads_dir();
    char *path = g_build_filename(dir, name, NULL);
    for (int n = 1; ns_fs_test(path, NS_FS_TEST_EXISTS) && n < 1000; n++) {
        g_free(path);
        char *alt = g_strdup_printf("%s.%d", name, n);
        path = g_build_filename(dir, alt, NULL);
        g_free(alt);
    }
    char *msg = g_strdup_printf("%s %s…", ns_i18n("Downloading"), name);
    set_note(w, msg);
    g_free(msg);
    Download *d = g_new0(Download, 1);
    d->window = w;
    d->url = g_strdup(url);
    d->path = path;
    d->name = name;
    NsThread *t = ns_thread_new("ns-download", download_worker, d);
    if (t)
        ns_thread_unref(t);
}

/* ---- save dialogs -------------------------------------------------------- */

typedef struct {
    NsWindow *window;
    gboolean  image;
    char     *path;
} SaveChoice;

static gboolean
save_chosen_idle(gpointer data)
{
    SaveChoice *c = data;
    if (c->path && g_list_find(g_app.windows, c->window)) {
        if (c->image)
            ns_page_view_save_image(c->window->view, c->path);
        else
            ns_page_view_save_page(c->window->view, c->path);
    }
    g_free(c->path);
    g_free(c);
    return NS_SOURCE_REMOVE;
}

/* SDL may call this on another thread; the choice is handed to the main loop. */
static void SDLCALL
save_dialog_done(void *userdata, const char *const *files, int filter)
{
    (void)filter;
    SaveChoice *c = userdata;
    if (files && files[0])
        c->path = g_strdup(files[0]);
    ns_sdl_idle_add(save_chosen_idle, c);
}

static void
show_save_dialog(NsWindow *w, gboolean image)
{
    char *name = image ? ns_page_view_save_image_name(w->view)
                       : ns_page_view_save_page_name(w->view);
    char *initial = g_build_filename(ns_security_downloads_dir(), name, NULL);
    SaveChoice *c = g_new0(SaveChoice, 1);
    c->window = w;
    c->image = image;
    static const SDL_DialogFileFilter png[] = { { "PNG image", "png" } };
    SDL_ShowSaveFileDialog(save_dialog_done, c, w->win, image ? png : NULL,
                           image ? 1 : 0, initial);
    g_free(initial);
    g_free(name);
}

/* ---- actions ------------------------------------------------------------- */

static gboolean
current_page_bookmarked(NsWindow *w)
{
    const char *url = ns_page_view_url(w->view);
    return url && *url && g_app.bookmarks &&
           ns_bookmarks_contains(g_app.bookmarks, url);
}

static void
toggle_bookmark(NsWindow *w)
{
    const char *url = ns_page_view_url(w->view);
    if (!url || !*url || !g_app.bookmarks)
        return;
    if (ns_bookmarks_contains(g_app.bookmarks, url)) {
        ns_bookmarks_remove(g_app.bookmarks, url);
        set_note(w, ns_i18n("Bookmark removed"));
    } else {
        ns_bookmarks_add(g_app.bookmarks, url, ns_page_view_title(w->view));
        set_note(w, ns_i18n("Bookmarked"));
    }
}

static void
open_find(NsWindow *w)
{
    w->find_open = TRUE;
    w->find_focus_request = TRUE;
    w->menu = MENU_NONE;
    mark_dirty(w);
}

static void
close_find(NsWindow *w)
{
    if (!w->find_open)
        return;
    w->find_open = FALSE;
    w->find_active = FALSE;
    w->find_label[0] = '\0';
    w->find_last[0] = '\0';
    ns_page_view_find(w->view, "", 0, FALSE);
    ns_page_view_set_focus(w->view, TRUE);
    mark_dirty(w);
}

static void
focus_address(NsWindow *w)
{
    w->address_focus_request = TRUE;
    w->menu = MENU_NONE;
    ns_page_view_set_focus(w->view, FALSE);
    mark_dirty(w);
}

static void
run_action(NsWindow *w, int action)
{
    NsPageView *v = w->view;
    w->menu = MENU_NONE;
    mark_dirty(w);
    if (action >= ACT_OPEN_BOOKMARK) {
        guint i = (guint)(action - ACT_OPEN_BOOKMARK);
        if (g_app.bookmarks && i < ns_bookmarks_count(g_app.bookmarks))
            window_load(w, ns_bookmarks_get(g_app.bookmarks, i)->url);
        return;
    }
    switch ((Action)action) {
    case ACT_BACK:         ns_page_view_back(v); break;
    case ACT_FORWARD:      ns_page_view_forward(v); break;
    case ACT_RELOAD:       ns_page_view_reload(v); break;
    case ACT_STOP:         ns_page_view_stop(v); break;
    case ACT_HOME:         window_load(w, g_app.home_url); break;
    case ACT_NEW_WINDOW:   window_new(g_app.home_url, w->private_mode); break;
    case ACT_CLOSE_WINDOW: window_close(w); break;
    case ACT_QUIT:         ns_sdl_loop_quit(); break;
    case ACT_FIND:         open_find(w); break;
    case ACT_FIND_NEXT:
    case ACT_FIND_PREV:
        if (!w->find_open || w->find_len == 0) {
            open_find(w);
            break;
        }
        ns_page_view_find(v, w->find, action == ACT_FIND_NEXT ? 1 : 2, FALSE);
        break;
    case ACT_ZOOM_IN:      ns_page_view_zoom_in(v); break;
    case ACT_ZOOM_OUT:     ns_page_view_zoom_out(v); break;
    case ACT_ZOOM_RESET:   ns_page_view_zoom_reset(v); break;
    case ACT_SAVE_PAGE:    show_save_dialog(w, FALSE); break;
    case ACT_SAVE_IMAGE:   show_save_dialog(w, TRUE); break;
    case ACT_BOOKMARK:     toggle_bookmark(w); break;
    case ACT_HISTORY:      window_load(w, "about:history"); break;
    case ACT_SETTINGS:     window_load(w, "about:settings"); break;
    case ACT_ABOUT:        window_load(w, "about:weststar"); break;
    case ACT_FULLSCREEN:
        SDL_SetWindowFullscreen(w->win,
            !(SDL_GetWindowFlags(w->win) & SDL_WINDOW_FULLSCREEN));
        break;
    case ACT_FOCUS_ADDRESS: focus_address(w); break;
    case ACT_OPEN_LINK:
        if (w->ctx_link)
            window_load(w, w->ctx_link);
        break;
    case ACT_COPY_LINK:
        if (w->ctx_link) {
            SDL_SetClipboardText(w->ctx_link);
            set_note(w, ns_i18n("Copied link address"));
        }
        break;
    case ACT_COPY:         ns_page_view_copy_selection(v); break;
    case ACT_SELECT_ALL:   ns_page_view_select_all(v); break;
    case ACT_COPY_URL:
        if (ns_page_view_url(v)) {
            SDL_SetClipboardText(ns_page_view_url(v));
            set_note(w, ns_i18n("Copied page address"));
        }
        break;
    case ACT_PRINT:
        set_note(w, ns_i18n("Printing is not available yet; "
                            "use --headless --export=pdf:FILE"));
        break;
    case ACT_DEVTOOLS:
        set_note(w, ns_i18n("Developer tools are not available yet"));
        break;
    case ACT_NONE:
    case ACT_OPEN_BOOKMARK:
        break;
    }
}

/* ---- view events ----------------------------------------------------------- */

static void
on_view(NsPageView *v, NsViewEvent evt, const char *text, gpointer user_data)
{
    NsWindow *w = user_data;
    switch (evt) {
    case NS_VIEW_EVT_TITLE:
        if (!ns_page_view_is_private(v))
            ns_history_record(ns_page_view_url(v), text);
        update_title(w);
        break;
    case NS_VIEW_EVT_URL:
        if (!w->address_active) {
            char *shown = address_display_url(text);
            set_address(w, shown);
            g_free(shown);
        }
        mark_dirty(w);
        break;
    case NS_VIEW_EVT_STATUS:
        set_status(w, text);
        break;
    case NS_VIEW_EVT_HISTORY:
        mark_dirty(w);
        break;
    case NS_VIEW_EVT_LOADING: {
        gboolean loading = text && *text == '1';
        gboolean was = w->loading;
        w->loading = loading;
        if (was && !loading) {
            set_note(w, ns_i18n("Done"));
            if (!w->address_active && !w->find_active)
                ns_page_view_set_focus(v, TRUE);
        }
        set_cursor(w, w->cursor_name);
        mark_dirty(w);
        break;
    }
    case NS_VIEW_EVT_ZOOM:
        mark_dirty(w);
        break;
    case NS_VIEW_EVT_DOWNLOAD:
        if (text && *text) {
            char **parts = g_strsplit(text, "\t", 2);
            start_download(w, parts[0], parts[1] && *parts[1] ? parts[1] : NULL);
            g_strfreev(parts);
        }
        break;
    case NS_VIEW_EVT_CURSOR:
        set_cursor(w, text);
        break;
    case NS_VIEW_EVT_CONTEXT_MENU: {
        g_free(w->ctx_link);
        w->ctx_link = text && *text ? g_strdup(text) : NULL;
        w->ctx_selection = ns_page_view_has_selection(v);
        float mx = 0, my = 0;
        SDL_GetMouseState(&mx, &my);
        float to_ui = (float)(w->density / w->ui_scale);
        w->menu = MENU_CONTEXT;
        w->menu_x = mx * to_ui;
        w->menu_y = my * to_ui;
        mark_dirty(w);
        break;
    }
    case NS_VIEW_EVT_FIND:
        if (g_strcmp0(text, "none") == 0)
            g_strlcpy(w->find_label, w->find_len ? ns_i18n("No results") : "",
                      sizeof w->find_label);
        else
            g_strlcpy(w->find_label, text ? text : "", sizeof w->find_label);
        mark_dirty(w);
        break;
    case NS_VIEW_EVT_REDRAW:
        w->needs_present = TRUE;
        ns_sdl_wake();
        break;
    }
}

/* ---- chrome ---------------------------------------------------------------- */

static struct nk_color
nkc(NsColor c)
{
    return nk_rgba((int)lround(c.r * 255), (int)lround(c.g * 255),
                   (int)lround(c.b * 255), (int)lround(c.a * 255));
}

static float
win_w_ui(NsWindow *w)
{
    return (float)(w->px_w / w->ui_scale);
}

static float
win_h_ui(NsWindow *w)
{
    return (float)(w->px_h / w->ui_scale);
}

static gboolean
icon_button(struct nk_context *ctx, const char *icon, gboolean enabled)
{
    if (!enabled)
        nk_widget_disable_begin(ctx);
    gboolean hit = nk_button_image(ctx, ns_nk_icon(icon));
    if (!enabled)
        nk_widget_disable_end(ctx);
    return enabled && hit;
}

static void
build_toolbar(NsWindow *w, struct nk_context *ctx)
{
    const NsUiTheme *t = ns_ui_theme();
    float W = win_w_ui(w);
    int zoom = ns_page_view_zoom_percent(w->view);
    int sec = ns_page_view_security(w->view);
    const char *url = ns_page_view_url(w->view);
    const char *sec_label = NULL;
    if (url && *url && (sec == NS_SEC_INVALID || sec == NS_SEC_PLAIN))
        sec_label = ns_i18n("Not secure");
    float sec_w = sec_label
        ? ns_nk_font(w->nk, NS_NK_FONT_SMALL)->width(
              ns_nk_font(w->nk, NS_NK_FONT_SMALL)->userdata, 12, sec_label,
              (int)strlen(sec_label)) + 12 : 0;
    float zoom_w = zoom != 100 ? 52 : 0;
    int cols = 7 + (sec_label ? 1 : 0) + (zoom_w > 0 ? 1 : 0);
    /* Six buttons, the window's padding on both sides, the spacing between
     * widgets, and a pixel of slack. */
    float fixed = 6 * TB_BUTTON + 2 * 6 + (cols - 1) * 4 + sec_w + zoom_w + 1;
    float addr_w = MAX(80.0f, W - fixed);

    ctx->style.window.fixed_background = nk_style_item_color(nkc(t->toolbar));
    ctx->style.window.padding = nk_vec2(6, 6);
    if (!nk_begin(ctx, "toolbar", nk_rect(0, 0, W, TB_H),
                  NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BACKGROUND)) {
        nk_end(ctx);
        return;
    }
    nk_layout_row_begin(ctx, NK_STATIC, TB_ROW, cols);
    nk_layout_row_push(ctx, TB_BUTTON);
    if (icon_button(ctx, "weststar-back", ns_page_view_can_back(w->view)))
        run_action(w, ACT_BACK);
    nk_layout_row_push(ctx, TB_BUTTON);
    if (icon_button(ctx, "weststar-forward", ns_page_view_can_forward(w->view)))
        run_action(w, ACT_FORWARD);
    nk_layout_row_push(ctx, TB_BUTTON);
    if (w->loading) {
        if (icon_button(ctx, "weststar-stop", TRUE))
            run_action(w, ACT_STOP);
    } else if (icon_button(ctx, "weststar-reload", TRUE)) {
        run_action(w, ACT_RELOAD);
    }
    nk_layout_row_push(ctx, TB_BUTTON);
    if (icon_button(ctx, "weststar-home", TRUE))
        run_action(w, ACT_HOME);

    if (sec_label) {
        nk_layout_row_push(ctx, sec_w);
        nk_style_push_font(ctx, ns_nk_font(w->nk, NS_NK_FONT_SMALL));
        nk_label_colored(ctx, sec_label, NK_TEXT_CENTERED, nkc(t->insecure));
        nk_style_pop_font(ctx);
    }

    nk_layout_row_push(ctx, addr_w);
    nk_flags edit_flags = NK_EDIT_FIELD | NK_EDIT_SIG_ENTER | NK_EDIT_AUTO_SELECT;
    if (w->address_focus_request) {
        /* nk_edit_focus skips the activation that would auto-select, so
         * select the whole address here. */
        nk_edit_focus(ctx, edit_flags);
        ctx->current->edit.sel_start = 0;
        int chars = (int)g_utf8_strlen(w->address, w->address_len);
        ctx->current->edit.sel_end = chars;
        ctx->current->edit.cursor = chars;
        w->address_focus_request = FALSE;
    }
    if (w->address_unfocus_request) {
        nk_edit_unfocus(ctx);
        w->address_unfocus_request = FALSE;
    }
    ctx->style.edit.border_color = nkc(w->address_active ? t->focus
                                                         : t->field_border);
    w->address_rect = nk_widget_bounds(ctx);
    nk_flags ev = nk_edit_string(ctx, edit_flags, w->address, &w->address_len,
                                 ADDRESS_MAX - 1, nk_filter_default);
    w->address[w->address_len] = '\0';
    gboolean was_active = w->address_active;
    w->address_active = (ev & NK_EDIT_ACTIVE) != 0;
    if (ev & NK_EDIT_ACTIVATED)
        ns_page_view_set_focus(w->view, FALSE);
    if (ev & NK_EDIT_COMMITED) {
        char *input = g_strdup(w->address);
        w->address_unfocus_request = TRUE;
        w->address_active = FALSE;
        window_load(w, input);
        ns_page_view_set_focus(w->view, TRUE);
        g_free(input);
    } else if (was_active && !w->address_active) {
        /* Left without loading: show the page's address again. */
        char *shown = address_display_url(ns_page_view_url(w->view));
        set_address(w, shown);
        g_free(shown);
    }

    if (zoom_w > 0) {
        char label[16];
        g_snprintf(label, sizeof label, "%d%%", zoom);
        nk_layout_row_push(ctx, zoom_w);
        if (nk_button_label(ctx, label))
            run_action(w, ACT_ZOOM_RESET);
    }
    nk_layout_row_push(ctx, TB_BUTTON);
    if (icon_button(ctx, current_page_bookmarked(w) ? "weststar-bookmarks-saved"
                                                    : "weststar-bookmarks", TRUE))
        run_action(w, ACT_BOOKMARK);
    nk_layout_row_push(ctx, TB_BUTTON);
    if (icon_button(ctx, "weststar-settings", TRUE)) {
        if (w->menu == MENU_APP) {
            w->menu = MENU_NONE;
        } else {
            w->menu = MENU_APP;
            w->menu_x = W - MENU_W - 4;
            w->menu_y = TB_H - 2;
        }
    }
    nk_layout_row_end(ctx);

    /* The line between the chrome and the page. */
    struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
    nk_stroke_line(canvas, 0, TB_H - 1, W, TB_H - 1, 1, nkc(t->border));
    nk_end(ctx);
}

static void
build_find(NsWindow *w, struct nk_context *ctx)
{
    const NsUiTheme *t = ns_ui_theme();
    float W = win_w_ui(w);
    float x = MAX(0.0f, W - FIND_W - 8);
    ctx->style.window.fixed_background = nk_style_item_color(nkc(t->toolbar));
    if (!nk_begin(ctx, "find", nk_rect(x, TB_H + 6, FIND_W, FIND_H),
                  NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
        nk_end(ctx);
        return;
    }
    nk_layout_row_begin(ctx, NK_STATIC, TB_ROW, 5);
    nk_layout_row_push(ctx, 230);
    nk_flags flags = NK_EDIT_FIELD | NK_EDIT_SIG_ENTER;
    if (w->find_focus_request) {
        nk_edit_focus(ctx, flags);
        w->find_focus_request = FALSE;
    }
    ctx->style.edit.border_color = nkc(w->find_active ? t->focus : t->field_border);
    nk_flags ev = nk_edit_string(ctx, flags, w->find, &w->find_len,
                                 FIND_MAX - 1, nk_filter_default);
    w->find[w->find_len] = '\0';
    w->find_active = (ev & NK_EDIT_ACTIVE) != 0;
    if (ev & NK_EDIT_ACTIVATED)
        ns_page_view_set_focus(w->view, FALSE);
    if (strcmp(w->find, w->find_last) != 0) {
        g_strlcpy(w->find_last, w->find, sizeof w->find_last);
        ns_page_view_find(w->view, w->find, 0, FALSE);
        if (!*w->find)
            w->find_label[0] = '\0';
    }
    if (ev & NK_EDIT_COMMITED) {
        gboolean shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
        ns_page_view_find(w->view, w->find, shift ? 2 : 1, FALSE);
        nk_edit_focus(ctx, flags);
    }
    nk_layout_row_push(ctx, 70);
    nk_style_push_font(ctx, ns_nk_font(w->nk, NS_NK_FONT_SMALL));
    nk_label_colored(ctx, w->find_label, NK_TEXT_CENTERED, nkc(t->text_dim));
    nk_style_pop_font(ctx);
    nk_layout_row_push(ctx, TB_ROW);
    if (nk_button_symbol(ctx, NK_SYMBOL_TRIANGLE_UP))
        ns_page_view_find(w->view, w->find, 2, FALSE);
    nk_layout_row_push(ctx, TB_ROW);
    if (nk_button_symbol(ctx, NK_SYMBOL_TRIANGLE_DOWN))
        ns_page_view_find(w->view, w->find, 1, FALSE);
    nk_layout_row_push(ctx, TB_ROW);
    gboolean close = nk_button_symbol(ctx, NK_SYMBOL_X);
    nk_layout_row_end(ctx);
    nk_end(ctx);
    if (close)
        close_find(w);
}

/* One clickable menu row with a label and, right-aligned, its shortcut. */
static gboolean
menu_item(NsWindow *w, struct nk_context *ctx, const char *label,
          const char *accel, gboolean enabled)
{
    const NsUiTheme *t = ns_ui_theme();
    struct nk_rect r;
    nk_layout_row_dynamic(ctx, MENU_ROW, 1);
    if (nk_widget(&r, ctx) == NK_WIDGET_INVALID)
        return FALSE;
    struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
    gboolean hover = enabled && nk_input_is_mouse_hovering_rect(&ctx->input, r);
    if (hover)
        nk_fill_rect(canvas, r, 3, nkc(t->hover));
    const struct nk_user_font *font = ns_nk_font(w->nk, NS_NK_FONT_NORMAL);
    struct nk_color fg = nkc(enabled ? t->text : t->text_dim);
    nk_draw_text(canvas, nk_rect(r.x + 10, r.y, r.w - 20, r.h), label,
                 (int)strlen(label), font, nk_rgba(0, 0, 0, 0), fg);
    if (accel && *accel) {
        const struct nk_user_font *small = ns_nk_font(w->nk, NS_NK_FONT_SMALL);
        float aw = small->width(small->userdata, small->height, accel,
                                (int)strlen(accel));
        nk_draw_text(canvas, nk_rect(r.x + r.w - 10 - aw, r.y, aw + 2, r.h),
                     accel, (int)strlen(accel), small, nk_rgba(0, 0, 0, 0),
                     nkc(t->text_dim));
    }
    return enabled && nk_input_mouse_clicked(&ctx->input, NK_BUTTON_LEFT, r);
}

static void
menu_separator(struct nk_context *ctx)
{
    const NsUiTheme *t = ns_ui_theme();
    struct nk_rect r;
    nk_layout_row_dynamic(ctx, 7, 1);
    if (nk_widget(&r, ctx) == NK_WIDGET_INVALID)
        return;
    nk_stroke_line(nk_window_get_canvas(ctx), r.x + 4, r.y + 3, r.x + r.w - 4,
                   r.y + 3, 1, nkc(t->border));
}

typedef struct {
    const char *label;     /* NULL: separator */
    const char *accel;
    int         action;
    gboolean    enabled;
} MenuEntry;

static void
build_menu(NsWindow *w, struct nk_context *ctx, const MenuEntry *items, int n)
{
    const NsUiTheme *t = ns_ui_theme();
    float h = 2 * 4 + 2;
    for (int i = 0; i < n; i++)
        h += (items[i].label ? MENU_ROW : 7) + ctx->style.window.spacing.y;
    float x = w->menu_x, y = w->menu_y;
    if (x + MENU_W > win_w_ui(w)) x = MAX(0.0f, win_w_ui(w) - MENU_W);
    if (y + h > win_h_ui(w)) y = MAX(0.0f, win_h_ui(w) - h);
    w->menu_rect = nk_rect(x, y, MENU_W, h);
    ctx->style.window.fixed_background = nk_style_item_color(nkc(t->popup));
    ctx->style.window.padding = nk_vec2(4, 4);
    int chosen = ACT_NONE;
    if (nk_begin(ctx, "menu", w->menu_rect,
                 NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
        for (int i = 0; i < n; i++) {
            if (!items[i].label) {
                menu_separator(ctx);
                continue;
            }
            if (menu_item(w, ctx, items[i].label, items[i].accel,
                          items[i].enabled))
                chosen = items[i].action;
        }
    }
    nk_end(ctx);
    nk_window_set_focus(ctx, "menu");
    if (chosen != ACT_NONE)
        run_action(w, chosen);
}

static void
build_app_menu(NsWindow *w, struct nk_context *ctx)
{
    MenuEntry items[32 + MENU_BOOKMARKS];
    int n = 0;
#define ITEM(l, a, act, en) items[n++] = (MenuEntry){ (l), (a), (act), (en) }
#define SEP() items[n++] = (MenuEntry){ NULL, NULL, 0, FALSE }
    gboolean page = ns_page_view_url(w->view) != NULL;
    ITEM(ns_i18n("New Window"), "Ctrl+N", ACT_NEW_WINDOW, TRUE);
    SEP();
    ITEM(ns_i18n("Find in Page…"), "Ctrl+F", ACT_FIND, page);
    ITEM(ns_i18n("Zoom In"), "Ctrl++", ACT_ZOOM_IN, TRUE);
    ITEM(ns_i18n("Zoom Out"), "Ctrl+-", ACT_ZOOM_OUT, TRUE);
    ITEM(ns_i18n("Actual Size"), "Ctrl+0", ACT_ZOOM_RESET, TRUE);
    ITEM(ns_i18n("Full Screen"), "F11", ACT_FULLSCREEN, TRUE);
    SEP();
    ITEM(ns_i18n("Save Page As…"), "Ctrl+S", ACT_SAVE_PAGE, page);
    ITEM(ns_i18n("Save Page as Image…"), "", ACT_SAVE_IMAGE, page);
    SEP();
    ITEM(current_page_bookmarked(w) ? ns_i18n("Remove Bookmark")
                                    : ns_i18n("Bookmark This Page"),
         "Ctrl+D", ACT_BOOKMARK, page);
    guint nb = g_app.bookmarks ? ns_bookmarks_count(g_app.bookmarks) : 0;
    for (guint i = 0; i < nb && i < MENU_BOOKMARKS; i++) {
        const ns_bookmark *b = ns_bookmarks_get(g_app.bookmarks, i);
        ITEM(b->title && *b->title ? b->title : b->url, "",
             ACT_OPEN_BOOKMARK + (int)i, TRUE);
    }
    ITEM(ns_i18n("History"), "Ctrl+H", ACT_HISTORY, TRUE);
    SEP();
    ITEM(ns_i18n("Settings"), "", ACT_SETTINGS, TRUE);
    ITEM(ns_i18n("About Weststar SDL Browser"), "", ACT_ABOUT, TRUE);
    SEP();
    ITEM(ns_i18n("Close Window"), "Ctrl+W", ACT_CLOSE_WINDOW, TRUE);
    ITEM(ns_i18n("Quit"), "Ctrl+Q", ACT_QUIT, TRUE);
    build_menu(w, ctx, items, n);
}

static void
build_context_menu(NsWindow *w, struct nk_context *ctx)
{
    MenuEntry items[16];
    int n = 0;
    if (w->ctx_link) {
        ITEM(ns_i18n("Open Link"), "", ACT_OPEN_LINK, TRUE);
        ITEM(ns_i18n("Copy Link Address"), "", ACT_COPY_LINK, TRUE);
        SEP();
    }
    if (w->ctx_selection) {
        ITEM(ns_i18n("Copy"), "Ctrl+C", ACT_COPY, TRUE);
        SEP();
    }
    ITEM(ns_i18n("Back"), "Alt+Left", ACT_BACK, ns_page_view_can_back(w->view));
    ITEM(ns_i18n("Forward"), "Alt+Right", ACT_FORWARD,
         ns_page_view_can_forward(w->view));
    ITEM(ns_i18n("Reload"), "F5", ACT_RELOAD, TRUE);
    SEP();
    ITEM(ns_i18n("Select All"), "Ctrl+A", ACT_SELECT_ALL, TRUE);
    ITEM(ns_i18n("Copy Page Address"), "", ACT_COPY_URL, TRUE);
    ITEM(ns_i18n("Save Page As…"), "", ACT_SAVE_PAGE, TRUE);
    ITEM(ns_i18n("Save Page as Image…"), "", ACT_SAVE_IMAGE, TRUE);
#undef ITEM
#undef SEP
    build_menu(w, ctx, items, n);
}

static void
build_status(NsWindow *w, struct nk_context *ctx)
{
    const char *text = w->status ? w->status : w->status_note;
    if (!text || !*text)
        return;
    const NsUiTheme *t = ns_ui_theme();
    const struct nk_user_font *font = ns_nk_font(w->nk, NS_NK_FONT_SMALL);
    float tw = font->width(font->userdata, font->height, text, (int)strlen(text));
    float bw = MIN(tw + 18, win_w_ui(w) * 0.6f);
    ctx->style.window.fixed_background = nk_style_item_color(nkc(t->popup));
    ctx->style.window.padding = nk_vec2(8, 2);
    if (nk_begin(ctx, "status", nk_rect(0, win_h_ui(w) - STATUS_H, bw, STATUS_H),
                 NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_NO_INPUT |
                 NK_WINDOW_BACKGROUND)) {
        nk_layout_row_dynamic(ctx, STATUS_H - 6, 1);
        nk_style_push_font(ctx, font);
        nk_label_colored(ctx, text, NK_TEXT_LEFT, nkc(t->text_dim));
        nk_style_pop_font(ctx);
    }
    nk_end(ctx);
}

/* Builds this turn's chrome and draws it into the chrome layer. */
static void
build_chrome(NsWindow *w)
{
    struct nk_context *ctx = ns_nk_context(w->nk);
    if (!w->nk_open)
        ns_nk_input_begin(w->nk);
    ns_nk_input_end(w->nk);
    w->nk_open = FALSE;
    w->nk_had_press = FALSE;
    w->chrome_dirty = FALSE;

    build_toolbar(w, ctx);
    if (w->find_open)
        build_find(w, ctx);
    build_status(w, ctx);
    if (w->menu == MENU_APP)
        build_app_menu(w, ctx);
    else if (w->menu == MENU_CONTEXT)
        build_context_menu(w, ctx);

    if (ns_nk_render(w->nk, w->ren, w->px_w, w->px_h, w->ui_scale))
        w->needs_present = TRUE;
}

/* ---- geometry ---------------------------------------------------------------- */

static void
update_geometry(NsWindow *w)
{
    int pw = 0, ph = 0;
    SDL_GetWindowSizeInPixels(w->win, &pw, &ph);
    double density = SDL_GetWindowPixelDensity(w->win);
    double scale = SDL_GetWindowDisplayScale(w->win);
    if (!(density > 0)) density = 1.0;
    if (!(scale > 0)) scale = density;
    if (ns_getenv("NS_UI_SCALE")) {
        double forced = g_ascii_strtod(ns_getenv("NS_UI_SCALE"), NULL);
        if (forced >= 1.0 && forced <= 4.0)
            scale = forced;
    }
    w->px_w = pw;
    w->px_h = ph;
    w->density = density;
    w->ui_scale = scale;
    int top = (int)ceil(TB_H * scale);
    ns_page_view_set_area(w->view, 0, top, pw, MAX(1, ph - top), scale);
    w->needs_present = TRUE;
    mark_dirty(w);
}

/* Window coordinates of an event to UI units. */
static float
to_ui(NsWindow *w)
{
    return (float)(w->density / w->ui_scale);
}

static gboolean
point_in_rect(struct nk_rect r, float x, float y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

/* Whether a point (UI units) is over the chrome rather than the page. */
static gboolean
over_chrome(NsWindow *w, float x, float y)
{
    if (y < TB_H)
        return TRUE;
    if (w->find_open) {
        float fx = MAX(0.0f, win_w_ui(w) - FIND_W - 8);
        if (point_in_rect(nk_rect(fx, TB_H + 6, FIND_W, FIND_H), x, y))
            return TRUE;
    }
    if (w->menu != MENU_NONE && point_in_rect(w->menu_rect, x, y))
        return TRUE;
    return FALSE;
}

/* ---- events ----------------------------------------------------------------- */

static void
feed_nk(NsWindow *w, const SDL_Event *ev)
{
    gboolean press = ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                     ev->type == SDL_EVENT_KEY_DOWN;
    gboolean release = ev->type == SDL_EVENT_MOUSE_BUTTON_UP ||
                       ev->type == SDL_EVENT_KEY_UP;
    /* Nuklear sees a click only if the button is still down when the frame
     * ends: a release after a press in the same frame starts a new frame. */
    if (release && w->nk_had_press)
        build_chrome(w);
    if (press)
        w->nk_had_press = TRUE;
    if (!w->nk_open) {
        ns_nk_input_begin(w->nk);
        w->nk_open = TRUE;
    }
    ns_nk_input_event(w->nk, ev, to_ui(w));
    mark_dirty(w);
}

/* Window shortcuts, taken before the chrome or the page sees the key. */
static gboolean
shortcut(NsWindow *w, const SDL_KeyboardEvent *k)
{
    SDL_Keymod m = k->mod;
    gboolean ctrl = (m & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
    gboolean shift = (m & SDL_KMOD_SHIFT) != 0;
    gboolean alt = (m & SDL_KMOD_ALT) != 0;
    gboolean editing = w->address_active || w->find_active;
    int act = ACT_NONE;
    switch (k->key) {
    case SDLK_ESCAPE:
        if (w->menu != MENU_NONE) {
            w->menu = MENU_NONE;
            mark_dirty(w);
            return TRUE;
        }
        if (w->find_open && !w->address_active) {
            close_find(w);
            return TRUE;
        }
        if (w->address_active) {
            w->address_unfocus_request = TRUE;
            ns_page_view_set_focus(w->view, TRUE);
            mark_dirty(w);
            return TRUE;
        }
        if (w->loading) {
            run_action(w, ACT_STOP);
            return TRUE;
        }
        return FALSE;
    case SDLK_F5:  act = ACT_RELOAD; break;
    case SDLK_F6:  act = ACT_FOCUS_ADDRESS; break;
    case SDLK_F11: act = ACT_FULLSCREEN; break;
    case SDLK_F12: act = ACT_DEVTOOLS; break;
    case SDLK_F3:  act = shift ? ACT_FIND_PREV : ACT_FIND_NEXT; break;
    case SDLK_LEFT:
        if (alt && !ctrl) act = ACT_BACK;
        break;
    case SDLK_RIGHT:
        if (alt && !ctrl) act = ACT_FORWARD;
        break;
    case SDLK_HOME:
        if (alt && !ctrl) act = ACT_HOME;
        break;
    case SDLK_D:
        if (alt && !ctrl) act = ACT_FOCUS_ADDRESS;
        else if (ctrl) act = ACT_BOOKMARK;
        break;
    default:
        break;
    }
    if (act == ACT_NONE && ctrl) {
        switch (k->key) {
        case SDLK_L:      act = ACT_FOCUS_ADDRESS; break;
        case SDLK_R:      act = ACT_RELOAD; break;
        case SDLK_F:      act = ACT_FIND; break;
        case SDLK_G:      act = shift ? ACT_FIND_PREV : ACT_FIND_NEXT; break;
        case SDLK_N:      act = ACT_NEW_WINDOW; break;
        case SDLK_W:      act = ACT_CLOSE_WINDOW; break;
        case SDLK_Q:      act = ACT_QUIT; break;
        case SDLK_S:      act = ACT_SAVE_PAGE; break;
        case SDLK_P:      act = ACT_PRINT; break;
        case SDLK_H:      act = ACT_HISTORY; break;
        case SDLK_LEFTBRACKET:  act = ACT_BACK; break;
        case SDLK_RIGHTBRACKET: act = ACT_FORWARD; break;
        case SDLK_I:
        case SDLK_J:
            if (shift) act = ACT_DEVTOOLS;
            break;
        case SDLK_EQUALS:
        case SDLK_PLUS:
        case SDLK_KP_PLUS:
            if (!editing) act = ACT_ZOOM_IN;
            break;
        case SDLK_MINUS:
        case SDLK_KP_MINUS:
            if (!editing) act = ACT_ZOOM_OUT;
            break;
        case SDLK_0:
        case SDLK_KP_0:
            if (!editing) act = ACT_ZOOM_RESET;
            break;
        default:
            break;
        }
    }
    if (act == ACT_NONE)
        return FALSE;
    run_action(w, act);
    return TRUE;
}

static void
window_event(NsWindow *w, const SDL_Event *ev)
{
    float u = to_ui(w);
    switch (ev->type) {
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        window_close(w);
        return;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
    case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
        update_geometry(w);
        return;
    case SDL_EVENT_WINDOW_EXPOSED:
        w->needs_present = TRUE;
        return;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        w->page_focus_wanted = ns_page_view_has_focus(w->view);
        ns_page_view_set_focus(w->view, FALSE);
        if (w->menu != MENU_NONE) {
            w->menu = MENU_NONE;
            mark_dirty(w);
        }
        return;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        if (w->page_focus_wanted && !w->address_active && !w->find_active)
            ns_page_view_set_focus(w->view, TRUE);
        return;
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        ns_page_view_mouse_leave(w->view);
        return;
    case SDL_EVENT_MOUSE_MOTION: {
        feed_nk(w, ev);
        float x = ev->motion.x * u, y = ev->motion.y * u;
        gboolean chrome = over_chrome(w, x, y);
        gboolean left = (ev->motion.state & SDL_BUTTON_LMASK) != 0;
        if (!chrome || left)
            ns_page_view_mouse_move(w->view, (float)(ev->motion.x * w->density),
                                    (float)(ev->motion.y * w->density), left);
        if (chrome && !left)
            set_cursor(w, point_in_rect(w->address_rect, x, y) ? "text" : NULL);
        return;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        float x = ev->button.x * u, y = ev->button.y * u;
        if (w->menu != MENU_NONE && !point_in_rect(w->menu_rect, x, y)) {
            /* A press outside an open menu only closes it. */
            w->menu = MENU_NONE;
            mark_dirty(w);
            return;
        }
        feed_nk(w, ev);
        if (over_chrome(w, x, y))
            return;
        if (w->address_active)
            w->address_unfocus_request = TRUE;
        ns_page_view_mouse_down(w->view, (float)(ev->button.x * w->density),
                                (float)(ev->button.y * w->density),
                                ev->button.button, ev->button.clicks,
                                SDL_GetModState());
        return;
    }
    case SDL_EVENT_MOUSE_BUTTON_UP:
        feed_nk(w, ev);
        ns_page_view_mouse_up(w->view, (float)(ev->button.x * w->density),
                              (float)(ev->button.y * w->density),
                              ev->button.button);
        return;
    case SDL_EVENT_MOUSE_WHEEL: {
        float x = ev->wheel.mouse_x * u, y = ev->wheel.mouse_y * u;
        if (over_chrome(w, x, y)) {
            feed_nk(w, ev);
            return;
        }
        float dx = ev->wheel.x, dy = ev->wheel.y;
        if (ev->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
            dx = -dx;
            dy = -dy;
        }
        ns_page_view_wheel(w->view, dx, dy, SDL_GetModState());
        return;
    }
    case SDL_EVENT_KEY_DOWN:
        if (shortcut(w, &ev->key))
            return;
        if (w->address_active || w->find_active || w->menu != MENU_NONE) {
            feed_nk(w, ev);
            return;
        }
        ns_page_view_key_down(w->view, &ev->key);
        return;
    case SDL_EVENT_KEY_UP:
        if (w->address_active || w->find_active) {
            feed_nk(w, ev);
            return;
        }
        feed_nk(w, ev);   /* keeps Nuklear's modifier state right */
        ns_page_view_key_up(w->view, &ev->key);
        return;
    case SDL_EVENT_TEXT_INPUT:
        if (w->address_active || w->find_active) {
            feed_nk(w, ev);
            return;
        }
        ns_page_view_text_input(w->view, ev->text.text);
        return;
    case SDL_EVENT_DROP_BEGIN:
        if (w->drop_paths)
            g_string_free(w->drop_paths, TRUE);
        w->drop_paths = g_string_new(NULL);
        return;
    case SDL_EVENT_DROP_FILE:
        if (!w->drop_paths)
            w->drop_paths = g_string_new(NULL);
        if (ev->drop.data && *ev->drop.data) {
            if (w->drop_paths->len)
                g_string_append_c(w->drop_paths, '\n');
            g_string_append(w->drop_paths, ev->drop.data);
        }
        return;
    case SDL_EVENT_DROP_COMPLETE:
        if (w->drop_paths && w->drop_paths->len) {
            float x = ev->drop.x * u, y = ev->drop.y * u;
            if (y < TB_H) {
                /* Dropped on the toolbar: open the first file. */
                char *first = g_strndup(w->drop_paths->str,
                                        strcspn(w->drop_paths->str, "\n"));
                window_load(w, first);
                g_free(first);
            } else {
                ns_page_view_drop_files(w->view, x * (float)w->ui_scale,
                                        y * (float)w->ui_scale,
                                        g_strdup(w->drop_paths->str));
            }
        }
        if (w->drop_paths) {
            g_string_free(w->drop_paths, TRUE);
            w->drop_paths = NULL;
        }
        return;
    default:
        return;
    }
}

/* ---- color scheme and media ---------------------------------------------------- */

static void
apply_color_scheme(gboolean reload)
{
    ns_color_scheme_pref pref = ns_config_get()->color_scheme;
    gboolean dark = pref == NS_COLOR_SCHEME_PREF_DARK ||
                    (pref != NS_COLOR_SCHEME_PREF_LIGHT &&
                     SDL_GetSystemTheme() == SDL_SYSTEM_THEME_DARK);
    ns_ui_set_dark(dark);
    for (GList *l = g_app.windows; l; l = l->next) {
        NsWindow *w = l->data;
        ns_nk_apply_theme(w->nk, ns_ui_theme());
        mark_dirty(w);
    }
    ns_css_color_scheme want = dark ? NS_CSS_COLOR_SCHEME_DARK
                                    : NS_CSS_COLOR_SCHEME_LIGHT;
    if (want == ns_css_get_color_scheme())
        return;
    ns_css_set_color_scheme(want);
    if (!reload)
        return;
    for (GList *l = g_app.windows; l; l = l->next) {
        NsWindow *w = l->data;
        if (ns_page_view_url(w->view))
            ns_page_view_reload(w->view);
    }
}

/* Feeds display size, density, pointer and reduced-motion into CSS media queries. */
static void
configure_media_inputs(void)
{
    ns_css_media_device device = {
        .width = 1920,
        .height = 1080,
        .resolution_dppx = 1,
        .color_bits = 8,
        .pointer = NS_CSS_MEDIA_POINTER_FINE,
        .any_pointer = NS_CSS_MEDIA_POINTER_FINE,
        .hover = TRUE,
        .any_hover = TRUE,
        .update = NS_CSS_MEDIA_UPDATE_FAST,
    };
    SDL_DisplayID display = SDL_GetPrimaryDisplay();
    SDL_Rect bounds;
    if (display && SDL_GetDisplayBounds(display, &bounds)) {
        float scale = SDL_GetDisplayContentScale(display);
        if (!(scale > 0)) scale = 1.0f;
        device.width = bounds.w;
        device.height = bounds.h;
        device.resolution_dppx = scale;
    }
    ns_css_set_media_device(&device);
    ns_reduced_motion_pref motion = ns_config_get()->reduced_motion;
    ns_css_set_reduced_motion(motion == NS_REDUCED_MOTION_PREF_REDUCE
                                  ? NS_CSS_REDUCED_MOTION_REDUCE
                                  : NS_CSS_REDUCED_MOTION_NO_PREFERENCE);
}

/* ---- windows ------------------------------------------------------------------- */

static NsWindow *
window_for_id(SDL_WindowID id)
{
    for (GList *l = g_app.windows; l; l = l->next) {
        NsWindow *w = l->data;
        if (w->id == id)
            return w;
    }
    return NULL;
}

static NsWindow *
window_new(const char *url, gboolean private_mode)
{
    int ww = g_app.init_w > 0 ? g_app.init_w : 1200;
    int wh = g_app.init_h > 0 ? g_app.init_h : 820;
    SDL_Window *win = SDL_CreateWindow(brand_versioned(), ww, wh,
                                       SDL_WINDOW_RESIZABLE |
                                       SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!win) {
        g_printerr("weststar: cannot open a window: %s\n", SDL_GetError());
        return NULL;
    }
    SDL_Renderer *ren = SDL_CreateRenderer(win, NULL);
    if (!ren) {
        g_printerr("weststar: cannot create a renderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        return NULL;
    }
    SDL_SetRenderVSync(ren, 1);
    SDL_Surface *icon = ns_sdl_icon_surface("weststar", 64);
    if (icon) {
        SDL_SetWindowIcon(win, icon);
        SDL_DestroySurface(icon);
    }
    NsWindow *w = g_new0(NsWindow, 1);
    w->win = win;
    w->id = SDL_GetWindowID(win);
    w->ren = ren;
    w->private_mode = private_mode;
    w->nk = ns_nk_new();
    w->view = ns_page_view_new(ren);
    ns_page_view_set_private(w->view, private_mode);
    ns_page_view_set_notify(w->view, on_view, w);
    g_app.windows = g_list_append(g_app.windows, w);
    SDL_StartTextInput(win);
    update_geometry(w);
    update_title(w);
    if (url && *url)
        window_load(w, url);
    return w;
}

static void
window_close(NsWindow *w)
{
    if (!w || w->closing)
        return;
    w->closing = TRUE;
    g_app.windows = g_list_remove(g_app.windows, w);
    if (w->note_timer)
        ns_loop_source_remove(w->note_timer);
    ns_page_view_close(w->view);
    ns_nk_free(w->nk);
    if (w->cursor)
        SDL_DestroyCursor(w->cursor);
    SDL_StopTextInput(w->win);
    SDL_DestroyRenderer(w->ren);
    SDL_DestroyWindow(w->win);
    if (w->drop_paths)
        g_string_free(w->drop_paths, TRUE);
    g_free(w->ctx_link);
    g_free(w->status);
    g_free(w->status_note);
    g_free(w->cursor_name);
    g_free(w);
    if (!g_app.windows)
        ns_sdl_loop_quit();
}

static void
window_present(NsWindow *w)
{
    if (w->chrome_dirty || w->nk_open)
        build_chrome(w);
    if (!w->needs_present)
        return;
    w->needs_present = FALSE;
    const NsUiTheme *t = ns_ui_theme();
    SDL_SetRenderDrawColor(w->ren, (Uint8)(t->toolbar.r * 255),
                           (Uint8)(t->toolbar.g * 255),
                           (Uint8)(t->toolbar.b * 255), 255);
    SDL_RenderClear(w->ren);
    ns_page_view_present(w->view);
    ns_nk_present(w->nk, w->ren);
    SDL_RenderPresent(w->ren);
}

/* ---- the loop ------------------------------------------------------------------ */

static gboolean
on_event(const SDL_Event *ev, gpointer user_data)
{
    (void)user_data;
    switch (ev->type) {
    case SDL_EVENT_QUIT:
        return FALSE;
    case SDL_EVENT_SYSTEM_THEME_CHANGED:
        apply_color_scheme(TRUE);
        return TRUE;
    case SDL_EVENT_RENDER_DEVICE_RESET:
    case SDL_EVENT_RENDER_TARGETS_RESET:
        for (GList *l = g_app.windows; l; l = l->next) {
            NsWindow *w = l->data;
            ns_page_view_device_reset(w->view);
            ns_nk_device_reset(w->nk);
            mark_dirty(w);
        }
        return TRUE;
    default:
        break;
    }
    SDL_Window *sw = SDL_GetWindowFromEvent(ev);
    NsWindow *w = sw ? window_for_id(SDL_GetWindowID(sw)) : NULL;
    if (w)
        window_event(w, ev);
    return TRUE;
}

static void
on_idle(gpointer user_data)
{
    (void)user_data;
    GList *copy = g_list_copy(g_app.windows);
    for (GList *l = copy; l; l = l->next)
        if (g_list_find(g_app.windows, l->data))
            window_present(l->data);
    g_list_free(copy);
}

static gboolean
session_url_recoverable(const char *u)
{
    return u && (g_str_has_prefix(u, "http://") || g_str_has_prefix(u, "https://") ||
                 g_str_has_prefix(u, "ftp://") || g_str_has_prefix(u, "file://"));
}

/* Writes the first window's URL to the session file (empty when private). */
static gboolean
write_session_cb(gpointer data)
{
    (void)data;
    if (!g_app.session_path)
        return NS_SOURCE_REMOVE;
    NsWindow *w = g_app.windows ? g_app.windows->data : NULL;
    const char *url = w ? ns_page_view_url(w->view) : NULL;
    char *contents = w && !w->private_mode && session_url_recoverable(url)
                   ? g_strconcat(url, "\n", NULL) : g_strdup("");
    ns_fs_set_contents(g_app.session_path, contents, -1);
    g_free(contents);
    return NS_SOURCE_CONTINUE;
}

static char *
recovered_url(const char *session_path)
{
    char *contents = NULL, *found = NULL;
    if (!ns_fs_get_contents(session_path, &contents, NULL))
        return NULL;
    char **lines = g_strsplit(contents, "\n", -1);
    for (int i = 0; lines && lines[i]; i++)
        if (session_url_recoverable(lines[i])) {
            g_free(found);
            found = g_strdup(lines[i]);
        }
    g_strfreev(lines);
    g_free(contents);
    return found;
}

/* With --ignore-certificate-errors, asks before the first connection is made. */
static gboolean
confirm_ignored_cert_errors(void)
{
    const SDL_MessageBoxButtonData buttons[] = {
        { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, ns_i18n("Quit") },
        { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, ns_i18n("Continue") },
    };
    const SDL_MessageBoxData box = {
        .flags = SDL_MESSAGEBOX_WARNING,
        .title = ns_i18n("Certificate errors are being ignored"),
        .message = ns_i18n(
            "Weststar was started with --ignore-certificate-errors. Pages "
            "whose certificate is self-signed, expired or otherwise not "
            "trusted will open, marked \"Not secure\" in the address bar.\n\n"
            "Anyone between you and such a server can read and change what "
            "it sends. Use this only for servers you control."),
        .numbuttons = 2,
        .buttons = buttons,
    };
    int choice = 0;
    if (!SDL_ShowMessageBox(&box, &choice))
        return TRUE;
    return choice == 1;
}

static void
clear_cache_dir(const char *name, gint64 min_age_s)
{
    char *dir = g_build_filename(ns_fs_user_cache_dir(), NS_APP_DIR_NAME, name,
                                 NULL);
    gint64 cutoff = ns_time_real_us() / G_USEC_PER_SEC - min_age_s;
    GQueue *stack = g_queue_new();
    GPtrArray *dirs = g_ptr_array_new_with_free_func(g_free);
    g_queue_push_head(stack, g_strdup(dir));
    guint guard = 0;
    while (!g_queue_is_empty(stack) && guard++ < 100000) {
        char *d = g_queue_pop_head(stack);
        NsDir *gd = ns_dir_open(d);
        if (gd) {
            const char *e;
            while ((e = ns_dir_read_name(gd))) {
                char *child = g_build_filename(d, e, NULL);
                if (ns_fs_test(child, NS_FS_TEST_IS_SYMLINK) ||
                    !ns_fs_test(child, NS_FS_TEST_IS_DIR)) {
                    NsFsStat st;
                    if (min_age_s <= 0 ||
                        (ns_fs_stat(child, &st) == 0 && st.mtime < cutoff))
                        ns_fs_unlink(child);
                    g_free(child);
                } else {
                    g_queue_push_head(stack, child);
                }
            }
            ns_dir_close(gd);
        }
        g_ptr_array_add(dirs, d);
    }
    for (guint i = dirs->len; i > 1; i--)
        ns_fs_rmdir(g_ptr_array_index(dirs, i - 1));
    g_queue_free_full(stack, g_free);
    g_ptr_array_free(dirs, TRUE);
    g_free(dir);
}

/* Prunes object caches (all at exit, else older than an hour). */
static void
clear_http_caches(gboolean at_exit)
{
    static const char *const dirs[] = { "cache", "jsbc", "webfonts", "frames" };
    for (gsize i = 0; i < G_N_ELEMENTS(dirs); i++)
        clear_cache_dir(dirs[i], at_exit ? 0 : 3600);
}

int
ns_sdl_app_run(const char *startup_url, const char *session_path,
               gboolean recover, gboolean private_mode)
{
    clear_http_caches(FALSE);
    setlocale(LC_NUMERIC, "C");
    SDL_SetAppMetadata(NS_BRAND_NAME, NS_VERSION, NS_APP_ID);
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        g_printerr("weststar: cannot initialize SDL video: %s\n", SDL_GetError());
        return 1;
    }
    ns_history_init();
    g_app.bookmarks = ns_bookmarks_load();
    const ns_config *cfg = ns_config_get();
    g_app.home_url = g_strdup(cfg && cfg->home_url && *cfg->home_url
                              ? cfg->home_url : "about:start");
    g_app.private_mode = private_mode;
    g_app.session_path = g_strdup(session_path);
    configure_media_inputs();
    apply_color_scheme(FALSE);

    char *recovered = NULL;
    if (recover && !private_mode && session_path)
        recovered = recovered_url(session_path);
    const char *first = recovered ? recovered
                      : startup_url ? startup_url : g_app.home_url;
    int status = 0;
    if (ns_net_ignore_cert_errors() && !confirm_ignored_cert_errors()) {
        first = NULL;
        status = 0;
    } else {
        NsWindow *w = window_new(first, private_mode);
        if (!w) {
            status = 1;
        } else {
            if (recovered)
                set_note(w, ns_i18n("Recovered the previous session after "
                                    "an unexpected exit"));
            if (g_app.session_path)
                g_app.session_timer = ns_loop_timeout_add_seconds(
                    NULL, 4, write_session_cb, NULL);
            ns_sdl_loop_run(on_event, on_idle, NULL);
        }
    }
    g_free(recovered);
    while (g_app.windows)
        window_close(g_app.windows->data);
    if (g_app.session_timer)
        ns_loop_source_remove(g_app.session_timer);
    ns_audio_shutdown();
    if (g_app.bookmarks)
        ns_bookmarks_free(g_app.bookmarks);
    g_free(g_app.home_url);
    g_free(g_app.session_path);
    clear_http_caches(TRUE);
    SDL_Quit();
    return status;
}
