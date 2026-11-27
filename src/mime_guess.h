/* Weststar SDL Browser — guessing the type of a file that came without one.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef NS_MIME_GUESS_H
#define NS_MIME_GUESS_H

#include <glib.h>

/* The MIME type of a local or FTP file, which has no Content-Type header:
 * by the extension of `path`, else by the first bytes of its contents when
 * `data` is given. NULL when neither tells; callers then use
 * application/octet-stream. The string is static. */
const char *ns_mime_guess(const char *path, const guchar *data, gsize len);

#endif
