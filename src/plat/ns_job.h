/* Weststar SDL Browser — background jobs on a pool of NsThread workers.
 *
 * ns_job_run(work, done, data) runs work(data) on a worker thread, then
 * done(data) on the loop of the thread that started the job (its pushed
 * thread-default NsLoop, else the default loop), from a source at
 * NS_PRIORITY_DEFAULT. done may be NULL.
 *
 * Workers start lazily, one whenever a job is queued and no worker is
 * free, up to NS_JOB_MAX_THREADS; past that, jobs wait in order. A worker
 * that has had nothing to do for NS_JOB_IDLE_SECS exits, which also runs
 * the destructors of its thread-local (NsPrivate) values. Jobs may block
 * (network fetches do, for seconds), which is why the pool is sized for
 * the fetch throttle rather than for the number of cores.
 *
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_JOB_H
#define NS_JOB_H

#define NS_JOB_MAX_THREADS 40
#define NS_JOB_IDLE_SECS   10

typedef void (*NsJobFunc)(void *data);

void ns_job_run(NsJobFunc work, NsJobFunc done, void *data);

#endif /* NS_JOB_H */
