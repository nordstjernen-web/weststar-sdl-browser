/* Weststar SDL Browser — what the browser asks the system about itself.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "plat/ns_sys.h"
#include "plat/ns_env.h"
#include "plat/ns_fs.h"
#include "plat/ns_plat.h"

#include "plat/ns_sdl.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <wchar.h>
#endif

int
ns_sys_num_processors(void)
{
    int n = SDL_GetNumLogicalCPUCores();
    return n > 0 ? n : 1;
}

/* ---- lists of strings built once ---- */

typedef struct StrList {
    char **items;
    int    n, cap;
} StrList;

/* Appends a copy of the first len bytes of s, unless it is already there
 * or empty. The list always ends with a NULL. */
static void
list_add(StrList *list, const char *s, size_t len)
{
    if (!len) return;
    for (int i = 0; i < list->n; i++)
        if (strlen(list->items[i]) == len && memcmp(list->items[i], s, len) == 0)
            return;
    if (list->n + 2 > list->cap) {
        int cap = list->cap ? list->cap * 2 : 16;
        char **items = SDL_realloc(list->items, (size_t)cap * sizeof *items);
        if (!items) return;
        list->items = items;
        list->cap = cap;
    }
    char *copy = SDL_strndup(s, len);
    if (!copy) return;
    list->items[list->n++] = copy;
    list->items[list->n] = NULL;
}

static const char *const empty_list[] = { NULL };

static const char *const *
list_finish(StrList *list)
{
    return list->items ? (const char *const *)list->items : empty_list;
}

/* ---- the system's name ---- */

#if !defined(_WIN32) && !defined(__APPLE__)
/* The value of KEY=value or KEY="value" in an os-release file. */
static char *
os_release_value(const char *contents, const char *key)
{
    size_t key_len = strlen(key);
    for (const char *line = contents; line && *line;) {
        const char *end = strchr(line, '\n');
        size_t len = end ? (size_t)(end - line) : strlen(line);
        if (len > key_len && memcmp(line, key, key_len) == 0 &&
            line[key_len] == '=') {
            const char *v = line + key_len + 1;
            size_t n = len - key_len - 1;
            if (n >= 2 && (v[0] == '"' || v[0] == '\'') && v[n - 1] == v[0]) {
                v++;
                n -= 2;
            }
            return n ? SDL_strndup(v, n) : NULL;
        }
        line = end ? end + 1 : NULL;
    }
    return NULL;
}
#endif

const char *
ns_sys_os_name(void)
{
    static NsOnce once;
    static char *name;
    if (ns_once_enter(&once)) {
#if !defined(_WIN32) && !defined(__APPLE__)
        static const char *const files[] = {
            "/etc/os-release", "/usr/lib/os-release",
        };
        for (size_t i = 0; !name && i < SDL_arraysize(files); i++) {
            char *contents = NULL;
            if (!ns_fs_get_contents(files[i], &contents, NULL))
                continue;
            name = os_release_value(contents, "PRETTY_NAME");
            if (!name) name = os_release_value(contents, "NAME");
            free(contents);
        }
#endif
        ns_once_leave(&once);
    }
    return name ? name : SDL_GetPlatform();
}

/* ---- data folders ---- */

const char *const *
ns_sys_data_dirs(void)
{
    static NsOnce once;
    static StrList dirs;
    if (ns_once_enter(&once)) {
#if !defined(_WIN32)
        const char *value = ns_getenv("XDG_DATA_DIRS");
        if (!value || !*value) value = "/usr/local/share/:/usr/share/";
        while (value && *value) {
            const char *end = strchr(value, ':');
            size_t len = end ? (size_t)(end - value) : strlen(value);
            list_add(&dirs, value, len);
            value = end ? end + 1 : NULL;
        }
#endif
        ns_once_leave(&once);
    }
    return list_finish(&dirs);
}

/* ---- languages ---- */

/* A locale name and its shorter forms: "nb_NO.UTF-8@x" gives nb_NO@x,
 * nb_NO, nb@x and nb. The character set is of no use to the callers. */
static void
add_locale_variants(StrList *list, const char *locale, size_t len)
{
    char buf[64], out[80];
    if (!len || len >= sizeof buf) return;
    memcpy(buf, locale, len);
    buf[len] = 0;
    char *modifier = strchr(buf, '@');
    if (modifier) *modifier++ = 0;
    char *codeset = strchr(buf, '.');
    if (codeset) *codeset = 0;
    char *territory = strchr(buf, '_');
    if (territory) *territory++ = 0;
    if (!*buf) return;

    if (territory && *territory) {
        if (modifier && *modifier) {
            SDL_snprintf(out, sizeof out, "%s_%s@%s", buf, territory, modifier);
            list_add(list, out, strlen(out));
        }
        SDL_snprintf(out, sizeof out, "%s_%s", buf, territory);
        list_add(list, out, strlen(out));
    }
    if (modifier && *modifier) {
        SDL_snprintf(out, sizeof out, "%s@%s", buf, modifier);
        list_add(list, out, strlen(out));
    }
    list_add(list, buf, strlen(buf));
}

