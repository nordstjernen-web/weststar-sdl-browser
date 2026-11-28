#!/usr/bin/env python3
"""Draws Weststar's about-page logo: a 256-colour "W" in mid-1990s style.

48 x 48 pixel art with one 256-entry palette, as a VGA game would have
it: an embossed, rust-and-bronze metal W (light bevels top-left, dark
bevels bottom-right, a noisy cast-metal texture and a rivet) standing
over a dark planet horizon with a glowing rim, under a deep blue night
sky where stars twinkle and a meteor streaks past. Saved as a looping
indexed GIF, every art pixel a 2 x 2 block (96 x 96); the pages show it
with image-rendering: pixelated.

The first frame is also the application icon: the window icon, drawn
from weststar.svg (the frame's pixels as one path per colour), and the
Windows executable's weststar.ico (16 to 256 pixels).

Usage: scripts/gen-retro-logo.py [out.gif [out.svg [out.ico]]]   (needs Pillow)
"""
import sys
from PIL import Image

ART = 48
SCALE = 2
FRAMES = 20
FRAME_MS = 80

# ---- palette ------------------------------------------------------------

pal = [(0, 0, 0)] * 256
BLACK = 0

def ramp(first, colors, n):
    """n palette entries from first, interpolated along colors."""
    out = []
    for i in range(n):
        pos = i / (n - 1) * (len(colors) - 1)
        k = min(int(pos), len(colors) - 2)
        t = pos - k
        a, b = colors[k], colors[k + 1]
        pal[first + i] = tuple(int(a[j] + (b[j] - a[j]) * t + 0.5) for j in range(3))
        out.append(first + i)
    return out

SKY = ramp(1, [(2, 4, 22), (8, 16, 58), (22, 38, 96)], 16)        # top -> horizon
METAL = ramp(32, [(26, 12, 6), (74, 36, 14), (128, 70, 26),
                  (176, 112, 46), (220, 170, 96), (250, 226, 170)], 32)
PLANET = ramp(64, [(2, 6, 14), (8, 24, 40), (14, 44, 66)], 8)
RIM = ramp(72, [(30, 90, 150), (110, 200, 255), (220, 245, 255)], 6)
STAR = ramp(80, [(70, 80, 120), (160, 175, 220), (255, 255, 255)], 4)
METEOR = ramp(88, [(30, 50, 120), (90, 150, 255), (200, 230, 255), (255, 255, 255)], 8)
OUTLINE = 100
pal[OUTLINE] = (8, 3, 2)
SHADOW = 101
pal[SHADOW] = (1, 2, 10)

# ---- shapes -------------------------------------------------------------

LX, TY, NW, NH, STROKE = 3, 8, 42, 28, 6

# The W: four thick strokes from the top down to two bottom points and
# up to a lower middle peak.
W_POINTS = [(LX + 3.0, TY + 0.5), (LX + 11.5, TY + NH - 1.5),
            (LX + NW / 2, TY + 5.0), (LX + NW - 11.5, TY + NH - 1.5),
            (LX + NW - 3.0, TY + 0.5)]

def seg_dist(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)))
    cx, cy = ax + t * dx, ay + t * dy
    return ((px - cx) ** 2 + (py - cy) ** 2) ** 0.5

def n_shape(x, y):
    """The letter (a W): within half a stroke of one of its four strokes,
    cut flat at the top and bottom."""
    if not (TY <= y < TY + NH):
        return False
    px, py = x + 0.5, y + 0.5
    return any(seg_dist(px, py, *W_POINTS[i], *W_POINTS[i + 1]) <= STROKE / 2
               for i in range(4))

N = [[n_shape(x, y) for x in range(ART)] for y in range(ART)]

def in_n(x, y):
    return 0 <= x < ART and 0 <= y < ART and N[y][x]

def noise(x, y):
    """Stable per-pixel noise in [0, 1), for the cast-metal texture."""
    h = (x * 374761393 + y * 668265263) & 0xffffffff
    h = (h ^ (h >> 13)) * 1274126177 & 0xffffffff
    return ((h ^ (h >> 16)) & 0xffff) / 65536

BAYER = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]

HORIZON_CX, HORIZON_CY, HORIZON_R = 24, 92, 52   # a planet below the frame

def planet_dist(x, y):
    dx, dy = x + 0.5 - HORIZON_CX, y + 0.5 - HORIZON_CY
    return (dx * dx + dy * dy) ** 0.5 - HORIZON_R

STARS = [(4, 4), (41, 3), (34, 9), (3, 15), (44, 14), (6, 26), (43, 27),
         (21, 2), (15, 9), (28, 3), (39, 20), (2, 33), (45, 35), (12, 1)]

