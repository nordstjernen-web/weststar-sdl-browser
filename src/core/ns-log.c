/* Weststar SDL Browser — nscore: logging.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ns-log.h"
#include "ns-mem.h"
#include "ns-private.h"
#include "ns-string.h"
#include "ns-strfuncs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <io.h>
#include <process.h>
#else
#include <sys/time.h>
#include <unistd.h>
#endif

#define NS_ALERT_LEVELS (NS_LOG_LEVEL_ERROR | NS_LOG_LEVEL_CRITICAL | NS_LOG_LEVEL_WARNING)
#define NS_DEFAULT_LEVELS (NS_ALERT_LEVELS | NS_LOG_LEVEL_MESSAGE)
#define NS_INFO_LEVELS (NS_LOG_LEVEL_INFO | NS_LOG_LEVEL_DEBUG)

static NsSpinLock ns_log_lock = NS_SPIN_LOCK_INIT;
static NsLogWriterFunc ns_log_writer_func = ns_log_writer_default;
static void *ns_log_writer_user_data;
static int ns_log_writer_set;

/* From the environment, read once: G_DEBUG, G_MESSAGES_PREFIXED and
 * G_MESSAGES_DEBUG, as GLib reads them. */
static int ns_log_env_read;
static int ns_log_always_fatal = NS_LOG_FATAL_MASK;
static int ns_log_msg_prefix = NS_LOG_LEVEL_ERROR | NS_LOG_LEVEL_WARNING |
                               NS_LOG_LEVEL_CRITICAL | NS_LOG_LEVEL_DEBUG;
static char *ns_log_debug_domains;

static _Thread_local unsigned int ns_log_depth;
static _Thread_local unsigned int ns_log_structured_depth;

typedef struct {
    const char *key;
    int value;
} NsDebugKey;

/* Keys separated by ':', ';', ',', spaces or tabs, matched without regard
 * to case; "all" sets every one. */
static int
ns_parse_debug_string(const char *string, const NsDebugKey *keys, size_t n, int init)
{
    if (!string) return init;
    int result = 0, all = 0;
    const char *p = string;
    while (*p) {
        const char *q = p;
        while (*q && !strchr(":;, \t", *q)) q++;
        size_t len = (size_t)(q - p);
        if (len == 3 && ns_ascii_strncasecmp(p, "all", 3) == 0) all = 1;
        for (size_t i = 0; i < n; i++)
            if (strlen(keys[i].key) == len && ns_ascii_strncasecmp(p, keys[i].key, len) == 0)
                result |= keys[i].value;
        p = *q ? q + 1 : q;
    }
    if (all)
        for (size_t i = 0; i < n; i++) result |= keys[i].value;
    return result;
}

static void
ns_log_read_env(void)
{
    if (__atomic_load_n(&ns_log_env_read, __ATOMIC_ACQUIRE)) return;
    ns_spin_lock(&ns_log_lock);
    if (!ns_log_env_read) {
        static const NsDebugKey prefix_keys[] = {
            { "error", NS_LOG_LEVEL_ERROR },
            { "critical", NS_LOG_LEVEL_CRITICAL },
            { "warning", NS_LOG_LEVEL_WARNING },
            { "message", NS_LOG_LEVEL_MESSAGE },
            { "info", NS_LOG_LEVEL_INFO },
            { "debug", NS_LOG_LEVEL_DEBUG },
        };
        static const NsDebugKey debug_keys[] = {
            { "gc-friendly", 0 },
            { "fatal-warnings", NS_LOG_LEVEL_WARNING | NS_LOG_LEVEL_CRITICAL },
            { "fatal-criticals", NS_LOG_LEVEL_CRITICAL },
        };
        const char *prefixed = getenv("G_MESSAGES_PREFIXED");
        if (prefixed)
            ns_log_msg_prefix = ns_parse_debug_string(prefixed, prefix_keys, 6, 0);
        ns_log_always_fatal |= ns_parse_debug_string(getenv("G_DEBUG"), debug_keys, 3, 0);
        const char *domains = getenv("G_MESSAGES_DEBUG");
        ns_log_debug_domains = domains ? ns_strdup(domains) : NULL;
        __atomic_store_n(&ns_log_env_read, 1, __ATOMIC_RELEASE);
    }
    ns_spin_unlock(&ns_log_lock);
}

NsLogLevelFlags
ns_log_set_always_fatal(NsLogLevelFlags fatal_mask)
{
    ns_log_read_env();
    int old = ns_log_always_fatal;
    fatal_mask = (NsLogLevelFlags)((fatal_mask & NS_LOG_LEVEL_MASK) | NS_LOG_LEVEL_ERROR);
    ns_log_always_fatal = (fatal_mask & ~NS_LOG_FLAG_FATAL) | NS_LOG_FLAG_RECURSION;
    return (NsLogLevelFlags)old;
}

