project(
  'weststar',
  'c',
  version: '1.0.0',
  license: 'GPL-3.0-or-later',
  license_files: ['LICENSE', 'THIRD-PARTY-LICENSES.md'],
  default_options: {
    'c_std': 'gnu2x,c2x,gnu11,c11',
    'warning_level': '2',
    'quickjs-ng:warning_level': '0',
    'quickjs-ng:default_library': 'static',
    'buildtype': 'release',
    'optimization': '3',
    'b_lto': true,
    'b_ndebug': 'true',
    'b_pie': true,
  },
  meson_version: '>=1.3',
)

cc = meson.get_compiler('c')
fs = import('fs')
host_system = host_machine.system()

# Windows: the oldest version the build is for, _WIN32_WINNT as the
# compiler sees it. The Windows XP build (scripts/winxp-i686.ini) sets
# 0x0501, and then the sources call nothing newer than XP has.
ns_win32_winnt = 0
if host_system == 'windows'
  ns_win32_winnt = cc.compute_int('_WIN32_WINNT', prefix: '#include <windows.h>')
endif
ns_winxp = host_system == 'windows' and ns_win32_winnt < 0x0600

common_warning_flags = [
  '-Wshadow',
  '-Wpointer-arith',
  '-Wstrict-prototypes',
  '-Wmissing-prototypes',
  '-Wmissing-declarations',
  '-Wold-style-definition',
  '-Wcast-align',
  '-Wwrite-strings',
  '-Wformat=2',
  '-Wformat-security',
  '-Wnull-dereference',
  '-Wlogical-op',
  '-Wundef',
  '-Wvla',
  '-Winit-self',
  '-Wredundant-decls',
]

suppressed_warnings = [
  '-Wno-constant-logical-operand',
]

# A C library without level 3 (glibc before 2.34) treats it as level 2.
hardening_c_flags = [
  '-fstack-protector-strong',
  '-fstack-clash-protection',
  '-fcf-protection=full',
  '-U_FORTIFY_SOURCE',
  '-D_FORTIFY_SOURCE=3',
]

hardening_link_flags = [
  '-Wl,-z,relro',
  '-Wl,-z,now',
  '-Wl,-z,noexecstack',
  '-Wl,-z,separate-code',
]

add_project_arguments(
  cc.get_supported_arguments(common_warning_flags + suppressed_warnings +
                             hardening_c_flags),
  language: 'c',
)

add_project_link_arguments(
  cc.get_supported_link_arguments(hardening_link_flags),
  language: 'c',
)

ns_buildtype = get_option('buildtype')
ns_is_release = (ns_buildtype == 'release' or ns_buildtype == 'minsize')
# Every build profile gets -O3 (via default_options optimization=3) plus
# dead-section pruning. The browser leans on the optimizer to inline the
# lexbor parser walks, QuickJS interpreter dispatch, and our box-layout
# loops; -O2 left ~30% on the table for JS-heavy pages. The -O level is
# left to meson's optimization option so it isn't passed twice (which
# warns); we only add the section-GC flags here. Dev/non-release builds
# keep -g3 + frame pointers so step debugging still works under -O3.
add_project_arguments(
  cc.get_supported_arguments(['-ffunction-sections', '-fdata-sections']),
  language: 'c',
)
add_project_link_arguments(
  cc.get_supported_link_arguments(['-Wl,--gc-sections']),
  language: 'c',
)
if ns_is_release
  add_project_link_arguments(
    cc.get_supported_link_arguments(['-Wl,-s']),
    language: 'c',
  )
else
  add_project_arguments(
    cc.get_supported_arguments(['-g3', '-fno-omit-frame-pointer']),
    language: 'c',
  )
endif
# binutils ar on Windows has no LLVM plugin, so it archives clang's LTO
# bitcode without indexing a single symbol and every static library links as
# if it were empty. Meson picks llvm-ar for clang when it is installed.
if get_option('b_lto') and cc.get_id() == 'clang' and host_system == 'windows'
  llvm_ar = find_program('llvm-ar-' + cc.version().split('.')[0], 'llvm-ar',
                         required: false)
  if not llvm_ar.found()
    error('LTO with clang needs llvm-ar; install it (MSYS2: pacman -S ' +
          'mingw-w64-ucrt-x86_64-llvm-tools) or configure with -Db_lto=false')
  endif
