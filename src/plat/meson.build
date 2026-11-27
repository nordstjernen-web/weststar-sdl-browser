# The SDL layer (threads, files, the environment, system services) is built
# into the browser from src/meson.build. Here: the test of what it reads
# from the system and of its calendar arithmetic, run in a folder of its
# own. On Windows, the SDL2 side (ns_sdl2.c) calls the save dialog and the
# registry too.
ns_plat_test_win_libs = host_machine.system() == 'windows' ? [
  cc.find_library('shell32'),
  cc.find_library('comdlg32'),
  cc.find_library('advapi32'),
] : []
ns_sys_test_dir = meson.current_build_dir() / 'ns-sys-test-dir'
test(
  'ns_sys',
  executable(
    'ns-sys-test',
    files(
      'tests/ns-sys-test.c',
      'ns_env.c',
      'ns_fs.c',
      'ns_plat.c',
      'ns_sdl2.c',
      'ns_sys.c',
    ),
    dependencies: [sdl_dep, libm] +
      ns_plat_test_win_libs,
    include_directories: include_directories('..'),
    pie: host_machine.system() != 'windows',
  ),
  env: {'NS_SYS_TEST_DIR': ns_sys_test_dir},
  suite: 'plat',
)

# In an SDL3 build, the same test against SDL2 too, where it is installed:
# the platform layer on SDL2 under SDL3's names (ns_sdl.h, ns_sdl2.c), which
# is what Windows XP and the Amiga family build with. (With -Dsdl=sdl2 the
# test above is that already.)
sdl2_dep = dependency('', required: false)
if ns_sdl == 'sdl3'
  sdl2_dep = dependency('sdl2', version: ns_floors['sdl2'], required: false,
                        allow_fallback: false)
endif
if sdl2_dep.found()
  test(
    'ns_sys-sdl2',
    executable(
      'ns-sys-test-sdl2',
      files(
        'tests/ns-sys-test.c',
        'ns_env.c',
        'ns_fs.c',
        'ns_plat.c',
        'ns_sdl2.c',
        'ns_sys.c',
      ),
      c_args: ['-DNS_SDL2'],
      dependencies: [sdl2_dep, libm] +
        ns_plat_test_win_libs,
      include_directories: include_directories('..'),
      pie: host_machine.system() != 'windows',
    ),
    env: {'NS_SYS_TEST_DIR': ns_sys_test_dir + '-sdl2'},
    suite: 'plat',
  )
endif
