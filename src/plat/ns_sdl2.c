/* Weststar SDL Browser — what SDL3 has and SDL2 lacks, for SDL2 builds.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * See ns_sdl.h. Compiled only with NS_SDL2: files and folders, calendar
 * time and a few small things, over POSIX or Win32. Paths are UTF-8, as
 * SDL3's are.
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

/* ---- thread-local values and once-only initialisation ---- */

void *
ns_sdl2_GetTLS(NsSdl2TlsId *id)
{
    int key = SDL_AtomicGet(&id->key);
    return key ? SDL_TLSGet((unsigned int)key) : NULL;
}

bool
ns_sdl2_SetTLS(NsSdl2TlsId *id, const void *value,
               SDL_TLSDestructorCallback destructor)
{
    int key = SDL_AtomicGet(&id->key);
    if (!key) {
        /* SDL2's keys cannot be given back; a key lost to another thread
         * making one at the same moment stays unused. */
        int made = (int)SDL_TLSCreate();
        if (!made) return false;
        SDL_AtomicCAS(&id->key, 0, made);
        key = SDL_AtomicGet(&id->key);
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

bool
ns_sdl2_TimeToDateTime(SDL_Time ticks, SDL_DateTime *dt, bool local)
{
    Sint64 secs = ticks / SDL_NS_PER_SECOND;
    Sint64 ns = ticks % SDL_NS_PER_SECOND;
    if (ns < 0) {
        ns += SDL_NS_PER_SECOND;
        secs--;
    }
    time_t t = (time_t)secs;
    if ((Sint64)t != secs) return false;
    struct tm tm;
#ifdef _WIN32
    struct tm *got = local ? localtime(&t) : gmtime(&t);
    if (!got) return false;
    tm = *got;
#else
    if (!(local ? localtime_r(&t, &tm) : gmtime_r(&t, &tm))) return false;
#endif
    dt->year = tm.tm_year + 1900;
    dt->month = tm.tm_mon + 1;
    dt->day = tm.tm_mday;
    dt->hour = tm.tm_hour;
    dt->minute = tm.tm_min;
    dt->second = tm.tm_sec;
    dt->nanosecond = (int)ns;
    dt->day_of_week = tm.tm_wday;
    /* The fields read as UTC, less the time they stand for. */
    Sint64 as_utc = days_from_civil(dt->year, dt->month, dt->day) * 86400 +
                    dt->hour * 3600 + dt->minute * 60 + dt->second;
    dt->utc_offset = local ? (int)(as_utc - secs) : 0;
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

bool
ns_sdl2_CreateDirectory(const char *path)
{
    wchar_t *w = wide(path);
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

bool
ns_sdl2_CreateDirectory(const char *path)
{
    struct stat st;
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

#else /* SDL3 */

/* A translation unit may not be empty. */
typedef int ns_sdl2_unused;

#endif
