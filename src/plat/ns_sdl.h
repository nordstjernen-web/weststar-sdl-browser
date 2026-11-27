/* Weststar SDL Browser — SDL3, or SDL2 under SDL3's names.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Weststar is written against SDL3. Windows XP and the Amiga family
 * (AmigaOS 4, MorphOS, AROS) have SDL2 only, so a build with NS_SDL2
 * defined uses SDL2 instead, and this header gives it the SDL3 names the
 * code uses: a different spelling of the same thing (SDL_cond for
 * SDL_Condition), a different convention (SDL3's functions answer true
 * for success, SDL2's 0), or something SDL2 lacks, written here or in
 * ns_sdl2.c over the C library and the operating system (files and
 * folders, calendar time, read-write locks).
 *
 * Every source file includes this header in place of SDL's own.
 *
 * It covers the whole browser: the platform layer (threads, files,
 * environment, system services), the font finder, and the shell in
 * src/sdl/: the window and its renderer, events (SDL2's, translated into
 * SDL3's shape as they are taken from the queue), the clipboard, cursors,
 * message boxes, the save dialog, and audio. The watchdog's child process
 * has its own SDL2 side in src/watchdog.c. Build with -Dsdl=sdl2; see
 * docs/porting-to-sdl.md, stage 6.6.
 */

#ifndef NS_SDL_H
#define NS_SDL_H

#ifndef NS_SDL2

#include <SDL3/SDL.h>

/* Which SDL, for the About page. */
#define NS_SDL_NAME "SDL3"

#else /* SDL2 */

#include <stdbool.h>
/* SDL2's header would rename main() on some platforms; the browser has
 * its own entry point. */
#define SDL_MAIN_HANDLED
#include <SDL.h>

#define NS_SDL_NAME "SDL2"

/* SDL3's version number, of the SDL2 that runs. */
static inline int ns_sdl2_GetVersion(void)
{
    SDL_version v;
    SDL_GetVersion(&v);
    return v.major * 1000000 + v.minor * 1000 + v.patch;
}
#define SDL_GetVersion ns_sdl2_GetVersion
#define SDL_VERSIONNUM_MAJOR(v) ((v) / 1000000)
#define SDL_VERSIONNUM_MINOR(v) (((v) / 1000) % 1000)
#define SDL_VERSIONNUM_MICRO(v) ((v) % 1000)

/* ---- errors: SDL3's answer false, SDL2's -1 ---- */

bool ns_sdl2_SetError(SDL_PRINTF_FORMAT_STRING const char *fmt, ...)
    SDL_PRINTF_VARARG_FUNC(1);
bool ns_sdl2_OutOfMemory(void);
#define SDL_SetError ns_sdl2_SetError
#undef SDL_OutOfMemory
#define SDL_OutOfMemory ns_sdl2_OutOfMemory

/* ---- strings and memory ---- */

#define SDL_strtok_r SDL_strtokr
#define SDL_strcasestr ns_sdl2_strcasestr
#define SDL_strndup ns_sdl2_strndup
#define SDL_rand_bits ns_sdl2_rand_bits
#define SDL_setenv_unsafe SDL_setenv
#define SDL_unsetenv_unsafe ns_sdl2_unsetenv
#ifdef _WIN32
/* SDL2's getenv on Windows answers in one buffer that the next call
 * overwrites, in the ANSI code page; this one's answers stay, in UTF-8. */
const char *ns_sdl2_getenv(const char *name);
#define SDL_getenv ns_sdl2_getenv
#endif
char *ns_sdl2_strcasestr(const char *haystack, const char *needle);
char *ns_sdl2_strndup(const char *s, size_t maxlen);
Uint32 ns_sdl2_rand_bits(void);
int ns_sdl2_unsetenv(const char *name);

/* ---- atomics ---- */

typedef SDL_atomic_t SDL_AtomicInt;
#define SDL_SetAtomicInt SDL_AtomicSet
#define SDL_GetAtomicInt SDL_AtomicGet
#define SDL_AddAtomicInt SDL_AtomicAdd
#define SDL_CompareAndSwapAtomicInt SDL_AtomicCAS
#define SDL_SetAtomicPointer SDL_AtomicSetPtr
#define SDL_GetAtomicPointer SDL_AtomicGetPtr
#define SDL_CompareAndSwapAtomicPointer SDL_AtomicCASPtr