endif

# muon hands GCC a bare -flto, under which GCC runs the link-time
# partitions of the browser one after another ("using serial compilation of
# N LTRANS jobs"); meson compiles with -flto=auto, which runs them in
# parallel. Naming it on the link line gives muon builds the parallel link
# too.
if get_option('b_lto') and cc.get_id() == 'gcc'
  add_project_link_arguments(
    cc.get_supported_link_arguments(['-flto=auto']),
    language: 'c',
  )
endif

ns_release_profile = get_option('b_lto') ? 'release (LTO + stripped + hardened)' : 'release (stripped + hardened)'
message('weststar build profile: ' +
        (ns_is_release ? ns_release_profile
                       : 'development (debug symbols + frame pointer + hardened)'))

# web-text-lib enforces the text-stack floors from inside its own build, where
# a too-old library surfaces as a bare version mismatch and only the first of
# several is reported. Checking every floor here first names them all at once.
# The floors are named once here and reused by every dependency() call on
# these libraries, here and in src/web-text-lib/.
ns_floors = {
  'glib-2.0': '>=2.80',
  'gobject-2.0': '>=2.80',
  'harfbuzz': '>=8.3',
  'fribidi': '>=1.0.6',
  'fontconfig': '>=2.15',
  'sdl3': '>=3.2.0',
  'sdl2': '>=2.0.18',
  'libcurl': '>=8.5',
}
ns_sdl = get_option('sdl')
ns_unmet_floors = []
foreach name, floor : ns_floors
  # Only the SDL that is asked for; SDL2 may come from the pinned
  # subproject (subprojects/sdl2.wrap).
  if name.startswith('sdl') and name != ns_sdl
    continue
  endif
  if name == 'sdl2' and get_option('wrap_mode') != 'nofallback'
    continue
  endif
  # fontconfig is only required when asked for: without it the fonts are
  # found by nsfc, in the tree.
  if name == 'fontconfig' and not get_option('fontconfig').enabled()
    continue
  endif
  # GLib and GObject may come from the pinned subproject instead.
  if get_option('glib') != 'system' and (name == 'glib-2.0' or name == 'gobject-2.0')
    continue
  endif
  if not dependency(name, version: floor, required: false).found()
    installed = dependency(name, required: false)
    ns_unmet_floors += '  @0@ @1@ (@2@)'.format(name, floor,
      installed.found() ? 'found ' + installed.version() : 'not found')
  endif
endforeach
if ns_unmet_floors.length() > 0
  error('Weststar SDL Browser needs newer system libraries than this machine has:\n' +
        '\n'.join(ns_unmet_floors) + '\n' +
        'Ubuntu 24.04, Debian 13 and Fedora 40 are the oldest releases of ' +
        'their distributions that ship all of them; see docs/building.md.')
endif

# SDL opens the window, takes input and plays audio: SDL3, or with
# -Dsdl=sdl2 SDL2 under SDL3's names (src/plat/ns_sdl.h, ns_sdl2.c), for the
# systems that have only SDL2. SDL2 is the system's if it is installed, else
# the release pinned in subprojects/sdl2.wrap, built statically; force that
# with --force-fallback-for=sdl2 (as for a system whose SDL2 is sdl2-compat,
# SDL2's API over SDL3).
if ns_sdl == 'sdl2'
  sdl_dep = dependency('sdl2', version: ns_floors['sdl2'],
                       fallback: ['sdl2', 'sdl2_dep'],
                       default_options: ['default_library=static',
                                         'test=false', 'werror=false',
                                         'warning_level=0'])
  add_project_arguments('-DNS_SDL2', language: 'c')
  # The bundled GLib's threads and clocks are SDL's too (packagefiles/glib/).
  meson.override_dependency('weststar-sdl', declare_dependency(
    dependencies: sdl_dep,
    compile_args: ['-DNS_SDL2'],
    include_directories: include_directories('src/plat'),
  ))
else
  sdl_dep = dependency('sdl3', version: ns_floors['sdl3'])
endif

