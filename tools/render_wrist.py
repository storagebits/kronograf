#!/usr/bin/env python3
"""Illustration of the watch on a wrist, with a real simulator screenshot as the display.

  python3 tools/render_wrist.py docs/screens/sim/t5-aviator-badge.png docs/screens/kronograf-wrist.png

Needs Pillow. The arm, strap and case are drawn procedurally; only the screen is real.
"""
import math
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter

W, H = 1600, 1000
SS = 2  # supersampling for smooth edges


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(len(a)))


def vgradient(size, top, bottom):
    w, h = size
    g = Image.new("RGB", (1, h))
    for y in range(h):
        g.putpixel((0, y), lerp(top, bottom, y / max(1, h - 1)))
    return g.resize((w, h))


def rounded_mask(size, radius):
    m = Image.new("L", size, 0)
    ImageDraw.Draw(m).rounded_rectangle([0, 0, size[0] - 1, size[1] - 1], radius, fill=255)
    return m


def background():
    bg = Image.new("RGB", (W, H))
    cx, cy = W * 0.55, H * 0.42
    px = bg.load()
    for y in range(H):
        for x in range(0, W, 4):
            d = math.hypot((x - cx) / W, (y - cy) / H)
            c = lerp((44, 56, 72), (10, 13, 18), min(1, d * 1.6))
            for k in range(4):
                if x + k < W:
                    px[x + k, y] = c
    return bg


def arm_layer():
    """Forearm crossing the frame, skin shaded from a top highlight to a darker underside."""
    w, h = W * SS, H * SS
    top, bottom = int(h * 0.12), int(h * 0.94)
    mask = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(mask)
    # Slight taper towards the hand (right side) and rounded ends off-frame.
    d.polygon([(-200, top - 40), (w + 200, top + 60), (w + 200, bottom - 30), (-200, bottom + 30)], fill=255)
    mask = mask.filter(ImageFilter.GaussianBlur(6 * SS))
    # Cylindrical shading across the forearm: darker edges, a highlight in the upper third.
    stops = [(0.0, (128, 84, 64)), (0.22, (234, 192, 164)), (0.55, (204, 152, 122)), (1.0, (92, 58, 44))]
    col = Image.new("RGB", (1, h))
    a, b = top + 30, bottom
    for y in range(h):
        t = min(1, max(0, (y - a) / (b - a)))
        for (t0, c0), (t1, c1) in zip(stops, stops[1:]):
            if t <= t1:
                col.putpixel((0, y), lerp(c0, c1, (t - t0) / (t1 - t0)))
                break
    skin = col.resize((w, h))
    return skin, mask, (top, bottom)


