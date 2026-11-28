# nsfc: the part of fontconfig's API the text stack calls, over the
# platform's font folders (see docs/porting-to-sdl.md). Its sources are
# built into the browser when the system's fontconfig is not used
# (src/meson.build); the test is built either way.
nsfc_sources = files(
  'nsfc-bundled.c',
  'nsfc-match.c',
  'nsfc-pattern.c',
  'nsfc-scan.c',
)

nsfc_inc = include_directories('.')

# The fallback fonts of -Dbundled_fonts (the top-level meson.build), each
# embedded as the files of src/embedded/ are, and nsfc_bundled_fonts.h, the
# table over them for nsfc-bundled.c: empty without them.
nsfc_bundled_includes = []
nsfc_bundled_rows = []
foreach font : nsfc_bundled_fonts
  i = nsfc_bundled_rows.length()
  header = 'nsfc_bundled_font_@0@.h'.format(i)
  symbol = 'nsfc_bundled_font_@0@'.format(i)
  if ns_have_embed
    embed_conf = configuration_data()
    embed_conf.set('SYMBOL', symbol)
    embed_conf.set('PATH', (meson.project_source_root() / fs.parent(font) /
                            fs.name(font)).replace('\\', '/'))
    nsfc_sources += configure_file(
      input: '../embedded/embed.h.in',
      output: header,
      configuration: embed_conf,
    )
  else
    nsfc_sources += custom_target(
      header,
      input: ['../../scripts/embed-text.py', font],
      output: header,
      command: [import('python').find_installation(), '@INPUT0@', '@INPUT1@',
                '@OUTPUT@', symbol],
    )
  endif
  nsfc_bundled_includes += '#include "@0@"'.format(header)
  nsfc_bundled_rows += ('    { "@0@", (const unsigned char *)@1@,' +
                        ' sizeof @1@ - 1 },').format(fs.name(font), symbol)
endforeach
nsfc_bundled_conf = configuration_data()
nsfc_bundled_conf.set('BUNDLED_FONT_INCLUDES', '\n'.join(nsfc_bundled_includes))
nsfc_bundled_conf.set('BUNDLED_FONT_ROWS', '\n'.join(nsfc_bundled_rows))
nsfc_sources += configure_file(
  input: 'nsfc_bundled_fonts.h.in',
  output: 'nsfc_bundled_fonts.h',
  configuration: nsfc_bundled_conf,
)

# Patterns, character sets, the family rules and the order of a sorted
# list, checked with made-up fonts; NSFC_TEST_FONT names a font file to
# scan as well.
test(
  'nsfc',
  executable(
    'nsfc-test',
    files('tests/nsfc-test.c') + nsfc_sources + files(
      '../plat/ns_env.c',
      '../plat/ns_fs.c',
      '../plat/ns_plat.c',
      '../plat/ns_sdl2.c',
    ),
    dependencies: [sdl_dep, libfreetype, libm] +
      ns_plat_test_win_libs,
    # nsfc's headers come before the system's fontconfig, if any.
    include_directories: [nsfc_inc, include_directories('..')],
    # As for the browser: no -pie on Windows, where clang rejects it as
    # unused at link time (src/sdl/meson.build).
    pie: host_machine.system() != 'windows',
  ),
  suite: 'nsfc',
)

# And against SDL2, with the platform layer's SDL2 side (src/plat/).
if sdl2_dep.found()
  test(
    'nsfc-sdl2',
    executable(
      'nsfc-test-sdl2',
      files('tests/nsfc-test.c') + nsfc_sources + files(
        '../plat/ns_env.c',
        '../plat/ns_fs.c',
        '../plat/ns_plat.c',
        '../plat/ns_sdl2.c',
      ),
      c_args: ['-DNS_SDL2'],
      dependencies: [sdl2_dep, libfreetype, libm] +
        ns_plat_test_win_libs,
      include_directories: [nsfc_inc, include_directories('..')],
      pie: host_machine.system() != 'windows',
    ),
    suite: 'nsfc',
  )
endif
