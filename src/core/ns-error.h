/* Weststar SDL Browser — nscore: errors (GError) and quarks.
 *
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_ERROR_H
#define NS_ERROR_H

#include "ns-base.h"

NS_BEGIN_DECLS

typedef struct _NsError {
    NsQuark domain;
    int code;
    char *message;
} NsError;

NsError *ns_error_new(NsQuark domain, int code, const char *format, ...) NS_PRINTF(3, 4);
NsError *ns_error_new_literal(NsQuark domain, int code, const char *message);
NsError *ns_error_new_valist(NsQuark domain, int code, const char *format, va_list args)
    NS_PRINTF(3, 0);
void ns_error_free(NsError *error);
NsError *ns_error_copy(const NsError *error);
int ns_error_matches(const NsError *error, NsQuark domain, int code);
void ns_set_error(NsError **err, NsQuark domain, int code, const char *format, ...)
    NS_PRINTF(4, 5);
void ns_set_error_literal(NsError **err, NsQuark domain, int code, const char *message);
void ns_propagate_error(NsError **dest, NsError *src);
void ns_clear_error(NsError **err);

/* Strings that live as long as the program, each spelling stored once. */
NsQuark ns_quark_from_string(const char *string);
NsQuark ns_quark_from_static_string(const char *string);
NsQuark ns_quark_try_string(const char *string);
const char *ns_quark_to_string(NsQuark quark);
const char *ns_intern_string(const char *string);
const char *ns_intern_static_string(const char *string);

/* GLib's G_FILE_ERROR codes, in GLib's order. */
typedef enum {
    NS_FILE_ERROR_EXIST,
    NS_FILE_ERROR_ISDIR,
    NS_FILE_ERROR_ACCES,
    NS_FILE_ERROR_NAMETOOLONG,
    NS_FILE_ERROR_NOENT,
    NS_FILE_ERROR_NOTDIR,
    NS_FILE_ERROR_NXIO,
    NS_FILE_ERROR_NODEV,
    NS_FILE_ERROR_ROFS,
    NS_FILE_ERROR_TXTBSY,
    NS_FILE_ERROR_FAULT,
    NS_FILE_ERROR_LOOP,
    NS_FILE_ERROR_NOSPC,
    NS_FILE_ERROR_NOMEM,
    NS_FILE_ERROR_MFILE,
    NS_FILE_ERROR_NFILE,
    NS_FILE_ERROR_BADF,
    NS_FILE_ERROR_INVAL,
    NS_FILE_ERROR_PIPE,
    NS_FILE_ERROR_AGAIN,
    NS_FILE_ERROR_INTR,
    NS_FILE_ERROR_IO,
    NS_FILE_ERROR_PERM,
    NS_FILE_ERROR_NOSYS,
    NS_FILE_ERROR_FAILED
} NsFileError;

typedef enum {
    NS_CONVERT_ERROR_NO_CONVERSION,
    NS_CONVERT_ERROR_ILLEGAL_SEQUENCE,
    NS_CONVERT_ERROR_FAILED,
    NS_CONVERT_ERROR_PARTIAL_INPUT,
    NS_CONVERT_ERROR_BAD_URI,
    NS_CONVERT_ERROR_NOT_ABSOLUTE_PATH,
    NS_CONVERT_ERROR_NO_MEMORY,
    NS_CONVERT_ERROR_EMBEDDED_NUL
} NsConvertError;

NsQuark ns_file_error_quark(void);
NsQuark ns_convert_error_quark(void);
NsFileError ns_file_error_from_errno(int err_no);

#define NS_FILE_ERROR ns_file_error_quark()
#define NS_CONVERT_ERROR ns_convert_error_quark()

NS_END_DECLS

#endif
