/* Weststar SDL Browser — the platform layer's event loop on SDL3: sources
 * sorted by priority, a process-wide id registry, blocking waits on an
 * NsCond, and nested runs.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "plat/ns_loop.h"
#include "plat/ns_plat.h"

#include <SDL3/SDL_log.h>
#include <stdint.h>
#include <stdlib.h>

enum { NS_SRC_TIMEOUT, NS_SRC_IDLE, NS_SRC_CUSTOM };

typedef struct NsSource NsSource;
struct NsSource {
    unsigned             id;
    int                  kind;
    int                  priority;
    Sint64               interval_us;
    Sint64               due_us;
    NsSourceFunc         fn;
    const NsSourceFuncs *funcs;
    void                *data;
    NsDestroyNotify      notify;
    NsLoop              *loop;      /* not a reference: the loop holds us */
    SDL_AtomicInt        refs;
    bool                 destroyed; /* under the loop's lock */
    bool                 blocked;   /* its callback is running */
    NsSource            *prev, *next;
    NsSource            *hnext;     /* registry chain */
    NsSource            *dead_next;
};

struct NsLoop {
    SDL_AtomicInt refs;
    NsMutex       lock;
    NsCond        cond;
    NsSource     *head, *tail;   /* by priority, then in order added */
    int           n_sources;
    SDL_ThreadID  owner;
    int           owner_count;
    SDL_ThreadID  home;          /* the thread that last took ownership */
    bool          woken;
    void        (*wake_func)(void);
};

struct NsMainLoop {
    SDL_AtomicInt refs;
    SDL_AtomicInt running;
    NsLoop       *loop;
};

static void
ns_loop_oom(void)
{
    SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "ns_loop: out of memory");
    abort();
}

static void *
ns_loop_alloc(size_t size)
{
    void *p = SDL_calloc(1, size);
    if (!p) ns_loop_oom();
    return p;
}

/* ---- The id registry: id -> source, for removal without a loop ---- */

static NsMutex    g_reg_lock;
static NsSource **g_reg;
static unsigned   g_reg_cap;     /* a power of two */
static unsigned   g_reg_count;
static unsigned   g_reg_next = 1;

static NsSource *
reg_find_locked(unsigned id)
{
    if (!g_reg_cap) return NULL;
    for (NsSource *s = g_reg[id & (g_reg_cap - 1)]; s; s = s->hnext)
        if (s->id == id) return s;
    return NULL;
}

static void
reg_grow_locked(void)
{
    unsigned cap = g_reg_cap ? g_reg_cap * 2 : 256;
    NsSource **table = ns_loop_alloc(cap * sizeof *table);
    for (unsigned i = 0; i < g_reg_cap; i++) {
        NsSource *s = g_reg[i];
        while (s) {
            NsSource *next = s->hnext;
            s->hnext = table[s->id & (cap - 1)];
            table[s->id & (cap - 1)] = s;
            s = next;
        }
    }
    SDL_free(g_reg);
    g_reg = table;
    g_reg_cap = cap;
}

/* Gives s a fresh id and registers it. */
static unsigned
reg_insert(NsSource *s)
{
    ns_mutex_lock(&g_reg_lock);
    if (g_reg_count >= g_reg_cap) reg_grow_locked();
    unsigned id;
    do {
        id = g_reg_next++;
        if (!g_reg_next) g_reg_next = 1;
    } while (!id || reg_find_locked(id));
    s->id = id;
    NsSource **slot = &g_reg[id & (g_reg_cap - 1)];
    s->hnext = *slot;
    *slot = s;
    g_reg_count++;
    ns_mutex_unlock(&g_reg_lock);
    return id;
}

static void
reg_remove(NsSource *s)
{
    ns_mutex_lock(&g_reg_lock);
    if (g_reg_cap) {
        for (NsSource **p = &g_reg[s->id & (g_reg_cap - 1)]; *p;
             p = &(*p)->hnext) {
            if (*p == s) {
                *p = s->hnext;
                g_reg_count--;
                break;
            }
        }
    }
    ns_mutex_unlock(&g_reg_lock);
}

