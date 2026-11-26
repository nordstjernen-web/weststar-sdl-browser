/* Weststar SDL Browser — the thread the page engine runs on, with its own NsLoop.
 * Started on first post with an 8 MiB stack; runs queued jobs in order outside nested iterations.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "enginethread.h"
#include "plat/ns_plat.h"
#include "audio/audio.h"
#include "mainctx.h"
#include "security.h"
#include "trace.h"

#define NS_ENGINE_STACK_BYTES ((size_t)8 * 1024 * 1024)

typedef struct {
    NsEngineJob fn;
    gpointer    data;
} Job;

static NsLoop      *g_loop;
static GAsyncQueue *g_jobs;

/* Custom source prepare: ready when jobs are queued and the loop is not
 * inside a nested iteration (a job never runs inside another callback). */
static int
jobs_prepare(void *data, int *timeout_ms)
{
    (void)data;
    (void)timeout_ms;
    return ns_loop_depth() == 0 && g_async_queue_length(g_jobs) > 0;
}

/* Runs every queued engine job in order. */
static int
jobs_dispatch(void *data)
{
    (void)data;
    Job *job;
    while ((job = g_async_queue_try_pop(g_jobs))) {
        job->fn(job->data);
        g_free(job);
    }
    return NS_SOURCE_CONTINUE;
}

static const NsSourceFuncs jobs_funcs = {
    .prepare = jobs_prepare,
    .dispatch = jobs_dispatch,
};

/* Engine thread body: W^X setup, trace name, job source and the run on its loop. */
static gpointer
engine_main(gpointer data)
{
    (void)data;
    ns_security_deny_exec_memory(FALSE, FALSE);
    ns_loop_push_thread_default(g_loop);
    ns_engine_context_set(g_loop);
    ns_trace_thread_name("ns-engine");
    ns_loop_custom_add(g_loop, NS_PRIORITY_DEFAULT, &jobs_funcs, NULL, NULL);
    NsMainLoop *run = ns_main_loop_new(g_loop);
    ns_main_loop_run(run);
    ns_main_loop_unref(run);
    ns_loop_pop_thread_default(g_loop);
    return NULL;
}

/* Starts the detached engine thread (named by SDL) with an 8 MiB stack, else the default one. */
static void
engine_spawn(void)
{
    /* The engine thread may not create UNIX sockets, so the audio device
     * is opened by a helper started from this unconfined thread. */
    ns_audio_start_device_helper();
    NsThread *t = ns_thread_try_new("ns-engine", engine_main, NULL,
                                    NS_ENGINE_STACK_BYTES);
    if (!t) t = ns_thread_new("ns-engine", engine_main, NULL);
    ns_thread_unref(t);
}

/* Creates the engine loop and job queue and spawns the thread exactly once. */
static void
engine_start(void)
{
    static NsOnce started;
    if (ns_once_enter(&started)) {
        g_loop = ns_loop_new();
        g_jobs = g_async_queue_new();
        engine_spawn();
        ns_once_leave(&started);
    }
}

/* Queues fn(data) to run on the engine thread, starting it on first use. */
void
ns_engine_thread_post(NsEngineJob fn, gpointer data)
{
    engine_start();
    Job *job = g_new0(Job, 1);
    job->fn = fn;
    job->data = data;
    g_async_queue_push(g_jobs, job);
    ns_loop_wakeup(g_loop);
}
