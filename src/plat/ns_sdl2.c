/* Weststar SDL Browser — what SDL3 has and SDL2 lacks, for SDL2 builds.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * See ns_sdl.h. Compiled only with NS_SDL2: files and folders, calendar
 * time and a few small things, over POSIX or Win32; and for the shell,
 * SDL2's events in SDL3's shape, the window's pixel density and scale,
 * the save dialog, and SDL3's audio streams over SDL2's device. Paths are
 * UTF-8, as SDL3's are.
 */

#ifdef NS_SDL2

#include "plat/ns_sdl.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#  include <windows.h>
#  include <commdlg.h>
#  include <SDL_syswm.h>
#else
#  include <dirent.h>
#  include <sys/stat.h>
#  include <sys/time.h>
#  include <unistd.h>
#endif

/* SDL2's own functions of these names are called below. */
#undef SDL_SetError
#undef SDL_GetPreferredLocales
#undef SDL_GetBasePath

/* ---- errors ---- */

bool
ns_sdl2_SetError(const char *fmt, ...)
{
    char text[1024];
    va_list ap;
    va_start(ap, fmt);
    SDL_vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);
    SDL_SetError("%s", text);
    return false;
}

bool
ns_sdl2_OutOfMemory(void)
{
    SDL_Error(SDL_ENOMEM);
    return false;
}

/* ---- strings ---- */

char *
ns_sdl2_strcasestr(const char *haystack, const char *needle)
{
    size_t n = strlen(needle);
    for (; *haystack; haystack++)
        if (SDL_strncasecmp(haystack, needle, n) == 0)
            return (char *)(uintptr_t)haystack;
    return n ? NULL : (char *)(uintptr_t)haystack;
}

char *
ns_sdl2_strndup(const char *s, size_t maxlen)
{
    size_t len = 0;
    while (len < maxlen && s[len]) len++;
    char *copy = SDL_malloc(len + 1);
    if (copy) {
        memcpy(copy, s, len);
        copy[len] = 0;
    }
    return copy;
}

/* Not for secrets: the browser's come from the system's random source. */
Uint32
ns_sdl2_rand_bits(void)
{
    static SDL_atomic_t state;
    Uint32 x = (Uint32)SDL_AtomicGet(&state);
    if (!x) x = (Uint32)SDL_GetPerformanceCounter() | 1u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    SDL_AtomicSet(&state, (int)x);
    return x;
}

int
ns_sdl2_unsetenv(const char *name)
{
#ifdef _WIN32
    return SetEnvironmentVariableA(name, NULL) ? 0 : -1;
#else
    return unsetenv(name);
#endif
}

#ifdef _WIN32
/* Each name's value as last read; a value that changes is kept too, as a
 * caller may still hold it. Few names are read, and they seldom change. */
typedef struct EnvEntry {
    struct EnvEntry *next;
    char *name;
    char *value;
} EnvEntry;

const char *
ns_sdl2_getenv(const char *name)
{
    static SDL_SpinLock lock;
    static EnvEntry *entries;
    if (!name || !*name) return NULL;
    int nlen = MultiByteToWideChar(CP_UTF8, 0, name, -1, NULL, 0);
    if (nlen <= 0) return NULL;
    wchar_t *wname = SDL_malloc((size_t)nlen * sizeof *wname);
    if (!wname) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, name, -1, wname, nlen);
    DWORD n = GetEnvironmentVariableW(wname, NULL, 0);
    char *value = NULL;
    if (n > 0) {
        wchar_t *w = SDL_malloc((size_t)n * sizeof *w);
        if (w && GetEnvironmentVariableW(wname, w, n) < n) {
            int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
            value = len > 0 ? SDL_malloc((size_t)len) : NULL;
            if (value)
                WideCharToMultiByte(CP_UTF8, 0, w, -1, value, len, NULL, NULL);
        }
        SDL_free(w);
    }
    SDL_free(wname);
    if (!value) return NULL;
    SDL_AtomicLock(&lock);
    const char *answer = NULL;
    for (EnvEntry *e = entries; e; e = e->next)
        if (strcmp(e->name, name) == 0 && strcmp(e->value, value) == 0) {
            answer = e->value;
            break;
        }
    if (!answer) {
        EnvEntry *e = SDL_malloc(sizeof *e);
        char *copy = SDL_strdup(name);
        if (e && copy) {
            e->name = copy;
            e->value = value;
            e->next = entries;
            entries = e;
            answer = value;
            value = NULL;
        } else {
            SDL_free(e);
            SDL_free(copy);
        }
    }
    SDL_AtomicUnlock(&lock);
    SDL_free(value);
    return answer;
}
#endif

/* ---- thread-local values and once-only initialisation ---- */

void *
ns_sdl2_GetTLS(NsSdl2TlsId *id)
{
    int key = SDL_AtomicGet(id);
    return key ? SDL_TLSGet((unsigned int)key) : NULL;
}

bool
ns_sdl2_SetTLS(NsSdl2TlsId *id, const void *value,
               SDL_TLSDestructorCallback destructor)
{
    int key = SDL_AtomicGet(id);
    if (!key) {
        /* SDL2's keys cannot be given back; a key lost to another thread
         * making one at the same moment stays unused. */
        int made = (int)SDL_TLSCreate();
        if (!made) return false;
        SDL_AtomicCAS(id, 0, made);
        key = SDL_AtomicGet(id);
    }
    return SDL_TLSSet((unsigned int)key, value, destructor) == 0;
}

bool
ns_sdl2_ShouldInit(SDL_InitState *state)
{
    for (;;) {
        if (SDL_AtomicGet(&state->status) == 2)
            return false;
        if (SDL_AtomicCAS(&state->status, 0, 1))
            return true;
        /* Another thread is at it. */
        SDL_Delay(1);
    }
}

