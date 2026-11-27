/* Weststar SDL Browser — NsCancellable: a flag that asks running work to stop.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "cancellable.h"

#include "plat/ns_sdl.h"

struct NsCancellable {
    SDL_AtomicInt ref;
    SDL_AtomicInt cancelled;
};

GQuark
ns_io_error_quark(void)
{
    return g_quark_from_static_string("ns-io-error-quark");
}

NsCancellable *
ns_cancellable_new(void)
{
    NsCancellable *cancellable = g_new0(NsCancellable, 1);
    SDL_SetAtomicInt(&cancellable->ref, 1);
    return cancellable;
}

NsCancellable *
ns_cancellable_ref(NsCancellable *cancellable)
{
    if (cancellable)
        SDL_AtomicIncRef(&cancellable->ref);
    return cancellable;
}

void
ns_cancellable_unref(NsCancellable *cancellable)
{
    if (cancellable && SDL_AtomicDecRef(&cancellable->ref))
        g_free(cancellable);
}

void
ns_cancellable_clear(NsCancellable **slot)
{
    NsCancellable *old = *slot;
    *slot = NULL;
    ns_cancellable_unref(old);
}

void
ns_cancellable_set(NsCancellable **slot, NsCancellable *cancellable)
{
    NsCancellable *old = *slot;
    if (old == cancellable)
        return;
    *slot = ns_cancellable_ref(cancellable);
    ns_cancellable_unref(old);
}

void
ns_cancellable_cancel(NsCancellable *cancellable)
{
    if (cancellable)
        SDL_SetAtomicInt(&cancellable->cancelled, 1);
}

gboolean
ns_cancellable_is_cancelled(NsCancellable *cancellable)
{
    return cancellable && SDL_GetAtomicInt(&cancellable->cancelled) != 0;
}

gboolean
ns_cancellable_set_error_if_cancelled(NsCancellable *cancellable, GError **error)
{
    if (!ns_cancellable_is_cancelled(cancellable))
        return FALSE;
    g_set_error_literal(error, NS_IO_ERROR, NS_IO_ERROR_CANCELLED,
                        "Operation was cancelled");
    return TRUE;
}
