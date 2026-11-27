# web-text-lib — the browser's text layout library, a Pango fork built in-tree.
# Public surface: `web_text_dep`, `web_text_gfx_dep`, `web_text_fc_dep`.

wtl_version = '1.58.2'.split('.')
wtl_major_version = wtl_version[0].to_int()
wtl_minor_version = wtl_version[1].to_int()
wtl_micro_version = wtl_version[2].to_int()
wtl_api_version = '@0@.0'.format(wtl_major_version)

wtl_conf = configuration_data()
wtl_conf.set('NS_PANGO_BINARY_AGE', wtl_minor_version * 100 + wtl_micro_version)
wtl_conf.set('NS_PANGO_INTERFACE_AGE',
             wtl_minor_version.is_odd() ? 0 : wtl_micro_version)
wtl_conf.set('NS_PANGO_VERSION_MAJOR', wtl_major_version)
wtl_conf.set('NS_PANGO_VERSION_MINOR', wtl_minor_version)
wtl_conf.set('NS_PANGO_VERSION_MICRO', wtl_micro_version)

# The project-wide warning, hardening and section-GC flags apply here too;
# these are web-text-lib's own on top of them. GLib's headers need
# -mms-bitfields on Windows.
wtl_cflags = [
  '-D_POSIX_C_SOURCE=200809L',
  '-D_POSIX_THREAD_SAFE_FUNCTIONS',
  '-D_GNU_SOURCE',
  '-DG_LOG_USE_STRUCTURED=1',
  '-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_80',
  '-DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_80',
]
wtl_extra_cflags = ['-w', '-fno-strict-aliasing']
if host_system == 'windows'
  wtl_cflags += '-D_WIN32_WINNT=0x0A00'
  wtl_extra_cflags += '-mms-bitfields'
elif host_system == 'linux'
  wtl_extra_cflags += ['-fno-semantic-interposition', '-fno-plt']
endif
wtl_cflags += cc.get_supported_arguments(wtl_extra_cflags)

if ns_is_release
  wtl_cflags += ['-DG_DISABLE_CAST_CHECKS', '-DNDEBUG']
else
  wtl_cflags += '-DNS_PANGO_ENABLE_DEBUG'
endif

wtl_override_options = {
  'c_std': 'gnu17',
  'optimization': '3',
  'warning_level': '0',
  'werror': false,
}

# SDL: the library's threads, locks and queues come from the browser's SDL
# layer (ns-glib-threads.h), not from GLib.
wtl_deps = [libm, glib, gobject, sdl3_dep,
  dependency('fribidi', version: ns_floors['fribidi']),
  dependency('harfbuzz', version: ns_floors['harfbuzz']),
]

wtl_thai_dep = dependency('libthai', version: '>= 0.1.9', required: false)
if wtl_thai_dep.found()
  wtl_conf.set('HAVE_LIBTHAI', 1)
  wtl_deps += wtl_thai_dep
  if cc.has_function('th_brk_find_breaks', dependencies: wtl_thai_dep)
    wtl_conf.set('HAVE_TH_BRK_FIND_BREAKS', 1)
  endif
endif

# fontconfig, or nsfc's headers in its place (see the top-level meson.build).
wtl_fontconfig_dep = libfontconfig
wtl_deps += wtl_fontconfig_dep
wtl_conf.set('HAVE_FC_CONFIG_SET_DEFAULT_SUBSTITUTE',
             have_system_fontconfig and
             cc.has_function('FcConfigSetDefaultSubstitute',
                             dependencies: wtl_fontconfig_dep,
                             prefix: '#include <fontconfig/fontconfig.h>'))

wtl_freetype_dep = dependency(['freetype2', 'freetype'])
wtl_deps += wtl_freetype_dep
wtl_conf.set('HAVE_FREETYPE', 1)
wtl_conf.set('HAVE_SYSPROF', false)

configure_file(output: 'config.h', configuration: wtl_conf)

wtl_root_inc = include_directories('..')

wtl_sources = files(
  'break.c',
  'ellipsize.c',
  'fonts.c',
  'glyphstring.c',
  'itemize.c',
  'pango-attributes.c',
  'pango-bidi-type.c',
  'pango-context.c',
  'pango-coverage.c',
  'pango-emoji.c',
  'pango-fontmap.c',
  'pango-fontset.c',
  'pango-fontset-simple.c',
  'pango-glyph-item.c',
  'pango-gravity.c',
  'pango-item.c',
  'pango-language.c',
  'pango-layout.c',
  'pango-matrix.c',
  'pango-renderer.c',
  'pango-script.c',
  'pango-tabs.c',
  'pango-utils.c',
  'shape.c',
  'ns-break-cache.c',
  'ns-item-cache.c',
  'ns-shape-cache.c',
)

wtl_features_conf = configuration_data()
wtl_features_conf.set('NS_PANGO_VERSION_MAJOR', wtl_major_version)
wtl_features_conf.set('NS_PANGO_VERSION_MINOR', wtl_minor_version)
wtl_features_conf.set('NS_PANGO_VERSION_MICRO', wtl_micro_version)
configure_file(
  input: 'pango-features.h.meson',
  output: 'pango-features.h',
  configuration: wtl_features_conf,
)

wtl_lib_cflags = wtl_cflags + [
  '-DG_LOG_DOMAIN="WebTextLib"',
  '-DNS_PANGO_COMPILATION',
]
wtl_inc = [include_directories('.'), wtl_root_inc]

wtl_lib = static_library('web-text-lib',
  wtl_sources,
  dependencies: wtl_deps,
  include_directories: wtl_inc,
  c_args: wtl_lib_cflags,
  override_options: wtl_override_options,
)
web_text_dep = declare_dependency(
  link_with: wtl_lib,
  include_directories: wtl_root_inc,
  dependencies: wtl_deps,
)

wtl_fc_lib = static_library('web-text-lib-fc',
  files(
    'pangofc-font.c',
    'pangofc-fontmap.c',
    'pangofc-decoder.c',
  ),
  dependencies: wtl_deps + web_text_dep,
  include_directories: wtl_inc,
  c_args: wtl_lib_cflags + '-DNS_PANGO_DISABLE_DEPRECATION_WARNINGS',
  override_options: wtl_override_options,
)
web_text_fc_dep = declare_dependency(
  link_with: wtl_fc_lib,
  include_directories: wtl_root_inc,
  dependencies: wtl_deps + web_text_dep,
)

# The backend drawing text with gfx, the browser's rasterizer: glyphs are
# rasterized with FreeType. It is the browser's own code, so it is built
# with the project's warning flags rather than the fork's.
wtl_gfx_lib = static_library('web-text-lib-gfx',
  files(
    'pangogfx-fontmap.c',
    'pangogfx-font.c',
    'pangogfx-render.c',
  ),
  dependencies: wtl_deps + [web_text_dep, web_text_fc_dep, weststar_gfx_dep],
  include_directories: wtl_inc,
  c_args: [
    '-DG_LOG_DOMAIN="WebTextLib"',
    '-DNS_PANGO_COMPILATION',
    '-DNS_PANGO_DISABLE_DEPRECATION_WARNINGS',
  ] + cc.get_supported_arguments(host_system == 'windows' ? ['-mms-bitfields'] : []),
)
web_text_gfx_dep = declare_dependency(
  link_with: wtl_gfx_lib,
  include_directories: wtl_root_inc,
  dependencies: wtl_deps + [web_text_dep, web_text_fc_dep, weststar_gfx_dep],
)
