# nscore, Unicode part: UTF-8, UTF-16 and Unicode properties in place of
# GLib's (ns-unicode.h; G_ names in ns-unicode-glib.h). The tables in
# ns-unicode-tables.h are made by scripts/gen-unicode-tables.py.

ns_unicode_sources = files('ns-unicode.c')

# Known answers; with NS_UCD_DIR set to a directory holding the UCD's
# NormalizationTest.txt (gen-unicode-tables.py downloads it), also the
# normalization conformance test.
test('nscore-unicode',
  executable('test-unicode', 'test-unicode.c', ns_unicode_sources,
             build_by_default: false),
  suite: 'nscore')

# The same, plus every function compared with the system GLib's over all
# code points, random input and the files in data/. Only where GLib is
# installed; nscore itself never needs it.
ns_unicode_glib_dep = dependency('glib-2.0', required: false)
if ns_unicode_glib_dep.found()
  test('nscore-unicode-vs-glib',
    executable('test-unicode-vs-glib', 'test-unicode.c', ns_unicode_sources,
               c_args: ['-DNS_TEST_VS_GLIB'],
               dependencies: ns_unicode_glib_dep,
               build_by_default: false),
    args: [meson.project_source_root() / 'data'],
    suite: 'nscore',
    timeout: 600)
endif
