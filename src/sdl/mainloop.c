/* Weststar SDL Browser — the main thread's loop: the default NsLoop and SDL events together.
 * The NsLoop is iterated without blocking; the thread then sleeps in SDL until its next
 * timeout, an input event, or a wake from another thread.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "mainloop.h"
#include "plat/ns_plat.h"

/* The longest the loop sleeps without a wake: state other threads change
 * without posting to the loop is still noticed within this many ms. */
#define NS_SDL_MAX_SLEEP_MS 100

static Uint32 g_wake_type;
static gint   g_wake_pending;
static gboolean g_quit;

Uint32
ns_sdl_wake_event_type(void)
{
    static NsOnce once;
    if (ns_once_enter(&once)) {
        g_wake_type = SDL_RegisterEvents(1);
        ns_once_leave(&once);
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

unsigned
ns_sdl_idle_add(NsSourceFunc fn, void *data)
{
    /* An addition from another thread wakes the default loop, which on the
     * main thread means ns_sdl_wake(). */
    return ns_loop_idle_add(ns_loop_default(), fn, data);
}

void
ns_sdl_loop_quit(void)
{
    g_quit = TRUE;
    ns_sdl_wake();
}

void
ns_sdl_loop_run(NsSdlEventFunc on_event, NsSdlIdleFunc on_idle,
                gpointer user_data)
{
    Uint32 wake_type = ns_sdl_wake_event_type();
    NsLoop *loop = ns_loop_default();
    ns_loop_set_wake_func(loop, ns_sdl_wake);
    g_quit = FALSE;
    while (!g_quit) {
        /* Bounded, so a source that rearms itself at once cannot keep
         * input waiting. */
        for (int i = 0; i < 32 && !g_quit; i++) {
            if (!ns_loop_iterate(loop, false))
                break;
        }
        if (g_quit)
            break;
        if (on_idle)
            on_idle(user_data);
        int timeout = ns_loop_next_timeout_ms(loop);
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
    ns_loop_set_wake_func(loop, NULL);
}