static NS_NORETURN void
ns_log_abort(void)
{
    fflush(stdout);
    fflush(stderr);
    abort();
}

static FILE *
ns_log_level_to_file(int log_level)
{
    return (log_level & NS_DEFAULT_LEVELS) ? stderr : stdout;
}

static const char *
ns_log_level_to_priority(int log_level)
{
    if (log_level & NS_LOG_LEVEL_ERROR) return "3";
    if (log_level & NS_LOG_LEVEL_CRITICAL) return "4";
    if (log_level & NS_LOG_LEVEL_WARNING) return "4";
    if (log_level & NS_LOG_LEVEL_MESSAGE) return "5";
    if (log_level & NS_LOG_LEVEL_INFO) return "6";
    if (log_level & NS_LOG_LEVEL_DEBUG) return "7";
    return "5";
}

static const char *
ns_log_level_color(int log_level, int use_color)
{
    if (!use_color) return "";
    if (log_level & NS_LOG_LEVEL_ERROR) return "\033[1;31m";
    if (log_level & NS_LOG_LEVEL_CRITICAL) return "\033[1;35m";
    if (log_level & NS_LOG_LEVEL_WARNING) return "\033[1;33m";
    if (log_level & (NS_LOG_LEVEL_MESSAGE | NS_LOG_LEVEL_INFO | NS_LOG_LEVEL_DEBUG))
        return "\033[1;32m";
    return "";
}

/* "WARNING **", "Message", "LOG-0100" and so on. */
static void
ns_log_level_prefix(char buf[64], int log_level, int use_color)
{
    const char *name;
    char custom[32];
    switch (log_level & NS_LOG_LEVEL_MASK) {
    case NS_LOG_LEVEL_ERROR: name = "ERROR"; break;
    case NS_LOG_LEVEL_CRITICAL: name = "CRITICAL"; break;
    case NS_LOG_LEVEL_WARNING: name = "WARNING"; break;
    case NS_LOG_LEVEL_MESSAGE: name = "Message"; break;
    case NS_LOG_LEVEL_INFO: name = "INFO"; break;
    case NS_LOG_LEVEL_DEBUG: name = "DEBUG"; break;
    default:
        if (log_level) {
            snprintf(custom, sizeof custom, "LOG-0x%x",
                     (unsigned int)(log_level & NS_LOG_LEVEL_MASK));
            name = custom;
        } else {
            name = "LOG";
        }
        break;
    }
    snprintf(buf, 64, "%s%s%s%s%s", ns_log_level_color(log_level, use_color), name,
             use_color ? "\033[0m" : "",
             (log_level & NS_LOG_FLAG_RECURSION) ? " (recursed)" : "",
             (log_level & NS_ALERT_LEVELS) ? " **" : "");
}

static unsigned long
ns_log_pid(void)
{
#ifdef _WIN32
    return (unsigned long)GetCurrentProcessId();
#else
    return (unsigned long)getpid();
#endif
}

static int64_t
ns_log_real_time_us(void)
{
#ifdef _WIN32
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    uint64_t t = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (int64_t)(t / 10) - INT64_C(11644473600000000);
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
#endif
}

/* The length of the UTF-8 character at p (at most end - p bytes), or 0 if
 * it is not a well-formed one; *out gets the code point. */
static size_t
ns_log_utf8_char(const unsigned char *p, const unsigned char *end, uint32_t *out)
{
    unsigned char c = *p;
    size_t n;
    uint32_t cp, min;
    if (c < 0x80) {
        *out = c;
        return 1;
    } else if ((c & 0xe0) == 0xc0) {
        n = 2; cp = c & 0x1f; min = 0x80;
    } else if ((c & 0xf0) == 0xe0) {
        n = 3; cp = c & 0x0f; min = 0x800;
    } else if ((c & 0xf8) == 0xf0) {
        n = 4; cp = c & 0x07; min = 0x10000;
    } else {
        return 0;
    }
    if ((size_t)(end - p) < n) return 0;
    for (size_t i = 1; i < n; i++) {
        if ((p[i] & 0xc0) != 0x80) return 0;
        cp = (cp << 6) | (p[i] & 0x3f);
    }
    if (cp < min || cp > 0x10ffff || (cp >= 0xd800 && cp < 0xe000)) return 0;
    *out = cp;
    return n;
}

