# The SDL3 frontend: the window, its chrome (drawn with Nuklear) and the page
# view that drives the engine thread.
weststar_sdl_sources = files(
  '../appmain.c',
  '../libnorthstar.c',
  '../page_session.c',
  '../threaddump.c',
  'enginethread.c',
  'icons.c',
  'keys.c',
  'mainloop.c',
  'nkui.c',
  'pageview.c',
  'ui.c',
  'window.c',
)

# Nuklear's implementation is third-party code; it is built on its own,
# without the project's warnings.
weststar_nuklear_lib = static_library(
  'weststar-nuklear',
  files('nk_impl.c'),
  dependencies: libnuklear,
  c_args: cc.get_supported_arguments(['-w']),
)

weststar_sdl_platform_link_args = []

# muon compiles a pie: true executable with -fPIE but, unlike meson, does
# not link it with -pie, so with a compiler that does not default to PIE
# the browser would link position-dependent. Naming -pie here keeps muon
# builds PIE; meson then passes it twice, which changes nothing.
weststar_pie = host_system != 'windows'
if weststar_pie and host_system != 'darwin'
  weststar_sdl_platform_link_args += cc.get_supported_link_arguments(['-pie'])
endif

executable(
  'weststar',
  northstar_engine_sources + northstar_app_sources +
    weststar_sdl_sources + northstar_audio_sources +
    northstar_win_resources + ns_embedded_headers + [sb_key_h] +
    pdf_viewer_headers,
  dependencies: northstar_deps + [sdl3_dep, libnuklear, weststar_gfx_dep] +
    northstar_audio_deps,
  link_with: [northstar_audio_libs, weststar_nuklear_lib],
  include_directories: [include_directories('..', '.'), ns_embedded_inc],
  pie: weststar_pie,
  win_subsystem: northstar_win_subsystem,
  link_args: northstar_link_args + weststar_sdl_platform_link_args,
  install: true,
)

if host_system != 'windows'
  install_symlink(
    'open-internet-browser',
    pointing_to: 'weststar',
    install_dir: get_option('bindir'),
  )
endif