def metal_index(x, y):
    """Shaded metal: base by height, bevels by neighbours, noise texture."""
    level = 0.62 - (y - TY) / NH * 0.22           # a little darker lower down
    up_left_out = not in_n(x - 1, y - 1) or not in_n(x, y - 1) or not in_n(x - 1, y)
    down_right_out = not in_n(x + 1, y + 1) or not in_n(x, y + 1) or not in_n(x + 1, y)
    two_up_left_out = not in_n(x - 2, y - 2)
    two_down_right_out = not in_n(x + 2, y + 2)
    if up_left_out:
        level = 0.96
    elif two_up_left_out:
        level = 0.80
    elif down_right_out:
        level = 0.12
    elif two_down_right_out:
        level = 0.28
    level += (noise(x, y) - 0.5) * 0.16           # pitted, cast surface
    # Rust streaks running down from the top of each stroke.
    if noise(x // 3, 7) < 0.3 and not up_left_out and not down_right_out:
        level -= 0.10 * (1 - (y - TY) / NH)
    level = min(max(level, 0.0), 0.999)
    return METAL[int(level * len(METAL))]

def frame(f):
    px = [[BLACK] * ART for _ in range(ART)]
    # Sky, dithered from deep blue at the top towards the horizon glow.
    for y in range(ART):
        for x in range(ART):
            v = y / (ART - 1) * (len(SKY) - 1)
            base = int(v)
            up = 1 if (v - base) * 16 > BAYER[y % 4][x % 4] and base + 1 < len(SKY) else 0
            px[y][x] = SKY[base + up]
    for i, (sx, sy) in enumerate(STARS):
        phase = (f + i * 7) % 10
        px[sy][sx] = STAR[[1, 2, 3, 2, 1, 0, 1, 1, 0, 1][phase]]
    # A meteor from the upper right, its tail fading behind it.
    t = f / FRAMES
    hx, hy = 52 - t * 70, -6 + t * 34
    for k in range(10):
        x, y = int(hx + k * 1.6), int(hy - k * 0.8)
        if 0 <= x < ART and 0 <= y < ART and not in_n(x, y):
            px[y][x] = METEOR[max(0, 7 - k)]
    # The planet and its glowing rim.
    for y in range(ART):
        for x in range(ART):
            d = planet_dist(x, y)
            if d < 0:
                depth = min(-d / 10, 0.999)
                px[y][x] = PLANET[int((1 - depth) * (len(PLANET) - 1) * 0.999)]
            elif d < 1.6:
                px[y][x] = RIM[int((1 - d / 1.6) * (len(RIM) - 1))]
    # The N: shadow, outline, metal, rivet.
    for y in range(ART):
        for x in range(ART):
            if in_n(x - 2, y - 1) and not in_n(x, y):
                px[y][x] = SHADOW
    for y in range(ART):
        for x in range(ART):
            if not in_n(x, y) and any(in_n(x + dx, y + dy)
                                      for dx in (-1, 0, 1) for dy in (-1, 0, 1)):
                px[y][x] = OUTLINE
    for y in range(ART):
        for x in range(ART):
            if in_n(x, y):
                px[y][x] = metal_index(x, y)
    for rx, ry in ((LX + 3, TY + 2), (LX + NW - 5, TY + 2)):
        px[ry][rx] = METAL[31]
        px[ry + 1][rx + 1] = METAL[2]
        px[ry][rx + 1] = px[ry + 1][rx] = METAL[14]
    # A thin dark frame.
    for i in range(ART):
        px[0][i] = px[ART - 1][i] = px[i][0] = px[i][ART - 1] = BLACK
    return px

def to_image(px):
    im = Image.new('P', (ART * SCALE, ART * SCALE))
    im.putpalette([c for rgb in pal for c in rgb])
    im.putdata([px[y // SCALE][x // SCALE]
                for y in range(ART * SCALE) for x in range(ART * SCALE)])
    return im

def to_svg(px):
    """The pixels as an SVG: one path per colour, a rectangle per run."""
    runs = {}
    for y in range(ART):
        x = 0
        while x < ART:
            c = px[y][x]
            end = x
            while end < ART and px[y][end] == c:
                end += 1
            runs.setdefault(c, []).append(f'M{x} {y}h{end - x}v1h-{end - x}z')
            x = end
    out = ['<?xml version="1.0" encoding="UTF-8"?>',
           '<!-- Generated by scripts/gen-retro-logo.py: the first frame of '
           'weststar.gif. -->',
           f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {ART} {ART}" '
           f'width="256" height="256" shape-rendering="crispEdges">']
    for c in sorted(runs):
        r, g, b = pal[c]
        out.append(f'<path fill="#{r:02x}{g:02x}{b:02x}" d="{"".join(runs[c])}"/>')
    out.append('</svg>')
    return '\n'.join(out) + '\n'

def to_ico(px, out):
    """weststar.ico: whole multiples of the art where they fit (48, 96,
    240 framed in 256), the other sizes reduced from twice the art."""
    art = to_image(px).convert('RGBA')                 # ART * SCALE square
    big = Image.new('RGBA', (256, 256))
    big.paste(art.resize((ART * 5, ART * 5), Image.NEAREST), (8, 8))
    images = {256: big}
    for size in (128, 64, 48, 32, 24, 16):
        if size % ART == 0:
            images[size] = art.resize((size, size), Image.NEAREST)
        else:
            images[size] = art.resize((size, size), Image.LANCZOS)
    big.save(out, format='ICO', sizes=[(n, n) for n in sorted(images)],
             append_images=[images[n] for n in sorted(images) if n != 256])

def main():
    args = sys.argv[1:]
    out = args[0] if len(args) > 0 else \
        'data/icons/hicolor/scalable/apps/weststar.gif'
    svg = args[1] if len(args) > 1 else \
        'data/icons/hicolor/scalable/apps/weststar.svg'
    ico = args[2] if len(args) > 2 else 'data/icons/weststar.ico'
    pixels = [frame(f) for f in range(FRAMES)]
    frames = [to_image(p) for p in pixels]
    frames[0].save(out, save_all=True, append_images=frames[1:],
                   duration=FRAME_MS, loop=0, optimize=False, disposal=1)
    print(f'{out}: {ART * SCALE}x{ART * SCALE}, {FRAMES} frames, 256 colours')
    with open(svg, 'w', encoding='utf-8', newline='\n') as f:
        f.write(to_svg(pixels[0]))
    print(f'{svg}: the first frame, {ART}x{ART} pixels')
    to_ico(pixels[0], ico)
    print(f'{ico}: 16 to 256 pixels')

if __name__ == '__main__':
    main()
