/* Weststar SDL Browser — a scripted GUI test driver, for comparing what the window shows.
 * NS_GUI_TEST names a script, one command a line:
 *   wait MS        let the browser run for MS milliseconds
 *   key NAME       press and release PageDown, PageUp, End, Home, Down, Up or Space
 *   wheel N        turn the mouse wheel N notches over the page (down if positive)
 *   shot NAME      write the next frame the window shows to NAME.bmp beside the script
 *   quit           close the browser
 * It is a tool for tests (layers against NS_LAYERS=0, say), driven through SDL's
 * event queue as a user would drive the window, and writes nothing but those shots.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "guitest.h"
#include "plat/ns_plat.h"
#include "plat/ns_env.h"
#include "plat/ns_fs.h"
#include "plat/ns_loop.h"

#include <stdlib.h>
#include <string.h>

#include <glib.h>

static struct {
    SDL_Window *window;
    char      **lines;
    int         line;
    char       *dir;
    char       *shot;      /* the shot the next present writes */
    gint64      resume_us;
} g_test;

static void
push_key(SDL_Keycode key, SDL_Scancode scancode)
{
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.windowID = SDL_GetWindowID(g_test.window);
    e.key.key = key;
    e.key.scancode = scancode;
    e.key.down = true;
    SDL_PushEvent(&e);
    e.type = SDL_EVENT_KEY_UP;
    e.key.down = false;
    SDL_PushEvent(&e);
}

static void
push_wheel(int notches)
{
    int w = 0, h = 0;
    SDL_GetWindowSize(g_test.window, &w, &h);
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_EVENT_MOUSE_WHEEL;
    e.wheel.windowID = SDL_GetWindowID(g_test.window);
    e.wheel.y = (float)-notches;
    e.wheel.integer_y = -notches;
    e.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
    e.wheel.mouse_x = (float)w / 2;
    e.wheel.mouse_y = (float)h * 2 / 3;
    SDL_PushEvent(&e);
}

/* Asks for a frame: the window draws itself again when exposed. */
static void
push_expose(void)
{
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_EVENT_WINDOW_EXPOSED;
    e.window.windowID = SDL_GetWindowID(g_test.window);
    SDL_PushEvent(&e);
}

static const struct {
    const char  *name;
    SDL_Keycode  key;
    SDL_Scancode scancode;
} keys[] = {
    { "PageDown", SDLK_PAGEDOWN, SDL_SCANCODE_PAGEDOWN },
    { "PageUp",   SDLK_PAGEUP,   SDL_SCANCODE_PAGEUP },
    { "End",      SDLK_END,      SDL_SCANCODE_END },
    { "Home",     SDLK_HOME,     SDL_SCANCODE_HOME },
    { "Down",     SDLK_DOWN,     SDL_SCANCODE_DOWN },
    { "Up",       SDLK_UP,       SDL_SCANCODE_UP },
    { "Space",    SDLK_SPACE,    SDL_SCANCODE_SPACE },
};

/* Runs commands until one has to wait: for time to pass or a shot. */
static int
step(void *data)
{
    (void)data;
    if (g_test.shot || ns_time_monotonic_us() < g_test.resume_us)
        return NS_SOURCE_CONTINUE;
    while (g_test.lines && g_test.lines[g_test.line]) {
        char *cmd = g_strstrip(g_test.lines[g_test.line++]);
        if (!*cmd || *cmd == '#')
            continue;
        if (g_str_has_prefix(cmd, "wait ")) {
            g_test.resume_us = ns_time_monotonic_us() +
                               g_ascii_strtoll(cmd + 5, NULL, 10) * 1000;
            return NS_SOURCE_CONTINUE;
        }
        if (g_str_has_prefix(cmd, "shot ")) {
            char *name = g_strconcat(g_strstrip(cmd + 5), ".bmp", NULL);
            g_test.shot = g_build_filename(g_test.dir, name, NULL);
            g_free(name);
            push_expose();
            return NS_SOURCE_CONTINUE;
        }
        if (g_str_has_prefix(cmd, "wheel ")) {
            push_wheel((int)g_ascii_strtoll(cmd + 6, NULL, 10));
            continue;
        }
        if (g_str_has_prefix(cmd, "key ")) {
            const char *name = g_strstrip(cmd + 4);
            for (gsize i = 0; i < G_N_ELEMENTS(keys); i++)
                if (strcmp(keys[i].name, name) == 0)
                    push_key(keys[i].key, keys[i].scancode);
            continue;
        }
        if (strcmp(cmd, "quit") == 0) {
            SDL_Event e;
            SDL_zero(e);
            e.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&e);
            break;
        }
        g_printerr("[guitest] unknown command: %s\n", cmd);
    }
    g_strfreev(g_test.lines);
    g_test.lines = NULL;
    return NS_SOURCE_REMOVE;
}

void
ns_guitest_start(SDL_Window *window)
{
    const char *path = ns_getenv("NS_GUI_TEST");
    if (!path || !*path || g_test.window)
        return;
    char *text = NULL;
    if (!ns_fs_get_contents(path, &text, NULL)) {
        g_printerr("[guitest] cannot read %s\n", path);
        return;
    }
    g_test.window = window;
    g_test.lines = g_strsplit(text, "\n", -1);
    g_test.dir = g_path_get_dirname(path);
    g_free(text);
    ns_loop_timeout_add(NULL, 10, step, NULL);
}

void
ns_guitest_present(SDL_Renderer *renderer)
{
    if (!g_test.shot)
        return;
    SDL_Surface *shot = SDL_RenderReadPixels(renderer, NULL);
    if (shot) {
        if (!SDL_SaveBMP(shot, g_test.shot))
            g_printerr("[guitest] cannot write %s: %s\n", g_test.shot,
                       SDL_GetError());
        SDL_DestroySurface(shot);
    }
    g_free(g_test.shot);
    g_test.shot = NULL;
}