/* ---- Sources ---- */

static void
source_ref(NsSource *s)
{
    SDL_AddAtomicInt(&s->refs, 1);
}

/* Drops a reference; the last one runs the destroy notify. Never called
 * with a loop's lock held. */
static void
source_unref(NsSource *s)
{
    if (SDL_AddAtomicInt(&s->refs, -1) != 1) return;
    if (s->notify) s->notify(s->data);
    SDL_free(s);
}

/* Detaches s from its loop; the loop's reference moves to *dead, to be
 * dropped by bury() once the lock is released. */
static void
source_destroy_locked(NsLoop *loop, NsSource *s, NsSource **dead)
{
    if (s->destroyed) return;
    s->destroyed = true;
    if (s->prev) s->prev->next = s->next;
    else loop->head = s->next;
    if (s->next) s->next->prev = s->prev;
    else loop->tail = s->prev;
    s->prev = s->next = NULL;
    loop->n_sources--;
    reg_remove(s);
    s->dead_next = *dead;
    *dead = s;
}

static void
bury(NsSource *dead)
{
    while (dead) {
        NsSource *next = dead->dead_next;
        source_unref(dead);
        dead = next;
    }
}

/* ---- Thread state: the pushed loops and the dispatch depth ---- */

typedef struct {
    NsLoop **v;
    int      n, cap;
} ThreadDefaults;

static void
thread_defaults_free(void *p)
{
    ThreadDefaults *td = p;
    if (!td) return;
    SDL_free(td->v);
    SDL_free(td);
}

static NsPrivate g_thread_defaults = NS_PRIVATE_INIT(thread_defaults_free);
static NsPrivate g_depth = NS_PRIVATE_INIT(NULL);

int
ns_loop_depth(void)
{
    return (int)(intptr_t)ns_private_get(&g_depth);
}

static void
depth_add(int delta)
{
    ns_private_set(&g_depth, (void *)(intptr_t)(ns_loop_depth() + delta));
}

/* ---- Loops ---- */

NsLoop *
ns_loop_new(void)
{
    NsLoop *loop = ns_loop_alloc(sizeof *loop);
    SDL_SetAtomicInt(&loop->refs, 1);
    return loop;
}

NsLoop *
ns_loop_ref(NsLoop *loop)
{
    if (loop) SDL_AddAtomicInt(&loop->refs, 1);
    return loop;
}

/* A reference unless the loop is already being freed. */
static bool
loop_try_ref(NsLoop *loop)
{
    for (;;) {
        int n = SDL_GetAtomicInt(&loop->refs);
        if (n <= 0) return false;
        if (SDL_CompareAndSwapAtomicInt(&loop->refs, n, n + 1)) return true;
    }
}

void
ns_loop_unref(NsLoop *loop)
{
    if (!loop || SDL_AddAtomicInt(&loop->refs, -1) != 1) return;
    NsSource *dead = NULL;
    ns_mutex_lock(&loop->lock);
    while (loop->head) source_destroy_locked(loop, loop->head, &dead);
    ns_mutex_unlock(&loop->lock);
    bury(dead);
    ns_cond_clear(&loop->cond);
    ns_mutex_clear(&loop->lock);
    SDL_free(loop);
}

static NsLoop *g_default_loop;

NsLoop *
ns_loop_default(void)
{
    static NsOnce once;
    if (ns_once_enter(&once)) {
        g_default_loop = ns_loop_new();
        ns_once_leave(&once);
    }
    return g_default_loop;
}

static NsLoop *
loop_or_default(NsLoop *loop)
{
    return loop ? loop : ns_loop_default();
}

static void
loop_acquire_locked(NsLoop *loop)
{
    SDL_ThreadID self = SDL_GetCurrentThreadID();
    if (loop->owner == self) {
        loop->owner_count++;
    } else {
        loop->owner = self;
        loop->owner_count = 1;
    }
    loop->home = self;
}

static void
loop_release_locked(NsLoop *loop)
{
    if (loop->owner == SDL_GetCurrentThreadID() && --loop->owner_count <= 0) {
        loop->owner = 0;
        loop->owner_count = 0;
    }
}

