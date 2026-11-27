/* Weststar SDL Browser — the platform layer on SDL3: the user's folders,
 * whole-file reads and atomic writes, path tests, folder listings.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "plat/ns_fs.h"
#include "plat/ns_env.h"
#include "plat/ns_plat.h"

#include "brand.h"

#include "plat/ns_sdl.h"
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define NS_FS_WINDOWS 1
#  define NS_FS_SEP '\\'
#  include <io.h>
#  include <windows.h>
#  include <shlobj.h>
#else
#  define NS_FS_SEP '/'
#endif

#if defined(__unix__) || defined(__APPLE__) || defined(__HAIKU__)
#  define NS_FS_POSIX 1
#  include <errno.h>
#  include <fcntl.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

/* The XDG base directories are what GLib used on Linux and the BSDs. */
#if defined(__unix__) && !defined(__APPLE__)
#  define NS_FS_XDG 1
#endif

/* Whether c separates path components: '/' everywhere, and '\' on
 * Windows too. */
static inline bool
ns_fs_is_sep(char c)
{
#ifdef NS_FS_WINDOWS
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

const char *
ns_fs_last_error(void)
{
    return SDL_GetError();
}

#ifdef NS_FS_POSIX
/* Sets SDL's error from errno, which it leaves as it was; false. */
static bool
ns_fs_errno(const char *what, const char *path)
{
    int e = errno;
    SDL_SetError("%s %s: %s", what, path, strerror(e));
    errno = e;
    return false;
}
#endif

#ifdef NS_FS_WINDOWS
/* path as UTF-16, or NULL; free with SDL_free. */
static wchar_t *
ns_fs_wide(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
    if (n <= 0) {
        SDL_SetError("not UTF-8: %s", s);
        return NULL;
    }
    wchar_t *w = SDL_malloc((size_t)n * sizeof *w);
    if (w) MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

static char *
ns_fs_utf8(const wchar_t *w)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (n <= 0) return NULL;
    char *s = SDL_malloc((size_t)n);
    if (s) WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL);
    return s;
}
#endif

/* ---- The user's folders ---- */

static NsOnce g_dirs_once;
static NsOnce g_runtime_once;
static char  *g_home_dir;
static char  *g_config_dir;
static char  *g_data_dir;
static char  *g_cache_dir;
static char  *g_runtime_dir;

/* dir and sub joined with the platform's separator; SDL_malloc'd. */
static char *
ns_fs_join(const char *dir, const char *sub)
{
    size_t n = strlen(dir);
    bool sep = n > 0 && ns_fs_is_sep(dir[n - 1]);
    const char sepstr[2] = { sep ? '\0' : NS_FS_SEP, '\0' };
    char *out = NULL;
    if (SDL_asprintf(&out, "%s%s%s", dir, sepstr, sub) < 0)
        return NULL;
    return out;
}

#if defined(NS_FS_XDG) || defined(NS_FS_WINDOWS)
/* $name when it is set and not empty, as GLib reads the XDG variables. */
static char *
ns_fs_env_dir(const char *name)
{
    const char *v = ns_getenv(name);
    return v && *v ? SDL_strdup(v) : NULL;
}
#endif

/* SDL's home folder without the separator it ends with (but a root stays). */
static char *
ns_fs_find_home(void)
{
    const char *h = SDL_GetUserFolder(SDL_FOLDER_HOME);
#ifdef NS_FS_WINDOWS
    char *home = SDL_strdup(h && *h ? h : "C:\\");
#else
    char *home = SDL_strdup(h && *h ? h : "/");
#endif
    size_t n = strlen(home);
    while (n > 1 && ns_fs_is_sep(home[n - 1]) &&
           !(n == 3 && home[1] == ':'))
        home[--n] = '\0';
    return home;
}

#ifdef NS_FS_WINDOWS
/* A shell folder (CSIDL_*) as UTF-8, or NULL. */
static char *
ns_fs_shell_folder(int csidl)
{
    wchar_t w[MAX_PATH];
    if (SHGetFolderPathW(NULL, csidl, NULL, SHGFP_TYPE_CURRENT, w) != S_OK ||
        !w[0])
        return NULL;
    return ns_fs_utf8(w);
}
#endif