# GLib and GObject: the system's, or with -Dglib=bundled the part of them
# the browser uses, built statically from the pinned release
# (subprojects/glib.wrap and packagefiles/glib/).
# With -Dglib=native neither: nscore (src/core/) takes GLib's place under
# its names, declared further down, once every project argument is set.
ns_glib = get_option('glib')
if ns_glib == 'native'
  if get_option('text') != 'native'
    error('-Dglib=native needs -Dtext=native: web-text-lib, the Pango fork ' +
          'of -Dtext=pango, is built on GLib and GObject')
  endif
  glib    = dependency('', required: false)
  gobject = dependency('', required: false)
elif ns_glib == 'bundled'
  glib_proj = subproject('glib')
  glib    = glib_proj.get_variable('glib_dep')
  gobject = glib_proj.get_variable('gobject_dep')
else
  glib    = dependency('glib-2.0', version: ns_floors['glib-2.0'])
  gobject = dependency('gobject-2.0', version: ns_floors['gobject-2.0'])
endif

# The chrome is drawn with Nuklear, vendored in subprojects/nuklear.
libnuklear = dependency('nuklear', fallback: ['nuklear', 'nuklear_dep'])
libcurl = dependency('libcurl', version: ns_floors['libcurl'])
if libcurl.version().version_compare('<8.11')
  warning('libcurl @0@ predates stable WebSocket support (curl 8.11): WebSocket will be unavailable at runtime unless this libcurl was built with the ws/wss protocols.'.format(libcurl.version()))
endif
# Web Crypto, random numbers and hashing (src/plat/ns_crypto.h) come from
# OpenSSL's libcrypto, or with -Dcrypto=mbedtls from Mbed TLS 3.6 LTS: the
# system's if installed, else the release pinned in subprojects/mbedtls.wrap,
# built statically. That build has no libcrypto; it lacks Ed25519 and Ed448.
crypto_backend = get_option('crypto')
if crypto_backend == 'openssl'
  libcrypto = dependency('libcrypto')
else
  libcrypto = dependency('mbedcrypto', version: ['>=3.6.0', '<4'],
                         fallback: ['mbedtls', 'mbedcrypto_dep'])
endif
libm = cc.find_library('m', required: false)

sqlite3 = dependency('sqlite3')

# Fonts are found through fontconfig where it is installed, and otherwise
# (or with -Dfontconfig=disabled) by nsfc, src/fc/: the part of fontconfig's
# API the text stack calls, over the platform's font folders. Its headers
# take the place of fontconfig's, and its sources are built into the
# browser (src/meson.build).
libfontconfig = dependency('fontconfig', version: ns_floors['fontconfig'],
                           required: get_option('fontconfig'))
have_system_fontconfig = libfontconfig.found()
if not have_system_fontconfig
  libfontconfig = declare_dependency(
    include_directories: include_directories('src/fc'),
  )
endif
libfreetype = dependency('freetype2')

# Fallback fonts compiled into the browser for nsfc to offer after the
# system's (src/fc/nsfc-bundled.c): auto wherever nsfc is built, from the
# DejaVu release pinned in subprojects/dejavu-fonts.wrap.
nsfc_bundled_fonts = []
bundled_fonts_source = 'none'
bundled_fonts_opt = get_option('bundled_fonts')
if have_system_fontconfig and bundled_fonts_opt.enabled()
  error('-Dbundled_fonts=enabled: the bundled fonts are offered by nsfc, ' +
        'which the system fontconfig replaces; set -Dfontconfig=disabled')
endif
if not have_system_fontconfig and bundled_fonts_opt.allowed()
  dejavu_sub = subproject('dejavu-fonts', required: bundled_fonts_opt)
  if dejavu_sub.found()
    nsfc_bundled_fonts = dejavu_sub.get_variable('dejavu_fonts')
    bundled_fonts_source = ('DejaVu ' + dejavu_sub.get_variable('dejavu_version') +
                            ' (subprojects/dejavu-fonts.wrap)')
  endif
endif

# A CA certificate file compiled into the browser as its own trust store,
# for systems without one or with one that is too old (src/net.c).
have_ca_bundle = get_option('ca_bundle') != ''
if have_ca_bundle
  if not fs.is_file(get_option('ca_bundle'))
    error('-Dca_bundle=@0@: no such file'.format(get_option('ca_bundle')))
  endif
  ca_bundle_file = files(get_option('ca_bundle'))
endif

libavif = dependency('libavif', required: get_option('avif'))
have_avif = libavif.found()

