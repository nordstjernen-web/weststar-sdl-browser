/* Weststar SDL Browser — nscore: escaping text for XML, as
 * g_markup_escape_text does.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ns-convert.h"
#include "ns-string.h"

#include <string.h>

char *
ns_markup_escape_text(const char *text, ptrdiff_t length)
{
    if (!text) return NULL;
    size_t n = length < 0 ? strlen(text) : (size_t)length;
    NsString *s = ns_string_sized_new(n);
    const unsigned char *p = (const unsigned char *)text, *end = p + n;
    while (p < end) {
        unsigned char c = *p;
        switch (c) {
        case '&': ns_string_append(s, "&amp;"); p++; continue;
        case '<': ns_string_append(s, "&lt;"); p++; continue;
        case '>': ns_string_append(s, "&gt;"); p++; continue;
        case '\'': ns_string_append(s, "&apos;"); p++; continue;
        case '"': ns_string_append(s, "&quot;"); p++; continue;
        default: break;
        }
        if ((c >= 0x1 && c <= 0x8) || c == 0xb || c == 0xc || (c >= 0xe && c <= 0x1f) ||
            c == 0x7f) {
            ns_string_append_printf(s, "&#x%x;", (unsigned int)c);
            p++;
        } else if (c == 0xc2 && p + 1 < end && p[1] >= 0x80 && p[1] <= 0x9f && p[1] != 0x85) {
            /* U+0080..U+009F but NEL, the C1 controls */
            ns_string_append_printf(s, "&#x%x;", (unsigned int)p[1]);
            p += 2;
        } else {
            ns_string_append_c(s, (char)c);
            p++;
        }
    }
    return ns_string_free(s, 0);
}