static void
ns_fs_init_dirs(void)
{
    if (!ns_once_enter(&g_dirs_once)) return;
    g_home_dir = ns_fs_find_home();
#if defined(NS_FS_XDG) || defined(NS_FS_WINDOWS)
    /* GLib's order: the XDG variable, then (on Windows) the shell folder,
     * then the XDG default under home. */
    g_config_dir = ns_fs_env_dir("XDG_CONFIG_HOME");
    g_data_dir   = ns_fs_env_dir("XDG_DATA_HOME");
    g_cache_dir  = ns_fs_env_dir("XDG_CACHE_HOME");
#  ifdef NS_FS_WINDOWS
    if (!g_config_dir) g_config_dir = ns_fs_shell_folder(CSIDL_LOCAL_APPDATA);
    if (!g_data_dir)   g_data_dir   = ns_fs_shell_folder(CSIDL_LOCAL_APPDATA);
    if (!g_cache_dir)  g_cache_dir  = ns_fs_shell_folder(CSIDL_INTERNET_CACHE);
#  endif
    if (!g_config_dir) g_config_dir = ns_fs_join(g_home_dir, ".config");
    if (!g_data_dir) {
        char *local = ns_fs_join(g_home_dir, ".local");
        g_data_dir = local ? ns_fs_join(local, "share") : NULL;
        SDL_free(local);
    }
    if (!g_cache_dir) g_cache_dir = ns_fs_join(g_home_dir, ".cache");
#else
    char *pref = SDL_GetPrefPath(NS_BRAND_SHORT, "SDL Browser");
    const char *base = pref ? pref : g_home_dir;
    g_config_dir = ns_fs_join(base, "config");
    g_data_dir   = ns_fs_join(base, "data");
    g_cache_dir  = ns_fs_join(base, "cache");
    SDL_free(pref);
#endif
    if (!g_config_dir) g_config_dir = SDL_strdup(g_home_dir);
    if (!g_data_dir)   g_data_dir   = SDL_strdup(g_home_dir);
    if (!g_cache_dir)  g_cache_dir  = SDL_strdup(g_home_dir);
    ns_once_leave(&g_dirs_once);
}

const char *ns_fs_home_dir(void)        { ns_fs_init_dirs(); return g_home_dir; }
const char *ns_fs_user_config_dir(void) { ns_fs_init_dirs(); return g_config_dir; }
const char *ns_fs_user_data_dir(void)   { ns_fs_init_dirs(); return g_data_dir; }
const char *ns_fs_user_cache_dir(void)  { ns_fs_init_dirs(); return g_cache_dir; }

const char *
ns_fs_user_runtime_dir(void)
{
    if (ns_once_enter(&g_runtime_once)) {
#if defined(NS_FS_XDG) || defined(NS_FS_WINDOWS)
        g_runtime_dir = ns_fs_env_dir("XDG_RUNTIME_DIR");
#endif
        if (!g_runtime_dir) {
            /* GLib's fallback: the cache folder, made sure to exist. */
            g_runtime_dir = SDL_strdup(ns_fs_user_cache_dir());
            (void)ns_fs_mkdir_p(g_runtime_dir, 0700);
        }
        ns_once_leave(&g_runtime_once);
    }
    return g_runtime_dir;
}

/* ---- Whole files ---- */

FILE *
ns_fs_fopen(const char *path, const char *mode)
{
#ifdef NS_FS_WINDOWS
    wchar_t *wpath = ns_fs_wide(path);
    wchar_t *wmode = ns_fs_wide(mode);
    FILE *f = wpath && wmode ? _wfopen(wpath, wmode) : NULL;
    SDL_free(wpath);
    SDL_free(wmode);
    if (!f) SDL_SetError("could not open %s", path);
    return f;
#else
    FILE *f = fopen(path, mode);
#  ifdef NS_FS_POSIX
    if (!f) ns_fs_errno("could not open", path);
#  else
    if (!f) SDL_SetError("could not open %s", path);
#  endif
    return f;
#endif
}