# PDF.js, embedded into the PDF viewer page at build time; see src/pdf.c.
# -Dpdfjs is optional (auto) or required (enabled). It comes from an
# installed copy named by -Dpdfjs_dir, which must then be a supported
# version, or else from the release pinned in subprojects/pdfjs-dist.wrap.
pdfjs_embedded = {}
pdfjs_standard_fonts = []
pdfjs_source = 'not built'
if not get_option('pdfjs').disabled()
  if get_option('pdfjs_dir') != ''
    # Both the pdfjs-dist npm layout (build/pdf.min.mjs, wasm/,
    # standard_fonts/) and the generic viewer layout distributions install
    # (build/pdf.mjs, web/wasm/, web/standard_fonts/) are accepted, minified
    # or not. The viewer (data/js/pdf-viewer.js) is written against 6.5 to 6.x.
    pdfjs_root = get_option('pdfjs_dir')
    if not fs.is_dir(pdfjs_root)
      error('-Dpdfjs_dir: @0@ is not a directory'.format(pdfjs_root))
    endif
    pdfjs_candidates = {
      'pdfjs_lib': ['build/pdf.min.mjs', 'build/pdf.mjs'],
      'pdfjs_worker': ['build/pdf.worker.min.mjs', 'build/pdf.worker.mjs'],
      'pdfjs_openjpeg': ['wasm/openjpeg.wasm', 'web/wasm/openjpeg.wasm'],
      'pdfjs_jbig2': ['wasm/jbig2.wasm', 'web/wasm/jbig2.wasm'],
      'pdfjs_qcms': ['wasm/qcms_bg.wasm', 'web/wasm/qcms_bg.wasm'],
    }
    foreach name, rels : pdfjs_candidates
      pdfjs_found = ''
      foreach rel : rels
        if pdfjs_found == '' and fs.is_file(pdfjs_root / rel)
          pdfjs_found = pdfjs_root / rel
        endif
      endforeach
      if pdfjs_found == ''
        error('-Dpdfjs_dir: @0@ has none of @1@'.format(pdfjs_root,
                                                        ', '.join(rels)))
      endif
      pdfjs_embedded += {name: files(pdfjs_found)}
    endforeach

    # Glyph data for PDF's standard fonts when a document does not embed
    # them. Only the BSD-licensed Foxit fonts (PDFium) are embedded; PDF.js's
    # Liberation Sans, under GPL-2.0 with a font exception, is left out, and
    # Helvetica/Arial then draw with a system sans-serif font.
    pdfjs_font_dir = ''
    foreach rel : ['standard_fonts', 'web/standard_fonts']
      if pdfjs_font_dir == '' and fs.is_dir(pdfjs_root / rel)
        pdfjs_font_dir = pdfjs_root / rel
      endif
    endforeach
    if pdfjs_font_dir == ''
      error('-Dpdfjs_dir: @0@ has neither standard_fonts nor web/standard_fonts'.format(pdfjs_root))
    endif
    foreach name : ['FoxitDingbats.pfb', 'FoxitFixed.pfb', 'FoxitFixedBold.pfb',
                    'FoxitFixedBoldItalic.pfb', 'FoxitFixedItalic.pfb',
                    'FoxitSerif.pfb', 'FoxitSerifBold.pfb',
                    'FoxitSerifBoldItalic.pfb', 'FoxitSerifItalic.pfb',
                    'FoxitSymbol.pfb']
      if not fs.is_file(pdfjs_font_dir / name)
        error('-Dpdfjs_dir: @0@ lacks the standard font @1@'.format(
          pdfjs_font_dir, name))
      endif
      pdfjs_standard_fonts += files(pdfjs_font_dir / name)
    endforeach

    # The library and worker each state "pdfjsVersion = X.Y.Z" in their
    # header comment; both must agree and fall in the supported range.
    pdfjs_versions = []
    foreach name : ['pdfjs_lib', 'pdfjs_worker']
      pdfjs_text = fs.read(pdfjs_embedded[name][0])
      if pdfjs_text.to_lower().contains('</script')
        error('-Dpdfjs_dir: @0@ contains "</script" and cannot be inlined in a script element'.format(name))
      endif
      pdfjs_parts = pdfjs_text.split('pdfjsVersion')
      if pdfjs_parts.length() < 2
        error('-Dpdfjs_dir: @0@ does not state a pdfjsVersion'.format(name))
      endif
      pdfjs_v = pdfjs_parts[1].split('\n')[0].split(',')[0].split(';')[0]
      pdfjs_v = pdfjs_v.strip().strip('=').strip().strip('"').strip()
      pdfjs_digits = pdfjs_v
      foreach c : ['0', '1', '2', '3', '4', '5', '6', '7', '8', '9']
        pdfjs_digits = pdfjs_digits.replace(c, '')
      endforeach
      if pdfjs_v.split('.').length() != 3 or pdfjs_digits != '..'
        error('-Dpdfjs_dir: @0@ states an unreadable pdfjsVersion "@1@"'.format(
          name, pdfjs_v))
      endif
      pdfjs_versions += pdfjs_v
    endforeach
    pdfjs_version = pdfjs_versions[0]
    if pdfjs_versions[1] != pdfjs_version
      error('-Dpdfjs_dir: PDF.js library @0@ and worker @1@ differ in version'.format(
        pdfjs_version, pdfjs_versions[1]))
    endif
    if not (pdfjs_version.version_compare('>=6.5.0') and
            pdfjs_version.version_compare('<7'))
      error('-Dpdfjs_dir: PDF.js @0@ in @1@ is outside the supported range 6.5.0 to 6.x'.format(
        pdfjs_version, pdfjs_root))
    endif
    pdfjs_source = pdfjs_version + ' from ' + pdfjs_root
  else
    pdfjs_sub = subproject('pdfjs-dist', required: get_option('pdfjs'))
    if pdfjs_sub.found()
      pdfjs_embedded = pdfjs_sub.get_variable('pdfjs_embedded')
      pdfjs_standard_fonts = pdfjs_sub.get_variable('pdfjs_standard_fonts', [])
      pdfjs_version = pdfjs_sub.get_variable('pdfjs_version', 'pinned')
      pdfjs_source = pdfjs_version + ' (subprojects/pdfjs-dist.wrap)'
    endif
  endif
