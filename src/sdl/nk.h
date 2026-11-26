/* Weststar SDL Browser — Nuklear with the configuration every user of it must share.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_SDL_NK_H
#define NS_SDL_NK_H

/* No font baking and no vertex output: the chrome measures and draws text
 * with the engine's text stack and renders Nuklear's command list itself. */
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_BOOL
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR

#include <nuklear.h>

#endif
