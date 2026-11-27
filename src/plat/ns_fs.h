/* Weststar SDL Browser — the platform layer: the user's folders, files,
 * directories and paths, built on SDL3.
 *
 * It stands in for GLib's g_get_user_*_dir, g_file_get/set_contents,
 * g_mkdir_with_parents, g_file_test, GDir, g_stat and gstdio's
 * g_unlink/g_remove/g_rmdir/g_rename/g_chmod/g_fopen, with the same shapes
 * and return conventions (0 or -1 where GLib follows POSIX, true or false
 * where it returns a gboolean), so call sites convert one for one. Paths are
 * UTF-8 on every platform, as SDL's are. After a failure, ns_fs_last_error()
 * says what went wrong (SDL's error string, kept per thread).
 *
 * The user's folders keep the places GLib gave them, so existing profiles
 * stay where they are:
 *
 * - Linux, the BSDs and other Unix systems follow the XDG base directory
 *   rules exactly as GLib applies them: $XDG_CONFIG_HOME, $XDG_DATA_HOME and
 *   $XDG_CACHE_HOME when set and not empty, else ~/.config, ~/.local/share
 *   and ~/.cache; $XDG_RUNTIME_DIR, else (as GLib) the cache folder, created
 *   0700 if it is missing.
 * - Windows also honours the XDG variables first, as GLib does, then uses the
 *   shell folders GLib uses: Local AppData (C:\Users\<user>\AppData\Local)
 *   for both config and data, the Temporary Internet Files folder
 *   (...\AppData\Local\Microsoft\Windows\INetCache) for the cache and, with
 *   no XDG_RUNTIME_DIR, the runtime folder. They are looked up with
 *   SHGetFolderPathW, which (unlike SHGetKnownFolderPath) Windows XP has.
 * - Everywhere else (macOS, the Amiga family, ...) GLib's locations were
 *   never used by Weststar, so the folders are SDL's: config, data and cache
 *   are "config", "data" and "cache" in SDL_GetPrefPath("Weststar",
 *   "SDL Browser"), and the runtime folder is the cache folder.
 *
 * The home folder is SDL_GetUserFolder(SDL_FOLDER_HOME) without its trailing
 * separator: $HOME, else the password database, on Unix (as GLib), and the
 * user's profile folder on Windows (GLib preferred $HOME there when it named
 * an existing absolute folder).
 *
 * Folder modes: SDL creates folders and files with the platform's defaults.
 * Where a mode matters (profile folders are 0700, profile files 0600), Unix
 * builds pass it to mkdir and chmod as GLib did; other platforms have no
 * such bits and ignore it.
 *
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_FS_H
#define NS_FS_H

#include <stddef.h>
#include <stdio.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_filesystem.h>

/* ---- The user's folders ----
 * Each is computed once and cached for the life of the process; never NULL. */

const char *ns_fs_user_config_dir(void);  /* g_get_user_config_dir */
const char *ns_fs_user_data_dir(void);    /* g_get_user_data_dir */
const char *ns_fs_user_cache_dir(void);   /* g_get_user_cache_dir */
const char *ns_fs_user_runtime_dir(void); /* g_get_user_runtime_dir */
const char *ns_fs_home_dir(void);         /* g_get_home_dir */

/* What went wrong in the calling thread's last failed ns_fs_ call. */
const char *ns_fs_last_error(void);

/* ---- Whole files ---- */

/* Reads the whole file into *contents (NUL-terminated, the NUL not counted in
 * *length; length may be NULL). The buffer comes from malloc(): free it with
 * free() or g_free() (the same allocator since GLib 2.46). On failure
 * *contents is NULL and false is returned. */
bool ns_fs_get_contents(const char *path, char **contents, size_t *length);

/* Replaces the file with length bytes of data (-1: data is a C string),
 * atomically, as g_file_set_contents does: the bytes go to a new file beside
 * it (path.XXXXXX), flushed to disk on Unix when an older file is replaced,
 * which is then renamed over path (MoveFileEx with MOVEFILE_REPLACE_EXISTING
 * on Windows). A reader sees the old file or the new one, never a part; on
 * failure the temporary file is removed and the old file left as it was. */
bool ns_fs_set_contents(const char *path, const char *data, ptrdiff_t length);
/* As ns_fs_set_contents, with the new file's permissions given (0600 for a
 * file nobody else should read) where the platform has them. */
bool ns_fs_set_contents_mode(const char *path, const char *data,
                             ptrdiff_t length, int mode);

/* The target of a symbolic link, from malloc(), or NULL: when path is not a
 * link, or on a platform without them. */
char *ns_fs_read_link(const char *path);

/* The user's Downloads folder, without a trailing separator, or NULL when
 * the platform does not say: XDG_DOWNLOAD_DIR from the user's
 * user-dirs.dirs on Unix, as GLib read it; SDL's answer elsewhere. */
const char *ns_fs_user_download_dir(void);

/* fopen() with a UTF-8 path on every platform (_wfopen on Windows). */
FILE *ns_fs_fopen(const char *path, const char *mode);

/* ---- Paths ---- */

typedef enum {
    NS_FS_TEST_EXISTS     = 1 << 0,
    NS_FS_TEST_IS_REGULAR = 1 << 1, /* a file (after following symlinks) */
    NS_FS_TEST_IS_DIR     = 1 << 2, /* a folder (after following symlinks) */
    NS_FS_TEST_IS_SYMLINK = 1 << 3, /* a symlink (a reparse point on Windows) */
} NsFsTest;

/* True when any of the tests holds, as g_file_test. */
bool ns_fs_test(const char *path, NsFsTest tests);

typedef struct NsFsStat {
    SDL_PathType type;  /* SDL_PATHTYPE_FILE, _DIRECTORY or _OTHER */
    Uint64       size;  /* bytes */
    Sint64       mtime; /* last modification, seconds since 1970 (UTC) */
} NsFsStat;

/* Fills *st for path, following symlinks; 0, or -1 if it cannot be read. */
int ns_fs_stat(const char *path, NsFsStat *st);

/* Creates path and any missing parents (mode for each on Unix); 0 also when
 * it already is a folder, -1 on failure. */
int ns_fs_mkdir_p(const char *path, int mode);

/* Removes a file, a symlink or an empty folder; 0, or -1 when it failed or
 * there was nothing to remove (as g_unlink, g_remove and g_rmdir). */
int ns_fs_remove(const char *path);
static inline int ns_fs_unlink(const char *path) { return ns_fs_remove(path); }
static inline int ns_fs_rmdir(const char *path) { return ns_fs_remove(path); }

/* Renames, replacing newpath if it exists (on Windows too); 0 or -1. */
int ns_fs_rename(const char *oldpath, const char *newpath);

/* Sets the Unix permission bits; nothing (and 0) on other platforms. */
int ns_fs_chmod(const char *path, int mode);

/* ---- Folder listings (GDir) ---- */

typedef struct NsDir NsDir;

/* Lists the folder's entries; NULL (see ns_fs_last_error) if it cannot. */
NsDir *ns_dir_open(const char *path);
/* The next entry's name (never "." or ".."), or NULL after the last one. The
 * string stays valid until ns_dir_close. */
const char *ns_dir_read_name(NsDir *dir);
void ns_dir_close(NsDir *dir);

#endif /* NS_FS_H */
