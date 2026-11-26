/* Weststar SDL Browser — the main thread's loop: GLib's default context and SDL events together.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_SDL_MAINLOOP_H
#define NS_SDL_MAINLOOP_H

#include <SDL3/SDL.h>
#include <glib.h>

G_BEGIN_DECLS

/* Handles one SDL event; returns FALSE to leave the loop. */
typedef gboolean (*NsSdlEventFunc)(const SDL_Event *ev, gpointer user_data);
/* Called once per loop turn after events, to present whatever changed. */
typedef void (*NsSdlIdleFunc)(gpointer user_data);

/* The SDL event type that only wakes the loop; registered on first use. */
Uint32 ns_sdl_wake_event_type(void);

/* Wakes the main loop from any thread, so work just queued on the
 * default GMainContext runs without waiting for the next timeout. */
void ns_sdl_wake(void);

/* g_idle_add on the default context, waking the main loop: use it from
 * other threads instead of g_idle_add. */
guint ns_sdl_idle_add(GSourceFunc fn, gpointer data);

/* Runs until on_event returns FALSE or ns_sdl_loop_quit() is called. */
void ns_sdl_loop_run(NsSdlEventFunc on_event, NsSdlIdleFunc on_idle,
                     gpointer user_data);
void ns_sdl_loop_quit(void);

G_END_DECLS

#endif
