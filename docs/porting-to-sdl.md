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
   with FreeType into gfx. The browser no longer links Cairo; it is an
   optional build dependency, used only by the gfx test and benchmark as
   the reference. PDF export (`--export=pdf:`) and the dormant print path
   drew through Cairo's PDF and recording surfaces; they are removed for
   now, to come back in a form that does not need Cairo.
5. **Composite with SDL textures.** Next. Today the engine paints each
   frame (or its damaged rectangles) on the CPU and the shell uploads it
   to one texture. Content that moves without changing — the page while
   it scrolls, `position: fixed` and `sticky` boxes, elements animating
   only `transform` or `opacity`, canvases and animated images — can
   instead be painted once into its own texture and moved or faded by
   SDL's renderer on the GPU each frame. That needs a layer tree from the
   engine (which boxes get their own layer, and their transforms and
   opacities), handed to the shell with each frame.

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

`meson test -C builddir --suite gfx` draws sixteen scenes with gfx and
with Cairo and compares the pixels, and compares `in_fill`, `in_stroke`,
extents, `copy_path` and the current point; both CI workflows run it.
Cairo 1.18.4 (Ubuntu 26.04) draws only the outlines of mesh patches, so
there the mesh scene is skipped; canvas conic gradients, which are mesh
patterns, were broken with such a Cairo and draw correctly since canvas
moved to gfx. The
known differences are a level or two of anti-aliasing at edges, ties on
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
results there with `g_idle_add`. The SDL shell keeps GLib (the engine
needs it regardless) and runs both loops on the main thread
(`src/sdl/mainloop.c`): it iterates the default `GMainContext` without
blocking, builds and presents whatever changed, then waits in
`SDL_WaitEventTimeout` until the next GLib timeout is due, at most 100 ms.
Results posted from the engine thread use `ns_sdl_idle_add`, which also
pushes an SDL wake event, so the wait ends at once. GLib's own timers
(the watchdog heartbeat, session saving) run unchanged.

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
