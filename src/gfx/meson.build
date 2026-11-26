# gfx: the in-tree 2D rasterizer replacing Cairo (see docs/porting-to-sdl.md).
weststar_gfx_sources = files(
  'gfx-composite.c',
  'gfx-context.c',
  'gfx-matrix.c',
  'gfx-path.c',
  'gfx-pattern.c',
  'gfx-raster.c',
  'gfx-stroke.c',
  'gfx-surface.c',
)

# The bridge to Cairo for call sites not ported yet; it leaves with the
# Cairo dependency.
weststar_gfx_cairo_sources = files('gfx-cairo.c')

weststar_gfx_lib = static_library(
  'weststar-gfx',
  weststar_gfx_sources + weststar_gfx_cairo_sources,
  dependencies: [libm, zlib, cairo],
)

weststar_gfx_dep = declare_dependency(
  link_with: weststar_gfx_lib,
  include_directories: include_directories('.'),
  dependencies: [libm, zlib, cairo],
)

# Draws every scene with gfx and with Cairo and compares the pixels.
test(
  'gfx',
  executable(
    'gfx-test',
    files('tests/gfx-test.c'),
    dependencies: [weststar_gfx_dep],
    include_directories: include_directories('tests'),
    # As for the browser: no -pie on Windows, where clang rejects it as
    # unused at link time (src/sdl/meson.build).
    pie: host_machine.system() != 'windows',
    build_by_default: true,
  ),
  suite: 'gfx',
)

# Times page-like workloads with gfx and with Cairo:
# meson test -C builddir --benchmark --verbose
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
