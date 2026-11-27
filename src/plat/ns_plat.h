/* Weststar SDL Browser — the platform layer: locks, conditions, threads,
 * thread-local values, one-time initialisation and clocks, built on SDL3.
 *
 * It stands in for GLib's GMutex/GCond/GThread/GPrivate/GOnce and its clocks
 * with the same shapes, so call sites convert one for one:
 *
 * - NsMutex, NsRecMutex, NsCond and NsRWLock are valid when zero-filled,
 *   like their GLib counterparts (a static one needs no init call). The SDL
 *   object behind each is created on first use, with an atomic
 *   compare-and-swap; the _clear functions destroy it. An object that lives
 *   for the whole process may stay uncleared. SDL mutexes are recursive, so
 *   NsRecMutex is the same type as NsMutex under a name that says so.
 * - NsThread runs a void *(*)(void *) function and hands its result to
 *   ns_thread_join(); ns_thread_unref() detaches it instead.
 * - NsPrivate is a thread-local value with an optional destructor, run when
 *   a thread created by ns_thread_* (or SDL) exits. Threads made elsewhere
 *   (the main thread, or a library's own threads) do not run it.
 * - NsOnce guards a block that runs once:
 *       static NsOnce once;
 *       if (ns_once_enter(&once)) { ...; ns_once_leave(&once); }
 * - Times are in microseconds. The monotonic clock counts from an arbitrary
 *   point well before the program starts (the OS performance counter, as
 *   GLib's), never from zero, so 0 stays free as an "unset" value.
 *
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_PLAT_H
#define NS_PLAT_H

#include <stddef.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_atomic.h>
#include <SDL3/SDL_mutex.h>
#include <SDL3/SDL_thread.h>

/* ---- Mutexes ---- */

typedef struct NsMutex {
    void *m; /* SDL_Mutex *, created on first lock */
} NsMutex;

/* SDL mutexes may be locked again by the thread that holds them. */
typedef NsMutex NsRecMutex;

SDL_Mutex *ns_mutex_get_slow(NsMutex *mutex);

/* The SDL mutex behind mutex, creating it if this is the first use. */
static inline SDL_Mutex *
ns_mutex_get(NsMutex *mutex)
{
    SDL_Mutex *m = (SDL_Mutex *)SDL_GetAtomicPointer(&mutex->m);
    return m ? m : ns_mutex_get_slow(mutex);
}

/* Prepares a mutex in memory that is not zero-filled. */
static inline void ns_mutex_init(NsMutex *mutex) { mutex->m = NULL; }
static inline void ns_mutex_lock(NsMutex *mutex) { SDL_LockMutex(ns_mutex_get(mutex)); }
static inline void ns_mutex_unlock(NsMutex *mutex) { SDL_UnlockMutex(ns_mutex_get(mutex)); }
/* True when the lock was taken without waiting. */
static inline bool ns_mutex_trylock(NsMutex *mutex) { return SDL_TryLockMutex(ns_mutex_get(mutex)); }
/* Destroys the SDL mutex; the NsMutex is zero (usable) again afterwards. */
void ns_mutex_clear(NsMutex *mutex);

static inline void ns_rec_mutex_init(NsRecMutex *mutex) { ns_mutex_init(mutex); }
static inline void ns_rec_mutex_lock(NsRecMutex *mutex) { ns_mutex_lock(mutex); }
static inline void ns_rec_mutex_unlock(NsRecMutex *mutex) { ns_mutex_unlock(mutex); }
static inline void ns_rec_mutex_clear(NsRecMutex *mutex) { ns_mutex_clear(mutex); }

/* Unlocks the mutex a NS_MUTEX_AUTO_LOCK variable holds as it goes out of scope. */
static inline void
ns_mutex_auto_unlock(NsMutex **held)
{
    if (*held) ns_mutex_unlock(*held);
}

/* Locks mutex until the end of the enclosing block (GLib's GMutexLocker). */
#define NS_MUTEX_AUTO_LOCK(var, mutex)                                       \
    NsMutex *var __attribute__((cleanup(ns_mutex_auto_unlock), unused)) =   \
        (ns_mutex_lock(mutex), (mutex))

/* ---- Condition variables ---- */

typedef struct NsCond {
    void *c; /* SDL_Condition *, created on first use */
} NsCond;

SDL_Condition *ns_cond_get_slow(NsCond *cond);

static inline SDL_Condition *
ns_cond_get(NsCond *cond)
{
    SDL_Condition *c = (SDL_Condition *)SDL_GetAtomicPointer(&cond->c);
    return c ? c : ns_cond_get_slow(cond);
}

static inline void ns_cond_init(NsCond *cond) { cond->c = NULL; }
/* Waits for a signal; mutex must be locked (once) by the caller. */
static inline void
ns_cond_wait(NsCond *cond, NsMutex *mutex)
{
    SDL_WaitCondition(ns_cond_get(cond), ns_mutex_get(mutex));
}
/* Waits for a signal until the monotonic time end_us (see
 * ns_time_monotonic_us); false when that time has passed. Like
 * g_cond_wait_until, a true return may be a spurious wake-up. */
