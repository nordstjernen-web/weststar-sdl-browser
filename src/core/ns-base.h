/* Weststar SDL Browser — nscore: the in-tree core library that takes the
 * place of GLib. Common types and attributes.
 *
 * nscore is plain C11 and includes nothing of GLib. Its functions carry
 * GLib's names with ns_ in place of g_, and its containers keep the public
 * fields of GLib's (str/len/allocated_len, pdata/len, data/len), so that
 * the engine's sources build against it unchanged through src/core/glib.h.
 * See docs/porting-to-sdl.md, stage 6.5.
 *
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_BASE_H
#define NS_BASE_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>

#ifdef __cplusplus
#define NS_BEGIN_DECLS extern "C" {
#define NS_END_DECLS }
#else
#define NS_BEGIN_DECLS
#define NS_END_DECLS
#endif

/* printf-style checking: C99's conversions on every platform, MinGW's
 * old C runtime included, since nscore formats with a C99 printf there. */
#if defined(__GNUC__) && !defined(__clang__)
#define NS_PRINTF(f, a) __attribute__((__format__(gnu_printf, f, a)))
#elif defined(__GNUC__) || defined(__clang__)
#define NS_PRINTF(f, a) __attribute__((__format__(__printf__, f, a)))
#else
#define NS_PRINTF(f, a)
#endif

#if defined(__GNUC__) || defined(__clang__)
#define NS_MALLOC_ATTR __attribute__((__malloc__))
#define NS_NORETURN __attribute__((__noreturn__))
#define NS_SENTINEL __attribute__((__sentinel__))
#define NS_WARN_UNUSED __attribute__((__warn_unused_result__))
#else
#define NS_MALLOC_ATTR
#define NS_NORETURN
#define NS_SENTINEL
#define NS_WARN_UNUSED
#endif

NS_BEGIN_DECLS

typedef uint32_t NsQuark;

typedef void (*NsDestroyNotify)(void *data);
typedef int (*NsCompareFunc)(const void *a, const void *b);
typedef int (*NsCompareDataFunc)(const void *a, const void *b, void *user_data);
typedef unsigned int (*NsHashFunc)(const void *key);
typedef int (*NsEqualFunc)(const void *a, const void *b);
typedef void *(*NsCopyFunc)(const void *src, void *user_data);
typedef void (*NsFunc)(void *data, void *user_data);

NS_END_DECLS

#endif