endif
have_pdfjs = pdfjs_embedded.keys().length() > 0
if have_pdfjs and pdfjs_standard_fonts.length() == 0
  error('pdfjs-dist: no standard fonts; the extracted subproject predates ' +
        'them, delete subprojects/pdfjs-dist-*/ and set up again')
endif

# vorbisfile and opusfile are the high-level decode APIs over libvorbis and
# libopus + libogg, used by the in-process SDL audio mixer to decode
# Ogg/Vorbis and Ogg/Opus natively.
have_audio = get_option('audio').allowed()
libvorbisfile = dependency('vorbisfile', required: false)
libopusfile = dependency('opusfile', required: false)
# Spell checking through Enchant, where it is installed; not with
# -Dglib=native, since the system Enchant links GLib.
if ns_glib == 'native'
  libenchant = dependency('', required: false)
  message('spell checking (Enchant) left out: -Dglib=native links no GLib')
else
  libenchant = dependency('enchant-2', required: false)
endif
have_enchant = libenchant.found()

libseccomp = dependency('libseccomp', required: host_system == 'linux')
have_seccomp = libseccomp.found()

libwuffs = dependency('wuffs', fallback: ['wuffs', 'wuffs_dep'])
libplmpeg = dependency('plmpeg', fallback: ['plmpeg', 'plmpeg_dep'])
zlib = dependency('zlib')

# Brotli and zstd Content-Encoding; without them they are not advertised.
libbrotlidec = dependency('libbrotlidec', required: false)
libzstd = dependency('libzstd', required: false)

# A library built from its subproject (every one in the Windows XP build) is
# third-party code: its headers count as system headers, as an installed
# library's do, so that the browser's own warnings stay off them.
# (libcurl's subproject hands on the compression libraries' headers as its
# own, unmarked: they are named first, so they are system headers there.)
sqlite3 = sqlite3.as_system()
libfreetype = libfreetype.as_system()
zlib = zlib.as_system()
if libbrotlidec.found()
  # Its headers come with libbrotlicommon, which as_system() does not reach
  # through libbrotlidec.
  libbrotlidec = declare_dependency(dependencies: [
    dependency('libbrotlicommon', required: false).as_system(),
    libbrotlidec.as_system()])
endif
libzstd = libzstd.as_system()
libcurl = declare_dependency(dependencies: [zlib, libbrotlidec, libzstd,
                                            libcurl.as_system()])