void
ns_sdl2_SetInitialized(SDL_InitState *state, bool initialized)
{
    SDL_AtomicSet(&state->status, initialized ? 2 : 0);
}

/* ---- time ---- */

bool
ns_sdl2_GetCurrentTime(SDL_Time *ticks)
{
#ifdef _WIN32
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    /* 100 ns units since 1601. */
    Uint64 t = ((Uint64)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    *ticks = (SDL_Time)((t - 116444736000000000ULL) * 100u);
#else
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0) return false;
    *ticks = (SDL_Time)tv.tv_sec * SDL_NS_PER_SECOND + (SDL_Time)tv.tv_usec * 1000;
#endif
    return true;
}

/* Days from 1970-01-01 to a date, to find the zone's offset from the
 * fields the C library gives. */
static Sint64
days_from_civil(int year, int month, int day)
{
    Sint64 y = year - (month <= 2);
    Sint64 era = (y >= 0 ? y : y - 399) / 400;
    Sint64 yoe = y - era * 400;
    Sint64 doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    return era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
}

/* The date of a day counted from 1970-01-01. */
static void
civil_from_days(Sint64 z, int *year, int *month, int *day)
{
    z += 719468;
    Sint64 era = (z >= 0 ? z : z - 146096) / 146097;
    Sint64 doe = z - era * 146097;
    Sint64 yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    Sint64 doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    Sint64 mp = (5 * doy + 2) / 153;
    int m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *day = (int)(doy - (153 * mp + 2) / 5 + 1);
    *month = m;
    *year = (int)(yoe + era * 400 + (m <= 2));
}

/* Seconds east of UTC of the local zone at a time, from the C library;
 * where it knows no such time (Windows, before 1970), the zone's offset
 * at 1970. */
static bool
local_offset(Sint64 secs, int *offset)
{
    time_t t = (time_t)secs;
    if ((Sint64)t != secs) return false;
    struct tm tm;
#ifdef _WIN32
    struct tm *got = localtime(&t);
    if (!got && secs < 0) {
        t = 0;
        got = localtime(&t);
    }
    if (!got) return false;
    tm = *got;
#else
    if (!localtime_r(&t, &tm)) return false;
#endif
    /* The fields read as UTC, less the time they stand for. */
    Sint64 as_utc = days_from_civil(tm.tm_year + 1900, tm.tm_mon + 1,
                                    tm.tm_mday) * 86400 +
                    tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec;
    *offset = (int)(as_utc - (Sint64)t);
    return true;
}

/* Calendar arithmetic for the fields, which C libraries do not all do
 * before 1970; the C library only for the local zone's offset. */
bool
ns_sdl2_TimeToDateTime(SDL_Time ticks, SDL_DateTime *dt, bool local)
{
    Sint64 secs = ticks / SDL_NS_PER_SECOND;
    Sint64 ns = ticks % SDL_NS_PER_SECOND;
    if (ns < 0) {
        ns += SDL_NS_PER_SECOND;
        secs--;
    }
    int offset = 0;
    if (local && !local_offset(secs, &offset)) return false;
    Sint64 at = secs + offset;
    Sint64 days = (at >= 0 ? at : at - 86399) / 86400;
    Sint64 rest = at - days * 86400;
    civil_from_days(days, &dt->year, &dt->month, &dt->day);
    dt->hour = (int)(rest / 3600);
    dt->minute = (int)(rest / 60 % 60);
    dt->second = (int)(rest % 60);
    dt->nanosecond = (int)ns;
    dt->day_of_week = (int)(((days % 7) + 11) % 7); /* 1970-01-01 was a Thursday */
    dt->utc_offset = offset;
    return true;
}

Uint64
ns_sdl2_GetTicksNS(void)
{
    Uint64 count = SDL_GetPerformanceCounter();
    Uint64 freq = SDL_GetPerformanceFrequency();
    if (!freq) return 0;
    return (count / freq) * 1000000000u + (count % freq) * 1000000000u / freq;
}

/* ---- the system ---- */

SDL_Locale **
ns_sdl2_GetPreferredLocales(int *count)
{
    SDL_Locale *list = SDL_GetPreferredLocales();
    int n = 0;
    size_t text = 0;
    for (; list && list[n].language; n++)
        text += strlen(list[n].language) + 1 +
                (list[n].country ? strlen(list[n].country) + 1 : 0);
    /* One block, as SDL3 gives: the pointers, the entries, their text. */
    SDL_Locale **out = SDL_malloc(((size_t)n + 1) * sizeof *out +
                                  (size_t)n * sizeof **out + text);
    if (out) {
        SDL_Locale *entries = (SDL_Locale *)(out + n + 1);
        char *p = (char *)(entries + n);
        for (int i = 0; i < n; i++) {
            out[i] = &entries[i];
            entries[i].language = p;
            p = strcpy(p, list[i].language) + strlen(list[i].language) + 1;
            entries[i].country = NULL;
            if (list[i].country) {
                entries[i].country = p;
                p = strcpy(p, list[i].country) + strlen(list[i].country) + 1;
            }
        }
        out[n] = NULL;
        if (count) *count = n;
    } else if (count) {
        *count = 0;
    }
    SDL_free(list);
    return out;
}

const char *
ns_sdl2_GetBasePath(void)
{
    static SDL_atomic_t lock;
    static char *path;
    /* SDL2 hands out a new copy each time; keep the first. */
    while (!SDL_AtomicCAS(&lock, 0, 1)) SDL_Delay(1);
    if (!path) path = SDL_GetBasePath();
    SDL_AtomicSet(&lock, 0);
    return path;
}

