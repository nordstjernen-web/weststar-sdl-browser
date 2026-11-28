/* Weststar SDL Browser — nscore: key files.
 *
 * Laid out as GLib keeps a key file, so that loading, setting and writing
 * give what GLib gives: a nameless first group for what precedes the
 * first [group], groups in the order they were made, and in each group
 * its pairs in file order, a pair with no key standing for a blank line.
 * A group made after another one gives that one a trailing blank line,
 * unless it already ends in one; a key set later goes after the group's
 * last key, before its trailing blank lines.
 *
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ns-keyfile.h"
#include "ns-hash.h"
#include "ns-log.h"
#include "ns-mem.h"
#include "ns-string.h"
#include "ns-strfuncs.h"
#include "ns-unicode.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    char *key;   /* NULL: a blank line or comment */
    char *value;
} NsKeyPair;

typedef struct {
    char *name;  /* NULL for the group before the first [group] */
    NsKeyPair **pairs;
    size_t n_pairs, cap_pairs;
    NsHashTable *lookup;  /* key -> its last pair */
} NsKeyGroup;

struct NsKeyFile {
    NsKeyGroup **groups;  /* in the order made; [0] is the nameless one */
    size_t n_groups, cap_groups;
    NsHashTable *group_hash;
    NsKeyGroup *current;
    NsKeyGroup *start;    /* the first named group */
    NsString *parse_buffer;
    NsKeyFileFlags flags;
    char list_separator;
};

NsQuark
ns_key_file_error_quark(void)
{
    return ns_quark_from_static_string("g-key-file-error-quark");
}

/* ---- structure ----------------------------------------------------------- */

static NsKeyGroup *
ns_key_group_new(const char *name)
{
    NsKeyGroup *g = ns_malloc0(sizeof *g);
    g->name = ns_strdup(name);
    g->lookup = ns_hash_table_new(ns_str_hash, ns_str_equal);
    return g;
}

static void
ns_key_group_free(NsKeyGroup *g)
{
    for (size_t i = 0; i < g->n_pairs; i++) {
        ns_free(g->pairs[i]->key);
        ns_free(g->pairs[i]->value);
        ns_free(g->pairs[i]);
    }
    ns_free(g->pairs);
    ns_hash_table_destroy(g->lookup);
    ns_free(g->name);
    ns_free(g);
}

static void
ns_key_group_insert(NsKeyGroup *g, size_t at, char *key, char *value)
{
    if (g->n_pairs == g->cap_pairs) {
        g->cap_pairs = g->cap_pairs ? g->cap_pairs * 2 : 8;
        g->pairs = ns_realloc_n(g->pairs, g->cap_pairs, sizeof *g->pairs);
    }
    memmove(g->pairs + at + 1, g->pairs + at, (g->n_pairs - at) * sizeof *g->pairs);
    NsKeyPair *p = ns_malloc(sizeof *p);
    p->key = key;
    p->value = value;
    g->pairs[at] = p;
    g->n_pairs++;
    if (key) ns_hash_table_replace(g->lookup, key, p);
}

static void
ns_key_file_init(NsKeyFile *kf)
{
    kf->groups = NULL;
    kf->n_groups = kf->cap_groups = 0;
    kf->group_hash = ns_hash_table_new(ns_str_hash, ns_str_equal);
    kf->start = NULL;
    kf->parse_buffer = NULL;
    kf->flags = NS_KEY_FILE_NONE;
    kf->list_separator = ';';
    kf->cap_groups = 8;
    kf->groups = ns_malloc_n(kf->cap_groups, sizeof *kf->groups);
    kf->groups[kf->n_groups++] = ns_key_group_new(NULL);
    kf->current = kf->groups[0];
}

static void
ns_key_file_clear(NsKeyFile *kf)
{
    for (size_t i = 0; i < kf->n_groups; i++) ns_key_group_free(kf->groups[i]);
    ns_free(kf->groups);
    kf->groups = NULL;
    kf->n_groups = 0;
    ns_hash_table_destroy(kf->group_hash);
    kf->group_hash = NULL;
    if (kf->parse_buffer) ns_string_free(kf->parse_buffer, 1);
    kf->parse_buffer = NULL;
}

NsKeyFile *
ns_key_file_new(void)
{
    NsKeyFile *kf = ns_malloc0(sizeof *kf);
    ns_key_file_init(kf);
    return kf;
}

