#!/usr/bin/env python3
"""Generate packaging/windows/jot.ico (pure Python, no PIL).

Design: dark rounded square with a cyan terminal prompt ">_" - matches the
editor's identity. Sizes 16..128 as BMP entries inside the ICO container.
"""
import struct, math, os

SIZES = [16, 24, 32, 48, 64, 128]

BG = (26, 27, 38)         # #1a1b26 dark navy
FG = (122, 162, 247)      # #7aa2f7 soft blue chevron
CURSOR = (192, 202, 245)  # #c0caf5 near-white cursor


def clamp(v, lo, hi):
    return max(lo, min(hi, v))


def rounded_rect_sdf(x, y, s, r):
    """Signed distance to a centered rounded square, negative inside."""
    half = s / 2.0
    qx = abs(x - half + 0.5) - (half - r)
    qy = abs(y - half + 0.5) - (half - r)
    ox, oy = max(qx, 0.0), max(qy, 0.0)
    return math.hypot(ox, oy) + min(max(qx, qy), 0.0) - r


def seg_dist(x, y, x1, y1, x2, y2):
    vx, vy = x2 - x1, y2 - y1
    l2 = vx * vx + vy * vy
    t = 0.0 if l2 == 0 else clamp(((x - x1) * vx + (y - y1) * vy) / l2, 0.0, 1.0)
    return math.hypot(x - (x1 + t * vx), y - (y1 + t * vy))


def chevron_alpha(x, y, s):
    """Coverage for a '>' chevron drawn as two thick segments."""
    t = max(1.5, s / 9.0)
    cx, cy = s * 0.34, s * 0.56
    arm = s * 0.30
    d1 = seg_dist(x, y, cx, cy, cx + arm, cy + arm * 0.55)
    d2 = seg_dist(x, y, cx, cy, cx + arm, cy + arm * 1.1)
    return clamp(0.5 - (min(d1, d2) - t / 2.0), 0.0, 1.0)


def cursor_alpha(x, y, s):
    w = max(1.5, s / 11.0)
    h = max(3.0, s / 3.6)
    x0, y0 = s * 0.70, s * 0.56
    if x < x0 or x > x0 + w or y < y0 or y > y0 + h:
        return 0.0
    # soft vertical ends
    return clamp(min(y - y0 + 0.5, y0 + h - y + 0.5), 0.0, 1.0)


def render(size):
    r = max(3.0, size * 0.18)
    px = bytearray()
    for yy in range(size):
        for xx in range(size):
            x, y = xx, size - 1 - yy  # bottom-up rows
            bg_a = clamp(0.5 - rounded_rect_sdf(x, y, size, r), 0.0, 1.0)
            if bg_a <= 0.01:
                px += bytes((0, 0, 0, 0))
                continue
            c = BG
            ca = chevron_alpha(x, y, size)
            k = cursor_alpha(x, y, size)
            if k > ca:
                c, a = CURSOR, k * bg_a
            elif ca > 0:
                c, a = FG, ca * bg_a
            else:
                a = bg_a
            # blend the glyph over the background inside the tile
            if ca > 0 or k > 0:
                bgr = (c[0] * a + BG[0] * (bg_a - a)) / max(bg_a, 1e-9)
                bgg = (c[1] * a + BG[1] * (bg_a - a)) / max(bg_a, 1e-9)
                bgb = (c[2] * a + BG[2] * (bg_a - a)) / max(bg_a, 1e-9)
                r_, g_, b_ = int(bgr), int(bgg), int(bgb)
            else:
                r_, g_, b_ = c
            px += bytes((b_, g_, r_, int(255 * bg_a)))
    return bytes(px)


def bmp_entry(size):
    data = render(size)
    header = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0,
                         len(data), 0, 0, 0, 0)
    return header + data


entries = []
blob = b""
for size in SIZES:
    raw = bmp_entry(size)
    entries.append((size, size, len(raw), 6 + 16 * len(SIZES) + len(blob)))
    blob += raw

header = struct.pack("<HHH", 0, 1, len(SIZES))
for w, h, byte_count, offset in entries:
    header += struct.pack("<BBBBHHII", w & 0xFF, h & 0xFF, 0, 0, 1, 32,
                          byte_count, offset)

out = os.path.join(os.path.join(os.path.dirname(os.path.abspath(__file__)), "jot.ico"))
os.makedirs(os.path.dirname(out), exist_ok=True)
with open(out, "wb") as f:
    f.write(header + blob)
print("wrote", out, len(header + blob), "bytes")