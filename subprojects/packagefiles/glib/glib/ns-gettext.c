/* Weststar SDL Browser — GLib's gettext functions, untranslated.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * In place of glib/ggettext.c in Weststar's build of GLib. GLib translates
 * its own messages (the text of a GError, mostly) through gettext and a
 * message catalogue installed with GLib. This build installs no catalogue
 * and the browser shows none of those messages to the user, so each
 * function returns the text it was given, and libintl is not needed.
 */

#include "config.h"

#include <string.h>

#include "ggettext.h"
#include "glibintl.h"

const gchar *
glib_gettext (const gchar *str)
{
  return str;
}

/* "context\004message": the message. */
const gchar *
glib_pgettext (const gchar *msgctxtid, gsize msgidoffset)
{
  return msgctxtid + msgidoffset;
}

/* The message without the "context|" before it, when it came back
 * untranslated, which here it always does. */
const gchar *
g_strip_context (const gchar *msgid, const gchar *msgval)
{
  if (msgval == msgid)
    {
      const char *c = strchr (msgid, '|');
      if (c != NULL)
        return c + 1;
    }
  return msgval;
}

const gchar *
g_dpgettext (const gchar *domain, const gchar *msgctxtid, gsize msgidoffset)
{
  (void) domain;
  if (msgidoffset != 0)
    return msgctxtid + msgidoffset;
  const char *sep = strchr (msgctxtid, '|');
  return sep != NULL ? sep + 1 : msgctxtid;
}

const gchar *
g_dpgettext2 (const gchar *domain, const gchar *msgctxt, const gchar *msgid)
{
  (void) domain;
  (void) msgctxt;
  return msgid;
}

const gchar *
g_dgettext (const gchar *domain, const gchar *msgid)
{
  (void) domain;
  return msgid;
}

const gchar *
g_dcgettext (const gchar *domain, const gchar *msgid, gint category)
{
  (void) domain;
  (void) category;
  return msgid;
}

const gchar *
g_dngettext (const gchar *domain, const gchar *msgid, const gchar *msgid_plural,
             gulong n)
{
  (void) domain;
  return n == 1 ? msgid : msgid_plural;
}
