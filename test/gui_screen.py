#!/usr/bin/env python3
"""Shared harness for probes that drive the GUI frontend (`jot --gui`).

The GUI reads X events instead of stdin, so these probes cannot use
run_in_pty's keystrokes. The shape is otherwise the house one: the real
binary, real rendering, real input, and the screen read back and asserted
on. A probe starts a private Xvfb display (never the user's), runs jot on
it, injects wheel input with XTEST, and samples window pixels and RSS.
JOT_GUI_PROBE_DISPLAY=<display> borrows an X server that is already running
instead of starting an Xvfb, for hosts that have none installed.

Usage sketch:
    session = GuiSession(binary, [file], cfg=tmp, env_extra=env)
    rows = session.capture()          # rows of (r, g, b) tuples
    marks = banded_rows(rows, (95, 95, 0))
    session.wheel(clicks=3, down=True, delay=0.3)
    flat = session.capture_bands(session.packed_color((95, 95, 0)))   # same rows
    rss = session.rss_kb()
    session.stop()
"""
from __future__ import annotations

import array
import os
import re
import subprocess
import time

try:
    import Xlib.display
    from Xlib import X, XK
    from Xlib.ext import xtest
except ImportError:  # probes exit 2 when the harness is unavailable
    Xlib = None


def _free_display(start: int = 90, end: int = 120) -> int:
    for d in range(start, end):
        if not os.path.exists(f"/tmp/.X{d}-lock") and not os.path.exists(f"/tmp/.X11-unix/X{d}"):
            return d
    raise RuntimeError("no free X display in :%d..:%d" % (start, end))


