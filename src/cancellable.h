/* Weststar SDL Browser — NsCancellable: a flag that asks running work to stop.
 * Takes the place of GIO's GCancellable, which was all the browser used GIO's
 * object system for: a reference-counted flag, set once from any thread and
 * polled by the work it concerns.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef NS_CANCELLABLE_H
#define NS_CANCELLABLE_H

#include <glib.h>

typedef struct NsCancellable NsCancellable;

/* The errors of cancelled and unsupported operations, as GIO numbered them. */
#define NS_IO_ERROR (ns_io_error_quark())
enum {
    NS_IO_ERROR_NOT_SUPPORTED = 15,
    NS_IO_ERROR_CANCELLED = 19,
};
GQuark ns_io_error_quark(void);

NsCancellable *ns_cancellable_new(void);
NsCancellable *ns_cancellable_ref(NsCancellable *cancellable);
void ns_cancellable_unref(NsCancellable *cancellable);

/* Drops the reference in *slot, if any, and empties it. */
void ns_cancellable_clear(NsCancellable **slot);
/* Makes *slot refer to `cancellable` (which may be NULL). */
void ns_cancellable_set(NsCancellable **slot, NsCancellable *cancellable);

/* All of these accept NULL: no cancellable is never cancelled. */
void ns_cancellable_cancel(NsCancellable *cancellable);
gboolean ns_cancellable_is_cancelled(NsCancellable *cancellable);
/* Sets *error to NS_IO_ERROR_CANCELLED and returns TRUE when cancelled. */
gboolean ns_cancellable_set_error_if_cancelled(NsCancellable *cancellable,
                                               GError **error);

#endif
