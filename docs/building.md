# Building and running Weststar SDL Browser

Weststar builds with **meson + ninja** and a C compiler (GCC or Clang).
It is C only. It is built and tested on Linux and on Windows (MSYS2
UCRT64), both in CI; SDL3 also runs on macOS and the BSDs, where Weststar
has not been built yet (see [porting-to-sdl.md](porting-to-sdl.md)).

## Linux dependencies

Debian 13 / Ubuntu 25.04 or newer:

```sh
sudo apt install build-essential git pkg-config meson ninja-build \
    libsdl3-dev libcurl4-openssl-dev libssl-dev \
    libharfbuzz-dev libfribidi-dev libfontconfig-dev \
    libfreetype-dev libpsl-dev libsqlite3-dev libseccomp-dev \
    libglib2.0-dev zlib1g-dev
```

Fedora:

```sh
sudo dnf install gcc git pkgconf meson ninja-build SDL3-devel \
    libcurl-devel openssl-devel harfbuzz-devel fribidi-devel \
    fontconfig-devel freetype-devel libpsl-devel \
    sqlite-devel libseccomp-devel glib2-devel zlib-devel
```

`libseccomp` is required on Linux — `meson setup` fails without it.

The text stack has version floors set by web-text-lib: GLib ≥ 2.80,
HarfBuzz ≥ 8.3, fontconfig ≥ 2.15 and FriBidi ≥ 1.0.6, with FreeType;
fontconfig is optional (see `fontconfig` under Build options);
SDL3 must be ≥ 3.2 and libcurl ≥ 8.5. `meson setup` checks every floor
and lists each library that is missing or too old in a single error.
With a libcurl older than 8.11 the build warns that WebSocket is
unavailable unless that libcurl was built with WebSocket support.

