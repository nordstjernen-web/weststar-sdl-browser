/* Weststar SDL Browser — nscore: file names and files.
 *
 * File names are UTF-8 on Windows (the wide API is called underneath) and
 * the system's bytes elsewhere, as in GLib. On Windows both '\' and '/'
 * separate, and ns_build_filename keeps the separator the parts use.
 *
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_PATH_H
#define NS_PATH_H

#include "ns-base.h"

NS_BEGIN_DECLS

#ifdef _WIN32
#define NS_DIR_SEPARATOR '\\'
#define NS_DIR_SEPARATOR_S "\\"
#define NS_IS_DIR_SEPARATOR(c) ((c) == '\\' || (c) == '/')
#define NS_SEARCHPATH_SEPARATOR ';'
#define NS_SEARCHPATH_SEPARATOR_S ";"
#else
#define NS_DIR_SEPARATOR '/'
#define NS_DIR_SEPARATOR_S "/"
#define NS_IS_DIR_SEPARATOR(c) ((c) == '/')
#define NS_SEARCHPATH_SEPARATOR ':'
#define NS_SEARCHPATH_SEPARATOR_S ":"
#endif

char *ns_build_filename(const char *first_element, ...) NS_SENTINEL NS_MALLOC_ATTR;
char *ns_build_filenamev(char **args) NS_MALLOC_ATTR;
int ns_path_is_absolute(const char *file_name);
const char *ns_path_skip_root(const char *file_name);
char *ns_path_get_basename(const char *file_name) NS_MALLOC_ATTR;
char *ns_path_get_dirname(const char *file_name) NS_MALLOC_ATTR;
char *ns_canonicalize_filename(const char *filename, const char *relative_to) NS_MALLOC_ATTR;
char *ns_get_current_dir(void) NS_MALLOC_ATTR;
char *ns_find_program_in_path(const char *program) NS_MALLOC_ATTR;
char *ns_filename_display_name(const char *filename) NS_MALLOC_ATTR;

/* ns-file.c: gstdio's, over the wide calls on Windows. */
int ns_open(const char *filename, int flags, int mode);
int ns_utime(const char *filename, void *utb);
int ns_unlink(const char *filename);
int ns_remove(const char *filename);
int ns_rmdir(const char *filename);
int ns_rename(const char *oldfilename, const char *newfilename);
int ns_chmod(const char *filename, int mode);
int ns_mkdir(const char *filename, int mode);
void *ns_fopen(const char *filename, const char *mode);
#ifdef _WIN32
/* The system's text for a GetLastError() code, in UTF-8. */
char *ns_win32_error_message(int error) NS_MALLOC_ATTR;
#endif

NS_END_DECLS

#endif
