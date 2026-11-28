project('dejavu-fonts', version: '2.37', license: 'Bitstream-Vera',
        meson_version: '>=1.1')

dejavu_version = meson.project_version()

# The fallback fonts of -Dbundled_fonts, compiled into the browser: a
# sans-serif in two weights, a serif and a monospace, about 2.1 MB together,
# for Latin, Greek, Cyrillic, Armenian, Georgian, Hebrew and Arabic in
# part, IPA, box drawing and many symbols. Their licence (Bitstream Vera,
# with the Arev glyphs' and the DejaVu changes in the public domain) is
# the release's LICENSE, reproduced in THIRD-PARTY-LICENSES.md.
dejavu_fonts = files(
  'ttf/DejaVuSans.ttf',
  'ttf/DejaVuSans-Bold.ttf',
  'ttf/DejaVuSerif.ttf',
  'ttf/DejaVuSansMono.ttf',
)
dejavu_license = files('LICENSE')
