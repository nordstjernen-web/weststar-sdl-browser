/* Weststar SDL Browser — the platform layer's event loop: NsLoop.
 *
 * An NsLoop holds sources (timeouts, idles and custom sources) and runs
 * their callbacks on the thread that iterates it. It takes the place of
 * GLib's GMainContext, with the same rules, so call sites convert one for
 * one:
 *
 * - Each source has an id (never 0), unique in the process, so
 *   ns_loop_source_remove(id) needs no loop. A callback returns
 *   NS_SOURCE_CONTINUE to run again (a timeout re-arms from the time of the
 *   iteration that ran it) or NS_SOURCE_REMOVE to be removed. A destroy
 *   notify given with the source runs once it is gone and not running.
 * - Every iteration runs the ready sources of the best (lowest) priority
 *   only. Timeouts default to NS_PRIORITY_DEFAULT, idles to
 *   NS_PRIORITY_DEFAULT_IDLE, so idles run when nothing else is ready.
 * - A source whose callback is running is not run again by a nested
 *   iteration from inside that callback; other sources, timers included,
 *   are.
 * - Any thread may add or remove sources; a loop waiting on another thread
 *   is woken (its condition variable, plus the wake function the main
 *   thread sets so that SDL's event wait ends too). ns_loop_invoke() runs
 *   a function on the loop's thread, at once when called there.
 * - A thread can push a loop as its thread default (GLib's
 *   push_thread_default): ns_loop_ref_thread_default() is what "the loop
 *   of the current thread" means for code that delivers results later,
 *   such as background jobs. Without one pushed it is the default loop,
 *   the main thread's.
 * - NULL as a loop argument means the default loop.
 *
 * NsMainLoop runs an NsLoop until quit (GLib's GMainLoop). Runs nest: a
 * callback may run another NsMainLoop on the same NsLoop, and quitting a
 * NsMainLoop ends only its own run, so a timer quitting an outer wait from
 * inside a nested one leaves the nested one running.
 *
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_LOOP_H
#define NS_LOOP_H

#include <stdbool.h>

#define NS_PRIORITY_HIGH         (-100)
#define NS_PRIORITY_DEFAULT      0
#define NS_PRIORITY_HIGH_IDLE    100
#define NS_PRIORITY_DEFAULT_IDLE 200
#define NS_PRIORITY_LOW          300

#define NS_SOURCE_REMOVE   0
#define NS_SOURCE_CONTINUE 1

typedef struct NsLoop     NsLoop;
typedef struct NsMainLoop NsMainLoop;

/* Same shapes as GSourceFunc and GDestroyNotify. */
typedef int  (*NsSourceFunc)(void *data);
typedef void (*NsDestroyNotify)(void *data);

/* A custom source: prepare says whether it is ready now and, if not, may
 * set *timeout_ms (preset to -1) to when it should be asked again;
 * dispatch runs it and returns NS_SOURCE_CONTINUE or NS_SOURCE_REMOVE.
 * prepare runs without the loop's lock, on the iterating thread. */
typedef struct NsSourceFuncs {
    int (*prepare)(void *data, int *timeout_ms);
    int (*dispatch)(void *data);
} NsSourceFuncs;

/* ---- Loops ---- */

NsLoop *ns_loop_new(void);
NsLoop *ns_loop_ref(NsLoop *loop);
void    ns_loop_unref(NsLoop *loop);
/* The main thread's loop, created on first use. */
NsLoop *ns_loop_default(void);

/* The loop pushed on this thread, or NULL (g_main_context_get_thread_default). */
NsLoop *ns_loop_get_thread_default(void);
/* The pushed loop, else the default loop, with a reference for the caller. */
NsLoop *ns_loop_ref_thread_default(void);
/* Makes loop this thread's default until the matching pop; the thread
 * owns the loop meanwhile (ns_loop_invoke runs directly there). */
void    ns_loop_push_thread_default(NsLoop *loop);
void    ns_loop_pop_thread_default(NsLoop *loop);

/* ---- Sources ---- */

unsigned ns_loop_timeout_add(NsLoop *loop, unsigned ms, NsSourceFunc fn,
                             void *data);
unsigned ns_loop_timeout_add_full(NsLoop *loop, int priority, unsigned ms,
                                  NsSourceFunc fn, void *data,
                                  NsDestroyNotify notify);
unsigned ns_loop_timeout_add_seconds(NsLoop *loop, unsigned seconds,
                                     NsSourceFunc fn, void *data);
unsigned ns_loop_idle_add(NsLoop *loop, NsSourceFunc fn, void *data);
unsigned ns_loop_idle_add_full(NsLoop *loop, int priority, NsSourceFunc fn,
                               void *data, NsDestroyNotify notify);
unsigned ns_loop_custom_add(NsLoop *loop, int priority,
                            const NsSourceFuncs *funcs, void *data,
                            NsDestroyNotify notify);
/* Removes a source of any loop; false when the id is not (or no longer)
 * attached. */
bool     ns_loop_source_remove(unsigned id);

/* Calls fn(data) on loop's thread: at once (repeating while it returns
 * NS_SOURCE_CONTINUE) when this thread owns loop, else from an idle source
 * at priority. notify(data) runs when it is done. */
void ns_loop_invoke(NsLoop *loop, NsSourceFunc fn, void *data);
void ns_loop_invoke_full(NsLoop *loop, int priority, NsSourceFunc fn,
                         void *data, NsDestroyNotify notify);

/* ---- Running ---- */

/* Runs the ready sources of the best priority once; with may_block, first
 * waits until one is ready or the loop is woken. True when a callback ran. */
bool ns_loop_iterate(NsLoop *loop, bool may_block);
/* True when a source is ready to run. */
bool ns_loop_pending(NsLoop *loop);
/* Milliseconds until a source is due: 0 when one is ready, -1 when none
 * has a time. For a caller that sleeps elsewhere (the SDL event wait). */
int  ns_loop_next_timeout_ms(NsLoop *loop);
/* Makes a blocking iteration of loop return. */
void ns_loop_wakeup(NsLoop *loop);
/* Also called by ns_loop_wakeup and by any cross-thread addition: the main
 * thread waits in SDL rather than on the loop. */
void ns_loop_set_wake_func(NsLoop *loop, void (*wake)(void));
/* How many source callbacks are running on this thread (g_main_depth). */
int  ns_loop_depth(void);

/* ---- Runs ---- */

NsMainLoop *ns_main_loop_new(NsLoop *loop);
NsMainLoop *ns_main_loop_ref(NsMainLoop *ml);
void        ns_main_loop_unref(NsMainLoop *ml);
/* Iterates the loop, blocking, until ns_main_loop_quit (as GLib, a quit
 * before the run starts is forgotten). */
void        ns_main_loop_run(NsMainLoop *ml);
/* Ends the run; safe from any thread. */
void        ns_main_loop_quit(NsMainLoop *ml);
bool        ns_main_loop_is_running(NsMainLoop *ml);
NsLoop     *ns_main_loop_get_loop(NsMainLoop *ml);

#endif /* NS_LOOP_H */