const char *
ns_sdl2_GetUserFolder(SDL_Folder folder)
{
    if (folder != SDL_FOLDER_HOME) {
        ns_sdl2_SetError("SDL2 does not know this folder");
        return NULL;
    }
#ifdef _WIN32
    return SDL_getenv("USERPROFILE");
#else
    return SDL_getenv("HOME");
#endif
}

/* ---- files and folders ---- */

#ifdef _WIN32

static wchar_t *
wide(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0) return NULL;
    wchar_t *w = SDL_malloc((size_t)n * sizeof *w);
    if (w) MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

static SDL_Time
filetime_ns(FILETIME ft)
{
    Uint64 t = ((Uint64)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (SDL_Time)((t - 116444736000000000ULL) * 100u);
}

bool
ns_sdl2_GetPathInfo(const char *path, SDL_PathInfo *info)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    wchar_t *w = wide(path);
    BOOL ok = w && GetFileAttributesExW(w, GetFileExInfoStandard, &data);
    SDL_free(w);
    if (!ok) return ns_sdl2_SetError("could not stat %s", path);
    if (info) {
        info->type = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                         ? SDL_PATHTYPE_DIRECTORY
                     : (data.dwFileAttributes & FILE_ATTRIBUTE_DEVICE)
                         ? SDL_PATHTYPE_OTHER : SDL_PATHTYPE_FILE;
        info->size = ((Uint64)data.nFileSizeHigh << 32) | data.nFileSizeLow;
        info->create_time = filetime_ns(data.ftCreationTime);
        info->modify_time = filetime_ns(data.ftLastWriteTime);
        info->access_time = filetime_ns(data.ftLastAccessTime);
    }
    return true;
}

bool
ns_sdl2_EnumerateDirectory(const char *path,
                           SDL_EnumerateDirectoryCallback callback,
                           void *userdata)
{
    char *pattern = NULL;
    if (SDL_asprintf(&pattern, "%s\\*", path) < 0) return false;
    wchar_t *w = wide(pattern);
    SDL_free(pattern);
    WIN32_FIND_DATAW found;
    HANDLE h = w ? FindFirstFileW(w, &found) : INVALID_HANDLE_VALUE;
    SDL_free(w);
    if (h == INVALID_HANDLE_VALUE)
        return ns_sdl2_SetError("could not list %s", path);
    SDL_EnumerationResult r = SDL_ENUM_CONTINUE;
    do {
        char name[MAX_PATH * 4];
        if (!wcscmp(found.cFileName, L".") || !wcscmp(found.cFileName, L".."))
            continue;
        if (WideCharToMultiByte(CP_UTF8, 0, found.cFileName, -1, name,
                                (int)sizeof name, NULL, NULL) > 0)
            r = callback(userdata, path, name);
    } while (r == SDL_ENUM_CONTINUE && FindNextFileW(h, &found));
    FindClose(h);
    return r != SDL_ENUM_FAILURE;
}

/* As SDL3's, the missing folders above it too. */
bool
ns_sdl2_CreateDirectory(const char *path)
{
    wchar_t *w = wide(path);
    for (wchar_t *c = w ? w + 1 : NULL; c && *c; c++) {
        if ((*c == L'\\' || *c == L'/') && c[-1] != L':' &&
            c[-1] != L'\\' && c[-1] != L'/') {
            wchar_t sep = *c;
            *c = 0;
            (void)CreateDirectoryW(w, NULL);
            *c = sep;
        }
    }
    BOOL ok = w && (CreateDirectoryW(w, NULL) ||
                    GetLastError() == ERROR_ALREADY_EXISTS);
    SDL_free(w);
    return ok ? true : ns_sdl2_SetError("could not create %s", path);
}

bool
ns_sdl2_RemovePath(const char *path)
{
    wchar_t *w = wide(path);
    if (!w) return ns_sdl2_SetError("could not remove %s", path);
    DWORD attrs = GetFileAttributesW(w);
    BOOL ok = attrs == INVALID_FILE_ATTRIBUTES
                  ? GetLastError() == ERROR_FILE_NOT_FOUND ||
                    GetLastError() == ERROR_PATH_NOT_FOUND
              : (attrs & FILE_ATTRIBUTE_DIRECTORY) ? RemoveDirectoryW(w)
                                                    : DeleteFileW(w);
    SDL_free(w);
    return ok ? true : ns_sdl2_SetError("could not remove %s", path);
}

bool
ns_sdl2_RenamePath(const char *oldpath, const char *newpath)
{
    wchar_t *a = wide(oldpath), *b = wide(newpath);
    BOOL ok = a && b && MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING);
    SDL_free(a);
    SDL_free(b);
    return ok ? true : ns_sdl2_SetError("could not rename %s", oldpath);
}

#else /* POSIX */

bool
ns_sdl2_GetPathInfo(const char *path, SDL_PathInfo *info)
{
    struct stat st;
    if (stat(path, &st) != 0)
        return ns_sdl2_SetError("could not stat %s: %s", path, strerror(errno));
    if (info) {
        info->type = S_ISREG(st.st_mode) ? SDL_PATHTYPE_FILE
                   : S_ISDIR(st.st_mode) ? SDL_PATHTYPE_DIRECTORY
                                         : SDL_PATHTYPE_OTHER;
        info->size = (Uint64)st.st_size;
        info->create_time = (SDL_Time)st.st_ctime * SDL_NS_PER_SECOND;
        info->modify_time = (SDL_Time)st.st_mtime * SDL_NS_PER_SECOND;
        info->access_time = (SDL_Time)st.st_atime * SDL_NS_PER_SECOND;
    }
    return true;
}

