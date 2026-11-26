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
4. **Replace Cairo.** An in-tree 2D rasterizer (`src/gfx/`) implements
   the subset of the Cairo API the engine uses — paths, fills with both
   fill rules, strokes with joins, caps and dashes, clipping, transforms,
   linear, radial and mesh gradients, surface patterns with filters and
   extend modes, the Porter-Duff and blend operators, groups and masks —
   drawing into premultiplied ARGB32 pixel buffers that are wrapped as
   `SDL_Surface`s. Glyphs are rasterized with FreeType. Once every call
   site compiles against `src/gfx/`, the Cairo dependency is dropped and
   the remaining names are moved off the `cairo_` prefix.
   PDF output (`--export=pdf:`) and the recording surface need a separate
   answer; until then they are built only when Cairo is present.

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
the commands are drawn with Cairo into a window-sized layer, and only the
part that changed since the last frame is uploaded to the GPU. Text goes
through an `nk_user_font` that measures and draws with web-text-lib (the
engine's Pango fork), so the chrome gets HarfBuzz shaping and fontconfig
fallback; kerning is off in the chrome because Nuklear places the caret
by adding up the widths of single characters. In stage 4 the backend
draws through the in-tree rasterizer like the rest of the engine.

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
| GTK print dialog | not yet; `--export=pdf:` still works headless |
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
