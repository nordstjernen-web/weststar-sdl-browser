/* Weststar SDL Browser — SDL keyboard events as DOM KeyboardEvent key, code and keyCode.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_SDL_KEYS_H
#define NS_SDL_KEYS_H

#include "plat/ns_sdl.h"
#include <glib.h>

G_BEGIN_DECLS

/* The engine's modifier bits: 1 Shift, 2 Control, 4 Alt, 8 Meta. */
int ns_sdl_dom_mods(SDL_Keymod mod);

/* KeyboardEvent.code for a physical key ("KeyA", "Digit1", "Enter"); "" if none. */
const char *ns_sdl_dom_code(SDL_Scancode sc);

/* KeyboardEvent.key: the character the key types (into buf), or its name
 * ("ArrowUp", "Enter", "Shift"); "Unidentified" when neither is known. */
const char *ns_sdl_dom_key(SDL_Keycode key, char *buf, gsize bufsz);

/* The legacy KeyboardEvent.keyCode; 0 when unmapped. */
int ns_sdl_dom_keycode(SDL_Keycode key, SDL_Scancode sc);

/* Whether the key, pressed without Ctrl, Alt or GUI, types a character, so
 * a text-input event will follow its key-down event. */
gboolean ns_sdl_key_types_text(SDL_Keycode key);

G_END_DECLS

#endif
