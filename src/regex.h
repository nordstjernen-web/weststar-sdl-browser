/* Weststar SDL Browser — regular expressions, by the JavaScript engine.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef NS_REGEX_H
#define NS_REGEX_H

#include <glib.h>

/* A compiled JavaScript regular expression, usable from any thread. The
 * browser's regular expressions outside of scripts are the web's own: the
 * pattern attribute and the filters of extensions' request rules, both
 * defined in JavaScript's syntax. They run on the JavaScript engine's
 * regular expression code, in a small runtime of their own behind a
 * lock, so no second regular expression library (GLib's GRegex and the
 * PCRE2 under it) is needed. */
typedef struct NsRegex NsRegex;

/* Compiles `pattern` with JavaScript RegExp flags ("i", "v", "u", …);
 * NULL when it is not a valid expression. */
NsRegex *ns_regex_new(const char *pattern, const char *flags);
void ns_regex_free(NsRegex *regex);

/* 1 if the expression matches somewhere in `text`, 0 if not, -1 if the
 * match could not be finished (it ran too long, or out of memory). */
int ns_regex_test(NsRegex *regex, const char *text);

/* Whether a form control's value satisfies its pattern attribute: the
 * whole value must match the pattern, compiled with the v flag; an
 * empty or invalid pattern is satisfied by anything, as HTML says. */
gboolean ns_regex_pattern_attribute_matches(const char *value,
                                            const char *pattern);

#endif