/* The file being read whole: an SDL stream, except on Windows, where SDL
 * opens files without FILE_SHARE_WRITE and so cannot read one that another
 * handle has open for writing (a log, a file a library keeps open), which
 * GLib's reads could. There it is a Win32 handle opened with full sharing,
 * read with ReadFile. (Not stdio: with _FORTIFY_SOURCE and LTO, mingw's
 * checked fread leaves a call to __chk_fail, which it marks as a warning,
 * and the build treats warnings as errors.) */
#ifdef NS_FS_WINDOWS
typedef struct { HANDLE h; } NsFsReader;

static NsFsReader *
ns_fs_reader_open(const char *path)
{
    wchar_t *w = ns_fs_wide(path);
    if (!w) return NULL;
    HANDLE h = CreateFileW(w, GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    SDL_free(w);
    if (h == INVALID_HANDLE_VALUE) {
        SDL_SetError("could not open %s", path);
        return NULL;
    }
    NsFsReader *r = SDL_malloc(sizeof *r);
    if (!r) {
        CloseHandle(h);
        return NULL;
    }
    r->h = h;
    return r;
}

static Sint64
ns_fs_reader_size(NsFsReader *r)
{
    LARGE_INTEGER size;
    return GetFileSizeEx(r->h, &size) ? (Sint64)size.QuadPart : -1;
}

static void
ns_fs_reader_close(NsFsReader *r)
{
    CloseHandle(r->h);
    SDL_free(r);
}

/* Up to n bytes; 0 at the end, or on an error, which sets *failed. */
static size_t
ns_fs_reader_read(NsFsReader *r, char *buf, size_t n, bool *failed)
{
    DWORD want = n > 0x40000000u ? 0x40000000u : (DWORD)n, got = 0;
    if (!ReadFile(r->h, buf, want, &got, NULL)) {
        *failed = !SDL_SetError("could not read the file");
        return 0;
    }
    return got;
}
#else
typedef SDL_IOStream NsFsReader;

static NsFsReader *ns_fs_reader_open(const char *path) { return SDL_IOFromFile(path, "rb"); }
static Sint64 ns_fs_reader_size(NsFsReader *r) { return SDL_GetIOSize(r); }
static void ns_fs_reader_close(NsFsReader *r) { (void)SDL_CloseIO(r); }

static size_t
ns_fs_reader_read(NsFsReader *r, char *buf, size_t n, bool *failed)
{
    size_t got = SDL_ReadIO(r, buf, n);
    if (got == 0 && SDL_GetIOStatus(r) == SDL_IO_STATUS_ERROR) *failed = true;
    return got;
}
#endif

bool
ns_fs_get_contents(const char *path, char **contents, size_t *length)
{
    *contents = NULL;
    if (length) *length = 0;
    NsFsReader *io = ns_fs_reader_open(path);
    if (!io) return false;
    /* The size is a hint: files under /proc report 0, others may grow. */
    Sint64 hint = ns_fs_reader_size(io);
    size_t cap = hint > 0 && (Uint64)hint < SIZE_MAX - 1 ? (size_t)hint + 1 : 4096;
    size_t len = 0;
    char *buf = malloc(cap);
    bool ok = buf != NULL;
    if (!ok) SDL_OutOfMemory();
    while (ok) {
        if (cap - len < 2) {
            size_t ncap = cap * 2;
            char *nbuf = ncap > cap ? realloc(buf, ncap) : NULL;
            if (!nbuf) {
                ok = SDL_OutOfMemory();
                break;
            }
            buf = nbuf;
            cap = ncap;
        }
        bool failed = false;
        size_t n = ns_fs_reader_read(io, buf + len, cap - len - 1, &failed);
        if (n == 0) {
            ok = !failed;
            break;
        }
        len += n;
    }
    ns_fs_reader_close(io);
    if (!ok) {
        free(buf);
        return false;
    }
    buf[len] = '\0';
    *contents = buf;
    if (length) *length = len;
    return true;
}

/* An unused name beside path for the new contents (path.XXXXXX); SDL_free. */
static char *
ns_fs_temp_name(const char *path)
{
    static const char abc[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    static SDL_AtomicInt counter;
    Uint64 x = SDL_GetTicksNS() ^ ((Uint64)SDL_GetCurrentThreadID() << 20) ^
               ((Uint64)(Uint32)SDL_AddAtomicInt(&counter, 1) * 0x9E3779B97F4A7C15ull) ^
               (Uint64)(uintptr_t)path;
    char suffix[7];
    for (int i = 0; i < 6; i++) {
        x ^= x >> 33;
        x *= 0xff51afd7ed558ccdull;
        x ^= x >> 29;
        suffix[i] = abc[x % (sizeof abc - 1)];
    }
    suffix[6] = '\0';
    char *tmp = NULL;
    if (SDL_asprintf(&tmp, "%s.%s", path, suffix) < 0) return NULL;
    return tmp;
}

/* True when something (a file, folder or link, even a dangling one) is at path. */
static bool
ns_fs_present(const char *path)
{
#if defined(NS_FS_POSIX)
    struct stat st;
    return lstat(path, &st) == 0;
#elif defined(NS_FS_WINDOWS)
    wchar_t *w = ns_fs_wide(path);
    bool present = w && GetFileAttributesW(w) != INVALID_FILE_ATTRIBUTES;
    SDL_free(w);
    return present;
#else
    return SDL_GetPathInfo(path, NULL);
#endif
}

#ifdef NS_FS_POSIX
/* Writes data to a new file tmp (mode less the umask, as GLib), synced when
 * sync is set. */
static bool
ns_fs_write_new(const char *tmp, const char *data, size_t len, bool sync,
                int mode)
{
    int fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, (mode_t)mode);
    if (fd < 0) return ns_fs_errno("could not create", tmp);
    while (len > 0) {
        ssize_t n = write(fd, data, len);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            ns_fs_errno("could not write", tmp);
            close(fd);
            return false;
        }
        data += n;
        len -= (size_t)n;
    }
    if (sync && fsync(fd) != 0) {
        ns_fs_errno("could not sync", tmp);
        close(fd);
        return false;
    }
    if (close(fd) != 0) return ns_fs_errno("could not close", tmp);
    return true;
}
#else
static bool
ns_fs_write_new(const char *tmp, const char *data, size_t len, bool sync,
                int mode)
{
    (void)sync; /* SDL_CloseIO flushes SDL's and the C library's buffers. */
    (void)mode; /* No Unix permissions here. */
    SDL_IOStream *io = SDL_IOFromFile(tmp, "wb");
    if (!io) return false;
    bool ok = SDL_WriteIO(io, data, len) == len;
    if (!SDL_CloseIO(io)) ok = false;
    return ok;
}
#endif

