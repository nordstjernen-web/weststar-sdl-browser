# Weststar SDL Browser

[![linux](https://github.com/nordstjernen-web/weststar-sdl-browser/actions/workflows/linux.yml/badge.svg)](https://github.com/nordstjernen-web/weststar-sdl-browser/actions/workflows/linux.yml)
[![windows](https://github.com/nordstjernen-web/weststar-sdl-browser/actions/workflows/windows.yml/badge.svg)](https://github.com/nordstjernen-web/weststar-sdl-browser/actions/workflows/windows.yml)

Weststar SDL Browser is a fork of the
[Northstar Web Browser](https://github.com/nordstjernen-web/northstar-web-browser)
in which the GTK 4 user interface and the Cairo 2D renderer are being
replaced by [SDL3](https://libsdl.org). The page engine is Northstar's
hand-written C engine: lexbor for HTML parsing, its own CSS cascade and
layout, QuickJS-ng for JavaScript, libcurl for the network. It runs on
Linux and Windows.

The current version is 1.0.0. It is licensed under the GNU General Public
License, version 3 or later.

![Weststar 1.0.0 on Windows 11, showing the start page](docs/images/weststar-windows.png)

## Status

The port is done in stages, each merged to `main` while it builds and
runs on Linux and Windows:

| Stage | What | State |
|-------|------|-------|
| 1 | Import Northstar without history, rebrand | done |
| 2 | Remove WebGL, WebGPU and `<video>` | done |
| 3 | Replace the GTK shell with an SDL3 shell, chrome drawn with Nuklear | done |
| 4 | Replace Cairo with an in-tree rasterizer drawing into SDL surfaces | in progress: `src/gfx/` draws the chrome, SVG, MathML and canvas 2D; the page painter and glyphs are next |

See [docs/porting-to-sdl.md](docs/porting-to-sdl.md) for the plan, what
has been removed and why, and what is left.

`src/gfx/` is Weststar's own 2D rasterizer: Cairo's API under a `gfx_`
prefix, with Cairo's pixel formats, so each part of the engine moves over
by renaming its calls. Until the move is complete, Cairo stays linked and a
small bridge lets code on either side call the other. Two checks guard the
move, both run by CI on Linux and Windows: `meson test -C builddir --suite
gfx` draws a set of scenes with gfx and with Cairo and compares the
pixels, and `data/fixtures/canvas-2d.html` is a page of canvas 2D checks
that each print PASS or FAIL.

What is deliberately **not** in Weststar for now:

- **WebGL 1/2 and WebGPU.** `canvas.getContext("webgl")` returns `null`.
- **`<video>`**, Media Source Extensions and camera capture. A `<video>`
  shows a dark box with its controls; `<audio>` still plays through SDL3's
  audio device.
- **Printing and PDF export** (`--export=pdf:`), removed for now while
  Cairo is replaced; they will come back without it.
- Parts of Northstar's GTK chrome not ported yet: the developer-tools
  window, address-bar suggestions and the downloads list (downloads
  report in the status bubble).

## Building

Weststar builds with meson and ninja. On Debian/Ubuntu:

```sh
sudo apt install build-essential pkg-config meson ninja-build \
    libsdl3-dev libcurl4-openssl-dev libssl-dev libharfbuzz-dev \
    libfribidi-dev libcairo2-dev libfontconfig-dev libfreetype-dev \
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
