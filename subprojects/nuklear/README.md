# Nuklear (vendored)

`nuklear.h` is the single-header immediate-mode GUI library from
<https://github.com/Immediate-Mode-UI/Nuklear>, unmodified, at commit
`c8bfb4d` (2026-11-26). It is dual-licensed MIT or public domain
(Unlicense); see the end of `nuklear.h`.

Weststar draws its window chrome with it (`src/sdl/nkui.c`). Nuklear's
own font baking, vertex output and demo backends are not used: the
chrome measures and draws text with the engine's text stack and renders
Nuklear's command list itself.
