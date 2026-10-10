#!/usr/bin/env python3
"""Generate packaging/jot-icon-128.rgba from packaging/windows/icon.png.

The .rgba file is raw RGBA8888 (128x128, straight alpha, rounded corners
already cut away) that cmake/embed_icon.cmake embeds into the binary so the
SDL window icon (SDL_SetWindowIcon in src/gui/gui_setup.cpp) never needs a
file on disk.

The decoding and rendering are borrowed from packaging/windows/make_ico.py so
the window icon is pixel-for-pixel the same artwork as jot.ico.

Regenerate after any artwork change:
    python3 packaging/make_gui_icon.py
"""
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SIZE = 128
SRC = os.path.join(HERE, "windows", "icon.png")
OUT = os.path.join(HERE, "jot-icon-%d.rgba" % SIZE)


def load_ico_module():
    """Import packaging/windows/make_ico.py (same artwork, same renderer)."""
    path = os.path.join(HERE, "windows", "make_ico.py")
    spec = importlib.util.spec_from_file_location("jot_make_ico", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def main():
    ico = load_ico_module()
    w, h, rgba = ico.decode_png(SRC)
    data = ico.render(SIZE, rgba, w, h)
    expected = SIZE * SIZE * 4
    if len(data) != expected:
        raise SystemExit("render returned %d bytes, expected %d" % (len(data), expected))
    with open(OUT, "wb") as f:
        f.write(data)
    print("wrote %s (%d bytes, %dx%d RGBA8888)" % (OUT, len(data), SIZE, SIZE))


if __name__ == "__main__":
    sys.exit(main())