void
ns_key_file_free(NsKeyFile *key_file)
{
    if (!key_file) return;
    ns_key_file_clear(key_file);
    ns_free(key_file);
}

static NsKeyGroup *
ns_key_file_lookup_group(NsKeyFile *kf, const char *name)
{
    return kf->group_hash ? ns_hash_table_lookup(kf->group_hash, name) : NULL;
}

/* No '[', ']' or control character, and not empty. */
static int
ns_key_file_is_group_name(const char *name)
{
    if (!name || !*name) return 0;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++)
        if (*p == '[' || *p == ']' || *p < 0x20 || *p == 0x7f) return 0;
    return 1;
}

/* A key: no '=', '[' or ']', no space at either end, not empty, and
 * optionally a [locale] of letters, digits and "-_.@" at the end. */
static int
ns_key_file_is_key_name(const char *name, size_t len)
{
    const char *p = name, *q = name, *end = name + len;
    while (q < end && *q && *q != '=' && *q != '[' && *q != ']') q++;
    if (q == p) return 0;
    if (*p == ' ' || q[-1] == ' ') return 0;
    if (q < end && *q == '[') {
        q++;
        while (q < end && *q) {
            unsigned char c = (unsigned char)*q;
            if (c == '-' || c == '_' || c == '.' || c == '@') {
                q++;
                continue;
            }
            ns_unichar uc = ns_utf8_get_char_validated(q, end - q);
            if (uc == (ns_unichar)-1 || uc == (ns_unichar)-2 || !ns_unichar_isalnum(uc)) break;
            q = ns_utf8_next_char(q);
            if (q > end) q = end;
        }
        if (q >= end || *q != ']') return 0;
        q++;
    }
    return q >= end;
}

static void
ns_key_file_add_group(NsKeyFile *kf, const char *name, int created)
{
    NsKeyGroup *g = ns_key_file_lookup_group(kf, name);
    if (g) {
        kf->current = g;
        return;
    }
    g = ns_key_group_new(name);
    if (kf->n_groups == kf->cap_groups) {
        kf->cap_groups *= 2;
        kf->groups = ns_realloc_n(kf->groups, kf->cap_groups, sizeof *kf->groups);
    }
    NsKeyGroup *prev = kf->groups[kf->n_groups - 1];
    kf->groups[kf->n_groups++] = g;
    kf->current = g;
    if (!kf->start) {
        kf->start = g;
    } else if (!(kf->flags & NS_KEY_FILE_KEEP_COMMENTS) || created) {
        NsKeyPair *last = prev->n_pairs ? prev->pairs[prev->n_pairs - 1] : NULL;
        if (!last || (last->key && !strchr(last->value, '\n')))
            ns_key_group_insert(prev, prev->n_pairs, NULL, ns_strdup(""));
    }
    ns_hash_table_insert(kf->group_hash, g->name, g);
}

/* A key set by name goes after the group's last key. */
static void
ns_key_file_add_key(NsKeyGroup *g, const char *key, const char *value)
{
    size_t at = g->n_pairs;
    while (at > 0 && g->pairs[at - 1]->key == NULL) at--;
    ns_key_group_insert(g, at, ns_strdup(key), ns_strdup(value));
}

/* ---- loading ---------------------------------------------------------------- */

/* The names GLib's g_get_language_names gives, from LANGUAGE, LC_ALL,
 * LC_MESSAGES or LANG: whether locale (of length len) is one of them,
 * that is lang[_TERRITORY][.CODESET][@MODIFIER] with any of the optional
 * parts left out, or "C". */