bool ns_cond_wait_until(NsCond *cond, NsMutex *mutex, Sint64 end_us);
static inline void ns_cond_signal(NsCond *cond) { SDL_SignalCondition(ns_cond_get(cond)); }
static inline void ns_cond_broadcast(NsCond *cond) { SDL_BroadcastCondition(ns_cond_get(cond)); }
void ns_cond_clear(NsCond *cond);

/* ---- Reader/writer locks ---- */

typedef struct NsRWLock {
    void *l; /* SDL_RWLock *, created on first use */
} NsRWLock;

SDL_RWLock *ns_rw_lock_get_slow(NsRWLock *lock);

static inline SDL_RWLock *
ns_rw_lock_get(NsRWLock *lock)
{
    SDL_RWLock *l = (SDL_RWLock *)SDL_GetAtomicPointer(&lock->l);
    return l ? l : ns_rw_lock_get_slow(lock);
}

static inline void ns_rw_lock_reader_lock(NsRWLock *lock) { SDL_LockRWLockForReading(ns_rw_lock_get(lock)); }
static inline void ns_rw_lock_reader_unlock(NsRWLock *lock) { SDL_UnlockRWLock(ns_rw_lock_get(lock)); }
static inline void ns_rw_lock_writer_lock(NsRWLock *lock) { SDL_LockRWLockForWriting(ns_rw_lock_get(lock)); }
static inline void ns_rw_lock_writer_unlock(NsRWLock *lock) { SDL_UnlockRWLock(ns_rw_lock_get(lock)); }
void ns_rw_lock_clear(NsRWLock *lock);

/* ---- Threads ---- */

typedef void *(*NsThreadFunc)(void *data);
typedef struct NsThread NsThread;

/* Starts fn(data) on a new thread named name; stack_bytes 0 is the system
 * default. NULL when the thread could not be created. The caller owns the
 * returned handle and must join or unref it. */
NsThread *ns_thread_try_new(const char *name, NsThreadFunc fn, void *data,
                            size_t stack_bytes);
/* ns_thread_try_new with the default stack that aborts on failure, as
 * g_thread_new does. */
NsThread *ns_thread_new(const char *name, NsThreadFunc fn, void *data);
/* Waits for the thread to finish, frees the handle and returns fn's result. */
void *ns_thread_join(NsThread *thread);
/* Detaches the thread: it is cleaned up when it finishes. */
void ns_thread_unref(NsThread *thread);
/* The calling thread's handle, or NULL on a thread not started by ns_thread_*. */
NsThread *ns_thread_self(void);

/* ---- Queues between threads ---- */

/* A queue any thread may push to and pop from (GAsyncQueue): what a
 * worker thread takes its commands from. Reference-counted, so that the
 * thread that made it and the thread that drains it can each let go. */
typedef struct NsAsyncQueue NsAsyncQueue;

NsAsyncQueue *ns_async_queue_new(void);
NsAsyncQueue *ns_async_queue_ref(NsAsyncQueue *queue);
/* Items still queued when the last reference goes are not freed. */
void ns_async_queue_unref(NsAsyncQueue *queue);
/* item must not be NULL. */
void ns_async_queue_push(NsAsyncQueue *queue, void *item);
/* Waits for an item. */
void *ns_async_queue_pop(NsAsyncQueue *queue);
/* The next item, or NULL when the queue is empty. */
void *ns_async_queue_try_pop(NsAsyncQueue *queue);
int ns_async_queue_length(NsAsyncQueue *queue);

/* ---- Thread-local values ---- */

typedef struct NsPrivate {
    SDL_TLSID                 id;
    SDL_TLSDestructorCallback destroy;
} NsPrivate;

/* Static initialiser naming the destructor (or NULL) of the thread's value. */
#define NS_PRIVATE_INIT(destroy_fn) { { 0 }, (destroy_fn) }

static inline void *ns_private_get(NsPrivate *key) { return SDL_GetTLS(&key->id); }
/* Sets this thread's value; the previous one is not destroyed. */
static inline void
ns_private_set(NsPrivate *key, void *value)
{
    (void)SDL_SetTLS(&key->id, value, key->destroy);
}
/* Sets this thread's value and destroys the previous one. */
void ns_private_replace(NsPrivate *key, void *value);

/* ---- One-time initialisation ---- */

typedef SDL_InitState NsOnce;

/* True for the one caller that should run the initialisation, which then
 * calls ns_once_leave(); other callers wait until it has. */
static inline bool ns_once_enter(NsOnce *once) { return SDL_ShouldInit(once); }
static inline void ns_once_leave(NsOnce *once) { SDL_SetInitialized(once, true); }

/* ---- Clocks ---- */

/* Microseconds on a clock that never goes back (g_get_monotonic_time). */
Sint64 ns_time_monotonic_us(void);
/* Microseconds since 1970-01-01 UTC (g_get_real_time). */
Sint64 ns_time_real_us(void);
/* Sleeps the calling thread for at least us microseconds (g_usleep). */
void ns_sleep_us(Sint64 us);

#endif /* NS_PLAT_H */