def strap_layer(arm_mask, span, cx):
    """Strap wrapped around the forearm: clipped to it, shaded darker where it turns away."""
    w, h = arm_mask.size
    top, bottom = span
    sw = 330 * SS
    strap = Image.new("RGB", (w, h), (0, 0, 0))
    col = Image.new("RGB", (1, h))
    for y in range(h):
        t = min(1, max(0, (y - top) / (bottom - top)))
        edge = min(t, 1 - t) * 2  # 0 at the edges of the arm, 1 in the middle
        col.putpixel((0, y), lerp((8, 8, 10), (40, 42, 47), min(1, edge * 2.2)))
    strap.paste(col.resize((sw, h)), (cx - sw // 2, 0))
    m = Image.new("L", (w, h), 0)
    ImageDraw.Draw(m).rectangle([cx - sw // 2, 0, cx + sw // 2, h], fill=255)
    m = ImageChops.multiply(m, arm_mask.filter(ImageFilter.MaxFilter(9)))
    d = ImageDraw.Draw(strap)
    for k in range(-12, 13):  # stitched texture lines
        y = (top + bottom) // 2 + k * 36 * SS
        d.line([(cx - sw // 2 + 28 * SS, y), (cx + sw // 2 - 28 * SS, y)], fill=(52, 54, 60), width=2 * SS)
    return strap, m


def watch_layer(screen):
    """T-Watch 2020: brushed case, black front, display, crown and strap."""
    cw, ch = 520 * SS, 560 * SS
    pad = 260 * SS
    lw, lh = cw + 2 * pad, ch + 2 * pad
    layer = Image.new("RGBA", (lw, lh), (0, 0, 0, 0))
    ox, oy = pad, pad

    sd = ImageDraw.Draw(layer)

    # Crown and button on the right side.
    sd.rounded_rectangle([ox + cw - 20 * SS, oy + 150 * SS, ox + cw + 34 * SS, oy + 230 * SS], 14 * SS, fill=(150, 158, 170, 255))
    for k in range(6):
        y = oy + (160 + k * 12) * SS
        sd.line([(ox + cw + 10 * SS, y), (ox + cw + 32 * SS, y)], fill=(95, 102, 112, 255), width=2 * SS)

    # Case: metallic gradient with a bevel.
    case = vgradient((cw, ch), (188, 196, 208), (70, 78, 90)).convert("RGBA")
    layer.paste(case, (ox, oy), rounded_mask((cw, ch), 110 * SS))
    bevel = vgradient((cw - 16 * SS, ch - 16 * SS), (120, 128, 140), (205, 212, 222)).convert("RGBA")
    layer.paste(bevel, (ox + 8 * SS, oy + 8 * SS), rounded_mask((cw - 16 * SS, ch - 16 * SS), 102 * SS))

    # Black glass front.
    gw, gh = cw - 44 * SS, ch - 44 * SS
    glass = Image.new("RGBA", (gw, gh), (6, 7, 9, 255))
    layer.paste(glass, (ox + 22 * SS, oy + 22 * SS), rounded_mask((gw, gh), 88 * SS))

    # Display: the real screenshot, slightly above centre as on the T-Watch.
    side = 400 * SS
    disp = screen.convert("RGB").resize((side, side), Image.LANCZOS)
    dx, dy = ox + (cw - side) // 2, oy + 54 * SS
    layer.paste(disp, (dx, dy))

    # Glass reflection: a soft diagonal sheen.
    sheen = Image.new("L", (lw, lh), 0)
    ImageDraw.Draw(sheen).polygon(
        [(ox + 22 * SS, oy + 22 * SS), (ox + cw * 0.62, oy + 22 * SS), (ox + 22 * SS, oy + ch * 0.55)], fill=34)
    sheen = sheen.filter(ImageFilter.GaussianBlur(30 * SS))
    glass_mask = Image.new("L", (lw, lh), 0)
    glass_mask.paste(rounded_mask((gw, gh), 88 * SS), (ox + 22 * SS, oy + 22 * SS))
    sheen = ImageChops.multiply(sheen, glass_mask)
    white = Image.new("RGBA", (lw, lh), (255, 255, 255, 255))
    layer = Image.composite(white, layer, sheen)
    return layer


def main():
    screen = Image.open(sys.argv[1])
    out = sys.argv[2]
    bg = background().resize((W * SS, H * SS))
    skin, arm_mask, span = arm_layer()

    # Arm shadow on the background.
    shadow = arm_mask.filter(ImageFilter.GaussianBlur(40 * SS)).point(lambda v: v * 0.6)
    bg = Image.composite(Image.new("RGB", bg.size, (0, 0, 0)), bg, ImageChops.offset(shadow, 0, 30 * SS))
    bg = Image.composite(skin, bg, arm_mask)
    strap, strap_mask = strap_layer(arm_mask, span, int(W * SS * 0.52))
    bg = Image.composite(strap, bg, strap_mask)

    watch = watch_layer(screen)
    # Contact shadow of the watch on the skin.
    wshadow = watch.split()[3].filter(ImageFilter.GaussianBlur(26 * SS)).point(lambda v: v * 0.55)
    px, py = int(W * SS * 0.52 - watch.width / 2), int(H * SS * 0.50 - watch.height / 2)
    bg.paste((0, 0, 0), (px + 18 * SS, py + 26 * SS), wshadow)
    bg.paste(watch, (px, py), watch)

    bg.resize((W, H), Image.LANCZOS).save(out, quality=88, optimize=True)
    print(f"illustration {out} ({W}×{H})")


if __name__ == "__main__":
    main()