bool
ns_sdl2_EnumerateDirectory(const char *path,
                           SDL_EnumerateDirectoryCallback callback,
                           void *userdata)
{
    DIR *dir = opendir(path);
    if (!dir)
        return ns_sdl2_SetError("could not list %s: %s", path, strerror(errno));
    SDL_EnumerationResult r = SDL_ENUM_CONTINUE;
    struct dirent *entry;
    while (r == SDL_ENUM_CONTINUE && (entry = readdir(dir)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;
        r = callback(userdata, path, entry->d_name);
    }
    closedir(dir);
    return r != SDL_ENUM_FAILURE;
}

/* As SDL3's, the missing folders above it too. */
bool
ns_sdl2_CreateDirectory(const char *path)
{
    struct stat st;
    char *parents = SDL_strdup(path);
    for (char *c = parents ? parents + 1 : NULL; c && *c; c++) {
        if (*c == '/' && c[-1] != '/') {
            *c = '\0';
            (void)mkdir(parents, 0777);
            *c = '/';
        }
    }
    SDL_free(parents);
    if (mkdir(path, 0777) == 0) return true;
    if (errno == EEXIST && stat(path, &st) == 0 && S_ISDIR(st.st_mode))
        return true;
    return ns_sdl2_SetError("could not create %s: %s", path, strerror(errno));
}

/* Success too when nothing is there, as SDL3's. */
bool
ns_sdl2_RemovePath(const char *path)
{
    if (remove(path) == 0 || errno == ENOENT) return true;
    return ns_sdl2_SetError("could not remove %s: %s", path, strerror(errno));
}

bool
ns_sdl2_RenamePath(const char *oldpath, const char *newpath)
{
    if (rename(oldpath, newpath) == 0) return true;
    return ns_sdl2_SetError("could not rename %s: %s", oldpath, strerror(errno));
}

#endif

/* ==== the shell: window, renderer, events, dialogs, audio ==== */

/* SDL2's own functions and types of these names are used below. */
#undef SDL_Init
#undef SDL_CreateRenderer
#undef SDL_SetTextureBlendMode
#undef SDL_GetMouseState
#undef SDL_SetWindowFullscreen
#undef SDL_GetWindowSizeInPixels
#undef SDL_RegisterEvents
#undef SDL_PollEvent
#undef SDL_WaitEventTimeout
#undef SDL_PushEvent
#undef SDL_Event
#undef SDL_AudioSpec
#undef SDL_ShowCursor

#if !SDL_VERSION_ATLEAST(2, 0, 18)
#  error "Weststar needs SDL 2.0.18 or later"
#endif

/* ---- start-up ---- */

bool
ns_sdl2_Init(Uint32 flags)
{
    /* Sharp on high-DPI Windows displays: the window is in pixels, and the
     * UI is scaled by the display's DPI (ns_sdl2_GetWindowDisplayScale).
     * SDL before 2.24 does not know the hint and leaves Windows to scale
     * the window as a picture. */
    SDL_SetHint("SDL_WINDOWS_DPI_AWARENESS", "permonitorv2");
    /* As SDL3 does: a click that focuses the window is a click too. */
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
#ifdef _WIN32
    /* Direct3D 11 where there is one, as SDL3 draws by default; SDL2's
     * own first choice is Direct3D 9, which SDL2 still falls back to
     * (Windows XP). Below a render_driver setting or SDL_RENDER_DRIVER. */
    SDL_SetHintWithPriority(SDL_HINT_RENDER_DRIVER, "direct3d11",
                            SDL_HINT_DEFAULT);
#endif
    return SDL_Init(flags) == 0;
}

/* ---- windows, displays and the renderer ---- */

bool
ns_sdl2_SetWindowFullscreen(SDL_Window *window, bool fullscreen)
{
    return SDL_SetWindowFullscreen(window, fullscreen
                                   ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) == 0;
}

bool
ns_sdl2_GetWindowSizeInPixels(SDL_Window *window, int *w, int *h)
{
    SDL_Renderer *renderer = SDL_GetRenderer(window);
    if (renderer && SDL_GetRendererOutputSize(renderer, w, h) == 0)
        return true;
#if SDL_VERSION_ATLEAST(2, 26, 0)
    SDL_GetWindowSizeInPixels(window, w, h);
#else
    SDL_GetWindowSize(window, w, h);
#endif
    return true;
}

float
ns_sdl2_GetWindowPixelDensity(SDL_Window *window)
{
    int w = 0, h = 0, pw = 0, ph = 0;
    SDL_GetWindowSize(window, &w, &h);
    ns_sdl2_GetWindowSizeInPixels(window, &pw, &ph);
    return w > 0 && pw > 0 ? (float)pw / (float)w : 1.0f;
}

/* The UI's scale for a display: its DPI over 96 where SDL2 knows it
 * truly (Windows, which answers 96 to a program it scales itself),
 * else 1. SDL2 on X11 gives the screen's physical DPI, not the desktop's
 * setting; NS_UI_SCALE sets the scale there. */
static float
display_dpi_scale(int index)
{
#ifdef _WIN32
    float hdpi = 0;
    if (index >= 0 && SDL_GetDisplayDPI(index, NULL, &hdpi, NULL) == 0 &&
        hdpi > 0)
        return hdpi / 96.0f;
#else
    (void)index;
#endif
    return 1.0f;
}

float
ns_sdl2_GetWindowDisplayScale(SDL_Window *window)
{
    /* Where SDL2 makes the pixels finer than the window (macOS, Wayland),
     * that is the scale, as in SDL3. */
    float density = ns_sdl2_GetWindowPixelDensity(window);
    if (density > 1.0f)
        return density;
    return display_dpi_scale(SDL_GetWindowDisplayIndex(window));
}

float
ns_sdl2_GetDisplayContentScale(SDL_DisplayID id)
{
    return display_dpi_scale((int)id - 1);
}

SDL_SystemTheme
ns_sdl2_GetSystemTheme(void)
{
#ifdef _WIN32
    /* Windows 10 and later; XP has no such setting. */
    HKEY key;
    DWORD value = 1, size = sizeof value;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                      0, KEY_READ, &key) != ERROR_SUCCESS)
        return SDL_SYSTEM_THEME_UNKNOWN;
    LONG r = RegQueryValueExW(key, L"AppsUseLightTheme", NULL, NULL,
                              (LPBYTE)&value, &size);
    RegCloseKey(key);
    if (r != ERROR_SUCCESS)
        return SDL_SYSTEM_THEME_UNKNOWN;
    return value ? SDL_SYSTEM_THEME_LIGHT : SDL_SYSTEM_THEME_DARK;
