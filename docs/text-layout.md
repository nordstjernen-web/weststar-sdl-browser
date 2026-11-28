# Text without Pango: nstext

The engine lays text out and draws it through one API, the `ns_pango_*`
functions of `src/ns_pango.h`. Two libraries implement it:

- **web-text-lib** (`src/web-text-lib/`, `-Dtext=pango`, the default): the
  in-tree Pango 1.58 fork with its gfx backend (`pangogfx`), on GLib and
  GObject.
- **nstext** (`src/text/`, `-Dtext=native`): the same API written for the
  browser on HarfBuzz, FriBidi and FreeType, with fonts from fontconfig or
  nsfc (`src/fc/`), drawn with gfx. No Pango, no GObject, no GLib.

nstext is opt-in while it is compared against the fork (see *Status*).
This page records what the engine needs of the API, how nstext provides
it, and how far the two agree.

## What the engine uses

105 functions and 23 types and enumerations, at about 390 call sites
(`paint.c` 233, `layout.c` 87, `js_canvas.c` 21, `sdl/ui.c` 18, `svg.c`
and `mathml.c` 12 each, and `appmain.c`, `js_intl.c`, `font.c`, `net.c`).
What the call sites rely on:

**Font descriptions.** `new`, `free`, `from_string` (`"sans 9"` for the
chrome, the rest of a CSS `font` shorthand for canvas), `set_family`
(comma-separated family lists from CSS, resolved to fontconfig names by
`css.c`), `set_family_static`, `set_style`, `set_weight` (CSS weights
bucketed to Pango's), `set_stretch`, `set_absolute_size` (CSS px times
1024), `set_variations` (`"wght=650,wdth=80"`). Only fields that are set
take part in merging (the layout's description over the context's, an
attribute's over the layout's).

**Attributes and lists.** Family, weight, style, stretch, small-caps
variant, absolute size, a font description (for variations), OpenType
features (`"kern=0, liga=1"`, from font-kerning, font-variant-ligatures
and font-feature-settings), language, foreground and background colours
with separate alpha attributes (alpha 1 is used to hide text), underline
(single, double, error for wavy) with colour, overline with colour,
strikethrough with colour, rise (super/subscript), letter and word
spacing, shape (an inline-block, image or control: a rectangle that
reserves space and is drawn by the engine), allow-breaks false (inside
form controls) and insert-hyphens (CSS `hyphens`). The engine sets
`start_index` and `end_index` itself (bytes; `G_MAXUINT` for "to the
end") and inserts with `ns_pango_attr_list_insert`; lists are reference
counted.

**Layouts.** `new` on a context (`ns_pango_gfx_create_context(NULL)`
shared by the painter, or `ns_pango_gfx_create_layout(g)` scaled to a
canvas), `set_text`, `set_attributes`, `set_font_description`,
`set_width` (-1 for no wrapping), `set_height` (negative: a line-clamp
count), `set_wrap` (word, char, word-char), `set_ellipsize` (start for
RTL, end), `set_indent`, `set_line_spacing` (a factor of the font's
height, from CSS line-height), `set_justify`, `set_alignment`,
`set_auto_dir`, `set_tabs` (pixel tab stops for `tab-size`),
`context_changed` (after the engine adds shape attributes for stretched
inputs). Layouts are held and dropped with `ns_pango_layout_ref` and
`ns_pango_layout_unref` (the one API change, below).

**Measuring.** `get_extents`, `get_pixel_extents` (ink heights of "x"
and "H" give CSS `ex` and `cap`), `get_size`, `get_pixel_size` (the
advance of "0" and of "水" give `ch` and `ic`), `get_baseline`,
`get_line_count`, `get_line_readonly` (line start and length, in bytes),
`line_get_extents`; iterators over lines with their extents and
baselines (decorations drawn by the engine, selection rectangles, the
strut computation of inline boxes).

**Positions.** `index_to_pos` (carets, inline box placement, stretched
inputs), `index_to_line_x`, `line_index_to_x` (dashed decorations),
`xy_to_index` (hit testing, caretPositionFromPoint),
`line_get_x_ranges` (selection highlights), `get_log_attrs_readonly`
(word selection by double click) and `ns_pango_get_log_attrs`
(`Intl.Segmenter`: graphemes, words, sentences).

**Fonts.** `ns_pango_gfx_font_map_get_default`, `list_families` and
`font_family_get_name` (the `font-family` availability oracle),
`get_serial` (a generation counter for caches), and
`ns_pango_fc_font_map_config_changed` after a web font is added to
fontconfig's application set (`font.c`). `ns_pango_context_get_metrics`
gives ascent, descent and the approximate character width (input sizes,
`ch` fallbacks, line-height normal).

**Drawing.** `ns_pango_gfx_show_layout` and `show_layout_line` at gfx's
current point in its current source (colour attributes override it), and
`ns_pango_gfx_layout_path` (canvas `strokeText` and SVG text as
outlines).

**Diagnostics.** `ns_pango_version_string` (about:weststar) and the
shape, break and item cache statistics (`--debug=net`). nstext keeps a
shape cache only (`NS_PANGO_SHAPE_CACHE=0` turns it off, as for the
fork); its break and item statistics read zero.

## The design of nstext

nstext implements those functions and nothing else. Its types are plain
C structs with reference counts; its locks, once-only initialisation and
thread-local values are the SDL platform layer's (`src/plat/ns_plat.h`).
Where the fork's behaviour decides a pixel (line breaks, rounding, line
heights, decoration geometry), nstext follows the fork's code closely, so
that pages lay out the same; the rest is written anew.