bool
ns_fs_set_contents(const char *path, const char *data, ptrdiff_t length)
{
    return ns_fs_set_contents_mode(path, data, length, 0666);
}

bool
ns_fs_set_contents_mode(const char *path, const char *data, ptrdiff_t length,
                        int mode)
{
    size_t len = length < 0 ? strlen(data) : (size_t)length;
    /* As GLib: sync the new contents first when they replace a file that
     * has some, so a crash cannot leave an empty file in its place. */
    NsFsStat old;
    bool sync = ns_fs_stat(path, &old) == 0 && old.size > 0;
    for (int attempt = 0; attempt < 100; attempt++) {
        char *tmp = ns_fs_temp_name(path);
        if (!tmp) return false;
        if (ns_fs_present(tmp)) {
            SDL_free(tmp);
            continue;
        }
        if (!ns_fs_write_new(tmp, data, len, sync, mode)) {
#ifdef NS_FS_POSIX
            bool taken = errno == EEXIST;
#else
            bool taken = false;
#endif
            if (taken) {
                /* Another writer's file: leave it, pick another name. */
                SDL_free(tmp);
                continue;
            }
            char *why = SDL_strdup(SDL_GetError());
            (void)SDL_RemovePath(tmp);
            SDL_SetError("%s", why ? why : "write failed");
            SDL_free(why);
            SDL_free(tmp);
            return false;
        }
        if (!SDL_RenamePath(tmp, path)) {
            char *why = SDL_strdup(SDL_GetError());
            (void)SDL_RemovePath(tmp);
            SDL_SetError("could not replace %s: %s", path, why ? why : "?");
            SDL_free(why);
            SDL_free(tmp);
            return false;
        }
        SDL_free(tmp);
        return true;
    }
    return SDL_SetError("could not find a temporary name beside %s", path);
}

