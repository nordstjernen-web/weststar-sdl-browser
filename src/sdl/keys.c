/* Weststar SDL Browser — SDL keyboard events as DOM KeyboardEvent key, code and keyCode.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "keys.h"

int
ns_sdl_dom_mods(SDL_Keymod mod)
{
    return ((mod & SDL_KMOD_SHIFT) ? 1 : 0) |
           ((mod & SDL_KMOD_CTRL)  ? 2 : 0) |
           ((mod & SDL_KMOD_ALT)   ? 4 : 0) |
           ((mod & SDL_KMOD_GUI)   ? 8 : 0);
}

typedef struct {
    SDL_Scancode sc;
    const char  *code;
} code_entry;

static const code_entry k_codes[] = {
    { SDL_SCANCODE_RETURN,       "Enter" },
    { SDL_SCANCODE_ESCAPE,       "Escape" },
    { SDL_SCANCODE_BACKSPACE,    "Backspace" },
    { SDL_SCANCODE_TAB,          "Tab" },
    { SDL_SCANCODE_SPACE,        "Space" },
    { SDL_SCANCODE_MINUS,        "Minus" },
    { SDL_SCANCODE_EQUALS,       "Equal" },
    { SDL_SCANCODE_LEFTBRACKET,  "BracketLeft" },
    { SDL_SCANCODE_RIGHTBRACKET, "BracketRight" },
    { SDL_SCANCODE_BACKSLASH,    "Backslash" },
    { SDL_SCANCODE_NONUSHASH,    "Backslash" },
    { SDL_SCANCODE_SEMICOLON,    "Semicolon" },
    { SDL_SCANCODE_APOSTROPHE,   "Quote" },
    { SDL_SCANCODE_GRAVE,        "Backquote" },
    { SDL_SCANCODE_COMMA,        "Comma" },
    { SDL_SCANCODE_PERIOD,       "Period" },
    { SDL_SCANCODE_SLASH,        "Slash" },
    { SDL_SCANCODE_CAPSLOCK,     "CapsLock" },
    { SDL_SCANCODE_PRINTSCREEN,  "PrintScreen" },
    { SDL_SCANCODE_SCROLLLOCK,   "ScrollLock" },
    { SDL_SCANCODE_PAUSE,        "Pause" },
    { SDL_SCANCODE_INSERT,       "Insert" },
    { SDL_SCANCODE_HOME,         "Home" },
    { SDL_SCANCODE_PAGEUP,       "PageUp" },
    { SDL_SCANCODE_DELETE,       "Delete" },
    { SDL_SCANCODE_END,          "End" },
    { SDL_SCANCODE_PAGEDOWN,     "PageDown" },
    { SDL_SCANCODE_RIGHT,        "ArrowRight" },
    { SDL_SCANCODE_LEFT,         "ArrowLeft" },
    { SDL_SCANCODE_DOWN,         "ArrowDown" },
    { SDL_SCANCODE_UP,           "ArrowUp" },
    { SDL_SCANCODE_NUMLOCKCLEAR, "NumLock" },
    { SDL_SCANCODE_KP_DIVIDE,    "NumpadDivide" },
    { SDL_SCANCODE_KP_MULTIPLY,  "NumpadMultiply" },
    { SDL_SCANCODE_KP_MINUS,     "NumpadSubtract" },
    { SDL_SCANCODE_KP_PLUS,      "NumpadAdd" },
    { SDL_SCANCODE_KP_ENTER,     "NumpadEnter" },
    { SDL_SCANCODE_KP_PERIOD,    "NumpadDecimal" },
    { SDL_SCANCODE_KP_EQUALS,    "NumpadEqual" },
    { SDL_SCANCODE_NONUSBACKSLASH, "IntlBackslash" },
    { SDL_SCANCODE_APPLICATION,  "ContextMenu" },
    { SDL_SCANCODE_LCTRL,        "ControlLeft" },
    { SDL_SCANCODE_LSHIFT,       "ShiftLeft" },
    { SDL_SCANCODE_LALT,         "AltLeft" },
    { SDL_SCANCODE_LGUI,         "MetaLeft" },
    { SDL_SCANCODE_RCTRL,        "ControlRight" },
    { SDL_SCANCODE_RSHIFT,       "ShiftRight" },
    { SDL_SCANCODE_RALT,         "AltRight" },
    { SDL_SCANCODE_RGUI,         "MetaRight" },
};

const char *
ns_sdl_dom_code(SDL_Scancode sc)
{
    static char buf[16];
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) {
        g_snprintf(buf, sizeof buf, "Key%c", 'A' + (int)(sc - SDL_SCANCODE_A));
        return buf;
    }
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) {
        g_snprintf(buf, sizeof buf, "Digit%c", '1' + (int)(sc - SDL_SCANCODE_1));
        return buf;
    }
    if (sc == SDL_SCANCODE_0)
        return "Digit0";
    if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12) {
        g_snprintf(buf, sizeof buf, "F%d", 1 + (int)(sc - SDL_SCANCODE_F1));
        return buf;
    }
    if (sc >= SDL_SCANCODE_KP_1 && sc <= SDL_SCANCODE_KP_9) {
        g_snprintf(buf, sizeof buf, "Numpad%c", '1' + (int)(sc - SDL_SCANCODE_KP_1));
        return buf;
    }
    if (sc == SDL_SCANCODE_KP_0)
        return "Numpad0";
    for (gsize i = 0; i < G_N_ELEMENTS(k_codes); i++)
        if (k_codes[i].sc == sc)
            return k_codes[i].code;
    return "";
}

typedef struct {
    SDL_Keycode key;
    const char *name;
    int         keycode;
} key_entry;

static const key_entry k_keys[] = {
    { SDLK_RETURN,      "Enter",       13 },
    { SDLK_KP_ENTER,    "Enter",       13 },
    { SDLK_ESCAPE,      "Escape",      27 },
    { SDLK_BACKSPACE,   "Backspace",   8 },
    { SDLK_TAB,         "Tab",         9 },
    { SDLK_DELETE,      "Delete",      46 },
    { SDLK_INSERT,      "Insert",      45 },
    { SDLK_HOME,        "Home",        36 },
    { SDLK_END,         "End",         35 },
    { SDLK_PAGEUP,      "PageUp",      33 },
    { SDLK_PAGEDOWN,    "PageDown",    34 },
    { SDLK_LEFT,        "ArrowLeft",   37 },
    { SDLK_UP,          "ArrowUp",     38 },
    { SDLK_RIGHT,       "ArrowRight",  39 },
    { SDLK_DOWN,        "ArrowDown",   40 },
    { SDLK_LSHIFT,      "Shift",       16 },
    { SDLK_RSHIFT,      "Shift",       16 },
    { SDLK_LCTRL,       "Control",     17 },
    { SDLK_RCTRL,       "Control",     17 },
    { SDLK_LALT,        "Alt",         18 },
    { SDLK_RALT,        "Alt",         18 },
    { SDLK_LGUI,        "Meta",        91 },
    { SDLK_RGUI,        "Meta",        92 },
    { SDLK_CAPSLOCK,    "CapsLock",    20 },
    { SDLK_NUMLOCKCLEAR, "NumLock",    144 },
    { SDLK_SCROLLLOCK,  "ScrollLock",  145 },
    { SDLK_PAUSE,       "Pause",       19 },
    { SDLK_PRINTSCREEN, "PrintScreen", 44 },
    { SDLK_APPLICATION, "ContextMenu", 93 },
    { SDLK_MODE,        "AltGraph",    225 },
};

const char *
ns_sdl_dom_key(SDL_Keycode key, char *buf, gsize bufsz)
{
    for (gsize i = 0; i < G_N_ELEMENTS(k_keys); i++)
        if (k_keys[i].key == key)
            return k_keys[i].name;
    if (key >= SDLK_F1 && key <= SDLK_F12) {
        g_snprintf(buf, bufsz, "F%d", 1 + (int)(key - SDLK_F1));
        return buf;
    }
    /* Printable keys' keycodes are their Unicode code points. */
    if (!(key & SDLK_SCANCODE_MASK) && key >= 0x20 && key != 0x7f &&
        g_unichar_validate((gunichar)key) && bufsz >= 8) {
        int len = g_unichar_to_utf8((gunichar)key, buf);
        buf[len] = '\0';
        return buf;
    }
    return "Unidentified";
}

