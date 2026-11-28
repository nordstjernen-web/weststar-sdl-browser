/* Weststar SDL Browser — GLib's names over nscore (meson -Dglib=native).
 *
 * With -Dglib=native, src/core is first on the include path, so the
 * engine's #include <glib.h> lands here: GLib's types as typedefs of
 * nscore's, GLib's functions as #defines of their ns_ names, and GLib's
 * macros written out. Nothing of GLib itself is included or linked.
 *
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_CORE_GLIB_H
#define NS_CORE_GLIB_H

#ifdef __G_LIB_H__
#error "GLib's own glib.h was included in a -Dglib=native build"
#endif

#include <errno.h>
#include <float.h>
#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "nscore.h"
#include "ns-unicode-glib.h"

/* ---- types ------------------------------------------------------------- */

typedef char gchar;
typedef short gshort;
typedef long glong;
typedef int gint;
typedef int gboolean;
typedef unsigned char guchar;
typedef unsigned short gushort;
typedef unsigned long gulong;
typedef unsigned int guint;
typedef float gfloat;
typedef double gdouble;
typedef void *gpointer;
typedef const void *gconstpointer;
typedef int8_t gint8;
typedef uint8_t guint8;
typedef int16_t gint16;
typedef uint16_t guint16;
typedef int32_t gint32;
typedef uint32_t guint32;
typedef int64_t gint64;
typedef uint64_t guint64;
typedef size_t gsize;
typedef ptrdiff_t gssize;
typedef int64_t goffset;
typedef intptr_t gintptr;
typedef uintptr_t guintptr;
typedef int64_t GTimeSpan;
typedef NsQuark GQuark;
typedef char **GStrv;
typedef int grefcount;
typedef int gatomicrefcount;

typedef NsDestroyNotify GDestroyNotify;
typedef NsCompareFunc GCompareFunc;
typedef NsCompareDataFunc GCompareDataFunc;
typedef NsHashFunc GHashFunc;
typedef NsEqualFunc GEqualFunc;
typedef NsCopyFunc GCopyFunc;
typedef NsFunc GFunc;
typedef NsHFunc GHFunc;
typedef NsHRFunc GHRFunc;

typedef NsString GString;
typedef NsArray GArray;
typedef NsPtrArray GPtrArray;
typedef NsByteArray GByteArray;
typedef NsGBytes GBytes;
typedef NsHashTable GHashTable;
typedef NsHashTableIter GHashTableIter;
typedef NsList GList;
typedef NsQueue GQueue;
typedef NsError GError;
typedef NsKeyFile GKeyFile;
typedef NsKeyFileFlags GKeyFileFlags;
typedef NsChecksum GChecksum;
typedef NsChecksumType GChecksumType;
typedef NsLogLevelFlags GLogLevelFlags;
typedef NsLogField GLogField;
typedef NsLogWriterOutput GLogWriterOutput;
typedef NsLogWriterFunc GLogWriterFunc;
typedef NsUriParamsFlags GUriParamsFlags;
typedef NsFileError GFileError;
typedef NsConvertError GConvertError;

/* ---- limits and formats ------------------------------------------------ */

#define G_MININT8 INT8_MIN
#define G_MAXINT8 INT8_MAX
#define G_MAXUINT8 UINT8_MAX
#define G_MININT16 INT16_MIN
#define G_MAXINT16 INT16_MAX
#define G_MAXUINT16 UINT16_MAX
#define G_MININT32 INT32_MIN
#define G_MAXINT32 INT32_MAX
#define G_MAXUINT32 UINT32_MAX
#define G_MININT64 INT64_MIN
#define G_MAXINT64 INT64_MAX
#define G_MAXUINT64 UINT64_MAX
#define G_MINSHORT SHRT_MIN
#define G_MAXSHORT SHRT_MAX
#define G_MAXUSHORT USHRT_MAX
#define G_MININT INT_MIN
#define G_MAXINT INT_MAX
#define G_MAXUINT UINT_MAX
#define G_MINLONG LONG_MIN
#define G_MAXLONG LONG_MAX
#define G_MAXULONG ULONG_MAX
#define G_MAXSIZE SIZE_MAX
#define G_MINSSIZE PTRDIFF_MIN
#define G_MAXSSIZE PTRDIFF_MAX
#define G_MINOFFSET G_MININT64
#define G_MAXOFFSET G_MAXINT64
#define G_MINFLOAT FLT_MIN
#define G_MAXFLOAT FLT_MAX
#define G_MINDOUBLE DBL_MIN
#define G_MAXDOUBLE DBL_MAX

#define G_GINT64_CONSTANT(val) (INT64_C(val))
#define G_GUINT64_CONSTANT(val) (UINT64_C(val))

/* GLib's spellings: "ll" on Windows and wherever a 64-bit integer is a
 * long long, "l" where it is a long. */
#if defined(_WIN32) || LONG_MAX != INT64_MAX
#define G_GINT64_MODIFIER "ll"
#else
#define G_GINT64_MODIFIER "l"
#endif
#define G_GINT64_FORMAT G_GINT64_MODIFIER "i"
#define G_GUINT64_FORMAT G_GINT64_MODIFIER "u"
#if SIZE_MAX == UINT_MAX
#define G_GSIZE_MODIFIER ""
#elif SIZE_MAX == ULONG_MAX && !defined(_WIN32)
#define G_GSIZE_MODIFIER "l"
#else
#define G_GSIZE_MODIFIER "ll"
#endif
#define G_GSSIZE_MODIFIER G_GSIZE_MODIFIER
#define G_GSIZE_FORMAT G_GSIZE_MODIFIER "u"
#define G_GSSIZE_FORMAT G_GSIZE_MODIFIER "i"
#define G_GINT32_MODIFIER ""
#define G_GINT32_FORMAT "i"
#define G_GUINT32_FORMAT "u"
#define G_GINT16_MODIFIER "h"
#define G_GINT16_FORMAT "hi"
#define G_GUINT16_FORMAT "hu"
#define G_GOFFSET_MODIFIER G_GINT64_MODIFIER
#define G_GOFFSET_FORMAT G_GINT64_FORMAT
#define G_GINTPTR_MODIFIER G_GSIZE_MODIFIER
#define G_GINTPTR_FORMAT G_GSIZE_MODIFIER "i"
#define G_GUINTPTR_FORMAT G_GSIZE_MODIFIER "u"

#define G_E 2.7182818284590452353602874713526624977572470937000
#define G_LN2 0.69314718055994530941723212145817656807550013436026
#define G_LN10 2.3025850929940456840179914546843642076011014886288
#define G_PI 3.1415926535897932384626433832795028841971693993751
#define G_PI_2 1.5707963267948966192313216916397514420985846996876
#define G_PI_4 0.78539816339744830961566084581987572104929234984378
#define G_SQRT2 1.4142135623730950488016887242096980785696718753769

