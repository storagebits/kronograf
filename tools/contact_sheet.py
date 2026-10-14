#!/usr/bin/env python3
"""Contact sheet of 240×240 PNG screenshots (8-bit RGB), dependency-free.

  tools/contact_sheet.py out.png a.png b.png … [--cols 3] [--scale 2]
"""
import argparse
import struct
import zlib


def read_png(path):
    data = open(path, "rb").read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", path
    pos, idat, w = 8, b"", 0
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", body[:10])
            assert depth == 8 and ctype in (2, 6), f"{path}: unsupported format"
            bpp = 3 if ctype == 2 else 4
        elif kind == b"IDAT":
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev = [], bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1: line[x] = (line[x] + a) & 255
            elif f == 2: line[x] = (line[x] + b) & 255
            elif f == 3: line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line[i] for i in range(stride) if i % bpp < 3) if bpp == 4 else bytes(line))
        prev = line
    return w, h, rows


def write_png(path, w, h, rows):
    def chunk(kind, body):
        c = kind + body
        return struct.pack(">I", len(body)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)
    raw = b"".join(b"\x00" + r for r in rows)
    out = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    out += chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
    open(path, "wb").write(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("images", nargs="+")
    ap.add_argument("--cols", type=int, default=3)
    ap.add_argument("--scale", type=int, default=1)
    a = ap.parse_args()
    imgs = [read_png(p) for p in a.images]
    tw, th, gap, s = imgs[0][0], imgs[0][1], 8, a.scale
    cols = min(a.cols, len(imgs))
    lines = (len(imgs) + cols - 1) // cols
    W, H = cols * tw * s + (cols + 1) * gap, lines * th * s + (lines + 1) * gap
    canvas = [bytearray(b"\x30" * (W * 3)) for _ in range(H)]
    for i, (w, h, rows) in enumerate(imgs):
        ox = gap + (i % cols) * (tw * s + gap)
        oy = gap + (i // cols) * (th * s + gap)
        for y in range(h):
            row = rows[y]
            scaled = bytearray()
            for x in range(w):
                scaled += row[x * 3:x * 3 + 3] * s
            for k in range(s):
                canvas[oy + y * s + k][ox * 3:(ox + w * s) * 3] = scaled
    write_png(a.out, W, H, [bytes(r) for r in canvas])
    print(f"sheet {a.out} ({W}×{H})")


if __name__ == "__main__":
    main()
