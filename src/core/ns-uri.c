/* Weststar SDL Browser — nscore: percent-encoding (RFC 3986), form
 * parameters and file: URIs, with GLib's rules.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ns-convert.h"
#include "ns-mem.h"
#include "ns-path.h"
#include "ns-string.h"
#include "ns-strfuncs.h"
#include "ns-unicode.h"

#include <string.h>

static const char ns_hex_upper[] = "0123456789ABCDEF";

NsQuark
ns_uri_error_quark(void)
{
    return ns_quark_from_static_string("g-uri-quark");
}

static int
ns_uri_is_unreserved(unsigned char c)
{
    return ns_ascii_isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~';
}

char *
ns_uri_escape_string(const char *unescaped, const char *reserved_chars_allowed,
                     int allow_utf8)
{
    if (!unescaped) return NULL;
    size_t n = strlen(unescaped);
    NsString *s = ns_string_sized_new(n + n / 4);
    const unsigned char *p = (const unsigned char *)unescaped, *end = p + n;
    while (p < end) {
        if (allow_utf8 && *p >= 0x80) {
            ns_unichar c = ns_utf8_get_char_validated((const char *)p, end - p);
            if (c != (ns_unichar)-1 && c != (ns_unichar)-2 && c > 0) {
                int len = ns_utf8_skip[*p];
                ns_string_append_len(s, (const char *)p, len);
                p += len;
                continue;
            }
        }
        if (ns_uri_is_unreserved(*p) ||
            (reserved_chars_allowed && strchr(reserved_chars_allowed, *p))) {
            ns_string_append_c(s, (char)*p);
        } else {
            ns_string_append_c(s, '%');
            ns_string_append_c(s, ns_hex_upper[*p >> 4]);
            ns_string_append_c(s, ns_hex_upper[*p & 15]);
        }
        p++;
    }
    return ns_string_free(s, 0);
}

/* Decodes %XX (and with www_form '+' as a space). A '%' not followed by
 * two hex digits is an error unless relaxed; so is a decoded character in
 * illegal. Unless any_bytes, the result must be UTF-8. Returns the length,
 * or -1 with *out NULL. */
static ptrdiff_t
ns_uri_decode(char **out, const char *illegal, const char *start, size_t length,
              int www_form, int relaxed, int any_bytes, NsError **error)
{
    NsString *d = ns_string_sized_new(length + 1);
    const char *s = start, *end = start + length;
    for (; s < end; s++) {
        if (*s == '%') {
            if (s + 2 >= end || !ns_ascii_isxdigit(s[1]) || !ns_ascii_isxdigit(s[2])) {
                if (!relaxed) {
                    ns_set_error_literal(error, NS_URI_ERROR, NS_URI_ERROR_FAILED,
                                         "Invalid %-encoding in URI");
                    ns_string_free(d, 1);
                    *out = NULL;
                    return -1;
                }
                ns_string_append_c(d, *s);
                continue;
            }
            char c = (char)(ns_ascii_xdigit_value(s[1]) << 4 | ns_ascii_xdigit_value(s[2]));
            if (illegal && strchr(illegal, c)) {
                ns_set_error_literal(error, NS_URI_ERROR, NS_URI_ERROR_FAILED,
                                     "Illegal character in URI");
                ns_string_free(d, 1);
                *out = NULL;
                return -1;
            }
            ns_string_append_c(d, c);
            s += 2;
        } else if (www_form && *s == '+') {
            ns_string_append_c(d, ' ');
        } else {
            ns_string_append_c(d, *s);
        }
    }
    ptrdiff_t len = (ptrdiff_t)d->len;
    if (!any_bytes && !ns_utf8_validate_len(d->str, d->len, NULL)) {
        ns_set_error_literal(error, NS_URI_ERROR, NS_URI_ERROR_FAILED,
                             "Invalid UTF-8 in URI");
        ns_string_free(d, 1);
        *out = NULL;
        return -1;
    }
    *out = ns_string_free(d, 0);
    return len;
}

char *
ns_uri_unescape_segment(const char *escaped_string, const char *escaped_string_end,
                        const char *illegal_characters)
{
    if (!escaped_string) return NULL;
    size_t length = escaped_string_end ? (size_t)(escaped_string_end - escaped_string)
                                       : strlen(escaped_string);
    char *out;
    ptrdiff_t len = ns_uri_decode(&out, illegal_characters, escaped_string, length, 0, 0,
                                  1, NULL);
    if (len < 0) return NULL;
    if (memchr(out, '\0', (size_t)len)) {
        ns_free(out);
        return NULL;
    }
    return out;
}

char *
ns_uri_unescape_string(const char *escaped_string, const char *illegal_characters)
{
    return ns_uri_unescape_segment(escaped_string, NULL, illegal_characters);
}

