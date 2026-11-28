# Headers that compile files into the binary (see ns_embeds in
# src/meson.build), plus pdfjs_fonts.h, the table over the embedded fonts,
# and ns_icons.h, the one over the chrome's icons.
if have_pdfjs
  pdfjs_fonts_conf = configuration_data()
  pdfjs_fonts_conf.set('PDFJS_FONT_INCLUDES', '\n'.join(pdfjs_font_includes))
  pdfjs_fonts_conf.set('PDFJS_FONT_ROWS', '\n'.join(pdfjs_font_rows))
  pdf_viewer_headers += configure_file(
    input: 'pdfjs_fonts.h.in',
    output: 'pdfjs_fonts.h',
    configuration: pdfjs_fonts_conf,
  )
endif

# With a C23 #embed compiler each header is a two-line #embed written at
# configure time, else scripts/embed-text.py writes it at build time
# (ns_have_embed, in the top-level meson.build).
ns_embedded_headers = []
foreach e : ns_embeds
  if ns_have_embed
    # fs.parent() gives a File's directory relative to the source root
    # (or absolute, for a -Dpdfjs_dir file); #embed needs an absolute path.
    embed_path = (meson.project_source_root() / fs.parent(e[1][0]) /
                  fs.name(e[1][0])).replace('\\', '/')
    embed_conf = configuration_data()
    embed_conf.set('SYMBOL', e[2])
    embed_conf.set('PATH', embed_path)
    ns_embedded_headers += configure_file(
      input: 'embed.h.in',
      output: e[0],
      configuration: embed_conf,
    )
  else
    ns_embedded_headers += custom_target(
      e[0],
      input: ['../../scripts/embed-text.py', e[1]],
      output: e[0],
      command: [python, '@INPUT0@', '@INPUT1@', '@OUTPUT@', e[2]],
    )
  endif
endforeach

# The icon table over the embedded icon headers.
ns_icons_conf = configuration_data()
ns_icons_conf.set('ICON_INCLUDES', '\n'.join(ns_icon_includes))
ns_icons_conf.set('ICON_ROWS', '\n'.join(ns_icon_rows))
ns_embedded_headers += configure_file(
  input: 'ns_icons.h.in',
  output: 'ns_icons.h',
  configuration: ns_icons_conf,
)