static int
ns_key_file_locale_is_interesting(NsKeyFile *kf, const char *locale, size_t len)
{
    if (kf->flags & NS_KEY_FILE_KEEP_TRANSLATIONS) return 1;
    if (len == 1 && locale[0] == 'C') return 1;
    const char *env = NULL;
    static const char *const vars[] = { "LANGUAGE", "LC_ALL", "LC_MESSAGES", "LANG" };
    for (size_t i = 0; i < 4 && !env; i++) {
        const char *v = getenv(vars[i]);
        if (v && *v) env = v;
    }
    if (!env) return 0;
    char **names = ns_strsplit(env, ":", 0);
    int found = 0;
    for (char **n = names; *n && !found; n++) {
        const char *s = *n;
        size_t l_lang = strcspn(s, "_.@");
        const char *terr = s[l_lang] == '_' ? s + l_lang : "";
        size_t l_terr = *terr ? strcspn(terr + 1, ".@") + 1 : 0;
        const char *rest = s + l_lang + l_terr;
        const char *code = *rest == '.' ? rest : "";
        size_t l_code = *code ? strcspn(code + 1, "@") + 1 : 0;
        const char *mod = strchr(s + l_lang, '@');
        size_t l_mod = mod ? strlen(mod) : 0;
        for (int mask = 0; mask < 8 && !found; mask++) {
            if (((mask & 1) && !l_terr) || ((mask & 2) && !l_code) || ((mask & 4) && !l_mod))
                continue;
            size_t total = l_lang + ((mask & 1) ? l_terr : 0) + ((mask & 2) ? l_code : 0) +
                           ((mask & 4) ? l_mod : 0);
            if (total != len) continue;
            char *v = ns_strndup(s, l_lang);
            NsString *str = ns_string_new(v);
            ns_free(v);
            if (mask & 1) ns_string_append_len(str, terr, (ptrdiff_t)l_terr);
            if (mask & 2) ns_string_append_len(str, code, (ptrdiff_t)l_code);
            if (mask & 4) ns_string_append_len(str, mod, (ptrdiff_t)l_mod);
            if (ns_ascii_strncasecmp(str->str, locale, len) == 0) found = 1;
            ns_string_free(str, 1);
        }
    }
    ns_strfreev(names);
    return found;
}

static void
ns_key_file_parse_group(NsKeyFile *kf, const char *line, size_t length, NsError **error)
{
    const char *start = line + 1;
    const char *end = line + length - 1;
    while (*end != ']') end--;
    char *name = ns_strndup(start, (size_t)(end - start));
    if (!ns_key_file_is_group_name(name)) {
        ns_set_error(error, NS_KEY_FILE_ERROR, NS_KEY_FILE_ERROR_PARSE,
                     "Invalid group name: %s", name);
        ns_free(name);
        return;
    }
    ns_key_file_add_group(kf, name, 0);
    ns_free(name);
}

static void
ns_key_file_parse_key_value_pair(NsKeyFile *kf, const char *line, size_t length,
                                 NsError **error)
{
    if (!kf->current || !kf->current->name) {
        ns_set_error_literal(error, NS_KEY_FILE_ERROR, NS_KEY_FILE_ERROR_GROUP_NOT_FOUND,
                             "Key file does not start with a group");
        return;
    }
    const char *eq = strchr(line, '=');
    const char *key_end = eq - 1;
    while (key_end > line && ns_ascii_isspace(*key_end)) key_end--;
    size_t key_len = (size_t)(key_end - line + 1);
    if (!ns_key_file_is_key_name(line, key_len)) {
        ns_set_error(error, NS_KEY_FILE_ERROR, NS_KEY_FILE_ERROR_PARSE,
                     "Invalid key name: %.*s", (int)key_len, line);
        return;
    }
    char *key = ns_strndup(line, key_len);
    const char *value_start = eq + 1;
    while (ns_ascii_isspace(*value_start)) value_start++;
    size_t value_len = (size_t)(line + length - value_start);

    if (kf->start == kf->current && strcmp(key, "Encoding") == 0 &&
        (value_len != 5 || ns_ascii_strncasecmp(value_start, "UTF-8", value_len) != 0)) {
        char *v = ns_utf8_make_valid(value_start, (ptrdiff_t)value_len);
        ns_set_error(error, NS_KEY_FILE_ERROR, NS_KEY_FILE_ERROR_UNKNOWN_ENCODING,
                     "Key file contains unsupported encoding \xe2\x80\x9c%s\xe2\x80\x9d", v);
        ns_free(v);
        ns_free(key);
        return;
    }

    /* key[locale]: kept only for a locale of the user's. */
    const char *bracket = strrchr(key, '[');
    size_t locale_len = bracket ? strlen(bracket) : 0;
    if (locale_len <= 2 ||
        ns_key_file_locale_is_interesting(kf, bracket + 1, locale_len - 2)) {
        NsKeyGroup *g = kf->current;
        ns_key_group_insert(g, g->n_pairs, key, ns_strndup(value_start, value_len));
        return;
    }
    ns_free(key);
}