/* ---- locks ---- */

typedef SDL_mutex SDL_Mutex;
typedef SDL_cond SDL_Condition;
#define SDL_TryLockMutex(m) (SDL_TryLockMutex(m) == 0)
#define SDL_CreateCondition SDL_CreateCond
#define SDL_DestroyCondition SDL_DestroyCond
#define SDL_SignalCondition SDL_CondSignal
#define SDL_BroadcastCondition SDL_CondBroadcast
#define SDL_WaitCondition SDL_CondWait
#define SDL_WaitConditionTimeout(c, m, ms) (SDL_CondWaitTimeout((c), (m), (Uint32)(ms)) == 0)

/* SDL2 has no read-write lock: one mutex, which a holder may lock again,
 * lets in one reader or writer at a time. Correct, with less going on at
 * once. */
typedef SDL_mutex SDL_RWLock;
#define SDL_CreateRWLock SDL_CreateMutex
#define SDL_DestroyRWLock SDL_DestroyMutex
#define SDL_LockRWLockForReading SDL_LockMutex
#define SDL_LockRWLockForWriting SDL_LockMutex
#define SDL_UnlockRWLock SDL_UnlockMutex
#define SDL_TryLockRWLockForReading(l) (SDL_TryLockMutex(l))
#define SDL_TryLockRWLockForWriting(l) (SDL_TryLockMutex(l))

/* ---- threads and thread-local values ---- */

/* SDL2's function of the thread's ID has SDL3's name of its type. */
static inline SDL_threadID ns_sdl2_GetCurrentThreadID(void) { return SDL_ThreadID(); }
#define SDL_GetCurrentThreadID ns_sdl2_GetCurrentThreadID
#define SDL_ThreadID SDL_threadID

/* SDL3's key is ready when zero and made on first use; SDL2's is made by
 * a call. The SDL3 shape, over SDL2's key. */
typedef SDL_atomic_t NsSdl2TlsId; /* SDL2's key, or 0 before the first set */
typedef void (SDLCALL *SDL_TLSDestructorCallback)(void *value);
void *ns_sdl2_GetTLS(NsSdl2TlsId *id);
bool ns_sdl2_SetTLS(NsSdl2TlsId *id, const void *value,
                    SDL_TLSDestructorCallback destructor);
#define SDL_TLSID NsSdl2TlsId
#define SDL_GetTLS ns_sdl2_GetTLS
#define SDL_SetTLS ns_sdl2_SetTLS

/* SDL3's once-only initialisation. */
typedef struct SDL_InitState {
    SDL_atomic_t status; /* 0 not done, 1 being done, 2 done */
} SDL_InitState;
bool ns_sdl2_ShouldInit(SDL_InitState *state);
void ns_sdl2_SetInitialized(SDL_InitState *state, bool initialized);
#define SDL_ShouldInit ns_sdl2_ShouldInit
#define SDL_SetInitialized ns_sdl2_SetInitialized

/* ---- time ---- */

typedef Sint64 SDL_Time; /* nanoseconds since 1970 */
typedef struct SDL_DateTime {
    int year, month, day, hour, minute, second, nanosecond;
    int day_of_week; /* 0 is Sunday */
    int utc_offset;  /* seconds east of UTC */
} SDL_DateTime;
#define SDL_NS_PER_SECOND 1000000000LL
#define SDL_SECONDS_TO_NS(s) (((Uint64)(s)) * SDL_NS_PER_SECOND)
#define SDL_NS_TO_SECONDS(ns) ((ns) / SDL_NS_PER_SECOND)
bool ns_sdl2_GetCurrentTime(SDL_Time *ticks);
bool ns_sdl2_TimeToDateTime(SDL_Time ticks, SDL_DateTime *dt, bool local);
Uint64 ns_sdl2_GetTicksNS(void);
#define SDL_GetCurrentTime ns_sdl2_GetCurrentTime
#define SDL_TimeToDateTime ns_sdl2_TimeToDateTime
#define SDL_GetTicksNS ns_sdl2_GetTicksNS
#define SDL_DelayNS(ns) SDL_Delay((Uint32)(((ns) + 999999u) / 1000000u))

