/* Weststar SDL Browser — nscore: encodings of text: Base64, XML escaping,
 * percent-encoding and file: URIs, each as GLib's.
 *
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_CONVERT_H
#define NS_CONVERT_H

#include "ns-base.h"
#include "ns-error.h"
#include "ns-hash.h"

NS_BEGIN_DECLS

/* ns-base64.c: no line breaks written; anything that is not Base64 is
 * skipped when reading. */
char *ns_base64_encode(const unsigned char *data, size_t len) NS_MALLOC_ATTR;
unsigned char *ns_base64_decode(const char *text, size_t *out_len) NS_MALLOC_ATTR;

/* ns-markup.c: & < > ' " and the C0 and C1 controls as entities. */
char *ns_markup_escape_text(const char *text, ptrdiff_t length) NS_MALLOC_ATTR;

/* ns-uri.c */
typedef enum {
    NS_URI_PARAMS_NONE = 0,
    NS_URI_PARAMS_CASE_INSENSITIVE = 1 << 0,
    NS_URI_PARAMS_WWW_FORM = 1 << 1,
    NS_URI_PARAMS_PARSE_RELAXED = 1 << 2
} NsUriParamsFlags;

typedef enum {
    NS_URI_ERROR_FAILED
} NsUriError;

NsQuark ns_uri_error_quark(void);
#define NS_URI_ERROR ns_uri_error_quark()

#define NS_URI_RESERVED_CHARS_GENERIC_DELIMITERS ":/?#[]@"
#define NS_URI_RESERVED_CHARS_SUBCOMPONENT_DELIMITERS "!$&'()*+,;="
#define NS_URI_RESERVED_CHARS_ALLOWED_IN_PATH_ELEMENT \
    NS_URI_RESERVED_CHARS_SUBCOMPONENT_DELIMITERS ":@"
#define NS_URI_RESERVED_CHARS_ALLOWED_IN_PATH \
    NS_URI_RESERVED_CHARS_ALLOWED_IN_PATH_ELEMENT "/"
#define NS_URI_RESERVED_CHARS_ALLOWED_IN_USERINFO \
    NS_URI_RESERVED_CHARS_SUBCOMPONENT_DELIMITERS ":"

char *ns_uri_escape_string(const char *unescaped, const char *reserved_chars_allowed,
                           int allow_utf8) NS_MALLOC_ATTR;
char *ns_uri_unescape_string(const char *escaped_string, const char *illegal_characters)
    NS_MALLOC_ATTR;
char *ns_uri_unescape_segment(const char *escaped_string, const char *escaped_string_end,
                              const char *illegal_characters) NS_MALLOC_ATTR;
NsHashTable *ns_uri_parse_params(const char *params, ptrdiff_t length,
                                 const char *separators, NsUriParamsFlags flags,
                                 NsError **error);
char *ns_filename_to_uri(const char *filename, const char *hostname, NsError **error)
    NS_MALLOC_ATTR;
char *ns_filename_from_uri(const char *uri, char **hostname, NsError **error)
    NS_MALLOC_ATTR;

NS_END_DECLS

#endif
