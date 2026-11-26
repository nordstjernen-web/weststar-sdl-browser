/* Weststar SDL Browser — NsPageView: one page shown in an SDL window, driven on the engine thread.
 * Posts load, render, input and find requests to the engine thread and uploads the frames
 * (or only their damaged rectangles) it gets back into an SDL texture.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "pageview.h"
#include "plat/ns_plat.h"
#include "plat/ns_env.h"
#include "plat/ns_fs.h"

#include "enginethread.h"
#include "keys.h"
#include "mainloop.h"
#include "ui.h"

#include "i18n.h"
#include "net.h"
#include "page_session.h"
#include "proc_limits.h"
#include "security.h"
#include "trace.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define NS_VIEW_CARET_BLINK_MS 530
#define NS_VIEW_ANIM_FRAME_MS  16
#define NS_VIEW_LINE_PX        60.0
#define NS_VIEW_SCROLLBAR_UI   10.0

typedef enum {
    REQ_LOAD, REQ_RENDER, REQ_LINK, REQ_CLICK, REQ_VIEWPORT, REQ_KEY,
    REQ_SELECT, REQ_HOVER, REQ_RELEASE, REQ_FIND, REQ_EXPORT,
    REQ_DROPFILES, REQ_SCROLL, REQ_SCROLLBAR, REQ_CLOSE
} ReqType;

typedef enum { ACT_HOVER, ACT_NAVIGATE, ACT_CONTEXT } LinkAct;

typedef struct {
    NsPageView *view;
    ReqType     type;
    int         seq;
    char       *url;
    int         vw, vh;
    int         w, h, sx, sy;
    double      scale;
    int         x, y;
    int         dx, dy;
    int         mods;
    int         kind;
    int         keycode;
    int         fallback_scroll;
    double      fallback_x, fallback_y;
    char       *key;
    char       *code;
    LinkAct     action;
    char       *query;
    int         find_dir;
    int         find_from_y;
    int         find_case;
    char       *export_dest;
    char       *paths;
    gboolean    history;
    gboolean    user_activated;
    gboolean    caret_active;
    gboolean    force_full;
    gint64      frame_time_us;
} Req;

typedef enum {
    RES_PAGE, RES_FRAME, RES_LINK, RES_CLICK, RES_VIEWPORT, RES_KEY,
    RES_SELECT, RES_COPY, RES_HOVER, RES_RELEASE, RES_FIND, RES_EXPORT,
    RES_DROPFILES, RES_SCROLL, RES_SCROLLBAR
} ResType;

typedef struct {
    NsPageView *view;
    ResType     type;
    int         seq;
    gboolean    ok;
    int         pw, ph;
    char       *title;
    char       *url;
    gboolean    url_pushed;
    char       *nav;
    int         security;
    char       *download;
    char       *clipboard;
    /* Frame pixels: the whole frame, or the bounding box of the damage. */
    guint8     *pixels;
    int         px_x, px_y, px_w, px_h, px_stride;
    gboolean    is_patch;
    int        *damage;
    int         n_damage;
    guint64     gen, base_gen;
    char       *href;
    char       *cursor;
    LinkAct     action;
    int         kind;
    int         prevented;
    int         fallback_scroll;
    double      fallback_x, fallback_y;
    gboolean    animating;
    gboolean    caret_blinking;
    int         requested_scroll_y, requested_scroll_x;
    int         find_total, find_current, find_scroll_y;
} Res;

struct NsPageView {
    gatomicrefcount  rc;
    gboolean         closed;

    /* Engine-thread side. */
    ns_page_session *session;

    /* Main-thread side. */
    SDL_Renderer    *renderer;
    SDL_Texture     *texture;
    int              tex_w, tex_h;
    guint64          frame_gen;
    gboolean         have_frame;
    gboolean         frame_force_full;

    NsViewNotify     notify;
    gpointer         notify_ud;
    gboolean         private_mode;

    int              ax, ay, aw, ah;      /* page area, window pixels */
    double           ui_scale;

    char            *current_url;
    char            *current_title;
    int              security;
    int              page_w, page_h;      /* CSS px */
    int              scroll_x, scroll_y;  /* CSS px */
    gboolean         opened;
    gboolean         loading;
    gboolean         focused;
    double           zoom;

    gboolean         render_inflight, render_pending;
    gboolean         viewport_inflight, viewport_pending;
    gboolean         scroll_inflight, scroll_pending;
    double           scroll_pending_dx, scroll_pending_dy;
    gboolean         link_inflight, link_pending;
    int              link_pending_x, link_pending_y;
    LinkAct          link_pending_action;
    gboolean         hover_inflight, hover_pending;
    int              hover_pending_x, hover_pending_y;

    int              load_seq, render_seq, link_seq, click_seq;
    int              viewport_seq, key_seq, select_seq, hover_seq;
    int              scroll_seq, find_seq;
    int              last_vp_w, last_vp_h;

    GPtrArray       *history;
    int              hist_index;
    gboolean         pending_record;
    int              js_redirects;

    char            *deferred_url;
    gboolean         deferred_record, deferred_history, deferred_activated;

    gboolean         has_selection;
    gboolean         multi_click;
    gboolean         left_down;
    double           pointer_x, pointer_y;    /* area pixels */
    double           drag_start_x, drag_start_y;
    gboolean         drag_anchored;
    gboolean         sb_probe, sb_dragging, sb_have_last;
    double           sb_last_x, sb_last_y;

    /* The viewport scrollbars the view draws itself. */
    int              vbar_drag;             /* 0 none, 1 vertical, 2 horizontal */
    double           vbar_grab;             /* pointer offset into the thumb */

    /* A typed key waits for its text-input event. */
    gboolean         typed_pending;
    SDL_Keycode      typed_key;
    SDL_Scancode     typed_scancode;
    SDL_Keymod       typed_mod;

    guint            anim_id;
    guint            anim_interval;
    gboolean         page_animating;
    gboolean         caret_blinking;
};

enum {
    NS_PV_ZOOM_MIN_PERMILLE = (int)(NS_PROC_ZOOM_MIN * 1000.0 + 0.5),
    NS_PV_ZOOM_MAX_PERMILLE = (int)(NS_PROC_ZOOM_MAX * 1000.0 + 0.5)
};

static NsPageView *pv_ref(NsPageView *v) { g_atomic_ref_count_inc(&v->rc); return v; }

static void
pv_free(NsPageView *v)
{
    if (v->texture)
        SDL_DestroyTexture(v->texture);
    if (v->history)
        g_ptr_array_unref(v->history);
    g_free(v->current_url);
    g_free(v->current_title);
    g_free(v->deferred_url);
    g_free(v);
}

static void pv_unref(NsPageView *v) { if (g_atomic_ref_count_dec(&v->rc)) pv_free(v); }

static int
pv_settle_ms(void)
{
    const char *e = ns_getenv(NS_PROC_SETTLE_ENV);
    if (e && *e) {
        int n = atoi(e);
        if (n >= 0 && n <= 10000)
            return n;
    }
    return NS_PROC_SETTLE_MS;
}

static void
emit(NsPageView *v, NsViewEvent evt, const char *text)
{
    if (v->notify && !v->closed)
        v->notify(v, evt, text, v->notify_ud);
}

/* ---- engine thread ----------------------------------------------------- */

static gboolean on_result(gpointer data);

static void
post(Res *res)
{
    ns_sdl_idle_add(on_result, res);
}

static gboolean
pv_unref_idle(gpointer data)
{
    pv_unref(data);
    return NS_SOURCE_REMOVE;
}

static Res *
res_new(NsPageView *v, ResType type, int seq)
{
    Res *res = g_new0(Res, 1);
    res->view = pv_ref(v);
    res->type = type;
    res->seq = seq;
    return res;
}

static void
req_free(Req *req)
{
    g_free(req->url);
    g_free(req->key);
    g_free(req->code);
    g_free(req->query);
    g_free(req->export_dest);
    g_free(req->paths);
    g_free(req);
}

/* Copies a whole frame out of the session's buffer. */
static void
stage_full(Res *res, const guint8 *px, int w, int h, int stride)
{
    gsize row = (gsize)w * 4u;
    res->pixels = g_try_malloc(row * (gsize)h);
    if (!res->pixels)
        return;
    for (int y = 0; y < h; y++)
        memcpy(res->pixels + (gsize)y * row, px + (gsize)y * stride, row);
    res->px_x = res->px_y = 0;
    res->px_w = w;
    res->px_h = h;
    res->px_stride = (int)row;
    res->is_patch = FALSE;
}

