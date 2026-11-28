/* Weststar SDL Browser — nscore: C strings, string vectors, ASCII,
 * printf and number conversions.
 *
 * Formatting is C99's printf on every platform (on Windows, MinGW's own
 * C99 printf in place of the old C runtime's), so %zu, %lld and the
 * 64-bit formats work wherever the browser runs. The ASCII functions and
 * ns_ascii_strtod/ns_ascii_dtostr ignore the locale.
 *
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_STRFUNCS_H
#define NS_STRFUNCS_H

#include "ns-base.h"

NS_BEGIN_DECLS

/* Character classes of ns_ascii_table. */
enum {
    NS_ASCII_ALNUM  = 1 << 0,
    NS_ASCII_ALPHA  = 1 << 1,
    NS_ASCII_CNTRL  = 1 << 2,
    NS_ASCII_DIGIT  = 1 << 3,
    NS_ASCII_GRAPH  = 1 << 4,
    NS_ASCII_LOWER  = 1 << 5,
    NS_ASCII_PRINT  = 1 << 6,
    NS_ASCII_PUNCT  = 1 << 7,
    NS_ASCII_SPACE  = 1 << 8,
    NS_ASCII_UPPER  = 1 << 9,
    NS_ASCII_XDIGIT = 1 << 10,
};

extern const uint16_t *const ns_ascii_table;

#define ns_ascii_isalnum(c)  ((ns_ascii_table[(unsigned char)(c)] & NS_ASCII_ALNUM) != 0)
#define ns_ascii_isalpha(c)  ((ns_ascii_table[(unsigned char)(c)] & NS_ASCII_ALPHA) != 0)
#define ns_ascii_iscntrl(c)  ((ns_ascii_table[(unsigned char)(c)] & NS_ASCII_CNTRL) != 0)
#define ns_ascii_isdigit(c)  ((ns_ascii_table[(unsigned char)(c)] & NS_ASCII_DIGIT) != 0)
#define ns_ascii_isgraph(c)  ((ns_ascii_table[(unsigned char)(c)] & NS_ASCII_GRAPH) != 0)
#define ns_ascii_islower(c)  ((ns_ascii_table[(unsigned char)(c)] & NS_ASCII_LOWER) != 0)
#define ns_ascii_isprint(c)  ((ns_ascii_table[(unsigned char)(c)] & NS_ASCII_PRINT) != 0)
#define ns_ascii_ispunct(c)  ((ns_ascii_table[(unsigned char)(c)] & NS_ASCII_PUNCT) != 0)
#define ns_ascii_isspace(c)  ((ns_ascii_table[(unsigned char)(c)] & NS_ASCII_SPACE) != 0)
#define ns_ascii_isupper(c)  ((ns_ascii_table[(unsigned char)(c)] & NS_ASCII_UPPER) != 0)
#define ns_ascii_isxdigit(c) ((ns_ascii_table[(unsigned char)(c)] & NS_ASCII_XDIGIT) != 0)

char ns_ascii_tolower(char c);
char ns_ascii_toupper(char c);
int ns_ascii_digit_value(char c);
int ns_ascii_xdigit_value(char c);
int ns_ascii_strcasecmp(const char *s1, const char *s2);
int ns_ascii_strncasecmp(const char *s1, const char *s2, size_t n);
char *ns_ascii_strdown(const char *str, ptrdiff_t len) NS_MALLOC_ATTR;
char *ns_ascii_strup(const char *str, ptrdiff_t len) NS_MALLOC_ATTR;

/* Room for any double ns_ascii_dtostr writes. */
#define NS_ASCII_DTOSTR_BUF_SIZE (29 + 10)

double ns_ascii_strtod(const char *nptr, char **endptr);
char *ns_ascii_dtostr(char *buffer, int buf_len, double d);
char *ns_ascii_formatd(char *buffer, int buf_len, const char *format, double d);
int64_t ns_ascii_strtoll(const char *nptr, char **endptr, unsigned int base);
uint64_t ns_ascii_strtoull(const char *nptr, char **endptr, unsigned int base);

char *ns_strdup(const char *str) NS_MALLOC_ATTR;
char *ns_strndup(const char *str, size_t n) NS_MALLOC_ATTR;
char *ns_strdup_printf(const char *format, ...) NS_PRINTF(1, 2) NS_MALLOC_ATTR;
char *ns_strdup_vprintf(const char *format, va_list args) NS_PRINTF(1, 0) NS_MALLOC_ATTR;
int ns_snprintf(char *string, unsigned long n, const char *format, ...) NS_PRINTF(3, 4);
int ns_vsnprintf(char *string, unsigned long n, const char *format, va_list args) NS_PRINTF(3, 0);
int ns_vasprintf(char **string, const char *format, va_list args) NS_PRINTF(2, 0);
char *ns_strconcat(const char *string1, ...) NS_SENTINEL NS_MALLOC_ATTR;

int ns_str_has_prefix(const char *str, const char *prefix);
int ns_str_has_suffix(const char *str, const char *suffix);
int ns_strcmp0(const char *str1, const char *str2);
char *ns_strchug(char *string);
char *ns_strchomp(char *string);
#define ns_strstrip(string) ns_strchomp(ns_strchug(string))
char *ns_strdelimit(char *string, const char *delimiters, char new_delimiter);
size_t ns_strlcpy(char *dest, const char *src, size_t dest_size);
char *ns_strstr_len(const char *haystack, ptrdiff_t haystack_len, const char *needle);
char *ns_strrstr_len(const char *haystack, ptrdiff_t haystack_len, const char *needle);
char *ns_strescape(const char *source, const char *exceptions) NS_MALLOC_ATTR;
const char *ns_strerror(int errnum);

char **ns_strsplit(const char *string, const char *delimiter, int max_tokens) NS_MALLOC_ATTR;
char **ns_strsplit_set(const char *string, const char *delimiters, int max_tokens) NS_MALLOC_ATTR;
char *ns_strjoinv(const char *separator, char **str_array) NS_MALLOC_ATTR;
char **ns_strdupv(char **str_array) NS_MALLOC_ATTR;
void ns_strfreev(char **str_array);
unsigned int ns_strv_length(char **str_array);

/* Sizes as "13.0 kB" (powers of 1000), and "1 byte"/"13 bytes" below 1 kB. */
char *ns_format_size(uint64_t size) NS_MALLOC_ATTR;

NS_END_DECLS

#endif
