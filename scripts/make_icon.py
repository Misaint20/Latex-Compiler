#!/usr/bin/env python3
"""Generate the app icon master PNG (1024x1024) without external deps.

Design: rounded-square parchment card over an ink gradient with a brass
"LX" monogram, matching the app's ink-and-brass theme. Deterministic output.
"""
import math
import struct
import sys
import zlib

SIZE = 1024

BG_TOP = (18, 22, 30)
BG_BOTTOM = (10, 12, 18)
PARCH = (235, 229, 217)
PARCH_EDGE = (198, 189, 170)
INK = (24, 29, 40)
BRASS = (176, 141, 87)
BRASS_HI = (212, 181, 126)
RADIUS = 180


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def rounded_square_distance(px, py, size, radius):
    """Signed distance to a rounded square: negative inside, positive outside."""
    half = size / 2.0
    ox = abs(px - half) - (half - radius)
    oy = abs(py - half) - (half - radius)
    outside = math.hypot(max(ox, 0.0), max(oy, 0.0))
    inside = min(max(ox, oy), 0.0)
    return outside + inside - radius


def clamp01(v):
    return max(0.0, min(1.0, v))


def render():
    rows = []
    card = 880
    offset = (SIZE - card) // 2
    for y in range(SIZE):
        row = bytearray()
        for x in range(SIZE):
            d = rounded_square_distance(x - offset, y - offset, card, RADIUS)
            cover = clamp01(0.5 - d)
            if cover <= 0.0:
                t = y / SIZE
                px = lerp(BG_TOP, BG_BOTTOM, t)
                row.extend(px + (255,))
                continue
            t = y / SIZE
            px = lerp(BG_TOP, BG_BOTTOM, t)
            # Parchment with a slightly darker beveled rim over the card edge.
            edge = clamp01(-d / 8.0)
            paper = lerp(PARCH, PARCH_EDGE, edge)
            cx, cy = x - offset, y - offset
            in_circle = (cx - card * 0.50) ** 2 + (cy - card * 0.44) ** 2 < (card * 0.30) ** 2
            if in_circle:
                paper = lerp(paper, INK, 0.82)
            mono = monogram(cx, cy, card)
            color = lerp(paper, mono[0], mono[1])
            row.extend(tuple(int(c * cover) for c in color) + (int(255 * cover),))
        rows.append(bytes(row))
    return rows


def monogram(cx, cy, card):
    """Distance-field 'LX': returns (color, alpha) for the brass strokes."""
    s = card / 1024.0
    x, y = cx - card / 2.0, cy - card / 2.0
    thickness = 34 * s

    def seg(px, py, ax, ay, bx, by):
        apx, apy = px - ax, py - ay
        bpx, bpy = bx - ax, by - ay
        h = max(0.0, min(1.0, (apx * bpx + apy * bpy) / (bpx * bpx + bpy * bpy + 1e-6)))
        return math.hypot(apx - bpx * h, apy - bpy * h)

    dx, dy = x / s, y / s
    d = 1e9
    # L
    d = min(d, seg(dx, dy, -150, -130, -150, 150))
    d = min(d, seg(dx, dy, -150, 150, 10, 150))
    # X
    d = min(d, seg(dx, dy, 110, -130, 260, 150))
    d = min(d, seg(dx, dy, 260, -130, 110, 150))
    alpha = max(0.0, min(1.0, (thickness - d) / 1.5 + 0.5))
    if alpha <= 0.0:
        return BRASS, 0.0
    sheen = 1.0 - min(1.0, abs(dy + 20 * s) / 340.0)
    return lerp(BRASS, BRASS_HI, sheen * 0.6), alpha


def write_png(rows, path):
    def chunk(tag, data):
        body = tag + data
        return struct.pack('>I', len(data)) + body + struct.pack('>I', zlib.crc32(body))
    ihdr = struct.pack('>IIBBBBB', SIZE, SIZE, 8, 6, 0, 0, 0)
    raw = b''.join(b'\x00' + row for row in rows)
    with open(path, 'wb') as fh:
        fh.write(b'\x89PNG\r\n\x1a\n')
        fh.write(chunk(b'IHDR', ihdr))
        fh.write(chunk(b'IDAT', zlib.compress(raw, 9)))
        fh.write(chunk(b'IEND', b''))


if __name__ == '__main__':
    out = sys.argv[1] if len(sys.argv) > 1 else 'icon.png'
    write_png(render(), out)
    print('wrote', out)
