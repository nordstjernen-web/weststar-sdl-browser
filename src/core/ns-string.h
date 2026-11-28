/* Weststar SDL Browser — nscore: growing strings (GString).
 *
 * str is always NUL-terminated, len excludes the NUL, and allocated_len is
 * the size of the buffer, as in GLib, whose fields the engine reads.
 *
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_STRING_H
#define NS_STRING_H

#include "ns-base.h"

NS_BEGIN_DECLS

typedef struct NsString {
    char *str;
    size_t len;
    size_t allocated_len;
} NsString;

NsString *ns_string_new(const char *init);
NsString *ns_string_new_len(const char *init, ptrdiff_t len);
NsString *ns_string_sized_new(size_t dfl_size);
char *ns_string_free(NsString *string, int free_segment);
char *ns_string_free_and_steal(NsString *string) NS_WARN_UNUSED;
NsString *ns_string_assign(NsString *string, const char *rval);
NsString *ns_string_truncate(NsString *string, size_t len);
NsString *ns_string_set_size(NsString *string, size_t len);
NsString *ns_string_insert_len(NsString *string, ptrdiff_t pos, const char *val,
                               ptrdiff_t len);
NsString *ns_string_append(NsString *string, const char *val);
NsString *ns_string_append_len(NsString *string, const char *val, ptrdiff_t len);
NsString *ns_string_append_c(NsString *string, char c);
NsString *ns_string_append_unichar(NsString *string, uint32_t wc);
NsString *ns_string_prepend(NsString *string, const char *val);
NsString *ns_string_prepend_c(NsString *string, char c);
NsString *ns_string_prepend_len(NsString *string, const char *val, ptrdiff_t len);
NsString *ns_string_insert(NsString *string, ptrdiff_t pos, const char *val);
NsString *ns_string_insert_c(NsString *string, ptrdiff_t pos, char c);
NsString *ns_string_erase(NsString *string, ptrdiff_t pos, ptrdiff_t len);
void ns_string_printf(NsString *string, const char *format, ...) NS_PRINTF(2, 3);
void ns_string_append_printf(NsString *string, const char *format, ...) NS_PRINTF(2, 3);
void ns_string_append_vprintf(NsString *string, const char *format, va_list args)
    NS_PRINTF(2, 0);

NS_END_DECLS

#endif