/* Copies the damaged rectangles into one buffer covering their bounding box. */
static gboolean
stage_patch(Res *res, const guint8 *px, int stride, const int *rects, int n)
{
    int x0 = G_MAXINT, y0 = G_MAXINT, x1 = G_MININT, y1 = G_MININT;
    for (int i = 0; i < n; i++) {
        x0 = MIN(x0, rects[4 * i]);
        y0 = MIN(y0, rects[4 * i + 1]);
        x1 = MAX(x1, rects[4 * i] + rects[4 * i + 2]);
        y1 = MAX(y1, rects[4 * i + 1] + rects[4 * i + 3]);
    }
    if (x1 <= x0 || y1 <= y0)
        return FALSE;
    int w = x1 - x0, h = y1 - y0;
    gsize row = (gsize)w * 4u;
    res->pixels = g_try_malloc0(row * (gsize)h);
    if (!res->pixels)
        return FALSE;
    for (int i = 0; i < n; i++) {
        int rx = rects[4 * i], ry = rects[4 * i + 1];
        gsize len = (gsize)rects[4 * i + 2] * 4u;
        for (int y = ry; y < ry + rects[4 * i + 3]; y++)
            memcpy(res->pixels + (gsize)(y - y0) * row + (gsize)(rx - x0) * 4u,
                   px + (gsize)y * stride + (gsize)rx * 4u, len);
    }
    res->px_x = x0;
    res->px_y = y0;
    res->px_w = w;
    res->px_h = h;
    res->px_stride = (int)row;
    res->is_patch = TRUE;
    return TRUE;
}

static void
run_load(NsPageView *v, ns_page_session *s, Req *req)
{
    Res *res = res_new(v, RES_PAGE, req->seq);
    ns_page_info pg;
    if (ns_page_session_open(s, req->url, req->vw, req->vh, pv_settle_ms(),
                             req->history, req->user_activated, &pg) == 0 &&
        pg.ok) {
        res->ok = TRUE;
        res->pw = pg.page_width;
        res->ph = pg.page_height;
        res->title = g_strdup(pg.title ? pg.title : "");
        res->url = g_strdup(pg.url ? pg.url : req->url);
        res->nav = pg.nav ? g_strdup(pg.nav) : NULL;
        res->security = pg.security;
    }
    ns_page_info_clear(&pg);
    post(res);
}

static void
run_render(NsPageView *v, ns_page_session *s, Req *req)
{
    Res *res = res_new(v, RES_FRAME, req->seq);
    ns_page_frame fr;
    ns_page_session_set_frame_time(s, req->frame_time_us);
    if (req->force_full)
        ns_page_session_invalidate_frame(s);
    if (ns_page_session_render(s, req->w, req->h, req->sx, req->sy,
                               req->scale, req->caret_active, &fr) == 0) {
        res->ok = TRUE;
        res->animating = fr.animating ? TRUE : FALSE;
        res->caret_blinking = fr.caret_blinking ? TRUE : FALSE;
        res->pw = fr.page_w;
        res->ph = fr.page_h;
        res->requested_scroll_y = fr.scroll_y;
        res->requested_scroll_x = fr.scroll_x;
        if (!fr.unchanged) {
            gint64 trace_start = ns_trace_now();
            gboolean patched = FALSE;
            if (fr.n_damage > 0) {
                patched = stage_patch(res, fr.pixels, fr.stride, fr.damage,
                                      fr.n_damage);
                if (patched) {
                    res->damage = fr.damage;
                    res->n_damage = fr.n_damage;
                    fr.damage = NULL;
                    fr.n_damage = 0;
                }
            }
            if (!patched)
                stage_full(res, fr.pixels, fr.width, fr.height, fr.stride);
            res->gen = fr.gen;
            res->base_gen = fr.base_gen;
            ns_trace_complete("frame", patched ? "copy damage" : "copy frame",
                              trace_start, NULL);
        }
        res->nav = fr.nav;
        res->url = fr.url;
        res->url_pushed = fr.url_pushed ? TRUE : FALSE;
        res->title = fr.title;
        res->download = fr.download;
        fr.nav = fr.url = fr.title = fr.download = NULL;
        if (fr.clipboard)
            res->clipboard = ns_page_session_clipboard(s);
    }
    ns_page_frame_clear(&fr);
    post(res);
}

static void arm_anim(NsPageView *v);

static gboolean
pv_wake_idle(gpointer data)
{
    NsPageView *v = data;
    if (!v->closed && v->opened)
        arm_anim(v);
    pv_unref(v);
    return NS_SOURCE_REMOVE;
}

/* The engine asks for a frame (a mutation, a timer, a loaded image). */
static void
pv_session_wake(gpointer data)
{
    ns_sdl_idle_add(pv_wake_idle, pv_ref(data));
}

static void
run_req(gpointer data)
{
    Req *req = data;
    NsPageView *v = req->view;
    if (req->type == REQ_CLOSE) {
        ns_page_session_free(v->session);
        v->session = NULL;
        req_free(req);
        ns_sdl_idle_add(pv_unref_idle, v);
        return;
    }
    if (!v->session) {
        v->session = ns_page_session_new(NS_PROC_MAX_WIDTH, NS_PROC_MAX_HEIGHT);
        ns_page_session_set_wake(v->session, pv_session_wake, v);
    }
    ns_page_session *s = v->session;
    Res *res;
    switch (req->type) {
    case REQ_LOAD:
        run_load(v, s, req);
        break;
    case REQ_RENDER:
        run_render(v, s, req);
        break;
    case REQ_LINK:
        res = res_new(v, RES_LINK, req->seq);
        res->action = req->action;
        if (req->action == ACT_HOVER) {
            res->href = ns_page_session_link_at(s, req->x, req->y, &res->cursor);
        } else if (req->action == ACT_CONTEXT) {
            res->prevented = ns_page_session_contextmenu(s, req->x, req->y);
            if (!res->prevented)
                res->href = ns_page_session_link_at(s, req->x, req->y, NULL);
        } else {
            res->href = ns_page_session_link_at(s, req->x, req->y, NULL);
        }
        post(res);
        break;
    case REQ_CLICK:
        res = res_new(v, RES_CLICK, req->seq);
        res->href = ns_page_session_click(s, req->x, req->y, req->mods);
        post(res);
        break;
    case REQ_VIEWPORT: {
        res = res_new(v, RES_VIEWPORT, req->seq);
        ns_page_info pg;
        if (ns_page_session_set_viewport(s, req->vw, req->vh, &pg) == 0) {
            res->ok = pg.ok;
            res->pw = pg.page_width;
            res->ph = pg.page_height;
        }
        ns_page_info_clear(&pg);
        post(res);
        break;
    }
    case REQ_KEY:
        res = res_new(v, RES_KEY, req->seq);
        res->kind = req->kind;
        res->fallback_scroll = req->fallback_scroll;
        res->fallback_x = req->fallback_x;
        res->fallback_y = req->fallback_y;
        res->href = ns_page_session_key(s, req->kind, req->key, req->code,
                                        req->keycode, req->mods,
                                        &res->prevented);
        post(res);
        break;
    case REQ_SELECT:
        res = res_new(v, req->kind == 4 ? RES_COPY : RES_SELECT, req->seq);
        res->href = ns_page_session_select(s, req->kind, req->x, req->y);
        post(res);
        break;
    case REQ_HOVER:
        res = res_new(v, RES_HOVER, req->seq);
        res->ok = ns_page_session_hover(s, req->x, req->y, &res->href,
                                        &res->cursor) == 1;
        post(res);
        break;
    case REQ_SCROLL:
        res = res_new(v, RES_SCROLL, req->seq);
        res->ok = ns_page_session_scroll(s, req->x, req->y, req->dx, req->dy);
        res->fallback_x = req->fallback_x;
        res->fallback_y = req->fallback_y;
        post(res);
        break;
    case REQ_SCROLLBAR:
        res = res_new(v, RES_SCROLLBAR, req->seq);
        res->kind = req->kind;
        res->ok = ns_page_session_scrollbar(s, req->kind, req->x, req->y);
        post(res);
        break;
    case REQ_DROPFILES:
        res = res_new(v, RES_DROPFILES, req->seq);
        if (req->paths && *req->paths) {
            char **list = g_strsplit(req->paths, "\n", -1);
            guint count = list ? g_strv_length(list) : 0;
            if (count > 0)
                res->ok = ns_page_session_drop_files(
                    s, req->x, req->y, (const char *const *)list,
                    (int)count) == 1;
            g_strfreev(list);
        }
        post(res);
        break;
    case REQ_RELEASE:
        res = res_new(v, RES_RELEASE, req->seq);
        res->href = ns_page_session_release_at(s, req->x, req->y, &res->ok);
        post(res);
        break;
    case REQ_FIND:
        res = res_new(v, RES_FIND, req->seq);
        ns_page_session_find(s, req->query, req->find_case, req->find_dir,
                             req->find_from_y, &res->find_total,
                             &res->find_current, &res->find_scroll_y);
        post(res);
        break;
    case REQ_EXPORT: {
        res = res_new(v, RES_EXPORT, req->seq);
        gboolean ok = FALSE;
        if (req->url && req->export_dest &&
            ns_page_session_export(s, req->url) == 0) {
            char *bytes = NULL;
            gsize len = 0;
            ok = ns_fs_get_contents(req->url, &bytes, &len) &&
                 ns_fs_set_contents(req->export_dest, bytes, (gssize)len);
            g_free(bytes);
        }
        if (req->url)
            ns_fs_unlink(req->url);
        res->ok = ok;
        res->url = g_strdup(req->export_dest ? req->export_dest : "");
        post(res);
        break;
    }
    case REQ_CLOSE:
        break;
    }
    req_free(req);
    ns_sdl_idle_add(pv_unref_idle, v);
}

