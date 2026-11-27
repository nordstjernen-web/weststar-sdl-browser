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
 * So far it covers the platform layer and the font finder (src/plat/
 * threads, files, environment and system services; src/fc/); the window,
 * the event loop, audio and child processes are still SDL3's alone. See
 * docs/porting-to-sdl.md, stage 6.6.
 */

#ifndef NS_SDL_H
#define NS_SDL_H

#ifndef NS_SDL2

#include <SDL3/SDL.h>

#else /* SDL2 */

#include <stdbool.h>
/* SDL2's header would rename main() on some platforms; the browser has
 * its own entry point. */
#define SDL_MAIN_HANDLED
#include <SDL.h>

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

#define SDL_GetCurrentThreadID SDL_ThreadID

/* SDL3's key is ready when zero and made on first use; SDL2's is made by
 * a call. The SDL3 shape, over SDL2's key. */
typedef struct NsSdl2TlsId {
    SDL_atomic_t key; /* SDL2's key, or 0 before the first set */
} NsSdl2TlsId;
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

#endif /* SDL2 */

#endif /* NS_SDL_H */