static unsigned int
ns_str_ascii_case_hash(const void *v)
{
    uint32_t h = 5381;
    for (const char *p = v; *p; p++)
        h = (h << 5) + h + (uint32_t)(signed char)ns_ascii_toupper(*p);
    return h;
}

static int
ns_str_ascii_case_equal(const void *a, const void *b)
{
    return ns_ascii_strcasecmp(a, b) == 0;
}

NsHashTable *
ns_uri_parse_params(const char *params, ptrdiff_t length, const char *separators,
                    NsUriParamsFlags flags, NsError **error)
{
    if ((length != 0 && !params) || length < -1 || !separators) return NULL;
    NsHashTable *hash = (flags & NS_URI_PARAMS_CASE_INSENSITIVE)
        ? ns_hash_table_new_full(ns_str_ascii_case_hash, ns_str_ascii_case_equal, ns_free, ns_free)
        : ns_hash_table_new_full(ns_str_hash, ns_str_equal, ns_free, ns_free);
    const char *end = length < 0 ? params + strlen(params) : params + length;
    unsigned char is_sep[256] = { 0 };
    for (const unsigned char *s = (const unsigned char *)separators; *s; s++) is_sep[*s] = 1;
    int www = (flags & NS_URI_PARAMS_WWW_FORM) != 0;
    int relaxed = (flags & NS_URI_PARAMS_PARSE_RELAXED) != 0;

    const char *attr = params;
    while (attr < end) {
        const char *val_end = attr;
        while (val_end < end && !is_sep[(unsigned char)*val_end]) val_end++;
        const char *eq = memchr(attr, '=', (size_t)(val_end - attr));
        if (!eq) {
            ns_set_error_literal(error, NS_URI_ERROR, NS_URI_ERROR_FAILED,
                                 "Missing \xe2\x80\x98=\xe2\x80\x99 and parameter value");
            ns_hash_table_destroy(hash);
            return NULL;
        }
        char *a, *v;
        if (ns_uri_decode(&a, NULL, attr, (size_t)(eq - attr), www, relaxed, 0, error) < 0) {
            ns_hash_table_destroy(hash);
            return NULL;
        }
        if (ns_uri_decode(&v, NULL, eq + 1, (size_t)(val_end - eq - 1), www, relaxed, 0,
                          error) < 0) {
            ns_free(a);
            ns_hash_table_destroy(hash);
            return NULL;
        }
        ns_hash_table_insert(hash, a, v);
        attr = val_end + 1;
    }
    return hash;
}

/* ---- file: URIs ------------------------------------------------------------- */

/* What a file: URI keeps as it is: in the path, letters, digits and
 * "!$&'()*+,-./:=@_~"; in the host, letters, digits and "!'()*-./:@_~". */
static char *
ns_uri_escape_file_part(const char *string, int host)
{
    const char *keep = host ? "!'()*-./:@_~" : "!$&'()*+,-./:=@_~";
    NsString *s = ns_string_new(NULL);
    for (const unsigned char *p = (const unsigned char *)string; *p; p++) {
        if (ns_ascii_isalnum(*p) || (*p && strchr(keep, *p))) {
            ns_string_append_c(s, (char)*p);
        } else {
            ns_string_append_c(s, '%');
            ns_string_append_c(s, ns_hex_upper[*p >> 4]);
            ns_string_append_c(s, ns_hex_upper[*p & 15]);
        }
    }
    return ns_string_free(s, 0);
}

/* Labels of letters, digits and '-' (not at the end), the last starting
 * with a letter; an optional final dot. Empty is valid. */
static int
ns_hostname_validate(const char *hostname)
{
    const char *p = hostname;
    if (!*p) return 1;
    for (;;) {
        unsigned char c = (unsigned char)*p++;
        if (!ns_ascii_isalnum(c)) return 0;
        unsigned char first = c, last;
        do {
            last = c;
            c = (unsigned char)*p++;
        } while (ns_ascii_isalnum(c) || c == '-');
        if (last == '-') return 0;
        if (c == '\0' || (c == '.' && *p == '\0')) return ns_ascii_isalpha(first);
        if (c != '.') return 0;
    }
}