| File | What |
|------|------|
| `nstext.h` | The public API: the `ns_pango_*` names, types and enums with Pango's values. |
| `nstext-attrs.c` | Font descriptions (and `from_string`), attributes, lists kept sorted by start, the range iterator with Pango's merging rules, tab arrays. |
| `nstext-util.c` | Memory, UTF-8, Unicode properties (general category, script, mirroring from HarfBuzz; Line_Break from the generated table), a hash table, languages (the default from `LC_ALL`/`LC_CTYPE`/`LANG`, else the Windows thread locale, as GLib does), script runs with paired punctuation, the version string. |
| `nstext-linebreak-table.h` | Line_Break classes of Unicode 17.0, generated by `scripts/gen-linebreak-table.py` from `LineBreak.txt` (48 kB of source, committed so the build needs no network). |
| `nstext-case-table.h` | The full uppercase mapping of each lowercase letter of Unicode 17.0 (U+00DF to "SS"), for synthesized small caps, generated by `scripts/gen-case-table.py` from `UnicodeData.txt` and `SpecialCasing.txt`. |
| `nstext-break.c`, `nstext-break-table.h` | Line, word, sentence and grapheme boundaries: Pango's `break.c` with its Arabic, Indic, Thai (libthai dictionary breaks, when installed) and Catalan tailorings, ported off GLib; allow-breaks and insert-hyphens. |
| `nstext-emoji.c`, `-emoji-table.h`, `-emoji-scanner.c` | Emoji presentation runs (Chromium's scanner, as Pango has it) and the emoji properties. |
| `nstext-font.c` | The font map (one per thread), fontsets (FcFontSetMatch for the first font, FcFontSetSort for fallbacks, over the SFNT fonts of fontconfig's system and application sets), fonts (a HarfBuzz font at the pattern's pixel size with variations, synthetic slant and emboldening; nsfc's bundled fonts from memory), metrics and glyph extents from FreeType hinted at the device size of a scaled context, the hex boxes of missing glyphs, glyph rasterization with a per-font cache, outlines, contexts. |
| `nstext-itemize.c` | Bidi levels (FriBidi, with Pango's fast paths), paragraph boundaries, items of one level, script, emoji presentation and font attributes, the font of each character from its fontset (spaces take their neighbours'), colour emoji fonts; small caps synthesized where the font lacks `smcp`/`c2sc` (itemize.c's post-processing: lowercase runs shaped uppercased at the x-height's share of the cap height). |
| `nstext-shape.c` | Items, glyph strings, HarfBuzz shaping with the paragraph as context, OpenType features, ligatures off under letter spacing, small caps by the font's features, a bounded shape cache shared between threads (sharded, reader/writer locks, evicting what was not read since the last sweep), letter and word spacing, caret positions inside clusters (ligature carets). |
| `nstext-glyph-item.c` | Runs as Pango's glyph items: the cluster iterator and the split of a run (`pango-glyph-item.c`). |
| `nstext-layout.c` | Layouts: paragraphs, Pango's greedy line breaker (`process_item`, `process_line`), tabs, hyphens, trailing spaces, rise, bidi reordering, justification between words and between clusters (`justify_words`, `justify_clusters`), extents, line spacing, alignment and indent, hit testing, iterators, the context's metrics. |
| `nstext-ellipsize.c` | Ellipsis: Pango's `ellipsize.c`, a gap grown from the start, the middle or the end of the line, balanced around that position, with the ellipsis (U+2026, U+22EF before wide characters, or "...") shaped in the gap's font, and the line's runs rebuilt around it. |
| `nstext-render.c` | Drawing: backgrounds, glyphs and decorations run by run as Pango's renderer does, glyphs as the fork's gfx backend draws them (bitmaps at quarter-pixel phases, colour glyphs, outlines when rotated). |

Line breaking uses Pango's rules on a table generated from the Unicode
Character Database rather than libunibreak: the rules are what the fork
runs today (so breaks agree to the character), libunibreak would add a
dependency and its own tailoring, and the table is the only Unicode data
the rules need that HarfBuzz does not have.

`NSTEXT_DEBUG=1` in the environment prints each font nstext loads (its
file, size and extents) and each context metrics computation, to see
which fonts a page got.

### The one API change

The fork's layouts are GObjects, which the engine held with
`g_object_ref`/`g_object_unref`. Those 36 calls now read
`ns_pango_layout_ref`/`ns_pango_layout_unref`, which `ns_pango.h`
defines over GObject for the fork. `appmain.c` reaches the cache
statistics through `ns_pango.h` instead of a fork header.

### Dependencies

nstext needs HarfBuzz, FriBidi, FreeType, fontconfig (or nsfc) and SDL
through the platform layer; libthai is used where installed. It calls no
GLib function and includes no GLib header. With `-Dtext=native` the
browser does not link GObject at all; GLib itself is still linked, for
the rest of the engine (stage 6.5 of [porting-to-sdl.md](porting-to-sdl.md)).

## Status

`meson setup -Dtext=native` builds the whole browser, with SDL3 or SDL2,
with fontconfig or nsfc (and its bundled fonts), with gcc and with clang
ThinLTO under `--werror`; the default stays `pango` until the comparison
below is clean everywhere. Measured on Windows (MSYS2 UCRT64, system
fontconfig) with `--headless --viewport=800x1000`, comparing `--dump=png`
byte for byte and `--dump=layout` line by line, 77 pages:

| Pages | Pictures | Layout |
|-------|----------|--------|
| `data/fixtures/*.html`, 23 pages | identical | identical |
| `data/render-tests/*.html`, 51 pages | identical | identical |
| about:license | identical | identical |
| about:start | the animated logo only (it differs between two runs of the same build too) | identical |
| about:weststar | the line naming the text library | that line |

canvas-2d gives 21 PASS and webcrypto 57 PASS with either library. The
WPT checkout's testharness tests under `css/css-text`, `css/css-text-decor`
and `css/css-fonts` (355 files, 6718 subtests, parsing tests left out)
give 4135 passes with the fork and 4138 with nstext: the same results in
every file but `css-fonts/variations/at-font-face-font-matching.html`,
where nstext passes three more.

`text-small-caps.html` has small caps in a font with `smcp` (Calibri or
Linux Libertine, where installed) and synthesized in DejaVu (U+00DF,
ligatures, Greek and Cyrillic, bold and italic, letter spacing, wrapping,
an underline), and canvas text under `scale()`, a scale unequal in x and
y and `setTransform()`, with the width and ink ascent and descent
`measureText` gives. Before nstext synthesized small caps and hinted at
the device size, its pictures and those numbers differed from the fork's.

In `text-layout.html` the boxes, line breaks and glyph positions of
Latin with wrapping and justification, mixed fonts, sizes and weights,
spacing, decorations, Arabic and Hebrew with embedded numbers and Latin,
Devanagari, Thai (dictionary breaks), CJK, colour emoji with modifiers,
flags and ZWJ sequences, combining marks, tabs, the white-space values,
hyphens, overflow-wrap, inline boxes, indent, line-height and the
ellipsis are the same, and so are their pixels. `text-ellipsis.html`
cuts lines at the end (ltr) and at the start (rtl): Latin, Hebrew,
Arabic and mixed bidi, letter spacing, a font change and combining
marks at the cut, CJK (the midline ellipsis), and justifies CJK, Korean
and Thai paragraphs that have no spaces. (The fixture's caret probe
reads Range rectangles, which the headless engine reports as empty with
either library.)

The middle ellipsis, which no CSS asks for, and carets and hits inside
an ellipsis are compared by `text-layout-dump`, a small program built
with either library (`ninja -C build-X text-layout-dump`): it prints the
lines, extents, every character's position, hits across the width, x
ranges and a hash of the pixels of a set of ellipsized and justified
layouts, and writes their pictures to a directory given as argument.
The two builds print the same.

Differences found and fixed on the way, each visible in the pictures:
decorations pending when a colour changes are drawn in the old colour
(Pango's `handle_line_state_change`: the underline of a link beside
another link); glyph offsets are not rounded (marks in Devanagari and
Thai); the context's metrics shape their sample string unrounded (the
width of text inputs); a variation selector asks for a colour or a text
emoji font (the height of a line with emoji).

### What is left before the fork can go

- Vertical text and gravity are not implemented (the engine sets none).
- The comparison has been run on Windows only; the `text-native` job of
  the Linux workflow builds both libraries and requires the same
  `--dump=layout` for every fixture there, and the same
  `text-layout-dump` but for its pixel hashes.
- Of the fork's cache statistics only the shape cache's are reported:
  nstext has no break or item cache.