/* ---- files and folders: SDL2 has none of this ---- */

typedef enum SDL_PathType {
    SDL_PATHTYPE_NONE,
    SDL_PATHTYPE_FILE,
    SDL_PATHTYPE_DIRECTORY,
    SDL_PATHTYPE_OTHER
} SDL_PathType;
typedef struct SDL_PathInfo {
    SDL_PathType type;
    Uint64       size;
    SDL_Time     create_time, modify_time, access_time;
} SDL_PathInfo;
typedef enum SDL_EnumerationResult {
    SDL_ENUM_CONTINUE,
    SDL_ENUM_SUCCESS,
    SDL_ENUM_FAILURE
} SDL_EnumerationResult;
typedef SDL_EnumerationResult (SDLCALL *SDL_EnumerateDirectoryCallback)(
    void *userdata, const char *dirname, const char *fname);
typedef enum SDL_Folder {
    SDL_FOLDER_HOME,
    SDL_FOLDER_DOWNLOADS
} SDL_Folder;
bool ns_sdl2_GetPathInfo(const char *path, SDL_PathInfo *info);
bool ns_sdl2_EnumerateDirectory(const char *path,
                                SDL_EnumerateDirectoryCallback callback,
                                void *userdata);
bool ns_sdl2_CreateDirectory(const char *path);
bool ns_sdl2_RemovePath(const char *path);
bool ns_sdl2_RenamePath(const char *oldpath, const char *newpath);
const char *ns_sdl2_GetUserFolder(SDL_Folder folder);
const char *ns_sdl2_GetBasePath(void);
#define SDL_GetPathInfo ns_sdl2_GetPathInfo
#define SDL_EnumerateDirectory ns_sdl2_EnumerateDirectory
#define SDL_CreateDirectory ns_sdl2_CreateDirectory
#define SDL_RemovePath ns_sdl2_RemovePath
#define SDL_RenamePath ns_sdl2_RenamePath
#define SDL_GetUserFolder ns_sdl2_GetUserFolder
#define SDL_GetBasePath ns_sdl2_GetBasePath

/* SDL3's IOStream is SDL2's RWops. */
typedef SDL_RWops SDL_IOStream;
#define SDL_IO_STATUS_READY 0
#define SDL_IO_STATUS_ERROR 1
#define SDL_IOFromFile SDL_RWFromFile
#define SDL_ReadIO(io, ptr, size) SDL_RWread((io), (ptr), 1, (size))
#define SDL_WriteIO(io, ptr, size) SDL_RWwrite((io), (ptr), 1, (size))
#define SDL_CloseIO(io) (SDL_RWclose(io) == 0)
#define SDL_GetIOSize SDL_RWsize
/* RWops do not tell a failed read from the end of the file. */
#define SDL_GetIOStatus(io) SDL_IO_STATUS_READY

/* ---- the system ---- */

#define SDL_GetNumLogicalCPUCores SDL_GetCPUCount
/* SDL3's list of pointers, ended by its count, from SDL2's array. */
SDL_Locale **ns_sdl2_GetPreferredLocales(int *count);
#define SDL_GetPreferredLocales ns_sdl2_GetPreferredLocales

/* ---- start-up ---- */

/* SDL2 reads the per-monitor DPI hint before the video subsystem starts;
 * ns_sdl2_Init sets it (and the app's name) first. */
bool ns_sdl2_Init(Uint32 flags);
#define SDL_Init ns_sdl2_Init
static inline bool ns_sdl2_InitSubSystem(Uint32 flags)
{ return SDL_InitSubSystem(flags) == 0; }
#define SDL_InitSubSystem ns_sdl2_InitSubSystem
#define SDL_SetAppMetadata(name, version, id) \
    ((void)(version), (void)(id), SDL_SetHint("SDL_APP_NAME", (name)))
#define SDL_GetTicks SDL_GetTicks64

/* ---- windows, displays and the renderer ---- */

typedef Uint32 SDL_WindowID;
typedef Uint32 SDL_DisplayID; /* SDL2's display index + 1; 0 is none */

#define SDL_WINDOW_HIGH_PIXEL_DENSITY SDL_WINDOW_ALLOW_HIGHDPI
#define SDL_CreateWindow(title, w, h, flags)                             \
    SDL_CreateWindow((title), SDL_WINDOWPOS_UNDEFINED,                   \
                     SDL_WINDOWPOS_UNDEFINED, (w), (h), (flags))