#else
    return SDL_SYSTEM_THEME_UNKNOWN;
#endif
}

SDL_Renderer *
ns_sdl2_CreateRenderer(SDL_Window *window, const char *name)
{
    int index = -1;
    for (int i = 0; name && i < SDL_GetNumRenderDrivers(); i++) {
        SDL_RendererInfo info;
        if (SDL_GetRenderDriverInfo(i, &info) == 0 &&
            SDL_strcasecmp(info.name, name) == 0)
            index = i;
    }
    return SDL_CreateRenderer(window, index, 0);
}

bool
ns_sdl2_RenderTexture(SDL_Renderer *renderer, SDL_Texture *texture,
                      const SDL_FRect *src, const SDL_FRect *dst)
{
    SDL_Rect s;
    if (src) {
        s.x = (int)src->x;
        s.y = (int)src->y;
        s.w = (int)src->w;
        s.h = (int)src->h;
    }
    return SDL_RenderCopyF(renderer, texture, src ? &s : NULL, dst) == 0;
}

/* SDL3's SDL_RenderTextureAffine: src (or the whole texture) drawn into the
 * parallelogram with corners origin, right (top right) and down (bottom
 * left); a NULL corner is the render target's. The texture's colour and
 * alpha modulation go into the vertex colours, and are set aside for the
 * draw so that no renderer applies them twice. */
bool
ns_sdl2_RenderTextureAffine(SDL_Renderer *renderer, SDL_Texture *texture,
                            const SDL_FRect *src, const SDL_FPoint *origin,
                            const SDL_FPoint *right, const SDL_FPoint *down)
{
    int tw = 0, th = 0, ow = 0, oh = 0;
    if (SDL_QueryTexture(texture, NULL, NULL, &tw, &th) != 0 || tw <= 0 || th <= 0)
        return false;
    if ((!origin || !right || !down) &&
        SDL_GetRendererOutputSize(renderer, &ow, &oh) != 0)
        return false;
    SDL_FPoint o = origin ? *origin : (SDL_FPoint){ 0.0f, 0.0f };
    SDL_FPoint r = right ? *right : (SDL_FPoint){ (float)ow, 0.0f };
    SDL_FPoint d = down ? *down : (SDL_FPoint){ 0.0f, (float)oh };
    SDL_FPoint br = { r.x + d.x - o.x, r.y + d.y - o.y };
    float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
    if (src) {
        u0 = src->x / (float)tw;
        v0 = src->y / (float)th;
        u1 = (src->x + src->w) / (float)tw;
        v1 = (src->y + src->h) / (float)th;
    }
    Uint8 cr = 255, cg = 255, cb = 255, ca = 255;
    SDL_GetTextureColorMod(texture, &cr, &cg, &cb);
    SDL_GetTextureAlphaMod(texture, &ca);
    SDL_Color c = { cr, cg, cb, ca };
    SDL_Vertex v[4] = {
        { o,  c, { u0, v0 } },
        { r,  c, { u1, v0 } },
        { d,  c, { u0, v1 } },
        { br, c, { u1, v1 } },
    };
    static const int idx[6] = { 0, 1, 2, 1, 3, 2 };
    SDL_SetTextureColorMod(texture, 255, 255, 255);
    SDL_SetTextureAlphaMod(texture, 255);
    bool ok = SDL_RenderGeometry(renderer, texture, v, 4, idx, 6) == 0;
    SDL_SetTextureColorMod(texture, cr, cg, cb);
    SDL_SetTextureAlphaMod(texture, ca);
    return ok;
}

#undef SDL_RenderReadPixels /* SDL2's own, below */
SDL_Surface *
ns_sdl2_RenderReadPixels(SDL_Renderer *r, const SDL_Rect *rect)
{
    int w, h;
    if (rect) {
        w = rect->w;
        h = rect->h;
    } else if (SDL_GetRendererOutputSize(r, &w, &h) != 0) {
        return NULL;
    }
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32,
                                                    SDL_PIXELFORMAT_ARGB8888);
    if (!s) return NULL;
    if (SDL_RenderReadPixels(r, rect, SDL_PIXELFORMAT_ARGB8888, s->pixels,
                             s->pitch) != 0) {
        SDL_FreeSurface(s);
        return NULL;
    }
    return s;
}

bool
ns_sdl2_SetTextureBlendMode(SDL_Texture *texture, SDL_BlendMode mode)
{
    if (SDL_SetTextureBlendMode(texture, mode) == 0)
        return true;
    /* A custom mode the renderer cannot do. */
    return SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND) == 0;
}

/* ---- input ---- */

