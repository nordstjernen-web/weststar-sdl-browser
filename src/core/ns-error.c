/* Weststar SDL Browser — nscore: errors and quarks.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ns-error.h"
#include "ns-hash.h"
#include "ns-log.h"
#include "ns-mem.h"
#include "ns-private.h"
#include "ns-strfuncs.h"

#include <errno.h>
#include <string.h>

/* ---- quarks ----------------------------------------------------------------- */

static NsSpinLock ns_quark_lock = NS_SPIN_LOCK_INIT;
static NsHashTable *ns_quark_table;   /* string -> quark */
static const char **ns_quark_strings; /* quark -> string; 0 is none */
static NsQuark ns_quark_count = 1;
static NsQuark ns_quark_cap;

static NsQuark
ns_quark_lookup_locked(const char *string)
{
    if (!ns_quark_table) return 0;
    return (NsQuark)(uintptr_t)ns_hash_table_lookup(ns_quark_table, string);
}

static NsQuark
ns_quark_from(const char *string, int copy)
{
    if (!string) return 0;
    ns_spin_lock(&ns_quark_lock);
    NsQuark q = ns_quark_lookup_locked(string);
    if (!q) {
        if (!ns_quark_table) ns_quark_table = ns_hash_table_new(ns_str_hash, ns_str_equal);
        if (ns_quark_count >= ns_quark_cap) {
            ns_quark_cap = ns_quark_cap ? ns_quark_cap * 2 : 512;
            ns_quark_strings = ns_realloc_n((void *)ns_quark_strings, ns_quark_cap,
                                            sizeof *ns_quark_strings);
            ns_quark_strings[0] = NULL;
        }
        const char *s = copy ? ns_strdup(string) : string;
        q = ns_quark_count++;
        ns_quark_strings[q] = s;
        ns_hash_table_insert(ns_quark_table, (void *)s, (void *)(uintptr_t)q);
    }
    ns_spin_unlock(&ns_quark_lock);
    return q;
}

NsQuark
ns_quark_from_string(const char *string)
{
    return ns_quark_from(string, 1);
}

NsQuark
ns_quark_from_static_string(const char *string)
{
    return ns_quark_from(string, 0);
}

NsQuark
ns_quark_try_string(const char *string)
{
    if (!string) return 0;
    ns_spin_lock(&ns_quark_lock);
    NsQuark q = ns_quark_lookup_locked(string);
    ns_spin_unlock(&ns_quark_lock);
    return q;
}

const char *
ns_quark_to_string(NsQuark quark)
{
    const char *s = NULL;
    ns_spin_lock(&ns_quark_lock);
    if (quark > 0 && quark < ns_quark_count) s = ns_quark_strings[quark];
    ns_spin_unlock(&ns_quark_lock);
    return s;
}

const char *
ns_intern_string(const char *string)
{
    return string ? ns_quark_to_string(ns_quark_from(string, 1)) : NULL;
}

const char *
ns_intern_static_string(const char *string)
{
    return string ? ns_quark_to_string(ns_quark_from(string, 0)) : NULL;
}

NsQuark
ns_file_error_quark(void)
{
    return ns_quark_from_static_string("g-file-error-quark");
}

NsQuark
ns_convert_error_quark(void)
{
    return ns_quark_from_static_string("g_convert_error");
}