static void
push_req(NsPageView *v, Req *req)
{
    if (v->closed && req->type != REQ_CLOSE) {
        req_free(req);
        return;
    }
    req->view = pv_ref(v);
    ns_engine_thread_post(run_req, req);
}

/* ---- geometry ---------------------------------------------------------- */

static double
cur_zoom(NsPageView *v)
{
    return v->zoom > 0.0 ? v->zoom : 1.0;
}

/* Device pixels per CSS pixel: the page zoom times the UI scale. */
static double
cur_scale(NsPageView *v)
{
    return cur_zoom(v) * (v->ui_scale > 0 ? v->ui_scale : 1.0);
}

static int
area_w(NsPageView *v)
{
    return v->aw > 1 ? v->aw : 1;
}

static int
area_h(NsPageView *v)
{
    return v->ah > 1 ? v->ah : 1;
}

/* The CSS width the page is laid out at: the area at 100% zoom. */
static int
layout_px(NsPageView *v, int px)
{
    double s = v->ui_scale > 0 ? v->ui_scale : 1.0;
    int css = (int)(px / s + 0.5);
    return css > 1 ? css : 1;
}

static double
visible_w(NsPageView *v)
{
    return area_w(v) / cur_scale(v);
}

static double
visible_h(NsPageView *v)
{
    return area_h(v) / cur_scale(v);
}

static void
clamp_scroll(NsPageView *v)
{
    int max_x = (int)ceil(v->page_w - visible_w(v));
    int max_y = (int)ceil(v->page_h - visible_h(v));
    if (max_x < 0) max_x = 0;
    if (max_y < 0) max_y = 0;
    v->scroll_x = CLAMP(v->scroll_x, 0, max_x);
    v->scroll_y = CLAMP(v->scroll_y, 0, max_y);
}

/* Area pixels to page CSS pixels. */
static int
page_x(NsPageView *v, double ax)
{
    return v->scroll_x + (int)(ax / cur_scale(v));
}

static int
page_y(NsPageView *v, double ay)
{
    return v->scroll_y + (int)(ay / cur_scale(v));
}

/* ---- rendering --------------------------------------------------------- */

static void start_render(NsPageView *v);

static void
request_render(NsPageView *v)
{
    if (!v->opened)
        return;
    if (v->render_inflight) {
        v->render_pending = TRUE;
        return;
    }
    start_render(v);
}

static void
start_render(NsPageView *v)
{
    if (!v->opened)
        return;
    v->render_inflight = TRUE;
    Req *req = g_new0(Req, 1);
    req->type = REQ_RENDER;
    req->seq = ++v->render_seq;
    req->w = area_w(v);
    req->h = area_h(v);
    req->sx = v->scroll_x;
    req->sy = v->scroll_y;
    req->scale = cur_scale(v);
    req->caret_active = v->focused;
    req->force_full = v->frame_force_full || !v->have_frame;
    v->frame_force_full = FALSE;
    req->frame_time_us = ns_time_monotonic_us();
    push_req(v, req);
}

static void
set_scroll(NsPageView *v, double x, double y)
{
    int ox = v->scroll_x, oy = v->scroll_y;
    v->scroll_x = (int)x;
    v->scroll_y = (int)y;
    clamp_scroll(v);
    if (v->scroll_x != ox || v->scroll_y != oy)
        request_render(v);
}

static gboolean
anim_tick(gpointer data)
{
    NsPageView *v = data;
    if (v->closed || !v->opened) {
        v->anim_id = 0;
        return NS_SOURCE_REMOVE;
    }
    request_render(v);
    return NS_SOURCE_CONTINUE;
}

static void
disarm_anim(NsPageView *v)
{
    if (v->anim_id)
        ns_loop_source_remove(v->anim_id);
    v->anim_id = 0;
    v->anim_interval = 0;
}

/* Renders at 60 fps while the page animates, at the caret's rate while a
 * caret blinks, and once when the engine wakes the view. */
static void
arm_anim(NsPageView *v)
{
    if (v->closed || !v->opened)
        return;
    guint want = v->page_animating ? NS_VIEW_ANIM_FRAME_MS
               : v->caret_blinking ? NS_VIEW_CARET_BLINK_MS : 0;
    if (want == 0) {
        disarm_anim(v);
        request_render(v);
        return;
    }
    if (v->anim_id && v->anim_interval == want)
        return;
    disarm_anim(v);
    v->anim_interval = want;
    v->anim_id = ns_loop_timeout_add(NULL, want, anim_tick, v);
    request_render(v);
}

static gboolean
ensure_texture(NsPageView *v, int w, int h)
{
    if (v->texture && v->tex_w == w && v->tex_h == h)
        return TRUE;
    if (v->texture)
        SDL_DestroyTexture(v->texture);
    v->texture = SDL_CreateTexture(v->renderer, SDL_PIXELFORMAT_ARGB8888,
                                   SDL_TEXTUREACCESS_STREAMING, w, h);
    v->tex_w = v->texture ? w : 0;
    v->tex_h = v->texture ? h : 0;
    v->have_frame = FALSE;
    if (!v->texture)
        return FALSE;
    /* Page frames are opaque, and drawn pixel for pixel. */
    SDL_SetTextureBlendMode(v->texture, SDL_BLENDMODE_NONE);
    SDL_SetTextureScaleMode(v->texture, SDL_SCALEMODE_NEAREST);
    return TRUE;
}

static gboolean
apply_patch(NsPageView *v, const Res *res)
{
    if (!v->texture || !v->have_frame || v->frame_gen != res->base_gen)
        return FALSE;
    for (int i = 0; i < res->n_damage; i++) {
        const int *r = res->damage + 4 * i;
        if (r[0] < 0 || r[1] < 0 || r[0] + r[2] > v->tex_w ||
            r[1] + r[3] > v->tex_h)
            return FALSE;
    }
    for (int i = 0; i < res->n_damage; i++) {
        const int *r = res->damage + 4 * i;
        SDL_Rect rect = { r[0], r[1], r[2], r[3] };
        const guint8 *src = res->pixels +
            (gsize)(r[1] - res->px_y) * res->px_stride +
            (gsize)(r[0] - res->px_x) * 4u;
        SDL_UpdateTexture(v->texture, &rect, src, res->px_stride);
    }
    v->frame_gen = res->gen;
    return TRUE;
}

static void
apply_full(NsPageView *v, const Res *res)
{
    if (!ensure_texture(v, res->px_w, res->px_h))
        return;
    SDL_UpdateTexture(v->texture, NULL, res->pixels, res->px_stride);
    v->frame_gen = res->gen;
    v->have_frame = TRUE;
}

/* ---- scrollbars -------------------------------------------------------- */

typedef struct {
    gboolean shown;
    double   track_x, track_y, track_w, track_h;   /* area pixels */
    double   thumb_pos, thumb_len;
} Bar;

static void
bar_geometry(NsPageView *v, gboolean vertical, Bar *b)
{
    memset(b, 0, sizeof *b);
    double thick = NS_VIEW_SCROLLBAR_UI * (v->ui_scale > 0 ? v->ui_scale : 1);
    double vis = vertical ? visible_h(v) : visible_w(v);
    double total = vertical ? v->page_h : v->page_w;
    if (!v->opened || total <= vis + 0.5)
        return;
    b->shown = TRUE;
    if (vertical) {
        b->track_x = area_w(v) - thick;
        b->track_y = 0;
        b->track_w = thick;
        b->track_h = area_h(v) - thick;
    } else {
        b->track_x = 0;
        b->track_y = area_h(v) - thick;
        b->track_w = area_w(v) - thick;
        b->track_h = thick;
    }
    double len = vertical ? b->track_h : b->track_w;
    b->thumb_len = MAX(len * vis / total, thick * 2);
    double range = total - vis;
    double pos = vertical ? v->scroll_y : v->scroll_x;
    b->thumb_pos = range > 0 ? (len - b->thumb_len) * pos / range : 0;
}

