/* Weststar SDL Browser — nscore: C strings, string vectors, ASCII,
 * printf and number conversions.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ns-strfuncs.h"
#include "ns-error.h"
#include "ns-mem.h"
#include "ns-private.h"
#include "ns-string.h"

#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the ASCII character table ---------------------------------------- */

#define NS_C_DIGIT(c) ((c) >= '0' && (c) <= '9')
#define NS_C_UPPER(c) ((c) >= 'A' && (c) <= 'Z')
#define NS_C_LOWER(c) ((c) >= 'a' && (c) <= 'z')
#define NS_C_ALPHA(c) (NS_C_UPPER(c) || NS_C_LOWER(c))
#define NS_C_ALNUM(c) (NS_C_ALPHA(c) || NS_C_DIGIT(c))
#define NS_C_GRAPH(c) ((c) > ' ' && (c) < 127)
#define NS_C_CLASS(c) ((uint16_t)( \
    (NS_C_ALNUM(c) ? NS_ASCII_ALNUM : 0) | \
    (NS_C_ALPHA(c) ? NS_ASCII_ALPHA : 0) | \
    (((c) < ' ' || (c) == 127) ? NS_ASCII_CNTRL : 0) | \
    (NS_C_DIGIT(c) ? NS_ASCII_DIGIT : 0) | \
    (NS_C_GRAPH(c) ? NS_ASCII_GRAPH : 0) | \
    (NS_C_LOWER(c) ? NS_ASCII_LOWER : 0) | \
    (((c) >= ' ' && (c) < 127) ? NS_ASCII_PRINT : 0) | \
    ((NS_C_GRAPH(c) && !NS_C_ALNUM(c)) ? NS_ASCII_PUNCT : 0) | \
    (((c) == ' ' || ((c) >= '\t' && (c) <= '\r')) ? NS_ASCII_SPACE : 0) | \
    (NS_C_UPPER(c) ? NS_ASCII_UPPER : 0) | \
    ((NS_C_DIGIT(c) || ((c) >= 'a' && (c) <= 'f') || ((c) >= 'A' && (c) <= 'F')) \
        ? NS_ASCII_XDIGIT : 0)))
#define NS_C_ROW(b) \
    NS_C_CLASS((b) + 0), NS_C_CLASS((b) + 1), NS_C_CLASS((b) + 2), \
    NS_C_CLASS((b) + 3), NS_C_CLASS((b) + 4), NS_C_CLASS((b) + 5), \
    NS_C_CLASS((b) + 6), NS_C_CLASS((b) + 7), NS_C_CLASS((b) + 8), \
    NS_C_CLASS((b) + 9), NS_C_CLASS((b) + 10), NS_C_CLASS((b) + 11), \
    NS_C_CLASS((b) + 12), NS_C_CLASS((b) + 13), NS_C_CLASS((b) + 14), \
    NS_C_CLASS((b) + 15)

/* ASCII only: the upper half of the table is all zero. */
static const uint16_t ns_ascii_table_data[256] = {
    NS_C_ROW(0), NS_C_ROW(16), NS_C_ROW(32), NS_C_ROW(48),
    NS_C_ROW(64), NS_C_ROW(80), NS_C_ROW(96), NS_C_ROW(112),
};
const uint16_t *const ns_ascii_table = ns_ascii_table_data;

char
ns_ascii_tolower(char c)
{
    return NS_C_UPPER(c) ? (char)(c - 'A' + 'a') : c;
}

char
ns_ascii_toupper(char c)
{
    return NS_C_LOWER(c) ? (char)(c - 'a' + 'A') : c;
}

int
ns_ascii_digit_value(char c)
{
    return NS_C_DIGIT(c) ? c - '0' : -1;
}

int
ns_ascii_xdigit_value(char c)
{
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return ns_ascii_digit_value(c);
}