NsFileError
ns_file_error_from_errno(int err_no)
{
    switch (err_no) {
#ifdef EEXIST
    case EEXIST: return NS_FILE_ERROR_EXIST;
#endif
#ifdef EISDIR
    case EISDIR: return NS_FILE_ERROR_ISDIR;
#endif
#ifdef EACCES
    case EACCES: return NS_FILE_ERROR_ACCES;
#endif
#ifdef ENAMETOOLONG
    case ENAMETOOLONG: return NS_FILE_ERROR_NAMETOOLONG;
#endif
#ifdef ENOENT
    case ENOENT: return NS_FILE_ERROR_NOENT;
#endif
#ifdef ENOTDIR
    case ENOTDIR: return NS_FILE_ERROR_NOTDIR;
#endif
#ifdef ENXIO
    case ENXIO: return NS_FILE_ERROR_NXIO;
#endif
#ifdef ENODEV
    case ENODEV: return NS_FILE_ERROR_NODEV;
#endif
#ifdef EROFS
    case EROFS: return NS_FILE_ERROR_ROFS;
#endif
#ifdef ETXTBSY
    case ETXTBSY: return NS_FILE_ERROR_TXTBSY;
#endif
#ifdef EFAULT
    case EFAULT: return NS_FILE_ERROR_FAULT;
#endif
#ifdef ELOOP
    case ELOOP: return NS_FILE_ERROR_LOOP;
#endif
#ifdef ENOSPC
    case ENOSPC: return NS_FILE_ERROR_NOSPC;
#endif
#ifdef ENOMEM
    case ENOMEM: return NS_FILE_ERROR_NOMEM;
#endif
#ifdef EMFILE
    case EMFILE: return NS_FILE_ERROR_MFILE;
#endif
#ifdef ENFILE
    case ENFILE: return NS_FILE_ERROR_NFILE;
#endif
#ifdef EBADF
    case EBADF: return NS_FILE_ERROR_BADF;
#endif
#ifdef EINVAL
    case EINVAL: return NS_FILE_ERROR_INVAL;
#endif
#ifdef EPIPE
    case EPIPE: return NS_FILE_ERROR_PIPE;
#endif
#ifdef EAGAIN
    case EAGAIN: return NS_FILE_ERROR_AGAIN;
#endif
#ifdef EINTR
    case EINTR: return NS_FILE_ERROR_INTR;
#endif
#ifdef EIO
    case EIO: return NS_FILE_ERROR_IO;
#endif
#ifdef EPERM
    case EPERM: return NS_FILE_ERROR_PERM;
#endif
#ifdef ENOSYS
    case ENOSYS: return NS_FILE_ERROR_NOSYS;
#endif
    default: return NS_FILE_ERROR_FAILED;
    }
}

/* ---- errors ------------------------------------------------------------------ */

NsError *
ns_error_new_literal(NsQuark domain, int code, const char *message)
{
    NsError *e = ns_malloc(sizeof *e);
    e->domain = domain;
    e->code = code;
    e->message = ns_strdup(message ? message : "");
    return e;
}

NsError *
ns_error_new_valist(NsQuark domain, int code, const char *format, va_list args)
{
    NsError *e = ns_malloc(sizeof *e);
    e->domain = domain;
    e->code = code;
    e->message = ns_strdup_vprintf(format, args);
    if (!e->message) e->message = ns_strdup("");
    return e;
}

NsError *
ns_error_new(NsQuark domain, int code, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    NsError *e = ns_error_new_valist(domain, code, format, ap);
    va_end(ap);
    return e;
}

void
ns_error_free(NsError *error)
{
    if (!error) return;
    ns_free(error->message);
    ns_free(error);
}

NsError *
ns_error_copy(const NsError *error)
{
    if (!error) return NULL;
    return ns_error_new_literal(error->domain, error->code, error->message);
}

int
ns_error_matches(const NsError *error, NsQuark domain, int code)
{
    return error && error->domain == domain && error->code == code;
}

static void
ns_error_overwrite_warning(const char *message)
{
    ns_log(NULL, NS_LOG_LEVEL_WARNING,
           "GError set over the top of a previous GError or uninitialized memory.\n"
           "This indicates a bug in someone's code. You must ensure an error is NULL "
           "before it's set.\nThe overwriting error message was: %s",
           message);
}

void
ns_set_error_literal(NsError **err, NsQuark domain, int code, const char *message)
{
    if (!err) return;
    if (*err) {
        ns_error_overwrite_warning(message);
        return;
    }
    *err = ns_error_new_literal(domain, code, message);
}

void
ns_set_error(NsError **err, NsQuark domain, int code, const char *format, ...)
{
    if (!err) return;
    va_list ap;
    va_start(ap, format);
    NsError *e = ns_error_new_valist(domain, code, format, ap);
    va_end(ap);
    if (*err) {
        ns_error_overwrite_warning(e->message);
        ns_error_free(e);
        return;
    }
    *err = e;
}

void
ns_propagate_error(NsError **dest, NsError *src)
{
    if (!src) return;
    if (!dest) {
        ns_error_free(src);
        return;
    }
    if (*dest) {
        ns_error_overwrite_warning(src->message);
        ns_error_free(src);
        return;
    }
    *dest = src;
}

void
ns_clear_error(NsError **err)
{
    if (err && *err) {
        ns_error_free(*err);
        *err = NULL;
    }
}
