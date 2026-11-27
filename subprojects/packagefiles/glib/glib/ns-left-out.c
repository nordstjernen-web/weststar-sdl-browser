/* Weststar SDL Browser — the GLib functions this build leaves out.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Weststar's build of GLib compiles the source files the browser's code
 * reaches (see ../meson.build). Those files also hold functions the
 * browser does not reach, which the linker drops, and some of those call
 * into files that are not compiled at all: gboxed.c registers a boxed type
 * for GDateTime and for GLib's main loop, gtestutils.c starts child
 * processes and runs a main loop. A linker may ask for those names all the same, so each is
 * defined here, to say which it was and stop should one ever be called.
 *
 * The list is what the two libraries leave undefined in GLib's namespace:
 *   nm -u libglib-2.0.a libgobject-2.0.a, less nm --defined-only.
 * After a change of GLib's version or of the file lists, a link error
 * names what to add, and a name defined twice what to take out.
 */

#include <stdio.h>
#include <stdlib.h>

static void
ns_glib_left_out (const char *name)
{
  fprintf (stderr, "GLib: %s() is not part of this build of GLib "
           "(subprojects/packagefiles/glib)\n", name);
  abort ();
}

/* No header declares these here, so each can be a function of no
 * arguments whatever GLib's own signature for it is: it never returns. */
#define LEFT_OUT(name) void name (void); void name (void) { ns_glib_left_out (#name); }

LEFT_OUT (g_bookmark_file_copy)
LEFT_OUT (g_bookmark_file_free)
LEFT_OUT (g_child_watch_source_new)
LEFT_OUT (g_close)
LEFT_OUT (g_date_time_ref)
LEFT_OUT (g_date_time_unref)
LEFT_OUT (g_get_worker_context)
LEFT_OUT (g_hmac_ref)
LEFT_OUT (g_hmac_unref)
LEFT_OUT (g_hostname_is_ip_address)
LEFT_OUT (g_hostname_is_non_ascii)
LEFT_OUT (g_hostname_to_ascii)
LEFT_OUT (g_io_channel_read_chars)
LEFT_OUT (g_io_channel_set_buffered)
LEFT_OUT (g_io_channel_set_close_on_unref)
LEFT_OUT (g_io_channel_set_encoding)
LEFT_OUT (g_io_channel_unix_new)
LEFT_OUT (g_io_channel_unref)
LEFT_OUT (g_io_create_watch)
LEFT_OUT (g_main_context_new)
LEFT_OUT (g_main_context_new_with_next_id)
LEFT_OUT (g_main_context_ref)
LEFT_OUT (g_main_context_unref)
LEFT_OUT (g_main_loop_new)
LEFT_OUT (g_main_loop_quit)
LEFT_OUT (g_main_loop_ref)
LEFT_OUT (g_main_loop_run)
LEFT_OUT (g_main_loop_unref)
LEFT_OUT (g_mapped_file_ref)
LEFT_OUT (g_mapped_file_unref)
LEFT_OUT (g_match_info_ref)
LEFT_OUT (g_match_info_unref)
LEFT_OUT (g_option_group_ref)
LEFT_OUT (g_option_group_unref)
LEFT_OUT (g_rand_copy)
LEFT_OUT (g_rand_double)
LEFT_OUT (g_rand_double_range)
LEFT_OUT (g_rand_free)
LEFT_OUT (g_rand_int)
LEFT_OUT (g_rand_int_range)
LEFT_OUT (g_rand_new_with_seed)
LEFT_OUT (g_rand_new_with_seed_array)
LEFT_OUT (g_random_int)
LEFT_OUT (g_regex_ref)
LEFT_OUT (g_regex_unref)
LEFT_OUT (g_rmdir)
LEFT_OUT (g_shell_unquote)
LEFT_OUT (g_source_attach)
LEFT_OUT (g_source_ref)
LEFT_OUT (g_source_set_callback)
LEFT_OUT (g_source_set_ready_time)
LEFT_OUT (g_source_unref)
LEFT_OUT (g_spawn_async_with_pipes)
LEFT_OUT (g_str_to_ascii)
LEFT_OUT (g_strv_builder_ref)
LEFT_OUT (g_strv_builder_unref)
LEFT_OUT (g_time_zone_ref)
LEFT_OUT (g_time_zone_unref)
LEFT_OUT (g_timeout_source_new)
LEFT_OUT (g_timer_destroy)
LEFT_OUT (g_timer_elapsed)
LEFT_OUT (g_timer_new)
LEFT_OUT (g_timer_start)
LEFT_OUT (g_timer_stop)
LEFT_OUT (g_tree_ref)
LEFT_OUT (g_tree_unref)
LEFT_OUT (g_unlink)
LEFT_OUT (g_wakeup_acknowledge)
LEFT_OUT (g_wakeup_free)
LEFT_OUT (g_wakeup_get_pollfd)
LEFT_OUT (g_wakeup_new)
LEFT_OUT (g_wakeup_signal)