NsLoop *
ns_loop_get_thread_default(void)
{
    ThreadDefaults *td = ns_private_get(&g_thread_defaults);
    return td && td->n ? td->v[td->n - 1] : NULL;
}

NsLoop *
ns_loop_ref_thread_default(void)
{
    NsLoop *loop = ns_loop_get_thread_default();
    return ns_loop_ref(loop ? loop : ns_loop_default());
}

void
ns_loop_push_thread_default(NsLoop *loop)
{
    loop = loop_or_default(loop);
    ThreadDefaults *td = ns_private_get(&g_thread_defaults);
    if (!td) {
        td = ns_loop_alloc(sizeof *td);
        ns_private_set(&g_thread_defaults, td);
    }
    if (td->n == td->cap) {
        int cap = td->cap ? td->cap * 2 : 4;
        NsLoop **v = SDL_realloc(td->v, (size_t)cap * sizeof *v);
        if (!v) ns_loop_oom();
        td->v = v;
        td->cap = cap;
    }
    td->v[td->n++] = ns_loop_ref(loop);
    ns_mutex_lock(&loop->lock);
    loop_acquire_locked(loop);
    ns_mutex_unlock(&loop->lock);
}

void
ns_loop_pop_thread_default(NsLoop *loop)
{
    loop = loop_or_default(loop);
    ThreadDefaults *td = ns_private_get(&g_thread_defaults);
    if (!td || !td->n || td->v[td->n - 1] != loop) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ns_loop_pop_thread_default: not the pushed loop");
        return;
    }
    td->n--;
    ns_mutex_lock(&loop->lock);
    loop_release_locked(loop);
    ns_mutex_unlock(&loop->lock);
    ns_loop_unref(loop);
}

void
ns_loop_set_wake_func(NsLoop *loop, void (*wake)(void))
{
    loop = loop_or_default(loop);
    ns_mutex_lock(&loop->lock);
    loop->wake_func = wake;
    ns_mutex_unlock(&loop->lock);
}

void
ns_loop_wakeup(NsLoop *loop)
{
    loop = loop_or_default(loop);
    ns_mutex_lock(&loop->lock);
    loop->woken = true;
    void (*wake)(void) = loop->wake_func;
    ns_cond_broadcast(&loop->cond);
    ns_mutex_unlock(&loop->lock);
    if (wake) wake();
}

/* ---- Adding sources ---- */

static unsigned
source_attach(NsLoop *loop, NsSource *s)
{
    loop = loop_or_default(loop);
    s->loop = loop;
    SDL_SetAtomicInt(&s->refs, 1);
    ns_mutex_lock(&loop->lock);
    /* After the last source of the same or a better priority. */
    NsSource *after = loop->tail;
    while (after && after->priority > s->priority) after = after->prev;
    s->prev = after;
    s->next = after ? after->next : loop->head;
    if (s->next) s->next->prev = s;
    else loop->tail = s;
    if (after) after->next = s;
    else loop->head = s;
    loop->n_sources++;
    unsigned id = reg_insert(s);
    /* The thread that runs the loop cannot be waiting on it while it adds
     * a source; any other thread may find it waiting. */
    SDL_ThreadID self = SDL_GetCurrentThreadID();
    bool wake = loop->owner != self && loop->home != self;
    ns_mutex_unlock(&loop->lock);
    /* s may be gone already: another thread can remove it by id. */
    if (wake) ns_loop_wakeup(loop);
    return id;
}

static NsSource *
source_new(int kind, int priority, NsSourceFunc fn, void *data,
           NsDestroyNotify notify)
{
    NsSource *s = ns_loop_alloc(sizeof *s);
    s->kind = kind;
    s->priority = priority;
    s->fn = fn;
    s->data = data;
    s->notify = notify;
    return s;
}