static void
draw_bar(NsPageView *v, const Bar *b, gboolean vertical)
{
    if (!b->shown)
        return;
    const NsUiTheme *t = ns_ui_theme();
    SDL_FRect track = { (float)(v->ax + b->track_x), (float)(v->ay + b->track_y),
                        (float)b->track_w, (float)b->track_h };
    SDL_SetRenderDrawBlendMode(v->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(v->renderer, 128, 128, 128, 40);
    SDL_RenderFillRect(v->renderer, &track);
    double pad = b->track_w < b->track_h ? b->track_w * 0.2 : b->track_h * 0.2;
    SDL_FRect thumb;
    if (vertical)
        thumb = (SDL_FRect){ track.x + (float)pad, track.y + (float)b->thumb_pos,
                             track.w - (float)(2 * pad), (float)b->thumb_len };
    else
        thumb = (SDL_FRect){ track.x + (float)b->thumb_pos, track.y + (float)pad,
                             (float)b->thumb_len, track.h - (float)(2 * pad) };
    Uint8 c = t->dark ? 170 : 110;
    SDL_SetRenderDrawColor(v->renderer, c, c, c,
                           v->vbar_drag == (vertical ? 1 : 2) ? 230 : 170);
    SDL_RenderFillRect(v->renderer, &thumb);
}

/* A press on a viewport scrollbar: grabs the thumb or pages. TRUE if hit. */
static gboolean
bar_press(NsPageView *v, double ax, double ay)
{
    for (int i = 0; i < 2; i++) {
        gboolean vertical = i == 0;
        Bar b;
        bar_geometry(v, vertical, &b);
        if (!b.shown || ax < b.track_x || ax >= b.track_x + b.track_w ||
            ay < b.track_y || ay >= b.track_y + b.track_h)
            continue;
        double at = vertical ? ay - b.track_y : ax - b.track_x;
        if (at >= b.thumb_pos && at < b.thumb_pos + b.thumb_len) {
            v->vbar_drag = vertical ? 1 : 2;
            v->vbar_grab = at - b.thumb_pos;
        } else {
            double page = (vertical ? visible_h(v) : visible_w(v)) - NS_VIEW_LINE_PX;
            if (page < NS_VIEW_LINE_PX) page = NS_VIEW_LINE_PX;
            double dir = at < b.thumb_pos ? -1 : 1;
            if (vertical)
                set_scroll(v, v->scroll_x, v->scroll_y + dir * page);
            else
                set_scroll(v, v->scroll_x + dir * page, v->scroll_y);
        }
        return TRUE;
    }
    return FALSE;
}

static void
bar_drag(NsPageView *v, double ax, double ay)
{
    gboolean vertical = v->vbar_drag == 1;
    Bar b;
    bar_geometry(v, vertical, &b);
    if (!b.shown)
        return;
    double len = vertical ? b.track_h : b.track_w;
    double at = (vertical ? ay - b.track_y : ax - b.track_x) - v->vbar_grab;
    double frac = len > b.thumb_len ? at / (len - b.thumb_len) : 0;
    frac = CLAMP(frac, 0.0, 1.0);
    if (vertical)
        set_scroll(v, v->scroll_x, frac * (v->page_h - visible_h(v)));
    else
        set_scroll(v, frac * (v->page_w - visible_w(v)), v->scroll_y);
}

void
ns_page_view_present(NsPageView *v)
{
    if (!v || v->closed)
        return;
    const NsUiTheme *t = ns_ui_theme();
    SDL_FRect area = { (float)v->ax, (float)v->ay, (float)v->aw, (float)v->ah };
    gboolean covers = v->have_frame && v->tex_w >= v->aw && v->tex_h >= v->ah;
    if (!covers) {
        Uint8 c = v->have_frame ? 255 : (Uint8)(t->window.r * 255);
        SDL_SetRenderDrawBlendMode(v->renderer, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(v->renderer, c, c, c, 255);
        SDL_RenderFillRect(v->renderer, &area);
    }
    if (v->have_frame && v->texture) {
        SDL_Rect clip = { v->ax, v->ay, v->aw, v->ah };
        SDL_SetRenderClipRect(v->renderer, &clip);
        SDL_FRect dst = { (float)v->ax, (float)v->ay, (float)v->tex_w,
                          (float)v->tex_h };
        SDL_RenderTexture(v->renderer, v->texture, NULL, &dst);
        SDL_SetRenderClipRect(v->renderer, NULL);
    }
    Bar vb, hb;
    bar_geometry(v, TRUE, &vb);
    bar_geometry(v, FALSE, &hb);
    draw_bar(v, &vb, TRUE);
    draw_bar(v, &hb, FALSE);
}

void
ns_page_view_device_reset(NsPageView *v)
{
    if (v->texture)
        SDL_DestroyTexture(v->texture);
    v->texture = NULL;
    v->tex_w = v->tex_h = 0;
    v->have_frame = FALSE;
    v->frame_force_full = TRUE;
    request_render(v);
}

/* ---- requests ---------------------------------------------------------- */

static void start_link(NsPageView *v, int x, int y, LinkAct action);
static void start_hover(NsPageView *v, int x, int y);
static void start_scroll(NsPageView *v, double dx, double dy);

static void
request_link(NsPageView *v, int x, int y, LinkAct action)
{
    if (!v->opened)
        return;
    if (v->link_inflight) {
        if (action != ACT_HOVER || v->link_pending_action == ACT_HOVER) {
            v->link_pending_x = x;
            v->link_pending_y = y;
            v->link_pending_action = action;
        }
        v->link_pending = TRUE;
        return;
    }
    start_link(v, x, y, action);
}

static void
start_link(NsPageView *v, int x, int y, LinkAct action)
{
    if (!v->opened)
        return;
    v->link_inflight = TRUE;
    Req *req = g_new0(Req, 1);
    req->type = REQ_LINK;
    req->seq = ++v->link_seq;
    req->x = x;
    req->y = y;
    req->action = action;
    push_req(v, req);
}

static void
request_hover(NsPageView *v, int x, int y)
{
    if (!v->opened)
        return;
    if (v->hover_inflight) {
        v->hover_pending_x = x;
        v->hover_pending_y = y;
        v->hover_pending = TRUE;
        return;
    }
    start_hover(v, x, y);
}

static void
start_hover(NsPageView *v, int x, int y)
{
    if (!v->opened)
        return;
    v->hover_inflight = TRUE;
    Req *req = g_new0(Req, 1);
    req->type = REQ_HOVER;
    req->seq = ++v->hover_seq;
    req->x = x;
    req->y = y;
    push_req(v, req);
}

/* kind 0 keydown, 1 keyup, 2 text, 3 typed key (keydown + keypress + text). */
static void
start_key_full(NsPageView *v, int kind, const char *key, const char *code,
               int keycode, int mods, int fallback_scroll, double fallback_x,
               double fallback_y)
{
    if (!v->opened)
        return;
    Req *req = g_new0(Req, 1);
    req->type = REQ_KEY;
    req->seq = (kind == 0 || kind == 2 || kind == 3) ? ++v->key_seq : v->key_seq;
    req->kind = kind;
    req->keycode = keycode;
    req->mods = mods;
    req->fallback_scroll = fallback_scroll;
    req->fallback_x = fallback_x;
    req->fallback_y = fallback_y;
    req->key = g_strdup(key ? key : "");
    req->code = g_strdup(code ? code : "");
    push_req(v, req);
}

static void
start_key_sdl(NsPageView *v, int kind, const SDL_KeyboardEvent *k)
{
    char buf[16];
    start_key_full(v, kind, ns_sdl_dom_key(k->key, buf, sizeof buf),
                   ns_sdl_dom_code(k->scancode),
                   ns_sdl_dom_keycode(k->key, k->scancode),
                   ns_sdl_dom_mods(k->mod), 0, 0, 0);
}

static void
start_select(NsPageView *v, int kind, int x, int y)
{
    if (!v->opened)
        return;
    Req *req = g_new0(Req, 1);
    req->type = REQ_SELECT;
    req->seq = ++v->select_seq;
    req->kind = kind;
    req->x = x;
    req->y = y;
    push_req(v, req);
}

static void
start_click(NsPageView *v, int x, int y, int mods)
{
    if (!v->opened)
        return;
    Req *req = g_new0(Req, 1);
    req->type = REQ_CLICK;
    req->seq = ++v->click_seq;
    req->x = x;
    req->y = y;
    req->mods = mods;
    push_req(v, req);
}

static void
start_release(NsPageView *v, int x, int y)
{
    if (!v->opened)
        return;
    Req *req = g_new0(Req, 1);
    req->type = REQ_RELEASE;
    req->x = x;
    req->y = y;
    push_req(v, req);
}

static void
push_scrollbar(NsPageView *v, int kind, int x, int y)
{
    Req *req = g_new0(Req, 1);
    req->type = REQ_SCROLLBAR;
    req->kind = kind;
    req->x = x;
    req->y = y;
    push_req(v, req);
}

static void
start_viewport(NsPageView *v, int width, int height)
{
    if (!v->opened)
        return;
    if (v->viewport_inflight) {
        v->viewport_pending = TRUE;
        return;
    }
    v->viewport_inflight = TRUE;
    Req *req = g_new0(Req, 1);
    req->type = REQ_VIEWPORT;
    req->seq = ++v->viewport_seq;
    req->vw = layout_px(v, width);
    req->vh = layout_px(v, height);
    push_req(v, req);
}

static gboolean
maybe_update_viewport(NsPageView *v)
{
    if (!v->opened)
        return FALSE;
    int w = area_w(v), h = area_h(v);
    if (w <= 1 || h <= 1)
        return FALSE;
    if (w == v->last_vp_w && h == v->last_vp_h)
        return FALSE;
    v->last_vp_w = w;
    v->last_vp_h = h;
    start_viewport(v, w, h);
    return TRUE;
}

static void
start_scroll(NsPageView *v, double dx, double dy)
{
    if (v->scroll_inflight) {
        v->scroll_pending = TRUE;
        v->scroll_pending_dx += dx;
        v->scroll_pending_dy += dy;
        return;
    }
    v->scroll_inflight = TRUE;
    double s = cur_scale(v);
    Req *req = g_new0(Req, 1);
    req->type = REQ_SCROLL;
    req->seq = v->scroll_seq;
    req->x = page_x(v, v->pointer_x);
    req->y = page_y(v, v->pointer_y);
    req->dx = (int)(dx * NS_VIEW_LINE_PX / s * (v->ui_scale > 0 ? v->ui_scale : 1));
    req->dy = (int)(dy * NS_VIEW_LINE_PX / s * (v->ui_scale > 0 ? v->ui_scale : 1));
    req->fallback_x = dx;
    req->fallback_y = dy;
    push_req(v, req);
}

/* ---- history and navigation -------------------------------------------- */

static void
push_history(NsPageView *v, const char *url)
{
    if (!url || !*url)
        return;
    if (v->hist_index >= 0 &&
        g_strcmp0(g_ptr_array_index(v->history, v->hist_index), url) == 0)
        return;
    while ((int)v->history->len > v->hist_index + 1)
        g_ptr_array_remove_index(v->history, v->history->len - 1);
    g_ptr_array_add(v->history, g_strdup(url));
    v->hist_index = (int)v->history->len - 1;
    emit(v, NS_VIEW_EVT_HISTORY, NULL);
}

static void
replace_history(NsPageView *v, const char *url)
{
    if (!url || !*url || v->hist_index < 0 ||
        v->hist_index >= (int)v->history->len)
        return;
    g_free(g_ptr_array_index(v->history, v->hist_index));
    g_ptr_array_index(v->history, v->hist_index) = g_strdup(url);
}

static void
finish_loading(NsPageView *v)
{
    if (v->loading) {
        v->loading = FALSE;
        emit(v, NS_VIEW_EVT_LOADING, "0");
    }
}

static void
do_load(NsPageView *v, const char *url, gboolean record, gboolean history,
        gboolean user_activated)
{
    if (!url || !*url || v->closed)
        return;
    v->pending_record = record;
    int seq = ++v->load_seq;
    ++v->render_seq;
    ++v->link_seq;
    ++v->click_seq;
    ++v->viewport_seq;
    ++v->key_seq;
    ++v->select_seq;
    ++v->hover_seq;
    ++v->scroll_seq;
    ++v->find_seq;
    v->render_pending = v->render_inflight = FALSE;
    v->viewport_inflight = v->viewport_pending = FALSE;
    v->scroll_inflight = v->scroll_pending = FALSE;
    v->scroll_pending_dx = v->scroll_pending_dy = 0.0;
    v->link_inflight = v->link_pending = FALSE;
    v->link_pending_action = ACT_HOVER;
    v->hover_inflight = v->hover_pending = FALSE;
    v->has_selection = FALSE;
    v->typed_pending = FALSE;
    v->opened = FALSE;
    v->page_animating = v->caret_blinking = FALSE;
    disarm_anim(v);
    v->have_frame = FALSE;
    emit(v, NS_VIEW_EVT_REDRAW, NULL);
    if (!v->loading) {
        v->loading = TRUE;
        emit(v, NS_VIEW_EVT_LOADING, "1");
    }
    emit(v, NS_VIEW_EVT_STATUS, ns_i18n("Loading…"));

    if (v->aw <= 1 || v->ah <= 1) {
        g_free(v->deferred_url);
        v->deferred_url = g_strdup(url);
        v->deferred_record = record;
        v->deferred_history = history;
        v->deferred_activated = user_activated;
        return;
    }
    v->last_vp_w = v->aw;
    v->last_vp_h = v->ah;

    Req *req = g_new0(Req, 1);
    req->type = REQ_LOAD;
    req->seq = seq;
    req->url = g_strdup(url);
    req->vw = layout_px(v, v->aw);
    req->vh = layout_px(v, v->ah);
    req->history = history;
    req->user_activated = user_activated;
    push_req(v, req);
}

void
ns_page_view_load(NsPageView *v, const char *url)
{
    do_load(v, url, TRUE, FALSE, TRUE);
}

gboolean ns_page_view_can_back(NsPageView *v) { return v->hist_index > 0; }

gboolean
ns_page_view_can_forward(NsPageView *v)
{
    return v->hist_index >= 0 && v->hist_index < (int)v->history->len - 1;
}

void
ns_page_view_back(NsPageView *v)
{
    if (!ns_page_view_can_back(v))
        return;
    v->hist_index--;
    emit(v, NS_VIEW_EVT_HISTORY, NULL);
    do_load(v, g_ptr_array_index(v->history, v->hist_index), FALSE, TRUE, TRUE);
}

void
ns_page_view_forward(NsPageView *v)
{
    if (!ns_page_view_can_forward(v))
        return;
    v->hist_index++;
    emit(v, NS_VIEW_EVT_HISTORY, NULL);
    do_load(v, g_ptr_array_index(v->history, v->hist_index), FALSE, TRUE, TRUE);
}

void
ns_page_view_reload(NsPageView *v)
{
    if (v->hist_index >= 0 && v->hist_index < (int)v->history->len)
        do_load(v, g_ptr_array_index(v->history, v->hist_index), FALSE, FALSE,
                TRUE);
    else if (v->current_url)
        do_load(v, v->current_url, FALSE, FALSE, TRUE);
}

void
ns_page_view_stop(NsPageView *v)
{
    if (!v)
        return;
    v->render_seq++;
    v->render_inflight = v->render_pending = FALSE;
    finish_loading(v);
}

const char *ns_page_view_url(NsPageView *v) { return v->current_url; }
const char *ns_page_view_title(NsPageView *v) { return v->current_title; }
gboolean ns_page_view_is_loading(NsPageView *v) { return v->loading; }
int ns_page_view_security(NsPageView *v) { return v ? v->security : 0; }

/* ---- zoom -------------------------------------------------------------- */

static void
set_zoom(NsPageView *v, double scale)
{
    int permille = (int)(scale * 1000.0 + 0.5);
    permille = CLAMP(permille, NS_PV_ZOOM_MIN_PERMILLE, NS_PV_ZOOM_MAX_PERMILLE);
    double clamped = permille / 1000.0;
    if (clamped == cur_zoom(v))
        return;
    v->zoom = clamped;
    char percent[16];
    g_snprintf(percent, sizeof percent, "%d", permille / 10);
    emit(v, NS_VIEW_EVT_ZOOM, percent);
    if (v->opened) {
        clamp_scroll(v);
        request_render(v);
    }
}

static const int k_zoom_ladder[] = {
    25, 33, 50, 67, 75, 80, 90, 100, 110, 125, 150, 175, 200, 250, 300, 400, 500
};

static void
zoom_step(NsPageView *v, int direction)
{
    int now = (int)(cur_zoom(v) * 100.0 + 0.5);
    gsize n = G_N_ELEMENTS(k_zoom_ladder);
    if (direction > 0) {
        for (gsize i = 0; i < n; i++)
            if (k_zoom_ladder[i] > now) {
                set_zoom(v, k_zoom_ladder[i] / 100.0);
                return;
            }
        set_zoom(v, k_zoom_ladder[n - 1] / 100.0);
        return;
    }
    for (gsize i = n; i-- > 0; )
        if (k_zoom_ladder[i] < now) {
            set_zoom(v, k_zoom_ladder[i] / 100.0);
            return;
        }
    set_zoom(v, k_zoom_ladder[0] / 100.0);
}

void ns_page_view_zoom_in(NsPageView *v)    { zoom_step(v, 1); }
void ns_page_view_zoom_out(NsPageView *v)   { zoom_step(v, -1); }
void ns_page_view_zoom_reset(NsPageView *v) { set_zoom(v, 1.0); }

int
ns_page_view_zoom_percent(NsPageView *v)
{
    return v ? (int)(cur_zoom(v) * 100.0 + 0.5) : 100;
}

/* ---- results ----------------------------------------------------------- */

static void
res_free(Res *res)
{
    g_free(res->pixels);
    free(res->damage);
    g_free(res->title);
    g_free(res->url);
    g_free(res->nav);
    g_free(res->download);
    free(res->clipboard);
    free(res->href);
    free(res->cursor);
    pv_unref(res->view);
    g_free(res);
}

static void
on_frame(NsPageView *v, Res *res)
{
    gboolean current = res->seq == v->render_seq;
    if (current && res->ok && v->opened && !v->loading) {
        if (res->url && *res->url && g_strcmp0(res->url, v->current_url) != 0) {
            g_free(v->current_url);
            v->current_url = g_strdup(res->url);
            if (res->url_pushed)
                push_history(v, v->current_url);
            else
                replace_history(v, v->current_url);
            emit(v, NS_VIEW_EVT_URL, v->current_url);
        }
        if (res->title && g_strcmp0(res->title, v->current_title) != 0) {
            g_free(v->current_title);
            v->current_title = g_strdup(res->title);
            emit(v, NS_VIEW_EVT_TITLE, v->current_title);
        }
    }
    if (current && res->ok) {
        gboolean was = v->page_animating || v->caret_blinking;
        v->page_animating = res->animating;
        v->caret_blinking = res->caret_blinking;
        if (v->page_animating || v->caret_blinking)
            arm_anim(v);
        else if (was)
            disarm_anim(v);
        if (res->ph > 0) v->page_h = res->ph;
        if (res->pw > 0) v->page_w = res->pw;
        if (res->requested_scroll_y >= 0)
            v->scroll_y = res->requested_scroll_y;
        if (res->requested_scroll_x >= 0)
            v->scroll_x = res->requested_scroll_x;
        clamp_scroll(v);
    }
    if (current && res->ok && res->pixels && !res->is_patch) {
        apply_full(v, res);
        emit(v, NS_VIEW_EVT_REDRAW, NULL);
    } else if (res->ok && res->pixels && res->is_patch) {
        if (apply_patch(v, res)) {
            emit(v, NS_VIEW_EVT_REDRAW, NULL);
        } else {
            v->frame_force_full = TRUE;
            request_render(v);
        }
    }
    if (current && res->ok && res->nav && *res->nav &&
        v->js_redirects < NS_PROC_MAX_JS_REDIRECTS) {
        v->js_redirects++;
        do_load(v, res->nav, FALSE, FALSE, FALSE);
    }
    if (current && res->ok && res->download && *res->download)
        emit(v, NS_VIEW_EVT_DOWNLOAD, res->download);
    if (current && res->ok && res->clipboard) {
        SDL_SetClipboardText(res->clipboard);
        emit(v, NS_VIEW_EVT_STATUS, ns_i18n("Copied to clipboard"));
    }
    v->render_inflight = FALSE;
    if (v->render_pending) {
        v->render_pending = FALSE;
        start_render(v);
    } else if (current && !res->ok) {
        finish_loading(v);
    }
}

static void
on_page(NsPageView *v, Res *res)
{
    if (res->seq != v->load_seq)
        return;
    if (!res->ok) {
        emit(v, NS_VIEW_EVT_STATUS, ns_i18n("Failed to load page"));
        finish_loading(v);
        return;
    }
    if (res->nav && *res->nav) {
        if (v->js_redirects < NS_PROC_MAX_JS_REDIRECTS) {
            v->js_redirects++;
            do_load(v, res->nav, v->pending_record, FALSE, FALSE);
            return;
        }
        emit(v, NS_VIEW_EVT_STATUS, ns_i18n("Stopped after too many redirects"));
    }
    v->js_redirects = 0;
    g_free(v->current_url);
    v->current_url = g_strdup(res->url);
    g_free(v->current_title);
    v->current_title = g_strdup(res->title);
    v->security = res->security;
    v->page_w = res->pw;
    v->page_h = res->ph;
    v->scroll_x = v->scroll_y = 0;
    v->opened = TRUE;
    if (v->pending_record)
        push_history(v, v->current_url);
    emit(v, NS_VIEW_EVT_URL, v->current_url);
    emit(v, NS_VIEW_EVT_TITLE, v->current_title);
    emit(v, NS_VIEW_EVT_STATUS, "");
    finish_loading(v);
    maybe_update_viewport(v);
    request_render(v);
}

static void
on_link(NsPageView *v, Res *res)
{
    if (res->seq != v->link_seq)
        return;
    v->link_inflight = FALSE;
    gboolean navigated = FALSE;
    if (res->action == ACT_CONTEXT) {
        if (res->prevented)
            request_render(v);
        else
            emit(v, NS_VIEW_EVT_CONTEXT_MENU, res->href);
    } else if (res->href && *res->href) {
        emit(v, NS_VIEW_EVT_STATUS, res->href);
        emit(v, NS_VIEW_EVT_CURSOR, res->cursor ? res->cursor : "pointer");
        if (res->action == ACT_NAVIGATE) {
            navigated = TRUE;
            ns_page_view_load(v, res->href);
        }
    } else {
        emit(v, NS_VIEW_EVT_STATUS, "");
        emit(v, NS_VIEW_EVT_CURSOR, res->cursor);
    }
    if (!navigated && v->link_pending) {
        v->link_pending = FALSE;
        LinkAct a = v->link_pending_action;
        v->link_pending_action = ACT_HOVER;
        start_link(v, v->link_pending_x, v->link_pending_y, a);
    }
}

static void
on_scrollbar(NsPageView *v, Res *res)
{
    if (res->kind == 0) {
        gboolean still = v->sb_probe;
        v->sb_probe = FALSE;
        if (res->ok) {
            request_render(v);
            if (still) {
                v->sb_dragging = TRUE;
                if (v->sb_have_last)
                    push_scrollbar(v, 1, page_x(v, v->sb_last_x),
                                   page_y(v, v->sb_last_y));
            }
        } else if (still && v->sb_have_last) {
            if (!v->multi_click) {
                start_select(v, 0, page_x(v, v->drag_start_x),
                             page_y(v, v->drag_start_y));
                v->drag_anchored = TRUE;
            }
            start_select(v, 1, page_x(v, v->sb_last_x), page_y(v, v->sb_last_y));
            v->has_selection = TRUE;
        }
    } else if (res->kind == 1 && res->ok) {
        request_render(v);
    }
}

static gboolean
on_result(gpointer data)
{
    Res *res = data;
    NsPageView *v = res->view;
    if (v->closed)
        goto done;
    switch (res->type) {
    case RES_PAGE:
        on_page(v, res);
        break;
    case RES_FRAME:
        on_frame(v, res);
        break;
    case RES_VIEWPORT:
        if (res->seq != v->viewport_seq)
            break;
        v->viewport_inflight = FALSE;
        if (res->ok) {
            v->page_w = res->pw;
            v->page_h = res->ph;
            clamp_scroll(v);
            request_render(v);
        }
        if (v->viewport_pending) {
            v->viewport_pending = FALSE;
            start_viewport(v, v->last_vp_w, v->last_vp_h);
        }
        break;
    case RES_SELECT:
        if (res->seq == v->select_seq)
            request_render(v);
        break;
    case RES_COPY:
        if (res->href && *res->href) {
            SDL_SetClipboardText(res->href);
            emit(v, NS_VIEW_EVT_STATUS, ns_i18n("Copied selection"));
        }
        break;
    case RES_KEY:
        if (res->seq != v->key_seq)
            break;
        if ((res->kind == 0 || res->kind == 3) && res->href && *res->href) {
            emit(v, NS_VIEW_EVT_STATUS, res->href);
            ns_page_view_load(v, res->href);
        } else {
            if (res->fallback_scroll && !res->prevented)
                set_scroll(v, res->fallback_x, res->fallback_y);
            request_render(v);
        }
        break;
    case RES_CLICK:
        if (res->seq != v->click_seq)
            break;
        if (res->href && *res->href) {
            emit(v, NS_VIEW_EVT_STATUS, res->href);
            ns_page_view_load(v, res->href);
        } else {
            request_render(v);
        }
        break;
    case RES_LINK:
        on_link(v, res);
        break;
    case RES_HOVER:
        if (res->seq != v->hover_seq)
            break;
        v->hover_inflight = FALSE;
        if (res->href && *res->href) {
            emit(v, NS_VIEW_EVT_STATUS, res->href);
            emit(v, NS_VIEW_EVT_CURSOR, res->cursor ? res->cursor : "pointer");
        } else {
            emit(v, NS_VIEW_EVT_STATUS, "");
            emit(v, NS_VIEW_EVT_CURSOR, res->cursor);
        }
        if (res->ok)
            request_render(v);
        if (v->hover_pending) {
            v->hover_pending = FALSE;
            start_hover(v, v->hover_pending_x, v->hover_pending_y);
        }
        break;
    case RES_DROPFILES:
        if (res->ok)
            request_render(v);
        break;
    case RES_SCROLL:
        if (res->seq != v->scroll_seq)
            break;
        v->scroll_inflight = FALSE;
        if (res->ok) {
            request_render(v);
        } else {
            double s = v->ui_scale > 0 ? v->ui_scale : 1;
            set_scroll(v,
                       v->scroll_x + res->fallback_x * NS_VIEW_LINE_PX * s / cur_scale(v),
                       v->scroll_y + res->fallback_y * NS_VIEW_LINE_PX * s / cur_scale(v));
        }
        if (v->scroll_pending && v->opened) {
            double dx = v->scroll_pending_dx, dy = v->scroll_pending_dy;
            v->scroll_pending = FALSE;
            v->scroll_pending_dx = v->scroll_pending_dy = 0.0;
            start_scroll(v, dx, dy);
        }
        break;
    case RES_SCROLLBAR:
        on_scrollbar(v, res);
        break;
    case RES_RELEASE:
        if (res->href && *res->href) {
            emit(v, NS_VIEW_EVT_STATUS, res->href);
            ns_page_view_load(v, res->href);
        } else if (res->ok) {
            request_render(v);
        }
        break;
    case RES_FIND: {
        if (res->seq != v->find_seq)
            break;
        char buf[64];
        if (res->find_total > 0)
            g_snprintf(buf, sizeof buf, "%d/%d", res->find_current,
                       res->find_total);
        else
            g_strlcpy(buf, "none", sizeof buf);
        emit(v, NS_VIEW_EVT_FIND, buf);
        if (res->find_total > 0)
            set_scroll(v, v->scroll_x,
                       res->find_scroll_y > 40 ? res->find_scroll_y - 40 : 0);
        request_render(v);
        break;
    }
    case RES_EXPORT:
        emit(v, NS_VIEW_EVT_STATUS,
             res->ok && res->url ? res->url : ns_i18n("Could not save page"));
        break;
    }
done:
    res_free(res);
    return NS_SOURCE_REMOVE;
}

/* ---- public: area, input ----------------------------------------------- */

void
ns_page_view_set_area(NsPageView *v, int x, int y, int w, int h,
                      double ui_scale)
{
    gboolean resized = w != v->aw || h != v->ah || ui_scale != v->ui_scale;
    v->ax = x;
    v->ay = y;
    v->aw = w;
    v->ah = h;
    v->ui_scale = ui_scale;
    if (!resized)
        return;
    if (v->deferred_url && w > 1 && h > 1) {
        char *u = v->deferred_url;
        v->deferred_url = NULL;
        do_load(v, u, v->deferred_record, v->deferred_history,
                v->deferred_activated);
        g_free(u);
        return;
    }
    if (v->opened) {
        if (maybe_update_viewport(v))
            return;
        clamp_scroll(v);
        request_render(v);
    }
}

static gboolean
in_area(NsPageView *v, float x, float y)
{
    return x >= v->ax && y >= v->ay && x < v->ax + v->aw && y < v->ay + v->ah;
}

void
ns_page_view_mouse_down(NsPageView *v, float wx, float wy, int button,
                        int clicks, SDL_Keymod mod)
{
    if (!v->opened || !in_area(v, wx, wy))
        return;
    double x = wx - v->ax, y = wy - v->ay;
    v->pointer_x = x;
    v->pointer_y = y;
    int px = page_x(v, x), py = page_y(v, y);
    if (button == SDL_BUTTON_RIGHT) {
        request_link(v, px, py, ACT_CONTEXT);
        return;
    }
    if (button == SDL_BUTTON_LEFT && bar_press(v, x, y))
        return;
    if (!v->focused)
        ns_page_view_set_focus(v, TRUE);
    if (button == SDL_BUTTON_MIDDLE ||
        (button == SDL_BUTTON_LEFT && (mod & SDL_KMOD_CTRL))) {
        request_link(v, px, py, ACT_NAVIGATE);
        return;
    }
    if (button != SDL_BUTTON_LEFT)
        return;
    v->left_down = TRUE;
    if (!(mod & SDL_KMOD_SHIFT))
        v->has_selection = FALSE;
    start_click(v, px, py, ns_sdl_dom_mods(mod));
    v->multi_click = clicks >= 2;
    if (mod & SDL_KMOD_SHIFT) {
        start_select(v, 1, px, py);
        v->has_selection = TRUE;
    } else if (clicks == 2) {
        start_select(v, 5, px, py);
        v->has_selection = TRUE;
    } else if (clicks >= 3) {
        start_select(v, 6, px, py);
        v->has_selection = TRUE;
    }
    /* A drag may start here: ask whether the press hit an element's
     * scrollbar before treating motion as a text selection. */
    v->drag_start_x = x;
    v->drag_start_y = y;
    v->drag_anchored = FALSE;
    v->sb_dragging = FALSE;
    v->sb_have_last = FALSE;
    v->sb_probe = TRUE;
    push_scrollbar(v, 0, px, py);
}

void
ns_page_view_mouse_up(NsPageView *v, float wx, float wy, int button)
{
    if (button != SDL_BUTTON_LEFT)
        return;
    if (v->vbar_drag) {
        v->vbar_drag = 0;
        emit(v, NS_VIEW_EVT_REDRAW, NULL);
        return;
    }
    if (!v->left_down)
        return;
    v->left_down = FALSE;
    if (v->sb_dragging || v->sb_probe) {
        push_scrollbar(v, 2, 0, 0);
        v->sb_dragging = v->sb_probe = v->sb_have_last = FALSE;
    }
    if (v->opened) {
        double x = wx - v->ax, y = wy - v->ay;
        int px = page_x(v, x), py = page_y(v, y);
        start_release(v, px, py);
        request_link(v, px, py, ACT_HOVER);
    }
}

void
ns_page_view_mouse_move(NsPageView *v, float wx, float wy, gboolean left_held)
{
    double x = wx - v->ax, y = wy - v->ay;
    if (v->vbar_drag) {
        bar_drag(v, x, y);
        return;
    }
    v->pointer_x = x;
    v->pointer_y = y;
    if (!v->opened)
        return;
    if (v->left_down && left_held) {
        if (v->sb_dragging) {
            push_scrollbar(v, 1, page_x(v, x), page_y(v, y));
            return;
        }
        if (v->sb_probe) {
            v->sb_last_x = x;
            v->sb_last_y = y;
            v->sb_have_last = TRUE;
            return;
        }
        if (v->multi_click) {
            start_select(v, 1, page_x(v, x), page_y(v, y));
            return;
        }
        if (!v->drag_anchored) {
            start_select(v, 0, page_x(v, v->drag_start_x),
                         page_y(v, v->drag_start_y));
            v->drag_anchored = TRUE;
        }
        start_select(v, 1, page_x(v, x), page_y(v, y));
        v->has_selection = TRUE;
        return;
    }
    if (in_area(v, wx, wy))
        request_hover(v, page_x(v, x), page_y(v, y));
}

void
ns_page_view_mouse_leave(NsPageView *v)
{
    emit(v, NS_VIEW_EVT_STATUS, "");
}

void
ns_page_view_wheel(NsPageView *v, float dx, float dy, SDL_Keymod mod)
{
    if (!v->opened)
        return;
    if (mod & SDL_KMOD_CTRL) {
        double delta = dy != 0.0f ? dy : dx;
        if (delta > 0)
            ns_page_view_zoom_in(v);
        else if (delta < 0)
            ns_page_view_zoom_out(v);
        return;
    }
    /* SDL's wheel is positive away from the user; scrolling is the other way. */
    start_scroll(v, dx, -dy);
}

void
ns_page_view_set_focus(NsPageView *v, gboolean focused)
{
    if (v->focused == focused)
        return;
    v->focused = focused;
    v->typed_pending = FALSE;
    request_render(v);
}

gboolean
ns_page_view_has_focus(NsPageView *v)
{
    return v->focused;
}

void
ns_page_view_drop_files(NsPageView *v, float wx, float wy, char *paths)
{
    if (!v->opened || !paths || !*paths) {
        g_free(paths);
        return;
    }
    Req *req = g_new0(Req, 1);
    req->type = REQ_DROPFILES;
    req->x = page_x(v, wx - v->ax);
    req->y = page_y(v, wy - v->ay);
    req->paths = paths;
    push_req(v, req);
}

/* Whether the modifiers make a typed character: none, or AltGr. */
static gboolean
mods_allow_text(SDL_Keymod mod)
{
    if (mod & SDL_KMOD_GUI)
        return FALSE;
    gboolean altgr = (mod & SDL_KMOD_MODE) ||
                     ((mod & SDL_KMOD_RALT) && !(mod & SDL_KMOD_LALT));
    if (altgr)
        return TRUE;
    return !(mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT));
}

static double
page_step(NsPageView *v)
{
    double page = visible_h(v) - NS_VIEW_LINE_PX;
    return page < NS_VIEW_LINE_PX ? NS_VIEW_LINE_PX : page;
}

gboolean
ns_page_view_key_down(NsPageView *v, const SDL_KeyboardEvent *k)
{
    if (!v->opened)
        return FALSE;
    SDL_Keymod mod = k->mod;
    int mods = ns_sdl_dom_mods(mod);
    if (ns_sdl_key_types_text(k->key) && mods_allow_text(mod)) {
        /* The character comes in the text-input event that follows. */
        v->typed_pending = TRUE;
        v->typed_key = k->key;
        v->typed_scancode = k->scancode;
        v->typed_mod = mod;
        return TRUE;
    }
    if (mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) {
        start_key_sdl(v, 0, k);
        switch (k->key) {
        case SDLK_C:
            start_select(v, 4, 0, 0);
            return TRUE;
        case SDLK_V: {
            char *text = SDL_GetClipboardText();
            if (text && *text)
                start_key_full(v, 2, text, "", 0, 0, 0, 0, 0);
            SDL_free(text);
            return TRUE;
        }
        case SDLK_A:
            v->has_selection = TRUE;
            start_select(v, 3, 0, 0);
            return TRUE;
        case SDLK_PLUS:
        case SDLK_EQUALS:
        case SDLK_KP_PLUS:
            ns_page_view_zoom_in(v);
            return TRUE;
        case SDLK_MINUS:
        case SDLK_KP_MINUS:
            ns_page_view_zoom_out(v);
            return TRUE;
        case SDLK_0:
        case SDLK_KP_0:
            ns_page_view_zoom_reset(v);
            return TRUE;
        default:
            return FALSE;
        }
    }
    if ((mod & SDL_KMOD_ALT) && ns_sdl_key_types_text(k->key)) {
        start_key_sdl(v, 0, k);   /* accesskey */
        return TRUE;
    }
    double tx = v->scroll_x, ty = v->scroll_y;
    switch (k->key) {
    case SDLK_TAB:
        start_key_sdl(v, 0, k);
        return TRUE;
    case SDLK_RETURN:
    case SDLK_KP_ENTER: {
        char buf[16];
        start_key_full(v, 3, ns_sdl_dom_key(k->key, buf, sizeof buf),
                       ns_sdl_dom_code(k->scancode), 13, mods, 0, 0, 0);
        return TRUE;
    }
    case SDLK_DOWN:     ty += NS_VIEW_LINE_PX; break;
    case SDLK_UP:       ty -= NS_VIEW_LINE_PX; break;
    case SDLK_RIGHT:    tx += NS_VIEW_LINE_PX; break;
    case SDLK_LEFT:     tx -= NS_VIEW_LINE_PX; break;
    case SDLK_PAGEDOWN: ty += page_step(v); break;
    case SDLK_PAGEUP:   ty -= page_step(v); break;
    case SDLK_HOME:     ty = 0; break;
    case SDLK_END:      ty = v->page_h; break;
    default:
        start_key_sdl(v, 0, k);
        return FALSE;
    }
    char buf[16];
    start_key_full(v, 0, ns_sdl_dom_key(k->key, buf, sizeof buf),
                   ns_sdl_dom_code(k->scancode),
                   ns_sdl_dom_keycode(k->key, k->scancode), mods, 1, tx, ty);
    return TRUE;
}

void
ns_page_view_key_up(NsPageView *v, const SDL_KeyboardEvent *k)
{
    if (v->opened)
        start_key_sdl(v, 1, k);
}

void
ns_page_view_text_input(NsPageView *v, const char *text)
{
    if (!v->opened || !text || !*text)
        return;
    if (v->typed_pending && g_utf8_strlen(text, -1) == 1) {
        v->typed_pending = FALSE;
        int mods = ns_sdl_dom_mods(v->typed_mod) & 1;   /* Shift only */
        const char *code = ns_sdl_dom_code(v->typed_scancode);
        int keycode = ns_sdl_dom_keycode(v->typed_key, v->typed_scancode);
        if (v->typed_key == SDLK_SPACE) {
            /* Space pages down when nothing on the page takes it. */
            double ty = v->scroll_y + ((mods & 1) ? -page_step(v) : page_step(v));
            start_key_full(v, 3, text, code, keycode, mods, 1, v->scroll_x, ty);
            return;
        }
        start_key_full(v, 3, text, code, keycode, mods, 0, 0, 0);
        return;
    }
    v->typed_pending = FALSE;
    start_key_full(v, 2, text, "", 0, 0, 0, 0, 0);
}

/* ---- find, selection, saving ------------------------------------------- */

void
ns_page_view_find(NsPageView *v, const char *query, int direction,
                  gboolean case_sensitive)
{
    if (!v->opened)
        return;
    Req *req = g_new0(Req, 1);
    req->type = REQ_FIND;
    req->seq = ++v->find_seq;
    req->query = g_strdup(query ? query : "");
    req->find_dir = direction;
    req->find_from_y = v->scroll_y;
    req->find_case = case_sensitive;
    push_req(v, req);
}

void
ns_page_view_select_all(NsPageView *v)
{
    v->has_selection = TRUE;
    start_select(v, 3, 0, 0);
}

void
ns_page_view_copy_selection(NsPageView *v)
{
    start_select(v, 4, 0, 0);
}

gboolean
ns_page_view_has_selection(NsPageView *v)
{
    return v->has_selection;
}

void
ns_page_view_save_image(NsPageView *v, const char *dest)
{
    if (!v->opened || !dest)
        return;
    static int counter;
    char *base = g_strdup_printf("weststar-export-%" G_GINT64_FORMAT "-%d.png",
                                 ns_time_monotonic_us(), ++counter);
    Req *req = g_new0(Req, 1);
    req->type = REQ_EXPORT;
    req->url = g_build_filename(ns_fs_user_runtime_dir(), base, NULL);
    req->export_dest = g_strdup(dest);
    push_req(v, req);
    g_free(base);
}

typedef struct {
    NsPageView *view;
    char       *url;
    char       *dest;
    gboolean    ok;
} SavePage;

static gboolean
save_page_url_allowed(const char *url)
{
    static const char *const ok[] = { "http://", "https://", "data:", NULL };
    for (gsize i = 0; url && ok[i]; i++)
        if (g_ascii_strncasecmp(url, ok[i], strlen(ok[i])) == 0)
            return TRUE;
    return FALSE;
}

static gboolean
save_page_done(gpointer data)
{
    SavePage *c = data;
    emit(c->view, NS_VIEW_EVT_STATUS,
         c->ok ? c->dest : ns_i18n("Could not save page"));
    pv_unref(c->view);
    g_free(c->url);
    g_free(c->dest);
    g_free(c);
    return NS_SOURCE_REMOVE;
}

static gpointer
save_page_worker(gpointer data)
{
    SavePage *c = data;
    GError *err = NULL;
    ns_response *resp = ns_net_fetch_blocking(c->url, NULL, &err);
    if (resp && resp->final_url && !save_page_url_allowed(resp->final_url)) {
        ns_response_free(resp);
        resp = NULL;
    }
    if (resp && !resp->error && resp->body &&
        ns_fs_set_contents(c->dest, (const char *)resp->body->data,
                           (gssize)resp->body->len)) {
        c->ok = TRUE;
        ns_security_mark_download_origin(c->dest,
                                         resp->final_url ? resp->final_url : c->url);
    }
    if (resp)
        ns_response_free(resp);
    g_clear_error(&err);
    ns_sdl_idle_add(save_page_done, c);
    return NULL;
}

static const char *
save_page_source(NsPageView *v)
{
    const char *url = v->current_url;
    if (url && g_ascii_strncasecmp(url, "view-source:", 12) == 0)
        url += 12;
    return url;
}

void
ns_page_view_save_page(NsPageView *v, const char *dest)
{
    const char *url = save_page_source(v);
    if (!dest || !save_page_url_allowed(url)) {
        emit(v, NS_VIEW_EVT_STATUS, ns_i18n("Could not save page"));
        return;
    }
    SavePage *c = g_new0(SavePage, 1);
    c->view = pv_ref(v);
    c->url = g_strdup(url);
    c->dest = g_strdup(dest);
    NsThread *t = ns_thread_new("ns-save-page", save_page_worker, c);
    if (t)
        ns_thread_unref(t);
}

static char *
title_file_name(NsPageView *v, const char *ext)
{
    const char *t = (v->current_title && *v->current_title)
        ? v->current_title : "page";
    char *name = g_strconcat(t, ext, NULL);
    g_strdelimit(name, "/\\:*?\"<>|", '_');
    return name;
}

char *
ns_page_view_save_image_name(NsPageView *v)
{
    return title_file_name(v, ".png");
}

/* The URL's own name when it has a non-page extension (report.pdf), else the title + .html. */
char *
ns_page_view_save_page_name(NsPageView *v)
{
    static const char *const page_exts[] = {
        ".html", ".htm", ".xhtml", ".php", ".asp", ".aspx", ".jsp", ".cgi", NULL,
    };
    const char *url = save_page_source(v);
    char *name = NULL;
    if (url && g_ascii_strncasecmp(url, "data:", 5) != 0) {
        char *path = g_strdup(url);
        char *q = strpbrk(path, "?#");
        if (q)
            *q = '\0';
        const char *slash = strrchr(path, '/');
        const char *base = slash ? slash + 1 : path;
        const char *dot = strrchr(base, '.');
        gboolean host_only = slash && slash > path && slash[-1] == '/';
        if (*base && dot && dot != base && !host_only) {
            gboolean is_page = FALSE;
            for (gsize i = 0; page_exts[i]; i++)
                if (g_ascii_strcasecmp(dot, page_exts[i]) == 0)
                    is_page = TRUE;
            if (!is_page)
                name = g_uri_unescape_string(base, NULL);
        }
        g_free(path);
    }
    if (!name || !*name) {
        g_free(name);
        return title_file_name(v, ".html");
    }
    g_strdelimit(name, "/\\:*?\"<>|", '_');
    return name;
}

/* ---- lifetime ---------------------------------------------------------- */

NsPageView *
ns_page_view_new(SDL_Renderer *renderer)
{
    NsPageView *v = g_new0(NsPageView, 1);
    g_atomic_ref_count_init(&v->rc);
    v->renderer = renderer;
    v->history = g_ptr_array_new_with_free_func(g_free);
    v->hist_index = -1;
    v->link_pending_action = ACT_HOVER;
    v->pending_record = TRUE;
    v->zoom = 1.0;
    v->ui_scale = 1.0;
    return v;
}

void
ns_page_view_set_notify(NsPageView *v, NsViewNotify cb, gpointer ud)
{
    v->notify = cb;
    v->notify_ud = ud;
}

void
ns_page_view_set_private(NsPageView *v, gboolean private_mode)
{
    if (v)
        v->private_mode = private_mode;
}

gboolean
ns_page_view_is_private(NsPageView *v)
{
    return v && v->private_mode;
}

void
ns_page_view_close(NsPageView *v)
{
    if (!v || v->closed)
        return;
    disarm_anim(v);
    Req *req = g_new0(Req, 1);
    req->type = REQ_CLOSE;
    push_req(v, req);
    v->closed = TRUE;
    v->notify = NULL;
    if (v->texture) {
        SDL_DestroyTexture(v->texture);
        v->texture = NULL;
    }
    v->renderer = NULL;
    pv_unref(v);
}