# A system lexbor 3 is used when there is one, else the pinned subproject.
liblexbor = dependency('lexbor', version: '>=3.0.0',
                       fallback: ['lexbor', 'lexbor_dep'])

# Feature macros for the sources, each defined when its condition holds.
# src/media_types.c may advertise an audio type only where the decoder that
# plays it is built: the SDL in-process mixer.
ns_feature_macros = {
  'NS_HAVE_FONTCONFIG=1': libfontconfig.found(),
  'NS_FC_INTREE=1': not have_system_fontconfig,
  'NS_HAVE_CA_BUNDLE=1': have_ca_bundle,
  'NS_HAVE_FREETYPE=1': libfreetype.found(),
  'NS_HAVE_AVIF=1': have_avif,
  'NS_HAVE_PDFJS=1': have_pdfjs,
  'NS_HAVE_VORBISFILE=1': libvorbisfile.found(),
  'NS_HAVE_OPUSFILE=1': libopusfile.found(),
  'NS_AUDIO_PLAYBACK=1': have_audio,
  'NS_AUDIO_NATIVE_VORBIS=1': have_audio and libvorbisfile.found(),
  'NS_AUDIO_NATIVE_OPUS=1': have_audio and libopusfile.found(),
  'NS_HAVE_ENCHANT=1': have_enchant,
  'NS_HAVE_SECCOMP=1': have_seccomp,
  'NS_HAVE_BROTLI_DEC=1': libbrotlidec.found(),
  'NS_HAVE_ZSTD=1': libzstd.found(),
}
foreach macro, enabled : ns_feature_macros
  if enabled
    add_project_arguments('-D' + macro, language: 'c')
  endif
endforeach
add_project_arguments('-DNS_LEXBOR_VERSION="' + liblexbor.version() + '"',
                      language: 'c')

build_date = get_option('build_date')
if build_date == ''
  date_cmd = run_command('date', '-u', '+%Y-%m-%d', check: false)
  if date_cmd.returncode() == 0
    build_date = date_cmd.stdout().strip()
  endif
endif
add_project_arguments('-DNS_BUILD_DATE="' + build_date + '"', language: 'c')
add_project_arguments('-DNS_VERSION="' + meson.project_version() + '"', language: 'c')

js_engine = get_option('js_engine')
if js_engine == 'quickjs'
  libquickjs = subproject('quickjs').get_variable('quickjs_dep')
  add_project_arguments('-DNS_QUICKJS_ORIGINAL=1',
                        '-DNS_QUICKJS_VERSION="' + libquickjs.version() + '"',
                        language: 'c')
else
  libquickjs = dependency('quickjs-ng', fallback: ['quickjs-ng', 'qjs_dep'])
endif

# WAMR's call stubs cover x86-64 and 64-bit Arm (not Arm Windows).
wasm_feature = get_option('wasm').require(
  host_machine.cpu_family() == 'x86_64' or
    (host_machine.cpu_family() == 'aarch64' and host_system != 'windows'),
  error_message: 'wasm (WAMR) is built only for x86-64 and 64-bit Arm ' +
                 '(not Arm Windows); build with -Dwasm=disabled')
have_wasm = wasm_feature.allowed()
if have_wasm
  # WAMR is the upstream release pinned in subprojects/wamr.wrap. A subproject
  # does not inherit this project's arguments, so the hardening and
  # code-generation flags are handed to it.
  wamr_parent_c_args = cc.get_supported_arguments(
    hardening_c_flags + ['-ffunction-sections', '-fdata-sections'])
  if not ns_is_release
    wamr_parent_c_args += cc.get_supported_arguments(
      ['-g3', '-fno-omit-frame-pointer'])
  endif
  libwamr = subproject('wamr',
    default_options: {'parent_c_args': wamr_parent_c_args},
  ).get_variable('libwamr')
else
  libwamr = declare_dependency()
endif

# Every file in these two directories is installed, so a new icon or
# translation needs no change here.
install_subdir(
  'data/icons/hicolor/scalable/apps',
  install_dir: get_option('datadir') / 'icons' / 'hicolor' / 'scalable',
)

install_data(
  'LICENSE',
  'THIRD-PARTY-LICENSES.md',
  install_dir: get_option('datadir') / 'weststar',
)

