/* Weststar SDL Browser — the platform layer: environment variables, on SDL3.
 *
 * It stands in for g_getenv/g_setenv/g_unsetenv:
 *
 * - ns_getenv reads SDL's copy of the environment (SDL_getenv), which is
 *   safe from any thread and UTF-8 on Windows too. That copy is taken the
 *   first time any SDL environment function runs and then changes only
 *   through SDL, that is through ns_setenv/ns_unsetenv here; a variable
 *   another library sets with putenv() later is not seen. Names match case
 *   sensitively on every platform (GLib's g_getenv ignored case on Windows).
 * - ns_setenv/ns_unsetenv change both SDL's copy and the C library's
 *   environment, so libraries that read getenv() (fontconfig, OpenSSL, curl)
 *   see the new value. Like g_setenv, and like SDL_setenv_unsafe underneath,
 *   they are not safe while other threads read the C library's
 *   environment: call them during start-up, before threads are started.
 *   On Windows the value goes in with _wputenv_s, as GLib's does:
 *   SDL_setenv_unsafe there reaches neither getenv() nor non-ASCII values
 *   intact.
 *
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_ENV_H
#define NS_ENV_H

#include "plat/ns_sdl.h"

/* The variable's value, or NULL; valid until the variable is next changed. */
static inline const char *ns_getenv(const char *name) { return SDL_getenv(name); }

/* Sets name to value (overwrite false keeps an existing value); false on a
 * bad name or value. */
bool ns_setenv(const char *name, const char *value, bool overwrite);

/* Removes name from the environment. */
void ns_unsetenv(const char *name);

#endif /* NS_ENV_H */