/* Control characters as \uXXXX and bytes that are not UTF-8 as \xNN. */
static void
ns_log_escape(NsString *out, const char *msg, ptrdiff_t len)
{
    const unsigned char *p = (const unsigned char *)msg;
    const unsigned char *end = p + (len < 0 ? (ptrdiff_t)strlen(msg) : len);
    while (p < end) {
        uint32_t wc;
        size_t n = ns_log_utf8_char(p, end, &wc);
        if (n == 0) {
            ns_string_append_printf(out, "\\x%02x", (unsigned int)*p);
            p++;
            continue;
        }
        int safe;
        if (wc == '\r')
            safe = p + 1 < end && p[1] == '\n';
        else
            safe = !((wc < 0x20 && wc != '\t' && wc != '\n' && wc != '\r') || wc == 0x7f ||
                     (wc >= 0x80 && wc < 0xa0));
        if (safe)
            ns_string_append_len(out, (const char *)p, (ptrdiff_t)n);
        else
            ns_string_append_printf(out, "\\u%04x", (unsigned int)wc);
        p += n;
    }
}

char *
ns_log_writer_format_fields(NsLogLevelFlags log_level, const NsLogField *fields,
                            size_t n_fields, int use_color)
{
    const char *message = NULL, *log_domain = NULL;
    ptrdiff_t message_length = -1, log_domain_length = -1;
    for (size_t i = 0; (!message || !log_domain) && i < n_fields; i++) {
        if (ns_strcmp0(fields[i].key, "MESSAGE") == 0) {
            message = fields[i].value;
            message_length = fields[i].length;
        } else if (ns_strcmp0(fields[i].key, "GLIB_DOMAIN") == 0) {
            log_domain = fields[i].value;
            log_domain_length = fields[i].length;
        }
    }

    ns_log_read_env();
    char prefix[64];
    ns_log_level_prefix(prefix, log_level, use_color);

    NsString *s = ns_string_new(NULL);
    if (log_level & NS_ALERT_LEVELS) ns_string_append(s, "\n");
    if (!log_domain) ns_string_append(s, "** ");
    if ((ns_log_msg_prefix & (log_level & NS_LOG_LEVEL_MASK)) ==
        (log_level & NS_LOG_LEVEL_MASK))
        ns_string_append_printf(s, "(process:%lu): ", ns_log_pid());
    if (log_domain) {
        ns_string_append_len(s, log_domain, log_domain_length);
        ns_string_append_c(s, '-');
    }
    ns_string_append(s, prefix);
    ns_string_append(s, ": ");

    int64_t now = ns_log_real_time_us();
    time_t secs = (time_t)(now / 1000000);
    struct tm tm;
    char time_buf[64];
#ifdef _WIN32
    /* The C runtime's localtime keeps its result per thread. */
    const struct tm *lt = localtime(&secs);
    int ok = lt != NULL;
    if (ok) tm = *lt;
#else
    int ok = localtime_r(&secs, &tm) != NULL;
#endif
    if (ok) strftime(time_buf, sizeof time_buf, "%H:%M:%S", &tm);
    else strcpy(time_buf, "(error)");
    ns_string_append_printf(s, "%s%s.%03d%s: ", use_color ? "\033[34m" : "", time_buf,
                            (int)((now / 1000) % 1000), use_color ? "\033[0m" : "");

    if (!message)
        ns_string_append(s, "(NULL) message");
    else
        ns_log_escape(s, message, message_length);
    return ns_string_free(s, 0);
}

static int
ns_log_supports_color(FILE *stream)
{
#ifdef _WIN32
    (void)stream;
    return 0;
#else
    return isatty(fileno(stream));
#endif
}

NsLogWriterOutput
ns_log_writer_standard_streams(NsLogLevelFlags log_level, const NsLogField *fields,
                               size_t n_fields, void *user_data)
{
    (void)user_data;
    if (!fields || n_fields == 0) return NS_LOG_WRITER_UNHANDLED;
    FILE *stream = ns_log_level_to_file(log_level);
    if (!stream || fileno(stream) < 0) return NS_LOG_WRITER_UNHANDLED;
    char *out = ns_log_writer_format_fields(log_level, fields, n_fields,
                                            ns_log_supports_color(stream));
    fprintf(stream, "%s\n", out);
    fflush(stream);
    ns_free(out);
    return NS_LOG_WRITER_HANDLED;
}

static int
ns_log_domain_found(const char *domains, const char *log_domain)
{
    size_t len = strlen(log_domain);
    for (const char *f = strstr(domains, log_domain); f; f = strstr(f + 1, log_domain))
        if ((f == domains || f[-1] == ' ') && (f[len] == '\0' || f[len] == ' '))
            return 1;
    return 0;
}

