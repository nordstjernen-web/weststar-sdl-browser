/* Weststar SDL Browser — a scripted GUI test driver, for comparing what the window shows.
 * NS_GUI_TEST names a script of key presses, wheel turns, waits and window shots.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_GUITEST_H
#define NS_GUITEST_H

#include "plat/ns_sdl.h"

/* Starts the script NS_GUI_TEST names, if any, against this window. */
void ns_guitest_start(SDL_Window *window);

/* Called with each presented frame drawn, before it is shown: writes the
 * window shot the script waits for. */
void ns_guitest_present(SDL_Renderer *renderer);

#endif
