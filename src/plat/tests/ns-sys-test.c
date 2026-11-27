/* Weststar SDL Browser — test of the SDL layer's system services.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Calendar arithmetic and ISO 8601 against known values, and what is read
 * from the environment and from files (languages, data folders, the
 * Downloads folder, links, file permissions) against an environment and
 * a folder the test sets up itself. NS_SYS_TEST_DIR names an empty,
 * writable folder.
 */

#include "plat/ns_env.h"
#include "plat/ns_fs.h"
#include "plat/ns_plat.h"
#include "plat/ns_sys.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

static int g_failed;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            g_failed++;                                                      \
        }                                                                    \
    } while (0)

static Sint64
utc(int y, int mo, int d, int h, int mi, int s)
{
    Sint64 t = -1;
    return ns_time_from_utc(y, mo, d, h, mi, s, &t) ? t : -1;
}

static Sint64
iso(const char *text)
{
    Sint64 t = -1;
    return ns_time_parse_iso8601(text, &t) ? t : -1;
}

static void
test_time(void)
{
    Sint64 t;
    CHECK(utc(1970, 1, 1, 0, 0, 0) == 0);
    CHECK(utc(2000, 1, 1, 0, 0, 0) == 946684800);
    CHECK(utc(2000, 2, 29, 12, 0, 0) == 951825600);
    CHECK(utc(2026, 11, 27, 9, 30, 15) == 1795771815);
    CHECK(utc(1969, 12, 31, 23, 59, 59) == -1 &&
          ns_time_from_utc(1969, 12, 31, 23, 59, 59, &t) && t == -1);
    CHECK(utc(9999, 12, 31, 23, 59, 59) == 253402300799);
    CHECK(ns_time_from_utc(1, 1, 1, 0, 0, 0, &t) && t == -62135596800);
    /* Dates that do not exist. */
    CHECK(!ns_time_from_utc(2023, 2, 29, 0, 0, 0, &t));
    CHECK(!ns_time_from_utc(1900, 2, 29, 0, 0, 0, &t));
    CHECK(!ns_time_from_utc(2024, 13, 1, 0, 0, 0, &t));
    CHECK(!ns_time_from_utc(2024, 4, 31, 0, 0, 0, &t));
    CHECK(!ns_time_from_utc(2024, 1, 1, 24, 0, 0, &t));
    CHECK(!ns_time_from_utc(2024, 1, 1, 0, 60, 0, &t));
    CHECK(!ns_time_from_utc(2024, 1, 1, 0, 0, 60, &t));
    CHECK(!ns_time_from_utc(0, 1, 1, 0, 0, 0, &t));
    CHECK(!ns_time_from_utc(10000, 1, 1, 0, 0, 0, &t));

    /* And back, a day at a time across leap years and a century. */
    for (Sint64 day = -800; day < 60000; day += 37) {
        NsDateTime f;
        Sint64 secs = day * 86400 + 3723;
        CHECK(ns_time_to_fields(secs, false, &f));
        CHECK(utc(f.year, f.month, f.day, f.hour, f.minute, f.second) == secs);
        CHECK(f.utc_offset == 0 && f.hour == 1 && f.minute == 2 && f.second == 3);
    }
    /* Local fields differ from UTC's by the offset they report. */
    NsDateTime local;
    CHECK(ns_time_to_fields(1795771815, true, &local));
    CHECK(utc(local.year, local.month, local.day, local.hour, local.minute,
              local.second) - local.utc_offset == 1795771815);

    CHECK(iso("2026-11-27T09:30:15Z") == 1795771815);
    CHECK(iso("2026-11-27 09:30:15z") == 1795771815);
    CHECK(iso("20261127T093015Z") == 1795771815);
    CHECK(iso("2026-11-27T10:30:15+01:00") == 1795771815);
    CHECK(iso("2026-11-27T10:30:15+0100") == 1795771815);
    CHECK(iso("2026-11-27T10:30:15+01") == 1795771815);
    CHECK(iso("2026-11-27T04:00:15.250-05:30") == 1795771815);
    CHECK(iso("2026-11-27T09:30:15,5Z") == 1795771815);
    CHECK(iso(" 2026-11-27T09:30:15Z ") == 1795771815);
    /* Not ISO 8601 with a zone. */
    CHECK(!ns_time_parse_iso8601("2026-11-27T09:30:15", &t));
    CHECK(!ns_time_parse_iso8601("2026-11-27", &t));
    CHECK(!ns_time_parse_iso8601("2026-02-30T00:00:00Z", &t));
    CHECK(!ns_time_parse_iso8601("2026-11-27T09:30:15Zjunk", &t));
    CHECK(!ns_time_parse_iso8601("2026-11-27T09:30:15.Z", &t));
    CHECK(!ns_time_parse_iso8601("Fri, 27 Nov 2026 09:30:15 GMT", &t));
    CHECK(!ns_time_parse_iso8601("", &t));
    CHECK(!ns_time_parse_iso8601(NULL, &t));
}

