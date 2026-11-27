/* Weststar SDL Browser — background jobs: a lazily grown pool of NsThread
 * workers taking jobs from one FIFO queue.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "plat/ns_job.h"
#include "plat/ns_loop.h"
#include "plat/ns_plat.h"

#include "plat/ns_sdl.h"
#include <stdlib.h>

typedef struct NsJob NsJob;
struct NsJob {
    NsJobFunc work;
    NsJobFunc done;
    void     *data;
    NsLoop   *loop;   /* where done runs */
    NsJob    *next;
};

static NsMutex g_jobs_lock;
static NsCond  g_jobs_cond;
static NsJob  *g_jobs_head, *g_jobs_tail;
static int     g_jobs_queued;
static int     g_threads;
static int     g_threads_idle;

static int
job_done_cb(void *data)
{
    NsJob *job = data;
    job->done(job->data);
    return NS_SOURCE_REMOVE;
}

static void
job_free(void *data)
{
    NsJob *job = data;
    ns_loop_unref(job->loop);
    SDL_free(job);
}

static void *
job_worker(void *unused)
{
    (void)unused;
    ns_mutex_lock(&g_jobs_lock);
    for (;;) {
        Sint64 idle_until = ns_time_monotonic_us() +
                            (Sint64)NS_JOB_IDLE_SECS * 1000000;
        while (!g_jobs_head) {
            g_threads_idle++;
            bool signalled = ns_cond_wait_until(&g_jobs_cond, &g_jobs_lock,
                                                idle_until);
            g_threads_idle--;
            if (!signalled && !g_jobs_head) {
                g_threads--;
                ns_mutex_unlock(&g_jobs_lock);
                return NULL;
            }
        }
        NsJob *job = g_jobs_head;
        g_jobs_head = job->next;
        if (!g_jobs_head) g_jobs_tail = NULL;
        g_jobs_queued--;
        ns_mutex_unlock(&g_jobs_lock);

        job->work(job->data);
        if (job->done)
            ns_loop_idle_add_full(job->loop, NS_PRIORITY_DEFAULT, job_done_cb,
                                  job, job_free);
        else
            job_free(job);

        ns_mutex_lock(&g_jobs_lock);
    }
}

void
ns_job_run(NsJobFunc work, NsJobFunc done, void *data)
{
    NsJob *job = SDL_calloc(1, sizeof *job);
    if (!job) {
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "ns_job: out of memory");
        abort();
    }
    job->work = work;
    job->done = done;
    job->data = data;
    job->loop = done ? ns_loop_ref_thread_default() : NULL;

    ns_mutex_lock(&g_jobs_lock);
    if (g_jobs_tail) g_jobs_tail->next = job;
    else g_jobs_head = job;
    g_jobs_tail = job;
    g_jobs_queued++;
    bool spawn = g_jobs_queued > g_threads_idle &&
                 g_threads < NS_JOB_MAX_THREADS;
    if (spawn) g_threads++;
    ns_cond_signal(&g_jobs_cond);
    ns_mutex_unlock(&g_jobs_lock);

    if (spawn) {
        NsThread *t = ns_thread_try_new("ns-job", job_worker, NULL, 0);
        if (!t) {
            ns_mutex_lock(&g_jobs_lock);
            bool last = g_threads == 1;
            if (!last) g_threads--;
            ns_mutex_unlock(&g_jobs_lock);
            /* With no worker at all nothing would run the queue: this one
             * aborts if the thread cannot be made either. */
            if (last) t = ns_thread_new("ns-job", job_worker, NULL);
        }
        if (t) ns_thread_unref(t);
    }
}
