project('nuklear', 'c', license: 'MIT OR Unlicense')

# Nuklear is a single header. Its implementation is compiled once, in the
# browser's src/sdl/nk_impl.c, with the configuration in src/sdl/nk.h;
# this subproject only provides the include directory.
nuklear_dep = declare_dependency(
  include_directories: include_directories('.'),
)

meson.override_dependency('nuklear', nuklear_dep)
