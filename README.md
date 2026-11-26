# Weststar SDL Browser

[![linux](https://github.com/nordstjernen-web/weststar-sdl-browser/actions/workflows/linux.yml/badge.svg)](https://github.com/nordstjernen-web/weststar-sdl-browser/actions/workflows/linux.yml)
[![windows](https://github.com/nordstjernen-web/weststar-sdl-browser/actions/workflows/windows.yml/badge.svg)](https://github.com/nordstjernen-web/weststar-sdl-browser/actions/workflows/windows.yml)

Weststar SDL Browser is a fork of the
[Northstar Web Browser](https://github.com/nordstjernen-web/northstar-web-browser)
in which the GTK 4 user interface and the Cairo 2D renderer have been
replaced: the window is [SDL3](https://libsdl.org), and pages, chrome and
text are drawn by Weststar's own rasterizer. The page engine is Northstar's
hand-written C engine: lexbor for HTML parsing, its own CSS cascade and
layout, QuickJS-ng for JavaScript, libcurl for the network. It runs on
Linux and Windows.

The current version is 1.0.0. It is licensed under the GNU General Public
License, version 3 or later.

![Weststar 1.0.0 on Windows 11, showing its start page](docs/images/weststar-windows.png)

## Status

The port is done in stages, each merged to `main` while it builds and
runs on Linux and Windows:

| Stage | What | State |
|-------|------|-------|
| 1 | Import Northstar without history, rebrand | done |
| 2 | Remove WebGL, WebGPU and `<video>` | done |
| 3 | Replace the GTK shell with an SDL3 shell, chrome drawn with Nuklear | done |
| 4 | Replace Cairo with an in-tree rasterizer drawing into SDL surfaces | done: Cairo is not used at all |
| 5 | Composite with SDL textures: scrolling, fixed boxes and `transform`/`opacity` animations moved by the GPU | next |

See [docs/porting-to-sdl.md](docs/porting-to-sdl.md) for the plan, what
has been removed and why, and what is left.

`src/gfx/` is Weststar's own 2D rasterizer: Cairo's API under a `gfx_`
prefix, with Cairo's pixel formats, which an SDL texture takes as they
are. Text is laid out by web-text-lib (a Pango fork) and its glyphs are
rasterized with FreeType into gfx (`src/web-text-lib/pangogfx-*`).
`meson test -C builddir --suite gfx` draws a set of scenes and compares
them with reference images that were checked against Cairo,
`meson test -C builddir --benchmark --verbose` times page-like
workloads, and `data/fixtures/canvas-2d.html` is a page of canvas 2D
checks that each print PASS or FAIL. CI runs the tests on Linux and
Windows.

What is deliberately **not** in Weststar for now:

- **WebGL 1/2 and WebGPU.** `canvas.getContext("webgl")` returns `null`.
- **`<video>`**, Media Source Extensions and camera capture. A `<video>`
  shows a dark box with its controls; `<audio>` still plays through SDL3's
  audio device.
- **Printing and PDF export** (`--export=pdf:`), removed with Cairo; they
  will come back without it.
- Parts of Northstar's GTK chrome not ported yet: the developer-tools
  window, address-bar suggestions and the downloads list (downloads
  report in the status bubble).

## Size

Lines of C in `src/` (counted with `wc -l`, so comments and blank lines
included), on 26 November 2026:

| Part | Where | Lines |
|------|-------|------:|
| Page engine: HTML, CSS, layout, painting, JavaScript bindings, network | `src/*.c`, `src/*.h` | 208,385 |
| Text layout and glyph rendering (a Pango fork) | `src/web-text-lib/` | 48,256 |
| SDL3 shell: window, chrome, page view | `src/sdl/` | 6,080 |
| 2D rasterizer | `src/gfx/` | 5,536 |
| Audio | `src/audio/` | 3,682 |
| Rasterizer tests and benchmark | `src/gfx/tests/` | 1,156 |
| **Total** | | **273,095** |

Besides the C, `data/js/` holds 12,820 lines of JavaScript and CSS
(polyfills, the PDF viewer, internal pages). The vendored libraries below
are not counted.

## Dependencies

Libraries the build needs from the system:

| Library | For |
|---------|-----|
| [SDL3](https://libsdl.org) ≥ 3.2 | the window, input, audio output |
| GLib, GObject, GIO ≥ 2.80 | the main loop, threads, resources |
| [libcurl](https://curl.se) ≥ 8.5 | HTTP, HTTPS, WebSocket |
| OpenSSL (libcrypto) | Web Crypto, hashing |
| HarfBuzz ≥ 8.3, FriBidi ≥ 1.0.6 | text shaping, bidirectional text |
| fontconfig ≥ 2.15, FreeType | finding fonts, rasterizing glyphs |
| libpsl | public-suffix rules for cookies |
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
[minimp3](https://github.com/lieff/minimp3) (audio decoding), and
web-text-lib (the Pango fork). GTK and Cairo are not used.
[THIRD-PARTY-LICENSES.md](THIRD-PARTY-LICENSES.md) has each licence.

## Building

Weststar builds with meson and ninja. On Debian/Ubuntu:

```sh
sudo apt install build-essential pkg-config meson ninja-build \
    libsdl3-dev libcurl4-openssl-dev libssl-dev libharfbuzz-dev \
    libfribidi-dev libfontconfig-dev libfreetype-dev \
    libpsl-dev libsqlite3-dev libseccomp-dev libglib2.0-dev zlib1g-dev
meson setup builddir
meson compile -C builddir
./builddir/src/sdl/weststar https://example.org
```

On Windows, Weststar builds in an MSYS2 UCRT64 shell with the same
commands and the binary is `builddir/src/sdl/weststar.exe`;
[docs/building.md](docs/building.md#windows-msys2) lists the packages and
how to bundle the DLLs it needs to run outside MSYS2.

SDL3 3.2 or newer is required; GTK is not. Distributions without an SDL3
package (Ubuntu 24.04, Debian 12) can build it from the
[SDL release tarball](https://github.com/libsdl-org/SDL/releases) with
CMake; [docs/building.md](docs/building.md) has the commands.

## The window

The SDL shell (`src/sdl/`) opens one SDL window per browser window. The
page is rendered on the engine thread and uploaded to an SDL texture,
only the damaged rectangles when the engine reports them. The chrome is
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

Headless mode needs no display. It is Northstar's, without PDF export:

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
