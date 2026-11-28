/* Weststar SDL Browser — nscore: growing strings (GString).
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ns-string.h"
#include "ns-mem.h"
#include "ns-strfuncs.h"

#include <stdio.h>
#include <string.h>

static size_t
ns_nearest_pow(size_t n)
{
    size_t p = 1;
    while (p < n && p < SIZE_MAX / 2 + 1) p <<= 1;
    return p < n ? SIZE_MAX : p;
}

/* Room for len more bytes and the NUL. */
static void
ns_string_expand(NsString *s, size_t len)
{
    if (len >= SIZE_MAX - s->len)
        ns_mem_fatal("string would overflow, adding", len, 0);
    if (s->len + len >= s->allocated_len) {
        s->allocated_len = ns_nearest_pow(s->len + len + 1);
        s->str = ns_realloc(s->str, s->allocated_len);
    }
}

NsString *
ns_string_sized_new(size_t dfl_size)
{
    NsString *s = ns_malloc(sizeof *s);
    s->str = NULL;
    s->len = 0;
    s->allocated_len = 0;
    ns_string_expand(s, dfl_size > 64 ? dfl_size : 64);
    s->str[0] = '\0';
    return s;
}

NsString *
ns_string_new(const char *init)
{
    if (!init || !*init) return ns_string_sized_new(2);
    size_t n = strlen(init);
    NsString *s = ns_string_sized_new(n + 2);
    ns_string_append_len(s, init, (ptrdiff_t)n);
    return s;
}

NsString *
ns_string_new_len(const char *init, ptrdiff_t len)
{
    if (len < 0) return ns_string_new(init);
    NsString *s = ns_string_sized_new((size_t)len);
    if (init) ns_string_append_len(s, init, len);
    return s;
}

char *
ns_string_free(NsString *string, int free_segment)
{
    if (!string) return NULL;
    char *seg = NULL;
    if (free_segment) ns_free(string->str);
    else seg = string->str;
    ns_free(string);
    return seg;
}

char *
ns_string_free_and_steal(NsString *string)
{
    return ns_string_free(string, 0);
}

NsString *
ns_string_assign(NsString *string, const char *rval)
{
    if (string->str != rval) {
        /* rval may point into the string itself. */
        char *copy = ns_strdup(rval);
        ns_string_truncate(string, 0);
        ns_string_append(string, copy);
        ns_free(copy);
    }
    return string;
}

NsString *
ns_string_truncate(NsString *string, size_t len)
{
    if (len < string->len) string->len = len;
    string->str[string->len] = '\0';
    return string;
}

NsString *
ns_string_set_size(NsString *string, size_t len)
{
    if (len >= string->allocated_len) ns_string_expand(string, len - string->len);
    string->len = len;
    string->str[len] = '\0';
    return string;
}

NsString *
ns_string_insert_len(NsString *string, ptrdiff_t pos, const char *val, ptrdiff_t len)
{
    if (!string) return NULL;
    if (len == 0) return string;
    if (!val) return string;
    size_t n = len < 0 ? strlen(val) : (size_t)len;
    size_t at = pos < 0 ? string->len : (size_t)pos;
    if (at > string->len) return string;

    /* val may lie inside the string's own buffer. */
    if (val >= string->str && val <= string->str + string->len) {
        size_t offset = (size_t)(val - string->str);
        ns_string_expand(string, n);
        val = string->str + offset;
        if (at < string->len)
            memmove(string->str + at + n, string->str + at, string->len - at);
        if (offset < at) {
            size_t before = offset + n <= at ? n : at - offset;
            memcpy(string->str + at, val, before);
            if (before < n)
                memcpy(string->str + at + before, val + before + n, n - before);
        } else {
            memcpy(string->str + at, val + n, n);
        }
    } else {
        ns_string_expand(string, n);
        if (at < string->len)
            memmove(string->str + at + n, string->str + at, string->len - at);
        memcpy(string->str + at, val, n);
    }
    string->len += n;
    string->str[string->len] = '\0';
    return string;
}

NsString *
ns_string_append(NsString *string, const char *val)
{
    return ns_string_insert_len(string, -1, val, -1);
}

NsString *
ns_string_append_len(NsString *string, const char *val, ptrdiff_t len)
{
    return ns_string_insert_len(string, -1, val, len);
}

NsString *
ns_string_append_c(NsString *string, char c)
{
    if (string->len + 1 >= string->allocated_len) ns_string_expand(string, 1);
    string->str[string->len++] = c;
    string->str[string->len] = '\0';
    return string;
}

NsString *
ns_string_prepend(NsString *string, const char *val)
{
    return ns_string_insert_len(string, 0, val, -1);
}

NsString *
ns_string_prepend_len(NsString *string, const char *val, ptrdiff_t len)
{
    return ns_string_insert_len(string, 0, val, len);
}

NsString *
ns_string_insert(NsString *string, ptrdiff_t pos, const char *val)
{
    return ns_string_insert_len(string, pos, val, -1);
}

NsString *
ns_string_insert_c(NsString *string, ptrdiff_t pos, char c)
{
    size_t at = pos < 0 ? string->len : (size_t)pos;
    if (at > string->len) return string;
    ns_string_expand(string, 1);
    memmove(string->str + at + 1, string->str + at, string->len - at);
    string->str[at] = c;
    string->len++;
    string->str[string->len] = '\0';
    return string;
}

NsString *
ns_string_prepend_c(NsString *string, char c)
{
    return ns_string_insert_c(string, 0, c);
}

NsString *
ns_string_append_unichar(NsString *string, uint32_t wc)
{
    char buf[6];
    int n;
    unsigned char first;
    if (wc < 0x80) { first = 0; n = 1; }
    else if (wc < 0x800) { first = 0xc0; n = 2; }
    else if (wc < 0x10000) { first = 0xe0; n = 3; }
    else if (wc < 0x200000) { first = 0xf0; n = 4; }
    else if (wc < 0x4000000) { first = 0xf8; n = 5; }
    else { first = 0xfc; n = 6; }
    for (int i = n - 1; i > 0; i--) {
        buf[i] = (char)((wc & 0x3f) | 0x80);
        wc >>= 6;
    }
    buf[0] = (char)(wc | first);
    return ns_string_insert_len(string, -1, buf, n);
}

NsString *
ns_string_erase(NsString *string, ptrdiff_t pos, ptrdiff_t len)
{
    if (!string || pos < 0 || (size_t)pos > string->len) return string;
    size_t n = len < 0 ? string->len - (size_t)pos : (size_t)len;
    if ((size_t)pos + n > string->len) return string;
    if ((size_t)pos + n < string->len)
        memmove(string->str + pos, string->str + pos + n, string->len - ((size_t)pos + n));
    string->len -= n;
    string->str[string->len] = '\0';
    return string;
}

void
ns_string_append_vprintf(NsString *string, const char *format, va_list args)
{
    va_list copy;
    va_copy(copy, args);
    int len = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (len <= 0) return;
    ns_string_expand(string, (size_t)len);
    vsnprintf(string->str + string->len, (size_t)len + 1, format, args);
    string->len += (size_t)len;
}

void
ns_string_append_printf(NsString *string, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    ns_string_append_vprintf(string, format, ap);
    va_end(ap);
}

void
ns_string_printf(NsString *string, const char *format, ...)
{
    ns_string_truncate(string, 0);
    va_list ap;
    va_start(ap, format);
    ns_string_append_vprintf(string, format, ap);
    va_end(ap);
}