static int
ns_log_should_drop(int log_level, const char *log_domain, const NsLogField *fields,
                   size_t n_fields)
{
    if ((log_level & NS_DEFAULT_LEVELS) || (log_level >> NS_LOG_LEVEL_USER_SHIFT))
        return 0;
    ns_log_read_env();
    if (!(log_level & NS_INFO_LEVELS) || !ns_log_debug_domains) return 1;
    if (!log_domain)
        for (size_t i = 0; i < n_fields; i++)
            if (ns_strcmp0(fields[i].key, "GLIB_DOMAIN") == 0) {
                log_domain = fields[i].value;
                break;
            }
    if (strcmp(ns_log_debug_domains, "all") != 0 &&
        (!log_domain || !ns_log_domain_found(ns_log_debug_domains, log_domain)))
        return 1;
    return 0;
}

int
ns_log_writer_default_would_drop(NsLogLevelFlags log_level, const char *log_domain)
{
    return ns_log_should_drop(log_level, log_domain, NULL, 0);
}

static int
ns_log_is_old_api(const NsLogField *fields, size_t n_fields)
{
    return n_fields >= 1 && ns_strcmp0(fields[0].key, "GLIB_OLD_LOG_API") == 0 &&
           ns_strcmp0(fields[0].value, "1") == 0;
}

NsLogWriterOutput
ns_log_writer_default(NsLogLevelFlags log_level, const NsLogField *fields,
                      size_t n_fields, void *user_data)
{
    if (!fields || n_fields == 0) return NS_LOG_WRITER_UNHANDLED;
    if (ns_log_should_drop(log_level, NULL, fields, n_fields)) return NS_LOG_WRITER_HANDLED;
    ns_log_read_env();
    if ((log_level & ns_log_always_fatal) && !ns_log_is_old_api(fields, n_fields))
        log_level = (NsLogLevelFlags)(log_level | NS_LOG_FLAG_FATAL);
    if (ns_log_writer_standard_streams(log_level, fields, n_fields, user_data) !=
        NS_LOG_WRITER_HANDLED)
        return NS_LOG_WRITER_UNHANDLED;
    if (log_level & NS_LOG_FLAG_FATAL) ns_log_abort();
    return NS_LOG_WRITER_HANDLED;
}

/* For a message logged while logging: the plain fields, straight out. */
static NsLogWriterOutput
ns_log_writer_fallback(NsLogLevelFlags log_level, const NsLogField *fields,
                       size_t n_fields, void *user_data)
{
    (void)user_data;
    FILE *stream = ns_log_level_to_file(log_level);
    for (size_t i = 0; i < n_fields; i++) {
        const char *k = fields[i].key;
        if (strcmp(k, "MESSAGE") && strcmp(k, "PRIORITY") && strcmp(k, "GLIB_DOMAIN"))
            continue;
        fputs(k, stream);
        fputc('=', stream);
        if (fields[i].length < 0) fputs(fields[i].value, stream);
        else fwrite(fields[i].value, 1, (size_t)fields[i].length, stream);
    }
    fprintf(stream, "_PID=%lu", ns_log_pid());
    fflush(stream);
    return NS_LOG_WRITER_HANDLED;
}

void
ns_log_structured_array(NsLogLevelFlags log_level, const NsLogField *fields,
                        size_t n_fields)
{
    if (n_fields == 0) return;
    int recursion = ns_log_structured_depth > 0;
    ns_spin_lock(&ns_log_lock);
    NsLogWriterFunc writer = recursion ? ns_log_writer_fallback : ns_log_writer_func;
    void *data = ns_log_writer_user_data;
    ns_spin_unlock(&ns_log_lock);
    ns_log_structured_depth++;
    writer(log_level, fields, n_fields, data);
    ns_log_structured_depth--;
    if (log_level & NS_LOG_FATAL_MASK) ns_log_abort();
}

void
ns_log_set_writer_func(NsLogWriterFunc func, void *user_data,
                       NsDestroyNotify user_data_free)
{
    (void)user_data_free;
    if (!func) return;
    ns_spin_lock(&ns_log_lock);
    int was_set = ns_log_writer_set;
    if (!was_set) {
        ns_log_writer_func = func;
        ns_log_writer_user_data = user_data;
        ns_log_writer_set = 1;
    }
    ns_spin_unlock(&ns_log_lock);
    if (was_set) {
        fputs("nscore: the log writer function can only be set once\n", stderr);
        ns_log_abort();
    }
}

/* g_log_default_handler: the message as structured fields, the fatal flag
 * dropped since ns_logv has handled it. */
