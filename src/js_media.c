/* Weststar SDL Browser — HTMLMediaElement: load, play and pause state machine for audio.
 * Drives the audio mixer, media events and play promises. A <video> never finds a supported
 * source: Weststar decodes no video.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "js_internal.h"
#include "plat/ns_plat.h"

#include <math.h>
#include <string.h>

#include "audio/audio.h"
#include "config.h"
#include "mainctx.h"
#include "media_types.h"
#include "net.h"

#define NS_MEDIA_POLL_MS 50
#define NS_MEDIA_TIMEUPDATE_US (250 * 1000)
#define NS_MEDIA_CONTROLS_BAR_H 32.0
#define NS_MEDIA_CONTROLS_TIME_W 86.0

enum {
    NETWORK_EMPTY,
    NETWORK_IDLE,
    NETWORK_LOADING,
    NETWORK_NO_SOURCE,
};

enum {
    HAVE_NOTHING,
    HAVE_METADATA,
    HAVE_CURRENT_DATA,
    HAVE_FUTURE_DATA,
    HAVE_ENOUGH_DATA,
};

enum {
    MEDIA_ERR_SRC_NOT_SUPPORTED = 4,
};

typedef struct {
    ns_js     *js;
    ns_node   *el;
    char       token[24];
    char      *src;
    gboolean   video;
    int        network_state;
    int        ready_state;
    gboolean   paused;
    gboolean   ended;
    gboolean   seeking;
    gboolean   fetching;
    gboolean   autoplaying;
    gboolean   can_autoplay;
    gboolean   held_for_load;
    double     duration;
    double     position;
    double     start_position;
    double     volume;
    int        muted;
    int        error;
    char      *error_message;
    GPtrArray *play_promises;
    JSValue    pin;
    gint64     last_timeupdate_us;
} ns_media_player;

typedef struct {
    ns_node   *el;
    char      *type;
    GPtrArray *promises;
    char      *error_name;
    char      *message;
} ns_media_task;

static void media_poll_ensure(ns_js *js);
static void media_controls_dirty(ns_media_player *p);
static void media_internal_pause(ns_media_player *p, gboolean fire_events);
static gboolean media_connected(ns_js *js, const ns_node *node);

gboolean
ns_node_is_media_element(const ns_node *n)
{
    return n && n->kind == NS_NODE_ELEMENT && n->name &&
           (g_ascii_strcasecmp(n->name, "video") == 0 ||
            g_ascii_strcasecmp(n->name, "audio") == 0);
}

static ns_media_player *
media_player_find(ns_js *js, const ns_node *el)
{
    return js && js->media_players
        ? g_hash_table_lookup(js->media_players, el) : NULL;
}

static void
media_promise_pair_free(gpointer data)
{
    JSValue *pair = data;
    g_free(pair);
}

/* Returns the element's media player state, creating it with HTML defaults on first use. */
static ns_media_player *
media_player_for(ns_js *js, ns_node *el)
{
    if (!js || !ns_node_is_media_element(el)) return NULL;
    ns_media_player *p = media_player_find(js, el);
    if (p) return p;
    if (!js->media_players)
        js->media_players = g_hash_table_new(g_direct_hash, g_direct_equal);
    p = g_new0(ns_media_player, 1);
    p->js = js;
    p->el = el;
    g_snprintf(p->token, sizeof p->token, "m%u", ++js->next_audio_token);
    p->video = ns_node_is_tag(el, NS_TAG_VIDEO);
    p->network_state = NETWORK_EMPTY;
    p->ready_state = HAVE_NOTHING;
    p->paused = TRUE;
    p->duration = NAN;
    p->volume = 1.0;
    p->muted = -1;
    p->play_promises = g_ptr_array_new_with_free_func(media_promise_pair_free);
    p->pin = JS_UNDEFINED;
    g_hash_table_insert(js->media_players, el, p);
    return p;
}

static void media_consider_element(ns_js *js, ns_node *el);

/* Player for a JS this value, first running autoplay/load consideration if connected. */
static ns_media_player *
media_player_this(JSContext *ctx, JSValueConst this_val)
{
    ns_node *el = (ns_node *)ns_unwrap_element(this_val);
    if (!ns_node_is_media_element(el)) return NULL;
    ns_js *js = js_from_ctx(ctx);
    if (js && !media_player_find(js, el) && media_connected(js, el))
        media_consider_element(js, el);
    return media_player_for(js, el);
}

/* Lazily creates the realm's audio mixer context. */
static NsAudioContext *
media_audio_context(ns_js *js)
{
    if (!js->audio_context)
        js->audio_context = ns_audio_context_new(js->current_url,
                                                 ns_js_fetch_policy(js, NULL));
    return js->audio_context;
}

/* The realm's audio mixer context, shared with Web Audio. */
NsAudioContext *
ns_js_media_audio_context(ns_js *js)
{
    return js ? media_audio_context(js) : NULL;
}

/* Effective muted state: script-set value if any, else the muted content attribute. */
static gboolean
media_muted(const ns_media_player *p)
{
    if (p->muted >= 0) return p->muted != 0;
    return ns_element_get_attr(p->el, "muted") != NULL;
}

static gboolean
media_looping(const ns_media_player *p)
{
    return ns_element_get_attr(p->el, "loop") != NULL;
}

static double
media_output_volume(const ns_media_player *p)
{
    return media_muted(p) ? 0.0 : p->volume;
}

