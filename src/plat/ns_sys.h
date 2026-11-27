/* Weststar SDL Browser — what the browser asks the system about itself.
 * The processors, the system's name, the user's languages and calendar
 * time, from SDL and the C library rather than from GLib
 * (g_get_num_processors, g_get_os_info, g_get_language_names,
 * g_get_system_data_dirs, GDateTime), so that GLib is left with data
 * structures only. See docs/porting-to-sdl.md.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_SYS_H
#define NS_SYS_H

#include <SDL3/SDL_stdinc.h>
#include <stdbool.h>

/* Logical processors, at least 1. */
int ns_sys_num_processors(void);

/* The operating system's name for a person to read ("Ubuntu 24.04.1 LTS",
 * "Windows"): the distribution's where there is an os-release file, else
 * SDL's name for the platform. */
const char *ns_sys_os_name(void);

/* The folders shared data is installed under, most specific first, ending
 * with NULL: XDG_DATA_DIRS or its default on Unix, none elsewhere. */
const char *const *ns_sys_data_dirs(void);

/* The user's languages, most wanted first, as locale names ("nb_NO",
 * "nb", "en", …) ending with "C" and NULL: LANGUAGE, LC_ALL, LC_MESSAGES
 * and LANG where the environment has them, else what SDL reports for the
 * platform. Each name is followed by its shorter forms. */
const char *const *ns_sys_language_names(void);

/* ---- Calendar time ---- */

typedef struct NsDateTime {
    int year, month, day;     /* month 1-12, day 1-31 */
    int hour, minute, second;
    int utc_offset;           /* seconds east of UTC; 0 for UTC fields */
} NsDateTime;

/* Seconds since 1970 of a date and time in UTC; false if there is no
 * such date (month 13, 30 February, 24:00) or the year is outside
 * 1-9999. */
bool ns_time_from_utc(int year, int month, int day, int hour, int minute,
                      int second, Sint64 *unix_secs);

/* The calendar fields of a time, in UTC or in the user's time zone. */
bool ns_time_to_fields(Sint64 unix_secs, bool local, NsDateTime *out);

/* Parses an ISO 8601 date and time with a zone: 2026-11-27T09:30:00Z,
 * 20261127T093000+0100, with a space for the T and a fraction of a second
 * allowed. False for anything else, a time without a zone included. */
bool ns_time_parse_iso8601(const char *text, Sint64 *unix_secs);

#endif /* NS_SYS_H */
