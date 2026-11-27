/* The header GLib's build generates with glib-mkenums from glib/gunicode.h:
 * the GType functions of GLib's four Unicode enumerations. It is the same
 * for every platform, so Weststar's build of GLib carries it written out
 * rather than run the generator. This code is licensed under the same
 * license as GLib; it links to GLib, so must comply with the LGPL linking
 * clauses. */

#ifndef __GOBJECT_ENUM_TYPES_H__
#define __GOBJECT_ENUM_TYPES_H__

#include <glib-object.h>

G_BEGIN_DECLS

/* enumerations from "glib/gunicode.h" */
GOBJECT_AVAILABLE_IN_2_60 GType g_unicode_type_get_type (void) G_GNUC_CONST;
#define G_TYPE_UNICODE_TYPE (g_unicode_type_get_type ())
GOBJECT_AVAILABLE_IN_2_60 GType g_unicode_break_type_get_type (void) G_GNUC_CONST;
#define G_TYPE_UNICODE_BREAK_TYPE (g_unicode_break_type_get_type ())
GOBJECT_AVAILABLE_IN_2_60 GType g_unicode_script_get_type (void) G_GNUC_CONST;
#define G_TYPE_UNICODE_SCRIPT (g_unicode_script_get_type ())
GOBJECT_AVAILABLE_IN_2_60 GType g_normalize_mode_get_type (void) G_GNUC_CONST;
#define G_TYPE_NORMALIZE_MODE (g_normalize_mode_get_type ())
G_END_DECLS

#endif /* __GOBJECT_ENUM_TYPES_H__ */