static bool
list_is(const char *const *list, const char *const *want)
{
    int i = 0;
    for (; list[i] && want[i]; i++)
        if (strcmp(list[i], want[i]) != 0) {
            fprintf(stderr, "  at %d: \"%s\", wanted \"%s\"\n", i, list[i], want[i]);
            return false;
        }
    if (list[i] || want[i])
        fprintf(stderr, "  at %d: \"%s\", wanted \"%s\"\n", i,
                list[i] ? list[i] : "(end)", want[i] ? want[i] : "(end)");
    return !list[i] && !want[i];
}

static void
test_environment(void)
{
    ns_setenv("LANGUAGE", "nb_NO.UTF-8:sr_RS@latin:en", true);
    ns_setenv("LC_ALL", "de_DE.UTF-8", true);
    ns_setenv("LANG", "fr_FR", true);
    static const char *const languages[] = {
        "nb_NO", "nb", "sr_RS@latin", "sr_RS", "sr@latin", "sr", "en",
        "de_DE", "de", "C", NULL,
    };
    CHECK(list_is(ns_sys_language_names(), languages));

#ifndef _WIN32
    ns_setenv("XDG_DATA_DIRS", "/opt/share:/usr/share::/opt/share", true);
    static const char *const dirs[] = { "/opt/share", "/usr/share", NULL };
    CHECK(list_is(ns_sys_data_dirs(), dirs));
#endif
    CHECK(ns_sys_num_processors() >= 1);
    CHECK(ns_sys_os_name() && *ns_sys_os_name());
    printf("system: %s, %d processors\n", ns_sys_os_name(), ns_sys_num_processors());
}

static void
test_files(const char *dir)
{
#ifndef _WIN32
    char path[1024], link[1024], config[1024];
    char *contents = NULL;

    /* A private file is the owner's alone, and replaced as a whole. */
    SDL_snprintf(path, sizeof path, "%s/private.txt", dir);
    CHECK(ns_fs_set_contents_mode(path, "one", -1, 0600));
    struct stat st;
    CHECK(stat(path, &st) == 0 && (st.st_mode & 0777) == 0600);
    CHECK(ns_fs_set_contents_mode(path, "two!", -1, 0600));
    CHECK(stat(path, &st) == 0 && (st.st_mode & 0777) == 0600 && st.st_size == 4);
    CHECK(ns_fs_get_contents(path, &contents, NULL) && !strcmp(contents, "two!"));
    free(contents);

    SDL_snprintf(link, sizeof link, "%s/link", dir);
    (void)unlink(link); /* from an earlier run */
    CHECK(symlink("private.txt", link) == 0);
    char *target = ns_fs_read_link(link);
    CHECK(target && !strcmp(target, "private.txt"));
    free(target);
    CHECK(ns_fs_read_link(path) == NULL);
    CHECK(ns_fs_read_link("/nonexistent/link") == NULL);

    /* The Downloads folder the user chose; the last line that names one
     * counts, and one that is not a path does not. */
    SDL_snprintf(config, sizeof config, "%s/user-dirs.dirs", dir);
    CHECK(ns_fs_set_contents(config,
        "# comment\n"
        "XDG_DESKTOP_DIR=\"$HOME/Desktop\"\n"
        "XDG_DOWNLOAD_DIR=\"/first\"\n"
        "  XDG_DOWNLOAD_DIR = \"$HOME/Hentet/\"\n"
        "XDG_DOWNLOAD_DIR=\"relative\"\n"
        "XDG_DOWNLOAD_DIRECTORY=\"/other\"\n", -1));
    const char *downloads = ns_fs_user_download_dir();
    char want[1024];
    SDL_snprintf(want, sizeof want, "%s/Hentet", ns_fs_home_dir());
    CHECK(downloads && !strcmp(downloads, want));
#else
    (void)dir;
#endif
}

int
main(void)
{
    const char *dir = getenv("NS_SYS_TEST_DIR");
    /* Before anything asks where the user's folders are. */
    if (dir && *dir) {
        ns_fs_mkdir_p(dir, 0700);
        ns_setenv("XDG_CONFIG_HOME", dir, true);
        ns_setenv("HOME", "/home/tester", true);
    }
    test_time();
    test_environment();
    if (dir && *dir)
        test_files(dir);
    if (g_failed) {
        fprintf(stderr, "%d check(s) failed\n", g_failed);
        return 1;
    }
    printf("ns_sys: all checks passed\n");
    return 0;
}