#define G_USEC_PER_SEC 1000000
#define G_TIME_SPAN_DAY (G_GINT64_CONSTANT(86400000000))
#define G_TIME_SPAN_HOUR (G_GINT64_CONSTANT(3600000000))
#define G_TIME_SPAN_MINUTE (G_GINT64_CONSTANT(60000000))
#define G_TIME_SPAN_SECOND (G_GINT64_CONSTANT(1000000))
#define G_TIME_SPAN_MILLISECOND (G_GINT64_CONSTANT(1000))

/* ---- byte order ---------------------------------------------------------- */

#define GUINT16_SWAP_LE_BE(val) ((guint16)__builtin_bswap16((guint16)(val)))
#define GUINT32_SWAP_LE_BE(val) ((guint32)__builtin_bswap32((guint32)(val)))
#define GUINT64_SWAP_LE_BE(val) ((guint64)__builtin_bswap64((guint64)(val)))
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define G_BYTE_ORDER 1234
#define GINT16_TO_LE(val) ((gint16)(val))
#define GUINT16_TO_LE(val) ((guint16)(val))
#define GINT32_TO_LE(val) ((gint32)(val))
#define GUINT32_TO_LE(val) ((guint32)(val))
#define GINT64_TO_LE(val) ((gint64)(val))
#define GUINT64_TO_LE(val) ((guint64)(val))
#define GINT16_TO_BE(val) ((gint16)GUINT16_SWAP_LE_BE(val))
#define GUINT16_TO_BE(val) (GUINT16_SWAP_LE_BE(val))
#define GINT32_TO_BE(val) ((gint32)GUINT32_SWAP_LE_BE(val))
#define GUINT32_TO_BE(val) (GUINT32_SWAP_LE_BE(val))
#define GINT64_TO_BE(val) ((gint64)GUINT64_SWAP_LE_BE(val))
#define GUINT64_TO_BE(val) (GUINT64_SWAP_LE_BE(val))
#else
#define G_BYTE_ORDER 4321
#define GINT16_TO_BE(val) ((gint16)(val))
#define GUINT16_TO_BE(val) ((guint16)(val))
#define GINT32_TO_BE(val) ((gint32)(val))
#define GUINT32_TO_BE(val) ((guint32)(val))
#define GINT64_TO_BE(val) ((gint64)(val))
#define GUINT64_TO_BE(val) ((guint64)(val))
#define GINT16_TO_LE(val) ((gint16)GUINT16_SWAP_LE_BE(val))
#define GUINT16_TO_LE(val) (GUINT16_SWAP_LE_BE(val))
#define GINT32_TO_LE(val) ((gint32)GUINT32_SWAP_LE_BE(val))
#define GUINT32_TO_LE(val) (GUINT32_SWAP_LE_BE(val))
#define GINT64_TO_LE(val) ((gint64)GUINT64_SWAP_LE_BE(val))
#define GUINT64_TO_LE(val) (GUINT64_SWAP_LE_BE(val))
#endif
#define G_LITTLE_ENDIAN 1234
#define G_BIG_ENDIAN 4321
#define GINT16_FROM_LE(val) (GINT16_TO_LE(val))
#define GUINT16_FROM_LE(val) (GUINT16_TO_LE(val))
#define GINT32_FROM_LE(val) (GINT32_TO_LE(val))
#define GUINT32_FROM_LE(val) (GUINT32_TO_LE(val))
#define GINT64_FROM_LE(val) (GINT64_TO_LE(val))
#define GUINT64_FROM_LE(val) (GUINT64_TO_LE(val))
#define GINT16_FROM_BE(val) (GINT16_TO_BE(val))
#define GUINT16_FROM_BE(val) (GUINT16_TO_BE(val))
#define GINT32_FROM_BE(val) (GINT32_TO_BE(val))
#define GUINT32_FROM_BE(val) (GUINT32_TO_BE(val))
#define GINT64_FROM_BE(val) (GINT64_TO_BE(val))
#define GUINT64_FROM_BE(val) (GUINT64_TO_BE(val))

/* ---- platform ------------------------------------------------------------ */

#ifdef _WIN32
#define G_OS_WIN32
#define G_PLATFORM_WIN32
#else
#define G_OS_UNIX
#endif

#define G_DIR_SEPARATOR NS_DIR_SEPARATOR
#define G_DIR_SEPARATOR_S NS_DIR_SEPARATOR_S
#define G_IS_DIR_SEPARATOR(c) NS_IS_DIR_SEPARATOR(c)
#define G_SEARCHPATH_SEPARATOR NS_SEARCHPATH_SEPARATOR
#define G_SEARCHPATH_SEPARATOR_S NS_SEARCHPATH_SEPARATOR_S

/* The GLib these names stand in for. */
#define GLIB_MAJOR_VERSION 2
#define GLIB_MINOR_VERSION 80
#define GLIB_MICRO_VERSION 0
#define GLIB_CHECK_VERSION(major, minor, micro) \
    (GLIB_MAJOR_VERSION > (major) || \
     (GLIB_MAJOR_VERSION == (major) && GLIB_MINOR_VERSION > (minor)) || \
     (GLIB_MAJOR_VERSION == (major) && GLIB_MINOR_VERSION == (minor) && \
      GLIB_MICRO_VERSION >= (micro)))

/* ---- macros ------------------------------------------------------------------ */

#ifndef FALSE
#define FALSE (0)
#endif
#ifndef TRUE
#define TRUE (!FALSE)
#endif
#undef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#undef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#undef ABS
#define ABS(a) (((a) < 0) ? -(a) : (a))
#undef CLAMP
#define CLAMP(x, low, high) (((x) > (high)) ? (high) : (((x) < (low)) ? (low) : (x)))

#define G_BEGIN_DECLS NS_BEGIN_DECLS
#define G_END_DECLS NS_END_DECLS
#define G_STMT_START do
#define G_STMT_END while (0)
#define G_N_ELEMENTS(arr) (sizeof(arr) / sizeof((arr)[0]))
#define G_STRINGIFY_ARG(contents) #contents
#define G_STRINGIFY(macro_or_string) G_STRINGIFY_ARG(macro_or_string)
#define G_PASTE_ARGS(a, b) a##b
#define G_PASTE(a, b) G_PASTE_ARGS(a, b)
#define G_STRFUNC ((const char *)(__func__))
#define G_STRLOC __FILE__ ":" G_STRINGIFY(__LINE__)
#define G_STRUCT_OFFSET(struct_type, member) ((glong)offsetof(struct_type, member))
#define G_STRUCT_MEMBER_P(struct_p, struct_offset) \
    ((gpointer)((guint8 *)(struct_p) + (glong)(struct_offset)))
