#!/usr/bin/env python3
"""Generate packaging/windows/jot.ico from packaging/windows/icon.png.

Pure Python (no PIL): decodes the PNG by hand (zlib + scanline filters),
locates the rounded icon tile via its cream border, cuts transparent corners,
and box-downsamples to the classic multi-size ICO set (16..128px).

Usage: python3 make_ico.py
"""
import math
import os
import struct
import sys
import zlib

SIZES = [16, 24, 32, 48, 64, 128]

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "icon.png")
OUT = os.path.join(HERE, "jot.ico")


# ── PNG decoding ──────────────────────────────────────────────────────────────

def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def decode_png(path):
    """Decode an 8-bit RGB/RGBA PNG into (width, height, bytes RGBA)."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos = 8
    width = height = bit_depth = color_type = 0
    idat = bytearray()
    while pos + 8 <= len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        ctype = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if ctype == b"IHDR":
            width, height, bit_depth, color_type = struct.unpack(">IIBB", chunk[:10])
        elif ctype == b"IDAT":
            idat += chunk
        elif ctype == b"IEND":
            break
    if bit_depth != 8:
        raise ValueError("unsupported bit depth: %d" % bit_depth)
    if color_type not in (2, 6):
        raise ValueError("unsupported color type: %d" % color_type)
    channels = 3 if color_type == 2 else 4
    stride = width * channels
    raw = zlib.decompress(bytes(idat))
    rows = bytearray()
    prev = bytearray(stride)
    off = 0
    for _ in range(height):
        ftype = raw[off]
        off += 1
        line = bytearray(raw[off:off + stride])
        off += stride
        if ftype == 1:  # Sub
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 0xFF
        elif ftype == 2:  # Up
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif ftype == 3:  # Average
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif ftype == 4:  # Paeth
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                up = prev[i]
                ul = prev[i - channels] if i >= channels else 0
                line[i] = (line[i] + paeth(left, up, ul)) & 0xFF
        elif ftype != 0:
            raise ValueError("bad filter: %d" % ftype)
        rows += line
        prev = line
    rgba = bytearray()
    for i in range(0, len(rows), channels):
        rgba += rows[i:i + 3]
        rgba.append(255 if color_type == 2 else rows[i + 3])
    return width, height, bytes(rgba)


# ── Tile detection + alpha ────────────────────────────────────────────────────

def clamp(v, lo, hi):
    return max(lo, min(hi, v))


def find_tile_bounds(rgba, w, h):
    """Locate the rounded tile via its bright cream border."""
    x0, y0, x1, y1 = w, h, -1, -1
    for y in range(h):
        row = y * w * 4
        for x in range(w):
            i = row + x * 4
            r, g, b = rgba[i], rgba[i + 1], rgba[i + 2]
            if r > 170 and g > 170 and b > 150:  # cream-ish
                x0 = min(x0, x)
                y0 = min(y0, y)
                x1 = max(x1, x)
                y1 = max(y1, y)
    if x1 < 0:
        return 0, 0, w, h
    return x0, y0, x1 - x0 + 1, y1 - y0 + 1


def rounded_rect_alpha(x, y, bx, by, bw, bh, r):
    """Coverage (0..1) of a rounded rect, 1 inside, soft 1px edge."""
    cx = bx + bw / 2.0 - 0.5
    cy = by + bh / 2.0 - 0.5
    hw = bw / 2.0 - r
    hh = bh / 2.0 - r
    qx = abs(x - cx) - hw
    qy = abs(y - cy) - hh
    ox, oy = max(qx, 0.0), max(qy, 0.0)
    d = math.hypot(ox, oy) + min(max(qx, qy), 0.0) - r
    return clamp(0.5 - d, 0.0, 1.0)


# ── Downsampling ──────────────────────────────────────────────────────────────

def downsample(rgba, sw, sh, n):
    """Area-average downsample with premultiplied alpha (clean edges)."""
    out = bytearray()
    for oy in range(n):
        y0 = oy * sh // n
        y1 = max(y0 + 1, (oy + 1) * sh // n)
        for ox in range(n):
            x0 = ox * sw // n
            x1 = max(x0 + 1, (ox + 1) * sw // n)
            ar = ag = ab = aa = 0.0
            for y in range(y0, y1):
                row = y * sw * 4
                for x in range(x0, x1):
                    i = row + x * 4
                    a = rgba[i + 3] / 255.0
                    ar += rgba[i] * a
                    ag += rgba[i + 1] * a
                    ab += rgba[i + 2] * a
                    aa += a
            count = (x1 - x0) * (y1 - y0)
            if count == 0 or aa <= 0.0:
                out += b"\x00\x00\x00\x00"
                continue
            out += bytes((
                int(ar / aa + 0.5),
                int(ag / aa + 0.5),
                int(ab / aa + 0.5),
                int(aa / count * 255 + 0.5),
            ))
    return bytes(out)


def render(size, rgba, sw, sh):
    """Downsample, then apply rounded-tile alpha in the target space."""
    bx, by, bw, bh = find_tile_bounds(rgba, sw, sh)
    radius = bw * 0.18
    px = downsample(rgba, sw, sh, size)
    out = bytearray()
    for yy in range(size):
        for xx in range(size):
            i = (yy * size + xx) * 4
            # map target pixel center back to source space for the SDF
            sx = (xx + 0.5) * sw / size
            sy = (yy + 0.5) * sh / size
            a = rounded_rect_alpha(sx, sy, bx, by, bw, bh, radius)
            if a <= 0.01:
                out += b"\x00\x00\x00\x00"
                continue
            r, g, b, oa = px[i], px[i + 1], px[i + 2], px[i + 3]
            alpha = int(oa * a + 0.5)
            out += bytes((r, g, b, alpha))
    return bytes(out)


def bmp_entry(size, rgba, sw, sh):
    data = render(size, rgba, sw, sh)
    header = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0,
                         len(data), 0, 0, 0, 0)
    return header + data


def main():
    sw, sh, rgba = decode_png(SRC)
    print("decoded %s: %dx%d" % (os.path.basename(SRC), sw, sh))
    entries = []
    blob = b""
    for size in SIZES:
        raw = bmp_entry(size, rgba, sw, sh)
        entries.append((size, size, len(raw), 6 + 16 * len(SIZES) + len(blob)))
        blob += raw
    header = struct.pack("<HHH", 0, 1, len(SIZES))
    for w, h, byte_count, offset in entries:
        header += struct.pack("<BBBBHHII", w & 0xFF, h & 0xFF, 0, 0, 1, 32,
                              byte_count, offset)
    with open(OUT, "wb") as f:
        f.write(header + blob)
    print("wrote", OUT, len(header + blob), "bytes")


if __name__ == "__main__":
    sys.exit(main())