static void
ns_key_file_parse_line(NsKeyFile *kf, const char *line, size_t length, NsError **error)
{
    const char *p = line;
    while (ns_ascii_isspace(*p)) p++;
    size_t rest = length - (size_t)(p - line);
    if (*p == '#' || *p == '\0' || *p == '\n') {
        return;  /* a comment or blank line: not kept */
    }
    if (*p == '[') {
        const char *q = strchr(p, ']');
        if (q) {
            q++;
            while (*q == ' ' || *q == '\t') q++;
            if (!*q) {
                ns_key_file_parse_group(kf, p, rest, error);
                return;
            }
        }
    }
    if (*p != '=' && strchr(p, '=')) {
        ns_key_file_parse_key_value_pair(kf, p, rest, error);
        return;
    }
    char *l = ns_utf8_make_valid(line, (ptrdiff_t)length);
    ns_set_error(error, NS_KEY_FILE_ERROR, NS_KEY_FILE_ERROR_PARSE,
                 "Key file contains line \xe2\x80\x9c%s\xe2\x80\x9d which is not a "
                 "key-value pair, group, or comment", l);
    ns_free(l);
}

static void
ns_key_file_flush_parse_buffer(NsKeyFile *kf, NsError **error)
{
    if (!kf->parse_buffer || kf->parse_buffer->len == 0) return;
    NsError *e = NULL;
    ns_key_file_parse_line(kf, kf->parse_buffer->str, kf->parse_buffer->len, &e);
    ns_string_truncate(kf->parse_buffer, 0);
    if (e) ns_propagate_error(error, e);
}

int
ns_key_file_load_from_data(NsKeyFile *key_file, const char *data, size_t length,
                           NsKeyFileFlags flags, NsError **error)
{
    if (!key_file || (!data && length)) return 0;
    if (length == (size_t)-1) length = strlen(data);
    char sep = key_file->list_separator;
    ns_key_file_clear(key_file);
    ns_key_file_init(key_file);
    key_file->list_separator = sep;
    key_file->flags = flags;
    key_file->parse_buffer = ns_string_sized_new(128);

    NsError *e = NULL;
    size_t i = 0;
    while (i < length) {
        if (data[i] == '\n') {
            NsString *buf = key_file->parse_buffer;
            if (buf->len > 0 && buf->str[buf->len - 1] == '\r')
                ns_string_truncate(buf, buf->len - 1);
            ns_key_file_flush_parse_buffer(key_file, &e);
            if (e) {
                ns_propagate_error(error, e);
                return 0;
            }
            i++;
        } else {
            const char *start = data + i;
            const char *nl = memchr(start, '\n', length - i);
            size_t n = nl ? (size_t)(nl - start) : length - i;
            ns_string_append_len(key_file->parse_buffer, start, (ptrdiff_t)n);
            i += n;
        }
    }
    ns_key_file_flush_parse_buffer(key_file, &e);
    if (e) {
        ns_propagate_error(error, e);
        return 0;
    }
    return 1;
}

/* ---- writing and reading -------------------------------------------------------- */

char *
ns_key_file_to_data(NsKeyFile *key_file, size_t *length, NsError **error)
{
    (void)error;
    NsString *s = ns_string_new(NULL);
    for (size_t gi = 0; gi < key_file->n_groups; gi++) {
        NsKeyGroup *g = key_file->groups[gi];
        if (g->name) ns_string_append_printf(s, "[%s]\n", g->name);
        for (size_t i = 0; i < g->n_pairs; i++) {
            NsKeyPair *p = g->pairs[i];
            if (p->key) ns_string_append_printf(s, "%s=%s\n", p->key, p->value);
            else ns_string_append_printf(s, "%s\n", p->value);
        }
    }
    if (length) *length = s->len;
    return ns_string_free(s, 0);
}

char **
ns_key_file_get_groups(NsKeyFile *key_file, size_t *length)
{
    char **v = ns_malloc_n(key_file->n_groups, sizeof *v);
    size_t n = 0;
    for (size_t i = 1; i < key_file->n_groups; i++)
        v[n++] = ns_strdup(key_file->groups[i]->name);
    v[n] = NULL;
    if (length) *length = n;
    return v;
}

