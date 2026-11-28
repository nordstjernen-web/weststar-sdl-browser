# nscore — the in-tree core library that takes GLib's place with
# -Dglib=native: containers, strings, printf, errors, logging, key files,
# checksums, URIs and file names (ns-*.c), and UTF-8, UTF-16 and Unicode
# properties (ns-unicode.c; tables made by scripts/gen-unicode-tables.py).
# glib.h here gives the engine GLib's names over it, so with -Dglib=native
# this directory comes first on the include path. Plain C11; no GLib.
# Public surface: `nscore_dep`. See docs/porting-to-sdl.md, stage 6.5.

ns_unicode_sources = files('ns-unicode.c')

nscore_sources = files(
  'ns-array.c',
  'ns-base64.c',
  'ns-checksum.c',
  'ns-error.c',
  'ns-file.c',
  'ns-hash.c',
  'ns-keyfile.c',
  'ns-list.c',
  'ns-log.c',
  'ns-markup.c',
  'ns-mem.c',
  'ns-path.c',
  'ns-strfuncs.c',
  'ns-string.c',
  'ns-uri.c',
) + ns_unicode_sources

# MinGW: its own C99 printf (and strtod) in place of the old C runtime's,
# whose printf knows neither %zu nor %lld on Windows XP.
nscore_c_args = []
if host_machine.system() == 'windows'
  nscore_c_args += ['-D__USE_MINGW_ANSI_STDIO=1']
endif

nscore_lib = static_library('nscore',
  nscore_sources,
  c_args: nscore_c_args,
  build_by_default: get_option('glib') == 'native',
)

nscore_dep = declare_dependency(
  link_with: nscore_lib,
  include_directories: include_directories('.'),
  compile_args: ['-DNS_GLIB_NATIVE'],
)

# Known answers for the unicode part; with NS_UCD_DIR set to a directory
# holding the UCD's NormalizationTest.txt (gen-unicode-tables.py downloads
# it), also the normalization conformance test.
test('nscore-unicode',
  executable('test-unicode', 'test-unicode.c', link_with: nscore_lib,
             build_by_default: false),
  suite: 'nscore')

# The rest of nscore: known answers, and round trips of what is written
# (doubles, key files).
test('nscore',
  executable('test-nscore', 'test-nscore.c', link_with: nscore_lib,
             c_args: nscore_c_args, build_by_default: false),
  suite: 'nscore')

# The same tests compared with the system GLib's answers, where GLib is
# installed (nscore itself never needs it): every unicode function over all
# code points, random input and the files in data/; and the other
# functions, key files written byte for byte as GLib writes them, the hash
# table's iteration order, the log format and so on.
ns_core_glib_dep = dependency('glib-2.0', required: false)
if ns_core_glib_dep.found()
  test('nscore-unicode-vs-glib',
    executable('test-unicode-vs-glib', 'test-unicode.c', link_with: nscore_lib,
               c_args: ['-DNS_TEST_VS_GLIB'],
               dependencies: ns_core_glib_dep,
               implicit_include_directories: false,
               build_by_default: false),
    args: [meson.project_source_root() / 'data'],
    suite: 'nscore',
    timeout: 600)
  test('nscore-vs-glib',
    executable('test-nscore-vs-glib', 'test-nscore.c', link_with: nscore_lib,
               c_args: nscore_c_args + ['-DNS_TEST_VS_GLIB'],
               dependencies: ns_core_glib_dep,
               implicit_include_directories: false,
               build_by_default: false),
    suite: 'nscore')
endif