GTK is not needed. The window chrome is drawn with
[Nuklear](https://github.com/Immediate-Mode-UI/Nuklear), vendored in
`subprojects/nuklear/`.

**Optional, auto-detected:** `libavif-dev` (AVIF images),
`libenchant-2-dev` (+ a dictionary such as `hunspell-en-us`) for
on-screen spell-checking, `libopusfile-dev` / `libvorbis-dev` for native
Ogg Opus/Vorbis decode in the audio mixer, `libthai-dev` for Thai word
breaking, and `libbrotli-dev` and `libzstd-dev` for Brotli and zstd
`Content-Encoding`. The build works without them.

### Installing SDL3 from source

Ubuntu 24.04 and Debian 12 have no SDL3 package. Build the release
tarball with CMake and install it to `/usr/local`:

```sh
sudo apt install cmake libx11-dev libxext-dev libxrandr-dev libxcursor-dev \
    libxi-dev libxfixes-dev libxss-dev libwayland-dev libxkbcommon-dev \
    libegl-dev libgl-dev libpulse-dev libasound2-dev
curl -LO https://github.com/libsdl-org/SDL/releases/download/release-3.2.26/SDL3-3.2.26.tar.gz
tar xzf SDL3-3.2.26.tar.gz
cmake -S SDL3-3.2.26 -B sdl3-build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF
sudo ninja -C sdl3-build install
```

## Windows (MSYS2)

Weststar builds natively on Windows in an [MSYS2](https://www.msys2.org)
**UCRT64** shell, with GCC or Clang. Install the toolchain and libraries:

```sh
pacman -S --needed git mingw-w64-ucrt-x86_64-{gcc,clang,lld,pkgconf,meson,ninja}     mingw-w64-ucrt-x86_64-{sdl3,glib2,harfbuzz,fribidi,fontconfig,freetype}     mingw-w64-ucrt-x86_64-{curl,openssl,ca-certificates,libpsl,sqlite3,zlib,brotli,zstd}     mingw-w64-ucrt-x86_64-{libavif,opusfile,libvorbis}
```

then build as below; the binary is `builddir/src/sdl/weststar.exe`. To run
it outside the MSYS2 shell, put it in a folder with the UCRT64 DLLs it
links (`ldd weststar.exe`) and `/ucrt64/etc/fonts` as `etc/fonts`: the
`windows` workflow (`.github/workflows/windows.yml`) builds exactly that
bundle and checks that it starts with only the Windows system folders on
`PATH`.

## Visual Studio Code

The repository's `.vscode/` workspace builds, runs and debugs the browser
on all three platforms; [vscode.md](vscode.md) walks through the setup.

## Build

```sh
meson setup builddir
meson compile -C builddir
./builddir/src/sdl/weststar
```

`meson setup` resolves two upstream projects through
`subprojects/*.wrap` — lexbor and the JavaScript engine (one of the two
QuickJS rows below) — and exposes them as the `liblexbor` / `libquickjs`
dependencies:

| Dependency | Pinned to | Resolution |
|------------|-----------|------------|
| [lexbor](https://github.com/lexbor/lexbor) — HTML parser and WHATWG URL module | master commit `145872a` (after v3.0.1) | A system lexbor ≥ 3.0.0 is used when `pkg-config` finds one; otherwise the wrap is cloned and built as a static library by `subprojects/packagefiles/lexbor/meson.build`, which stands in for lexbor's CMake build. |
| [quickjs-ng](https://github.com/quickjs-ng/quickjs) — JavaScript | master commit `147d01c` | A system quickjs-ng first, of any version; the wrap as fallback. |
| [QuickJS](https://github.com/bellard/quickjs) — JavaScript, with `-Djs_engine=quickjs` | the `2026-06-04` release commit | Always the subproject, built by the meson file in `subprojects/packagefiles/quickjs/`; upstream ships none. See [quickjs.md](quickjs.md). |

`-Djs_engine=quickjs` builds on Fabrice Bellard's original QuickJS instead
of quickjs-ng; [quickjs.md](quickjs.md) covers how it is built, the adapter
and the differences between the two.

WAMR, Wuffs, pl_mpeg, minimp3 and web-text-lib (`src/web-text-lib/`, the
Pango fork the engine lays text out with, shared with Nordstjernen Sol)
are vendored in-tree and need no network. web-text-lib is trimmed to what
the engine calls; a new caller may need a function restored from upstream
Pango, renamed with `src/web-text-lib/ns-rename.py`.
No in-tree fork of any browser engine is carried.

So the first build needs network access for whichever of those two it
cannot satisfy locally, unless the system supplies them. A tarball that must build offline has to embed them;
`debian/README.source` carries the recipe.

A quickjs-ng build also compiles `data/js/polyfills.js` with quickjs-ng's
`qjs` and `qjsc` (the subproject's, or the system's alongside a system
quickjs-ng) as a syntax and sanity check (`scripts/verify-polyfills.js`).
A `-Djs_engine=quickjs` build skips the check: that subproject builds no
tools, and a `qjsc` from Bellard's QuickJS on the `PATH` takes different
options.

`./scripts/dev.sh build` runs `meson setup` (only when needed) and
`meson compile -C builddir` in one step.

`meson test -C builddir --suite gfx` checks the in-tree rasterizer
(`src/gfx/`): it draws a set of scenes (fills, strokes, dashes,
gradients, images, clips, groups, masks, every operator) and compares
each with its reference image in `src/gfx/tests/ref/`. The references
were made while gfx was checked against Cairo scene by scene; after an
intended change to gfx's output, regenerate them with
`GFX_TEST_WRITE_REF=1` and look at the new images (`GFX_TEST_OUT=<dir>`
writes each scene as a PNG). `meson test -C builddir --benchmark
--verbose` times page-like workloads.

## Building with muon

[muon](https://muon.build) is an implementation of meson written in C99.
It builds the browser from the same build files, with the same options,
and has a ninja implementation (samurai) built in, so neither meson nor
ninja is needed. The other dependencies are the ones listed above;
Python 3 is still needed for a build-time script. Building with muon is
tested on Linux only.

```sh
muon setup builddir
muon -C builddir samu
./builddir/src/sdl/weststar
```

Options are given as with meson (`-Daudio=disabled`, `-Db_lto=false`);
meson's `--werror` is `-Dwerror=true`. `muon -C builddir install` installs,
and `-d <dir>` installs into a staging directory.

muon needs its libcurl and libarchive support to fetch and unpack the
pinned tarballs (WAMR, Wuffs and PDF.js). `muon version` lists both under
"enabled features" when muon was built with them. The
[muon CI job](../.github/workflows/muon.yml) builds muon 0.7.1 from
source this way, then builds and smoke-tests the browser with it.

Where muon 0.7.1 differs from meson:

- muon applies a wrap's `diff_files` only when the wrap also has a
  `patch_directory`. `subprojects/quickjs-ng.wrap` names one,
  `packagefiles/quickjs-ng/`, for that reason. A quickjs-ng checkout
  muon made before that lacks every quickjs-ng patch, and the link fails
  on `JS_GetCallerRealmOrNull` and other functions the patches add;
  `scripts/check-subprojects.sh` names the missing patches. Delete
  `subprojects/quickjs-ng/` and set up again.
- muon compiles the browser with `-fPIE` but links it without `-pie`, and
  links a GCC LTO build with a bare `-flto`, which runs the link-time
  partitions one at a time. The build files add `-pie` and `-flto=auto`
  to the browser's link themselves, so it is PIE and links in parallel,
  as with meson. quickjs-ng's `qjs` and `qjsc`, built for the polyfill
  check, still link one partition at a time, and GCC warns "using serial
  compilation" for each.
- A muon built with its libpkgconf backend can miss every system library
  on Debian and Ubuntu, because that backend does not search the
  multiarch `pkg-config` directory: `muon setup` then reports GLib, SDL3
  and the rest as not found although `pkg-config` finds them. Configure
  with `-Dmuon.pkgconfig=exec`, or build muon with `-Dlibpkgconf=disabled`.
- `muon install` has no `--skip-subprojects`; muon's meson command-line
  layer accepts the option and ignores it. A muon install therefore also
  installs quickjs-ng's `qjs`, `qjsc`, static library, headers and
  `pkg-config` file, which the distribution packages leave out. Delete
  them from the staging directory, or package from a meson build.

## Updating a pinned dependency

Editing the `revision =` line in a wrap is not enough on its own: meson
keeps the checkout it already has. Move the pin, then reset the checkout
to it.

```sh
$EDITOR subprojects/lexbor.wrap          # revision = <new tag or commit>
meson subprojects update --reset lexbor
meson compile -C builddir
```

`--reset` hard-resets the subproject to the wrap's revision and works on
the shallow clones these wraps ask for, in either direction. Deleting
`subprojects/<name>/` and reconfiguring does the same thing more slowly.

Local patches ride on top of the fetched sources, named by
`diff_files` in the wraps and living in `subprojects/packagefiles/`: a
bounds check for lexbor, a Windows link fix for quickjs-ng, and, for the
original QuickJS, a sort fix that makes `Array.prototype.sort` always call
its comparator. Regenerate
them against the new sources when a pin moves, or drop one upstream has
taken. A system lexbor or quickjs-ng does not get them.

WAMR and Wuffs are tag archives pinned by SHA-256 in `subprojects/wamr.wrap`
and `subprojects/wuffs.wrap`. Meson extracts them, applies the patches in
`diff_files` (`wamr-northstar.patch`, the local interpreter changes, and
`wuffs-jpeg-16bit-dqt.patch`), and copies the overlay from
`subprojects/packagefiles/wamr/` (the build file and `ns_wamr.c`, the narrow
accessors the WebAssembly JS API needs) or `packagefiles/wuffs/`. To move a
pin, change `directory`, `source_url`, `source_filename` and `source_hash`,
delete the old extracted directory, and refresh the patch.

The vendored copies are updated by replacing the files. minimp3
(`src/audio/minimp3.h`) is byte-identical to its upstream release, so
syncing it is a copy.
pl_mpeg carries one Northstar Web Browser change — a bounds fix for half-pel motion
compensation, which read a row past the plane — marked in place in the
vendored header; keep it when syncing.
[`../THIRD-PARTY-LICENSES.md`](../THIRD-PARTY-LICENSES.md) lists every
component and every patch, and is the file to update when any of this
moves.

Verify a dependency move the way any other change is verified: a clean
`meson compile` with no new warnings, `./scripts/dev.sh smoke` for layout
drift, and the browser launched on the paths the dependency touches. A
text-layout change wants more than that, because the shaping cache can be
wrong only for the second paragraph that shares a word with the first:
dump the layout of every page in `data/render-tests/` with the cache off
and on, require the two sets to be identical, then check that
`NS_PANGO_SHAPE_CACHE=verify` reports no mismatch over the same pages.

```sh
for f in data/render-tests/*.html; do
    n=$(basename "$f" .html)
    NS_PANGO_SHAPE_CACHE=0 ./builddir/src/sdl/weststar --headless \
        --dump=layout "$f" > "/tmp/off-$n.txt"
    ./builddir/src/sdl/weststar --headless --dump=layout "$f" > "/tmp/on-$n.txt"
    cmp -s "/tmp/off-$n.txt" "/tmp/on-$n.txt" || echo "differs: $n"
done
```

## Fast iteration

`ccache` is the biggest build-time win and meson picks it up
automatically; a warm-cache rebuild drops from ~35 s to ~1 s. Install it
once (`apt install ccache` / `dnf install ccache`). Optionally use the
`lld` linker for faster final links (`CC_LD=lld meson setup builddir`).

## Meson options

| Option | Default | Effect |
|--------|---------|--------|
| `wasm` | `auto` | Build the WebAssembly JS API over vendored WAMR. `auto` turns it off on 32-bit x86, which WAMR does not support; disable it on other platforms WAMR does not support. |
| `js_engine` | `quickjs-ng` | JavaScript engine: `quickjs-ng`, or `quickjs` for Fabrice Bellard's original QuickJS. See [quickjs.md](quickjs.md). |
| `audio` | `auto` | Enable in-process audio playback (needs SDL3 3.2 or newer). |
| `crypto` | `openssl` | The crypto library behind Web Crypto, random numbers and Safe Browsing hashes: `openssl` (OpenSSL's libcrypto from the system), or `mbedtls` for the portable build profile, Mbed TLS 3.6 from the system or else fetched and built as a subproject (`subprojects/mbedtls.wrap`). The `mbedtls` build lacks Ed25519 and Ed448, which Web Crypto then reports as not supported. HTTPS is libcurl's either way. |
| `fontconfig` | `auto` | Find fonts through the system's fontconfig. `auto` uses it when it is installed (2.15 or newer); `disabled`, or a system without it, builds nsfc instead (`src/fc/`), which reads the platform's font folders itself with FreeType: `/usr/share/fonts`, `/usr/local/share/fonts`, the `fonts` folder of each `XDG_DATA_DIRS` entry, `~/.local/share/fonts` and `~/.fonts`; `%WINDIR%\Fonts` and the per-user font folder on Windows; the three `Library/Fonts` folders on macOS; then a `fonts` folder beside the executable and the folders named in `NS_FONT_PATH`. It needs no configuration files, and keeps what it found in `fonts.cache` in the browser's cache folder. `enabled` makes fontconfig required. See [porting-to-sdl.md](porting-to-sdl.md). |
| `ca_bundle` | empty | A file of CA certificates in PEM format, such as [Mozilla's set from curl.se](https://curl.se/docs/caextract.html), to compile into the browser as its own trust store for HTTPS. It is used where the system has no certificate file, and wherever the environment has `NS_CA_BUNDLE=builtin`, for a system whose own root certificates are too old. (`NS_CA_BUNDLE` may also name a certificate file to use, in any build.) The certificates are as old as the build: a browser built this way needs rebuilding to learn of new ones. |
| `glib` | `system` | Where GLib and GObject come from. `system`: the installed libraries (2.80 or newer). `bundled`: the part of them the browser uses (containers, strings, the Unicode tables, key files, checksums, logging, GObject's type system), built statically from the GLib release pinned in `subprojects/glib.wrap` by `subprojects/packagefiles/glib/meson.build`; the browser then needs no GLib installed, nor PCRE2, libffi or libintl. For Linux, the BSDs and Windows (the Windows CI build uses it); not macOS yet. (A distribution's HarfBuzz may load the system's GLib for its own use all the same.) |
| `avif` | `auto` | Decode AVIF through libavif. Disabling drops a full AV1 decoder; AVIF images then fail to decode. |
| `pdfjs` | `auto` | The PDF viewer, built on PDF.js and embedded at build time. `auto` builds it when PDF.js is available; `enabled` makes PDF.js a required dependency, so the build fails without it; `disabled` leaves the viewer out, and a PDF shows a link to the file. |
| `pdfjs_dir` | *(empty)* | Directory of an installed PDF.js to use instead of the pinned release, such as Arch's `/usr/share/pdf.js`. See below. |
| `build_date` | *(configure date)* | Build-date stamp shown in the About dialog. |

Set with `-Dname=value`, e.g. `meson setup builddir -Dwasm=disabled`.

### Where PDF.js comes from

PDF.js is never part of this repository. Without `pdfjs_dir`, meson fetches
the release pinned in `subprojects/pdfjs-dist.wrap` (by version and
SHA-256) into the git-ignored `subprojects/pdfjs-dist-<version>/`. With
`pdfjs_dir`, it uses an installed copy instead, which
`meson.build` vets at setup: it must hold the library, the
worker, the three WebAssembly image decoders and the Foxit standard fonts
(the BSD-licensed glyph data for PDFs that name a standard font without
embedding it), in either the pdfjs-dist
layout (`build/pdf.min.mjs`, `wasm/`, `standard_fonts/`) or the generic
viewer layout distributions install (`build/pdf.mjs`, `web/wasm/`,
`web/standard_fonts/`), and be PDF.js 6.5
or a later 6.x — the range the viewer in `data/js/pdf-viewer.js` is written
for. A directory that fails the check fails the build, so a distribution
package cannot silently ship a viewer against an untested PDF.js.

```sh
meson setup builddir -Dpdfjs=enabled -Dpdfjs_dir=/usr/share/pdf.js   # Arch: pacman -S pdfjs
```

`data/fixtures/compressed-text.pdf`, written by `scripts/gen-pdf-fixture.py`,
is the viewer's smoke test: its page content is compressed and holds
more text than PDF.js sends in one chunk, so the Linux CI job's text dump
shows its last marker only when the worker inflates the page and the
text stream delivers every chunk.

Debian and Ubuntu's `libjs-pdf` (2.14) is too old; Fedora, MSYS2 and
Homebrew have no package, so those builds use the pinned release. To move
the pin, change `directory`, `source_url`, `source_filename` and
`source_hash` in the wrap and the version in
`subprojects/packagefiles/pdfjs-dist/meson.build`, delete the old
`subprojects/pdfjs-dist-*/`, check the new release against the viewer,
and raise the `version_compare` range in the `pdfjs_dir` block of `meson.build` if the
supported range moves.

## Command line

`weststar [options] [URL | FILE]` opens the start page, or the given URL
or local file. A normal launch runs under a watchdog process that restarts
the browser, with its session, after a crash or hang.

| Option | Effect |
|--------|--------|
| `--version` | Print the version and exit. |
| `--private` | Keep history, cookies, cache and site storage off disk for this session. |
| `--proxy=URL` | Use an HTTP(S) proxy. |
| `--ignore-certificate-errors` | Open pages with a self-signed or otherwise untrusted TLS certificate, such as home lab servers, after a warning dialog. The page is marked not secure; its own subresources, `fetch()` calls and WebSockets to the same host load too, and other hosts stay fully verified. See [security-model.md](security-model.md#network). |
| `--window-size=WxH` | Initial window size. |
| `--render-driver=NAME` | SDL render driver for the window: `software`, `opengl`, `opengles2`, `vulkan` or `gpu`. The default, `auto`, lets SDL choose and draws in software on a Linux machine without a GPU. `SDL_RENDER_DRIVER` in the environment takes precedence. |
| `--no-watchdog` | Run without the watchdog. |
| `--print-config` | Print the effective configuration and exit. |
| `--trace=FILE` | Write a Chrome trace-event JSON of frame phases, relayouts, scripts, fetches and decodes, for Perfetto (ui.perfetto.dev) or `chrome://tracing`. Works in the GUI and headless. |
| `--` | End of options: every argument after it is a URL or file, never an option. The URL and file handlers are registered this way (`weststar -- "%1"`), so a URL passed in by another program cannot add options. |

## Headless mode (scripting / testing)

The browser can render without a display, which is how the fixtures are
exercised and how behaviour can be scripted. Any of `--headless`,
`--dump=`, `--export=`, `--eval=`, `--act=`, `--inspect=`, `--inspect-at=`
or `--wpt` selects it; headless runs are never supervised by the watchdog.

```sh
# Dump a page to PNG
./builddir/src/sdl/weststar --headless \
    --url="https://example.com/" --dump="png:/tmp/out.png" --viewport=1000

# Evaluate JavaScript against a loaded page and print the result
./builddir/src/sdl/weststar --headless \
    --url="file://$HOME/Downloads/page.html" --eval="document.title"
```

| Option | Effect |
|--------|--------|
| `--url=URL`, or a bare URL or file path | The page to load. |
| `--dump=text\|dom\|layout\|none` | Print the page text (the default), the DOM, the box tree, or nothing. |
| `--dump=png:FILE` | Write a screenshot. This also writes `FILE-initial.png`, captured at the first render; the named file follows after `--time-ms`. |
| `--viewport=W` or `WxH`, `--viewport-height=H` | Viewport size in CSS pixels (default width 1000). |
| `--settle-ms=N` | Time to let the page settle before output (default 200). |
| `--time-ms=N` | Page time between the first and final screenshot (default 1000). |
| `--eval=EXPR` | Evaluate JavaScript after settling and print the result. |
| `--act='ACTION; ACTION'` | Input before output: `click X,Y`, `rightclick X,Y`, `move X,Y` (pointer motion: hover, `:hover`, mouseover/mousemove), `down X,Y` and `up` (press and release, with `move`s between them for a drag), `type TEXT`, `key NAME`, `wait MS`, `eval JS`, `evalfile PATH`, and more. Outside `--dump=png`, a point may be `sel=CSS [dx=N] [dy=N]`: the centre of the first matching element when the action runs. |
| `--inspect=SELECTOR`, `--inspect-at=X,Y` | Report the layout boxes matching a selector, or at a point. |
| `--wpt`, `--wpt-timeout-ms=N` | Run a testharness.js page and report every subtest; see [compliance.md](compliance.md). |
| `--debug[=info,warn,error,render,net,js]` | Stream engine events to stderr; `net` adds fetch, layout and shaping-cache totals at exit. |

Two traps in `--act`. Actions are split on `;`, so inline JS in an `eval`
action is cut at its first statement separator — put anything longer
than one expression in a file and use `evalfile`. And `type` inserts
text without keyboard events, so use `key` when the page's behaviour
depends on them: `key` with a single character, or `Enter`, presses it
as the GUI does (keydown, keypress, the character typed unless a
listener cancels, keyup).

On Linux the sandbox confines what a headless run can read. A local
page has to live somewhere Landlock allows: a build run from
`builddir/` can read its own source tree, so the fixtures under `data/`
load as files, but a page in `/tmp` or elsewhere in `$HOME` does not —
serve it over HTTP instead, as `scripts/render-tests.sh` does. The output
directory of a PNG dump is added to the sandbox
automatically.

## Configuration and environment

Settings live in `~/.config/weststar/weststar.conf`, one `key = value`
per line; `--print-config` prints every key with its effective value.
These environment variables override the file:

| Variable | Effect |
|----------|--------|
| `NS_HOME_URL`, `NS_USER_AGENT`, `NS_COMPAT_MODE` | Start page, User-Agent, compatibility mode. |
| `NS_HTTP_PROXY`, `NS_HTTPS_PROXY`, `NS_NO_PROXY`, `NS_DOH_URL` | Proxies and the DNS-over-HTTPS resolver. |
| `NS_RENDER_DRIVER` | SDL render driver, as for `--render-driver`. |
| `NS_JS_EVAL_BUDGET_MS` | Time one script may run before it is interrupted (`js_eval_budget_ms`, default 60000, at most 3600000). JetStream 2 needs more than the default: each of its benchmarks runs as one script for minutes on an interpreter. |
| `NS_PRIVATE` | Start in private mode. |
| `NS_NO_WATCHDOG`, `NS_NO_CACHE`, `NS_NO_IMAGES`, `NS_NO_LOCAL_STORAGE`, `NS_NO_HTTPS_FIRST`, `NS_NO_PRELOAD_SCAN`, `NS_NO_ASYNC_IMG_DECODE`, `NS_NO_HARDEN_ALLOC` | Turn the matching feature off. |

Others the browser reads:

| Variable | Effect |
|----------|--------|
| `NS_SAFE_BROWSING_API_KEY` | Google Safe Browsing API key. Read at build time into the generated `ns_sb_key.h` (release builds get it from the repository secret); set at run time it overrides the built-in key. Without a key only the local blocklist is used. |
| `NS_SAFEBROWSING_LIST` | Blocklist file used instead of `~/.config/weststar/safebrowsing.list` and the bundled list. |
| `NS_EXTENSIONS_DIR` | WebExtensions directory (default `~/.local/share/weststar-extensions`). |
| `NS_I18N_DIR` | Directory of the `.lang` UI catalogues. |
| `NS_LOG_FILE`, `NS_NO_LOG_FILE` | Mirror the debug event log to a file (on by default only on Windows). |
| `NS_CAMERA_ALLOW` | Grant every `getUserMedia` (microphone) request without asking. Headless/test runs only; ignored by the interactive browser. |
| `NS_EARLY_JS_FILE` | Script file evaluated before each page's own scripts. Headless/test runs only; ignored by the interactive browser. |
| `NS_FRAME_DUMP` | Directory to write every 30th rendered frame to as PNG (debugging; headless and tooling runs only). |
| `NS_FORCE_HTTP3` | Use HTTP/3 directly instead of waiting for Alt-Svc. |
| `NS_PANGO_SHAPE_CACHE` | `0` turns the web-text-lib caches off; `verify` checks every cached shape against a fresh one. |
| `NS_HEADLESS_LEGACY` | Send every headless run through the direct engine path. |

Running as `root` is refused for safety; `NS_ALLOW_ROOT=1` overrides it,
and only belongs in a throwaway container — though `scripts/dev.sh` and
the WPT runners set it by default so they work in one. `NS_NO_SANDBOX`
disables the whole sandbox and `NS_NO_SECCOMP` only the syscall filter;
they are for debugging only. Boolean switches like these and the
`NS_NO_*` feature switches count as unset when empty or `0`, `false`,
`no` or `off`.

## Smoke and render-test fixtures

`./scripts/dev.sh smoke` renders the fixture list built into
`scripts/dev.sh` — the pages in `data/fixtures/` — headless, and diffs
each against its baseline in `data/baseline/`, reporting drift. Text
fixtures use `--dump=text`; the `geo-*` ones use `--dump=layout`, which is
text-free and fixed-size so the diff is font-stable. After an intended
change, refresh with `./scripts/dev.sh baselines` (or
`dev.sh baseline <target>` for one).

`scripts/render-tests.sh [out-dir]` serves `data/render-tests/*.html` on
port 8137 and renders each to a PNG in `render-tests-out/` (800 px wide,
or `NS_TEST_VIEWPORT`) for visual inspection.

Neither is an automated test suite, and this project has none by design:
the baselines catch drift, the PNGs are read by eye, and correctness is
verified by running the browser. Two exceptions guard Weststar's own rasterizer:
the gfx test (above), and `data/fixtures/canvas-2d.html`, a page of
canvas checks that each print PASS or FAIL, which CI renders with
`--dump=text` and fails on any FAIL.

## Other scripts

| Script | Purpose |
|--------|---------|
| `wpt-run.sh`, `wpt-local.sh`, `wpt-fast.sh` | Web-platform-tests runners; see [compliance.md](compliance.md). |
| `_msys_build.sh`, `_msys_env.sh`, `_msys_eval.sh`, `_msys_act.sh`, `_msys_wpt.sh`, `run-windows.ps1`, `smoke-windows.ps1`, `check-windows-bundle.ps1` | Build, drive and smoke-test the Windows binary; `check-windows-bundle.ps1` starts the packaged bundle with only the Windows system folders on PATH. |
| `pack-linux.sh`, `pack-deb.sh`, `pack-rpm.sh`, `pack-srpm.sh`, `pack-appimage.sh`, `pack-bsd.sh`, `pack-windows.sh`, `pack-windows-installer.sh`, `pack-msix.sh` | Release packaging. |
| `nightly.sh`, `nightly-distro-build.sh` | Nightly build orchestration. |
| `check-subprojects.sh` | Fail when a `subprojects/` checkout is not at the revision its `.wrap` pins; the packaging scripts run it first. |
| `ci-linux-i386.sh` | The 32-bit x86 CI build and smoke test, run inside a 32-bit Debian 13 container (`debian:trixie` with `--platform linux/386`). |
| `ci-musl.sh` | The musl CI build and smoke test, run inside an `alpine:3.24` container. |
| `ci-pull-image.sh` | Pull a Docker official image for CI from Docker Hub, `mirror.gcr.io` or ECR Public, whichever serves it. |
| `render-screenshots.sh` | Render each URL listed in `data/screenshots/sites.txt` to a PNG beside it. |
| `speedometer-bench.sh`, `speedometer4-bench.sh`, `sample-profile.sh` | Benchmarks and a sampling profiler. |
| `gen-architecture.py`, `gen-badge.sh`, `gen-windows-icon.py` | Regenerate the architecture diagram, the badge and the Windows icon. |
| `embed-text.py`, `verify-polyfills.js` | Build helpers: embed a file as a C array where the compiler lacks C23 `#embed` (before GCC 15 and Clang 19; newer compilers read the files directly through `src/embedded/embed.h.in`), and the polyfill smoke test meson runs under `qjs`. |

## Definition of done

A change is complete when it (1) compiles cleanly with no new warnings
under the configured GCC/Clang flags, (2) launches and the affected UI path
works when exercised manually or headless, and (3) is committed and pushed.
See [`../README.md`](../README.md) for the project scope.