class GuiSession:
    """One jot --gui process on one private Xvfb display."""

    def __init__(self, binary, args=(), cfg="/tmp/jot_gui_probe_cfg", env_extra=None,
                 screen=(1600, 1200)):
        self.binary = binary
        self.stderr_path = os.path.join(cfg, "gui_stderr.log")
        os.makedirs(cfg, exist_ok=True)
        # JOT_GUI_PROBE_DISPLAY borrows an X server that already exists (a host
        # without Xvfb), instead of starting a private one. A borrowed display
        # is shared, so a probe on one has to find its own window rather than
        # take the harness's first: see the window-title probe.
        borrowed = os.environ.get("JOT_GUI_PROBE_DISPLAY")
        if borrowed:
            self.display_no = None
            self.display_name = borrowed
            self.xvfb = None
        else:
            self.display_no = _free_display()
            self.display_name = ":%d" % self.display_no
            self.xvfb = subprocess.Popen(
                ["Xvfb", self.display_name, "-screen", "0", "%dx%dx24" % screen],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            deadline = time.time() + 5.0
            while time.time() < deadline:
                if os.path.exists("/tmp/.X11-unix/X%d" % self.display_no):
                    break
                time.sleep(0.05)
        env = dict(os.environ)
        env["DISPLAY"] = self.display_name
        # SDL must open the private display, not whatever the host session
        # offers: with WAYLAND_DISPLAY set it prefers the host compositor and
        # the probe would watch (and flash a window on) the user's desktop.
        env["SDL_VIDEODRIVER"] = "x11"
        env["JOT_GUI_DEBUG"] = "1"
        env["JOT_CONFIG_HOME"] = cfg
        env["JOT_CACHE_HOME"] = cfg
        if env_extra:
            env.update(env_extra)
        self.stderr = open(self.stderr_path, "wb")
        self.proc = subprocess.Popen([str(binary), "--gui"] + [str(a) for a in args],
                                     env=env, stdout=subprocess.DEVNULL,
                                     stderr=self.stderr)
        self.dpy = Xlib.display.Display(self.display_name)
        self.root = self.dpy.screen().root
        self.win = None
        self._find_window()
        # The wheel handler hit-tests the pointer against the panes, so put
        # the pointer where the code pane surely is before any wheel input.
        self.warp_to_window_center()

    def _find_window(self):
        deadline = time.time() + 15.0
        while time.time() < deadline:
            if self.proc.poll() is not None:
                raise RuntimeError("jot --gui exited early (code %s)" % self.proc.returncode)
            for w in self._walk(self.root):
                try:
                    if not w.get_attributes().map_state == X.IsViewable:
                        continue
                    geom = w.get_geometry()
                    if geom.width >= 100 and geom.height >= 100:
                        self.win = w
                        self.win_geom = (geom.width, geom.height)
                        return
                except Exception:
                    continue
            time.sleep(0.1)
        raise RuntimeError("no GUI window appeared")

    def _walk(self, w):
        out = []
        try:
            children = w.query_tree().children
        except Exception:
            return out
        for c in children:
            out.append(c)
            out.extend(self._walk(c))
        return out

    def window_size(self):
        geom = self.win.get_geometry()
        return geom.width, geom.height

    def warp_to_window_center(self):
        w, h = self.window_size()
        # translate_coords maps the point from the window into the root, which
        # is the space warp_pointer addresses.
        pos = self.root.translate_coords(self.win, w // 2, h // 2)
        self.root.warp_pointer(pos.x, pos.y)
        self.dpy.sync()

    def wheel(self, clicks=1, down=True, delay=0.0):
        button = 5 if down else 4
        for _ in range(clicks):
            xtest.fake_input(self.dpy, X.ButtonPress, button)
            xtest.fake_input(self.dpy, X.ButtonRelease, button)
            self.dpy.sync()
            if delay:
                time.sleep(delay)

    def key(self, keysym, ctrl=False):
        """Press and release one key (by X keysym), optionally holding Ctrl."""
        code = self.dpy.keysym_to_keycode(keysym)
        mods = [self.dpy.keysym_to_keycode(XK.XK_Control_L)] if ctrl else []
        for mod in mods:
            xtest.fake_input(self.dpy, X.KeyPress, mod)
        xtest.fake_input(self.dpy, X.KeyPress, code)
        xtest.fake_input(self.dpy, X.KeyRelease, code)
        for mod in reversed(mods):
            xtest.fake_input(self.dpy, X.KeyRelease, mod)
        self.dpy.sync()

    def capture(self, x0=200, x1=240, y0=0, y1=None):
        """Pixels of a window strip as rows of (r, g, b) tuples.

        A narrow strip is enough to tell whole rows apart by their background
        and keeps Python-side parsing far below the animation's frame time.
        """
        w, h = self.window_size()
        if y1 is None:
            y1 = h
        x0 = max(0, min(x0, w - 2))
        x1 = max(x0 + 1, min(x1, w))
        y1 = max(y0 + 1, min(y1, h))
        img = self.win.get_image(x0, y0, x1 - x0, y1 - y0, X.ZPixmap, 0xFFFFFFFF)
        data = img.data
        width = x1 - x0
        rows = []
        stride = width * 4
        for y in range(y1 - y0):
            base = y * stride
            row = []
            for x in range(width):
                i = base + x * 4
                # LSBFirst (Xvfb on x86): bytes are B, G, R, pad.
                row.append((data[i + 2], data[i + 1], data[i]))
            rows.append(row)
        return rows

    def packed_color(self, rgb, x0=200, x1=240):
        """The window's packed 32-bit pixel for `rgb`.

        An X image carries a fourth byte next to the colour, and what the
        server puts there is its business, so the packed value is read back
        from a pixel the caller already knows the colour of.
        """
        r, g, b = rgb
        img = self.win.get_image(x0, 0, x1 - x0, self.window_size()[1],
                                 X.ZPixmap, 0xFFFFFFFF)
        data = img.data
        for i in range(0, len(data) - 3, 4):
            if data[i] == b and data[i + 1] == g and data[i + 2] == r:
                return (data[i] | (data[i + 1] << 8) | (data[i + 2] << 16)
                        | (data[i + 3] << 24))
        return None

    def capture_bands(self, value, x0=200, x1=240, y0=0, y1=None, min_fraction=0.5):
        """Screen rows that are mostly `value`, the same rule as banded_rows().

        The strip is compared as packed pixels counted in C, so a probe can
        sample several times per animation frame; capture() keeps the per-pixel
        Python loop it needs to read colours back.
        """
        w, h = self.window_size()
        if y1 is None:
            y1 = h
        x0 = max(0, min(x0, w - 2))
        x1 = max(x0 + 1, min(x1, w))
        y1 = max(y0 + 1, min(y1, h))
        img = self.win.get_image(x0, y0, x1 - x0, y1 - y0, X.ZPixmap, 0xFFFFFFFF)
        width = x1 - x0
        pixels = array.array("I")
        pixels.frombytes(img.data)
        need = min_fraction * width
        return [y for y in range(y1 - y0)
                if pixels[y * width:(y + 1) * width].count(value) >= need]

    def rss_kb(self):
        try:
            with open("/proc/%d/status" % self.proc.pid) as fh:
                for line in fh:
                    if line.startswith("VmRSS:"):
                        return int(line.split()[1])
        except OSError:
            pass
        return -1

    def debug_lines(self):
        self.stderr.flush()
        try:
            with open(self.stderr_path, "rb") as fh:
                return fh.read().decode("utf-8", "replace").splitlines()
        except OSError:
            return []

    def cell_size(self):
        """(cell_w, cell_h) pixels, parsed from jot's JOT_GUI_DEBUG boot line."""
        for line in self.debug_lines():
            match = re.search(r"cell=([0-9.]+)x([0-9.]+)", line)
            if match:
                return float(match.group(1)), float(match.group(2))
        return None

    def stop(self):
        for closer in (getattr(self, "stderr", None),):
            if closer:
                try:
                    closer.close()
                except Exception:
                    pass
        if getattr(self, "proc", None):
            self.proc.terminate()
            try:
                self.proc.wait(timeout=5)
            except Exception:
                self.proc.kill()
        if getattr(self, "xvfb", None):
            self.xvfb.terminate()
            try:
                self.xvfb.wait(timeout=5)
            except Exception:
                self.xvfb.kill()
        if getattr(self, "dpy", None):
            try:
                self.dpy.close()
            except Exception:
                pass


def banded_rows(rows, band_rgb, min_fraction=0.5):
    """Screen rows whose sampled strip is mostly `band_rgb` (a diagnostic
    band is edge-to-edge background)."""
    out = []
    for y, row in enumerate(rows):
        hits = sum(1 for px in row if px == band_rgb)
        if hits >= min_fraction * len(row):
            out.append(y)
    return out


def modal_row_color(rows):
    """The most common row background across the strip (the pane's plain bg)."""
    counts = {}
    for row in rows:
        counts[row[len(row) // 2]] = counts.get(row[len(row) // 2], 0) + 1
    return max(counts.items(), key=lambda kv: kv[1])[0]


def landmark_tops(banded, cell_h):
    """Top screen row of every fully visible run in `banded` (banded rows).

    A band clipped by the pane edge shows fewer rows than a cell, and its top
    is the edge rather than the content, so it cannot be a landmark: only runs
    a whole cell tall are kept, and those move with the content.
    """
    tops = []
    i = 0
    while i < len(banded):
        start = end = banded[i]
        while i + 1 < len(banded) and banded[i + 1] == end + 1:
            end = banded[i + 1]
            i += 1
        if end - start + 1 >= cell_h - 1:
            tops.append(start)
        i += 1
    return tops


def align_marks(tops, template_px, near=None, tolerance=3, min_hits=2):
    """Content offset that explains `tops` against landmark positions.

    A landmark at content pixel `t` sits at screen row `t - offset` plus a
    fixed pane origin, so each (top, template) pair proposes an offset. Only
    offsets that explain at least `min_hits` visible landmarks are candidates,
    and the one closest to `near` (the previous reading) wins: an irregular
    template repeats far away, and continuity is what tells the aliases apart,
    not distance from zero.

    Returns (offset, landmarks explained); offset is None when no candidate
    explains enough of them (nothing on screen lines up with the template).
    """
    candidates = sorted({round(t - y) for y in tops for t in template_px})
    best, scored = 0, []
    for offset in candidates:
        hits = 0
        for y in tops:
            if any(abs(t - (y + offset)) <= tolerance for t in template_px):
                hits += 1
        best = max(best, hits)
        scored.append((offset, hits))
    if best < min_hits:
        return None, best
    good = [offset for offset, hits in scored if hits == best]
    if near is None:
        return min(good), best
    return min(good, key=lambda offset: abs(offset - near)), best
