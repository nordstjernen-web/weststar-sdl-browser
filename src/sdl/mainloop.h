/* Weststar SDL Browser — the main thread's loop: the default NsLoop and SDL events together.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_SDL_MAINLOOP_H
#define NS_SDL_MAINLOOP_H

#include "plat/ns_sdl.h"
#include <glib.h>
#include "plat/ns_loop.h"

G_BEGIN_DECLS

/* Handles one SDL event; returns FALSE to leave the loop. */
typedef gboolean (*NsSdlEventFunc)(const SDL_Event *ev, gpointer user_data);
/* Called once per loop turn after events, to present whatever changed. */
typedef void (*NsSdlIdleFunc)(gpointer user_data);

/* The SDL event type that only wakes the loop; registered on first use. */
Uint32 ns_sdl_wake_event_type(void);

/* Wakes the main loop from any thread; the default NsLoop calls it for
 * every source added from another thread. */
void ns_sdl_wake(void);

/* An idle on the default NsLoop, so on the main thread; callable from any
 * thread. */
unsigned ns_sdl_idle_add(NsSourceFunc fn, void *data);

/* Runs until on_event returns FALSE or ns_sdl_loop_quit() is called. */
void ns_sdl_loop_run(NsSdlEventFunc on_event, NsSdlIdleFunc on_idle,
                     gpointer user_data);
void ns_sdl_loop_quit(void);

G_END_DECLS

#endif