static void
ns_key_file_group_not_found(NsError **error, const char *group_name)
{
    ns_set_error(error, NS_KEY_FILE_ERROR, NS_KEY_FILE_ERROR_GROUP_NOT_FOUND,
                 "Key file does not have group \xe2\x80\x9c%s\xe2\x80\x9d", group_name);
}

char **
ns_key_file_get_keys(NsKeyFile *key_file, const char *group_name, size_t *length,
                     NsError **error)
{
    NsKeyGroup *g = ns_key_file_lookup_group(key_file, group_name);
    if (!g) {
        ns_key_file_group_not_found(error, group_name);
        return NULL;
    }
    char **v = ns_malloc_n(g->n_pairs + 1, sizeof *v);
    size_t n = 0;
    for (size_t i = 0; i < g->n_pairs; i++)
        if (g->pairs[i]->key) v[n++] = ns_strdup(g->pairs[i]->key);
    v[n] = NULL;
    if (length) *length = n;
    return v;
}

int
ns_key_file_has_group(NsKeyFile *key_file, const char *group_name)
{
    return ns_key_file_lookup_group(key_file, group_name) != NULL;
}

char *
ns_key_file_get_value(NsKeyFile *key_file, const char *group_name, const char *key,
                      NsError **error)
{
    NsKeyGroup *g = ns_key_file_lookup_group(key_file, group_name);
    if (!g) {
        ns_key_file_group_not_found(error, group_name);
        return NULL;
    }
    NsKeyPair *p = ns_hash_table_lookup(g->lookup, key);
    if (!p) {
        ns_set_error(error, NS_KEY_FILE_ERROR, NS_KEY_FILE_ERROR_KEY_NOT_FOUND,
                     "Key file does not have key \xe2\x80\x9c%s\xe2\x80\x9d in group "
                     "\xe2\x80\x9c%s\xe2\x80\x9d", key, group_name);
        return NULL;
    }
    return ns_strdup(p->value);
}

void
ns_key_file_set_value(NsKeyFile *key_file, const char *group_name, const char *key,
                      const char *value)
{
    if (!key_file || !ns_key_file_is_group_name(group_name) || !key ||
        !ns_key_file_is_key_name(key, strlen(key)) || !value) {
        ns_log(NULL, NS_LOG_LEVEL_CRITICAL, "ns_key_file_set_value: assertion failed");
        return;
    }
    NsKeyGroup *g = ns_key_file_lookup_group(key_file, group_name);
    if (!g) {
        ns_key_file_add_group(key_file, group_name, 1);
        ns_key_file_add_key(key_file->current, key, value);
        return;
    }
    NsKeyPair *p = ns_hash_table_lookup(g->lookup, key);
    if (!p) {
        ns_key_file_add_key(g, key, value);
    } else {
        ns_free(p->value);
        p->value = ns_strdup(value);
    }
}

/* \s \n \t \r \\ back to what they stand for. */
static char *
ns_key_file_unescape(const char *value, NsError **error)
{
    char *out = ns_malloc(strlen(value) + 1);
    char *q = out;
    for (const char *p = value; *p; p++) {
        if (*p != '\\') {
            *q++ = *p;
            continue;
        }
        p++;
        switch (*p) {
        case 's': *q++ = ' '; break;
        case 'n': *q++ = '\n'; break;
        case 't': *q++ = '\t'; break;
        case 'r': *q++ = '\r'; break;
        case '\\': *q++ = '\\'; break;
        case '\0':
            ns_set_error_literal(error, NS_KEY_FILE_ERROR, NS_KEY_FILE_ERROR_INVALID_VALUE,
                                 "Key file contains escape character at end of line");
            ns_free(out);
            return NULL;
        default:
            ns_set_error(error, NS_KEY_FILE_ERROR, NS_KEY_FILE_ERROR_INVALID_VALUE,
                         "Key file contains invalid escape sequence "
                         "\xe2\x80\x9c\\%c\xe2\x80\x9d", *p);
            ns_free(out);
            return NULL;
        }
    }
    *q = '\0';
    return out;
}

