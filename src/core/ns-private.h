/* Weststar SDL Browser — nscore: what its sources share among themselves.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_PRIVATE_H
#define NS_PRIVATE_H

#include "ns-base.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sched.h>
#endif

/* A lock for the few global tables (quarks, interned strings, the log
 * writer): held only around a lookup, so spinning is cheaper than a mutex
 * that would need initialising. */
typedef struct { volatile int held; } NsSpinLock;
#define NS_SPIN_LOCK_INIT { 0 }

static inline void
ns_spin_lock(NsSpinLock *lock)
{
    while (__atomic_exchange_n(&lock->held, 1, __ATOMIC_ACQUIRE)) {
        while (__atomic_load_n(&lock->held, __ATOMIC_RELAXED)) {
#ifdef _WIN32
            SwitchToThread();
#else
            sched_yield();
#endif
        }
    }
}

static inline void
ns_spin_unlock(NsSpinLock *lock)
{
    __atomic_store_n(&lock->held, 0, __ATOMIC_RELEASE);
}

#ifdef _WIN32
/* UTF-8 to UTF-16 and back for the wide Windows calls; NULL on bad input. */
wchar_t *ns_win32_utf8_to_wide(const char *str);
char *ns_win32_wide_to_utf8(const wchar_t *str);
#endif

#endif