char *
ns_fs_read_link(const char *path)
{
#ifdef NS_FS_POSIX
    size_t cap = 256;
    for (;;) {
        char *buf = malloc(cap);
        if (!buf) return NULL;
        ssize_t n = readlink(path, buf, cap);
        if (n < 0) {
            free(buf);
            return NULL;
        }
        if ((size_t)n < cap) {
            buf[n] = 0;
            return buf;
        }
        free(buf);
        cap *= 2;
    }
#else
    (void)path;
    return NULL;
#endif
}

/* The Downloads folder named in the user's user-dirs.dirs, from malloc(),
 * or NULL. A line reads XDG_DOWNLOAD_DIR="$HOME/Downloads" or gives an
 * absolute path; anything else is not a folder the user chose. */
static char *
ns_fs_xdg_download_dir(void)
{
    char *file = NULL, *contents = NULL, *found = NULL;
    const char *config = ns_fs_user_config_dir(), *home = ns_fs_home_dir();
    static const char key[] = "XDG_DOWNLOAD_DIR";
    if (!config || SDL_asprintf(&file, "%s/user-dirs.dirs", config) < 0)
        return NULL;
    bool ok = ns_fs_get_contents(file, &contents, NULL);
    SDL_free(file);
    if (!ok) return NULL;
    for (char *line = contents; line && *line;) {
        char *end = strchr(line, '\n');
        if (end) *end = 0;
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (strncmp(p, key, sizeof key - 1) == 0) {
            p += sizeof key - 1;
            while (*p == ' ' || *p == '\t') p++;
            if (*p == '=') {
                p++;
                while (*p == ' ' || *p == '\t') p++;
                char *close_quote = *p == '"' ? strrchr(p + 1, '"') : NULL;
                if (close_quote) {
                    *close_quote = 0;
                    p++;
                    char *dir = NULL;
                    if (strncmp(p, "$HOME", 5) == 0 && (p[5] == '/' || !p[5])) {
                        if (home && SDL_asprintf(&dir, "%s%s", home, p + 5) < 0)
                            dir = NULL;
                    } else if (*p == '/') {
                        dir = SDL_strdup(p);
                    }
                    if (dir) {
                        /* The last such line counts. */
                        SDL_free(found);
                        found = dir;
                    }
                }
            }
        }
        line = end ? end + 1 : NULL;
    }
    free(contents);
    return found;
}

const char *
ns_fs_user_download_dir(void)
{
    static NsOnce once;
    static char *dir;
    if (ns_once_enter(&once)) {
#if defined(NS_FS_XDG)
        dir = ns_fs_xdg_download_dir();
#else
        (void)ns_fs_xdg_download_dir;
        const char *folder = SDL_GetUserFolder(SDL_FOLDER_DOWNLOADS);
        dir = folder && *folder ? SDL_strdup(folder) : NULL;
#endif
        /* Without a separator at the end, unless it is all there is. */
        size_t len = dir ? strlen(dir) : 0;
        while (len > 1 && (dir[len - 1] == '/' || dir[len - 1] == '\\'))
            dir[--len] = 0;
        ns_once_leave(&once);
    }
    return dir;
}

/* ---- Paths ---- */

static bool
ns_fs_is_symlink(const char *path)
{
#if defined(NS_FS_POSIX)
    struct stat st;
    return lstat(path, &st) == 0 && S_ISLNK(st.st_mode);
#elif defined(NS_FS_WINDOWS)
    /* Symlinks and junctions are both reparse points, as GLib treats them. */
    wchar_t *w = ns_fs_wide(path);
    DWORD a = w ? GetFileAttributesW(w) : INVALID_FILE_ATTRIBUTES;
    SDL_free(w);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT);
#else
    (void)path;
    return false;
#endif
}

bool
ns_fs_test(const char *path, NsFsTest tests)
{
    if (!path) return false;
    if ((tests & NS_FS_TEST_IS_SYMLINK) && ns_fs_is_symlink(path)) return true;
    if (!(tests & (NS_FS_TEST_EXISTS | NS_FS_TEST_IS_REGULAR | NS_FS_TEST_IS_DIR)))
        return false;
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path, &info)) return false;
    if (tests & NS_FS_TEST_EXISTS) return true;
    if ((tests & NS_FS_TEST_IS_REGULAR) && info.type == SDL_PATHTYPE_FILE)
        return true;
    if ((tests & NS_FS_TEST_IS_DIR) && info.type == SDL_PATHTYPE_DIRECTORY)
        return true;
    return false;
}