char *
ns_filename_to_uri(const char *filename, const char *hostname, NsError **error)
{
    if (!filename) return NULL;
    if (!ns_path_is_absolute(filename)) {
        ns_set_error(error, NS_CONVERT_ERROR, NS_CONVERT_ERROR_NOT_ABSOLUTE_PATH,
                     "The pathname \xe2\x80\x9c%s\xe2\x80\x9d is not an absolute path",
                     filename);
        return NULL;
    }
    if (hostname && !(ns_utf8_validate(hostname, -1, NULL) && ns_hostname_validate(hostname))) {
        ns_set_error_literal(error, NS_CONVERT_ERROR, NS_CONVERT_ERROR_ILLEGAL_SEQUENCE,
                             "Invalid hostname");
        return NULL;
    }
#ifdef _WIN32
    if (hostname && ns_ascii_strcasecmp(hostname, "localhost") == 0) hostname = NULL;
    char *path = ns_strdup(filename);
    for (char *p = path; *p; p++)
        if (*p == '\\') *p = '/';
#else
    const char *path = filename;
#endif
    char *host = hostname && *hostname ? ns_uri_escape_file_part(hostname, 1) : NULL;
    char *escaped = ns_uri_escape_file_part(path, 0);
    char *uri = ns_strconcat("file://", host ? host : "", *escaped != '/' ? "/" : "",
                             escaped, NULL);
    ns_free(host);
    ns_free(escaped);
#ifdef _WIN32
    ns_free(path);
#endif
    return uri;
}

/* %XX decoding for file: URIs: stops (giving NULL) at a bad escape, an
 * escaped NUL, an escaped character in illegal, or, with ascii_must_not,
 * an escaped ASCII character. */
static char *
ns_unescape_file_part(const char *escaped, size_t len, const char *illegal, int ascii_must_not)
{
    char *result = ns_malloc(len + 1);
    char *out = result;
    const char *in = escaped, *end = escaped + len;
    for (; in < end; in++) {
        int c = (unsigned char)*in;
        if (c == '%') {
            if (in + 3 > end) break;
            int hi = ns_ascii_xdigit_value(in[1]), lo = ns_ascii_xdigit_value(in[2]);
            if (hi < 0 || lo < 0) break;
            c = hi << 4 | lo;
            if (c <= 0) break;
            if (ascii_must_not && c <= 0x7f) break;
            if (strchr(illegal, c)) break;
            in += 2;
        }
        *out++ = (char)c;
    }
    *out = '\0';
    if (in != end) {
        ns_free(result);
        return NULL;
    }
    return result;
}

char *
ns_filename_from_uri(const char *uri, char **hostname, NsError **error)
{
    if (hostname) *hostname = NULL;
    if (!uri || ns_ascii_strncasecmp(uri, "file:/", 6) != 0) {
        ns_set_error(error, NS_CONVERT_ERROR, NS_CONVERT_ERROR_BAD_URI,
                     "The URI \xe2\x80\x9c%s\xe2\x80\x9d is not an absolute URI using the "
                     "\xe2\x80\x9c" "file\xe2\x80\x9d scheme", uri ? uri : "");
        return NULL;
    }
    char *temp = ns_strdup(uri);
    char *past = temp + 5;
    char *cut = strchr(past, '?');
    if (cut) *cut = '\0';
    cut = strchr(past, '#');
    if (cut) *cut = '\0';

    if (ns_ascii_strncasecmp(past, "///", 3) == 0) {
        past += 2;
    } else if (ns_ascii_strncasecmp(past, "//", 2) == 0) {
        past += 2;
        char *host_part = past;
        past = strchr(past, '/');
        if (!past) {
            ns_free(temp);
            ns_set_error(error, NS_CONVERT_ERROR, NS_CONVERT_ERROR_BAD_URI,
                         "The URI \xe2\x80\x9c%s\xe2\x80\x9d is invalid", uri);
            return NULL;
        }
        char *host = ns_unescape_file_part(host_part, (size_t)(past - host_part), "", 1);
        if (!host || !ns_hostname_validate(host)) {
            ns_free(host);
            ns_free(temp);
            ns_set_error(error, NS_CONVERT_ERROR, NS_CONVERT_ERROR_BAD_URI,
                         "The hostname of the URI \xe2\x80\x9c%s\xe2\x80\x9d is invalid", uri);
            return NULL;
        }
        if (hostname) *hostname = host;
        else ns_free(host);
    }

    char *filename = ns_unescape_file_part(past, strlen(past), "/", 0);
    if (!filename) {
        ns_free(temp);
        ns_set_error(error, NS_CONVERT_ERROR, NS_CONVERT_ERROR_BAD_URI,
                     "The URI \xe2\x80\x9c%s\xe2\x80\x9d contains invalidly escaped characters",
                     uri);
        return NULL;
    }
    size_t offs = 0;
#ifdef _WIN32
    if (hostname && *hostname && ns_ascii_strcasecmp(*hostname, "localhost") == 0) {
        ns_free(*hostname);
        *hostname = NULL;
    }
    for (char *p = filename; *p; p++)
        if (*p == '/') *p = '\\';
    if (filename[0] && ns_ascii_isalpha(filename[1])) {
        if (filename[2] == ':') {
            offs = 1;
        } else if (filename[2] == '|') {
            filename[2] = ':';
            offs = 1;
        }
    }
#endif
    char *result = ns_strdup(filename + offs);
    ns_free(filename);
    ns_free(temp);
    return result;
}