const char *const *
ns_sys_language_names(void)
{
    static NsOnce once;
    static StrList names;
    if (ns_once_enter(&once)) {
        /* As gettext reads them: LANGUAGE is a list and comes first, then
         * the first of the three that is set. */
        const char *language = ns_getenv("LANGUAGE");
        const char *locale = ns_getenv("LC_ALL");
        if (!locale || !*locale) locale = ns_getenv("LC_MESSAGES");
        if (!locale || !*locale) locale = ns_getenv("LANG");
        while (language && *language) {
            const char *end = strchr(language, ':');
            size_t len = end ? (size_t)(end - language) : strlen(language);
            add_locale_variants(&names, language, len);
            language = end ? end + 1 : NULL;
        }
        if (locale && *locale)
            add_locale_variants(&names, locale, strlen(locale));

        if (!names.n) {
            int count = 0;
            SDL_Locale **locales = SDL_GetPreferredLocales(&count);
            for (int i = 0; locales && i < count; i++) {
                char buf[64];
                if (!locales[i]->language) continue;
                if (locales[i]->country)
                    SDL_snprintf(buf, sizeof buf, "%s_%s", locales[i]->language,
                                 locales[i]->country);
                else
                    SDL_strlcpy(buf, locales[i]->language, sizeof buf);
                add_locale_variants(&names, buf, strlen(buf));
            }
            SDL_free(locales);
        }
        list_add(&names, "C", 1);
        ns_once_leave(&once);
    }
    return list_finish(&names);
}

#ifdef _WIN32
/* ---- Windows: the locale and the time zone ----
 * The Vista functions are looked up at run time, so that the Windows XP
 * build (_WIN32_WINNT 0x0501) imports none of them and still uses them
 * where they are. */

typedef int (WINAPI *ns_locale_name_fn)(LPWSTR name, int len);

bool
ns_sys_win_locale_name(char *buf, size_t len)
{
    WCHAR name[85] = {0};  /* LOCALE_NAME_MAX_LENGTH */
    int n = 0;
    HMODULE k = GetModuleHandleW(L"kernel32.dll");
    ns_locale_name_fn get_name = k
        ? (ns_locale_name_fn)(void *)GetProcAddress(k, "GetUserDefaultLocaleName")
        : NULL;
    if (get_name) n = get_name(name, (int)SDL_arraysize(name));
    if (n <= 0) {
        WCHAR lang[9], country[9];
        if (GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_SISO639LANGNAME, lang,
                           (int)SDL_arraysize(lang)) <= 0)
            return false;
        /* Each at most 9 characters with its NUL: "ll-CC" fits. */
        size_t n_lang = wcslen(lang);
        memcpy(name, lang, (n_lang + 1) * sizeof *name);
        if (GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_SISO3166CTRYNAME, country,
                           (int)SDL_arraysize(country)) > 0) {
            name[n_lang] = L'-';
            memcpy(name + n_lang + 1, country, (wcslen(country) + 1) * sizeof *name);
        }
    }
    return len <= INT_MAX &&
           WideCharToMultiByte(CP_UTF8, 0, name, -1, buf, (int)len, NULL, NULL) > 0;
}

/* The fields of DYNAMIC_TIME_ZONE_INFORMATION, which the XP headers lack. */
typedef struct ns_dynamic_tz {
    LONG Bias;
    WCHAR StandardName[32];
    SYSTEMTIME StandardDate;
    LONG StandardBias;
    WCHAR DaylightName[32];
    SYSTEMTIME DaylightDate;
    LONG DaylightBias;
    WCHAR TimeZoneKeyName[128];
    BOOLEAN DynamicDaylightTimeDisabled;
} ns_dynamic_tz;
typedef DWORD (WINAPI *ns_dynamic_tz_fn)(ns_dynamic_tz *info);

/* Windows XP: the key under Time Zones whose "Std" is the zone's standard
 * name, as Windows writes both in the system's language. */
