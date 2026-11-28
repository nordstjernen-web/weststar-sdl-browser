/* Weststar SDL Browser — the platform layer on SDL3: environment variables.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "plat/ns_env.h"

#include "plat/ns_sdl.h"

#ifdef _WIN32
#  include <stdlib.h>
#  include <string.h>
#  include <wchar.h>
#  include <windows.h>

static wchar_t *
ns_env_wide(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
    if (n <= 0) return NULL;
    wchar_t *w = SDL_malloc((size_t)n * sizeof *w);
    if (w) MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

/* Sets (value) or removes (NULL) name in the C runtime's environment, which
 * also sets the process environment Win32 calls read. */
static bool
ns_env_crt_put(const char *name, const char *value)
{
    wchar_t *wname = ns_env_wide(name);
    wchar_t *wvalue = ns_env_wide(value ? value : "");
#ifdef _UCRT
    bool ok = wname && wvalue && _wputenv_s(wname, wvalue) == 0;
#else
    /* Windows XP's msvcrt.dll has no _wputenv_s: "name=value" to _wputenv,
     * which copies it ("name=" removes name). */
    bool ok = false;
    if (wname && wvalue) {
        size_t nlen = wcslen(wname), vlen = wcslen(wvalue);
        wchar_t *pair = SDL_malloc((nlen + vlen + 2) * sizeof *pair);
        if (pair) {
            memcpy(pair, wname, nlen * sizeof *pair);
            pair[nlen] = L'=';
            memcpy(pair + nlen + 1, wvalue, (vlen + 1) * sizeof *pair);
            ok = _wputenv(pair) == 0;
            SDL_free(pair);
        }
    }
#endif
    SDL_free(wname);
    SDL_free(wvalue);
    return ok;
}
#endif

static bool
ns_env_name_ok(const char *name)
{
    return name && *name && !SDL_strchr(name, '=');
}

bool
ns_setenv(const char *name, const char *value, bool overwrite)
{
    if (!ns_env_name_ok(name) || !value) return false;
    /* Takes SDL's copy first, if it is not taken yet, so the C library's
     * value below cannot be missed by or doubled into it. */
#ifndef NS_SDL2
    SDL_Environment *env = SDL_GetEnvironment();
#endif
    if (!overwrite && SDL_getenv(name)) return true;
#ifdef _WIN32
    if (!ns_env_crt_put(name, value)) return false;
#else
    if (SDL_setenv_unsafe(name, value, 1) != 0) return false;
#endif
#ifdef NS_SDL2
    /* SDL2 keeps no copy of the environment: the C library's is the one. */
    return true;
#else
    return env ? SDL_SetEnvironmentVariable(env, name, value, true) : true;
#endif
}

void
ns_unsetenv(const char *name)
{
    if (!ns_env_name_ok(name)) return;
#ifndef NS_SDL2
    SDL_Environment *env = SDL_GetEnvironment();
#endif
#ifdef _WIN32
    (void)ns_env_crt_put(name, NULL);
#else
    (void)SDL_unsetenv_unsafe(name);
#endif
#ifndef NS_SDL2
    if (env) (void)SDL_UnsetEnvironmentVariable(env, name);
#endif
}
