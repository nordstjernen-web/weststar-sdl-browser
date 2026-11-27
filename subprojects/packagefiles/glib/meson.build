# Weststar's build of GLib: the files of GLib and GObject the browser's
# code reaches, as two static libraries, in place of GLib's own build
# (which also builds GIO, GModule, the tools and the tests, and needs PCRE2
# and libffi). Selected with -Dglib=bundled.
#
# The browser imports about 320 GLib functions: containers, strings, UTF-8
# and the Unicode tables, key files, checksums, logging, and GObject's type
# system for the text library. Linked statically with unused sections
# dropped, that is about 0.7 MB. The file lists in glib/ and gobject/ are
# the sources those functions and what they call live in; the linker
# discards the rest of each file, so the lists name whole files that are
# only partly used. Files named ns-*.c are Weststar's, in place of GLib's:
# threads and clocks on SDL, gettext without translation, and the names
# of what is left out.
#
# This build is for Unix-like systems so far. What it configures is what
# those files consult: about sixty of GLib's two hundred configuration
# macros.
project('glib', 'c', version: '2.80.5', license: 'LGPL-2.1-or-later',
        meson_version: '>=1.3')

cc = meson.get_compiler('c')
host_system = host_machine.system()
if host_system == 'windows' or host_system == 'darwin'
  error('-Dglib=bundled is set up for Linux and the BSDs so far; use the system GLib on ' + host_system)
endif

python = find_program('python3', 'python')
gen_visibility_macros = files('tools/gen-visibility-macros.py')
glib_version = meson.project_version()
version_parts = glib_version.split('.')

# ---- config.h ----

conf = configuration_data()
conf.set('_GNU_SOURCE', 1)
conf.set_quoted('GETTEXT_PACKAGE', 'glib20')
conf.set_quoted('GLIB_LOCALE_DIR', get_option('prefix') / get_option('localedir'))
conf.set('GLIB_MAJOR_VERSION', version_parts[0].to_int())
conf.set('GLIB_MINOR_VERSION', version_parts[1].to_int())
conf.set('GLIB_MICRO_VERSION', version_parts[2].to_int())
conf.set('GLIB_INTERFACE_AGE', version_parts[2].to_int())
conf.set('GLIB_BINARY_AGE', 100 * version_parts[1].to_int() + version_parts[2].to_int())
# Messages are not translated (no ENABLE_NLS): the browser shows none of
# GLib's to the user.
conf.set('THREADS_POSIX', 1)
# The C library's printf is C99's; GLib's own copy (gnulib) is not built.
conf.set('USE_SYSTEM_PRINTF', true)
conf.set('HAVE_C99_SNPRINTF', 1)
conf.set('HAVE_C99_VSNPRINTF', 1)

gnu = ['-D_GNU_SOURCE']
foreach h : ['alloca.h', 'dirent.h', 'sched.h', 'stdatomic.h', 'stdint.h',
             'syslog.h', 'sys/auxv.h', 'sys/param.h', 'sys/prctl.h',
             'sys/resource.h', 'sys/select.h', 'sys/time.h', 'wchar.h',
             'xlocale.h', 'poll.h', 'pwd.h', 'locale.h', 'langinfo.h']
  if cc.has_header(h)
    conf.set('HAVE_' + h.underscorify().to_upper(), 1)
  endif
endforeach
foreach f : ['close_range', 'fallocate', 'fsync', 'getauxval', 'getpwuid_r',
             'getresuid', 'localtime_r', 'memalign', 'mkostemp', 'newlocale',
             'pipe2', 'poll', 'posix_memalign', 'prctl', 'readlink', 'setenv',
             'stpcpy', 'strcasecmp', 'strerror_r', 'strlcpy', 'strncasecmp',
             'strsignal', 'strtod_l', 'strtoll_l', 'strtoull_l', 'unsetenv',
             'uselocale', 'vasprintf', 'aligned_alloc', 'issetugid',
             'pthread_attr_setinheritsched', 'pthread_attr_setstacksize',
             'pthread_condattr_setclock', 'pthread_getaffinity_np']
  if cc.has_function(f, args: gnu, dependencies: dependency('threads'))
    conf.set('HAVE_' + f.to_upper(), 1)
  endif
endforeach
if cc.links('''#include <langinfo.h>
               int main(void) { return nl_langinfo(CODESET)[0]; }''',
            name: 'nl_langinfo(CODESET)')
  conf.set('HAVE_CODESET', 1)
  conf.set('HAVE_LANGINFO_CODESET', 1)
endif
if cc.compiles('''#include <signal.h>
                  sig_atomic_t v;''', name: 'sig_atomic_t')
  conf.set('HAVE_SIG_ATOMIC_T', 1)
endif
if cc.compiles('__uint128_t v;', name: '__uint128_t')
  conf.set('HAVE_UINT128_T', 1)
endif
if cc.compiles('''#define _GNU_SOURCE
                  #include <string.h>
                  int main(void) { char b[8]; char *p = strerror_r(0, b, 8); return !p; }''',
               args: ['-Werror'], name: 'strerror_r returns char *')
  conf.set('STRERROR_R_CHAR_P', 1)
endif
foreach t : [['char', 'CHAR'], ['short', 'SHORT'], ['int', 'INT'],
             ['long', 'LONG'], ['long long', 'LONG_LONG'],
             ['size_t', 'SIZE_T'], ['ssize_t', 'SSIZE_T'],
             ['void *', 'VOID_P'], ['wchar_t', 'WCHAR_T']]
  conf.set('SIZEOF_' + t[1],
           cc.sizeof(t[0], prefix: '#include <stddef.h>\n#include <sys/types.h>'))
endforeach
configure_file(output: 'config.h', configuration: conf)

threads_dep = dependency('threads')
# Threads, locks and clocks come from SDL (glib/ns-gthread-sdl.c,
# glib/ns-gclock.c), as everything else the browser asks a system for.
sdl3_dep = dependency('sdl3')
libm = cc.find_library('m', required: false)

# Third-party code: built without the project's warnings, and with its
# sections apart so that the linker can drop what is not called.
glib_build_args = ['-w', '-DHAVE_CONFIG_H'] + cc.get_supported_arguments(
  '-ffunction-sections', '-fdata-sections', '-fno-strict-aliasing')

root_inc = include_directories('.')
subdir('glib')
subdir('gobject')

meson.override_dependency('glib-2.0', glib_dep)
meson.override_dependency('gobject-2.0', gobject_dep)