bool ns_sdl2_SetWindowFullscreen(SDL_Window *window, bool fullscreen);
#define SDL_SetWindowFullscreen ns_sdl2_SetWindowFullscreen
bool  ns_sdl2_GetWindowSizeInPixels(SDL_Window *window, int *w, int *h);
float ns_sdl2_GetWindowPixelDensity(SDL_Window *window);
float ns_sdl2_GetWindowDisplayScale(SDL_Window *window);
#define SDL_GetWindowSizeInPixels ns_sdl2_GetWindowSizeInPixels
#define SDL_GetWindowPixelDensity ns_sdl2_GetWindowPixelDensity
#define SDL_GetWindowDisplayScale ns_sdl2_GetWindowDisplayScale
#define SDL_GetPrimaryDisplay() ((SDL_DisplayID)1)
static inline bool ns_sdl2_GetDisplayBounds(SDL_DisplayID id, SDL_Rect *r)
{ return id > 0 && SDL_GetDisplayBounds((int)id - 1, r) == 0; }
#define SDL_GetDisplayBounds ns_sdl2_GetDisplayBounds
float ns_sdl2_GetDisplayContentScale(SDL_DisplayID id);
#define SDL_GetDisplayContentScale ns_sdl2_GetDisplayContentScale

typedef enum SDL_SystemTheme {
    SDL_SYSTEM_THEME_UNKNOWN,
    SDL_SYSTEM_THEME_LIGHT,
    SDL_SYSTEM_THEME_DARK
} SDL_SystemTheme;
SDL_SystemTheme ns_sdl2_GetSystemTheme(void);
#define SDL_GetSystemTheme ns_sdl2_GetSystemTheme

SDL_Renderer *ns_sdl2_CreateRenderer(SDL_Window *window, const char *name);
#define SDL_CreateRenderer ns_sdl2_CreateRenderer
static inline bool ns_sdl2_SetRenderVSync(SDL_Renderer *r, int vsync)
{ return SDL_RenderSetVSync(r, vsync) == 0; }
static inline bool ns_sdl2_SetRenderTarget(SDL_Renderer *r, SDL_Texture *t)
{ return SDL_SetRenderTarget(r, t) == 0; }
static inline bool ns_sdl2_SetRenderClipRect(SDL_Renderer *r, const SDL_Rect *rect)
{ return SDL_RenderSetClipRect(r, rect) == 0; }
static inline bool ns_sdl2_RenderFillRect(SDL_Renderer *r, const SDL_FRect *rect)
{ return SDL_RenderFillRectF(r, rect) == 0; }
static inline bool ns_sdl2_RenderClear(SDL_Renderer *r)
{ return SDL_RenderClear(r) == 0; }
#define SDL_SetRenderVSync ns_sdl2_SetRenderVSync
#define SDL_SetRenderTarget ns_sdl2_SetRenderTarget
#define SDL_SetRenderClipRect ns_sdl2_SetRenderClipRect
#define SDL_RenderFillRect ns_sdl2_RenderFillRect
#define SDL_RenderClear ns_sdl2_RenderClear
bool ns_sdl2_RenderTexture(SDL_Renderer *renderer, SDL_Texture *texture,
                           const SDL_FRect *src, const SDL_FRect *dst);
#define SDL_RenderTexture ns_sdl2_RenderTexture

/* SDL2 has premultiplied blending only as a custom mode, which its
 * software renderer lacks; there the texture blends as straight alpha. */
#define SDL_BLENDMODE_BLEND_PREMULTIPLIED                                  \
    SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE,                        \
                               SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,        \
                               SDL_BLENDOPERATION_ADD, SDL_BLENDFACTOR_ONE, \
                               SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,        \
                               SDL_BLENDOPERATION_ADD)
bool ns_sdl2_SetTextureBlendMode(SDL_Texture *texture, SDL_BlendMode mode);
#define SDL_SetTextureBlendMode ns_sdl2_SetTextureBlendMode
#define SDL_SCALEMODE_NEAREST SDL_ScaleModeNearest
#define SDL_SCALEMODE_LINEAR SDL_ScaleModeLinear

