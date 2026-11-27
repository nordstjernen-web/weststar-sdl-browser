# nsfc: the part of fontconfig's API the text stack calls, over the
# platform's font folders (see docs/porting-to-sdl.md). Its sources are
# built into the browser when the system's fontconfig is not used
# (src/meson.build); the test is built either way.
nsfc_sources = files(
  'nsfc-match.c',
  'nsfc-pattern.c',
  'nsfc-scan.c',
)

nsfc_inc = include_directories('.')

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
    ),
    dependencies: [sdl3_dep, libfreetype, libm] +
      (host_machine.system() == 'windows' ? [cc.find_library('shell32')] : []),
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
        (host_machine.system() == 'windows' ? [cc.find_library('shell32')] : []),
      include_directories: [nsfc_inc, include_directories('..')],
      pie: host_machine.system() != 'windows',
    ),
    suite: 'nsfc',
  )
endif