static void
ns_log_default_handler(const char *log_domain, int log_level, const char *message)
{
    NsLogField fields[4] = {
        { "GLIB_OLD_LOG_API", "1", -1 },
        { "MESSAGE", message, -1 },
        { "PRIORITY", ns_log_level_to_priority(log_level), -1 },
        { "GLIB_DOMAIN", log_domain, -1 },
    };
    ns_log_structured_array((NsLogLevelFlags)(log_level & ~NS_LOG_FLAG_FATAL), fields,
                            log_domain ? 4 : 3);
}

static void
ns_log_fallback_handler(const char *log_domain, int log_level, const char *message)
{
    char prefix[64];
    ns_log_level_prefix(prefix, log_level, 0);
    FILE *stream = ns_log_level_to_file(log_level);
    fprintf(stream, "%s(process:%lu): %s%s%s: %s\n", log_domain ? "\n" : "\n** ",
            ns_log_pid(), log_domain ? log_domain : "", log_domain ? "-" : "", prefix,
            message ? message : "(NULL) message");
    fflush(stream);
}

void
ns_logv(const char *log_domain, NsLogLevelFlags log_level, const char *format,
        va_list args)
{
    int was_fatal = (log_level & NS_LOG_FLAG_FATAL) != 0;
    int was_recursion = (log_level & NS_LOG_FLAG_RECURSION) != 0;
    int level = log_level & NS_LOG_LEVEL_MASK;
    if (!level) return;
    ns_log_read_env();

    char *msg = ns_strdup_vprintf(format, args);
    for (int i = 31; i >= 0; i--) {
        int test_level = (int)(1u << i);
        if (!(level & test_level)) continue;
        if (was_fatal) test_level |= NS_LOG_FLAG_FATAL;
        if (was_recursion) test_level |= NS_LOG_FLAG_RECURSION;
        if (ns_log_depth) test_level |= NS_LOG_FLAG_RECURSION;
        if ((NS_LOG_FATAL_MASK | ns_log_always_fatal) & test_level)
            test_level |= NS_LOG_FLAG_FATAL;

        ns_log_depth++;
        if (test_level & NS_LOG_FLAG_RECURSION)
            ns_log_fallback_handler(log_domain, test_level, msg);
        else
            ns_log_default_handler(log_domain, test_level, msg);
        ns_log_depth--;

        if (test_level & NS_LOG_FLAG_FATAL) ns_log_abort();
    }
    ns_free(msg);
}

void
ns_log(const char *log_domain, NsLogLevelFlags log_level, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    ns_logv(log_domain, log_level, format, ap);
    va_end(ap);
}

static void ns_print_to(FILE *stream, const char *format, va_list args) NS_PRINTF(2, 0);

static void
ns_print_to(FILE *stream, const char *format, va_list args)
{
    char *s = ns_strdup_vprintf(format, args);
    if (s) {
        fputs(s, stream);
        fflush(stream);
        ns_free(s);
    }
}

void
ns_print(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    ns_print_to(stdout, format, ap);
    va_end(ap);
}

void
ns_printerr(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    ns_print_to(stderr, format, ap);
    va_end(ap);
}

void
ns_return_if_fail_warning(const char *log_domain, const char *pretty_function,
                          const char *expression)
{
    ns_log(log_domain, NS_LOG_LEVEL_CRITICAL, "%s: assertion '%s' failed",
           pretty_function, expression);
}

void
ns_warn_message(const char *domain, const char *file, int line, const char *func,
                const char *warnexpr)
{
    if (warnexpr)
        ns_log(domain, NS_LOG_LEVEL_WARNING, "%s:%d:%s%s: runtime check failed: (%s)",
               file, line, func, func[0] ? "" : "", warnexpr);
    else
        ns_log(domain, NS_LOG_LEVEL_WARNING, "%s:%d:%s%s code should not be reached",
               file, line, func, func[0] ? ":" : "");
}

void
ns_assertion_message(const char *domain, const char *file, int line, const char *func,
                     const char *message)
{
    if (!message) message = "code should not be reached";
    fprintf(stderr, "**\n%s%sERROR:%s:%d:%s%s %s\n", domain ? domain : "",
            domain && domain[0] ? ":" : "", file, line, func, func[0] ? ":" : "", message);
    ns_log_abort();
}

void
ns_assertion_message_expr(const char *domain, const char *file, int line,
                          const char *func, const char *expr)
{
    if (!expr) {
        ns_assertion_message(domain, file, line, func, NULL);
    } else {
        char *s = ns_strconcat("assertion failed: (", expr, ")", NULL);
        ns_assertion_message(domain, file, line, func, s);
    }
}
