"""Inlays a simulator screenshot into the display of a photo, following its perspective.

  python3 tools/inlay_screen.py photo.png docs/screens/sim/t5-aviator-badge.png out.jpg TLx,TLy TRx,TRy BRx,BRy BLx,BLy

Needs Pillow. The four points are the display corners in the photo (top-left, top-right,
bottom-right, bottom-left)."""
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter


def solve(a, b):
    n = len(b)
    m = [row[:] + [b[i]] for i, row in enumerate(a)]
    for c in range(n):
        p = max(range(c, n), key=lambda r: abs(m[r][c]))
        m[c], m[p] = m[p], m[c]
        for r in range(n):
            if r != c:
                f = m[r][c] / m[c][c]
                m[r] = [x - f * y for x, y in zip(m[r], m[c])]
    return [m[i][n] / m[i][i] for i in range(n)]


def coeffs(dst, src):
    """PIL PERSPECTIVE coefficients mapping output (dst) points to input (src) points."""
    a, b = [], []
    for (x, y), (u, v) in zip(dst, src):
        a.append([x, y, 1, 0, 0, 0, -u * x, -u * y]); b.append(u)
        a.append([0, 0, 0, x, y, 1, -v * x, -v * y]); b.append(v)
    return solve(a, b)


photo = Image.open(sys.argv[1]).convert("RGB")
shot = Image.open(sys.argv[2]).convert("RGB")
out = sys.argv[3]
quad = [tuple(map(float, p.split(","))) for p in sys.argv[4:8]]  # TL TR BR BL in the photo

S = 4 * shot.width
src = shot.resize((S, S), Image.LANCZOS)
mask = Image.new("L", (S, S), 0)
ImageDraw.Draw(mask).rounded_rectangle([0, 0, S - 1, S - 1], int(S * 0.045), fill=255)
src.putalpha(mask)

corners = [(0, 0), (S, 0), (S, S), (0, S)]
warped = src.transform(photo.size, Image.PERSPECTIVE, coeffs(quad, corners), Image.BICUBIC)
warped = warped.filter(ImageFilter.GaussianBlur(0.5))
alpha = warped.split()[3]
screen = warped.convert("RGB")
# No blend with the photo: a dial already on its display would show through.
result = Image.composite(screen, photo, alpha)
result.save(out, quality=90, optimize=True)
print(out, result.size)