int
ns_ascii_strcasecmp(const char *s1, const char *s2)
{
    if (!s1 || !s2) return 0;
    for (;; s1++, s2++) {
        int c1 = (unsigned char)ns_ascii_tolower(*s1);
        int c2 = (unsigned char)ns_ascii_tolower(*s2);
        if (c1 != c2) return c1 - c2;
        if (!c1) return 0;
    }
}

int
ns_ascii_strncasecmp(const char *s1, const char *s2, size_t n)
{
    if (!s1 || !s2) return 0;
    for (; n; n--, s1++, s2++) {
        int c1 = (unsigned char)ns_ascii_tolower(*s1);
        int c2 = (unsigned char)ns_ascii_tolower(*s2);
        if (c1 != c2) return c1 - c2;
        if (!c1) return 0;
    }
    return 0;
}

char *
ns_ascii_strdown(const char *str, ptrdiff_t len)
{
    if (!str) return NULL;
    if (len < 0) len = (ptrdiff_t)strlen(str);
    char *r = ns_strndup(str, (size_t)len);
    for (char *p = r; *p; p++) *p = ns_ascii_tolower(*p);
    return r;
}

char *
ns_ascii_strup(const char *str, ptrdiff_t len)
{
    if (!str) return NULL;
    if (len < 0) len = (ptrdiff_t)strlen(str);
    char *r = ns_strndup(str, (size_t)len);
    for (char *p = r; *p; p++) *p = ns_ascii_toupper(*p);
    return r;
}

/* ---- doubles, whatever the locale -------------------------------------- */

/* The C library's conversions follow LC_NUMERIC: where its decimal point is
 * not '.', the number is rewritten into the locale's spelling before
 * strtod sees it, and printf's output back into '.'. */

double
ns_ascii_strtod(const char *nptr, char **endptr)
{
    if (!nptr) {
        if (endptr) *endptr = NULL;
        return 0;
    }
    const char *dp = localeconv()->decimal_point;
    size_t dp_len = dp ? strlen(dp) : 0;
    if (dp_len == 0 || (dp[0] == '.' && dp[1] == '\0')) {
        errno = 0;
        return strtod(nptr, endptr);
    }

    /* Find the '.' that strtod would read as the decimal point. */
    const char *p = nptr;
    while (ns_ascii_isspace(*p)) p++;
    if (*p == '+' || *p == '-') p++;
    int hex = p[0] == '0' && (p[1] == 'x' || p[1] == 'X');
    if (hex) {
        p += 2;
        while (ns_ascii_isxdigit(*p)) p++;
    } else {
        while (ns_ascii_isdigit(*p)) p++;
    }
    if (*p != '.') {
        errno = 0;
        return strtod(nptr, endptr);
    }
    const char *point = p++;
    if (hex) {
        while (ns_ascii_isxdigit(*p)) p++;
        if (*p == 'p' || *p == 'P') p++;
    } else {
        while (ns_ascii_isdigit(*p)) p++;
        if (*p == 'e' || *p == 'E') p++;
    }
    if (*p == '+' || *p == '-') p++;
    while (ns_ascii_isdigit(*p)) p++;
    const char *end = p;

    size_t head = (size_t)(point - nptr);
    size_t tail = (size_t)(end - point - 1);
    char *copy = ns_malloc(head + dp_len + tail + 1);
    memcpy(copy, nptr, head);
    memcpy(copy + head, dp, dp_len);
    memcpy(copy + head + dp_len, point + 1, tail);
    copy[head + dp_len + tail] = '\0';

    char *fail = NULL;
    errno = 0;
    double val = strtod(copy, &fail);
    int saved = errno;
    if (endptr) {
        size_t used = (size_t)(fail - copy);
        if (used <= head)
            *endptr = (char *)nptr + used;
        else if (used < head + dp_len)
            *endptr = (char *)nptr + head;  /* stopped inside the point */
        else
            *endptr = (char *)nptr + used - dp_len + 1;
    }
    ns_free(copy);
    errno = saved;
    return val;
}