/* Holds or drops a JS wrapper reference so a playing element is not collected. */
static void
media_pin(ns_media_player *p, gboolean pinned)
{
    JSContext *ctx = p->js->ctx;
    if (!ctx) return;
    if (pinned && JS_IsUndefined(p->pin)) {
        p->pin = ns_make_element(ctx, p->el);
    } else if (!pinned && !JS_IsUndefined(p->pin)) {
        JS_FreeValue(ctx, p->pin);
        p->pin = JS_UNDEFINED;
    }
}

static gboolean media_tasks_flush(gpointer data);

/* Queues a media element task (event name) on the realm's media task list. */
static void
media_queue_task(ns_media_player *p, const char *type)
{
    ns_js *js = p->js;
    if (!js->media_tasks) js->media_tasks = g_ptr_array_new();
    ns_media_task *task = g_new0(ns_media_task, 1);
    task->el = p->el;
    task->type = g_strdup(type);
    g_ptr_array_add(js->media_tasks, task);
    if (!js->media_task_source)
        js->media_task_source = ns_engine_idle_add(media_tasks_flush, js);
}

/* Creates an Error object with DOMException prototype, name and message. */
static JSValue
media_dom_error(JSContext *ctx, const char *name, const char *message)
{
    JSValue err = JS_NewError(ctx);
    JS_DefinePropertyValueStr(ctx, err, "name", JS_NewString(ctx, name),
        JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValueStr(ctx, err, "message", JS_NewString(ctx, message),
        JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue dx = JS_GetPropertyStr(ctx, global, "DOMException");
    if (JS_IsObject(dx)) {
        JSValue proto = JS_GetPropertyStr(ctx, dx, "prototype");
        if (JS_IsObject(proto)) JS_SetPrototype(ctx, err, proto);
        JS_FreeValue(ctx, proto);
    }
    JS_FreeValue(ctx, dx);
    JS_FreeValue(ctx, global);
    return err;
}

/* Frees resolve/reject function pairs held for pending play() promises. */
static void
media_free_promises(JSContext *ctx, GPtrArray *promises)
{
    if (!promises) return;
    for (guint i = 0; ctx && i < promises->len; i++) {
        JSValue *pair = g_ptr_array_index(promises, i);
        JS_FreeValue(ctx, pair[0]);
        JS_FreeValue(ctx, pair[1]);
    }
    g_ptr_array_free(promises, TRUE);
}

/* Resolves, or rejects with a DOMException, a list of pending play() promises. */
static void
media_settle_promises(JSContext *ctx, GPtrArray *promises,
                      const char *error_name, const char *message)
{
    for (guint i = 0; ctx && i < promises->len; i++) {
        JSValue *pair = g_ptr_array_index(promises, i);
        JSValue arg = error_name ? media_dom_error(ctx, error_name, message)
                                 : JS_UNDEFINED;
        JSValue r = JS_Call(ctx, pair[error_name ? 1 : 0], JS_UNDEFINED, 1,
                            &arg);
        JS_FreeValue(ctx, r);
        JS_FreeValue(ctx, arg);
    }
    media_free_promises(ctx, promises);
}

static void
media_task_free(ns_js *js, ns_media_task *task)
{
    media_free_promises(js->ctx, task->promises);
    g_free(task->type);
    g_free(task->error_name);
    g_free(task->message);
    g_free(task);
}

/* Runs one media task: settles promises, pauses a detached element, or fires an event. */
static void
media_task_run(ns_js *js, ns_media_task *task)
{
    if (task->promises) {
        GPtrArray *promises = task->promises;
        task->promises = NULL;
        media_settle_promises(js->ctx, promises, task->error_name,
                              task->message);
        return;
    }
    if (!task->el) return;
    ns_media_player *p = media_player_find(js, task->el);
    if (strcmp(task->type, "!detached") == 0) {
        if (p && !media_connected(js, p->el)) media_internal_pause(p, TRUE);
        return;
    }
    ns_js_dispatch_event(js, task->el, task->type, NULL);
}

/* Idle callback running all queued media tasks, then microtasks; defers while pumping. */
static gboolean
media_tasks_flush(gpointer data)
{
    ns_js *js = data;
    js->media_task_source = 0;
    if (js->halted) return G_SOURCE_REMOVE;
    if (js->in_pump) {
        js->media_task_source = ns_engine_timeout_add(4, media_tasks_flush, js);
        return G_SOURCE_REMOVE;
    }
    GPtrArray *tasks = js->media_tasks;
    js->media_tasks = NULL;
    js->media_tasks_flushing = tasks;
    for (guint i = 0; tasks && i < tasks->len; i++) {
        ns_media_task *task = g_ptr_array_index(tasks, i);
        media_task_run(js, task);
        media_task_free(js, task);
    }
    js->media_tasks_flushing = NULL;
    if (tasks) g_ptr_array_free(tasks, TRUE);
    ns_drain_microtasks(js);
    return G_SOURCE_REMOVE;
}

/* Moves pending play() promises into a task that will resolve or reject them. */
static void
media_queue_settle(ns_media_player *p, const char *error_name,
                   const char *message)
{
    if (p->play_promises->len == 0) return;
    media_queue_task(p, "");
    ns_media_task *task = g_ptr_array_index(p->js->media_tasks,
                                            p->js->media_tasks->len - 1);
    task->promises = p->play_promises;
    task->error_name = g_strdup(error_name);
    task->message = g_strdup(message);
    p->play_promises = g_ptr_array_new_with_free_func(media_promise_pair_free);
}

static void
media_reject_play(ns_media_player *p, const char *name, const char *message)
{
    media_queue_settle(p, name, message);
}

/* Queues the playing event and resolves pending play() promises. */
static void
media_notify_playing(ns_media_player *p)
{
    media_queue_task(p, "playing");
    media_queue_settle(p, NULL, NULL);
}

/* Selects and resolves the media source URL, applying fetch policy; caller frees. */
static char *
ns_media_resolve_src(JSContext *ctx, ns_node *node)
{
    const char *src = ns_media_select_source(node);
    if (!src) return NULL;
    ns_js *js = js_from_ctx(ctx);
    char *abs = (js && js->current_url) ? ns_url_resolve(js->current_url, src)
                                        : g_strdup(src);
    if (!abs) return NULL;
    ns_fetch_verdict verdict = ns_fetch_policy_check(
        js ? ns_js_fetch_policy(js, node) : NULL, NS_FETCH_DEST_MEDIA,
        js ? js->current_url : NULL, abs, NULL);
    if (ns_fetch_verdict_blocks(verdict)) {
        char *message = ns_fetch_verdict_message(verdict, NS_FETCH_DEST_MEDIA,
                                                 abs);
        if (js && js->log_cb) js->log_cb(message, js->log_user_data);
        g_free(message);
        g_free(abs);
        return NULL;
    }
    return abs;
}

/* Tears down the audio stream for a player. */
static void
media_close_backend(ns_media_player *p)
{
    if (p->fetching && p->js->audio_context)
        ns_audio_context_close(p->js->audio_context, p->token);
    p->fetching = FALSE;
}

/* Enters NETWORK_NO_SOURCE with an error, fires error and rejects play promises. */
static void
media_fail_source(ns_media_player *p, int code, const char *message)
{
    p->error = code;
    g_free(p->error_message);
    p->error_message = g_strdup(message);
    p->network_state = NETWORK_NO_SOURCE;
    p->fetching = FALSE;
    media_pin(p, FALSE);
    media_queue_task(p, "error");
    media_reject_play(p, "NotSupportedError",
                      "Failed to load because no supported source was found.");
}

/* Opens an audio-only source (URL or blob) in the mixer with volume and loop set. */
static void
media_start_fetch(ns_media_player *p)
{
    if (p->video || p->fetching || !p->src) return;
    ns_js *js = p->js;
    NsAudioContext *audio = media_audio_context(js);
    if (g_str_has_prefix(p->src, "blob:")) {
        GBytes *bytes = ns_net_resolve_blob_for(p->src,
                                                ns_js_current_url(js), NULL);
        if (!bytes) {
            media_fail_source(p, MEDIA_ERR_SRC_NOT_SUPPORTED,
                              "blob URL not found");
            return;
        }
        ns_audio_context_open_bytes(audio, p->token, bytes, FALSE);
        g_bytes_unref(bytes);
    } else {
        ns_audio_context_open(audio, p->token, p->src);
    }
    ns_audio_context_set_volume(audio, p->token, media_output_volume(p));
    ns_audio_context_set_loop(audio, p->token, media_looping(p));
    p->fetching = TRUE;
    p->network_state = NETWORK_LOADING;
    media_poll_ensure(js);
}

/* True if config disables autoplay and the user has not interacted with
 * the page yet.  As in Chrome and Firefox, the block lasts until the first
 * click or key press (sticky activation), not only for a few seconds after
 * each one: a page that starts its video from an animation frame after the
 * user pressed its start button, as WebGL films do, may then play it. */
static gboolean
media_autoplay_blocked(const ns_media_player *p)
{
    const ns_config *cfg = ns_config_get();
    return cfg && !cfg->autoplay_enabled && !p->js->user_ever_activated &&
           !ns_js_has_transient_activation(p->js);
}

/* Autoplay policy: muted, or after user activation, unless blocked. */
static gboolean
media_autoplay_allowed(const ns_media_player *p)
{
    if (media_autoplay_blocked(p)) return FALSE;
    return media_muted(p) || p->js->user_ever_activated;
}

/* Media element load algorithm: aborts play, resets state and selects a source. */
static void
media_load(ns_media_player *p)
{
    media_reject_play(p, "AbortError",
                      "The play() request was interrupted by a new load "
                      "request.");
    media_close_backend(p);
    if (p->network_state != NETWORK_EMPTY) {
        media_queue_task(p, "emptied");
        if (!isnan(p->duration)) media_queue_task(p, "durationchange");
    }
    p->network_state = NETWORK_EMPTY;
    p->ready_state = HAVE_NOTHING;
    p->paused = TRUE;
    p->ended = FALSE;
    p->seeking = FALSE;
    p->autoplaying = FALSE;
    p->can_autoplay = TRUE;
    p->held_for_load = FALSE;
    p->position = 0.0;
    p->start_position = 0.0;
    p->duration = NAN;
    p->error = 0;
    g_clear_pointer(&p->error_message, g_free);
    g_clear_pointer(&p->src, g_free);
    media_pin(p, FALSE);

    gboolean has_source = ns_element_get_attr(p->el, "src") != NULL;
    for (const ns_node *c = p->el->first_child; !has_source && c;
         c = c->next_sibling)
        has_source = ns_node_is_tag(c, NS_TAG_SOURCE);
    if (!has_source) return;

    p->network_state = NETWORK_LOADING;
    media_queue_task(p, "loadstart");
    JSContext *ctx = p->js->ctx;
    p->src = ctx ? ns_media_resolve_src(ctx, p->el) : NULL;
    if (!p->src || p->video) {
        media_fail_source(p, MEDIA_ERR_SRC_NOT_SUPPORTED,
                          "MEDIA_ELEMENT_ERROR: Format error");
        return;
    }
    gboolean eager =
        ns_element_get_attr(p->el, "autoplay") != NULL ||
        g_strcmp0(ns_element_get_attr(p->el, "preload"), "auto") == 0;
    if (eager) {
        media_start_fetch(p);
        media_poll_ensure(p->js);
    } else {
        p->network_state = NETWORK_IDLE;
        media_queue_task(p, "suspend");
    }
}

/* True once the document has finished loading (playback is held until then). */
static gboolean
media_page_loaded(const ns_media_player *p)
{
    return p->js->ready_state >= 2;
}

/* Starts audio output. */
static void
media_backend_play(ns_media_player *p)
{
    p->held_for_load = !media_page_loaded(p);
    if (p->held_for_load) {
        media_poll_ensure(p->js);
        return;
    }
    if (p->js->audio_context)
        ns_audio_context_play(p->js->audio_context, p->token);
}


/* Pauses the audio stream. */
static void
media_backend_pause(ns_media_player *p)
{
    if (p->fetching && p->js->audio_context)
        ns_audio_context_pause(p->js->audio_context, p->token);
}

/* Seeks the audio stream to t seconds. */
static void
media_backend_seek(ns_media_player *p, double t)
{
    if (p->fetching && p->js->audio_context)
        ns_audio_context_seek(p->js->audio_context, p->token, t);
}

/* Current playback position from the audio status. */
static double
media_current_time(ns_media_player *p)
{
    if (p->ready_state < HAVE_METADATA) return p->start_position;
    if (p->fetching && p->js->audio_context) {
        NsAudioStatus status;
        if (ns_audio_context_status(p->js->audio_context, p->token, &status) &&
            status.state == NS_AUDIO_PLAYER_READY)
            p->position = status.position;
    }
    return p->position;
}

/* Internal play steps: restart if ended, fire play/waiting/playing and start output. */
static void
media_internal_play(ns_media_player *p)
{
    if (p->ended && !media_looping(p)) {
        p->position = 0.0;
        media_backend_seek(p, 0.0);
    }
    p->ended = FALSE;
    media_start_fetch(p);
    if (p->paused) {
        p->paused = FALSE;
        media_pin(p, TRUE);
        media_queue_task(p, "play");
        if (p->ready_state <= HAVE_CURRENT_DATA)
            media_queue_task(p, "waiting");
        else
            media_notify_playing(p);
    } else if (p->ready_state >= HAVE_FUTURE_DATA) {
        media_queue_settle(p, NULL, NULL);
    }
    if (p->ready_state >= HAVE_METADATA) media_backend_play(p);
    p->last_timeupdate_us = ns_time_monotonic_us();
    media_poll_ensure(p->js);
    media_controls_dirty(p);
}

/* Internal pause steps: stop output, fire timeupdate/pause, reject play promises. */
static void
media_internal_pause(ns_media_player *p, gboolean fire_events)
{
    p->autoplaying = FALSE;
    if (p->paused) return;
    p->paused = TRUE;
    media_current_time(p);
    media_backend_pause(p);
    media_pin(p, FALSE);
    media_controls_dirty(p);
    if (fire_events) {
        media_queue_task(p, "timeupdate");
        media_queue_task(p, "pause");
    }
    media_reject_play(p, "AbortError",
                      "The play() request was interrupted by a call to "
                      "pause().");
}

/* Metadata/data available: fires the readiness events and starts autoplay if allowed. */
static void
media_became_ready(ns_media_player *p, double duration)
{
    p->duration = duration > 0 ? duration : NAN;
    p->ready_state = HAVE_ENOUGH_DATA;
    p->network_state = NETWORK_IDLE;
    media_queue_task(p, "durationchange");
    media_queue_task(p, "loadedmetadata");
    media_queue_task(p, "loadeddata");
    media_queue_task(p, "canplay");
    media_queue_task(p, "canplaythrough");
    if (p->start_position > 0 && p->js->audio_context) {
        ns_audio_context_seek(p->js->audio_context, p->token,
                              p->start_position);
    }
    p->position = p->start_position;
    if (!p->paused) {
        media_backend_play(p);
        media_notify_playing(p);
    } else if (p->can_autoplay &&
               ns_element_get_attr(p->el, "autoplay") &&
               media_autoplay_allowed(p)) {
        p->autoplaying = TRUE;
        media_internal_play(p);
    }
}

/* Playback ended: fires timeupdate, pause (if playing) and ended. */
static void
media_reached_end(ns_media_player *p)
{
    p->position = isnan(p->duration) ? p->position : p->duration;
    p->ended = TRUE;
    media_controls_dirty(p);
    media_queue_task(p, "timeupdate");
    if (!p->paused) {
        p->paused = TRUE;
        media_pin(p, FALSE);
        media_queue_task(p, "pause");
    }
    media_queue_task(p, "ended");
}

/* Polls one player for readiness, end and timeupdate; returns TRUE while still active. */
static gboolean
media_poll_player(ns_media_player *p, gint64 now)
{
    if (p->network_state == NETWORK_NO_SOURCE ||
        p->network_state == NETWORK_EMPTY)
        return FALSE;
    if (p->held_for_load) {
        if (!media_page_loaded(p)) return TRUE;
        p->held_for_load = FALSE;
        if (!p->paused) media_backend_play(p);
    }
    if (!p->fetching || !p->js->audio_context)
        return p->network_state == NETWORK_LOADING;
    NsAudioStatus status;
    gboolean known = ns_audio_context_status(p->js->audio_context,
                                             p->token, &status);
    if (known && status.state == NS_AUDIO_PLAYER_FAILED) {
        media_fail_source(p, MEDIA_ERR_SRC_NOT_SUPPORTED,
            status.error == NS_AUDIO_ERROR_DECODE
                ? "MEDIA_ELEMENT_ERROR: Format error"
                : "MEDIA_ELEMENT_ERROR: Network error");
        return FALSE;
    }
    if (p->ready_state < HAVE_METADATA) {
        if (known && status.state == NS_AUDIO_PLAYER_READY)
            media_became_ready(p, status.duration);
        return TRUE;
    }
    if (known && status.state == NS_AUDIO_PLAYER_READY &&
        status.duration > 0 && status.duration != p->duration) {
        p->duration = status.duration;
        media_queue_task(p, "durationchange");
    }
    if (!p->paused && known && status.state == NS_AUDIO_PLAYER_READY) {
        p->position = status.position;
        if (status.ended && !media_looping(p)) {
            media_reached_end(p);
            return FALSE;
        }
    }
    if (p->paused) return FALSE;
    if (now - p->last_timeupdate_us >= NS_MEDIA_TIMEUPDATE_US) {
        p->last_timeupdate_us = now;
        media_queue_task(p, "timeupdate");
        media_controls_dirty(p);
    }
    return TRUE;
}

/* Periodic timer polling all players; removes itself when none are active. */
static gboolean
media_poll(gpointer data)
{
    ns_js *js = data;
    if (js->halted || !js->media_players) {
        js->media_poll_source = 0;
        return G_SOURCE_REMOVE;
    }
    gint64 now = ns_time_monotonic_us();
    gboolean active = FALSE;
    GHashTableIter it;
    gpointer value;
    g_hash_table_iter_init(&it, js->media_players);
    while (g_hash_table_iter_next(&it, NULL, &value))
        if (media_poll_player(value, now)) active = TRUE;
    if (active) return G_SOURCE_CONTINUE;
    js->media_poll_source = 0;
    return G_SOURCE_REMOVE;
}

/* Starts the media poll timer if it is not already running. */
static void
media_poll_ensure(ns_js *js)
{
    if (!js->media_poll_source)
        js->media_poll_source =
            ns_engine_timeout_add(NS_MEDIA_POLL_MS, media_poll, js);
}

/* Releases a player's backend, pin, pending promises and strings. */
static void
media_player_free(ns_media_player *p)
{
    media_close_backend(p);
    media_pin(p, FALSE);
    media_free_promises(p->js->ctx, p->play_promises);
    g_free(p->src);
    g_free(p->error_message);
    g_free(p);
}

/* Clears element pointers in queued and in-flight media tasks for a freed node. */
static void
media_forget_tasks(ns_js *js, const ns_node *el)
{
    for (guint i = 0; js->media_tasks && i < js->media_tasks->len; i++) {
        ns_media_task *task = g_ptr_array_index(js->media_tasks, i);
        if (task->el == el) task->el = NULL;
    }
    for (guint i = 0; js->media_tasks_flushing &&
                      i < js->media_tasks_flushing->len; i++) {
        ns_media_task *task = g_ptr_array_index(js->media_tasks_flushing, i);
        if (task->el == el) task->el = NULL;
    }
}

/* Drops a media player when its DOM node is being freed. */
void
ns_media_node_released(ns_js *js, ns_node *n)
{
    ns_media_player *p = media_player_find(js, n);
    if (!p) return;
    g_hash_table_remove(js->media_players, n);
    media_forget_tasks(js, n);
    media_player_free(p);
}

/* Tears down all media players, tasks, timers, MSE state and the audio context. */
void
ns_media_teardown(ns_js *js)
{
    if (!js) return;
    ns_webaudio_teardown(js);
    if (js->media_poll_source) {
        ns_engine_source_remove(js->media_poll_source);
        js->media_poll_source = 0;
    }
    if (js->media_task_source) {
        ns_engine_source_remove(js->media_task_source);
        js->media_task_source = 0;
    }
    if (js->media_tasks) {
        for (guint i = 0; i < js->media_tasks->len; i++)
            media_task_free(js, g_ptr_array_index(js->media_tasks, i));
        g_ptr_array_free(js->media_tasks, TRUE);
        js->media_tasks = NULL;
    }
    if (js->media_players) {
        GHashTableIter it;
        gpointer value;
        g_hash_table_iter_init(&it, js->media_players);
        while (g_hash_table_iter_next(&it, NULL, &value))
            media_player_free(value);
        g_hash_table_destroy(js->media_players);
        js->media_players = NULL;
    }
    if (js->audio_context) {
        ns_audio_context_destroy(js->audio_context);
        js->audio_context = NULL;
    }
}

/* Pauses all players without events and disables autoplay (e.g. on page hide). */
void
ns_js_suspend_media(ns_js *js)
{
    if (!js) return;
    ns_webaudio_suspend_all(js);
    if (!js->media_players) return;
    GHashTableIter it;
    gpointer value;
    g_hash_table_iter_init(&it, js->media_players);
    while (g_hash_table_iter_next(&it, NULL, &value)) {
        ns_media_player *p = value;
        p->can_autoplay = FALSE;
        media_internal_pause(p, FALSE);
    }
}

/* True if node is in the realm's current document. */
static gboolean
media_connected(ns_js *js, const ns_node *node)
{
    for (const ns_node *p = node; p; p = p->parent)
        if (p == js->current_doc) return TRUE;
    return FALSE;
}

/* Starts the load algorithm for an unloaded media element that has a source. */
static void
media_consider_element(ns_js *js, ns_node *el)
{
    ns_media_player *p = media_player_find(js, el);
    if (p && p->network_state != NETWORK_EMPTY) return;
    gboolean has_source = ns_element_get_attr(el, "src") != NULL;
    for (const ns_node *c = el->first_child; !has_source && c;
         c = c->next_sibling)
        has_source = ns_node_is_tag(c, NS_TAG_SOURCE);
    if (!has_source) return;
    media_load(media_player_for(js, el));
}

/* Recursively considers every media element under root for loading. */
static void
media_walk_connected(ns_js *js, ns_node *root, int depth)
{
    if (!root || depth >= 512) return;
    if (ns_node_is_media_element(root)) media_consider_element(js, root);
    for (ns_node *c = root->first_child; c; c = c->next_sibling)
        media_walk_connected(js, c, depth + 1);
}

/* Loads media elements in a newly connected subtree. */
void
ns_media_subtree_connected(ns_js *js, ns_node *root)
{
    if (!js || !root || !media_connected(js, root)) return;
    media_walk_connected(js, root, 0);
}

/* Initial media scan of a document, adopting it as the current document if unset. */
void
ns_media_scan(ns_js *js, ns_node *doc, const char *base_url)
{
    if (!js || !doc) return;
    if (!js->current_doc) {
        js->current_doc = doc;
        if (!js->current_url) js->current_url = g_strdup(base_url);
    }
    if (js->current_doc == doc) media_walk_connected(js, doc, 0);
}

/* Queues a pause check for playing media elements inside a removed subtree. */
void
ns_media_subtree_disconnected(ns_js *js, ns_node *root)
{
    if (!js || !root || !js->media_players) return;
    GHashTableIter it;
    gpointer value;
    g_hash_table_iter_init(&it, js->media_players);
    while (g_hash_table_iter_next(&it, NULL, &value)) {
        ns_media_player *p = value;
        for (const ns_node *n = p->el; n; n = n->parent) {
            if (n != root) continue;
            if (!p->paused) media_queue_task(p, "!detached");
            break;
        }
    }
}

/* Reacts to src (reload), loop and muted attribute changes on a media element. */
void
ns_media_attr_changed(ns_js *js, ns_node *el, const char *name)
{
    if (!js || !ns_node_is_media_element(el) || !name) return;
    ns_media_player *p = media_player_find(js, el);
    if (g_ascii_strcasecmp(name, "src") == 0) {
        if (!p && ns_element_get_attr(el, "src") == NULL) return;
        media_load(media_player_for(js, el));
        return;
    }
    if (!p) return;
    if (g_ascii_strcasecmp(name, "loop") == 0) {
        if (p->fetching && js->audio_context)
            ns_audio_context_set_loop(js->audio_context, p->token,
                                      media_looping(p));
    } else if (g_ascii_strcasecmp(name, "muted") == 0 && p->muted < 0) {
        if (p->fetching && js->audio_context)
            ns_audio_context_set_volume(js->audio_context, p->token,
                                        media_output_volume(p));
    }
}

/* Reopens audio players whose blob: source URL got new data. */
void
ns_media_blob_updated(ns_js *js, const char *url)
{
    if (!js || !url || !js->media_players || !js->audio_context) return;
    GHashTableIter it;
    gpointer value;
    g_hash_table_iter_init(&it, js->media_players);
    while (g_hash_table_iter_next(&it, NULL, &value)) {
        ns_media_player *p = value;
        if (!p->fetching || !p->src || strcmp(p->src, url) != 0) continue;
        GBytes *bytes = ns_net_resolve_blob_for(url,
                                                ns_js_current_url(js), NULL);
        if (!bytes) continue;
        ns_audio_context_open_bytes(js->audio_context, p->token, bytes, TRUE);
        g_bytes_unref(bytes);
    }
}

/* HTMLMediaElement.play(): returns a promise, rejecting when unsupported or blocked. */
JSValue
ns_media_play(JSContext *ctx, JSValueConst this_val,
              int argc, JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    ns_media_player *p = media_player_this(ctx, this_val);
    if (!p) return ns_returns_resolved_undefined(ctx, this_val, 0, NULL);
    if (p->network_state == NETWORK_EMPTY) media_load(p);
    if (p->error == MEDIA_ERR_SRC_NOT_SUPPORTED)
        return ns_promise_reject_dom(ctx, "NotSupportedError",
            "Failed to load because no supported source was found.");
    if (p->paused && media_autoplay_blocked(p))
        return ns_promise_reject_dom(ctx, "NotAllowedError",
            "play() failed because the user didn't interact with the "
            "document first.");
    JSValue *pair = g_new0(JSValue, 2);
    JSValue promise = JS_NewPromiseCapability(ctx, pair);
    if (JS_IsException(promise)) {
        g_free(pair);
        return promise;
    }
    g_ptr_array_add(p->play_promises, pair);
    p->can_autoplay = FALSE;
    media_internal_play(p);
    return promise;
}

/* HTMLMediaElement.pause(), loading first if the network state is empty. */
JSValue
ns_media_pause(JSContext *ctx, JSValueConst this_val,
               int argc, JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    ns_media_player *p = media_player_this(ctx, this_val);
    if (!p) return JS_UNDEFINED;
    if (p->network_state == NETWORK_EMPTY) media_load(p);
    p->can_autoplay = FALSE;
    media_internal_pause(p, TRUE);
    return JS_UNDEFINED;
}

JSValue
ns_media_load(JSContext *ctx, JSValueConst this_val,
              int argc, JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    ns_media_player *p = media_player_this(ctx, this_val);
    if (p) media_load(p);
    return JS_UNDEFINED;
}

/* Seeks to t clamped to duration, firing seeking, timeupdate and seeked. */
static void
media_seek_to(ns_media_player *p, double t)
{
    if (!isnan(p->duration) && t > p->duration) t = p->duration;
    p->position = t;
    p->ended = FALSE;
    p->seeking = TRUE;
    media_queue_task(p, "seeking");
    media_backend_seek(p, t);
    p->seeking = FALSE;
    media_queue_task(p, "timeupdate");
    media_queue_task(p, "seeked");
    media_controls_dirty(p);
}

/* Sets the muted IDL state, updates output volume and fires volumechange. */
static void
media_apply_muted(ns_media_player *p, gboolean muted)
{
    p->muted = muted ? 1 : 0;
    if (p->fetching && p->js->audio_context)
        ns_audio_context_set_volume(p->js->audio_context, p->token,
                                    media_output_volume(p));
    media_queue_task(p, "volumechange");
    media_controls_dirty(p);
}

JSValue
ns_media_get_current_time(JSContext *ctx, JSValueConst this_val)
{
    ns_media_player *p = media_player_this(ctx, this_val);
    return JS_NewFloat64(ctx, p ? media_current_time(p) : 0.0);
}

/* currentTime setter; before metadata it only records the start position. */
JSValue
ns_media_set_current_time(JSContext *ctx, JSValueConst this_val,
                          JSValueConst val)
{
    double t = 0.0;
    if (JS_ToFloat64(ctx, &t, val)) return JS_EXCEPTION;
    if (isnan(t) || t < 0.0) t = 0.0;
    ns_media_player *p = media_player_this(ctx, this_val);
    if (!p) return JS_UNDEFINED;
    if (p->ready_state < HAVE_METADATA) {
        p->start_position = t;
        return JS_UNDEFINED;
    }
    media_seek_to(p, t);
    return JS_UNDEFINED;
}

JSValue
ns_media_fast_seek(JSContext *ctx, JSValueConst this_val,
                   int argc, JSValueConst *argv)
{
    if (argc < 1) return JS_UNDEFINED;
    return ns_media_set_current_time(ctx, this_val, argv[0]);
}

JSValue
ns_media_get_duration(JSContext *ctx, JSValueConst this_val)
{
    ns_media_player *p = media_player_this(ctx, this_val);
    return JS_NewFloat64(ctx, p ? p->duration : NAN);
}

JSValue
ns_media_get_paused(JSContext *ctx, JSValueConst this_val)
{
    ns_media_player *p = media_player_this(ctx, this_val);
    return JS_NewBool(ctx, p ? p->paused : TRUE);
}

JSValue
ns_media_get_ended(JSContext *ctx, JSValueConst this_val)
{
    ns_media_player *p = media_player_this(ctx, this_val);
    return JS_NewBool(ctx, p && p->ended);
}

JSValue
ns_media_get_seeking(JSContext *ctx, JSValueConst this_val)
{
    ns_media_player *p = media_player_this(ctx, this_val);
    return JS_NewBool(ctx, p && p->seeking);
}

JSValue
ns_media_get_readyState(JSContext *ctx, JSValueConst this_val)
{
    ns_media_player *p = media_player_this(ctx, this_val);
    return JS_NewInt32(ctx, p ? p->ready_state : HAVE_NOTHING);
}

JSValue
ns_media_get_networkState(JSContext *ctx, JSValueConst this_val)
{
    ns_media_player *p = media_player_this(ctx, this_val);
    return JS_NewInt32(ctx, p ? p->network_state : NETWORK_EMPTY);
}

/* MediaError object for the element's error, or null when none. */
JSValue
ns_media_get_error(JSContext *ctx, JSValueConst this_val)
{
    ns_media_player *p = media_player_this(ctx, this_val);
    if (!p) return JS_UNDEFINED;
    if (!p->error) return JS_NULL;
    JSValue err = JS_NewObject(ctx);
    ns_obj_adopt_global_proto(ctx, err, "MediaError");
    JS_SetPropertyStr(ctx, err, "_nd_code", JS_NewInt32(ctx, p->error));
    JS_SetPropertyStr(ctx, err, "_nd_message",
                      JS_NewString(ctx, p->error_message ? p->error_message
                                                         : ""));
    return err;
}

JSValue
ns_media_get_volume(JSContext *ctx, JSValueConst this_val)
{
    ns_media_player *p = media_player_this(ctx, this_val);
    return JS_NewFloat64(ctx, p ? p->volume : 1.0);
}

/* volume setter; IndexSizeError outside [0, 1], fires volumechange on change. */
JSValue
ns_media_set_volume(JSContext *ctx, JSValueConst this_val, JSValueConst val)
{
    double vol = 1.0;
    if (JS_ToFloat64(ctx, &vol, val)) return JS_EXCEPTION;
    if (isnan(vol) || vol < 0.0 || vol > 1.0)
        return ns_throw_dom_exception(ctx, "IndexSizeError", 1,
            "The volume provided is outside the range [0, 1].");
    ns_media_player *p = media_player_this(ctx, this_val);
    if (!p || p->volume == vol) return JS_UNDEFINED;
    p->volume = vol;
    if (p->fetching && p->js->audio_context)
        ns_audio_context_set_volume(p->js->audio_context, p->token,
                                    media_output_volume(p));
    media_queue_task(p, "volumechange");
    return JS_UNDEFINED;
}

JSValue
ns_media_get_muted(JSContext *ctx, JSValueConst this_val)
{
    ns_media_player *p = media_player_this(ctx, this_val);
    return JS_NewBool(ctx, p && media_muted(p));
}

/* muted setter; unmuting an autoplaying element without activation pauses it. */
JSValue
ns_media_set_muted(JSContext *ctx, JSValueConst this_val, JSValueConst val)
{
    ns_media_player *p = media_player_this(ctx, this_val);
    if (!p) return JS_UNDEFINED;
    gboolean muted = JS_ToBool(ctx, val) > 0;
    if (media_muted(p) == muted && p->muted >= 0) return JS_UNDEFINED;
    media_apply_muted(p, muted);
    if (!muted && !p->paused && p->autoplaying &&
        !p->js->user_ever_activated)
        media_internal_pause(p, TRUE);
    return JS_UNDEFINED;
}

/* seekable TimeRanges: 0 to duration. */
JSValue
ns_media_get_seekable_ranges(JSContext *ctx, JSValueConst this_val)
{
    ns_media_player *p = media_player_this(ctx, this_val);
    double end = p && !isnan(p->duration) ? p->duration : 0.0;
    return ns_media_time_ranges_for(ctx, end);
}

/* buffered TimeRanges: 0 to duration once metadata is known. */
JSValue
ns_media_get_buffered_ranges(JSContext *ctx, JSValueConst this_val)
{
    ns_media_player *p = media_player_this(ctx, this_val);
    double end = p && p->ready_state >= HAVE_METADATA && !isnan(p->duration)
        ? p->duration : 0.0;
    return ns_media_time_ranges_for(ctx, end);
}

/* played TimeRanges approximated as 0 to the current time. */
JSValue
ns_media_get_played_ranges(JSContext *ctx, JSValueConst this_val)
{
    ns_media_player *p = media_player_this(ctx, this_val);
    return ns_media_time_ranges_for(ctx, p ? media_current_time(p) : 0.0);
}

/* videoWidth: always 0, since no video is decoded. */
JSValue
ns_media_get_video_width(JSContext *ctx, JSValueConst this_val)
{
    (void)this_val;
    return JS_NewInt32(ctx, 0);
}

/* videoHeight: always 0, since no video is decoded. */
JSValue
ns_media_get_video_height(JSContext *ctx, JSValueConst this_val)
{
    (void)this_val;
    return JS_NewInt32(ctx, 0);
}

/* Requests a repaint of the element when it shows native controls. */
static void
media_controls_dirty(ns_media_player *p)
{
    if (ns_element_get_attr(p->el, "controls"))
        ns_js_request_repaint_node(p->js, p->el);
}

/* Computes the native controls bar geometry (play, seek, time, mute) for a box. */
gboolean
ns_media_controls_layout(const ns_node *el, double x, double y,
                         double w, double h, ns_media_controls_rect *out)
{
    if (!ns_node_is_media_element(el) ||
        !ns_element_get_attr(el, "controls") || !(w >= 40.0) || !(h >= 16.0))
        return FALSE;
    double bar_h = MIN(h, NS_MEDIA_CONTROLS_BAR_H);
    out->x = x;
    out->y = y + h - bar_h;
    out->w = w;
    out->h = bar_h;
    out->play_x = x;
    out->play_w = bar_h;
    out->mute_w = bar_h;
    out->mute_x = x + w - bar_h;
    out->time_w = w >= 2 * bar_h + NS_MEDIA_CONTROLS_TIME_W + 60.0
        ? NS_MEDIA_CONTROLS_TIME_W : 0.0;
    out->time_x = out->mute_x - out->time_w;
    out->seek_x0 = x + bar_h + 6.0;
    out->seek_x1 = MAX(out->seek_x0, out->time_x - 8.0);
    return TRUE;
}

/* Hit-tests a point against the controls layout, returning the control under it. */
ns_media_control
ns_media_controls_hit(const ns_media_controls_rect *r, double px, double py)
{
    if (!r || px < r->x || px > r->x + r->w || py < r->y ||
        py > r->y + r->h)
        return NS_MEDIA_CONTROL_NONE;
    if (px < r->play_x + r->play_w) return NS_MEDIA_CONTROL_PLAY;
    if (px >= r->mute_x) return NS_MEDIA_CONTROL_MUTE;
    if (px >= r->seek_x0 - 6.0 && px <= r->seek_x1 + 6.0)
        return NS_MEDIA_CONTROL_SEEK;
    return NS_MEDIA_CONTROL_NONE;
}

/* Snapshots the state needed to paint the native controls for an element. */
void
ns_media_controls_get(ns_js *js, const ns_node *el,
                      ns_media_controls_state *out)
{
    memset(out, 0, sizeof *out);
    out->paused = TRUE;
    out->duration = NAN;
    out->audible = !ns_node_is_tag(el, NS_TAG_VIDEO);
    ns_media_player *p = media_player_find(js, el);
    if (!p) return;
    out->paused = p->paused;
    out->muted = media_muted(p);
    out->audible = !p->video;
    out->position = media_current_time(p);
    out->duration = p->duration;
}

/* Handles a click on a native control: toggle play, seek to fraction, or mute. */
void
ns_media_controls_activate(ns_js *js, const ns_node *el,
                           ns_media_control control, double fraction)
{
    if (!js || !ns_node_is_media_element(el)) return;
    ns_media_player *p = media_player_find(js, el);
    if (!p && control == NS_MEDIA_CONTROL_PLAY) {
        p = media_player_for(js, (ns_node *)el);
        if (p && p->network_state == NETWORK_EMPTY) media_load(p);
    }
    if (!p) return;
    switch (control) {
    case NS_MEDIA_CONTROL_PLAY:
        p->can_autoplay = FALSE;
        if (p->paused || p->ended) media_internal_play(p);
        else media_internal_pause(p, TRUE);
        break;
    case NS_MEDIA_CONTROL_SEEK:
        if (p->ready_state >= HAVE_METADATA && !isnan(p->duration))
            media_seek_to(p, CLAMP(fraction, 0.0, 1.0) * p->duration);
        break;
    case NS_MEDIA_CONTROL_MUTE:
        media_apply_muted(p, !media_muted(p));
        break;
    default:
        break;
    }
}
