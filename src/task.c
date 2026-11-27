/* Weststar SDL Browser — NsTask: GTask's shape on NsLoop and NsJob.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "task.h"
#include "plat/ns_job.h"
#include "plat/ns_loop.h"

struct NsTask {
    gint                refs;
    NsLoop             *loop;
    NsCancellable       *cancellable;
    NsTaskReadyCallback callback;
    gpointer            user_data;
    gpointer            task_data;
    GDestroyNotify      task_data_destroy;
    gpointer            source_tag;
    NsTaskThreadFunc    thread_func;
    gboolean            threaded;
    gboolean            returned;
    gpointer            result;
    GDestroyNotify      result_destroy;
    gboolean            result_taken;
    gboolean            result_boolean;
    GError             *error;
};

NsTask *
ns_task_new(NsCancellable *cancellable, NsTaskReadyCallback callback,
            gpointer user_data)
{
    NsTask *task = g_new0(NsTask, 1);
    task->refs = 1;
    task->loop = ns_loop_ref_thread_default();
    task->cancellable = ns_cancellable_ref(cancellable);
    task->callback = callback;
    task->user_data = user_data;
    return task;
}

NsTask *
ns_task_ref(NsTask *task)
{
    if (task) g_atomic_int_inc(&task->refs);
    return task;
}

void
ns_task_unref(NsTask *task)
{
    if (!task || !g_atomic_int_dec_and_test(&task->refs)) return;
    if (task->result && !task->result_taken && task->result_destroy)
        task->result_destroy(task->result);
    g_clear_error(&task->error);
    if (task->task_data_destroy) task->task_data_destroy(task->task_data);
    ns_cancellable_clear(&task->cancellable);
    ns_loop_unref(task->loop);
    g_free(task);
}

void
ns_task_set_task_data(NsTask *task, gpointer task_data,
                      GDestroyNotify task_data_destroy)
{
    if (task->task_data_destroy) task->task_data_destroy(task->task_data);
    task->task_data = task_data;
    task->task_data_destroy = task_data_destroy;
}

gpointer
ns_task_get_task_data(NsTask *task)
{
    return task->task_data;
}

void
ns_task_set_source_tag(NsTask *task, gpointer source_tag)
{
    task->source_tag = source_tag;
}

gpointer
ns_task_get_source_tag(NsTask *task)
{
    return task->source_tag;
}

NsCancellable *
ns_task_get_cancellable(NsTask *task)
{
    return task->cancellable;
}

static int
task_deliver_cb(gpointer data)
{
    NsTask *task = data;
    task->callback(task, task->user_data);
    return NS_SOURCE_REMOVE;
}

/* Queues the callback on the task's loop. */
static void
task_deliver(NsTask *task)
{
    if (!task->callback) return;
    ns_loop_idle_add_full(task->loop, NS_PRIORITY_DEFAULT, task_deliver_cb,
                          ns_task_ref(task), (GDestroyNotify)ns_task_unref);
}

/* Records that a value was returned and, unless a thread function is still
 * running, delivers it. */
static void
task_returned(NsTask *task)
{
    if (task->returned) {
        g_warning("ns_task: returned twice");
        return;
    }
    task->returned = TRUE;
    if (!task->threaded) task_deliver(task);
}

void
ns_task_return_pointer(NsTask *task, gpointer result,
                       GDestroyNotify result_destroy)
{
    task->result = result;
    task->result_destroy = result_destroy;
    task_returned(task);
}

void
ns_task_return_boolean(NsTask *task, gboolean result)
{
    task->result_boolean = result;
    task_returned(task);
}

void
ns_task_return_error(NsTask *task, GError *error)
{
    task->error = error;
    task_returned(task);
}

void
ns_task_return_new_error(NsTask *task, GQuark domain, gint code,
                         const char *format, ...)
{
    va_list args;
    va_start(args, format);
    GError *error = g_error_new_valist(domain, code, format, args);
    va_end(args);
    ns_task_return_error(task, error);
}

static void
task_thread_work(void *data)
{
    NsTask *task = data;
    task->thread_func(task, task->task_data, task->cancellable);
    if (!task->returned)
        g_warning("ns_task: thread function returned no value");
    task_deliver(task);
    ns_task_unref(task);
}

void
ns_task_run_in_thread(NsTask *task, NsTaskThreadFunc fn)
{
    task->threaded = TRUE;
    task->thread_func = fn;
    ns_job_run(task_thread_work, NULL, ns_task_ref(task));
}

/* TRUE with *error set when the task failed or was cancelled. */
static gboolean
task_propagate_error(NsTask *task, GError **error)
{
    if (task->cancellable &&
        ns_cancellable_set_error_if_cancelled(task->cancellable, error))
        return TRUE;
    if (task->error) {
        g_propagate_error(error, task->error);
        task->error = NULL;
        return TRUE;
    }
    return FALSE;
}

gpointer
ns_task_propagate_pointer(NsTask *task, GError **error)
{
    if (task_propagate_error(task, error)) return NULL;
    task->result_taken = TRUE;
    return task->result;
}

gboolean
ns_task_propagate_boolean(NsTask *task, GError **error)
{
    if (task_propagate_error(task, error)) return FALSE;
    return task->result_boolean;
}