#define SDL_CreateSurface(w, h, format) \
    SDL_CreateRGBSurfaceWithFormat(0, (w), (h), 32, (format))
#define SDL_DestroySurface SDL_FreeSurface

#define SDL_GetRectIntersection SDL_IntersectRect
static inline bool ns_sdl2_GetRectUnion(const SDL_Rect *a, const SDL_Rect *b,
                                        SDL_Rect *out)
{ SDL_UnionRect(a, b, out); return true; }
#define SDL_GetRectUnion ns_sdl2_GetRectUnion

/* ---- input: keys, mouse, cursors, text, clipboard ---- */

#define SDL_KMOD_NONE   KMOD_NONE
#define SDL_KMOD_LSHIFT KMOD_LSHIFT
#define SDL_KMOD_RSHIFT KMOD_RSHIFT
#define SDL_KMOD_LCTRL  KMOD_LCTRL
#define SDL_KMOD_RCTRL  KMOD_RCTRL
#define SDL_KMOD_LALT   KMOD_LALT
#define SDL_KMOD_RALT   KMOD_RALT
#define SDL_KMOD_LGUI   KMOD_LGUI
#define SDL_KMOD_RGUI   KMOD_RGUI
#define SDL_KMOD_NUM    KMOD_NUM
#define SDL_KMOD_CAPS   KMOD_CAPS
#define SDL_KMOD_MODE   KMOD_MODE
#define SDL_KMOD_SHIFT  KMOD_SHIFT
#define SDL_KMOD_CTRL   KMOD_CTRL
#define SDL_KMOD_ALT    KMOD_ALT
#define SDL_KMOD_GUI    KMOD_GUI

/* SDL3 spells the letter keys in capitals; the values are the same. */
#define SDLK_A SDLK_a
#define SDLK_B SDLK_b
#define SDLK_C SDLK_c
#define SDLK_D SDLK_d
#define SDLK_E SDLK_e
#define SDLK_F SDLK_f
#define SDLK_G SDLK_g
#define SDLK_H SDLK_h
#define SDLK_I SDLK_i
#define SDLK_J SDLK_j
#define SDLK_K SDLK_k
#define SDLK_L SDLK_l
#define SDLK_M SDLK_m
#define SDLK_N SDLK_n
#define SDLK_O SDLK_o
#define SDLK_P SDLK_p
#define SDLK_Q SDLK_q
#define SDLK_R SDLK_r
#define SDLK_S SDLK_s
#define SDLK_T SDLK_t
#define SDLK_U SDLK_u
#define SDLK_V SDLK_v
#define SDLK_W SDLK_w
#define SDLK_X SDLK_x
#define SDLK_Y SDLK_y
#define SDLK_Z SDLK_z

Uint32 ns_sdl2_GetMouseState(float *x, float *y);
#define SDL_GetMouseState ns_sdl2_GetMouseState

#define SDL_SYSTEM_CURSOR_DEFAULT     SDL_SYSTEM_CURSOR_ARROW
#define SDL_SYSTEM_CURSOR_TEXT        SDL_SYSTEM_CURSOR_IBEAM
#define SDL_SYSTEM_CURSOR_PROGRESS    SDL_SYSTEM_CURSOR_WAITARROW
#define SDL_SYSTEM_CURSOR_NWSE_RESIZE SDL_SYSTEM_CURSOR_SIZENWSE
#define SDL_SYSTEM_CURSOR_NESW_RESIZE SDL_SYSTEM_CURSOR_SIZENESW
#define SDL_SYSTEM_CURSOR_EW_RESIZE   SDL_SYSTEM_CURSOR_SIZEWE
#define SDL_SYSTEM_CURSOR_NS_RESIZE   SDL_SYSTEM_CURSOR_SIZENS
#define SDL_SYSTEM_CURSOR_MOVE        SDL_SYSTEM_CURSOR_SIZEALL
#define SDL_SYSTEM_CURSOR_NOT_ALLOWED SDL_SYSTEM_CURSOR_NO
#define SDL_SYSTEM_CURSOR_POINTER     SDL_SYSTEM_CURSOR_HAND
/* SDL2 has no cursors for one edge: the two-way ones stand in. */
#define SDL_SYSTEM_CURSOR_NW_RESIZE   SDL_SYSTEM_CURSOR_SIZENWSE
#define SDL_SYSTEM_CURSOR_SE_RESIZE   SDL_SYSTEM_CURSOR_SIZENWSE
#define SDL_SYSTEM_CURSOR_NE_RESIZE   SDL_SYSTEM_CURSOR_SIZENESW
#define SDL_SYSTEM_CURSOR_SW_RESIZE   SDL_SYSTEM_CURSOR_SIZENESW
#define SDL_SYSTEM_CURSOR_N_RESIZE    SDL_SYSTEM_CURSOR_SIZENS
#define SDL_SYSTEM_CURSOR_S_RESIZE    SDL_SYSTEM_CURSOR_SIZENS
#define SDL_SYSTEM_CURSOR_E_RESIZE    SDL_SYSTEM_CURSOR_SIZEWE
#define SDL_SYSTEM_CURSOR_W_RESIZE    SDL_SYSTEM_CURSOR_SIZEWE
#define SDL_DestroyCursor SDL_FreeCursor
bool ns_sdl2_ShowCursor(bool show);
#define SDL_ShowCursor() ns_sdl2_ShowCursor(true)
#define SDL_HideCursor() ns_sdl2_ShowCursor(false)