char *
ns_ascii_dtostr(char *buffer, int buf_len, double d)
{
    return ns_ascii_formatd(buffer, buf_len, "%.17g", d);
}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif

char *
ns_ascii_formatd(char *buffer, int buf_len, const char *format, double d)
{
    if (!buffer || !format || format[0] != '%') return NULL;
    if (strpbrk(format + 1, "'l%")) return NULL;
    char last = format[strlen(format) - 1];
    if (last == '\0' || !strchr("eEfFgG", last)) return NULL;

    snprintf(buffer, (size_t)buf_len, format, d);

    const char *dp = localeconv()->decimal_point;
    size_t dp_len = dp ? strlen(dp) : 0;
    if (dp_len == 0 || (dp[0] == '.' && dp[1] == '\0')) return buffer;
    char *p = buffer;
    while (ns_ascii_isspace(*p)) p++;
    if (*p == '+' || *p == '-') p++;
    while (ns_ascii_isdigit(*p)) p++;
    if (strncmp(p, dp, dp_len) == 0) {
        *p++ = '.';
        if (dp_len > 1) memmove(p, p + dp_len - 1, strlen(p + dp_len - 1) + 1);
    }
    return buffer;
}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

/* ---- integers ----------------------------------------------------------- */

/* strtoull's grammar in the C locale: the magnitude, its sign apart; sets
 * ERANGE on overflow and EINVAL for a base that is not 0 or 2..36. */
