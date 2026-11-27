/* Weststar SDL Browser — regular expressions, by the JavaScript engine.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "regex.h"
#include "plat/ns_plat.h"
#include "quickjs_compat.h"

#include <string.h>

/* One match may take this long; a pattern that backtracks without end
 * (which a page or an extension can supply) is then given up on. */
#define NS_REGEX_BUDGET_US (250 * 1000)

struct NsRegex {
    JSValue regexp;
};

/* The runtime every expression lives in, made on first use and kept. The
 * lock is held around every use of it: the engine allows one thread at a
 * time, whichever thread that is. */
static NsMutex g_lock;
static JSRuntime *g_rt;
static JSContext *g_ctx;
static gint64 g_deadline_us;

static int
regex_interrupt(void *opaque)
{
    (void)opaque;
    return g_deadline_us && ns_time_monotonic_us() > g_deadline_us;
}

static int
regex_interrupt_ctx_cb(JSContext *ctx, void *opaque)
{
    (void)ctx;
    return regex_interrupt(opaque);
}

static int
regex_interrupt_rt_cb(JSRuntime *rt, void *opaque)
{
    (void)rt;
    return regex_interrupt(opaque);
}

/* Called with the lock held; FALSE when there is no engine to use. */
static gboolean
regex_enter(void)
{
    if (!g_rt) {
        JSRuntime *rt = JS_NewRuntime();
        JSContext *ctx = rt ? JS_NewContext(rt) : NULL;
        if (!ctx) {
            if (rt) JS_FreeRuntime(rt);
            return FALSE;
        }
        JS_SetMemoryLimit(rt, 32u * 1024u * 1024u);
        ns_js_set_interrupt_handler(rt, regex_interrupt_ctx_cb,
                                    regex_interrupt_rt_cb, NULL);
        g_rt = rt;
        g_ctx = ctx;
    }
    /* The engine measures its stack from the thread it believes it is on. */
    JS_UpdateStackTop(g_rt);
    return TRUE;
}

static void
regex_clear_exception(void)
{
    JS_FreeValue(g_ctx, JS_GetException(g_ctx));
}

/* Called with the lock held. */
static NsRegex *
regex_compile(const char *pattern, const char *flags)
{
    JSValue global = JS_GetGlobalObject(g_ctx);
    JSValue ctor = JS_GetPropertyStr(g_ctx, global, "RegExp");
    JSValue args[2] = {
        JS_NewString(g_ctx, pattern),
        JS_NewString(g_ctx, flags ? flags : ""),
    };
    JSValue regexp = JS_CallConstructor(g_ctx, ctor, 2, (JSValueConst *)args);
    JS_FreeValue(g_ctx, args[0]);
    JS_FreeValue(g_ctx, args[1]);
    JS_FreeValue(g_ctx, ctor);
    JS_FreeValue(g_ctx, global);
    if (JS_IsException(regexp)) {
        regex_clear_exception();
        return NULL;
    }
    NsRegex *regex = g_new0(NsRegex, 1);
    regex->regexp = regexp;
    return regex;
}

NsRegex *
ns_regex_new(const char *pattern, const char *flags)
{
    NsRegex *regex = NULL;
    if (!pattern || !g_utf8_validate(pattern, -1, NULL))
        return NULL;
    ns_mutex_lock(&g_lock);
    if (regex_enter())
        regex = regex_compile(pattern, flags);
    ns_mutex_unlock(&g_lock);
    return regex;
}

void
ns_regex_free(NsRegex *regex)
{
    if (!regex) return;
    ns_mutex_lock(&g_lock);
    JS_FreeValue(g_ctx, regex->regexp);
    ns_mutex_unlock(&g_lock);
    g_free(regex);
}

/* Called with the lock held. */
static int
regex_test(NsRegex *regex, const char *text)
{
    /* A sticky or global expression remembers where it stopped. */
    JS_SetPropertyStr(g_ctx, regex->regexp, "lastIndex", JS_NewInt32(g_ctx, 0));
    JSValue test = JS_GetPropertyStr(g_ctx, regex->regexp, "test");
    JSValue arg = JS_NewString(g_ctx, text ? text : "");
    g_deadline_us = ns_time_monotonic_us() + NS_REGEX_BUDGET_US;
    JSValue result = JS_Call(g_ctx, test, regex->regexp, 1, (JSValueConst *)&arg);
    g_deadline_us = 0;
    JS_FreeValue(g_ctx, arg);
    JS_FreeValue(g_ctx, test);
    if (JS_IsException(result)) {
        regex_clear_exception();
        return -1;
    }
    int matched = JS_ToBool(g_ctx, result) > 0;
    JS_FreeValue(g_ctx, result);
    return matched;
}

int
ns_regex_test(NsRegex *regex, const char *text)
{
    int matched = -1;
    if (!regex || (text && !g_utf8_validate(text, -1, NULL)))
        return -1;
    ns_mutex_lock(&g_lock);
    if (regex_enter())
        matched = regex_test(regex, text);
    ns_mutex_unlock(&g_lock);
    return matched;
}

/* The answers for the pattern attribute are remembered: the same control
 * is asked about at every style recalculation while its value stands, and
 * a pattern that runs out its budget should do so once. Keyed by pattern
 * and value; emptied when it has grown. Guarded by its own lock. */
#define NS_REGEX_CACHE_MAX 512
static NsMutex g_cache_lock;
static GHashTable *g_cache;

static gboolean
pattern_attribute_matches(const char *value, const char *pattern)
{
    /* The pattern must be an expression by itself: "a)(b" is not one,
     * though wrapping it for the whole value would make it compile. */
    NsRegex *bare = ns_regex_new(pattern, "v");
    if (!bare)
        return TRUE;
    ns_regex_free(bare);
    char *anchored = g_strdup_printf("^(?:%s)$", pattern);
    NsRegex *regex = ns_regex_new(anchored, "v");
    g_free(anchored);
    if (!regex)
        return TRUE;
    /* A match that could not be finished does not hold the form back. */
    int matched = ns_regex_test(regex, value);
    ns_regex_free(regex);
    return matched != 0;
}

gboolean
ns_regex_pattern_attribute_matches(const char *value, const char *pattern)
{
    if (!pattern || !*pattern)
        return TRUE;
    if (!value) value = "";
    /* No byte of UTF-8 text is 0xff, so it parts the two unambiguously. */
    char *key = g_strconcat(pattern, "\xff", value, NULL);
    gpointer cached = NULL;
    ns_mutex_lock(&g_cache_lock);
    gboolean known = g_cache &&
        g_hash_table_lookup_extended(g_cache, key, NULL, &cached);
    ns_mutex_unlock(&g_cache_lock);
    if (known) {
        g_free(key);
        return GPOINTER_TO_INT(cached);
    }

    gboolean matches = pattern_attribute_matches(value, pattern);

    ns_mutex_lock(&g_cache_lock);
    if (!g_cache)
        g_cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    if (g_hash_table_size(g_cache) >= NS_REGEX_CACHE_MAX)
        g_hash_table_remove_all(g_cache);
    g_hash_table_insert(g_cache, key, GINT_TO_POINTER(matches));
    ns_mutex_unlock(&g_cache_lock);
    return matches;
}