#define G_STATIC_ASSERT(expr) _Static_assert((expr), "static assertion failed: " #expr)

#if defined(__GNUC__) || defined(__clang__)
#define G_LIKELY(expr) (__builtin_expect(!!(expr), 1))
#define G_UNLIKELY(expr) (__builtin_expect(!!(expr), 0))
#define G_GNUC_UNUSED __attribute__((__unused__))
#define G_GNUC_CONST __attribute__((__const__))
#define G_GNUC_PURE __attribute__((__pure__))
#define G_GNUC_MALLOC __attribute__((__malloc__))
#define G_GNUC_NORETURN __attribute__((__noreturn__))
#define G_GNUC_WARN_UNUSED_RESULT __attribute__((__warn_unused_result__))
#define G_GNUC_NULL_TERMINATED __attribute__((__sentinel__))
#define G_GNUC_DEPRECATED __attribute__((__deprecated__))
#define G_GNUC_INTERNAL __attribute__((__visibility__("hidden")))
#define G_GNUC_FALLTHROUGH __attribute__((__fallthrough__))
#define G_GNUC_MAY_ALIAS __attribute__((__may_alias__))
#define G_ALWAYS_INLINE __attribute__((__always_inline__))
#define G_NO_INLINE __attribute__((__noinline__))
#else
#define G_LIKELY(expr) (expr)
#define G_UNLIKELY(expr) (expr)
#define G_GNUC_UNUSED
#define G_GNUC_CONST
#define G_GNUC_PURE
#define G_GNUC_MALLOC
#define G_GNUC_NORETURN
#define G_GNUC_WARN_UNUSED_RESULT
#define G_GNUC_NULL_TERMINATED
#define G_GNUC_DEPRECATED
#define G_GNUC_INTERNAL
#define G_GNUC_FALLTHROUGH
#define G_GNUC_MAY_ALIAS
#define G_ALWAYS_INLINE
#define G_NO_INLINE
#endif
#define G_GNUC_PRINTF(format_idx, arg_idx) NS_PRINTF(format_idx, arg_idx)
#define G_GNUC_SCANF(format_idx, arg_idx)
#define G_GNUC_FORMAT(arg_idx)
#define G_GNUC_EXTENSION __extension__
#define G_GNUC_BEGIN_IGNORE_DEPRECATIONS \
    _Pragma("GCC diagnostic push") _Pragma("GCC diagnostic ignored \"-Wdeprecated-declarations\"")
#define G_GNUC_END_IGNORE_DEPRECATIONS _Pragma("GCC diagnostic pop")
#define G_GNUC_CHECK_VERSION(major, minor) \
    (defined(__GNUC__) && \
     ((__GNUC__ > (major)) || ((__GNUC__ == (major)) && (__GNUC_MINOR__ >= (minor)))))
#define G_INLINE_FUNC static inline

#define GPOINTER_TO_INT(p) ((gint)(gintptr)(p))
#define GPOINTER_TO_UINT(p) ((guint)(guintptr)(p))
#define GINT_TO_POINTER(i) ((gpointer)(gintptr)(i))
#define GUINT_TO_POINTER(u) ((gpointer)(guintptr)(u))
#define GSIZE_TO_POINTER(s) ((gpointer)(guintptr)(s))
#define GPOINTER_TO_SIZE(p) ((gsize)(p))

#ifndef G_LOG_DOMAIN
#define G_LOG_DOMAIN ((gchar *)0)
#endif

/* ---- memory ------------------------------------------------------------------- */

#define g_new(struct_type, n_structs) \
    ((struct_type *)ns_malloc_n((n_structs), sizeof(struct_type)))
#define g_new0(struct_type, n_structs) \
    ((struct_type *)ns_malloc0_n((n_structs), sizeof(struct_type)))
#define g_renew(struct_type, mem, n_structs) \
    ((struct_type *)ns_realloc_n((mem), (n_structs), sizeof(struct_type)))
#define g_try_new(struct_type, n_structs) \
    ((struct_type *)ns_try_malloc_n((n_structs), sizeof(struct_type)))
#define g_try_new0(struct_type, n_structs) \
    ((struct_type *)ns_try_malloc0_n((n_structs), sizeof(struct_type)))

static inline gpointer
ns_glib_steal_pointer(gpointer pp)
{
    gpointer *ptr = (gpointer *)pp;
    gpointer ref = *ptr;
    *ptr = NULL;
    return ref;
}
#define g_steal_pointer(pp) ((__typeof__(*(pp)))ns_glib_steal_pointer(pp))

#define g_clear_pointer(pp, destroy)                       \
    G_STMT_START {                                         \
        G_STATIC_ASSERT(sizeof *(pp) == sizeof(gpointer)); \
        __typeof__((pp)) _pp = (pp);                       \
        __typeof__(*(pp)) _ptr = *_pp;                     \
        *_pp = NULL;                                       \
        if (_ptr) (destroy)(_ptr);                         \
    } G_STMT_END

/* ---- automatic cleanup -------------------------------------------------------- */

#define _GLIB_CLEANUP(func) __attribute__((cleanup(func)))
#define _GLIB_AUTOPTR_FUNC_NAME(TypeName) glib_autoptr_cleanup_##TypeName
#define _GLIB_AUTOPTR_TYPENAME(TypeName) TypeName##_autoptr
#define _GLIB_AUTO_FUNC_NAME(TypeName) glib_auto_cleanup_##TypeName

#define G_DEFINE_AUTOPTR_CLEANUP_FUNC(TypeName, func)                          \
    typedef TypeName *_GLIB_AUTOPTR_TYPENAME(TypeName);                         \
    static G_GNUC_UNUSED inline void _GLIB_AUTOPTR_FUNC_NAME(TypeName)(TypeName **_ptr) \
    {                                                                           \
        if (*_ptr) (func)(*_ptr);                                               \
    }
#define G_DEFINE_AUTO_CLEANUP_FREE_FUNC(TypeName, func, none_value)             \
    static G_GNUC_UNUSED inline void _GLIB_AUTO_FUNC_NAME(TypeName)(TypeName *_ptr) \
    {                                                                           \
        if (*_ptr != none_value) (func)(*_ptr);                                 \
    }
#define G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(TypeName, func)                        \
    static G_GNUC_UNUSED inline void _GLIB_AUTO_FUNC_NAME(TypeName)(TypeName *_ptr) \
    {                                                                           \
        (func)(_ptr);                                                           \
    }