static uint64_t
ns_parse_u64(const char *nptr, char **endptr, unsigned int base, int *negative,
             int *overflow)
{
    *negative = 0;
    *overflow = 0;
    if (base == 1 || base > 36) {
        errno = EINVAL;
        if (endptr) *endptr = (char *)nptr;
        return 0;
    }
    const char *s = nptr;
    while (ns_ascii_isspace(*s)) s++;
    if (*s == '-') {
        *negative = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    if (*s == '0') {
        if ((base == 0 || base == 16) && (s[1] == 'x' || s[1] == 'X')) {
            s += 2;
            base = 16;
        } else if (base == 0) {
            base = 8;
        }
    } else if (base == 0) {
        base = 10;
    }
    const char *digits = s;
    uint64_t v = 0;
    for (;; s++) {
        unsigned int d;
        if (NS_C_DIGIT(*s)) d = (unsigned int)(*s - '0');
        else if (NS_C_LOWER(*s)) d = (unsigned int)(*s - 'a' + 10);
        else if (NS_C_UPPER(*s)) d = (unsigned int)(*s - 'A' + 10);
        else break;
        if (d >= base) break;
        if (v > (UINT64_MAX - d) / base) *overflow = 1;
        else v = v * base + d;
    }
    if (s == digits) {
        /* No digits: "0x" alone is the number 0 followed by "x". */
        if (endptr) {
            if (digits - nptr >= 2 && (digits[-1] == 'x' || digits[-1] == 'X') &&
                digits[-2] == '0')
                *endptr = (char *)digits - 1;
            else
                *endptr = (char *)nptr;
        }
        *negative = 0;
        return 0;
    }
    if (endptr) *endptr = (char *)s;
    if (*overflow) {
        errno = ERANGE;
        return UINT64_MAX;
    }
    return v;
}

int64_t
ns_ascii_strtoll(const char *nptr, char **endptr, unsigned int base)
{
    int negative, overflow;
    uint64_t v = ns_parse_u64(nptr, endptr, base, &negative, &overflow);
    if (negative && (overflow || v > (uint64_t)INT64_MAX + 1)) {
        errno = ERANGE;
        return INT64_MIN;
    }
    if (!negative && (overflow || v > (uint64_t)INT64_MAX)) {
        errno = ERANGE;
        return INT64_MAX;
    }
    return negative ? (int64_t)(0 - v) : (int64_t)v;
}

uint64_t
ns_ascii_strtoull(const char *nptr, char **endptr, unsigned int base)
{
    int negative, overflow;
    uint64_t v = ns_parse_u64(nptr, endptr, base, &negative, &overflow);
#ifndef _WIN32
    /* As glibc's strtoull_l, which GLib calls here: overflow stays
     * UINT64_MAX whatever the sign. On Windows GLib parses for itself and
     * negates that too. */
    if (overflow) return UINT64_MAX;
#endif
    return negative ? 0 - v : v;
}

/* ---- duplicating and formatting ----------------------------------------- */

char *
ns_strdup(const char *str)
{
    if (!str) return NULL;
    size_t n = strlen(str) + 1;
    char *r = ns_malloc(n);
    memcpy(r, str, n);
    return r;
}

char *
ns_strndup(const char *str, size_t n)
{
    if (!str) return NULL;
    char *r = ns_malloc(n + 1);
    size_t i = 0;
    for (; i < n && str[i]; i++) r[i] = str[i];
    memset(r + i, 0, n + 1 - i);
    return r;
}

int
ns_vsnprintf(char *string, unsigned long n, const char *format, va_list args)
{
    return vsnprintf(string, n, format, args);
}

int
ns_snprintf(char *string, unsigned long n, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    int r = vsnprintf(string, n, format, ap);
    va_end(ap);
    return r;
}

int
ns_vasprintf(char **string, const char *format, va_list args)
{
    va_list copy;
    va_copy(copy, args);
    int len = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (len < 0) {
        *string = NULL;
        return len;
    }
    *string = ns_malloc((size_t)len + 1);
    vsnprintf(*string, (size_t)len + 1, format, args);
    return len;
}

char *
ns_strdup_vprintf(const char *format, va_list args)
{
    char *s = NULL;
    ns_vasprintf(&s, format, args);
    return s;
}

char *
ns_strdup_printf(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    char *s = ns_strdup_vprintf(format, ap);
    va_end(ap);
    return s;
}

char *
ns_strconcat(const char *string1, ...)
{
    if (!string1) return NULL;
    size_t len = strlen(string1);
    va_list ap;
    va_start(ap, string1);
    for (const char *s = va_arg(ap, const char *); s; s = va_arg(ap, const char *))
        len += strlen(s);
    va_end(ap);

    char *r = ns_malloc(len + 1);
    char *p = r;
    size_t n = strlen(string1);
    memcpy(p, string1, n);
    p += n;
    va_start(ap, string1);
    for (const char *s = va_arg(ap, const char *); s; s = va_arg(ap, const char *)) {
        n = strlen(s);
        memcpy(p, s, n);
        p += n;
    }
    va_end(ap);
    *p = '\0';
    return r;
}

/* ---- searching and trimming --------------------------------------------- */

int
ns_str_has_prefix(const char *str, const char *prefix)
{
    if (!str || !prefix) return 0;
    return strncmp(str, prefix, strlen(prefix)) == 0;
}

int
ns_str_has_suffix(const char *str, const char *suffix)
{
    if (!str || !suffix) return 0;
    size_t n = strlen(str), m = strlen(suffix);
    return n >= m && strcmp(str + n - m, suffix) == 0;
}

int
ns_strcmp0(const char *str1, const char *str2)
{
    if (!str1) return -(str1 != str2);
    if (!str2) return str1 != str2;
    return strcmp(str1, str2);
}

char *
ns_strchug(char *string)
{
    if (!string) return NULL;
    char *p = string;
    while (*p && ns_ascii_isspace(*p)) p++;
    memmove(string, p, strlen(p) + 1);
    return string;
}

char *
ns_strchomp(char *string)
{
    if (!string) return NULL;
    size_t n = strlen(string);
    while (n > 0 && ns_ascii_isspace(string[n - 1])) string[--n] = '\0';
    return string;
}

char *
ns_strdelimit(char *string, const char *delimiters, char new_delimiter)
{
    if (!string) return NULL;
    if (!delimiters) delimiters = "_-|> <.";
    for (char *p = string; *p; p++)
        if (strchr(delimiters, *p)) *p = new_delimiter;
    return string;
}

size_t
ns_strlcpy(char *dest, const char *src, size_t dest_size)
{
    size_t n = strlen(src);
    if (dest_size) {
        size_t c = n < dest_size - 1 ? n : dest_size - 1;
        memcpy(dest, src, c);
        dest[c] = '\0';
    }
    return n;
}

char *
ns_strstr_len(const char *haystack, ptrdiff_t haystack_len, const char *needle)
{
    if (!haystack || !needle) return NULL;
    if (haystack_len < 0) return strstr(haystack, needle);
    size_t m = strlen(needle);
    if (m == 0) return (char *)haystack;
    if ((size_t)haystack_len < m) return NULL;
    const char *last = haystack + haystack_len - m;
    for (const char *p = haystack; p <= last && *p; p++)
        if (memcmp(p, needle, m) == 0) return (char *)p;
    return NULL;
}

char *
ns_strrstr_len(const char *haystack, ptrdiff_t haystack_len, const char *needle)
{
    if (!haystack || !needle) return NULL;
    size_t m = strlen(needle);
    const char *end;
    if (haystack_len < 0) {
        end = haystack + strlen(haystack);
    } else {
        end = haystack;
        while (end < haystack + haystack_len && *end) end++;
    }
    if ((size_t)(end - haystack) < m) return NULL;
    for (const char *p = end - m; p >= haystack; p--) {
        if (memcmp(p, needle, m) == 0) return (char *)p;
        if (p == haystack) break;
    }
    return NULL;
}

char *
ns_strescape(const char *source, const char *exceptions)
{
    if (!source) return NULL;
    unsigned char keep[256] = { 0 };
    if (exceptions)
        for (const unsigned char *e = (const unsigned char *)exceptions; *e; e++)
            keep[*e] = 1;
    char *dest = ns_malloc(strlen(source) * 4 + 1);
    char *q = dest;
    for (const unsigned char *p = (const unsigned char *)source; *p; p++) {
        unsigned char c = *p;
        const char *esc = NULL;
        if (!keep[c]) {
            switch (c) {
            case '\b': esc = "\\b"; break;
            case '\f': esc = "\\f"; break;
            case '\n': esc = "\\n"; break;
            case '\r': esc = "\\r"; break;
            case '\t': esc = "\\t"; break;
            case '\v': esc = "\\v"; break;
            case '\\': esc = "\\\\"; break;
            case '"': esc = "\\\""; break;
            default: break;
            }
        }
        if (esc) {
            *q++ = esc[0];
            *q++ = esc[1];
        } else if (!keep[c] && (c < ' ' || c >= 0177)) {
            *q++ = '\\';
            *q++ = (char)('0' + ((c >> 6) & 7));
            *q++ = (char)('0' + ((c >> 3) & 7));
            *q++ = (char)('0' + (c & 7));
        } else {
            *q++ = (char)c;
        }
    }
    *q = '\0';
    return dest;
}

const char *
ns_strerror(int errnum)
{
    static NsSpinLock lock = NS_SPIN_LOCK_INIT;
    ns_spin_lock(&lock);
    const char *msg = strerror(errnum);
    const char *r = ns_intern_string(msg ? msg : "Unknown error");
    ns_spin_unlock(&lock);
    return r;
}

/* ---- string vectors ------------------------------------------------------ */

char **
ns_strsplit(const char *string, const char *delimiter, int max_tokens)
{
    if (!string || !delimiter || !*delimiter) return NULL;
    if (max_tokens < 1) max_tokens = INT_MAX;
    size_t dlen = strlen(delimiter);
    size_t cap = 8, n = 0;
    char **v = ns_malloc_n(cap, sizeof *v);
    const char *rest = string;
    const char *s = strstr(rest, delimiter);
    while (--max_tokens && s) {
        if (n + 2 > cap) v = ns_realloc_n(v, cap *= 2, sizeof *v);
        v[n++] = ns_strndup(rest, (size_t)(s - rest));
        rest = s + dlen;
        s = strstr(rest, delimiter);
    }
    if (n + 2 > cap) v = ns_realloc_n(v, cap += 2, sizeof *v);
    if (*string) v[n++] = ns_strdup(rest);
    v[n] = NULL;
    return v;
}

char **
ns_strsplit_set(const char *string, const char *delimiters, int max_tokens)
{
    if (!string || !delimiters) return NULL;
    if (max_tokens < 1) max_tokens = INT_MAX;
    if (!*string) return ns_malloc0(sizeof(char *));
    unsigned char is_delim[256] = { 0 };
    for (const unsigned char *d = (const unsigned char *)delimiters; *d; d++)
        is_delim[*d] = 1;
    size_t cap = 8, n = 0;
    char **v = ns_malloc_n(cap, sizeof *v);
    const char *cur = string, *s = string;
    for (; *s; s++) {
        if (is_delim[(unsigned char)*s] && (int)n + 1 < max_tokens) {
            if (n + 2 > cap) v = ns_realloc_n(v, cap *= 2, sizeof *v);
            v[n++] = ns_strndup(cur, (size_t)(s - cur));
            cur = s + 1;
        }
    }
    if (n + 2 > cap) v = ns_realloc_n(v, cap += 2, sizeof *v);
    v[n++] = ns_strndup(cur, (size_t)(s - cur));
    v[n] = NULL;
    return v;
}

char *
ns_strjoinv(const char *separator, char **str_array)
{
    if (!str_array) return NULL;
    if (!separator) separator = "";
    size_t slen = strlen(separator), len = 0, i;
    for (i = 0; str_array[i]; i++) len += strlen(str_array[i]) + (i ? slen : 0);
    char *r = ns_malloc(len + 1);
    char *p = r;
    for (i = 0; str_array[i]; i++) {
        if (i) {
            memcpy(p, separator, slen);
            p += slen;
        }
        size_t n = strlen(str_array[i]);
        memcpy(p, str_array[i], n);
        p += n;
    }
    *p = '\0';
    return r;
}

char **
ns_strdupv(char **str_array)
{
    if (!str_array) return NULL;
    size_t n = 0;
    while (str_array[n]) n++;
    char **v = ns_malloc_n(n + 1, sizeof *v);
    for (size_t i = 0; i < n; i++) v[i] = ns_strdup(str_array[i]);
    v[n] = NULL;
    return v;
}

void
ns_strfreev(char **str_array)
{
    if (!str_array) return;
    for (char **p = str_array; *p; p++) ns_free(*p);
    ns_free(str_array);
}

unsigned int
ns_strv_length(char **str_array)
{
    unsigned int n = 0;
    if (str_array)
        while (str_array[n]) n++;
    return n;
}

char *
ns_format_size(uint64_t size)
{
    static const char *const units[] = { "kB", "MB", "GB", "TB", "PB", "EB" };
    if (size < 1000)
        return ns_strdup_printf("%u %s", (unsigned int)size,
                                size == 1 ? "byte" : "bytes");
    uint64_t factor = 1000;
    size_t i = 0;
    while (i + 1 < sizeof units / sizeof units[0] && size >= factor * 1000) {
        factor *= 1000;
        i++;
    }
    /* A no-break space between number and unit, as newer GLib (2.90)
     * writes it; 2.80 wrote a plain space. */
    return ns_strdup_printf("%.1f\xc2\xa0%s", (double)size / (double)factor, units[i]);
}
