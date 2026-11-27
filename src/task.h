/* Weststar SDL Browser — NsTask: an asynchronous operation with a result,
 * in the shape of GIO's GTask, on NsLoop and the NsJob worker pool.
 *
 * A task remembers the loop of the thread that created it (its pushed
 * thread-default NsLoop, else the default loop) and delivers its callback
 * there, from a source at NS_PRIORITY_DEFAULT, once one of the
 * ns_task_return_* functions has run. A task run with ns_task_run_in_thread
 * delivers only after its thread function has returned, wherever in that
 * function it returned its value. The callback is never called from inside
 * ns_task_return_* itself.
 *
 * The cancellable is only a flag here: the thread function may poll it,
 * and once it is cancelled ns_task_propagate_* report NS_IO_ERROR_CANCELLED
 * whatever the task returned, as GTask does.
 *
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_TASK_H
#define NS_TASK_H

#include <glib.h>
#include "cancellable.h"

typedef struct NsTask NsTask;

typedef void (*NsTaskReadyCallback)(NsTask *task, gpointer user_data);
typedef void (*NsTaskThreadFunc)(NsTask *task, gpointer task_data,
                                 NsCancellable *cancellable);

/* The caller owns the returned reference. callback may be NULL. */
NsTask  *ns_task_new(NsCancellable *cancellable, NsTaskReadyCallback callback,
                     gpointer user_data);
NsTask  *ns_task_ref(NsTask *task);
void     ns_task_unref(NsTask *task);

void     ns_task_set_task_data(NsTask *task, gpointer task_data,
                               GDestroyNotify task_data_destroy);
gpointer ns_task_get_task_data(NsTask *task);
void     ns_task_set_source_tag(NsTask *task, gpointer source_tag);
gpointer ns_task_get_source_tag(NsTask *task);
NsCancellable *ns_task_get_cancellable(NsTask *task);

/* Runs fn on a worker thread; the task holds its own reference meanwhile. */
void     ns_task_run_in_thread(NsTask *task, NsTaskThreadFunc fn);

void     ns_task_return_pointer(NsTask *task, gpointer result,
                                GDestroyNotify result_destroy);
void     ns_task_return_boolean(NsTask *task, gboolean result);
/* Takes ownership of error. */
void     ns_task_return_error(NsTask *task, GError *error);
void     ns_task_return_new_error(NsTask *task, GQuark domain, gint code,
                                  const char *format, ...) G_GNUC_PRINTF(4, 5);

/* The returned pointer, now the caller's, or NULL with *error set. */
gpointer ns_task_propagate_pointer(NsTask *task, GError **error);
gboolean ns_task_propagate_boolean(NsTask *task, GError **error);

#endif /* NS_TASK_H */
