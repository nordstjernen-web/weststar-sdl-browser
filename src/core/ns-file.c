/* Weststar SDL Browser — nscore: gstdio's file calls. On Windows the names
 * are UTF-8 and go to the wide C runtime calls; elsewhere straight to
 * POSIX.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ns-path.h"
#include "ns-mem.h"
#include "ns-private.h"
#include "ns-strfuncs.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <sys/utime.h>
#include <wchar.h>
#else
#include <unistd.h>
#include <utime.h>
#endif

#ifdef _WIN32

wchar_t *
ns_win32_utf8_to_wide(const char *str)
{
    if (!str) return NULL;
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, str, -1, NULL, 0);
    if (n <= 0) return NULL;
    wchar_t *w = ns_malloc_n((size_t)n, sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, str, -1, w, n);
    return w;
}

char *
ns_win32_wide_to_utf8(const wchar_t *str)
{
    if (!str) return NULL;
    int n = WideCharToMultiByte(CP_UTF8, 0, str, -1, NULL, 0, NULL, NULL);
    if (n <= 0) return NULL;
    char *s = ns_malloc((size_t)n);
    WideCharToMultiByte(CP_UTF8, 0, str, -1, s, n, NULL, NULL);
    return s;
}

char *
ns_win32_error_message(int error)
{
    wchar_t *msg = NULL;
    DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS |
                                 FORMAT_MESSAGE_FROM_SYSTEM,
                             NULL, (DWORD)error, 0, (wchar_t *)&msg, 0, NULL);
    char *r = NULL;
    if (n && msg) {
        /* Without the trailing line break. */
        while (n > 0 && (msg[n - 1] == L'\n' || msg[n - 1] == L'\r')) msg[--n] = 0;
        r = ns_win32_wide_to_utf8(msg);
    }
    if (msg) LocalFree(msg);
    return r ? r : ns_strdup("");
}

/* Runs call on the wide form of name, EINVAL if it is not UTF-8. */
#define NS_WIDE_CALL(name, wname, expr)                 \
    do {                                                \
        wchar_t *wname = ns_win32_utf8_to_wide(name);   \
        if (!wname) {                                   \
            errno = EINVAL;                             \
            return -1;                                  \
        }                                               \
        int r_ = (expr);                                \
        int e_ = errno;                                 \
        ns_free(wname);                                 \
        errno = e_;                                     \
        return r_;                                      \
    } while (0)

int
ns_open(const char *filename, int flags, int mode)
{
    NS_WIDE_CALL(filename, w, _wopen(w, flags, mode));
}

int
ns_utime(const char *filename, void *utb)
{
    NS_WIDE_CALL(filename, w, _wutime(w, (struct _utimbuf *)utb));
}

int
ns_unlink(const char *filename)
{
    NS_WIDE_CALL(filename, w, _wunlink(w));
}

int
ns_rmdir(const char *filename)
{
    NS_WIDE_CALL(filename, w, _wrmdir(w));
}

int
ns_remove(const char *filename)
{
    wchar_t *w = ns_win32_utf8_to_wide(filename);
    if (!w) {
        errno = EINVAL;
        return -1;
    }
    int r = _wremove(w);
    if (r == -1) r = _wrmdir(w);
    int e = errno;
    ns_free(w);
    errno = e;
    return r;
}

int
ns_rename(const char *oldfilename, const char *newfilename)
{
    wchar_t *wo = ns_win32_utf8_to_wide(oldfilename);
    wchar_t *wn = ns_win32_utf8_to_wide(newfilename);
    int r = -1;
    if (!wo || !wn) {
        errno = EINVAL;
    } else if (MoveFileExW(wo, wn, MOVEFILE_REPLACE_EXISTING)) {
        r = 0;
    } else {
        errno = GetLastError() == ERROR_ACCESS_DENIED ? EACCES : ENOENT;
    }
    ns_free(wo);
    ns_free(wn);
    return r;
}

int
ns_chmod(const char *filename, int mode)
{
    NS_WIDE_CALL(filename, w, _wchmod(w, mode));
}

int
ns_mkdir(const char *filename, int mode)
{
    (void)mode;
    NS_WIDE_CALL(filename, w, _wmkdir(w));
}

void *
ns_fopen(const char *filename, const char *mode)
{
    wchar_t *w = ns_win32_utf8_to_wide(filename);
    wchar_t *wm = ns_win32_utf8_to_wide(mode);
    FILE *f = NULL;
    if (w && wm) f = _wfopen(w, wm);
    else errno = EINVAL;
    int e = errno;
    ns_free(w);
    ns_free(wm);
    errno = e;
    return f;
}

#else

int
ns_open(const char *filename, int flags, int mode)
{
    int fd;
    do {
        fd = open(filename, flags, mode);
    } while (fd == -1 && errno == EINTR);
    return fd;
}

int
ns_utime(const char *filename, void *utb)
{
    return utime(filename, (struct utimbuf *)utb);
}

int
ns_unlink(const char *filename)
{
    return unlink(filename);
}

int
ns_rmdir(const char *filename)
{
    return rmdir(filename);
}

int
ns_remove(const char *filename)
{
    return remove(filename);
}

int
ns_rename(const char *oldfilename, const char *newfilename)
{
    return rename(oldfilename, newfilename);
}

int
ns_chmod(const char *filename, int mode)
{
    return chmod(filename, (mode_t)mode);
}

int
ns_mkdir(const char *filename, int mode)
{
    return mkdir(filename, (mode_t)mode);
}

void *
ns_fopen(const char *filename, const char *mode)
{
    return fopen(filename, mode);
}

#endif
