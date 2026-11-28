# Porting Northstar to SDL

Weststar starts from a snapshot of Northstar (commit `7980fa9`), imported
without history. This document is the plan for replacing GTK and Cairo
with SDL3, and the record of what has been done.

## Why the port is staged

Northstar's engine paints straight into a Cairo context: `paint.c`,
`js_canvas.c` and `svg.c` alone make about 1,500 Cairo calls, and the
vendored text stack (`src/web-text-lib/`, a Pango fork) renders glyphs
through `pangocairo` and `cairo-ft`. The GTK shell, by contrast, is
self-contained: about 7,000 lines in `src/gtk/` behind a small request /
response protocol with the engine thread (`page_session.c`).

So the shell goes first, and Cairo after it:

1. **Import and rebrand.** Done.
2. **Remove WebGL, WebGPU and video.** Done. WebGL needed an offscreen OpenGL
   context and the glslang validator; video needed GStreamer, Media
   Foundation, dav1d, libvpx and OpenH264, plus the video layer the GTK
   view composited under each frame. Both come out with their build
   options. WebGPU was never in Northstar. `<audio>` stays: it already
   plays through SDL3.
3. **SDL3 shell.** Done. `src/sdl/` replaces `src/gtk/`. The engine
   thread (`enginethread.c`) and the page session are unchanged; the SDL
   view (`pageview.c`) posts the same requests (`REQ_LOAD`, `REQ_RENDER`,
   `REQ_CLICK`, `REQ_KEY`, `REQ_SCROLL`, …) and uploads each frame, or its
   damaged rectangles, into an `SDL_Texture`. The window chrome is built
   with Nuklear (see below). The build no longer depends on GTK.