char *
ns_key_file_get_string(NsKeyFile *key_file, const char *group_name, const char *key,
                       NsError **error)
{
    NsError *e = NULL;
    char *value = ns_key_file_get_value(key_file, group_name, key, &e);
    if (e) {
        ns_propagate_error(error, e);
        return NULL;
    }
    if (!ns_utf8_validate(value, -1, NULL)) {
        char *v = ns_utf8_make_valid(value, -1);
        ns_set_error(error, NS_KEY_FILE_ERROR, NS_KEY_FILE_ERROR_UNKNOWN_ENCODING,
                     "Key file contains key \xe2\x80\x9c%s\xe2\x80\x9d with value "
                     "\xe2\x80\x9c%s\xe2\x80\x9d which is not UTF-8", key, v);
        ns_free(v);
        ns_free(value);
        return NULL;
    }
    char *s = ns_key_file_unescape(value, &e);
    ns_free(value);
    if (e) {
        if (e->code == NS_KEY_FILE_ERROR_INVALID_VALUE) {
            ns_set_error(error, NS_KEY_FILE_ERROR, NS_KEY_FILE_ERROR_INVALID_VALUE,
                         "Key file contains key \xe2\x80\x9c%s\xe2\x80\x9d which has a "
                         "value that cannot be interpreted.", key);
            ns_error_free(e);
        } else {
            ns_propagate_error(error, e);
        }
    }
    return s;
}

/* Leading spaces and tabs as \s and \t, line breaks and backslashes
 * always escaped. */
static char *
ns_key_file_escape(const char *string)
{
    char *out = ns_malloc(strlen(string) * 2 + 1);
    char *q = out;
    int leading = 1;
    for (const char *p = string; *p; p++) {
        switch (*p) {
        case ' ':
            if (leading) { *q++ = '\\'; *q++ = 's'; }
            else *q++ = ' ';
            break;
        case '\t':
            if (leading) { *q++ = '\\'; *q++ = 't'; }
            else *q++ = '\t';
            break;
        case '\n': *q++ = '\\'; *q++ = 'n'; break;
        case '\r': *q++ = '\\'; *q++ = 'r'; break;
        case '\\': *q++ = '\\'; *q++ = '\\'; leading = 0; break;
        default: *q++ = *p; leading = 0; break;
        }
    }
    *q = '\0';
    return out;
}

void
ns_key_file_set_string(NsKeyFile *key_file, const char *group_name, const char *key,
                       const char *string)
{
    if (!key_file || !string) return;
    char *value = ns_key_file_escape(string);
    ns_key_file_set_value(key_file, group_name, key, value);
    ns_free(value);
}

int
ns_key_file_remove_group(NsKeyFile *key_file, const char *group_name, NsError **error)
{
    NsKeyGroup *g = group_name ? ns_key_file_lookup_group(key_file, group_name) : NULL;
    if (!g) {
        ns_key_file_group_not_found(error, group_name);
        return 0;
    }
    size_t at = 0;
    while (key_file->groups[at] != g) at++;
    ns_hash_table_remove(key_file->group_hash, g->name);
    memmove(key_file->groups + at, key_file->groups + at + 1,
            (key_file->n_groups - at - 1) * sizeof *key_file->groups);
    key_file->n_groups--;
    if (key_file->current == g)
        key_file->current = key_file->groups[key_file->n_groups - 1];
    if (key_file->start == g)
        key_file->start = key_file->n_groups > 1 ? key_file->groups[1] : NULL;
    ns_key_group_free(g);
    return 1;
}

int
ns_key_file_remove_key(NsKeyFile *key_file, const char *group_name, const char *key,
                       NsError **error)
{
    NsKeyGroup *g = ns_key_file_lookup_group(key_file, group_name);
    if (!g) {
        ns_key_file_group_not_found(error, group_name);
        return 0;
    }
    NsKeyPair *p = ns_hash_table_lookup(g->lookup, key);
    if (!p) {
        ns_set_error(error, NS_KEY_FILE_ERROR, NS_KEY_FILE_ERROR_KEY_NOT_FOUND,
                     "Key file does not have key \xe2\x80\x9c%s\xe2\x80\x9d in group "
                     "\xe2\x80\x9c%s\xe2\x80\x9d", key, group_name);
        return 0;
    }
    /* g_list_remove takes the first in list order: the last in file order. */
    size_t at = g->n_pairs;
    while (at > 0 && g->pairs[at - 1] != p) at--;
    ns_hash_table_remove(g->lookup, p->key);
    memmove(g->pairs + at - 1, g->pairs + at, (g->n_pairs - at) * sizeof *g->pairs);
    g->n_pairs--;
    ns_free(p->key);
    ns_free(p->value);
    ns_free(p);
    return 1;
}
