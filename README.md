# Weststar SDL Browser

[![linux](https://github.com/nordstjernen-web/weststar-sdl-browser/actions/workflows/linux.yml/badge.svg)](https://github.com/nordstjernen-web/weststar-sdl-browser/actions/workflows/linux.yml)
[![windows](https://github.com/nordstjernen-web/weststar-sdl-browser/actions/workflows/windows.yml/badge.svg)](https://github.com/nordstjernen-web/weststar-sdl-browser/actions/workflows/windows.yml)

Weststar SDL Browser is a fork of the
[Northstar Web Browser](https://github.com/nordstjernen-web/northstar-web-browser)
in which GTK 4, Cairo and Pango have been replaced. The window, input,
audio and platform services come from [SDL](https://libsdl.org): SDL3, or
SDL2 for older systems. Pages and chrome are drawn by Weststar's own
rasterizer, and text is laid out by Weststar's own text library on
HarfBuzz, FriBidi and FreeType. The page engine is Northstar's
hand-written C engine: lexbor for HTML parsing, its own CSS cascade and
layout, QuickJS-ng for JavaScript, and libcurl for the network. It runs
on Linux and Windows, and it builds for Windows XP. The aim is a browser
that runs wherever SDL does, including the Amiga family (AmigaOS 4,
MorphOS, AROS).

The current version is 1.0.0. It is licensed under the GNU General Public
License, version 3 or later.

![Weststar 1.0.0 on Windows 11, showing its start page](docs/images/weststar-windows.png)

## Status

The port is done in stages. Each stage is merged to `main` once CI has
built and tested it on Linux and Windows:

| Stage | What | State |
|-------|------|-------|
| 1 | Import Northstar without history, rebrand | done |
| 2 | Remove WebGL, WebGPU and `<video>` | done |
| 3 | Replace the GTK shell with an SDL shell, chrome drawn with Nuklear | done |
| 4 | Replace Cairo with an in-tree rasterizer drawing into SDL surfaces | done: Cairo is not used at all |
| 5 | Composite with SDL textures: scrolling, fixed boxes and `transform`/`opacity` animations moved by the GPU | done for fixed boxes and animations; `sticky` boxes, canvases and animated images are left |
| 6 | SDL as the platform: threads, the main loop, files, fonts, crypto, SDL2, Windows XP | done, except for a run on XP itself and an Amiga-family build |
| 6a | Replace Pango with Weststar's own text layout (nstext) | done and the default: Pango and GObject are not linked |
| 6b | Replace GLib with a small in-tree core library (nscore, `src/core/`) | done, opt-in with `-Dglib=native`: no GLib is linked, and pages come out identical; the default stays the system's GLib for now |

See [docs/porting-to-sdl.md](docs/porting-to-sdl.md) for the plan, what
has been removed and why, and what is left.

The main in-tree parts are:

- **`src/gfx/`** is Weststar's own 2D rasterizer. It has Cairo's API
  under a `gfx_` prefix and Cairo's pixel formats, which an SDL texture
  takes as they are.
- **`src/text/`** (nstext) is Weststar's own text layout. It provides
  the `ns_pango_*` API the engine calls, written on HarfBuzz, FriBidi and
  FreeType.
  - Line breaking, bidi, ellipsis, justification and hit testing follow
    Pango, so pages lay out as they did before.
  - Glyphs are rasterized with FreeType into gfx.
  - The Pango fork it replaced (`src/web-text-lib/`, `-Dtext=pango`) is
    kept for a while as the reference that CI compares nstext against,
    in layout and in pixels. See
    [docs/text-layout.md](docs/text-layout.md).
- **`src/plat/`** is the platform layer over SDL: threads, locks, the
  event loop, files, the environment, system information and crypto.
- **`src/fc/`** (nsfc) finds fonts where fontconfig is not available. It
  has DejaVu fonts compiled in for systems that have none.

Tests and checks:

- `meson test -C builddir` runs the rasterizer, platform, font and text
  tests. The rasterizer's scenes are compared with reference images that
  were checked against Cairo.
- `meson test -C builddir --benchmark --verbose` times page-like
  workloads.
- `data/fixtures/` holds pages for headless checks. Some, such as
  `canvas-2d.html` and `webcrypto.html`, print PASS or FAIL for each
  check.

CI runs these on Linux (with SDL3 and with SDL2), on Windows, and for the
Windows XP build.

What is deliberately **not** in Weststar for now:

- **WebGL 1/2 and WebGPU.** `canvas.getContext("webgl")` returns `null`.
- **`<video>`**, Media Source Extensions and camera capture. A `<video>`
  shows a dark box with its controls; `<audio>` still plays through SDL's
  audio device.
- **Printing and PDF export** (`--export=pdf:`), removed with Cairo; they
  will come back without it.
- Parts of Northstar's GTK chrome not ported yet: the developer-tools
  window, address-bar suggestions and the downloads list (downloads
  report in the status bubble).

## Size

Lines of C in `src/` (counted with `wc -l`, so comments and blank lines
included), on 28 November 2026:

| Part | Where | Lines |
|------|-------|------:|
| Page engine: HTML, CSS, layout, painting, JavaScript bindings, network | `src/*.c`, `src/*.h` | 210,654 |
| Text layout and glyph rendering (nstext), with its Unicode tables | `src/text/` | 20,626 |
| Platform layer over SDL: threads, event loop, files, system, crypto | `src/plat/` | 7,909 |
| SDL shell: window, chrome, page view | `src/sdl/` | 6,514 |
| 2D rasterizer | `src/gfx/` | 5,536 |
| Audio | `src/audio/` | 3,685 |
| Font finder (nsfc) | `src/fc/` | 3,060 |
| Tests and benchmark | `src/*/tests/` | 3,673 |
| **Total** | | **261,657** |

The Pango fork in `src/web-text-lib/` (48,344 lines) is not counted. It
is no longer built by default and will be removed.

Besides the C, `data/js/` holds 12,820 lines of JavaScript and CSS
(polyfills, the PDF viewer, internal pages). The vendored libraries below
are not counted.

## Dependencies

Libraries the build needs from the system:

| Library | For |
|---------|-----|
| [SDL3](https://libsdl.org) ≥ 3.2, or SDL2 ≥ 2.0.18 with `-Dsdl=sdl2` | the window, input, audio output, threads, files |
| GLib ≥ 2.80 | strings, lists, hash tables and Unicode functions; not needed with `-Dglib=native`, which uses nscore (`src/core/`) instead |
| [libcurl](https://curl.se) ≥ 8.5 | HTTP, HTTPS, WebSocket |
| OpenSSL (libcrypto), or Mbed TLS with `-Dcrypto=mbedtls` | Web Crypto, random numbers, hashing |
| HarfBuzz ≥ 8.3, FriBidi ≥ 1.0.6 | text shaping, bidirectional text |
| FreeType | font metrics and rasterizing glyphs |
| fontconfig ≥ 2.15, optional | finding fonts; without it (`-Dfontconfig=disabled`) the browser finds them itself |
| SQLite | IndexedDB storage |
| zlib | gzip, deflate, PNG output |
| libseccomp (Linux only) | the engine's system-call sandbox |

Optional, used when found: libavif (AVIF images), libvorbisfile and
libopusfile (Ogg audio), libbrotlidec and libzstd (compressed responses),
Enchant (spell checking), and PDF.js (the PDF viewer; `-Dpdfjs`).

Vendored in the tree or fetched by meson as subprojects:
[lexbor](https://github.com/lexbor/lexbor) (HTML parsing, URLs),
[QuickJS-ng](https://github.com/quickjs-ng/quickjs) (JavaScript),
[WAMR](https://github.com/bytecodealliance/wasm-micro-runtime)
(WebAssembly), [Wuffs](https://github.com/google/wuffs) (PNG, APNG, GIF,
BMP, JPEG and WebP decoding), [Nuklear](https://github.com/Immediate-Mode-UI/Nuklear)
(the chrome), [pl_mpeg](https://github.com/phoboslab/pl_mpeg) and
[minimp3](https://github.com/lieff/minimp3) (audio decoding), and the
[Public Suffix List](https://publicsuffix.org/) (which cookies a site may
set, and which hosts are one site; `src/psl-table.h`, generated from a
pinned version of the list). SDL2,
Mbed TLS, libcurl, GLib, the DejaVu fonts and the other libraries in
`subprojects/` are also fetched this way when a build asks for them or
the system lacks them. GTK, Cairo, Pango
and GObject are not used.
[THIRD-PARTY-LICENSES.md](THIRD-PARTY-LICENSES.md) has each licence.

## Building

Weststar builds with meson and ninja. On Debian/Ubuntu:

```sh
sudo apt install build-essential pkg-config meson ninja-build \
    libsdl3-dev libcurl4-openssl-dev libssl-dev libharfbuzz-dev \
    libfribidi-dev libfontconfig-dev libfreetype-dev \
    libsqlite3-dev libseccomp-dev libglib2.0-dev zlib1g-dev
meson setup builddir
meson compile -C builddir
./builddir/src/sdl/weststar https://example.org
```

On Windows, Weststar builds in an MSYS2 UCRT64 shell with the same
commands and the binary is `builddir/src/sdl/weststar.exe`;
[docs/building.md](docs/building.md#windows-msys2) lists the packages and
how to bundle the DLLs it needs to run outside MSYS2.

SDL3 3.2 or newer is the default; GTK is not needed. Distributions
without an SDL3 package (Ubuntu 24.04, Debian 12) can build SDL3 from the
[SDL release tarball](https://github.com/libsdl-org/SDL/releases) with
CMake, or build Weststar against SDL2 with `meson setup -Dsdl=sdl2`.

Options for smaller and older systems:

| Option | Effect |
|--------|--------|
| `-Dsdl=sdl2` | builds against SDL2 instead of SDL3 |
| `-Dcrypto=mbedtls` | Web Crypto without OpenSSL |
| `-Dfontconfig=disabled` | fonts are found by nsfc, with DejaVu compiled in |
| `-Dglib=native` | no GLib: nscore, the in-tree core library, takes its place |
| `-Dglib=bundled` | builds the part of GLib that is used from source |
| `-Dca_bundle=<file>` | compiles the certificates in that file into the browser |

The Windows XP build uses all of them. It is one 32-bit `weststar.exe`,
built in MSYS2's MINGW32 environment with
`--native-file scripts/winxp-i686.ini`, with every library linked in
statically. CI builds it and checks that it imports only what XP SP3
has. It has not been run on XP itself yet.

[docs/building.md](docs/building.md) has the commands and every option.

## The window

The SDL shell (`src/sdl/`) opens one SDL window per browser window. The
page is rendered on the engine thread and uploaded to an SDL texture;
when the engine reports damaged rectangles, only those are uploaded.
Fixed boxes, and elements whose animations change only `transform` and
`opacity`, are each painted once into a layer of their own. SDL draws
these layers over the page every frame, so scrolling and these
animations repaint nothing. `NS_LAYERS=0` turns layers off. The chrome is
built every frame with [Nuklear](https://github.com/Immediate-Mode-UI/Nuklear),
an immediate-mode GUI library: back, forward, reload/stop, home, the
address bar, a bookmark star and a menu; a find bar (Ctrl+F), a context
menu, and a status bubble for hovered links and messages. Nuklear's draw
commands are rendered by `src/gfx/`, Weststar's own 2D rasterizer, with
text from the engine's own text stack, so the chrome shows any script the
page can.

Keyboard: Ctrl+L / F6 address bar, Ctrl+F find, F3 / Ctrl+G next match,
Alt+Left / Alt+Right back and forward, F5 / Ctrl+R reload, Ctrl+D
bookmark, Ctrl+N new window, Ctrl+W close, Ctrl+Q quit, Ctrl+= / Ctrl+- /
Ctrl+0 zoom, F11 full screen.

Headless mode needs no display. It is Northstar's, without PDF export
(`weststar --help` lists the options):

```sh
./builddir/src/sdl/weststar --headless --dump=text https://example.org
./builddir/src/sdl/weststar --headless --dump=png:page.png https://example.org
```

## Relationship to Northstar

Weststar keeps Northstar's internal names (`ns_` prefixes, file names such
as `libnorthstar.c`) so that fixes to the shared engine can be carried
between the two trees. User-visible names, the executable (`weststar`)
and the profile directories are Weststar's own (`~/.config/weststar`,
`~/.local/share/weststar` and `~/.cache/weststar` on Linux; folders named
`weststar` under the user's local application data on Windows), so the
two browsers can be installed side by side.

The other browsers of the same family are
[Nordstjernen Sol](https://github.com/nordstjernen-web/nordstjernen-sol-browser),
[Northstar](https://github.com/nordstjernen-web/northstar-web-browser) and
[Southstar](https://github.com/nordstjernen-web/southstar-browser).