4. **Replace Cairo.** Done. An in-tree 2D rasterizer (`src/gfx/`)
   implements the subset of the Cairo API the engine uses — paths, fills
   with both fill rules, strokes with joins, caps and dashes, clipping,
   transforms, linear, radial and mesh gradients, surface patterns with
   filters and extend modes, the Porter-Duff and blend operators, groups,
   masks and damage regions — drawing into premultiplied ARGB32 pixel
   buffers that upload to SDL textures as they are. Text is laid out by
   web-text-lib (the engine's Pango fork) and its glyphs are rasterized
   with FreeType into gfx. Cairo is not a build dependency
   at all. PDF export (`--export=pdf:`) and the dormant print path
   drew through Cairo's PDF and recording surfaces; they are removed for
   now, to come back in a form that does not need Cairo.
5. **Composite with SDL textures.** Started. The engine paints each
   frame (or its damaged rectangles) on the CPU and the shell uploads it
   to a texture. Content that moves without changing — the page while
   it scrolls, `position: fixed` and `sticky` boxes, elements animating
   only `transform` or `opacity`, canvases and animated images — can
   instead be painted once into its own texture and moved or faded by
   SDL's renderer on the GPU each frame.
   - *The page while it scrolls* is done (see Scrolling below): SDL moves
     the page's texture, and only the strip the scroll exposed, and any
     `position: fixed` boxes, are painted and uploaded.
   - *Fixed boxes and elements animating only `transform` or `opacity`*
     are done (see Layers below): the engine hands the shell a list of
     layers with each frame, each painted once into a buffer of its own
     and drawn by SDL over the page with its transform and opacity. A
     fixed header is not repainted while the page scrolls, also not by a
     scroll further than the window is tall, and an animation step
     repaints nothing.
   - Left: `sticky` boxes (still painted into the page, and a page with
     one is still repainted whole on a scroll), canvases and animated
     images as layers of their own, and `will-change`, which the CSS
     engine does not parse yet.
6. **Portability: SDL as the platform.** Weststar should run wherever SDL
   does — Linux and today's Windows, and in time Windows XP and the Amiga
   family (AmigaOS 4, MorphOS, AROS) — so platform services come from SDL
   rather than from libraries tied to modern desktops. In order:
   1. *Threads, locks, condition variables, thread-local data and time*
      move from GLib to a small layer over SDL3 (`src/plat/`): SDL's
      mutexes, condition variables, read-write locks, threads, TLS and
      clocks. (Done, web-text-lib included: the Pango fork keeps its
      `GMutex`, `G_LOCK`, `GPrivate` and `GAsyncQueue` spelling, and
      `ns-glib-threads.h` gives those names the SDL layer's meaning in
      the files that use them. The browser imports none of GLib's
      thread, lock or queue functions; what is left is inside GLib
      itself, `g_once` and GObject's type system.)
   2. *The main loop and timers* move from GLib's `GMainContext` to an
      SDL-based loop with SDL timers; the engine thread gets the same.
      (Done: see Event loop below.)
   3. *Files, folders, the environment and resources* move from GLib and
      GIO to SDL (`src/plat/ns_fs.c`, `ns_env.c`): reading and atomically
      writing files, folders and their listings, path tests, per-user
      folders (the same ones as before on Linux and Windows; SDL's
      preference path elsewhere), and environment variables. The toolbar
      icons are compiled into the binary like the other embedded files,
      so `glib-compile-resources` is no longer needed. (Done. The
      watchdog's supervisor starts the browser and waits for it with
      SDL's process API.)
   4. *A portable build profile*: fontconfig optional (fonts found in the
      platform's font folders, with a small bundled fallback set), TLS and
      Web Crypto through mbedTLS instead of OpenSSL, and a bundled CA
      certificate set for systems whose root store is too old. Fonts are
      done: with `-Dfontconfig=disabled`, or where fontconfig is not
      installed, they are found by nsfc, which then also carries DejaVu
      Sans, Sans Bold, Serif and Sans Mono compiled in
      (`-Dbundled_fonts`) for a system with few fonts or none (see
      Fonts below). Crypto is
      done: Web Crypto, random numbers and Safe Browsing go through
      `src/plat/ns_crypto.h`, with OpenSSL (the default) or Mbed TLS
      (`-Dcrypto=mbedtls`, fetched as a subproject) behind it; the
      `mbedtls` build lacks only EdDSA, reported as NotSupportedError.
      `data/fixtures/webcrypto.html` checks both in CI. (TLS for HTTPS is
      libcurl's, built against whatever TLS library the platform has.)
      The CA certificates are done too: `-Dca_bundle=<file>` compiles a
      PEM file into the browser, handed to libcurl from memory where the
      system has no certificate file, or where `NS_CA_BUNDLE=builtin`
      says the system's are too old to use.
      GIO is no longer linked at all: the last of it was the flag that
      cancels a fetch (`src/cancellable.c` now), the desktop's MIME
      database for the type of a local or FTP file (a table of
      extensions and file signatures, `src/mime_guess.c`, the same on
      every platform), and a list interface on the text library's font
      classes that nothing used.
      The system services went the same way (`src/plat/ns_sys.c`,
      `ns_fs.c`): calendar time and ISO 8601 instead of `GDateTime`, the
      local time zone's name, the user's languages, the processor count,
      the system's name, the data folders, the Downloads folder, reading
      a link, a file written for its owner alone, random UUIDs. What the
      Regular expressions outside of scripts (the `pattern` attribute,
      the filters of extensions' request rules) are JavaScript's by
      definition and now run on the JavaScript engine (`src/regex.c`: a
      small runtime of its own behind a lock, with a time budget per
      match), so `GRegex` and the PCRE2 under it are not needed. What
      the browser still takes from GLib is data structures, Unicode
      tables, string and URI functions, logging, and GObject for the
      text library: 318 functions, from 370.
   5. *GLib's data structures and GObject* (318 functions at 21,000
      call sites, and the Pango fork's type system) come from a trimmed
      GLib built statically, its few OS calls redirected to the SDL
      layer — rather than rewriting the call sites. Started: with
      `-Dglib=bundled` the browser builds the source files of GLib
      2.80 and GObject that those functions live in, from the release
      pinned in `subprojects/glib.wrap`, with a build of its own
      (`subprojects/packagefiles/glib/`) in place of GLib's, and links
      them statically; with unused sections dropped that is about 1 MB.
      GIO, GModule, PCRE2, libffi and libintl are not built or needed.
      It builds for Linux, the BSDs and Windows (where the CI build
      uses it); macOS is not set up. Its OS calls are moving to SDL: GLib's mutexes, condition variables, read-write
      locks, thread-local values and threads are SDL's
      (`glib/ns-gthread-sdl.c` in place of `gthread-posix.c`, so `g_once`
      and GObject's type system lock with SDL too), and its two clocks
      are SDL's (`glib/ns-gclock.c`), which lets GLib's main loop and its
      pipe, poll and wake-up helpers go: 68 files of GLib's own remain.
      With threads on SDL, Windows needed only its type sizes and two
      more of GLib's files (`gwin32.c`, `gstdio.c`). What those files
      still ask the C library for is the environment, the locale's
      character set, files and folders, and `stderr`; whether that needs
      changing is for the first platform without them to say (stage 7).
   6. *SDL2 as well as SDL3*, since the Amiga-family ports and
      XP-capable builds are SDL2. Done: `meson setup -Dsdl=sdl2` builds
      the whole browser on SDL 2.0.18 or later. The code stays written
      against SDL3, and `src/plat/ns_sdl.h`, which every source includes
      in place of SDL's headers, gives an SDL2 build (`NS_SDL2`) the SDL3
      names: other spellings of the same thing, SDL3's true-for-success
      convention over SDL2's zero, and in `ns_sdl2.c` what SDL2 lacks,
      over POSIX or Win32. For the platform layer that is files and
      folders, calendar time (by arithmetic, as Windows' C library knows
      no time before 1970), thread-local keys that are ready when
      zeroed, once-only initialisation, read-write locks as plain mutexes,
      and on Windows an environment reader whose answers stay valid (SDL2's
      shares one buffer between calls). For the shell:
      - *Events*: SDL2's are translated as they are taken from the queue
        into structures of SDL3's shape under SDL3's names (`SDL_Event`,
        `key.key`, `key.down`, float mouse positions, the wheel's precise
        amounts), and SDL2's window sub-events into SDL3's window events,
        so `window.c`, `pageview.c` and `nkui.c` read them unchanged. The
        loop's wake-up from other threads is an SDL2 user event the same
        way. SDL2 does not say where a drop landed (the pointer's place
        stands in) nor, before 2.26, where the wheel turned (likewise).
      - *Window and renderer*: SDL2's calls under SDL3's names; the
        window's size in pixels is the renderer's output size. On Windows
        the window is per-monitor DPI aware and the UI is scaled by the
        display's DPI; on macOS and Wayland by the pixel density, as SDL3
        does; on X11 SDL2 knows no desktop scale, and `NS_UI_SCALE` sets
        it. On Windows the renderer is Direct3D 11 unless one is chosen, as
        with SDL3; SDL2's Direct3D 9 renderer (Windows XP's) loses a scroll
        drawn into a texture that is also uploaded to, so there a scrolled
        page is sent whole. Premultiplied blending is a custom blend mode,
        which SDL2's software renderer lacks: the chrome then blends as
        straight alpha, with slightly dark edges.
      - *Dialogs*: SDL2 has message boxes but no file dialogs. On Windows
        the save dialog is the system's (`GetSaveFileNameW`, there since
        Windows 95); elsewhere the file is saved straight into the
        Downloads folder under a name not yet taken, and the status bar
        says where.
      - *The system's theme*: read from the registry on Windows 10 and
        later; elsewhere light, unless chosen in the settings; a change
        is not noticed while running.
      - *Audio*: SDL3's audio streams over SDL2's: the conversion streams
        are SDL2's own (2.0.7 and later), and the device stream is SDL2's
        callback device, which pulls from a stream the mixer fills; its
        lock is the device's.
      - *The watchdog*: SDL2 has no process API, so the supervisor starts
        the browser itself, with `CreateProcessW` on Windows and `fork`
        and `execv` elsewhere, and waits for it with
        `WaitForSingleObject` or `waitpid`. The Amiga family has neither,
        and runs without the watchdog.
      - *The bundled GLib*: its thread and clock files include `ns_sdl.h`
        too (the browser hands it to the subproject as the `weststar-sdl`
        dependency), and start threads with
        `SDL_CreateThreadWithStackSize`.
      SDL2 is the system's when installed; else, or with
      `--force-fallback-for=sdl2`, the release pinned in
      `subprojects/sdl2.wrap` (2.32.8, WrapDB's meson port), built
      statically. That is genuine SDL2: a system's SDL2 may be
      sdl2-compat, SDL2's API over SDL3 (MSYS2's is, as may be a
      distribution's). Both were tested on Windows: the platform, font
      and rasterizer tests, the headless smokes, and the window with
      typing, the address bar and wheel scrolling, on Direct3D 11 and on
      Direct3D 9. CI builds the Linux browser against the wrap's SDL2 as
      a shared library, and runs the same tests and the window under
      Xvfb.
   7. *Toolchains and targets*: the code kept to C11 (C23 `#embed`
      already has a fallback; the portable CI job builds with
      `-Dc_std=gnu11` and warnings as errors), a 32-bit Windows build against the old
      MSVCRT for XP, and an AROS build in an emulator, each in CI.
   Windows XP is the realistic floor on Windows (2000 and 95 lack too much
   of the API a browser needs today, and their hardware the memory); on
   the Amiga, the PowerPC and x86 systems, not the 68k classics, whose few
   megabytes and missing FPU cannot run a modern JavaScript engine.

## The rasterizer: gfx

`src/gfx/` is Cairo's API under a `gfx_` prefix (`gfx_t`,
`gfx_surface_t`, `gfx_pattern_t`, `GFX_OPERATOR_OVER`, …) with Cairo's
semantics, so each call site moved by renaming its calls. It kept its own
prefix, rather than reusing Cairo's names, so that both could live in one
binary while the engine moved over file by file (through a bridge between
the two, now gone), and so that the test can draw every scene with both.

- **Pixels** are Cairo's: native-endian premultiplied ARGB32, RGB24 and
  A8, so a gfx surface and a Cairo surface can share one buffer, and an
  ARGB32 surface uploads to an `SDL_PIXELFORMAT_ARGB8888` texture as it
  is.
- **Paths** are kept in target pixels, as Cairo keeps them in backend
  space; curves are flattened to the tolerance (0.1 px by default) by
  uniform subdivision.
- **Rasterization** (`gfx-raster.c`) accumulates each edge's signed area
  per pixel along a row, the technique of font-rs and stb_truetype; a
  running sum gives exact coverage where one edge crosses a pixel, to
  which the fill rule is applied.
- **Strokes** (`gfx-stroke.c`) are built in user space, so a non-uniform
  matrix shapes the pen as in Cairo: one outline polygon per run of
  segments that turn gently (curves), quadrilaterals elsewhere, and joins
  and caps as separate polygons, all wound the same way and filled
  non-zero. Dashes, including zero-length dashes with round caps, follow
  Cairo's rules.
- **Patterns** (`gfx-pattern.c`): solid colours; linear and radial
  gradients (Cairo's two-circle radial, root selection as in pixman) with
  premultiplied interpolation; surface patterns with nearest and bilinear
  filtering, and for `GOOD`/`BEST` a box filter when shrinking; mesh
  patterns with straight-sided patches, which is what the canvas conic
  gradient makes.
- **Compositing** (`gfx-composite.c`): every Cairo operator, Porter-Duff
  and blend modes, with Cairo's coverage rules for bounded and unbounded
  operators. Clips are a pixel box, or a box and an 8-bit mask.
- **Groups** draw into an ARGB32 surface the size of the current clip.
- **Regions** (`gfx-region.c`): sets of pixels as non-overlapping
  rectangles, for the page session's damage tracking.

While the engine moved over, `meson test -C builddir --suite gfx` drew
sixteen scenes with gfx and with Cairo and compared the pixels, and
compared `in_fill`, `in_stroke`, extents, `copy_path` and the current
point. With Cairo gone from the build, the scenes are compared with
reference images made at that point (`src/gfx/tests/ref/`, regenerated
with `GFX_TEST_WRITE_REF=1` after an intended change), and the queries
with fixed values; both CI workflows run it. Cairo 1.18.4 (Ubuntu 26.04)
draws only the outlines of mesh patches; canvas conic gradients, which
are mesh patterns, were broken with such a Cairo and draw correctly since
canvas moved to gfx. The known differences from Cairo were a level or two of anti-aliasing at edges, ties on
a repeating gradient's seams, coverage where two edges cross inside one
pixel, and the butt caps at the ends of an open curve, which gfx squares
to the first and last flattened segment where Cairo uses the exact
tangent.

**Speed.** `meson test -C builddir --benchmark --verbose` (or
`builddir/src/gfx/gfx-bench`) times twelve page-like workloads with gfx
and with Cairo. On Windows (Ryzen 5 9600X, `-O3`, Cairo 1.18.6 with
pixman's SIMD), after the first optimisation pass, SSE2 for blending and
bilinear sampling, a colour table for gradients and copying opaque images
(whose opacity is found once and cached, as Cairo does), gfx takes 1.31x
Cairo's time over all:

| Workload | gfx / Cairo |
|----------|-------------|
| Gradients, images drawn 1:1 | 0.7–0.8x |
| Solid page fills, translucent overlays, aligned rectangles, thin lines, circles | 0.8–1.2x |
| Shrunk images | 1.4x |
| Rounded boxes, opacity groups | 2.0–2.2x |
| Rounded clips | 2.9x |
| Scaled images | 3.9x |

What is left is per-pixel work that pixman does with wider SIMD:
blending scaled images, mask clips, and anti-aliased edges of filled
shapes.

Not in gfx: vector output (PDF) and curved mesh patch sides.

## Text: pangogfx

web-text-lib drew glyphs through its Cairo backend (`pangocairo-*`).
`pangogfx-*` replaces it: a fontconfig font map and font (subclasses of
web-text-lib's `NsPangoFcFontMap` and `NsPangoFcFont`) and a renderer.

- **Metrics** are hinted as cairo's image targets had them, from
  HarfBuzz and FreeType's grid-fitted values, so layout did not change:
  the 51 render tests keep their sizes and line breaks.
- **Glyphs** are rasterized with FreeType at the device pixel size when
  the matrix is a translation and a uniform scale, at quarter-pixel
  positions, and cached per font (bounded); otherwise, for rotated or
  skewed text, the outlines are filled. Colour glyphs (emoji) are drawn
  as images. Glyphs are composited as masks, so any source and operator
  apply: canvas text with a gradient fill draws like any other.
- **Outlines** (`ns_pango_gfx_layout_path()`) feed SVG text and canvas
  `strokeText`.
- Grayscale antialiasing is the only mode; fontconfig's hinting settings
  are honoured as cairo-ft honoured them.

## Fonts: nsfc

web-text-lib's font map (`pangofc-fontmap.c`, 3,700 lines) and the engine
find fonts by calling fontconfig: about 85 of its functions. fontconfig
is a library of the Unix desktop, configured by a tree of XML files;
Windows needs its configuration bundled beside the executable, and the
Amiga family has no port to speak of. Rather than rewrite the font map,
`src/fc/` (nsfc) implements those functions, under fontconfig's own names
and in headers that take the place of `<fontconfig/fontconfig.h>`, so
the callers are unchanged and either can be chosen at build time
(`-Dfontconfig`; the system's when it is installed).

- **Finding** (`nsfc-scan.c`): the `.ttf`, `.otf`, `.ttc` and `.otc`
  files under the platform's font folders (listed in
  [building.md](building.md)), a `fonts` folder beside the executable and
  the folders in `NS_FONT_PATH`. Each face is opened with FreeType, once:
  its family names (every one in its name table, so that a page can ask
  for a font by its Japanese name), style, weight, width, slant, whether
  it is fixed-width or has colour glyphs, the range of a variable font's
  weight axis, and the set of characters it has. That is kept in
  `fonts.cache` in the browser's cache folder, keyed by each file's path,
  size and modification time, so a later start only lists the folders:
  111 faces take 0.03 s from the cache. A damaged cache file is
  discarded and written again.
- **Rules** (`nsfc-match.c`): where fontconfig has XML, nsfc has two
  tables. One says what the generic families stand for (`sans-serif`:
  DejaVu Sans, Arial, Liberation Sans, Helvetica, Noto Sans, Segoe UI,
  …; likewise `serif`, `monospace`, `system-ui`, `cursive`, `fantasy`,
  `emoji`, `math`, `fangsong`), the other which families have the same
  metrics and can stand in for one another (Arial, Helvetica, Liberation
  Sans, Arimo, …). A family that is not installed and has no stand-in
  gets the fonts of its kind, told by its name (`…Mono`, `…Serif`).
- **Order**: a request's sorted list has the families the page named
  first, then the stand-ins, then the fonts for the text's language (told
  by one letter per language: a font for Japanese has あ), proportional
  before fixed-width, and within each the nearest slant, weight and
  width. Trimmed, as the font map asks for it, the list keeps only fonts
  that add characters, which is what makes it a fallback chain.
  fontconfig puts the language before the stand-ins, so that Latin text
  on a Japanese page comes out in the Japanese font's Latin letters; nsfc
  keeps the page's family for every character it has.
- **Faces a family lacks**: a variable font is set to the requested
  weight through its `wght` axis; otherwise a bold that is missing is
  emboldened and an italic that is missing slanted, as fontconfig's
  rules do.
- **Bundled fonts** (`nsfc-bundled.c`, `-Dbundled_fonts`, on by default
  wherever nsfc is built): DejaVu Sans, Sans Bold, Serif and Sans Mono
  2.37, 2.1 MB together (Latin, Greek, Cyrillic, box drawing and many
  symbols), fetched from the release pinned in
  `subprojects/dejavu-fonts.wrap` and compiled into the binary, so that a
  fresh Windows XP, an Amiga or a bare container still draws pages and
  the chrome. They are read from memory: their file names are
  `nsfc-bundled:DejaVuSans.ttf` and so on, which the font map hands to
  `NsFcBundledFontData` instead of opening a file, and they are scanned
  at every start (a few milliseconds) rather than cached. A face the
  system has itself, by family and style, is left out. The rest rank
  last: after a system font of a family the page named, and after every
  system font standing in for the same generic family, though before the
  stand-ins for the next one, so that a system with no monospace font
  gets DejaVu Sans Mono rather than its sans-serif. After the best font
  of a list they come after all the system's, and so give only the
  characters no system font has.

Not in nsfc: bitmap-only fonts other than colour emoji, the languages a
font declares (the font map then asks nothing of them), and any
configuration by the user beyond `NS_FONT_PATH`. For tests,
`NS_NO_SYSTEM_FONTS=1` skips the platform's folders and the `fonts`
folder beside the executable, leaving `NS_FONT_PATH` and the bundled
fonts, as on a system with no fonts at all; the list it gives is not
written to the cache.

`meson test -C builddir --suite nsfc` checks patterns, character sets,
the rules and the order with a handful of made-up fonts, and the bundled
fonts of the build with the system's turned off, in both CI workflows;
the Linux `portable` job builds the browser with
`-Dfontconfig=disabled -Dcrypto=mbedtls` on a machine without
fontconfig's headers and checks that text is drawn, both with the
system's fonts and with `NS_NO_SYSTEM_FONTS=1`, from the bundled ones
alone.

## The chrome: Nuklear

The toolbar, address bar, find bar, menus and status bubble are built
with [Nuklear](https://github.com/Immediate-Mode-UI/Nuklear), vendored
unmodified in `subprojects/nuklear/`. Alternatives considered:

- **Clay** (zlib) is a fast flexbox-style layout engine with SDL3 and
  Cairo renderers, but it has no text input, keyboard focus or menus:
  the address bar and every menu would have been written by hand.
- **microui** is very small, but its textbox can only append and
  backspace.
- **Dear ImGui** is C++; Weststar is C only.
- **The engine itself** could draw the chrome as an HTML page, the way
  Firefox does. It remains an option for later, at the cost of routing
  input and focus between two documents.

Nuklear brings edit fields with selection, clipboard and undo, menus,
popups and scrollable windows, and emits a plain list of draw commands.
`src/sdl/nkui.c` is Weststar's backend for it: SDL3 events are fed in,
the commands are drawn with gfx into a window-sized layer, and only the
part that changed since the last frame is uploaded to the GPU. Text goes
through an `nk_user_font` that measures and draws with web-text-lib (the
engine's Pango fork), so the chrome gets HarfBuzz shaping and fontconfig
fallback; kerning is off in the chrome because Nuklear places the caret
by adding up the widths of single characters. The chrome was the first
code moved to gfx.

Nuklear only reports a click when the button is still down at the end of
a frame, so the window ends the frame early when a release follows a
press within one turn of the loop (`feed_nk` in `window.c`).

## Event loop

GTK ran the GLib main loop on the main thread and the engine posted its
results there with `g_idle_add`. Weststar's loop is its own, on SDL
(`src/plat/ns_loop.c`): an `NsLoop` per thread that needs one (the main
thread, the engine thread), with timeouts, idles and custom sources by
id and priority, nested iterations, and posting from other threads,
which wakes the loop (an SDL user event on the main thread, a condition
variable elsewhere). `NsMainLoop` runs and quits one level of it.

The main thread (`src/sdl/mainloop.c`) iterates its loop without
blocking, builds and presents whatever changed, then waits in
`SDL_WaitEventTimeout` until the loop's next timeout is due, at most
100 ms; results posted from the engine thread wake the wait at once.
Background work (fetches, image decodes) runs on `ns_job` worker threads
(`src/plat/ns_job.c`, SDL threads started as needed, up to 40, idle ones
leaving after 10 s), and `NsTask` (`src/task.c`), shaped like GLib's
GTask, delivers each result on the loop of the thread that started it.

GLib's main loop is not used anywhere. The watchdog's supervisor, a
process of its own that restarts the browser after a crash or a hang,
needs no loop at all: it starts the browser with `SDL_CreateProcess`
(with `NS_WATCHDOG_RECOVER` in that one child's environment for a
recovery start) and blocks in `SDL_WaitProcess` until it exits; in an
SDL2 build, with the system's own calls (stage 6.6). On Unix,
SIGINT and SIGTERM are passed on to the browser from their signal
handlers, with an alarm that kills it if it has not gone after three
seconds.

## Scrolling

On a scroll by a whole number of device pixels, the engine's page session
keeps what it has already painted: it moves its frame buffer by the
scroll's distance and repaints the strip that came into view, and the
places where `position: fixed` boxes were and now are
(`session_scroll_blit`, `page_session.c`; a page with `sticky` boxes,
fixed backgrounds or 3D transforms is repainted whole). The frame it
hands to the shell says how far the pixels moved (`blit_dx`, `blit_dy`)
and which rectangles were painted.

The shell does the same to its copy with SDL (`scroll_texture`,
`pageview.c`). SDL cannot draw a texture onto itself, so the page is a
pair of textures, each one a render target: a scroll draws the front
texture into the back one at the offset, the two swap, and the painted
rectangles are uploaded into the new front. A scrolled frame used to be
copied whole out of the engine and uploaded whole; in a 1200×820 window that copy
took 0.86 ms and now takes 0.04 ms for a wheel step, and the upload
shrinks the same way. The window's contents after a series of wheel,
arrow, Page Down and End scrolls are identical, pixel for pixel, to those
of full repaints (`NS_DAMAGE=0`), with and without fixed boxes on the
page.

SDL2's Direct3D 9 renderer, the one Windows XP has, keeps a copy in
memory of a texture that is uploaded to and sends all of it again after
the texture was drawn into, which would undo the scroll; with that
renderer the shell asks for the whole frame after a scroll instead.

## Layers

Some boxes the engine does not paint into the page at all but into
compositing layers of their own, which the shell draws over the page
(`src/layers.c`):

- `position: fixed` boxes, and
- elements with a running or paused CSS animation, CSS transition or Web
  Animation that changes nothing but `transform` and `opacity`.

Each layer is painted once, at its size before its transform, into a
premultiplied ARGB buffer (`ns_paint_layer`), and the page is painted
without it (`ns_paint_set_layer_skip`). Each frame, `ns_page_frame`
carries the layers in the order to draw them: an id, the buffer (marked
when its pixels changed), where its top left pixel is, its transform in
device pixels and its opacity. A fixed layer stays in the viewport;
the others move with the page, and a buffer painted at one scroll offset
serves every other. A buffer is painted again only when the layer is new,
changed size or moved by a fraction of a pixel, when damage touches it,
or when the whole frame is repainted. The shell keeps a texture per layer
(`pageview.c`), uploads only the changed ones, and draws them after the
page with premultiplied blending, the opacity as colour and alpha
modulation, by whole pixels as they are, scaled with `SDL_RenderTexture`,
or turned and skewed with `SDL_RenderTextureAffine`. SDL's software
renderer draws a turned texture without premultiplied blending, so on it
the shell asks for layers that are only moved and scaled
(`NS_PAGE_LAYERS_AXIS_ONLY`), and a turned or mirrored one is painted
into the page.

An animation step of a layer's element repaints nothing: the tick does
not damage it (`browser_anim_tick`), and the frame only says where the
layer now is. A scroll moves the page texture as before, without the
fixed layers in it: their bands are no longer repainted on the way.

The compositor draws layers over the page, so a box becomes one only if
nothing the page paints after it can overlap it. The plan
(`ns_layers_plan_build`, made again when the page is laid out, restyled
or damaged, or the set of animated elements changes) finds the boxes
whose ancestors paint their descendants as they are (no group, transform,
filter, clip, scroller or stacking context; a positioned ancestor without
a z-index is fine), and collects for each the rectangles of what paints
above it: for a box in the flow, what follows it in the document and
every positioned box and float; for a positioned one, the positioned
boxes above it in z-index or after it at its own. Each frame
(`ns_layers_plan_resolve`) places the layers with their transforms at the
scroll offset and paints into the page, for that frame, any layer one of
those rectangles overlaps, and any layer before it that it overlaps. A
fixed header with a box of a higher z-index scrolling under it is in the
page while the two overlap; a box sliding over text later in the document
is in the page while it is over the text. Promoting or demoting a layer
damages where it is.

Painted into the page instead, always: `sticky` boxes; boxes with a blend
mode, filter, mask, `clip-path`, `clip` or a 3D transform; floats, boxes
in a line (inline-block) or a multi-column flow; boxes with a negative
z-index or with positioned descendants that would paint in another
stacking context; anything below such an ancestor; a transform nothing
animates (the GPU would resample text that is painted sharp in place);
layers larger than 4096 pixels a side or twice the viewport's area; more
than 32; and every layer while a modal dialog or a drop-down list is
open. A layer's content and the page under it are the same pixels as
before, so hit testing, `getBoundingClientRect` and the like are not
involved.

Headless consumers of `ns_page_session` frames (screenshots, the remote
driver) leave layers off and get the whole page in the frame's pixels.
`--dump=png` paints with layers and composites them on the CPU with gfx
(`ns_layers_paint_page`): the reference for what the shell shows.
`NS_LAYERS=0` turns layers off everywhere; `NS_LAYERS=log` prints the
plan and each frame's decisions; `NS_FRAME_STATS=1` prints each frame's
paint, copy and upload times in the window.

What comes out, against `NS_LAYERS=0`:

- `--dump=png` of `data/fixtures/layers-fixed.html?set=exact` and
  `layers-anim.html?set=exact` at `--time-ms=1200`, where every layer
  moves by whole pixels, is identical byte for byte (both CI workflows
  check this). Without `set=exact` the fixed header's text on its
  translucent background is one or two levels apart in 1122 pixels
  (8-bit rounding: text drawn onto the page through the background,
  against text drawn onto the background in a buffer that is then drawn
  onto the page), and the turning and scaling boxes of the animation page
  differ by up to 165 at their edges and in their text in about 2500
  pixels: the GPU resamples a painted buffer where paint draws the
  transformed shapes directly.
- The window (read back with `NS_GUI_TEST`): for the fixed header page
  after wheel, Page Down, End and Home scrolls, and while a box with a
  higher z-index scrolls under the header, at most 2 levels in the
  translucent header and the badge (the GPU's blending against gfx's),
  nowhere else. For the animation page paused at 700 ms
  (`?t=700&set=exact`), at most 2 levels; with the turning and scaling
  boxes, up to 156 at their edges, as above.
- In a 1200×820 window (Windows, SDL's default renderer), the fixed header page
  scrolled by 40 wheel steps and 8 scrolls of more than a window: the
  engine's paint, copy and upload took 2.1 ms a frame on average (median
  1.7 ms) against 6.7 ms (median 6.6 ms); a wheel step 1.4 to 1.5 ms
  against 6.2 to 6.7 ms, a scroll further than the window 3.4 to 4.0 ms
  against 7.2 to 8.1 ms, and the header and badge were repainted on none
  of those frames. The animation page, eight elements animating for four
  seconds: 0.03 ms a frame (median 0.01 ms) against 1.6 ms (median
  1.5 ms), and 6 300 pixels uploaded a frame (the box in the page while
  it slides over text) against 132 000.

Not yet: `sticky` boxes as layers (they move against the scroll, and a
page with one is still repainted whole when it scrolls), canvases and
animated images as layers of their own, `will-change` (not parsed), a
layer clipped by a scrolling ancestor, and repainting a layer at its
scale when a `scale()` animation enlarges it.

## What is gone, and where it went

| Northstar | Weststar |
|-----------|----------|
| `src/gtk/procwindow.c` (window, toolbar, menus, dialogs) | `src/sdl/` window and chrome |
| `src/gtk/procview.c` (page widget) | `src/sdl/` page view |
| `src/gtk/icons.c` (SVG icons for GTK) | icons drawn by the engine's SVG renderer |
| `src/gtk/uiscale.c` | SDL display content scale |
| GTK print dialog, `print.c` pagination, `--export=pdf:` | removed for now: they drew through Cairo's PDF surface; printing and PDF export are to be redone without Cairo |
| GTK file dialogs (Save Page, Save as PNG) | `SDL_ShowSaveFileDialog` |
| Developer tools window | not yet |
| WebGL (`webgl.c`, `glctx.c`, `glsl_validate.c`) | removed |
| Video (`video.c`, `av1.c`, `vp9.c`, `h264.c`, `avc.c`, `yuv.c`, `gstvideo.c`, `mfvideo.c`, `videodec.c`, `videoworker.c`, `videolayer.c`) | removed: a `<video>` fails with `MEDIA_ERR_SRC_NOT_SUPPORTED` and paints as a dark box with its controls; `canPlayType` answers `""` for every video type |
| Media Source Extensions (`mse.c`, `mp4.c`, `webm.c`, `progressive.c`, `js_mse.c`) and its AAC/Opus decoders (`audiodec.c`, `src/audio/aac.c`) | removed: `MediaSource` and `SourceBuffer` are undefined |
| Camera capture (V4L2 enumeration in `camera.c`) | removed: `getUserMedia({video})` rejects with `NotFoundError` and `enumerateDevices()` lists nothing; the per-site permission in `camera.c` stays for the (silent) microphone track |
| The `video_layers` of `ns_page_frame`, `ns_page_session_set_video_layers` and the video compositing in the view | removed |
| Packaging (Debian, RPM, Arch, MSIX, npm, AppImage) and release CI | removed; to be redone for Weststar |

## Names

Internal names (`ns_` prefixes, `libnorthstar.c`, `NS_` environment
variables, the `northstar-extension:` scheme) are kept so that engine
fixes can move between Northstar and Weststar. User-visible names, the
executable and the profile directories are Weststar's (`src/brand.h`).
