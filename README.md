# Weststar SDL Browser

Weststar SDL Browser is a fork of the
[Northstar Web Browser](https://github.com/nordstjernen-web/northstar-web-browser)
in which the GTK 4 user interface and the Cairo 2D renderer are being
replaced by [SDL3](https://libsdl.org). The page engine is Northstar's
hand-written C engine: lexbor for HTML parsing, its own CSS cascade and
layout, QuickJS-ng for JavaScript, libcurl for the network.

It is licensed under the GNU General Public License, version 3 or later.

## Status

This is the start of the port. The work is done in stages, each merged to
`main` while it builds and runs:

| Stage | What | State |
|-------|------|-------|
| 1 | Import Northstar without history, rebrand | done |
| 2 | Remove WebGL, WebGPU and `<video>` | in progress |
| 3 | Replace the GTK shell with an SDL3 shell | in progress |
| 4 | Replace Cairo with an in-tree rasterizer drawing into SDL surfaces | planned |

See [docs/porting-to-sdl.md](docs/porting-to-sdl.md) for the plan, what
has been removed and why, and what is left.

What is deliberately **not** in Weststar for now:

- **WebGL 1/2 and WebGPU.** `canvas.getContext("webgl")` returns `null`.
- **`<video>`** and Media Source Extensions. `<audio>` still plays through
  SDL3's audio device.
- The GTK-only chrome: developer-tools window, print dialog, file dialogs.

## Building

Weststar builds with meson and ninja. On Debian/Ubuntu:

```sh
sudo apt install build-essential pkg-config meson ninja-build \
    libcurl4-openssl-dev libssl-dev libharfbuzz-dev libfribidi-dev \
    libcairo2-dev libfontconfig-dev libfreetype-dev libpsl-dev \
    libsqlite3-dev libseccomp-dev libsdl3-dev zlib1g-dev
meson setup builddir
meson compile -C builddir
./builddir/src/sdl/weststar https://example.org
```

SDL3 3.2 or newer is required. Distributions without an SDL3 package
(Ubuntu 24.04, Debian 12) can build it from the
[SDL release tarball](https://github.com/libsdl-org/SDL/releases) with
CMake and install it to `/usr/local`.

Headless mode is unchanged from Northstar and needs no display:

```sh
./builddir/src/sdl/weststar --headless --dump=text https://example.org
./builddir/src/sdl/weststar --headless --dump=png:page.png https://example.org
```

## Relationship to Northstar

Weststar keeps Northstar's internal names (`ns_` prefixes, file names such
as `libnorthstar.c`) so that fixes to the shared engine can be carried
between the two trees. User-visible names, the executable (`weststar`)
and the profile directories (`~/.config/weststar`, `~/.local/share/weststar`,
`~/.cache/weststar`) are Weststar's own, so the two browsers can be
installed side by side.

The other browsers of the same family are
[Nordstjernen Sol](https://github.com/nordstjernen-web/nordstjernen-sol-browser),
[Northstar](https://github.com/nordstjernen-web/northstar-web-browser) and
[Southstar](https://github.com/nordstjernen-web/southstar-browser).
