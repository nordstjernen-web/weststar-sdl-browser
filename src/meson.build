python = import('python').find_installation()

qjs_tools_found = false
if js_engine == 'quickjs-ng' and not meson.is_cross_build()
  qjsc_prog = find_program('qjsc', required: false)
  qjs_prog  = find_program('qjs',  required: false)
  qjs_tools_found = qjsc_prog.found() and qjs_prog.found()
endif

if qjs_tools_found
  # Build-time check of data/js/polyfills.js: qjsc compiles it to catch syntax
  # errors, then qjs loads it and runs scripts/verify-polyfills.js to catch a
  # missing or broken API. Either failing fails the build; the outputs are
  # stamps so the check reruns only when an input changes.
  polyfills_syntax = custom_target(
    'polyfills-syntax',
    input: '../data/js/polyfills.js',
    output: 'polyfills.qjsc.c',
    command: [qjsc_prog, '-C', '-P', '-n', 'polyfills.js',
              '-o', '@OUTPUT@', '@INPUT@'],
    build_by_default: true,
  )
  polyfills_verify = custom_target(
    'polyfills-verify',
    input: ['../data/js/polyfills.js', '../scripts/verify-polyfills.js'],
    output: 'polyfills.verified',
    command: [qjs_prog, '-I', '@INPUT0@', '@INPUT1@'],
    capture: true,
    depends: polyfills_syntax,
    build_by_default: true,
  )
endif

# Files compiled into the binary, each as a NUL-terminated
# `static const char <symbol>[]` in its own generated header:
# [header, file, symbol].
ns_embeds = [
  ['polyfills.h', files('../data/js/polyfills.js'), 'ns_js_polyfills_src'],
  ['wpt_hook.h', files('../data/js/wpt-hook.js'), 'ns_wpt_hook_src'],
  ['typed_om.h', files('../data/js/typed-om.js'), 'ns_js_typed_om_src'],
]

# The Google Safe Browsing API key, read from NS_SAFE_BROWSING_API_KEY at
# build time into a generated header so it never sits in the tree or on a
# compiler command line. Empty when unset: only the local list is used.
sb_key_h = custom_target(
  'ns_sb_key.h',
  input: '../scripts/gen-safebrowsing-key.py',
  output: 'ns_sb_key.h',
  command: [python, '@INPUT@', '@OUTPUT@'],
  build_always_stale: true,
)

# PDF.js and the viewer script, embedded as C strings for src/pdf.c.
pdf_viewer_embedded = {}
if have_pdfjs
  pdf_viewer_embedded = pdfjs_embedded + {
    'pdf_viewer_js': files('../data/js/pdf-viewer.js'),
    'pdf_viewer_css': files('../data/js/pdf-viewer.css'),
  }
endif
pdf_viewer_headers = []
foreach name, src : pdf_viewer_embedded
  ns_embeds += [[name + '.h', src, 'ns_' + name]]
endforeach
if have_pdfjs
  # Each standard font is embedded like any other file (with the trailing
  # NUL left out of the length), and pdfjs_fonts.h is a table over them,
  # written at configure time.
  pdfjs_font_includes = []
  pdfjs_font_rows = []
  foreach font : pdfjs_standard_fonts
    i = pdfjs_font_rows.length()
    ns_embeds += [['pdfjs_font_@0@.h'.format(i), [font],
                   'ns_pdfjs_font_@0@'.format(i)]]
    pdfjs_font_includes += '#include "pdfjs_font_@0@.h"'.format(i)
    pdfjs_font_rows += ('    { "@0@", (const unsigned char *)ns_pdfjs_font_@1@,' +
                        ' sizeof ns_pdfjs_font_@1@ - 1 },').format(
      fs.name(font), i)
  endforeach
endif

# The CA certificates of -Dca_bundle, the browser's own trust store.
if have_ca_bundle
  ns_embeds += [['ca_bundle.h', ca_bundle_file, 'ns_ca_bundle_pem']]
endif

# The chrome's SVG icons (src/sdl/icons.c), embedded like any other file,
# and ns_icons.h, a table over them by name, written at configure time.
ns_icon_names = [
  'weststar', 'weststar-back', 'weststar-forward', 'weststar-home',
  'weststar-reload', 'weststar-stop', 'weststar-print', 'weststar-downloads',
  'weststar-bookmarks', 'weststar-bookmarks-saved', 'weststar-console',
  'weststar-settings', 'weststar-about', 'weststar-stage-connect',
  'weststar-stage-fetch', 'weststar-stage-parse', 'weststar-stage-script',
  'weststar-stage-render', 'weststar-stage-done',
]
ns_icon_includes = []
ns_icon_rows = []
foreach name : ns_icon_names
  icon_symbol = 'ns_icon_' + name.underscorify()
  ns_embeds += [['icon_@0@.h'.format(name.underscorify()),
                 files('../data/icons/hicolor/scalable/apps' / name + '.svg'),
                 icon_symbol]]
  ns_icon_includes += '#include "icon_@0@.h"'.format(name.underscorify())
  ns_icon_rows += '    { "@0@", @1@, sizeof @1@ - 1 },'.format(name, icon_symbol)
endforeach

# The generated headers are written to their own build directory,
# src/embedded/: an earlier build made headers of the same names in src/ at
# build time, and after a reconfigure meson deletes what were outputs of the
# old build graph, a configure_file() of the same name included.
subdir('embedded')
ns_embedded_inc = include_directories('embedded')

