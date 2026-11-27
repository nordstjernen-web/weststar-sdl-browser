# The SDL layer (threads, files, the environment, system services) is built
# into the browser from src/meson.build. Here: the test of what it reads
# from the system and of its calendar arithmetic, run in a folder of its
# own.
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
      'ns_sys.c',
    ),
    dependencies: [sdl3_dep, libm] +
      (host_machine.system() == 'windows' ? [cc.find_library('shell32')] : []),
    include_directories: include_directories('..'),
    pie: host_machine.system() != 'windows',
  ),
  env: {'NS_SYS_TEST_DIR': ns_sys_test_dir},
  suite: 'plat',
)

# The same test against SDL2, where it is installed: the platform layer on
# SDL2 under SDL3's names (ns_sdl.h, ns_sdl2.c), which is what Windows XP
# and the Amiga family will build with. The browser itself is not built
# with SDL2 yet.
sdl2_dep = dependency('sdl2', version: '>=2.0.14', required: false)
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
        (host_machine.system() == 'windows' ? [cc.find_library('shell32')] : []),
      include_directories: include_directories('..'),
      pie: host_machine.system() != 'windows',
    ),
    env: {'NS_SYS_TEST_DIR': ns_sys_test_dir + '-sdl2'},
    suite: 'plat',
  )
endif
