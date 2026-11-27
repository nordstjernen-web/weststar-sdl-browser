/* Weststar SDL Browser — GLib's clocks on SDL.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * In Weststar's build of GLib, glib/gmain.c is not compiled: the browser
 * has its own event loop (src/plat/ns_loop.c) and uses none of GLib's
 * main loop, sources or child watches, and with gmain.c go its helpers
 * for pipes, polling and wake-ups (glib-unix.c, gpoll.c, gwakeup.c). The
 * two clocks GLib itself reads (for log time stamps and for the timeouts
 * of condition variables) are the only part of gmain.c that is needed,
 * and they are here, from SDL.
 */

#include "config.h"

#include <SDL3/SDL_time.h>
#include <SDL3/SDL_timer.h>

#include "gmain.h"

/* Microseconds since 1970. */
gint64
g_get_real_time (void)
{
  SDL_Time now = 0;
  if (!SDL_GetCurrentTime (&now))
    return 0;
  return (gint64) (now / 1000);
}

/* Microseconds on a clock that does not go back. */
gint64
g_get_monotonic_time (void)
{
  return (gint64) (SDL_GetTicksNS () / 1000);
}