static bool
win_time_zone_key_by_name(const WCHAR *std_name, WCHAR *key, DWORD key_len)
{
    HKEY zones;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Time Zones",
                      0, KEY_READ, &zones) != ERROR_SUCCESS)
        return false;
    bool found = false;
    for (DWORD i = 0; !found; i++) {
        DWORD n = key_len;
        if (RegEnumKeyExW(zones, i, key, &n, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;
        HKEY zone;
        if (RegOpenKeyExW(zones, key, 0, KEY_READ, &zone) != ERROR_SUCCESS)
            continue;
        WCHAR std[64];
        DWORD type = 0, size = sizeof std - sizeof std[0];
        if (RegQueryValueExW(zone, L"Std", NULL, &type, (BYTE *)std, &size) == ERROR_SUCCESS &&
            type == REG_SZ) {
            std[size / sizeof std[0]] = 0;
            found = wcscmp(std, std_name) == 0;
        }
        RegCloseKey(zone);
    }
    RegCloseKey(zones);
    return found;
}

bool
ns_sys_win_time_zone_key(char *buf, size_t len)
{
    WCHAR key[128] = {0};
    bool ok = false;
    HMODULE k = GetModuleHandleW(L"kernel32.dll");
    ns_dynamic_tz_fn get_dynamic = k
        ? (ns_dynamic_tz_fn)(void *)GetProcAddress(k, "GetDynamicTimeZoneInformation")
        : NULL;
    if (get_dynamic) {
        ns_dynamic_tz info = {0};
        if (get_dynamic(&info) != TIME_ZONE_ID_INVALID && info.TimeZoneKeyName[0]) {
            memcpy(key, info.TimeZoneKeyName, sizeof key);
            key[SDL_arraysize(key) - 1] = 0;
            ok = true;
        }
    }
    if (!ok) {
        TIME_ZONE_INFORMATION info = {0};
        if (GetTimeZoneInformation(&info) != TIME_ZONE_ID_INVALID)
            ok = win_time_zone_key_by_name(info.StandardName, key,
                                           (DWORD)SDL_arraysize(key));
    }
    return ok && len <= INT_MAX &&
           WideCharToMultiByte(CP_UTF8, 0, key, -1, buf, (int)len, NULL, NULL) > 0;
}
#endif

/* ---- calendar time ---- */

static int
days_in_month(int year, int month)
{
    static const int days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return month == 2 && leap ? 29 : days[month - 1];
}

/* Days from 1970-01-01 to a date of the proleptic Gregorian calendar. */
static Sint64
days_from_civil(int year, int month, int day)
{
    Sint64 y = year - (month <= 2);
    Sint64 era = (y >= 0 ? y : y - 399) / 400;
    Sint64 yoe = y - era * 400;
    Sint64 doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    Sint64 doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

bool
ns_time_from_utc(int year, int month, int day, int hour, int minute,
                 int second, Sint64 *unix_secs)
{
    if (year < 1 || year > 9999 || month < 1 || month > 12 || day < 1 ||
        day > days_in_month(year, month) || hour < 0 || hour > 23 ||
        minute < 0 || minute > 59 || second < 0 || second > 59)
        return false;
    *unix_secs = days_from_civil(year, month, day) * 86400 +
                 hour * 3600 + minute * 60 + second;
    return true;
}

bool
ns_time_to_fields(Sint64 unix_secs, bool local, NsDateTime *out)
{
    SDL_DateTime dt;
    /* SDL counts nanoseconds in 64 bits: about 292 years either way. */
    if (unix_secs > SDL_MAX_SINT64 / SDL_NS_PER_SECOND ||
        unix_secs < SDL_MIN_SINT64 / SDL_NS_PER_SECOND ||
        !SDL_TimeToDateTime(SDL_SECONDS_TO_NS(unix_secs), &dt, local))
        return false;
    out->year = dt.year;
    out->month = dt.month;
    out->day = dt.day;
    out->hour = dt.hour;
    out->minute = dt.minute;
    out->second = dt.second;
    out->utc_offset = local ? dt.utc_offset : 0;
    return true;
}

/* Reads exactly n digits. */
static bool
digits(const char **p, int n, int *out)
{
    int v = 0;
    for (int i = 0; i < n; i++) {
        if ((*p)[i] < '0' || (*p)[i] > '9') return false;
        v = v * 10 + ((*p)[i] - '0');
    }
    *p += n;
    *out = v;
    return true;
}

bool
ns_time_parse_iso8601(const char *text, Sint64 *unix_secs)
{
    const char *p = text;
    int year, month, day, hour, minute, second, zh = 0, zm = 0;
    if (!p) return false;
    while (*p == ' ') p++;

    if (!digits(&p, 4, &year)) return false;
    bool extended = *p == '-';
    if (extended) p++;
    if (!digits(&p, 2, &month)) return false;
    if (extended && *p++ != '-') return false;
    if (!digits(&p, 2, &day)) return false;

    if (*p != 'T' && *p != 't' && *p != ' ') return false;
    p++;
    if (!digits(&p, 2, &hour)) return false;
    extended = *p == ':';
    if (extended) p++;
    if (!digits(&p, 2, &minute)) return false;
    if (extended && *p++ != ':') return false;
    if (!digits(&p, 2, &second)) return false;
    if (*p == '.' || *p == ',') {
        p++;
        if (*p < '0' || *p > '9') return false;
        while (*p >= '0' && *p <= '9') p++;
    }

    int sign = 0;
    if (*p == 'Z' || *p == 'z') {
        p++;
    } else if (*p == '+' || *p == '-') {
        sign = *p++ == '+' ? 1 : -1;
        if (!digits(&p, 2, &zh)) return false;
        if (*p == ':') p++;
        if (*p >= '0' && *p <= '9' && !digits(&p, 2, &zm)) return false;
        if (zh > 23 || zm > 59) return false;
    } else {
        return false;
    }
    while (*p == ' ') p++;
    if (*p) return false;

    Sint64 secs;
    if (!ns_time_from_utc(year, month, day, hour, minute, second, &secs))
        return false;
    *unix_secs = secs - sign * (zh * 3600 + zm * 60);
    return true;
}
