/* Weststar SDL Browser — nscore: key files (GKeyFile), the INI files the
 * browser keeps per origin.
 *
 * What ns_key_file_to_data writes is byte for byte what GLib's
 * g_key_file_to_data writes for the same calls, and everything GLib writes
 * loads back: groups in the order they were made, a blank line after each,
 * keys in the order they were set, values escaped (\s for leading space,
 * \t \n \r \\). Comments are not kept (the browser never asks GLib to).
 *
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_KEYFILE_H
#define NS_KEYFILE_H

#include "ns-base.h"
#include "ns-error.h"

NS_BEGIN_DECLS

typedef struct NsKeyFile NsKeyFile;

typedef enum {
    NS_KEY_FILE_NONE = 0,
    NS_KEY_FILE_KEEP_COMMENTS = 1 << 0,
    NS_KEY_FILE_KEEP_TRANSLATIONS = 1 << 1
} NsKeyFileFlags;

typedef enum {
    NS_KEY_FILE_ERROR_UNKNOWN_ENCODING,
    NS_KEY_FILE_ERROR_PARSE,
    NS_KEY_FILE_ERROR_NOT_FOUND,
    NS_KEY_FILE_ERROR_KEY_NOT_FOUND,
    NS_KEY_FILE_ERROR_GROUP_NOT_FOUND,
    NS_KEY_FILE_ERROR_INVALID_VALUE
} NsKeyFileError;

NsQuark ns_key_file_error_quark(void);
#define NS_KEY_FILE_ERROR ns_key_file_error_quark()

NsKeyFile *ns_key_file_new(void);
void ns_key_file_free(NsKeyFile *key_file);
int ns_key_file_load_from_data(NsKeyFile *key_file, const char *data, size_t length,
                               NsKeyFileFlags flags, NsError **error);
char *ns_key_file_to_data(NsKeyFile *key_file, size_t *length, NsError **error);
char **ns_key_file_get_groups(NsKeyFile *key_file, size_t *length);
char **ns_key_file_get_keys(NsKeyFile *key_file, const char *group_name, size_t *length,
                            NsError **error);
int ns_key_file_has_group(NsKeyFile *key_file, const char *group_name);
char *ns_key_file_get_value(NsKeyFile *key_file, const char *group_name,
                            const char *key, NsError **error);
void ns_key_file_set_value(NsKeyFile *key_file, const char *group_name,
                           const char *key, const char *value);
char *ns_key_file_get_string(NsKeyFile *key_file, const char *group_name,
                             const char *key, NsError **error);
void ns_key_file_set_string(NsKeyFile *key_file, const char *group_name,
                            const char *key, const char *string);
int ns_key_file_remove_group(NsKeyFile *key_file, const char *group_name,
                             NsError **error);
int ns_key_file_remove_key(NsKeyFile *key_file, const char *group_name,
                           const char *key, NsError **error);

NS_END_DECLS

#endif