Uint32
ns_sdl2_GetMouseState(float *x, float *y)
{
    int ix = 0, iy = 0;
    Uint32 buttons = SDL_GetMouseState(&ix, &iy);
    if (x) *x = (float)ix;
    if (y) *y = (float)iy;
    return buttons;
}

bool
ns_sdl2_ShowCursor(bool show)
{
    return SDL_ShowCursor(show ? SDL_ENABLE : SDL_DISABLE) >= 0;
}

/* ---- events ---- */

Uint32
ns_sdl2_RegisterEvents(int numevents)
{
    Uint32 first = SDL_RegisterEvents(numevents);
    return first == (Uint32)-1 ? 0 : first;
}

/* The file of the last drop event, which SDL2 hands over to be freed. */
static char *g_drop_data;

static Uint32
window_event_type(Uint8 event)
{
    switch (event) {
    case SDL_WINDOWEVENT_SHOWN:           return SDL_EVENT_WINDOW_SHOWN;
    case SDL_WINDOWEVENT_HIDDEN:          return SDL_EVENT_WINDOW_HIDDEN;
    case SDL_WINDOWEVENT_EXPOSED:         return SDL_EVENT_WINDOW_EXPOSED;
    case SDL_WINDOWEVENT_MOVED:           return SDL_EVENT_WINDOW_MOVED;
    case SDL_WINDOWEVENT_RESIZED:         return SDL_EVENT_WINDOW_RESIZED;
    case SDL_WINDOWEVENT_SIZE_CHANGED:    return SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED;
    case SDL_WINDOWEVENT_MINIMIZED:       return SDL_EVENT_WINDOW_MINIMIZED;
    case SDL_WINDOWEVENT_MAXIMIZED:       return SDL_EVENT_WINDOW_MAXIMIZED;
    case SDL_WINDOWEVENT_RESTORED:        return SDL_EVENT_WINDOW_RESTORED;
    case SDL_WINDOWEVENT_ENTER:           return SDL_EVENT_WINDOW_MOUSE_ENTER;
    case SDL_WINDOWEVENT_LEAVE:           return SDL_EVENT_WINDOW_MOUSE_LEAVE;
    case SDL_WINDOWEVENT_FOCUS_GAINED:    return SDL_EVENT_WINDOW_FOCUS_GAINED;
    case SDL_WINDOWEVENT_FOCUS_LOST:      return SDL_EVENT_WINDOW_FOCUS_LOST;
    case SDL_WINDOWEVENT_CLOSE:           return SDL_EVENT_WINDOW_CLOSE_REQUESTED;
    case SDL_WINDOWEVENT_DISPLAY_CHANGED: return SDL_EVENT_WINDOW_DISPLAY_CHANGED;
    default:                              return 0;
    }
}

/* Where the pointer is over a window, for the events SDL2 gives no
 * position. */
static void
pointer_in_window(Uint32 window_id, float *x, float *y)
{
    int gx = 0, gy = 0, wx = 0, wy = 0;
    SDL_Window *window = SDL_GetWindowFromID(window_id);
    SDL_GetGlobalMouseState(&gx, &gy);
    if (window)
        SDL_GetWindowPosition(window, &wx, &wy);
    *x = (float)(gx - wx);
    *y = (float)(gy - wy);
}

static void
translate(const SDL_Event *in, NsSdl2Event *out)
{
    SDL_zerop(out);
    out->type = in->type;
    switch (in->type) {
    case SDL_WINDOWEVENT:
        out->type = window_event_type(in->window.event);
        out->window.windowID = in->window.windowID;
        out->window.data1 = in->window.data1;
        out->window.data2 = in->window.data2;
        break;
    case SDL_KEYDOWN:
    case SDL_KEYUP:
        out->key.windowID = in->key.windowID;
        out->key.scancode = in->key.keysym.scancode;
        out->key.key = in->key.keysym.sym;
        out->key.mod = (SDL_Keymod)in->key.keysym.mod;
        out->key.down = in->key.state == SDL_PRESSED;
        out->key.repeat = in->key.repeat != 0;
        break;
    case SDL_TEXTINPUT:
        out->text.windowID = in->text.windowID;
        SDL_strlcpy(out->text.text, in->text.text, sizeof out->text.text);
        break;
    case SDL_MOUSEMOTION:
        out->motion.windowID = in->motion.windowID;
        out->motion.state = in->motion.state;
        out->motion.x = (float)in->motion.x;
        out->motion.y = (float)in->motion.y;
        out->motion.xrel = (float)in->motion.xrel;
        out->motion.yrel = (float)in->motion.yrel;
        break;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
        out->button.windowID = in->button.windowID;
        out->button.button = in->button.button;
        out->button.down = in->button.state == SDL_PRESSED;
        out->button.clicks = in->button.clicks;
        out->button.x = (float)in->button.x;
        out->button.y = (float)in->button.y;
        break;
    case SDL_MOUSEWHEEL:
        out->wheel.windowID = in->wheel.windowID;
        out->wheel.x = in->wheel.preciseX;
        out->wheel.y = in->wheel.preciseY;
        out->wheel.direction = (SDL_MouseWheelDirection)in->wheel.direction;
        out->wheel.integer_x = in->wheel.x;
        out->wheel.integer_y = in->wheel.y;
#if SDL_VERSION_ATLEAST(2, 26, 0)
        out->wheel.mouse_x = (float)in->wheel.mouseX;
        out->wheel.mouse_y = (float)in->wheel.mouseY;
#else
        ns_sdl2_GetMouseState(&out->wheel.mouse_x, &out->wheel.mouse_y);
#endif
        break;
    case SDL_DROPBEGIN:
    case SDL_DROPFILE:
    case SDL_DROPTEXT:
    case SDL_DROPCOMPLETE:
        out->drop.windowID = in->drop.windowID;
        pointer_in_window(in->drop.windowID, &out->drop.x, &out->drop.y);
        g_drop_data = in->drop.file;
        out->drop.data = g_drop_data;
        break;
    default:
        if (in->type >= SDL_USEREVENT) {
            out->user.windowID = in->user.windowID;
            out->user.code = in->user.code;
            out->user.data1 = in->user.data1;
            out->user.data2 = in->user.data2;
        }
        break;
    }
}