/* SDL2's text input is on or off for the whole program. */
static inline bool ns_sdl2_StartTextInput(SDL_Window *window)
{ (void)window; SDL_StartTextInput(); return true; }
static inline bool ns_sdl2_StopTextInput(SDL_Window *window)
{ (void)window; SDL_StopTextInput(); return true; }
#define SDL_StartTextInput ns_sdl2_StartTextInput
#define SDL_StopTextInput ns_sdl2_StopTextInput

static inline bool ns_sdl2_SetClipboardText(const char *text)
{ return SDL_SetClipboardText(text) == 0; }
#define SDL_SetClipboardText ns_sdl2_SetClipboardText

/* ---- events: SDL2's, in SDL3's shape ---- */

/* The event types SDL2 has under other names, and the window's own, which
 * SDL2 sends as sub-events of one type; the translation gives them the
 * numbers here, which SDL2 does not use. */
#define SDL_EVENT_QUIT                 SDL_QUIT
#define SDL_EVENT_KEY_DOWN             SDL_KEYDOWN
#define SDL_EVENT_KEY_UP               SDL_KEYUP
#define SDL_EVENT_TEXT_EDITING         SDL_TEXTEDITING
#define SDL_EVENT_TEXT_INPUT           SDL_TEXTINPUT
#define SDL_EVENT_MOUSE_MOTION         SDL_MOUSEMOTION
#define SDL_EVENT_MOUSE_BUTTON_DOWN    SDL_MOUSEBUTTONDOWN
#define SDL_EVENT_MOUSE_BUTTON_UP      SDL_MOUSEBUTTONUP
#define SDL_EVENT_MOUSE_WHEEL          SDL_MOUSEWHEEL
#define SDL_EVENT_DROP_FILE            SDL_DROPFILE
#define SDL_EVENT_DROP_TEXT            SDL_DROPTEXT
#define SDL_EVENT_DROP_BEGIN           SDL_DROPBEGIN
#define SDL_EVENT_DROP_COMPLETE        SDL_DROPCOMPLETE
#define SDL_EVENT_RENDER_TARGETS_RESET SDL_RENDER_TARGETS_RESET
#define SDL_EVENT_RENDER_DEVICE_RESET  SDL_RENDER_DEVICE_RESET
#define SDL_EVENT_USER                 SDL_USEREVENT
enum {
    SDL_EVENT_WINDOW_SHOWN = 0x210,
    SDL_EVENT_WINDOW_HIDDEN,
    SDL_EVENT_WINDOW_EXPOSED,
    SDL_EVENT_WINDOW_MOVED,
    SDL_EVENT_WINDOW_RESIZED,
    SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED,
    SDL_EVENT_WINDOW_MINIMIZED,
    SDL_EVENT_WINDOW_MAXIMIZED,
    SDL_EVENT_WINDOW_RESTORED,
    SDL_EVENT_WINDOW_MOUSE_ENTER,
    SDL_EVENT_WINDOW_MOUSE_LEAVE,
    SDL_EVENT_WINDOW_FOCUS_GAINED,
    SDL_EVENT_WINDOW_FOCUS_LOST,
    SDL_EVENT_WINDOW_CLOSE_REQUESTED,
    SDL_EVENT_WINDOW_DISPLAY_CHANGED,
    /* SDL2 tells neither of these: never sent. */
    SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED,
    SDL_EVENT_SYSTEM_THEME_CHANGED
};

