/* Weststar SDL Browser — the browser application: SDL windows, each with chrome and one page.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_SDL_WINDOW_H
#define NS_SDL_WINDOW_H

#include <glib.h>

G_BEGIN_DECLS

/* The running executable's absolute path (appmain.c). */
const char *ns_app_self_exe(void);

/* The size of the first window, from --window-size. */
void ns_sdl_app_set_window_size(int width, int height);

/* Runs the browser until its last window closes; returns the exit status. */
int  ns_sdl_app_run(const char *startup_url, const char *session_path,
                    gboolean recover, gboolean private_mode, gboolean chromeless_mode);

G_END_DECLS

#endif
