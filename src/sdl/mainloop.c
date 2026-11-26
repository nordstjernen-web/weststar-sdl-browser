/* Weststar SDL Browser — the main thread's loop: GLib's default context and SDL events together.
 * GLib is iterated without blocking; the thread then sleeps in SDL until the next GLib
 * timeout, an input event, or a wake from another thread.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "mainloop.h"

/* The longest the loop sleeps without a wake: GLib work queued from a thread
 * that does not call ns_sdl_wake() still runs within this many ms. */
#define NS_SDL_MAX_SLEEP_MS 100

static Uint32 g_wake_type;
static gint   g_wake_pending;
static gboolean g_quit;

Uint32
ns_sdl_wake_event_type(void)
{
    static gsize once;
    if (g_once_init_enter(&once)) {
        g_wake_type = SDL_RegisterEvents(1);
        g_once_init_leave(&once, 1);
    }
    return g_wake_type;
}

void
ns_sdl_wake(void)
{
    if (!g_atomic_int_compare_and_exchange(&g_wake_pending, 0, 1))
        return;
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = ns_sdl_wake_event_type();
    if (!SDL_PushEvent(&ev))
        g_atomic_int_set(&g_wake_pending, 0);
}

guint
ns_sdl_idle_add(GSourceFunc fn, gpointer data)
{
    guint id = g_idle_add(fn, data);
    ns_sdl_wake();
    return id;
}

void
ns_sdl_loop_quit(void)
{
    g_quit = TRUE;
    ns_sdl_wake();
}

/* One non-blocking GLib iteration on the default context. Returns how long
 * the loop may sleep before GLib has work again: 0 when sources are ready,
 * -1 when GLib has no timeout pending. */
static int
glib_iterate(void)
{
    static GPollFD *fds;
    static gint n_alloc;
    GMainContext *ctx = g_main_context_default();
    if (!g_main_context_acquire(ctx))
        return 4;
    gint priority = 0;
    gint timeout = -1;
    gboolean ready = g_main_context_prepare(ctx, &priority);
    gint n;
    while ((n = g_main_context_query(ctx, priority, &timeout, fds, n_alloc))
           > n_alloc) {
        n_alloc = n;
        fds = g_renew(GPollFD, fds, n_alloc);
    }
    if (n > 0)
        g_poll(fds, (guint)n, 0);
    gboolean dispatch = g_main_context_check(ctx, priority, fds, n);
    if (dispatch)
        g_main_context_dispatch(ctx);
    g_main_context_release(ctx);
    return (ready || dispatch) ? 0 : timeout;
}

void
ns_sdl_loop_run(NsSdlEventFunc on_event, NsSdlIdleFunc on_idle,
                gpointer user_data)
{
    Uint32 wake_type = ns_sdl_wake_event_type();
    g_quit = FALSE;
    while (!g_quit) {
        int timeout = -1;
        /* Bounded, so a source that rearms itself at once cannot keep
         * input waiting. */
        for (int i = 0; i < 32; i++) {
            timeout = glib_iterate();
            if (timeout != 0 || g_quit)
                break;
        }
        if (g_quit)
            break;
        if (on_idle)
            on_idle(user_data);
        if (timeout < 0 || timeout > NS_SDL_MAX_SLEEP_MS)
            timeout = NS_SDL_MAX_SLEEP_MS;
        SDL_Event ev;
        if (!SDL_WaitEventTimeout(&ev, timeout))
            continue;
        do {
            if (ev.type == wake_type) {
                g_atomic_int_set(&g_wake_pending, 0);
                continue;
            }
            if (on_event && !on_event(&ev, user_data)) {
                g_quit = TRUE;
                break;
            }
        } while (SDL_PollEvent(&ev));
    }
}