northstar_engine_sources = files(
  'anim.c',
  'bytecode_cache.c',
  'bookmarks.c',
  'cache.c',
  'config.c',
  'csp.c',
  'css.c',
  'css_calc.c',
  'css_media.c',
  'css_prop_syntax.c',
  'css_syntax.c',
  'datetime.c',
  'debuglog.c',
  'damage.c',
  'dom.c',
  'download_name.c',
  'encoding.c',
  'engine.c',
  'mainctx.c',
  'eventsource.c',
  'ext.c',
  'fetch_policy.c',
  'forms.c',
  'camera.c',
  'cancellable.c',
  'mime_guess.c',
  'font.c',
  'history.c',
  'html.c',
  'i18n.c',
  'image.c',
  'smil.c',
  'svg.c',
  'task.c',
  'image_ico.c',
  'image_wuffs.c',
  'idb.c',
  'js.c',
  'js_canvas.c',
  'js_intl.c',
  'js_date.c',
  'js_encoding.c',
  'js_media.c',
  'js_perf.c',
  'js_realm.c',
  'js_receiver.c',
  'js_webcrypto.c',
  'quickjs_compat.c',
  'regex.c',
  'layers.c',
  'layout.c',
  'mathml.c',
  'media_types.c',
  'net.c',
  'netutil.c',
  'content_decode.c',
  'paint.c',
  'pdf.c',
  'plat/ns_env.c',
  'plat/ns_fs.c',
  'plat/ns_job.c',
  'plat/ns_loop.c',
  'plat/ns_plat.c',
  'plat/ns_sdl2.c',
  'plat/ns_sys.c',
  'render.c',
  'safebrowsing.c',
  'safebrowsing_google.c',
  'security.c',
  'shared_buffer.c',
  'selection.c',
  'spellcheck.c',
  'sqlite_db.c',
  'texture.c',
  'trace.c',
  'wasm.c',
  'webaudio.c',
  'webcrypto.c',
  'ws.c',
  'html_lexbor.c',
  'xml.c',
)

# Toolkit-free helpers used by the desktop executable but not the engine UI:
# the headless render driver and the crash watchdog. Kept in src/ as common
# code; the SDL frontend itself lives in src/sdl/.
northstar_app_sources = files(
  'headless.c',
  'watchdog.c',
)

if have_avif
  northstar_engine_sources += files('image_avif.c')
endif

northstar_engine_sources += files('plat/ns_crypto_' + crypto_backend + '.c')

# nsfc, the in-tree font finder, where the system's fontconfig is not used.
if not have_system_fontconfig
  northstar_engine_sources += nsfc_sources
endif

northstar_win_resources = []
if host_system == 'windows'
  windows = import('windows')
  northstar_win_resources = windows.compile_resources(
    'weststar.rc',
    depend_files: files('../data/icons/weststar.ico'),
  )
endif

# A dependency that was not found is ignored, so optional libraries are
# listed unconditionally.
northstar_deps = [
  sdl_dep,
  glib,
  gobject,
  pango,
  pangogfx,
  libpangoft2,
  libcurl,
  libcrypto,
  libm,
  libpsl,
  libquickjs,
  libwuffs,
  libplmpeg,
  liblexbor,
  libwamr,
  sqlite3,
  zlib,
  libbrotlidec,
  libzstd,
  libseccomp,
  libavif,
  libenchant,
  libfontconfig,
  libfreetype,
]
northstar_win_subsystem = host_system == 'windows' ? 'windows' : 'console'

northstar_link_args = []
if host_system == 'windows'
  northstar_link_args += cc.get_supported_link_arguments(
    ['-Wl,--stack,8388608'],
  )
  # Random numbers from BCrypt, or on Windows XP, which has none, from
  # advapi32's RtlGenRandom (security.c).
  if not ns_winxp
    northstar_deps += cc.find_library('bcrypt')
  endif
  northstar_deps += [
    cc.find_library('advapi32'),
    cc.find_library('shell32'),
    cc.find_library('user32'),
    cc.find_library('comdlg32'),
  ]
elif host_system == 'linux'
  northstar_link_args += cc.get_supported_link_arguments(
    ['-Wl,-z,stack-size=2097152'],
  )
elif host_system == 'netbsd'
  # NetBSD pkgsrc gnutls (pulled in transitively) carries an undefined
  # reference to libc's mbrtoc32, which the hardened --no-undefined link
  # rejects even though the dynamic linker resolves it at runtime. Allow
  # undefined symbols originating from shared libraries on NetBSD only; our
  # own objects are still checked.
  northstar_link_args += cc.get_supported_link_arguments(
    ['-Wl,--allow-shlib-undefined'],
  )
endif

# Optional in-process audio playback over SDL, with minimp3 and pl_mpeg
# decoding MP3 and MPEG-1 audio and, where present, vorbisfile and opusfile
# decoding Ogg.
northstar_audio_sources = files('audio/stub.c')
northstar_audio_deps = []
northstar_audio_libs = []
if have_audio
  northstar_audio_sources = files('audio/audio.c')
  northstar_audio_deps = [sdl_dep, libplmpeg, libvorbisfile, libopusfile,
                          libm]
  northstar_audio_libs = static_library(
    'minimp3',
    files('audio/minimp3_impl.c'),
    include_directories: include_directories('audio'),
    c_args: cc.get_supported_arguments(['-w']),
  )
else
  message('in-process audio playback disabled (-Daudio=disabled)')
endif

subdir('sdl')
