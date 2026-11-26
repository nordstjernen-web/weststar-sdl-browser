# gfx: the in-tree 2D rasterizer replacing Cairo (see docs/porting-to-sdl.md).
weststar_gfx_sources = files(
  'gfx-composite.c',
  'gfx-context.c',
  'gfx-matrix.c',
  'gfx-path.c',
  'gfx-pattern.c',
  'gfx-raster.c',
  'gfx-region.c',
  'gfx-stroke.c',
  'gfx-surface.c',
)

weststar_gfx_lib = static_library(
  'weststar-gfx',
  weststar_gfx_sources,
  dependencies: [libm, zlib],
)

weststar_gfx_dep = declare_dependency(
  link_with: weststar_gfx_lib,
  include_directories: include_directories('.'),
  dependencies: [libm, zlib],
)

# The test and the benchmark compare gfx with Cairo, so they are built only
# where Cairo is installed; the browser itself does not use it.
# Draws every scene and compares it with its reference image in
# tests/ref/; GFX_TEST_WRITE_REF=1 regenerates them.
test(
  'gfx',
  executable(
    'gfx-test',
    files('tests/gfx-test.c'),
    dependencies: [weststar_gfx_dep, zlib],
    include_directories: include_directories('tests'),
    c_args: ['-DGFX_REF_DIR="@0@"'.format(meson.current_source_dir() / 'tests' / 'ref')],
    # As for the browser: no -pie on Windows, where clang rejects it as
    # unused at link time (src/sdl/meson.build).
    pie: host_machine.system() != 'windows',
    build_by_default: true,
  ),
  suite: 'gfx',
)

# Times page-like workloads: meson test -C builddir --benchmark --verbose
benchmark(
  'gfx',
  executable(
    'gfx-bench',
    files('tests/gfx-bench.c'),
    dependencies: [weststar_gfx_dep],
    include_directories: include_directories('tests'),
    pie: host_machine.system() != 'windows',
    build_by_default: true,
  ),
  suite: 'gfx',
  timeout: 600,
)