static unsigned
timeout_add_us(NsLoop *loop, int priority, Sint64 interval_us,
               NsSourceFunc fn, void *data, NsDestroyNotify notify)
{
    NsSource *s = source_new(NS_SRC_TIMEOUT, priority, fn, data, notify);
    s->interval_us = interval_us;
    s->due_us = ns_time_monotonic_us() + interval_us;
    return source_attach(loop, s);
}

unsigned
ns_loop_timeout_add_full(NsLoop *loop, int priority, unsigned ms,
                         NsSourceFunc fn, void *data, NsDestroyNotify notify)
{
    return timeout_add_us(loop, priority, (Sint64)ms * 1000, fn, data, notify);
}

unsigned
ns_loop_timeout_add(NsLoop *loop, unsigned ms, NsSourceFunc fn, void *data)
{
    return ns_loop_timeout_add_full(loop, NS_PRIORITY_DEFAULT, ms, fn, data,
                                    NULL);
}

unsigned
ns_loop_timeout_add_seconds(NsLoop *loop, unsigned seconds, NsSourceFunc fn,
                            void *data)
{
    return timeout_add_us(loop, NS_PRIORITY_DEFAULT,
                          (Sint64)seconds * 1000000, fn, data, NULL);
}

unsigned
ns_loop_idle_add_full(NsLoop *loop, int priority, NsSourceFunc fn, void *data,
                      NsDestroyNotify notify)
{
    return source_attach(loop, source_new(NS_SRC_IDLE, priority, fn, data,
                                          notify));
}

unsigned
ns_loop_idle_add(NsLoop *loop, NsSourceFunc fn, void *data)
{
    return ns_loop_idle_add_full(loop, NS_PRIORITY_DEFAULT_IDLE, fn, data,
                                 NULL);
}

unsigned
ns_loop_custom_add(NsLoop *loop, int priority, const NsSourceFuncs *funcs,
                   void *data, NsDestroyNotify notify)
{
    NsSource *s = source_new(NS_SRC_CUSTOM, priority, NULL, data, notify);
    s->funcs = funcs;
    return source_attach(loop, s);
}

bool
ns_loop_source_remove(unsigned id)
{
    if (!id) return false;
    NsLoop *loop = NULL;
    ns_mutex_lock(&g_reg_lock);
    NsSource *s = reg_find_locked(id);
    if (s && loop_try_ref(s->loop)) {
        loop = s->loop;
        source_ref(s);
    }
    ns_mutex_unlock(&g_reg_lock);
    if (!loop) return false;
    NsSource *dead = NULL;
    ns_mutex_lock(&loop->lock);
    bool removed = !s->destroyed;
    source_destroy_locked(loop, s, &dead);
    ns_mutex_unlock(&loop->lock);
    bury(dead);
    source_unref(s);
    ns_loop_unref(loop);
    return removed;
}

void
ns_loop_invoke_full(NsLoop *loop, int priority, NsSourceFunc fn, void *data,
                    NsDestroyNotify notify)
{
    loop = loop_or_default(loop);
    SDL_ThreadID self = SDL_GetCurrentThreadID();
    NsLoop *here = ns_loop_get_thread_default();
    ns_mutex_lock(&loop->lock);
    bool direct = loop->owner == self ||
                  (!loop->owner && loop == (here ? here : ns_loop_default()));
    if (direct) loop_acquire_locked(loop);
    ns_mutex_unlock(&loop->lock);
    if (!direct) {
        ns_loop_idle_add_full(loop, priority, fn, data, notify);
        return;
    }
    while (fn(data) != NS_SOURCE_REMOVE) {}
    if (notify) notify(data);
    ns_mutex_lock(&loop->lock);
    loop_release_locked(loop);
    ns_mutex_unlock(&loop->lock);
}

void
ns_loop_invoke(NsLoop *loop, NsSourceFunc fn, void *data)
{
    ns_loop_invoke_full(loop, NS_PRIORITY_DEFAULT, fn, data, NULL);
}

/* ---- Checking and dispatching ---- */

/* A growable array of sources, on the stack while small. */
typedef struct {
    NsSource **v;
    int        n, cap;
    NsSource  *small[32];
} SourceList;

