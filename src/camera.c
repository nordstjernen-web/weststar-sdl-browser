/* Weststar SDL Browser — per-site media-capture permission for getUserMedia.
 * Per-origin allow/deny decisions with one pending prompt handed to the UI thread. Weststar
 * captures no video, so this now guards only the (silent) microphone track.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "camera.h"
#include "plat/ns_plat.h"
#include "security.h"

#include <string.h>

#include "js.h"
#include "net.h"

/* Permission state is touched from the engine and UI threads. */
static NsMutex g_camera_lock;
static GHashTable *g_camera_decisions;
static char *g_camera_pending;

/* Stores an allow (1) or deny (2) camera decision for an origin; caller holds the lock. */
static void
ns_camera_record_decision(const char *origin, gboolean allow)
{
    if (!g_camera_decisions)
        g_camera_decisions = g_hash_table_new_full(g_str_hash, g_str_equal,
                                                   g_free, NULL);
    g_hash_table_insert(g_camera_decisions, g_strdup(origin),
                        GINT_TO_POINTER(allow ? 1 : 2));
}

gboolean
ns_camera_has_pending_origin(void)
{
    ns_mutex_lock(&g_camera_lock);
    gboolean pending = g_camera_pending != NULL;
    ns_mutex_unlock(&g_camera_lock);
    return pending;
}

/* Returns and clears the origin awaiting a camera prompt; caller frees. */
char *
ns_camera_take_pending_origin(void)
{
    ns_mutex_lock(&g_camera_lock);
    char *origin = g_camera_pending;
    g_camera_pending = NULL;
    ns_mutex_unlock(&g_camera_lock);
    return origin;
}

/* Records the user's camera choice for an origin and clears it if pending. */
void
ns_camera_set_decision(const char *origin, int allow)
{
    if (!origin || !*origin) return;
    ns_mutex_lock(&g_camera_lock);
    ns_camera_record_decision(origin, allow ? TRUE : FALSE);
    if (g_camera_pending && g_strcmp0(g_camera_pending, origin) == 0)
        g_clear_pointer(&g_camera_pending, g_free);
    ns_mutex_unlock(&g_camera_lock);
}

static int ns_camera_permission_locked(char *origin);

/* Permission key for the page: its origin, or URL without fragment for opaque origins. */
static char *
ns_camera_key(ns_js *js)
{
    const char *url = ns_js_current_url(js);
    char *origin = ns_url_origin_from(url);
    if (!origin || !*origin) {
        /* Opaque origins (file:, data:, ...) must not share one decision:
         * key them by their URL without the fragment. */
        g_free(origin);
        gsize n = url ? strcspn(url, "#") : 0;
        origin = n ? g_strndup(url, n) : g_strdup("about:blank");
    }
    return origin;
}

/* True only if the page's origin has an explicit allow decision. */
gboolean
ns_camera_permission_granted(ns_js *js)
{
    g_autofree char *origin = ns_camera_key(js);
    ns_mutex_lock(&g_camera_lock);
    gpointer v = g_camera_decisions
        ? g_hash_table_lookup(g_camera_decisions, origin) : NULL;
    ns_mutex_unlock(&g_camera_lock);
    return GPOINTER_TO_INT(v) == 1;
}

/* Returns 1 allowed, 0 denied, -1 pending for the page's origin, queuing a prompt. */
int
ns_camera_permission(ns_js *js)
{
    char *origin = ns_camera_key(js);
    ns_mutex_lock(&g_camera_lock);
    int r = ns_camera_permission_locked(origin);
    ns_mutex_unlock(&g_camera_lock);
    return r;
}

/* Looks up or queues a prompt for origin (consumed); test mode may auto-allow. */
static int
ns_camera_permission_locked(char *origin)
{
    if (!g_camera_decisions)
        g_camera_decisions = g_hash_table_new_full(g_str_hash, g_str_equal,
                                                   g_free, NULL);
    gpointer prev = NULL;
    if (g_hash_table_lookup_extended(g_camera_decisions, origin, NULL, &prev)) {
        int v = GPOINTER_TO_INT(prev);
        g_free(origin);
        if (v == 1) return 1;
        if (v == 2) return 0;
        return -1;
    }

    if (ns_security_tool_mode() && ns_env_flag("NS_CAMERA_ALLOW")) {
        ns_camera_record_decision(origin, TRUE);
        g_free(origin);
        return 1;
    }

    g_hash_table_insert(g_camera_decisions, g_strdup(origin),
                        GINT_TO_POINTER(3));
    g_free(g_camera_pending);
    g_camera_pending = g_strdup(origin);
    g_free(origin);
    return -1;
}