install_data(
  'data/safebrowsing.list',
  install_dir: get_option('datadir') / 'weststar',
)

install_subdir(
  'data/i18n',
  install_dir: get_option('datadir') / 'weststar',
)

install_data(
  'data/weststar.desktop',
  install_dir: get_option('datadir') / 'applications',
  rename: 'org.weststar.SdlBrowser.desktop',
)

install_data(
  'data/org.weststar.SdlBrowser.metainfo.xml',
  install_dir: get_option('datadir') / 'metainfo',
)

summary({
  'WebAssembly (WAMR)': have_wasm,
  'PDF viewer (PDF.js)': pdfjs_source,
  'AVIF images (libavif)': have_avif,
  'Audio playback (SDL)': have_audio,
  'SDL': ns_sdl + ' ' + sdl_dep.version(),
  'JavaScript engine': js_engine + ' ' + libquickjs.version(),
  'Crypto library': crypto_backend + ' ' + libcrypto.version(),
  'Built-in CA certificates': have_ca_bundle ? get_option('ca_bundle') : 'none',
  'GLib': ns_glib == 'native' ? 'none (nscore, in tree)' : ns_glib + ' ' + glib.version(),
  'Fonts found by': have_system_fontconfig ? 'fontconfig ' + libfontconfig.version() : 'nsfc (in tree)',
  'Text layout': get_option('text') == 'native' ? 'nstext (in tree, no Pango or GObject)' : 'web-text-lib (in-tree Pango fork)',
  'Bundled fallback fonts': bundled_fonts_source,
  'Windows XP': ns_winxp,
}, section: 'Features')

# Files are compiled into the binary (src/embedded/, and nsfc's bundled
# fonts in src/fc/) with C23 #embed where the compiler has it (GCC 15,
# Clang 19 and later): each header is a two-line #embed written at configure
# time, no generator runs, and the compiler records the file in the
# dependency file, so editing it rebuilds only what includes it. Older
# compilers get the same array spelled out byte by byte by
# scripts/embed-text.py at build time.
ns_have_embed = cc.compiles('''
static const char ns_embed_probe[] = {
#embed "@0@" suffix(, 0) if_empty(0)
};
int ns_embed_probe_len(void);
int ns_embed_probe_len(void) { return (int)sizeof ns_embed_probe; }
'''.format((meson.project_source_root() / 'LICENSE').replace('\\', '/')),
  werror: true,
  name: 'C23 #embed')

# Text is shaped through web-text-lib (src/web-text-lib/), the in-tree Pango
# fork that caches finished glyph strings across layouts; every symbol in it
# is renamed, because GTK loads the system Pango into the same process. It
# declares targets, so it comes after the last project argument. It draws
# with gfx (src/gfx/), the browser's rasterizer, which comes first.
subdir('src/gfx')
subdir('src/plat')
subdir('src/fc')
# nscore, the core library that stands in for GLib with -Dglib=native.
subdir('src/core')
if ns_glib == 'native'
  glib = nscore_dep
endif
# -Dtext=native: nstext (src/text/), the same API without Pango or GObject.
ns_text = get_option('text')
if ns_text == 'native'
  subdir('src/text')
  pango       = nstext_dep
  pangogfx    = dependency('', required: false)
  libpangoft2 = dependency('', required: false)
  ns_engine_gobject = dependency('', required: false)
else
  subdir('src/web-text-lib')
  pango       = web_text_dep
  pangogfx    = web_text_gfx_dep
  libpangoft2 = web_text_fc_dep
  ns_engine_gobject = gobject
endif

subdir('src')

# Prints the layout and a hash of the pixels of ellipsized and justified
# text (the middle ellipsis too, which CSS does not reach), with whichever
# text library is built; the outputs of a pango and a native build should
# be the same (docs/text-layout.md).
executable('text-layout-dump',
  files(
    'src/text/tests/text-layout-dump.c',
    'src/plat/ns_env.c',
    'src/plat/ns_fs.c',
    'src/plat/ns_plat.c',
    'src/plat/ns_sdl2.c',
    'src/plat/ns_sys.c',
  ),
  dependencies: [pango, pangogfx, weststar_gfx_dep, sdl_dep, libm] + ns_plat_test_win_libs,
  include_directories: include_directories('src'),
  pie: host_machine.system() != 'windows',
  build_by_default: false,
)
