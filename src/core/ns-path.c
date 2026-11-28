/* Weststar SDL Browser — nscore: file names, with GLib's rules for
 * joining, splitting and canonicalizing them.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ns-path.h"
#include "ns-mem.h"
#include "ns-private.h"
#include "ns-string.h"
#include "ns-strfuncs.h"
#include "ns-unicode.h"

#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

/* Joins the non-empty elements with one separator each, keeping the
 * leading separators of the first and the trailing ones of the last. On
 * Windows the separator is whichever of '\' and '/' the elements last
 * showed, '\' if none. An element made only of separators, when it is the
 * only one, is returned as it is. */
static char *
ns_build_path_va(const char *first, va_list *args, char **array)
{
    NsString *result = ns_string_new(NULL);
    int is_first = 1, have_leading = 0;
    const char *single = NULL, *last_trailing = NULL;
    char sep = NS_DIR_SEPARATOR;
    size_t i = 0;
    const char *next = array ? array[i++] : first;
    while (next) {
        const char *element = next;
        next = array ? array[i++] : va_arg(*args, const char *);
        if (!*element) continue;
        const char *start = element;
        while (NS_IS_DIR_SEPARATOR(*start)) {
            sep = *start;
            start++;
        }
        const char *end = start + strlen(start);
        while (end >= start + 1 && NS_IS_DIR_SEPARATOR(end[-1])) {
            sep = end[-1];
            end--;
        }
        last_trailing = end;
        while (last_trailing >= element + 1 && NS_IS_DIR_SEPARATOR(last_trailing[-1]))
            last_trailing--;
        if (!have_leading) {
            if (last_trailing <= start) single = element;
            ns_string_append_len(result, element, start - element);
            have_leading = 1;
        } else {
            single = NULL;
        }
        if (end == start) continue;
        if (!is_first) ns_string_append_c(result, sep);
        ns_string_append_len(result, start, end - start);
        is_first = 0;
    }
#ifndef _WIN32
    (void)sep;
#endif
    if (single) {
        ns_string_free(result, 1);
        return ns_strdup(single);
    }
    if (last_trailing) ns_string_append(result, last_trailing);
    return ns_string_free(result, 0);
}

char *
ns_build_filename(const char *first_element, ...)
{
    va_list ap;
    va_start(ap, first_element);
    char *r = ns_build_path_va(first_element, &ap, NULL);
    va_end(ap);
    return r;
}

char *
ns_build_filenamev(char **args)
{
    return ns_build_path_va(NULL, NULL, args);
}

int
ns_path_is_absolute(const char *file_name)
{
    if (!file_name) return 0;
    if (NS_IS_DIR_SEPARATOR(file_name[0])) return 1;
#ifdef _WIN32
    if (ns_ascii_isalpha(file_name[0]) && file_name[1] == ':' &&
        NS_IS_DIR_SEPARATOR(file_name[2]))
        return 1;
#endif
    return 0;
}

const char *
ns_path_skip_root(const char *file_name)
{
    if (!file_name) return NULL;
#ifdef _WIN32
    /* \\server\share\ */
    if (NS_IS_DIR_SEPARATOR(file_name[0]) && NS_IS_DIR_SEPARATOR(file_name[1]) &&
        file_name[2] && !NS_IS_DIR_SEPARATOR(file_name[2])) {
        const char *p = file_name + 2;
        while (*p && !NS_IS_DIR_SEPARATOR(*p)) p++;
        if (*p && p > file_name + 2 && p[1]) {
            file_name = p + 1;
            while (file_name[0] && !NS_IS_DIR_SEPARATOR(file_name[0])) file_name++;
            if (NS_IS_DIR_SEPARATOR(file_name[0])) file_name++;
            return file_name;
        }
    }
#endif
    if (NS_IS_DIR_SEPARATOR(file_name[0])) {
        while (NS_IS_DIR_SEPARATOR(file_name[0])) file_name++;
        return file_name;
    }
#ifdef _WIN32
    if (ns_ascii_isalpha(file_name[0]) && file_name[1] == ':' &&
        NS_IS_DIR_SEPARATOR(file_name[2]))
        return file_name + 3;
#endif
    return NULL;
}

char *
ns_path_get_basename(const char *file_name)
{
    if (!file_name) return NULL;
    if (!file_name[0]) return ns_strdup(".");
    ptrdiff_t last = (ptrdiff_t)strlen(file_name) - 1;
    while (last >= 0 && NS_IS_DIR_SEPARATOR(file_name[last])) last--;
    if (last == -1) return ns_strdup(NS_DIR_SEPARATOR_S);
#ifdef _WIN32
    if (last == 1 && ns_ascii_isalpha(file_name[0]) && file_name[1] == ':')
        return ns_strdup(NS_DIR_SEPARATOR_S);
#endif
    ptrdiff_t base = last;
    while (base >= 0 && !NS_IS_DIR_SEPARATOR(file_name[base])) base--;
#ifdef _WIN32
    if (base == -1 && ns_ascii_isalpha(file_name[0]) && file_name[1] == ':') base = 1;
#endif
    return ns_strndup(file_name + base + 1, (size_t)(last - base));
}

