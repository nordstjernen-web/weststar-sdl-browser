/* Weststar SDL Browser — nscore: Base64 (RFC 4648), as GLib reads and
 * writes it.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ns-convert.h"
#include "ns-mem.h"

#include <string.h>

static const char ns_b64_alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char *
ns_base64_encode(const unsigned char *data, size_t len)
{
    char *out = ns_malloc((len / 3 + 1) * 4 + 1);
    char *q = out;
    size_t i = 0;
    for (; i + 3 <= len; i += 3) {
        uint32_t v = (uint32_t)data[i] << 16 | (uint32_t)data[i + 1] << 8 | data[i + 2];
        *q++ = ns_b64_alphabet[v >> 18];
        *q++ = ns_b64_alphabet[(v >> 12) & 63];
        *q++ = ns_b64_alphabet[(v >> 6) & 63];
        *q++ = ns_b64_alphabet[v & 63];
    }
    if (len - i == 1) {
        uint32_t v = (uint32_t)data[i] << 16;
        *q++ = ns_b64_alphabet[v >> 18];
        *q++ = ns_b64_alphabet[(v >> 12) & 63];
        *q++ = '=';
        *q++ = '=';
    } else if (len - i == 2) {
        uint32_t v = (uint32_t)data[i] << 16 | (uint32_t)data[i + 1] << 8;
        *q++ = ns_b64_alphabet[v >> 18];
        *q++ = ns_b64_alphabet[(v >> 12) & 63];
        *q++ = ns_b64_alphabet[(v >> 6) & 63];
        *q++ = '=';
    }
    *q = '\0';
    return out;
}

/* The value of a Base64 character; '=' reads as 0, anything else that is
 * not Base64 as -1, to be skipped. */
static int
ns_b64_rank(unsigned char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    if (c == '=') return 0;
    return -1;
}

/* Every four Base64 characters give three bytes, less one for each '='
 * among the last two of the four. */
unsigned char *
ns_base64_decode(const char *text, size_t *out_len)
{
    size_t len = strlen(text);
    unsigned char *out = ns_malloc0(len / 4 * 3 + 1);
    unsigned char *q = out;
    uint32_t v = 0;
    int n = 0;
    unsigned char last0 = 0, last1 = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        int r = ns_b64_rank(*p);
        if (r < 0) continue;
        last1 = last0;
        last0 = *p;
        v = (v << 6) | (uint32_t)r;
        if (++n == 4) {
            *q++ = (unsigned char)(v >> 16);
            if (last1 != '=') *q++ = (unsigned char)(v >> 8);
            if (last0 != '=') *q++ = (unsigned char)v;
            n = 0;
        }
    }
    *out_len = (size_t)(q - out);
    return out;
}
