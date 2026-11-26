/* Weststar SDL Browser — the platform layer on SDL3: lazy SDL locks,
 * threads with pointer results, thread-local values and clocks.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "plat/ns_plat.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_time.h>
#include <SDL3/SDL_timer.h>
#include <stdlib.h>

/* An SDL lock that cannot be created leaves nothing to lock with: like
 * GLib, give up rather than run unlocked. */
static void
ns_plat_fatal(const char *what)
{
    SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "%s: %s", what,
                    SDL_GetError());
    abort();
}

/* Installs fresh in *slot unless another thread got there first; returns the
 * object in the slot and destroys fresh if it lost. */
static void *
ns_plat_install(void **slot, void *fresh, void (*destroy)(void *))
{
    if (SDL_CompareAndSwapAtomicPointer(slot, NULL, fresh))
        return fresh;
    destroy(fresh);
    return SDL_GetAtomicPointer(slot);
}

static void ns_plat_destroy_mutex(void *p) { SDL_DestroyMutex(p); }
static void ns_plat_destroy_cond(void *p) { SDL_DestroyCondition(p); }
static void ns_plat_destroy_rwlock(void *p) { SDL_DestroyRWLock(p); }

SDL_Mutex *
ns_mutex_get_slow(NsMutex *mutex)
{
    SDL_Mutex *m = SDL_CreateMutex();
    if (!m) ns_plat_fatal("SDL_CreateMutex");
    return ns_plat_install(&mutex->m, m, ns_plat_destroy_mutex);
}

void
ns_mutex_clear(NsMutex *mutex)
{
    SDL_Mutex *m = SDL_SetAtomicPointer(&mutex->m, NULL);
    if (m) SDL_DestroyMutex(m);
}

SDL_Condition *
ns_cond_get_slow(NsCond *cond)
{
    SDL_Condition *c = SDL_CreateCondition();
    if (!c) ns_plat_fatal("SDL_CreateCondition");
    return ns_plat_install(&cond->c, c, ns_plat_destroy_cond);
}

bool
ns_cond_wait_until(NsCond *cond, NsMutex *mutex, Sint64 end_us)
{
    Sint64 left = end_us - ns_time_monotonic_us();
    if (left <= 0) return false;
    /* Rounded up, so a timeout means end_us has passed. */
    Sint64 ms = (left + 999) / 1000;
    if (ms > SDL_MAX_SINT32) ms = SDL_MAX_SINT32;
    return SDL_WaitConditionTimeout(ns_cond_get(cond), ns_mutex_get(mutex),
                                    (Sint32)ms);
}

void
ns_cond_clear(NsCond *cond)
{
    SDL_Condition *c = SDL_SetAtomicPointer(&cond->c, NULL);
    if (c) SDL_DestroyCondition(c);
}

SDL_RWLock *
ns_rw_lock_get_slow(NsRWLock *lock)
{
    SDL_RWLock *l = SDL_CreateRWLock();
    if (!l) ns_plat_fatal("SDL_CreateRWLock");
    return ns_plat_install(&lock->l, l, ns_plat_destroy_rwlock);
}

void
ns_rw_lock_clear(NsRWLock *lock)
{
    SDL_RWLock *l = SDL_SetAtomicPointer(&lock->l, NULL);
    if (l) SDL_DestroyRWLock(l);
}

/* A thread handle, shared by the thread itself and its creator until both
 * are done with it: the thread drops its reference when fn returns, the
 * creator when it joins or detaches. */
struct NsThread {
    SDL_Thread  *thread;
    NsThreadFunc fn;
    void        *data;
    void        *result;
    SDL_AtomicInt refs;
};

static SDL_TLSID ns_thread_self_key;

static void
ns_thread_release(NsThread *t)
{
    if (SDL_AddAtomicInt(&t->refs, -1) == 1)
        SDL_free(t);
}

static int SDLCALL
ns_thread_main(void *data)
{
    NsThread *t = data;
    (void)SDL_SetTLS(&ns_thread_self_key, t, NULL);
    t->result = t->fn(t->data);
    (void)SDL_SetTLS(&ns_thread_self_key, NULL, NULL);
    ns_thread_release(t);
    return 0;
}

NsThread *
ns_thread_try_new(const char *name, NsThreadFunc fn, void *data,
                  size_t stack_bytes)
{
    NsThread *t = SDL_calloc(1, sizeof *t);
    if (!t) return NULL;
    t->fn = fn;
    t->data = data;
    SDL_SetAtomicInt(&t->refs, 2);
    if (stack_bytes) {
        SDL_PropertiesID props = SDL_CreateProperties();
        if (props) {
            SDL_SetPointerProperty(props,
                                   SDL_PROP_THREAD_CREATE_ENTRY_FUNCTION_POINTER,
                                   (void *)ns_thread_main);
            SDL_SetStringProperty(props, SDL_PROP_THREAD_CREATE_NAME_STRING,
                                  name);
            SDL_SetPointerProperty(props,
                                   SDL_PROP_THREAD_CREATE_USERDATA_POINTER, t);
            SDL_SetNumberProperty(props, SDL_PROP_THREAD_CREATE_STACKSIZE_NUMBER,
                                  (Sint64)stack_bytes);
            t->thread = SDL_CreateThreadWithProperties(props);
            SDL_DestroyProperties(props);
        }
    } else {
        t->thread = SDL_CreateThread(ns_thread_main, name, t);
    }
    if (!t->thread) {
        SDL_free(t);
        return NULL;
    }
    return t;
}

NsThread *
ns_thread_new(const char *name, NsThreadFunc fn, void *data)
{
    NsThread *t = ns_thread_try_new(name, fn, data, 0);
    if (!t) ns_plat_fatal("SDL_CreateThread");
    return t;
}

void *
ns_thread_join(NsThread *thread)
{
    SDL_WaitThread(thread->thread, NULL);
    void *result = thread->result;
    ns_thread_release(thread);
    return result;
}

void
ns_thread_unref(NsThread *thread)
{
    SDL_DetachThread(thread->thread);
    ns_thread_release(thread);
}

NsThread *
ns_thread_self(void)
{
    return SDL_GetTLS(&ns_thread_self_key);
}

void
ns_private_replace(NsPrivate *key, void *value)
{
    void *old = SDL_GetTLS(&key->id);
    (void)SDL_SetTLS(&key->id, value, key->destroy);
    if (old && old != value && key->destroy)
        key->destroy(old);
}

Sint64
ns_time_monotonic_us(void)
{
    /* The performance counter rather than SDL_GetTicksNS(): the ticks start
     * at zero when SDL first reads them (and again after SDL_Quit), and the
     * engine uses 0 as "no time yet". Split to keep counter * 10^6 in range. */
    Uint64 count = SDL_GetPerformanceCounter();
    Uint64 freq = SDL_GetPerformanceFrequency();
    if (!freq) return 0;
    return (Sint64)((count / freq) * 1000000u +
                    (count % freq) * 1000000u / freq);
}

Sint64
ns_time_real_us(void)
{
    SDL_Time ns = 0;
    if (!SDL_GetCurrentTime(&ns)) return 0;
    return (Sint64)(ns / 1000);
}

void
ns_sleep_us(Sint64 us)
{
    if (us > 0) SDL_DelayNS((Uint64)us * 1000u);
}
