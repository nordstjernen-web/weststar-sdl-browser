/* Weststar SDL Browser — web-text-lib's threads, locks and queues on SDL.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The text library is a fork of Pango and keeps Pango's spelling: GMutex,
 * g_mutex_lock, G_LOCK, GPrivate, GAsyncQueue. A source file that includes
 * this header, after its other headers, gets those names from the
 * browser's SDL layer (src/plat/ns_plat.h) instead of from GLib, so the
 * fork's text stays as upstream wrote it. None of these types appears in
 * a header of the library, so including this in some files and not in
 * others cannot disagree about a struct's layout.
 *
 * Not covered: g_once_init_enter and what GObject's own type system locks
 * with, which are GLib's as long as GObject is.
 */

#ifndef NS_GLIB_THREADS_H
#define NS_GLIB_THREADS_H

#include <glib.h>
#include "plat/ns_plat.h"

#define GMutex               NsMutex
#define g_mutex_init         ns_mutex_init
#define g_mutex_clear        ns_mutex_clear
#define g_mutex_lock         ns_mutex_lock
#define g_mutex_unlock       ns_mutex_unlock
#define g_mutex_trylock      ns_mutex_trylock

#define GCond                NsCond
#define g_cond_init          ns_cond_init
#define g_cond_clear         ns_cond_clear
#define g_cond_wait          ns_cond_wait
#define g_cond_signal        ns_cond_signal
#define g_cond_broadcast     ns_cond_broadcast

#define GRWLock                 NsRWLock
#define g_rw_lock_clear         ns_rw_lock_clear
#define g_rw_lock_reader_lock   ns_rw_lock_reader_lock
#define g_rw_lock_reader_unlock ns_rw_lock_reader_unlock
#define g_rw_lock_writer_lock   ns_rw_lock_writer_lock
#define g_rw_lock_writer_unlock ns_rw_lock_writer_unlock

/* A lock that is all zeroes is ready for use, as GLib's are. */
#undef G_LOCK_DEFINE_STATIC
#undef G_LOCK
#undef G_UNLOCK
#define G_LOCK_DEFINE_STATIC(name) static NsMutex ns_lock_##name
#define G_LOCK(name)               ns_mutex_lock (&ns_lock_##name)
#define G_UNLOCK(name)             ns_mutex_unlock (&ns_lock_##name)

#define GPrivate             NsPrivate
#undef G_PRIVATE_INIT
#define G_PRIVATE_INIT(notify) NS_PRIVATE_INIT (notify)
#define g_private_get        ns_private_get
#define g_private_set        ns_private_set
#define g_private_replace    ns_private_replace

#define GThread              NsThread
#define g_thread_new         ns_thread_new
#define g_thread_unref       ns_thread_unref

#define GAsyncQueue          NsAsyncQueue
#define g_async_queue_new    ns_async_queue_new
#define g_async_queue_ref    ns_async_queue_ref
#define g_async_queue_unref  ns_async_queue_unref
#define g_async_queue_push   ns_async_queue_push
#define g_async_queue_pop    ns_async_queue_pop

#endif /* NS_GLIB_THREADS_H */
