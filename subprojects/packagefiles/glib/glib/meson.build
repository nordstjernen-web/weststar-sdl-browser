# libglib, the files the browser reaches (see ../meson.build).

gversionmacros_h = custom_target(
  input: 'gversionmacros.h.in',
  output: 'gversionmacros.h',
  command: [python, gen_visibility_macros, glib_version, 'versions-macros',
            '@INPUT@', '@OUTPUT@'],
)
glib_visibility_h = custom_target(
  output: 'glib-visibility.h',
  command: [python, gen_visibility_macros, glib_version, 'visibility-macros',
            'GLIB', '@OUTPUT@'],
)
glib_built_headers = [gversionmacros_h, glib_visibility_h]

# glibconfig.h: the integer types and their printf formats, by the sizes
# the compiler reports.
size_long = cc.sizeof('long')
size_size_t = cc.sizeof('size_t', prefix: '#include <stddef.h>')
size_void_p = cc.sizeof('void *')
size_int = cc.sizeof('int')

gc = configuration_data()
gc.set('GLIB_MAJOR_VERSION', version_parts[0])
gc.set('GLIB_MINOR_VERSION', version_parts[1])
gc.set('GLIB_MICRO_VERSION', version_parts[2])
gc.set('GLIB_HAVE_ALLOCA_H', cc.has_header('alloca.h'))
gc.set('GLIB_STATIC_COMPILATION', '1')
gc.set('GOBJECT_STATIC_COMPILATION', '1')
gc.set('GLIB_USING_SYSTEM_PRINTF', true)
gc.set('gint16', 'short')
gc.set_quoted('gint16_modifier', 'h')
gc.set_quoted('gint16_format', 'hi')
gc.set_quoted('guint16_format', 'hu')
gc.set('gint32', 'int')
gc.set_quoted('gint32_modifier', '')
gc.set_quoted('gint32_format', 'i')
gc.set_quoted('guint32_format', 'u')
if size_long == 8
  gc.set('glib_extension', '')
  gc.set('gint64', 'long')
  gc.set_quoted('gint64_modifier', 'l')
  gc.set_quoted('gint64_format', 'li')
  gc.set_quoted('guint64_format', 'lu')
  gc.set('gint64_constant', '(val##L)')
  gc.set('guint64_constant', '(val##UL)')
else
  gc.set('glib_extension', 'G_GNUC_EXTENSION ')
  gc.set('gint64', 'long long')
  gc.set_quoted('gint64_modifier', 'll')
  gc.set_quoted('gint64_format', 'lli')
  gc.set_quoted('guint64_format', 'llu')
  gc.set('gint64_constant', '(G_GNUC_EXTENSION (val##LL))')
  gc.set('guint64_constant', '(G_GNUC_EXTENSION (val##ULL))')
endif
gc.set('glib_void_p', size_void_p)
gc.set('glib_long', size_long)
gc.set('glib_size_t', size_size_t)
gc.set('glib_ssize_t', size_size_t)
# size_t and pointers: long where they are as wide as long (every Unix),
# int on a 32-bit system where long is wider, long long on 64-bit Windows.
if size_size_t == size_long
  gc.set('glib_size_type_define', 'long')
  gc.set_quoted('gsize_modifier', 'l')
  gc.set_quoted('gssize_modifier', 'l')
  gc.set_quoted('gsize_format', 'lu')
  gc.set_quoted('gssize_format', 'li')
  gc.set('glib_msize_type', 'LONG')
elif size_size_t == size_int
  gc.set('glib_size_type_define', 'int')
  gc.set_quoted('gsize_modifier', '')
  gc.set_quoted('gssize_modifier', '')
  gc.set_quoted('gsize_format', 'u')
  gc.set_quoted('gssize_format', 'i')
  gc.set('glib_msize_type', 'INT')
else
  gc.set('glib_size_type_define', 'long long')
  gc.set_quoted('gsize_modifier', 'll')
  gc.set_quoted('gssize_modifier', 'll')
  gc.set_quoted('gsize_format', 'llu')
  gc.set_quoted('gssize_format', 'lli')
  gc.set('glib_msize_type', 'INT64')
endif
if size_void_p == size_long
  gc.set('glib_intptr_type_define', 'long')
  gc.set_quoted('gintptr_modifier', 'l')
  gc.set_quoted('gintptr_format', 'li')
  gc.set_quoted('guintptr_format', 'lu')
  gc.set('glib_gpi_cast', '(glong)')
  gc.set('glib_gpui_cast', '(gulong)')
elif size_void_p == size_int
  gc.set('glib_intptr_type_define', 'int')
  gc.set_quoted('gintptr_modifier', '')
  gc.set_quoted('gintptr_format', 'i')
  gc.set_quoted('guintptr_format', 'u')
  gc.set('glib_gpi_cast', '(gint)')
  gc.set('glib_gpui_cast', '(guint)')
else
  gc.set('glib_intptr_type_define', 'long long')
  gc.set_quoted('gintptr_modifier', 'll')
  gc.set_quoted('gintptr_format', 'lli')
  gc.set_quoted('guintptr_format', 'llu')
  gc.set('glib_gpi_cast', '(gint64)')
  gc.set('glib_gpui_cast', '(guint64)')
endif
gc.set('gintbits', size_int * 8)
gc.set('glongbits', size_long * 8)
gc.set('gsizebits', size_size_t * 8)
gc.set('glib_os', is_windows ? '#define G_OS_WIN32\n#define G_PLATFORM_WIN32' : '#define G_OS_UNIX')
# va_list is an array type where a copy of one cannot be assigned.
if cc.compiles('''#include <stdarg.h>
                  void f(va_list a) { va_list b; b = a; (void)b; }''',
               name: 'va_list can be assigned')
  gc.set('glib_vacopy', '')