static const char *
ns_last_separator(const char *file_name)
{
    const char *base = strrchr(file_name, NS_DIR_SEPARATOR);
#ifdef _WIN32
    const char *q = strrchr(file_name, '/');
    if (!base || (q && q > base)) base = q;
#endif
    return base;
}

char *
ns_path_get_dirname(const char *file_name)
{
    if (!file_name) return NULL;
    const char *base = ns_last_separator(file_name);
    if (!base) {
#ifdef _WIN32
        if (ns_ascii_isalpha(file_name[0]) && file_name[1] == ':') {
            char d[4] = { file_name[0], ':', '.', '\0' };
            return ns_strdup(d);
        }
#endif
        return ns_strdup(".");
    }
    while (base > file_name && NS_IS_DIR_SEPARATOR(*base)) base--;
#ifdef _WIN32
    /* Keep the separator of X:\ and of \\server\share\ . */
    if (base == file_name + 1 && ns_ascii_isalpha(file_name[0]) && file_name[1] == ':') {
        base++;
    } else if (NS_IS_DIR_SEPARATOR(file_name[0]) && NS_IS_DIR_SEPARATOR(file_name[1]) &&
               file_name[2] && !NS_IS_DIR_SEPARATOR(file_name[2]) && base >= file_name + 2) {
        const char *p = file_name + 2;
        while (*p && !NS_IS_DIR_SEPARATOR(*p)) p++;
        if (p == base + 1) {
            size_t len = strlen(file_name) + 1;
            char *r = ns_malloc(len + 1);
            strcpy(r, file_name);
            r[len - 1] = NS_DIR_SEPARATOR;
            r[len] = '\0';
            return r;
        }
        if (NS_IS_DIR_SEPARATOR(*p)) {
            p++;
            while (*p && !NS_IS_DIR_SEPARATOR(*p)) p++;
            if (p == base + 1) base++;
        }
    }
#endif
    return ns_strndup(file_name, (size_t)(base - file_name) + 1);
}

char *
ns_get_current_dir(void)
{
#ifdef _WIN32
    DWORD n = GetCurrentDirectoryW(0, NULL);
    wchar_t *w = ns_malloc_n(n + 1, sizeof(wchar_t));
    char *dir = NULL;
    if (GetCurrentDirectoryW(n + 1, w)) dir = ns_win32_wide_to_utf8(w);
    ns_free(w);
    return dir ? dir : ns_strdup("\\");
#else
    size_t cap = 256;
    for (;;) {
        char *buf = ns_malloc(cap);
        if (getcwd(buf, cap)) return buf;
        ns_free(buf);
        if (cap > (1u << 20)) break;
        cap *= 2;
    }
    return ns_strdup("/");
#endif
}

char *
ns_canonicalize_filename(const char *filename, const char *relative_to)
{
    if (relative_to && !ns_path_is_absolute(relative_to)) return NULL;
    char *canon;
    if (!ns_path_is_absolute(filename)) {
        char *cwd = relative_to ? NULL : ns_get_current_dir();
        canon = ns_build_filename(relative_to ? relative_to : cwd, filename, NULL);
        ns_free(cwd);
    } else {
        canon = ns_strdup(filename);
    }

    char *after_root = (char *)ns_path_skip_root(canon);
    if (!after_root) {
        ns_free(canon);
        return ns_build_filename(NS_DIR_SEPARATOR_S, filename, NULL);
    }

    /* The separators of the root become the canonical one. */
    char *output = after_root - 1;
    for (; output >= canon && NS_IS_DIR_SEPARATOR(*output); output--)
        *output = NS_DIR_SEPARATOR;
    output++;
    if (*output == NS_DIR_SEPARATOR) output++;
    /* "//" at the start means something of its own; three or more is "/". */
    if (after_root - output == 1) output++;

    char *input = after_root;
    char *output_start = output;
    while (*input) {
        while (NS_IS_DIR_SEPARATOR(input[0])) input++;
        if (input[0] == '.' && (input[1] == '\0' || NS_IS_DIR_SEPARATOR(input[1]))) {
            if (input[1] == '\0') break;
            input += 2;
        } else if (input[0] == '.' && input[1] == '.' &&
                   (input[2] == '\0' || NS_IS_DIR_SEPARATOR(input[2]))) {
            if (output > output_start) {
                do {
                    output--;
                } while (!NS_IS_DIR_SEPARATOR(output[-1]) && output > output_start);
            }
            if (input[2] == '\0') break;
            input += 3;
        } else {
            while (*input && !NS_IS_DIR_SEPARATOR(*input)) *output++ = *input++;
            if (input[0] == '\0') break;
            input++;
            *output++ = NS_DIR_SEPARATOR;
        }
    }
    if (output > output_start && NS_IS_DIR_SEPARATOR(output[-1])) output--;
    *output = '\0';
    return canon;
}