#define g_autoptr(TypeName) \
    _GLIB_CLEANUP(_GLIB_AUTOPTR_FUNC_NAME(TypeName)) _GLIB_AUTOPTR_TYPENAME(TypeName)
#define g_auto(TypeName) _GLIB_CLEANUP(_GLIB_AUTO_FUNC_NAME(TypeName)) TypeName

static G_GNUC_UNUSED inline void
ns_glib_autoptr_cleanup_generic_gfree(void *p)
{
    void **pp = (void **)p;
    ns_free(*pp);
}
#define g_autofree _GLIB_CLEANUP(ns_glib_autoptr_cleanup_generic_gfree)

static G_GNUC_UNUSED inline void
ns_glib_string_free_all(GString *s)
{
    ns_string_free(s, TRUE);
}
static G_GNUC_UNUSED inline void
ns_glib_array_free_all(GArray *a)
{
    ns_array_unref(a);
}
static G_GNUC_UNUSED inline void
ns_glib_queue_free(GQueue *q)
{
    ns_queue_free(q);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GString, ns_glib_string_free_all)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GArray, ns_glib_array_free_all)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GPtrArray, ns_ptr_array_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GByteArray, ns_byte_array_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GBytes, ns_bytes_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GHashTable, ns_hash_table_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GList, ns_list_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GQueue, ns_glib_queue_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GError, ns_error_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GKeyFile, ns_key_file_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GChecksum, ns_checksum_free)
G_DEFINE_AUTO_CLEANUP_FREE_FUNC(GStrv, ns_strfreev, NULL)

/* ---- atomics --------------------------------------------------------------------- */

#define g_atomic_int_get(atomic) (__atomic_load_n((atomic), __ATOMIC_SEQ_CST))
#define g_atomic_int_set(atomic, newval) \
    (__atomic_store_n((atomic), (newval), __ATOMIC_SEQ_CST))
#define g_atomic_int_inc(atomic) ((void)__atomic_fetch_add((atomic), 1, __ATOMIC_SEQ_CST))
#define g_atomic_int_dec_and_test(atomic) \
    (__atomic_fetch_sub((atomic), 1, __ATOMIC_SEQ_CST) == 1)
#define g_atomic_int_add(atomic, val) (__atomic_fetch_add((atomic), (val), __ATOMIC_SEQ_CST))
#define g_atomic_int_and(atomic, val) (__atomic_fetch_and((atomic), (val), __ATOMIC_SEQ_CST))
#define g_atomic_int_or(atomic, val) (__atomic_fetch_or((atomic), (val), __ATOMIC_SEQ_CST))
#define g_atomic_int_exchange(atomic, newval) \
    (__atomic_exchange_n((atomic), (newval), __ATOMIC_SEQ_CST))
#define g_atomic_int_compare_and_exchange(atomic, oldval, newval)                     \
    ({                                                                               \
        __typeof__(*(atomic)) ns_gaicae_old = (oldval);                              \
        (gboolean)__atomic_compare_exchange_n((atomic), &ns_gaicae_old, (newval), 0, \
                                              __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);   \
    })
#define g_atomic_pointer_get(atomic) (__atomic_load_n((atomic), __ATOMIC_SEQ_CST))
#define g_atomic_pointer_set(atomic, newval) \
    (__atomic_store_n((atomic), (newval), __ATOMIC_SEQ_CST))
#define g_atomic_pointer_add(atomic, val) \
    ((gintptr)__atomic_fetch_add((gintptr *)(atomic), (val), __ATOMIC_SEQ_CST))
#define g_atomic_pointer_exchange(atomic, newval) \
    (__atomic_exchange_n((atomic), (newval), __ATOMIC_SEQ_CST))
#define g_atomic_pointer_compare_and_exchange(atomic, oldval, newval)                 \
    ({                                                                               \
        __typeof__(*(atomic)) ns_gapcae_old = (oldval);                              \
        (gboolean)__atomic_compare_exchange_n((atomic), &ns_gapcae_old, (newval), 0, \
                                              __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);   \
    })

/* ---- messages, checks and assertions --------------------------------------------- */

#define G_LOG_FLAG_RECURSION NS_LOG_FLAG_RECURSION
#define G_LOG_FLAG_FATAL NS_LOG_FLAG_FATAL
#define G_LOG_LEVEL_ERROR NS_LOG_LEVEL_ERROR
#define G_LOG_LEVEL_CRITICAL NS_LOG_LEVEL_CRITICAL
#define G_LOG_LEVEL_WARNING NS_LOG_LEVEL_WARNING
#define G_LOG_LEVEL_MESSAGE NS_LOG_LEVEL_MESSAGE
#define G_LOG_LEVEL_INFO NS_LOG_LEVEL_INFO
#define G_LOG_LEVEL_DEBUG NS_LOG_LEVEL_DEBUG
#define G_LOG_LEVEL_MASK NS_LOG_LEVEL_MASK
#define G_LOG_LEVEL_USER_SHIFT NS_LOG_LEVEL_USER_SHIFT
#define G_LOG_FATAL_MASK NS_LOG_FATAL_MASK
#define G_LOG_WRITER_HANDLED NS_LOG_WRITER_HANDLED
#define G_LOG_WRITER_UNHANDLED NS_LOG_WRITER_UNHANDLED

#define g_error(...)                                             \
    G_STMT_START {                                               \
        ns_log(G_LOG_DOMAIN, G_LOG_LEVEL_ERROR, __VA_ARGS__);    \
        for (;;) {}                                              \
    } G_STMT_END
#define g_critical(...) ns_log(G_LOG_DOMAIN, G_LOG_LEVEL_CRITICAL, __VA_ARGS__)
#define g_warning(...) ns_log(G_LOG_DOMAIN, G_LOG_LEVEL_WARNING, __VA_ARGS__)
#define g_message(...) ns_log(G_LOG_DOMAIN, G_LOG_LEVEL_MESSAGE, __VA_ARGS__)
#define g_info(...) ns_log(G_LOG_DOMAIN, G_LOG_LEVEL_INFO, __VA_ARGS__)
#define g_debug(...) ns_log(G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, __VA_ARGS__)