else
  gc.set('glib_vacopy', '#define G_VA_COPY_AS_ARRAY 1')
endif
gc.set('g_threads_impl_def', is_windows ? 'WIN32' : 'POSIX')
gc.set('G_ATOMIC_LOCK_FREE',
       cc.links('''int main(void) { int v = 0; __sync_bool_compare_and_swap(&v, 0, 1);
                                    return __sync_fetch_and_add(&v, 1); }''',
                name: 'lock-free atomic builtins'))
if host_machine.endian() == 'big'
  gc.set('g_byte_order', 'G_BIG_ENDIAN')
  gc.set('g_bs_native', 'BE')
  gc.set('g_bs_alien', 'LE')
else
  gc.set('g_byte_order', 'G_LITTLE_ENDIAN')
  gc.set('g_bs_native', 'LE')
  gc.set('g_bs_alien', 'BE')
endif
# The values of the system's poll and socket constants.
if is_windows
  net_prefix = '#define _WIN32_WINNT 0x0601\n#include <winsock2.h>\n#include <ws2tcpip.h>'
else
  net_prefix = '#include <sys/types.h>\n#include <poll.h>\n#include <sys/socket.h>'
endif
foreach c : [['g_pollin', 'POLLIN'], ['g_pollout', 'POLLOUT'],
             ['g_pollpri', 'POLLPRI'], ['g_pollhup', 'POLLHUP'],
             ['g_pollerr', 'POLLERR'], ['g_pollnval', 'POLLNVAL'],
             ['g_af_unix', 'AF_UNIX'], ['g_af_inet', 'AF_INET'],
             ['g_af_inet6', 'AF_INET6'], ['g_msg_oob', 'MSG_OOB'],
             ['g_msg_peek', 'MSG_PEEK'], ['g_msg_dontroute', 'MSG_DONTROUTE']]
  gc.set(c[0], cc.compute_int(c[1], prefix: net_prefix))
endforeach
if is_windows
  gc.set('g_pid_type', 'void *')
  gc.set_quoted('g_pid_format', 'p')
  gc.set_quoted('g_pollfd_format', size_void_p == 8 ? '%#llx' : '%#x')
  gc.set('g_dir_separator', '\\\\')
  gc.set('g_searchpath_separator', ';')
  gc.set('g_module_suffix', 'dll')
else
  gc.set('g_pid_type', 'int')
  gc.set_quoted('g_pid_format', 'i')
  gc.set_quoted('g_pollfd_format', '%d')
  gc.set('g_dir_separator', '/')
  gc.set('g_searchpath_separator', ':')
  gc.set('g_module_suffix', 'so')
endif
glibconfig_h = configure_file(input: 'glibconfig.h.in', output: 'glibconfig.h',
                              configuration: gc)

glib_inc = include_directories('.')

# Windows needs GLib's wrappers for file names and its Windows helpers.
glib_platform_sources = []
glib_platform_deps = []
if is_windows
  # (gconvert.c takes in win_iconv.c itself.)
  glib_platform_sources = files('gwin32.c', 'gstdio.c')
  glib_platform_deps = [cc.find_library('ws2_32'), cc.find_library('winmm'),
                        cc.find_library('ole32'), cc.find_library('shell32'),
                        cc.find_library('uuid')]
endif

glib_lib = static_library('glib-2.0',
  glib_platform_sources + files(
    'garcbox.c', 'garray.c', 'gbase64.c', 'gbitlock.c', 'gbytes.c',
    'gcharset.c', 'gchecksum.c', 'gconvert.c', 'gdataset.c', 'gdate.c',
    'gdir.c', 'genviron.c', 'gerror.c', 'gfileutils.c', 'ns-gettext.c',
    'ghash.c', 'ghook.c', 'gjournal-private.c', 'gkeyfile.c', 'glib-init.c',
    'glib-private.c', 'glist.c', 'ns-gclock.c', 'gmarkup.c',
    'gmem.c', 'gmessages.c', 'gpattern.c', 'gprintf.c',
    'gqsort.c', 'gquark.c', 'gqueue.c', 'grcbox.c', 'grefcount.c',
    'gslice.c', 'gslist.c', 'gstrfuncs.c', 'gstring.c', 'gtestutils.c',
    'ns-gthread-sdl.c', 'gthread.c', 'gunibreak.c', 'gunicollate.c',
    'gunidecomp.c', 'guniprop.c', 'guri.c', 'gutf8.c', 'gutils.c',
    'gvariant-core.c', 'gvariant-serialiser.c', 'gvariant.c',
    'gvarianttype.c', 'gvarianttypeinfo.c', 'gversion.c',
    'libcharset/localcharset.c',
    'ns-left-out.c',
  ) + glib_built_headers,
  c_args: glib_build_args + [
    '-DG_LOG_DOMAIN="GLib"',
    '-DGLIB_COMPILATION',
    '-DGLIB_CHARSETALIAS_DIR="' + get_option('prefix') / get_option('libdir') + '"',
  ],
  include_directories: [root_inc, glib_inc],
  dependencies: [threads_dep, libm, sdl_dep] + glib_platform_deps,
  pic: true,
  install: false,
)

glib_dep = declare_dependency(
  link_with: glib_lib,
  sources: glib_built_headers,
  include_directories: [root_inc, glib_inc],
  dependencies: [threads_dep, libm] + glib_platform_deps,
  version: glib_version,
)