int
ns_sdl_dom_keycode(SDL_Keycode key, SDL_Scancode sc)
{
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z)
        return 65 + (int)(sc - SDL_SCANCODE_A);
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9)
        return 49 + (int)(sc - SDL_SCANCODE_1);
    if (sc == SDL_SCANCODE_0)
        return 48;
    if (key >= SDLK_F1 && key <= SDLK_F12)
        return 112 + (int)(key - SDLK_F1);
    if (key == SDLK_SPACE)
        return 32;
    for (gsize i = 0; i < G_N_ELEMENTS(k_keys); i++)
        if (k_keys[i].key == key)
            return k_keys[i].keycode;
    switch (sc) {
    case SDL_SCANCODE_SEMICOLON:    return 186;
    case SDL_SCANCODE_EQUALS:       return 187;
    case SDL_SCANCODE_COMMA:        return 188;
    case SDL_SCANCODE_MINUS:        return 189;
    case SDL_SCANCODE_PERIOD:       return 190;
    case SDL_SCANCODE_SLASH:        return 191;
    case SDL_SCANCODE_GRAVE:        return 192;
    case SDL_SCANCODE_LEFTBRACKET:  return 219;
    case SDL_SCANCODE_BACKSLASH:    return 220;
    case SDL_SCANCODE_RIGHTBRACKET: return 221;
    case SDL_SCANCODE_APOSTROPHE:   return 222;
    default:                        return 0;
    }
}

gboolean
ns_sdl_key_types_text(SDL_Keycode key)
{
    if (key & SDLK_SCANCODE_MASK)
        return FALSE;
    return key >= 0x20 && key != 0x7f;
}
