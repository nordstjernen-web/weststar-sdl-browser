/* Weststar SDL Browser — engine loop registry with timeout/idle/remove helpers.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_MAINCTX_H
#define NS_MAINCTX_H

#include "plat/ns_loop.h"

void     ns_engine_context_set(NsLoop *loop);
NsLoop  *ns_engine_context(void);
unsigned ns_engine_timeout_add(unsigned ms, NsSourceFunc fn, void *data);
unsigned ns_engine_idle_add(NsSourceFunc fn, void *data);
void     ns_engine_source_remove(unsigned id);

#endif