static void
list_init(SourceList *l)
{
    l->v = l->small;
    l->n = 0;
    l->cap = (int)SDL_arraysize(l->small);
}

static void
list_push(SourceList *l, NsSource *s)
{
    if (l->n == l->cap) {
        int cap = l->cap * 2;
        NsSource **v = SDL_malloc((size_t)cap * sizeof *v);
        if (!v) ns_loop_oom();
        SDL_memcpy(v, l->v, (size_t)l->n * sizeof *v);
        if (l->v != l->small) SDL_free(l->v);
        l->v = v;
        l->cap = cap;
    }
    l->v[l->n++] = s;
}

/* Drops the references of a snapshot and frees its storage. */
static void
list_release(SourceList *l)
{
    for (int i = 0; i < l->n; i++) source_unref(l->v[i]);
    if (l->v != l->small) SDL_free(l->v);
    list_init(l);
}

static void
merge_timeout(int *timeout, Sint64 ms)
{
    if (ms < 0) ms = 0;
    if (ms > SDL_MAX_SINT32) ms = SDL_MAX_SINT32;
    if (*timeout < 0 || ms < *timeout) *timeout = (int)ms;
}

/* Fills snap with references to the sources that may run (not destroyed,
 * not running) and ready with those of them that are ready, of the best
 * priority only. Returns 0 when something is ready, else the milliseconds
 * until the next due time or -1. Clears the woken flag: whatever is added
 * after the snapshot sets it again. */
static int
loop_check(NsLoop *loop, SourceList *snap, SourceList *ready, Sint64 *now_out)
{
    ns_mutex_lock(&loop->lock);
    loop->woken = false;
    for (NsSource *s = loop->head; s; s = s->next) {
        if (s->blocked) continue;
        source_ref(s);
        list_push(snap, s);
    }
    ns_mutex_unlock(&loop->lock);

    Sint64 now = ns_time_monotonic_us();
    *now_out = now;
    int timeout = -1;
    bool have_best = false;
    int best = 0;
    for (int i = 0; i < snap->n; i++) {
        NsSource *s = snap->v[i];
        if (have_best && s->priority > best) break;
        bool is_ready = false;
        switch (s->kind) {
        case NS_SRC_IDLE:
            is_ready = true;
            break;
        case NS_SRC_TIMEOUT:
            if (s->due_us <= now) is_ready = true;
            else merge_timeout(&timeout, (s->due_us - now + 999) / 1000);
            break;
        default: {
            int t = -1;
            is_ready = s->funcs->prepare && s->funcs->prepare(s->data, &t);
            if (!is_ready && t >= 0) merge_timeout(&timeout, t);
            break;
        }
        }
        if (is_ready) {
            if (!have_best) {
                have_best = true;
                best = s->priority;
            }
            list_push(ready, s);
        }
    }
    return have_best ? 0 : timeout;
}

/* Runs each ready source not destroyed meanwhile; true when one ran. */
static bool
loop_dispatch(NsLoop *loop, SourceList *ready, Sint64 now)
{
    bool ran = false;
    for (int i = 0; i < ready->n; i++) {
        NsSource *s = ready->v[i];
        ns_mutex_lock(&loop->lock);
        bool skip = s->destroyed || s->blocked;
        if (!skip) s->blocked = true;
        ns_mutex_unlock(&loop->lock);
        if (skip) continue;
        depth_add(1);
        int keep = s->kind == NS_SRC_CUSTOM ? s->funcs->dispatch(s->data)
                                            : s->fn(s->data);
        depth_add(-1);
        ran = true;
        NsSource *dead = NULL;
        ns_mutex_lock(&loop->lock);
        s->blocked = false;
        if (!s->destroyed) {
            if (keep == NS_SOURCE_REMOVE)
                source_destroy_locked(loop, s, &dead);
            else if (s->kind == NS_SRC_TIMEOUT)
                s->due_us = now + s->interval_us;
        }
        ns_mutex_unlock(&loop->lock);
        bury(dead);
    }
    return ran;
}

/* One iteration; a blocking one does not wait once *running (when given)
 * is 0, so a quit from another thread between two iterations is not lost
 * to the woken flag being cleared. */
