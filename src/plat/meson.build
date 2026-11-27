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
