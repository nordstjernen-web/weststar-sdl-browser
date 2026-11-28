/* Weststar SDL Browser — <glib-unix.h> over nscore: what
 * the engine takes from it is in glib.h.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_CORE_GLIB_UNIX_H
#define NS_CORE_GLIB_UNIX_H

#include <glib.h>

/* What GLib's glib-unix.h brings with it, which the engine relies on
 * (_exit, close, read and write). */
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>

#endif
