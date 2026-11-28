/* Weststar SDL Browser — nscore: logging (g_log and its writers), g_print,
 * and the messages of failed checks and assertions.
 *
 * As in GLib: a message goes, as structured fields (GLIB_OLD_LOG_API,
 * MESSAGE, PRIORITY, GLIB_DOMAIN), to the writer function set with
 * ns_log_set_writer_func, by default ns_log_writer_default, which drops
 * debug and info messages unless G_MESSAGES_DEBUG names their domain (or
 * "all") and prints the rest as
 *   ** (process:1234): WARNING **: 10:11:12.345: the message
 * to stderr (stdout for info and debug). An error-level message aborts;
 * critical and warning messages abort only under G_DEBUG=fatal-criticals
 * or fatal-warnings.
 *
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_LOG_H
#define NS_LOG_H

#include "ns-base.h"

NS_BEGIN_DECLS

typedef enum {
    NS_LOG_FLAG_RECURSION = 1 << 0,
    NS_LOG_FLAG_FATAL = 1 << 1,
    NS_LOG_LEVEL_ERROR = 1 << 2,
    NS_LOG_LEVEL_CRITICAL = 1 << 3,
    NS_LOG_LEVEL_WARNING = 1 << 4,
    NS_LOG_LEVEL_MESSAGE = 1 << 5,
    NS_LOG_LEVEL_INFO = 1 << 6,
    NS_LOG_LEVEL_DEBUG = 1 << 7,
    NS_LOG_LEVEL_MASK = ~(NS_LOG_FLAG_RECURSION | NS_LOG_FLAG_FATAL)
} NsLogLevelFlags;

#define NS_LOG_LEVEL_USER_SHIFT 8
#define NS_LOG_FATAL_MASK (NS_LOG_FLAG_RECURSION | NS_LOG_LEVEL_ERROR)

typedef enum {
    NS_LOG_WRITER_HANDLED = 1,
    NS_LOG_WRITER_UNHANDLED = 0
} NsLogWriterOutput;

typedef struct NsLogField {
    const char *key;
    const void *value;
    ptrdiff_t length;
} NsLogField;

typedef NsLogWriterOutput (*NsLogWriterFunc)(NsLogLevelFlags log_level,
                                             const NsLogField *fields, size_t n_fields,
                                             void *user_data);

void ns_log(const char *log_domain, NsLogLevelFlags log_level, const char *format, ...)
    NS_PRINTF(3, 4);
void ns_logv(const char *log_domain, NsLogLevelFlags log_level, const char *format,
             va_list args) NS_PRINTF(3, 0);
void ns_log_structured_array(NsLogLevelFlags log_level, const NsLogField *fields,
                             size_t n_fields);
void ns_log_set_writer_func(NsLogWriterFunc func, void *user_data,
                            NsDestroyNotify user_data_free);
NsLogWriterOutput ns_log_writer_default(NsLogLevelFlags log_level,
                                        const NsLogField *fields, size_t n_fields,
                                        void *user_data);
NsLogWriterOutput ns_log_writer_standard_streams(NsLogLevelFlags log_level,
                                                 const NsLogField *fields,
                                                 size_t n_fields, void *user_data);
int ns_log_writer_default_would_drop(NsLogLevelFlags log_level, const char *log_domain);
char *ns_log_writer_format_fields(NsLogLevelFlags log_level, const NsLogField *fields,
                                  size_t n_fields, int use_color);
NsLogLevelFlags ns_log_set_always_fatal(NsLogLevelFlags fatal_mask);

void ns_print(const char *format, ...) NS_PRINTF(1, 2);
void ns_printerr(const char *format, ...) NS_PRINTF(1, 2);

void ns_return_if_fail_warning(const char *log_domain, const char *pretty_function,
                               const char *expression);
void ns_warn_message(const char *domain, const char *file, int line, const char *func,
                     const char *warnexpr);
NS_NORETURN void ns_assertion_message_expr(const char *domain, const char *file,
                                           int line, const char *func,
                                           const char *expr);
NS_NORETURN void ns_assertion_message(const char *domain, const char *file, int line,
                                      const char *func, const char *message);

NS_END_DECLS

#endif