/* Events are taken on the main thread only, as in SDL3. */
static void
release_last(void)
{
    SDL_free(g_drop_data);
    g_drop_data = NULL;
}

bool
ns_sdl2_PollEvent(NsSdl2Event *event)
{
    SDL_Event in;
    release_last();
    if (!SDL_PollEvent(&in))
        return false;
    translate(&in, event);
    return true;
}

bool
ns_sdl2_WaitEventTimeout(NsSdl2Event *event, Sint32 timeout_ms)
{
    SDL_Event in;
    release_last();
    if (!SDL_WaitEventTimeout(&in, (int)timeout_ms))
        return false;
    translate(&in, event);
    return true;
}

/* For the events the browser sends itself: SDL_EVENT_QUIT and its own. */
bool
ns_sdl2_PushEvent(NsSdl2Event *event)
{
    SDL_Event out;
    SDL_zero(out);
    out.type = event->type;
    if (event->type >= SDL_USEREVENT) {
        out.user.windowID = event->user.windowID;
        out.user.code = event->user.code;
        out.user.data1 = event->user.data1;
        out.user.data2 = event->user.data2;
    } else if (event->type == SDL_KEYDOWN || event->type == SDL_KEYUP) {
        /* Synthetic input, as the GUI test driver sends it. */
        out.key.windowID = event->key.windowID;
        out.key.state = event->key.down ? SDL_PRESSED : SDL_RELEASED;
        out.key.repeat = event->key.repeat;
        out.key.keysym.scancode = event->key.scancode;
        out.key.keysym.sym = event->key.key;
        out.key.keysym.mod = (Uint16)event->key.mod;
    } else if (event->type == SDL_MOUSEWHEEL) {
        out.wheel.windowID = event->wheel.windowID;
        out.wheel.x = event->wheel.integer_x;
        out.wheel.y = event->wheel.integer_y;
        out.wheel.preciseX = event->wheel.x;
        out.wheel.preciseY = event->wheel.y;
        out.wheel.direction = (Uint32)event->wheel.direction;
#if SDL_VERSION_ATLEAST(2, 26, 0)
        out.wheel.mouseX = (Sint32)event->wheel.mouse_x;
        out.wheel.mouseY = (Sint32)event->wheel.mouse_y;
#endif
    } else if (event->type == SDL_EVENT_WINDOW_EXPOSED) {
        out.type = SDL_WINDOWEVENT;
        out.window.windowID = event->window.windowID;
        out.window.event = SDL_WINDOWEVENT_EXPOSED;
    }
    return SDL_PushEvent(&out) == 1;
}

SDL_Window *
ns_sdl2_GetWindowFromEvent(const NsSdl2Event *event)
{
    switch (event->type) {
    case SDL_QUIT:
    case SDL_RENDER_TARGETS_RESET:
    case SDL_RENDER_DEVICE_RESET:
        return NULL;
    default:
        return event->common.windowID
                   ? SDL_GetWindowFromID(event->common.windowID) : NULL;
    }
}

/* ---- the save dialog ---- */

#ifdef _WIN32

/* "Name\0*.a;*.b\0...\0", from SDL3's filters. */
static wchar_t *
dialog_filters(const SDL_DialogFileFilter *filters, int nfilters)
{
    size_t cap = 1;
    for (int i = 0; i < nfilters; i++)
        cap += strlen(filters[i].name) + 1 + 3 * strlen(filters[i].pattern) + 3;
    char *text = SDL_malloc(cap), *p = text;
    if (!text) return NULL;
    for (int i = 0; i < nfilters; i++) {
        p += SDL_snprintf(p, cap - (size_t)(p - text), "%s", filters[i].name) + 1;
        const char *ext = filters[i].pattern;
        while (*ext) {
            size_t n = strcspn(ext, ";");
            p += SDL_snprintf(p, cap - (size_t)(p - text), "%s*.%.*s",
                              ext == filters[i].pattern ? "" : ";", (int)n, ext);
            ext += n + (ext[n] == ';');
        }
        p++;
    }
    *p++ = 0;
    int n = MultiByteToWideChar(CP_UTF8, 0, text, (int)(p - text), NULL, 0);
    wchar_t *w = n > 0 ? SDL_malloc((size_t)n * sizeof *w) : NULL;
    if (w) MultiByteToWideChar(CP_UTF8, 0, text, (int)(p - text), w, n);
    SDL_free(text);
    return w;
}

