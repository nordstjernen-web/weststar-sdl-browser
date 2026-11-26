/* Weststar SDL Browser — the NsLoop the page engine runs on.
 * Timeout/idle/remove helpers attach to the thread default or the registered engine loop.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "mainctx.h"

static NsLoop *g_engine_loop;

void
ns_engine_context_set(NsLoop *loop)
{
    g_engine_loop = loop;
}

/* Loop for engine sources: the thread default, else the registered engine
 * one; NULL (the default loop) when neither exists, as in headless runs. */
NsLoop *
ns_engine_context(void)
{
    NsLoop *loop = ns_loop_get_thread_default();
    return loop ? loop : g_engine_loop;
}

/* A timeout on the engine loop. */
unsigned
ns_engine_timeout_add(unsigned ms, NsSourceFunc fn, void *data)
{
    return ns_loop_timeout_add(ns_engine_context(), ms, fn, data);
}

/* An idle on the engine loop. */
unsigned
ns_engine_idle_add(NsSourceFunc fn, void *data)
{
    return ns_loop_idle_add(ns_engine_context(), fn, data);
}

/* Removes a source by id (ids are unique across loops). */
void
ns_engine_source_remove(unsigned id)
{
    if (id) ns_loop_source_remove(id);
}