int
ns_fs_stat(const char *path, NsFsStat *st)
{
    SDL_PathInfo info;
    if (!path || !SDL_GetPathInfo(path, &info)) return -1;
    st->type = info.type;
    st->size = info.size;
    st->mtime = SDL_NS_TO_SECONDS(info.modify_time);
    return 0;
}

int
ns_fs_mkdir_p(const char *path, int mode)
{
    if (!path || !*path) {
        SDL_SetError("no folder to create");
        return -1;
    }
#ifdef NS_FS_POSIX
    /* As g_mkdir_with_parents: each missing folder is made with mode. */
    char *p = SDL_strdup(path);
    if (!p) return -1;
    char *c = p;
    while (*c == '/') c++;
    for (;;) {
        char *slash = strchr(c, '/');
        if (slash) *slash = '\0';
        struct stat st;
        if (stat(p, &st) != 0) {
            if (mkdir(p, (mode_t)mode) != 0 && errno != EEXIST) {
                ns_fs_errno("could not create", p);
                SDL_free(p);
                return -1;
            }
        } else if (!S_ISDIR(st.st_mode)) {
            errno = ENOTDIR;
            ns_fs_errno("could not create", p);
            SDL_free(p);
            return -1;
        }
        if (!slash) break;
        *slash = '/';
        c = slash + 1;
        while (*c == '/') c++;
        if (!*c) break;
    }
    SDL_free(p);
    return 0;
#else
    (void)mode;
    return SDL_CreateDirectory(path) ? 0 : -1;
#endif
}

int
ns_fs_remove(const char *path)
{
    /* SDL_RemovePath reports success when nothing is there; GLib does not. */
    if (!path || !ns_fs_present(path)) {
        SDL_SetError("nothing to remove at %s", path ? path : "(null)");
        return -1;
    }
    return SDL_RemovePath(path) ? 0 : -1;
}

int
ns_fs_rename(const char *oldpath, const char *newpath)
{
    return SDL_RenamePath(oldpath, newpath) ? 0 : -1;
}

int
ns_fs_chmod(const char *path, int mode)
{
#ifdef NS_FS_POSIX
    if (chmod(path, (mode_t)mode) != 0) {
        ns_fs_errno("could not chmod", path);
        return -1;
    }
    return 0;
#else
    (void)path;
    (void)mode;
    return 0;
#endif
}

/* ---- Folder listings ---- */

struct NsDir {
    char **names;
    int    count;
    int    cap;
    int    next;
};

static SDL_EnumerationResult SDLCALL
ns_dir_collect(void *userdata, const char *dirname, const char *fname)
{
    (void)dirname;
    NsDir *d = userdata;
    if (!strcmp(fname, ".") || !strcmp(fname, ".."))
        return SDL_ENUM_CONTINUE;
    if (d->count == d->cap) {
        int ncap = d->cap ? d->cap * 2 : 32;
        char **n = SDL_realloc(d->names, (size_t)ncap * sizeof *n);
        if (!n) return SDL_ENUM_FAILURE;
        d->names = n;
        d->cap = ncap;
    }
    char *name = SDL_strdup(fname);
    if (!name) return SDL_ENUM_FAILURE;
    d->names[d->count++] = name;
    return SDL_ENUM_CONTINUE;
}

NsDir *
ns_dir_open(const char *path)
{
    NsDir *d = SDL_calloc(1, sizeof *d);
    if (!d) return NULL;
    if (!path || !SDL_EnumerateDirectory(path, ns_dir_collect, d)) {
        ns_dir_close(d);
        return NULL;
    }
    return d;
}

const char *
ns_dir_read_name(NsDir *dir)
{
    return dir && dir->next < dir->count ? dir->names[dir->next++] : NULL;
}

void
ns_dir_close(NsDir *dir)
{
    if (!dir) return;
    for (int i = 0; i < dir->count; i++)
        SDL_free(dir->names[i]);
    SDL_free(dir->names);
    SDL_free(dir);
}