typedef struct NsSdl2CommonEvent {
    Uint32 type;
    SDL_WindowID windowID;
} NsSdl2CommonEvent;
typedef struct NsSdl2WindowEvent {
    Uint32 type;
    SDL_WindowID windowID;
    Sint32 data1, data2;
} NsSdl2WindowEvent;
typedef struct NsSdl2KeyboardEvent {
    Uint32 type;
    SDL_WindowID windowID;
    SDL_Scancode scancode;
    SDL_Keycode key;
    SDL_Keymod mod;
    bool down;
    bool repeat;
} NsSdl2KeyboardEvent;
typedef struct NsSdl2TextInputEvent {
    Uint32 type;
    SDL_WindowID windowID;
    char text[SDL_TEXTINPUTEVENT_TEXT_SIZE];
} NsSdl2TextInputEvent;
typedef struct NsSdl2MouseMotionEvent {
    Uint32 type;
    SDL_WindowID windowID;
    Uint32 state;
    float x, y, xrel, yrel;
} NsSdl2MouseMotionEvent;
typedef struct NsSdl2MouseButtonEvent {
    Uint32 type;
    SDL_WindowID windowID;
    Uint8 button;
    bool down;
    Uint8 clicks;
    float x, y;
} NsSdl2MouseButtonEvent;
typedef struct NsSdl2MouseWheelEvent {
    Uint32 type;
    SDL_WindowID windowID;
    float x, y; /* SDL2's precise amounts */
    SDL_MouseWheelDirection direction;
    float mouse_x, mouse_y;
} NsSdl2MouseWheelEvent;
typedef struct NsSdl2DropEvent {
    Uint32 type;
    SDL_WindowID windowID;
    float x, y; /* where the pointer is: SDL2 does not say */
    const char *source;
    const char *data; /* valid until the next event is taken */
} NsSdl2DropEvent;
typedef struct NsSdl2UserEvent {
    Uint32 type;
    SDL_WindowID windowID;
    Sint32 code;
    void *data1, *data2;
} NsSdl2UserEvent;
typedef union NsSdl2Event {
    Uint32 type;
    NsSdl2CommonEvent common;
    NsSdl2WindowEvent window;
    NsSdl2KeyboardEvent key;
    NsSdl2TextInputEvent text;
    NsSdl2MouseMotionEvent motion;
    NsSdl2MouseButtonEvent button;
    NsSdl2MouseWheelEvent wheel;
    NsSdl2DropEvent drop;
    NsSdl2UserEvent user;
    Uint8 padding[128];
} NsSdl2Event;
#define SDL_Event NsSdl2Event
#define SDL_CommonEvent NsSdl2CommonEvent
#define SDL_WindowEvent NsSdl2WindowEvent
#define SDL_KeyboardEvent NsSdl2KeyboardEvent
#define SDL_TextInputEvent NsSdl2TextInputEvent
#define SDL_MouseMotionEvent NsSdl2MouseMotionEvent
#define SDL_MouseButtonEvent NsSdl2MouseButtonEvent
#define SDL_MouseWheelEvent NsSdl2MouseWheelEvent
#define SDL_DropEvent NsSdl2DropEvent
#define SDL_UserEvent NsSdl2UserEvent

/* Event types an event of another thread may carry: 0 when none are
 * left, as SDL3 answers. */
Uint32 ns_sdl2_RegisterEvents(int numevents);
bool ns_sdl2_PollEvent(NsSdl2Event *event);
bool ns_sdl2_WaitEventTimeout(NsSdl2Event *event, Sint32 timeout_ms);
bool ns_sdl2_PushEvent(NsSdl2Event *event);
SDL_Window *ns_sdl2_GetWindowFromEvent(const NsSdl2Event *event);
#define SDL_RegisterEvents ns_sdl2_RegisterEvents
#define SDL_PollEvent ns_sdl2_PollEvent
#define SDL_WaitEventTimeout ns_sdl2_WaitEventTimeout
#define SDL_PushEvent ns_sdl2_PushEvent
#define SDL_GetWindowFromEvent ns_sdl2_GetWindowFromEvent