void
ns_sdl2_ShowSaveFileDialog(SDL_DialogFileCallback callback, void *userdata,
                           SDL_Window *window,
                           const SDL_DialogFileFilter *filters, int nfilters,
                           const char *default_location)
{
    wchar_t file[MAX_PATH * 4] = { 0 };
    if (default_location) {
        wchar_t *w = wide(default_location);
        if (w) {
            wcsncpy(file, w, SDL_arraysize(file) - 1);
            SDL_free(w);
        }
    }
    wchar_t *filter = nfilters > 0 ? dialog_filters(filters, nfilters) : NULL;
    wchar_t *ext = nfilters > 0 ? wide(filters[0].pattern) : NULL;
    if (ext) ext[wcscspn(ext, L";")] = 0;
    OPENFILENAMEW ofn;
    SDL_zero(ofn);
    ofn.lStructSize = sizeof ofn;
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (window && SDL_GetWindowWMInfo(window, &info) &&
        info.subsystem == SDL_SYSWM_WINDOWS)
        ofn.hwndOwner = info.info.win.window;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = (DWORD)SDL_arraysize(file);
    ofn.lpstrDefExt = ext;
    ofn.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST |
                OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
    char chosen[MAX_PATH * 4 * 3];
    const char *list[2] = { NULL, NULL };
    if (GetSaveFileNameW(&ofn) &&
        WideCharToMultiByte(CP_UTF8, 0, file, -1, chosen, (int)sizeof chosen,
                            NULL, NULL) > 0)
        list[0] = chosen;
    SDL_free(filter);
    SDL_free(ext);
    callback(userdata, CommDlgExtendedError() ? NULL : list,
             list[0] ? 0 : -1);
}

#else

/* No dialog: the file goes where it was offered, under a name not taken
 * yet ("page (2).html"). */
void
ns_sdl2_ShowSaveFileDialog(SDL_DialogFileCallback callback, void *userdata,
                           SDL_Window *window,
                           const SDL_DialogFileFilter *filters, int nfilters,
                           const char *default_location)
{
    (void)window;
    (void)filters;
    (void)nfilters;
    const char *list[2] = { NULL, NULL };
    char *path = default_location ? SDL_strdup(default_location) : NULL;
    if (path) {
        const char *slash = strrchr(default_location, '/');
        const char *dot = strrchr(default_location, '.');
        size_t stem = dot && (!slash || dot > slash + 1)
                          ? (size_t)(dot - default_location)
                          : strlen(default_location);
        for (int n = 2; path && ns_sdl2_GetPathInfo(path, NULL) && n < 1000; n++) {
            SDL_free(path);
            path = NULL;
            if (SDL_asprintf(&path, "%.*s (%d)%s", (int)stem, default_location,
                             n, default_location + stem) < 0)
                path = NULL;
        }
        list[0] = path;
    }
    callback(userdata, list, list[0] ? 0 : -1);
    SDL_free(path);
}

#endif

/* ---- audio ---- */

SDL_AudioStream *
ns_sdl2_CreateAudioStream(const NsSdl2AudioSpec *src, const NsSdl2AudioSpec *dst)
{
    return SDL_NewAudioStream(src->format, (Uint8)src->channels, src->freq,
                              dst->format, (Uint8)dst->channels, dst->freq);
}

/* The one device stream: the device and what the browser fills. */
static SDL_AudioDeviceID g_audio_device;
static SDL_AudioStream *g_audio_stream;
static SDL_AudioStreamCallback g_audio_callback;
static void *g_audio_userdata;

/* SDL2 asks for bytes; the browser's callback puts them into the stream,
 * from which SDL2 takes them. */
static void SDLCALL
audio_device_callback(void *userdata, Uint8 *buf, int len)
{
    (void)userdata;
    int have = SDL_AudioStreamAvailable(g_audio_stream);
    if (have < len)
        g_audio_callback(g_audio_userdata, g_audio_stream, len - have, len);
    int got = SDL_AudioStreamGet(g_audio_stream, buf, len);
    if (got < 0) got = 0;
    if (got < len)
        memset(buf + got, 0, (size_t)(len - got));
}

SDL_AudioStream *
ns_sdl2_OpenAudioDeviceStream(SDL_AudioDeviceID devid,
                              const NsSdl2AudioSpec *spec,
                              SDL_AudioStreamCallback callback,
                              void *userdata)
{
    (void)devid;
    if (g_audio_device) {
        ns_sdl2_SetError("an audio device stream is already open");
        return NULL;
    }
    g_audio_stream = ns_sdl2_CreateAudioStream(spec, spec);
    if (!g_audio_stream) return NULL;
    g_audio_callback = callback;
    g_audio_userdata = userdata;
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = spec->freq;
    want.format = spec->format;
    want.channels = (Uint8)spec->channels;
    want.samples = 1024;
    want.callback = audio_device_callback;
    /* No changes allowed: SDL2 converts to what the device plays. The
     * device starts paused, as SDL3's does. */
    g_audio_device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!g_audio_device) {
        SDL_FreeAudioStream(g_audio_stream);
        g_audio_stream = NULL;
        return NULL;
    }
    return g_audio_stream;
}

void
ns_sdl2_DestroyAudioStream(SDL_AudioStream *stream)
{
    if (!stream) return;
    if (stream == g_audio_stream && g_audio_device) {
        SDL_CloseAudioDevice(g_audio_device);
        g_audio_device = 0;
        g_audio_stream = NULL;
    }
    SDL_FreeAudioStream(stream);
}

bool
ns_sdl2_LockAudioStream(SDL_AudioStream *stream)
{
    if (stream != g_audio_stream || !g_audio_device) return false;
    SDL_LockAudioDevice(g_audio_device);
    return true;
}

bool
ns_sdl2_UnlockAudioStream(SDL_AudioStream *stream)
{
    if (stream != g_audio_stream || !g_audio_device) return false;
    SDL_UnlockAudioDevice(g_audio_device);
    return true;
}

bool
ns_sdl2_ResumeAudioStreamDevice(SDL_AudioStream *stream)
{
    if (stream != g_audio_stream || !g_audio_device) return false;
    SDL_PauseAudioDevice(g_audio_device, 0);
    return true;
}

#else /* SDL3 */

/* A translation unit may not be empty. */
typedef int ns_sdl2_unused;

#endif