#ifdef G_DISABLE_CHECKS
#define g_return_if_fail(expr) G_STMT_START { (void)0; } G_STMT_END
#define g_return_val_if_fail(expr, val) G_STMT_START { (void)0; } G_STMT_END
#define g_return_if_reached() G_STMT_START { return; } G_STMT_END
#define g_return_val_if_reached(val) G_STMT_START { return (val); } G_STMT_END
#else
#define g_return_if_fail(expr)                                              \
    G_STMT_START {                                                          \
        if (G_LIKELY(expr)) {                                               \
        } else {                                                            \
            ns_return_if_fail_warning(G_LOG_DOMAIN, G_STRFUNC, #expr);      \
            return;                                                         \
        }                                                                   \
    } G_STMT_END
#define g_return_val_if_fail(expr, val)                                     \
    G_STMT_START {                                                          \
        if (G_LIKELY(expr)) {                                               \
        } else {                                                            \
            ns_return_if_fail_warning(G_LOG_DOMAIN, G_STRFUNC, #expr);      \
            return (val);                                                   \
        }                                                                   \
    } G_STMT_END
#define g_return_if_reached()                                               \
    G_STMT_START {                                                          \
        ns_log(G_LOG_DOMAIN, G_LOG_LEVEL_CRITICAL,                          \
               "file %s: line %d (%s): should not be reached", __FILE__,     \
               __LINE__, G_STRFUNC);                                        \
        return;                                                             \
    } G_STMT_END
#define g_return_val_if_reached(val)                                        \
    G_STMT_START {                                                          \
        ns_log(G_LOG_DOMAIN, G_LOG_LEVEL_CRITICAL,                          \
               "file %s: line %d (%s): should not be reached", __FILE__,     \
               __LINE__, G_STRFUNC);                                        \
        return (val);                                                       \
    } G_STMT_END
#endif

#define g_warn_if_reached() ns_warn_message(G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC, NULL)
#define g_warn_if_fail(expr)                                                \
    G_STMT_START {                                                          \
        if (G_LIKELY(expr)) {                                               \
        } else {                                                            \
            ns_warn_message(G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC, #expr); \
        }                                                                   \
    } G_STMT_END

#ifdef G_DISABLE_ASSERT
#define g_assert(expr) G_STMT_START { (void)0; } G_STMT_END
#define g_assert_not_reached() G_STMT_START { (void)0; } G_STMT_END
#else
#define g_assert(expr)                                                      \
    G_STMT_START {                                                          \
        if (G_LIKELY(expr)) {                                               \
        } else {                                                            \
            ns_assertion_message_expr(G_LOG_DOMAIN, __FILE__, __LINE__,     \
                                      G_STRFUNC, #expr);                    \
        }                                                                   \
    } G_STMT_END
#define g_assert_not_reached()                                              \
    G_STMT_START {                                                          \
        ns_assertion_message_expr(G_LOG_DOMAIN, __FILE__, __LINE__,         \
                                  G_STRFUNC, NULL);                         \
    } G_STMT_END
#endif
#define g_assert_true(expr) g_assert(expr)
#define g_assert_false(expr) g_assert(!(expr))
#define g_assert_null(expr) g_assert((expr) == NULL)
#define g_assert_nonnull(expr) g_assert((expr) != NULL)

/* ---- constants of the modules ----------------------------------------------------- */

#define G_ASCII_DTOSTR_BUF_SIZE NS_ASCII_DTOSTR_BUF_SIZE
#define G_ASCII_ALNUM NS_ASCII_ALNUM
#define G_ASCII_ALPHA NS_ASCII_ALPHA
#define G_ASCII_CNTRL NS_ASCII_CNTRL
#define G_ASCII_DIGIT NS_ASCII_DIGIT
#define G_ASCII_GRAPH NS_ASCII_GRAPH
#define G_ASCII_LOWER NS_ASCII_LOWER
#define G_ASCII_PRINT NS_ASCII_PRINT
#define G_ASCII_PUNCT NS_ASCII_PUNCT
#define G_ASCII_SPACE NS_ASCII_SPACE
#define G_ASCII_UPPER NS_ASCII_UPPER
#define G_ASCII_XDIGIT NS_ASCII_XDIGIT
#define g_ascii_table ns_ascii_table

#define G_QUEUE_INIT NS_QUEUE_INIT

#define G_CHECKSUM_MD5 NS_CHECKSUM_MD5
#define G_CHECKSUM_SHA1 NS_CHECKSUM_SHA1
#define G_CHECKSUM_SHA256 NS_CHECKSUM_SHA256
#define G_CHECKSUM_SHA512 NS_CHECKSUM_SHA512
#define G_CHECKSUM_SHA384 NS_CHECKSUM_SHA384

#define G_KEY_FILE_NONE NS_KEY_FILE_NONE
#define G_KEY_FILE_KEEP_COMMENTS NS_KEY_FILE_KEEP_COMMENTS
#define G_KEY_FILE_KEEP_TRANSLATIONS NS_KEY_FILE_KEEP_TRANSLATIONS
#define G_KEY_FILE_ERROR NS_KEY_FILE_ERROR
#define G_KEY_FILE_ERROR_UNKNOWN_ENCODING NS_KEY_FILE_ERROR_UNKNOWN_ENCODING
#define G_KEY_FILE_ERROR_PARSE NS_KEY_FILE_ERROR_PARSE
#define G_KEY_FILE_ERROR_NOT_FOUND NS_KEY_FILE_ERROR_NOT_FOUND
#define G_KEY_FILE_ERROR_KEY_NOT_FOUND NS_KEY_FILE_ERROR_KEY_NOT_FOUND
#define G_KEY_FILE_ERROR_GROUP_NOT_FOUND NS_KEY_FILE_ERROR_GROUP_NOT_FOUND
#define G_KEY_FILE_ERROR_INVALID_VALUE NS_KEY_FILE_ERROR_INVALID_VALUE

#define G_FILE_ERROR NS_FILE_ERROR
#define G_FILE_ERROR_EXIST NS_FILE_ERROR_EXIST
#define G_FILE_ERROR_ISDIR NS_FILE_ERROR_ISDIR
#define G_FILE_ERROR_ACCES NS_FILE_ERROR_ACCES
#define G_FILE_ERROR_NAMETOOLONG NS_FILE_ERROR_NAMETOOLONG
#define G_FILE_ERROR_NOENT NS_FILE_ERROR_NOENT
#define G_FILE_ERROR_NOTDIR NS_FILE_ERROR_NOTDIR
#define G_FILE_ERROR_NXIO NS_FILE_ERROR_NXIO
#define G_FILE_ERROR_NODEV NS_FILE_ERROR_NODEV
#define G_FILE_ERROR_ROFS NS_FILE_ERROR_ROFS
#define G_FILE_ERROR_TXTBSY NS_FILE_ERROR_TXTBSY
#define G_FILE_ERROR_FAULT NS_FILE_ERROR_FAULT
#define G_FILE_ERROR_LOOP NS_FILE_ERROR_LOOP
#define G_FILE_ERROR_NOSPC NS_FILE_ERROR_NOSPC
#define G_FILE_ERROR_NOMEM NS_FILE_ERROR_NOMEM
#define G_FILE_ERROR_MFILE NS_FILE_ERROR_MFILE
#define G_FILE_ERROR_NFILE NS_FILE_ERROR_NFILE
#define G_FILE_ERROR_BADF NS_FILE_ERROR_BADF
#define G_FILE_ERROR_INVAL NS_FILE_ERROR_INVAL
#define G_FILE_ERROR_PIPE NS_FILE_ERROR_PIPE
#define G_FILE_ERROR_AGAIN NS_FILE_ERROR_AGAIN
#define G_FILE_ERROR_INTR NS_FILE_ERROR_INTR
#define G_FILE_ERROR_IO NS_FILE_ERROR_IO
#define G_FILE_ERROR_PERM NS_FILE_ERROR_PERM
#define G_FILE_ERROR_NOSYS NS_FILE_ERROR_NOSYS
#define G_FILE_ERROR_FAILED NS_FILE_ERROR_FAILED

#define G_CONVERT_ERROR NS_CONVERT_ERROR
#define G_CONVERT_ERROR_NO_CONVERSION NS_CONVERT_ERROR_NO_CONVERSION
#define G_CONVERT_ERROR_ILLEGAL_SEQUENCE NS_CONVERT_ERROR_ILLEGAL_SEQUENCE
#define G_CONVERT_ERROR_FAILED NS_CONVERT_ERROR_FAILED
#define G_CONVERT_ERROR_PARTIAL_INPUT NS_CONVERT_ERROR_PARTIAL_INPUT
#define G_CONVERT_ERROR_BAD_URI NS_CONVERT_ERROR_BAD_URI
#define G_CONVERT_ERROR_NOT_ABSOLUTE_PATH NS_CONVERT_ERROR_NOT_ABSOLUTE_PATH
#define G_CONVERT_ERROR_NO_MEMORY NS_CONVERT_ERROR_NO_MEMORY
#define G_CONVERT_ERROR_EMBEDDED_NUL NS_CONVERT_ERROR_EMBEDDED_NUL

#define G_URI_PARAMS_NONE NS_URI_PARAMS_NONE
#define G_URI_PARAMS_CASE_INSENSITIVE NS_URI_PARAMS_CASE_INSENSITIVE
#define G_URI_PARAMS_WWW_FORM NS_URI_PARAMS_WWW_FORM
#define G_URI_PARAMS_PARSE_RELAXED NS_URI_PARAMS_PARSE_RELAXED
#define G_URI_ERROR NS_URI_ERROR
#define G_URI_RESERVED_CHARS_GENERIC_DELIMITERS NS_URI_RESERVED_CHARS_GENERIC_DELIMITERS
#define G_URI_RESERVED_CHARS_SUBCOMPONENT_DELIMITERS NS_URI_RESERVED_CHARS_SUBCOMPONENT_DELIMITERS
#define G_URI_RESERVED_CHARS_ALLOWED_IN_PATH_ELEMENT NS_URI_RESERVED_CHARS_ALLOWED_IN_PATH_ELEMENT
#define G_URI_RESERVED_CHARS_ALLOWED_IN_PATH NS_URI_RESERVED_CHARS_ALLOWED_IN_PATH
#define G_URI_RESERVED_CHARS_ALLOWED_IN_USERINFO NS_URI_RESERVED_CHARS_ALLOWED_IN_USERINFO

/* ---- functions: g_X is ns_X ----------------------------------------------------- */

#define g_array_append_val ns_array_append_val
#define g_array_append_vals ns_array_append_vals
#define g_array_free ns_array_free
#define g_array_get_element_size ns_array_get_element_size
#define g_array_index ns_array_index
#define g_array_insert_val ns_array_insert_val
#define g_array_insert_vals ns_array_insert_vals
#define g_array_new ns_array_new
#define g_array_prepend_val ns_array_prepend_val
#define g_array_prepend_vals ns_array_prepend_vals
#define g_array_ref ns_array_ref
#define g_array_remove_index ns_array_remove_index
#define g_array_remove_index_fast ns_array_remove_index_fast
#define g_array_remove_range ns_array_remove_range
#define g_array_set_clear_func ns_array_set_clear_func
#define g_array_set_size ns_array_set_size
#define g_array_sized_new ns_array_sized_new
#define g_array_sort ns_array_sort
#define g_array_sort_with_data ns_array_sort_with_data
#define g_array_unref ns_array_unref
#define g_ascii_digit_value ns_ascii_digit_value
#define g_ascii_dtostr ns_ascii_dtostr
#define g_ascii_formatd ns_ascii_formatd
#define g_ascii_isalnum ns_ascii_isalnum
#define g_ascii_isalpha ns_ascii_isalpha
#define g_ascii_iscntrl ns_ascii_iscntrl
#define g_ascii_isdigit ns_ascii_isdigit
#define g_ascii_isgraph ns_ascii_isgraph
#define g_ascii_islower ns_ascii_islower
#define g_ascii_isprint ns_ascii_isprint
#define g_ascii_ispunct ns_ascii_ispunct
#define g_ascii_isspace ns_ascii_isspace
#define g_ascii_isupper ns_ascii_isupper
#define g_ascii_isxdigit ns_ascii_isxdigit
#define g_ascii_strcasecmp ns_ascii_strcasecmp
#define g_ascii_strdown ns_ascii_strdown
#define g_ascii_strncasecmp ns_ascii_strncasecmp
#define g_ascii_strtod ns_ascii_strtod
#define g_ascii_strtoll ns_ascii_strtoll
#define g_ascii_strtoull ns_ascii_strtoull
#define g_ascii_strup ns_ascii_strup
#define g_ascii_tolower ns_ascii_tolower
#define g_ascii_toupper ns_ascii_toupper
#define g_ascii_xdigit_value ns_ascii_xdigit_value
#define g_assertion_message ns_assertion_message
#define g_assertion_message_expr ns_assertion_message_expr
#define g_atomic_ref_count_dec ns_atomic_ref_count_dec
#define g_atomic_ref_count_inc ns_atomic_ref_count_inc
#define g_atomic_ref_count_init ns_atomic_ref_count_init
#define g_base64_decode ns_base64_decode
#define g_base64_encode ns_base64_encode
#define g_build_filename ns_build_filename
#define g_build_filenamev ns_build_filenamev
#define g_byte_array_append ns_byte_array_append
#define g_byte_array_free ns_byte_array_free
#define g_byte_array_free_to_bytes ns_byte_array_free_to_bytes
#define g_byte_array_new ns_byte_array_new
#define g_byte_array_prepend ns_byte_array_prepend
#define g_byte_array_ref ns_byte_array_ref
#define g_byte_array_remove_range ns_byte_array_remove_range
#define g_byte_array_set_size ns_byte_array_set_size
#define g_byte_array_sized_new ns_byte_array_sized_new
#define g_byte_array_unref ns_byte_array_unref
#define g_bytes_compare ns_bytes_compare
#define g_bytes_equal ns_bytes_equal
#define g_bytes_get_data ns_bytes_get_data
#define g_bytes_get_size ns_bytes_get_size
#define g_bytes_hash ns_bytes_hash
#define g_bytes_new ns_bytes_new
#define g_bytes_new_static ns_bytes_new_static
#define g_bytes_new_take ns_bytes_new_take
#define g_bytes_new_with_free_func ns_bytes_new_with_free_func
#define g_bytes_ref ns_bytes_ref
#define g_bytes_unref ns_bytes_unref
#define g_canonicalize_filename ns_canonicalize_filename
#define g_checksum_copy ns_checksum_copy
#define g_checksum_free ns_checksum_free
#define g_checksum_get_digest ns_checksum_get_digest
#define g_checksum_get_string ns_checksum_get_string
#define g_checksum_new ns_checksum_new
#define g_checksum_reset ns_checksum_reset
#define g_checksum_type_get_length ns_checksum_type_get_length
#define g_checksum_update ns_checksum_update
#define g_chmod ns_chmod
#define g_clear_error ns_clear_error
#define g_compute_checksum_for_data ns_compute_checksum_for_data
#define g_compute_checksum_for_string ns_compute_checksum_for_string
#define g_convert_error_quark ns_convert_error_quark
#define g_direct_equal ns_direct_equal
#define g_direct_hash ns_direct_hash
#define g_double_equal ns_double_equal
#define g_double_hash ns_double_hash
#define g_error_copy ns_error_copy
#define g_error_free ns_error_free
#define g_error_matches ns_error_matches
#define g_error_new ns_error_new
#define g_error_new_literal ns_error_new_literal
#define g_error_new_valist ns_error_new_valist
#define g_file_error_from_errno ns_file_error_from_errno
#define g_file_error_quark ns_file_error_quark
#define g_filename_display_name ns_filename_display_name
#define g_filename_from_uri ns_filename_from_uri
#define g_filename_to_uri ns_filename_to_uri
#define g_find_program_in_path ns_find_program_in_path
#define g_fopen ns_fopen
#define g_format_size ns_format_size
#define g_free ns_free
#define g_get_current_dir ns_get_current_dir
#define g_hash_table_add ns_hash_table_add
#define g_hash_table_contains ns_hash_table_contains
#define g_hash_table_destroy ns_hash_table_destroy
#define g_hash_table_foreach ns_hash_table_foreach
#define g_hash_table_foreach_remove ns_hash_table_foreach_remove
#define g_hash_table_get_keys ns_hash_table_get_keys
#define g_hash_table_get_values ns_hash_table_get_values
#define g_hash_table_insert ns_hash_table_insert
#define g_hash_table_iter_get_hash_table ns_hash_table_iter_get_hash_table
#define g_hash_table_iter_init ns_hash_table_iter_init
#define g_hash_table_iter_next ns_hash_table_iter_next
#define g_hash_table_iter_remove ns_hash_table_iter_remove
#define g_hash_table_iter_replace ns_hash_table_iter_replace
#define g_hash_table_iter_steal ns_hash_table_iter_steal
#define g_hash_table_lookup ns_hash_table_lookup
#define g_hash_table_lookup_extended ns_hash_table_lookup_extended
#define g_hash_table_new ns_hash_table_new
#define g_hash_table_new_full ns_hash_table_new_full
#define g_hash_table_ref ns_hash_table_ref
#define g_hash_table_remove ns_hash_table_remove
#define g_hash_table_remove_all ns_hash_table_remove_all
#define g_hash_table_replace ns_hash_table_replace
#define g_hash_table_size ns_hash_table_size
#define g_hash_table_steal ns_hash_table_steal
#define g_hash_table_steal_all ns_hash_table_steal_all
#define g_hash_table_steal_extended ns_hash_table_steal_extended
#define g_hash_table_unref ns_hash_table_unref
#define g_int64_equal ns_int64_equal
#define g_int64_hash ns_int64_hash
#define g_int_equal ns_int_equal
#define g_int_hash ns_int_hash
#define g_intern_static_string ns_intern_static_string
#define g_intern_string ns_intern_string
#define g_key_file_error_quark ns_key_file_error_quark
#define g_key_file_free ns_key_file_free
#define g_key_file_get_groups ns_key_file_get_groups
#define g_key_file_get_keys ns_key_file_get_keys
#define g_key_file_get_string ns_key_file_get_string
#define g_key_file_get_value ns_key_file_get_value
#define g_key_file_has_group ns_key_file_has_group
#define g_key_file_load_from_data ns_key_file_load_from_data
#define g_key_file_new ns_key_file_new
#define g_key_file_remove_group ns_key_file_remove_group
#define g_key_file_remove_key ns_key_file_remove_key
#define g_key_file_set_string ns_key_file_set_string
#define g_key_file_set_value ns_key_file_set_value
#define g_key_file_to_data ns_key_file_to_data
#define g_list_alloc ns_list_alloc
#define g_list_append ns_list_append
#define g_list_concat ns_list_concat
#define g_list_copy ns_list_copy
#define g_list_delete_link ns_list_delete_link
#define g_list_find ns_list_find
#define g_list_first ns_list_first
#define g_list_foreach ns_list_foreach
#define g_list_free ns_list_free
#define g_list_free_1 ns_list_free_1
#define g_list_free_full ns_list_free_full
#define g_list_insert_before ns_list_insert_before
#define g_list_last ns_list_last
#define g_list_length ns_list_length
#define g_list_nth ns_list_nth
#define g_list_nth_data ns_list_nth_data
#define g_list_prepend ns_list_prepend
#define g_list_remove ns_list_remove
#define g_list_remove_link ns_list_remove_link
#define g_list_reverse ns_list_reverse
#define g_list_sort ns_list_sort
#define g_list_sort_with_data ns_list_sort_with_data
#define g_log ns_log
#define g_log_set_always_fatal ns_log_set_always_fatal
#define g_log_set_writer_func ns_log_set_writer_func
#define g_log_structured_array ns_log_structured_array
#define g_log_writer_default ns_log_writer_default
#define g_log_writer_default_would_drop ns_log_writer_default_would_drop
#define g_log_writer_format_fields ns_log_writer_format_fields
#define g_log_writer_standard_streams ns_log_writer_standard_streams
#define g_logv ns_logv
#define g_malloc ns_malloc
#define g_malloc0 ns_malloc0
#define g_malloc0_n ns_malloc0_n
#define g_malloc_n ns_malloc_n
#define g_markup_escape_text ns_markup_escape_text
#define g_memdup2 ns_memdup2
#define g_mkdir ns_mkdir
#define g_open ns_open
#define g_path_get_basename ns_path_get_basename
#define g_path_get_dirname ns_path_get_dirname
#define g_path_is_absolute ns_path_is_absolute
#define g_path_skip_root ns_path_skip_root
#define g_print ns_print
#define g_printerr ns_printerr
#define g_propagate_error ns_propagate_error
#define g_ptr_array_add ns_ptr_array_add
#define g_ptr_array_copy ns_ptr_array_copy
#define g_ptr_array_find ns_ptr_array_find
#define g_ptr_array_find_with_equal_func ns_ptr_array_find_with_equal_func
#define g_ptr_array_foreach ns_ptr_array_foreach
#define g_ptr_array_free ns_ptr_array_free
#define g_ptr_array_index ns_ptr_array_index
#define g_ptr_array_insert ns_ptr_array_insert
#define g_ptr_array_new ns_ptr_array_new
#define g_ptr_array_new_full ns_ptr_array_new_full
#define g_ptr_array_new_with_free_func ns_ptr_array_new_with_free_func
#define g_ptr_array_ref ns_ptr_array_ref
#define g_ptr_array_remove ns_ptr_array_remove
#define g_ptr_array_remove_fast ns_ptr_array_remove_fast
#define g_ptr_array_remove_index ns_ptr_array_remove_index
#define g_ptr_array_remove_index_fast ns_ptr_array_remove_index_fast
#define g_ptr_array_remove_range ns_ptr_array_remove_range
#define g_ptr_array_set_free_func ns_ptr_array_set_free_func
#define g_ptr_array_set_size ns_ptr_array_set_size
#define g_ptr_array_sized_new ns_ptr_array_sized_new
#define g_ptr_array_sort ns_ptr_array_sort
#define g_ptr_array_sort_with_data ns_ptr_array_sort_with_data
#define g_ptr_array_steal_index ns_ptr_array_steal_index
#define g_ptr_array_steal_index_fast ns_ptr_array_steal_index_fast
#define g_ptr_array_unref ns_ptr_array_unref
#define g_quark_from_static_string ns_quark_from_static_string
#define g_quark_from_string ns_quark_from_string
#define g_quark_to_string ns_quark_to_string
#define g_quark_try_string ns_quark_try_string
#define g_queue_clear ns_queue_clear
#define g_queue_clear_full ns_queue_clear_full
#define g_queue_delete_link ns_queue_delete_link
#define g_queue_free ns_queue_free
#define g_queue_free_full ns_queue_free_full
#define g_queue_get_length ns_queue_get_length
#define g_queue_init ns_queue_init
#define g_queue_is_empty ns_queue_is_empty
#define g_queue_new ns_queue_new
#define g_queue_peek_head ns_queue_peek_head
#define g_queue_peek_tail ns_queue_peek_tail
#define g_queue_pop_head ns_queue_pop_head
#define g_queue_pop_tail ns_queue_pop_tail
#define g_queue_push_head ns_queue_push_head
#define g_queue_push_tail ns_queue_push_tail
#define g_queue_unlink ns_queue_unlink
#define g_realloc ns_realloc
#define g_realloc_n ns_realloc_n
#define g_remove ns_remove
#define g_rename ns_rename
#define g_return_if_fail_warning ns_return_if_fail_warning
#define g_rmdir ns_rmdir
#define g_set_error ns_set_error
#define g_set_error_literal ns_set_error_literal
#define g_snprintf ns_snprintf
#define g_str_equal ns_str_equal
#define g_str_has_prefix ns_str_has_prefix
#define g_str_has_suffix ns_str_has_suffix
#define g_str_hash ns_str_hash
#define g_strchomp ns_strchomp
#define g_strchug ns_strchug
#define g_strcmp0 ns_strcmp0
#define g_strconcat ns_strconcat
#define g_strdelimit ns_strdelimit
#define g_strdup ns_strdup
#define g_strdup_printf ns_strdup_printf
#define g_strdup_vprintf ns_strdup_vprintf
#define g_strdupv ns_strdupv
#define g_strerror ns_strerror
#define g_strescape ns_strescape
#define g_strfreev ns_strfreev
#define g_string_append ns_string_append
#define g_string_append_c ns_string_append_c
#define g_string_append_len ns_string_append_len
#define g_string_append_printf ns_string_append_printf
#define g_string_append_unichar ns_string_append_unichar
#define g_string_append_vprintf ns_string_append_vprintf
#define g_string_assign ns_string_assign
#define g_string_erase ns_string_erase
#define g_string_free ns_string_free
#define g_string_free_and_steal ns_string_free_and_steal
#define g_string_insert ns_string_insert
#define g_string_insert_c ns_string_insert_c
#define g_string_insert_len ns_string_insert_len
#define g_string_new ns_string_new
#define g_string_new_len ns_string_new_len
#define g_string_prepend ns_string_prepend
#define g_string_prepend_c ns_string_prepend_c
#define g_string_prepend_len ns_string_prepend_len
#define g_string_printf ns_string_printf
#define g_string_set_size ns_string_set_size
#define g_string_sized_new ns_string_sized_new
#define g_string_truncate ns_string_truncate
#define g_strjoinv ns_strjoinv
#define g_strlcpy ns_strlcpy
#define g_strndup ns_strndup
#define g_strrstr_len ns_strrstr_len
#define g_strsplit ns_strsplit
#define g_strsplit_set ns_strsplit_set
#define g_strstr_len ns_strstr_len
#define g_strstrip ns_strstrip
#define g_strv_length ns_strv_length
#define g_try_malloc ns_try_malloc
#define g_try_malloc0 ns_try_malloc0
#define g_try_malloc0_n ns_try_malloc0_n
#define g_try_malloc_n ns_try_malloc_n
#define g_unlink ns_unlink
#define g_uri_error_quark ns_uri_error_quark
#define g_uri_escape_string ns_uri_escape_string
#define g_uri_parse_params ns_uri_parse_params
#define g_uri_unescape_segment ns_uri_unescape_segment
#define g_uri_unescape_string ns_uri_unescape_string
#define g_utime ns_utime
#define g_vasprintf ns_vasprintf
#define g_vsnprintf ns_vsnprintf
#define g_warn_message ns_warn_message
#ifdef _WIN32
#define g_win32_error_message ns_win32_error_message
#endif

#endif