/* ---- dialogs ---- */

static inline bool ns_sdl2_ShowMessageBox(const SDL_MessageBoxData *data,
                                          int *buttonid)
{ return SDL_ShowMessageBox(data, buttonid) == 0; }
#define SDL_ShowMessageBox ns_sdl2_ShowMessageBox

typedef struct SDL_DialogFileFilter {
    const char *name;
    const char *pattern; /* extensions, separated by ';' */
} SDL_DialogFileFilter;
typedef void (SDLCALL *SDL_DialogFileCallback)(void *userdata,
                                               const char *const *filelist,
                                               int filter);
/* SDL2 has no file dialogs. On Windows this is the system's Save As
 * dialog; elsewhere the file goes to default_location, under a name not
 * yet taken. The callback is called before this returns. */
void ns_sdl2_ShowSaveFileDialog(SDL_DialogFileCallback callback,
                                void *userdata, SDL_Window *window,
                                const SDL_DialogFileFilter *filters,
                                int nfilters, const char *default_location);
#define SDL_ShowSaveFileDialog ns_sdl2_ShowSaveFileDialog

/* ---- audio: SDL3's streams over SDL2's device and converter ---- */

typedef struct NsSdl2AudioSpec {
    SDL_AudioFormat format;
    int channels;
    int freq;
} NsSdl2AudioSpec;
#define SDL_AudioSpec NsSdl2AudioSpec
#define SDL_AUDIO_F32 AUDIO_F32SYS
#define SDL_AUDIO_S16 AUDIO_S16SYS
#define SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK ((SDL_AudioDeviceID)0)
/* The device's buffer is set when it opens (1024 frames). */
#define SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES "SDL_AUDIO_DEVICE_SAMPLE_FRAMES"
typedef void (SDLCALL *SDL_AudioStreamCallback)(void *userdata,
                                                SDL_AudioStream *stream,
                                                int additional_amount,
                                                int total_amount);
SDL_AudioStream *ns_sdl2_CreateAudioStream(const NsSdl2AudioSpec *src,
                                           const NsSdl2AudioSpec *dst);
void ns_sdl2_DestroyAudioStream(SDL_AudioStream *stream);
/* One device stream at a time: SDL2's device, with the stream the
 * callback fills between SDL2's callback and the device. */
SDL_AudioStream *ns_sdl2_OpenAudioDeviceStream(SDL_AudioDeviceID devid,
                                               const NsSdl2AudioSpec *spec,
                                               SDL_AudioStreamCallback callback,
                                               void *userdata);
bool ns_sdl2_LockAudioStream(SDL_AudioStream *stream);
bool ns_sdl2_UnlockAudioStream(SDL_AudioStream *stream);
bool ns_sdl2_ResumeAudioStreamDevice(SDL_AudioStream *stream);
#define SDL_CreateAudioStream ns_sdl2_CreateAudioStream
#define SDL_DestroyAudioStream ns_sdl2_DestroyAudioStream
#define SDL_OpenAudioDeviceStream ns_sdl2_OpenAudioDeviceStream
#define SDL_LockAudioStream ns_sdl2_LockAudioStream
#define SDL_UnlockAudioStream ns_sdl2_UnlockAudioStream
#define SDL_ResumeAudioStreamDevice ns_sdl2_ResumeAudioStreamDevice
static inline bool ns_sdl2_PutAudioStreamData(SDL_AudioStream *s,
                                              const void *buf, int len)
{ return SDL_AudioStreamPut(s, buf, len) == 0; }
static inline bool ns_sdl2_FlushAudioStream(SDL_AudioStream *s)
{ return SDL_AudioStreamFlush(s) == 0; }
#define SDL_PutAudioStreamData ns_sdl2_PutAudioStreamData
#define SDL_FlushAudioStream ns_sdl2_FlushAudioStream
#define SDL_GetAudioStreamData SDL_AudioStreamGet
#define SDL_GetAudioStreamAvailable SDL_AudioStreamAvailable

#endif /* SDL2 */

#endif /* NS_SDL_H */
