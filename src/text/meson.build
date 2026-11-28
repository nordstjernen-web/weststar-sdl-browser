# nstext — the text layout library without Pango (meson -Dtext=native):
# the ns_pango_* API the engine calls, on HarfBuzz, FriBidi and FreeType,
# with fonts from fontconfig or nsfc, drawn with gfx. No GLib, no GObject.
# Public surface: `nstext_dep`. See docs/text-layout.md.

nstext_conf = configuration_data()
nstext_thai_dep = dependency('libthai', version: '>= 0.1.9', required: false)
nstext_conf.set('HAVE_LIBTHAI', nstext_thai_dep.found())
nstext_conf.set('HAVE_TH_BRK_FIND_BREAKS',
                nstext_thai_dep.found() and
                cc.has_function('th_brk_find_breaks', dependencies: nstext_thai_dep))
configure_file(output: 'nstext-config.h', configuration: nstext_conf)

nstext_deps = [
  libm,
  sdl_dep,
  libfontconfig,
  libfreetype,
  weststar_gfx_dep,
  dependency('harfbuzz', version: ns_floors['harfbuzz']),
  dependency('fribidi', version: ns_floors['fribidi']),
  nstext_thai_dep,
]

nstext_lib = static_library('nstext',
  files(
    'nstext-attrs.c',
    'nstext-break.c',
    'nstext-ellipsize.c',
    'nstext-emoji.c',
    'nstext-font.c',
    'nstext-glyph-item.c',
    'nstext-itemize.c',
    'nstext-layout.c',
    'nstext-render.c',
    'nstext-shape.c',
    'nstext-util.c',
  ),
  dependencies: nstext_deps,
  include_directories: [include_directories('.'), include_directories('..')],
  c_args: ['-DNS_TEXT_NATIVE'],
)

nstext_dep = declare_dependency(
  link_with: nstext_lib,
  include_directories: include_directories('..'),
  compile_args: ['-DNS_TEXT_NATIVE'],
  dependencies: nstext_deps,
)