#ifdef _WIN32
static int
ns_is_executable_file(const char *path)
{
    wchar_t *w = ns_win32_utf8_to_wide(path);
    if (!w) return 0;
    DWORD attr = GetFileAttributesW(w);
    ns_free(w);
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}
#else
static int
ns_is_executable_file(const char *path)
{
    struct stat st;
    return access(path, X_OK) == 0 && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}
#endif

#ifdef _WIN32
static char *
ns_win32_dir(UINT (WINAPI *get)(LPWSTR, UINT))
{
    wchar_t buf[MAX_PATH];
    UINT n = get(buf, MAX_PATH);
    return n > 0 && n < MAX_PATH ? ns_win32_wide_to_utf8(buf) : NULL;
}
#endif

/* The program as GLib's g_find_program_in_path finds it: a name with a
 * directory in it as it stands (made absolute against the current
 * directory) if it is an executable file; otherwise the first executable
 * dir/name along PATH, an empty entry standing for the current directory.
 * On Windows the search goes through the program's own folder, the
 * current one, the system and Windows folders and then PATH. */
static char *
ns_find_one(const char *program)
{
    int has_dir = strchr(program, NS_DIR_SEPARATOR) != NULL;
#ifdef _WIN32
    has_dir = has_dir || strchr(program, '/') != NULL;
#endif
    if (ns_path_is_absolute(program) || has_dir) {
        if (ns_is_executable_file(program)) {
            if (ns_path_is_absolute(program)) return ns_strdup(program);
            char *cwd = ns_get_current_dir();
            char *r = ns_build_filename(cwd, program, NULL);
            ns_free(cwd);
            return r;
        }
        if (ns_path_is_absolute(program)) return NULL;
    }
#ifdef _WIN32
    wchar_t *wpath = NULL;
    DWORD n = GetEnvironmentVariableW(L"PATH", NULL, 0);
    if (n) {
        wpath = ns_malloc_n(n, sizeof(wchar_t));
        GetEnvironmentVariableW(L"PATH", wpath, n);
    }
    char *env = wpath ? ns_win32_wide_to_utf8(wpath) : NULL;
    ns_free(wpath);
    char *appdir = NULL;
    wchar_t exe[MAX_PATH];
    DWORD en = GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (en > 0 && en < MAX_PATH) {
        char *f = ns_win32_wide_to_utf8(exe);
        appdir = f ? ns_path_get_dirname(f) : NULL;
        ns_free(f);
    }
    char *sysdir = ns_win32_dir(GetSystemDirectoryW);
    char *windir = ns_win32_dir(GetWindowsDirectoryW);
    char *path = ns_strconcat(appdir ? appdir : "", appdir ? ";" : "", ".;",
                              sysdir ? sysdir : "", sysdir ? ";" : "",
                              windir ? windir : "", windir ? ";" : "",
                              env ? env : "", NULL);
    ns_free(appdir);
    ns_free(sysdir);
    ns_free(windir);
    ns_free(env);
#else
    const char *env = getenv("PATH");
    char *path = ns_strdup(env ? env : "/bin:/usr/bin:.");
#endif
    char **dirs = ns_strsplit(path, NS_SEARCHPATH_SEPARATOR_S, 0);
    ns_free(path);
    char *found = NULL;
    for (char **d = dirs; *d && !found; d++) {
        char *candidate = **d ? ns_strconcat(*d, NS_DIR_SEPARATOR_S, program, NULL)
                              : ns_strdup(program);
        if (ns_is_executable_file(candidate)) {
            if (ns_path_is_absolute(candidate)) {
                found = candidate;
                continue;
            }
            char *cwd = ns_get_current_dir();
            found = ns_build_filename(cwd, candidate, NULL);
            ns_free(cwd);
        }
        ns_free(candidate);
    }
    ns_strfreev(dirs);
    return found;
}

char *
ns_find_program_in_path(const char *program)
{
    if (!program) return NULL;
#ifdef _WIN32
    const char *dot = strrchr(program, '.');
    if (!dot || strchr(dot, '\\') || strchr(dot, '/')) {
        const char *env = getenv("PATHEXT");
        char *exts = ns_strconcat(".exe;.cmd;.bat;.com", env && *env ? ";" : "",
                                  env ? env : "", NULL);
        char **list = ns_strsplit(exts, ";", 0);
        ns_free(exts);
        char *found = NULL;
        for (char **e = list; *e && !found; e++) {
            if (!**e) continue;
            char *name = ns_strconcat(program, *e, NULL);
            found = ns_find_one(name);
            ns_free(name);
        }
        ns_strfreev(list);
        return found;
    }
#endif
    return ns_find_one(program);
}

char *
ns_filename_display_name(const char *filename)
{
    return ns_utf8_make_valid(filename, -1);
}