static bool
loop_iterate(NsLoop *loop, bool may_block, SDL_AtomicInt *running)
{
    loop = ns_loop_ref(loop_or_default(loop));
    ns_mutex_lock(&loop->lock);
    loop_acquire_locked(loop);
    ns_mutex_unlock(&loop->lock);

    SourceList snap, ready;
    list_init(&snap);
    list_init(&ready);
    Sint64 now = 0;
    int timeout = loop_check(loop, &snap, &ready, &now);
    if (!ready.n && may_block) {
        ns_mutex_lock(&loop->lock);
        if (!loop->woken && (!running || SDL_GetAtomicInt(running))) {
            if (timeout < 0)
                ns_cond_wait(&loop->cond, &loop->lock);
            else
                ns_cond_wait_until(&loop->cond, &loop->lock,
                                   now + (Sint64)timeout * 1000);
        }
        ns_mutex_unlock(&loop->lock);
        list_release(&snap);
        ready.n = 0;
        loop_check(loop, &snap, &ready, &now);
    }
    bool ran = loop_dispatch(loop, &ready, now);
    if (ready.v != ready.small) SDL_free(ready.v);
    list_release(&snap);

    ns_mutex_lock(&loop->lock);
    loop_release_locked(loop);
    ns_mutex_unlock(&loop->lock);
    ns_loop_unref(loop);
    return ran;
}

bool
ns_loop_iterate(NsLoop *loop, bool may_block)
{
    return loop_iterate(loop, may_block, NULL);
}

/* Checks without running anything; the next due time as loop_check. */
static int
loop_peek(NsLoop *loop)
{
    loop = loop_or_default(loop);
    SourceList snap, ready;
    list_init(&snap);
    list_init(&ready);
    Sint64 now = 0;
    int timeout = loop_check(loop, &snap, &ready, &now);
    if (ready.v != ready.small) SDL_free(ready.v);
    list_release(&snap);
    return timeout;
}

bool
ns_loop_pending(NsLoop *loop)
{
    return loop_peek(loop) == 0;
}

int
ns_loop_next_timeout_ms(NsLoop *loop)
{
    return loop_peek(loop);
}

/* ---- Runs ---- */

NsMainLoop *
ns_main_loop_new(NsLoop *loop)
{
    NsMainLoop *ml = ns_loop_alloc(sizeof *ml);
    SDL_SetAtomicInt(&ml->refs, 1);
    ml->loop = ns_loop_ref(loop_or_default(loop));
    return ml;
}

NsMainLoop *
ns_main_loop_ref(NsMainLoop *ml)
{
    if (ml) SDL_AddAtomicInt(&ml->refs, 1);
    return ml;
}

void
ns_main_loop_unref(NsMainLoop *ml)
{
    if (!ml || SDL_AddAtomicInt(&ml->refs, -1) != 1) return;
    ns_loop_unref(ml->loop);
    SDL_free(ml);
}

void
ns_main_loop_run(NsMainLoop *ml)
{
    ns_main_loop_ref(ml);
    NsLoop *loop = ml->loop;
    ns_mutex_lock(&loop->lock);
    loop_acquire_locked(loop);
    ns_mutex_unlock(&loop->lock);
    SDL_SetAtomicInt(&ml->running, 1);
    while (SDL_GetAtomicInt(&ml->running))
        loop_iterate(loop, true, &ml->running);
    ns_mutex_lock(&loop->lock);
    loop_release_locked(loop);
    ns_mutex_unlock(&loop->lock);
    ns_main_loop_unref(ml);
}

void
ns_main_loop_quit(NsMainLoop *ml)
{
    if (!ml) return;
    SDL_SetAtomicInt(&ml->running, 0);
    ns_loop_wakeup(ml->loop);
}

bool
ns_main_loop_is_running(NsMainLoop *ml)
{
    return ml && SDL_GetAtomicInt(&ml->running);
}

NsLoop *
ns_main_loop_get_loop(NsMainLoop *ml)
{
    return ml ? ml->loop : NULL;
}
