#!/usr/bin/env python3
"""Probe: the GUI window behaves like a desktop window, not just a GL surface.

Three things the frontend owes the desktop, none of which a terminal screen can
show: a title naming the active file (with the unsaved marker), a minimum size
the window manager enforces, and a clipboard that works on a machine where
xclip/wl-copy are not installed. Everything else about the frontend is painted
pixels; these are window properties and the X selection, so they are read back
from the server rather than sampled from the screen.

Scenes:
  * title: the window names the file it opened; editing marks it unsaved, and a
    save takes the mark off again,
  * minimum size: WM_NORMAL_HINTS carries a floor of 20x5 cells at the current
    font, so the window cannot be dragged down to a grid no layout fits in,
  * clipboard: with neither xclip nor wl-copy on the frontend's PATH, Ctrl+A +
    Ctrl+C still puts the buffer on the CLIPBOARD selection the desktop reads
    (UI::set_clipboard_text), which is the half a GUI cannot outsource to a
    terminal's own selection handling.

Usage: test/gui_window_probe.py [path-to-jot] [--dump]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary, no python-xlib, no display).
"""
from __future__ import annotations

import math
import os
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
try:
    from gui_screen import GuiSession
except ImportError:
    GuiSession = None

if GuiSession is not None:
    try:
        import Xlib.display
        from Xlib import X, XK
        from Xlib.ext import xtest
    except ImportError:
        GuiSession = None

# The two scenes that need different environments (the clipboard one hides
# xclip) run one after the other on differently named files, so a borrowed
# display never has to tell two of the probe's own windows apart by title.
MAIN_FILE = "int alpha = 1;\nint beta = 2;\nint gamma = 3;\n"
CLIP_FILE = "void clip_probe() {\n  return;\n}\n"
CLIP_MARK = "void clip_probe() {"


def workspace(tmp: str) -> tuple:
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(os.path.join(tmp, "configs"))
    os.makedirs(os.path.join(tmp, "emptybin"))
    with open(os.path.join(tmp, "configs", "settings.conf"), "w") as fh:
        fh.write("# jot configuration file\n\nautosave=false\n")
    main_path = os.path.join(tmp, "main.cpp")
    clip_path = os.path.join(tmp, "clip.cpp")
    with open(main_path, "w") as fh:
        fh.write(MAIN_FILE)
    with open(clip_path, "w") as fh:
        fh.write(CLIP_FILE)
    return main_path, clip_path


def walk(session):
    out = []

    def visit(w):
        try:
            children = w.query_tree().children
        except Exception:
            return
        for child in children:
            out.append(child)
            visit(child)

    visit(session.root)
    return out


def window_name(session, win):
    """The name the window manager shows: _NET_WM_NAME first, then WM_NAME."""
    try:
        prop = win.get_full_property(session.dpy.intern_atom("_NET_WM_NAME"),
                                     session.dpy.intern_atom("UTF8_STRING"))
        if prop is not None:
            value = prop.value
            if isinstance(value, bytes):
                return value.decode("utf-8", "replace")
            return str(value)
    except Exception:
        pass
    return win.get_wm_name()


def attach_window(session, title_suffix="- jot", timeout=15.0):
    """The probe's own window, found by its title.

    A borrowed display is shared with whatever else is on it, so the harness's
    \"first viewable window of a usable size\" can be somebody else's: the probe
    claims the window whose name is the one it is about to assert on instead.
    """
    deadline = time.time() + timeout
    while time.time() < deadline:
        if session.proc.poll() is not None:
            raise RuntimeError("jot --gui exited early (code %s)" % session.proc.returncode)
        for win in walk(session):
            try:
                if win.get_attributes().map_state != X.IsViewable:
                    continue
                name = window_name(session, win) or ""
                geom = win.get_geometry()
                if name.endswith(title_suffix) and geom.width >= 100 and geom.height >= 100:
                    session.win = win
                    return win
            except Exception:
                continue
        time.sleep(0.1)
    raise RuntimeError("no window titled *%s appeared" % title_suffix)


def wait_for_title(session, win, want, timeout=5.0):
    """The title once it reads `want`, or the last title seen (the caller fails)."""
    deadline = time.time() + timeout
    seen = window_name(session, win)
    while time.time() < deadline:
        if seen == want:
            return seen
        time.sleep(0.05)
        seen = window_name(session, win)
    return seen


def click(session, win):
    """Focus the window and click its centre, so keys land in the code pane."""
    try:
        session.dpy.set_input_focus(win, X.RevertToParent, X.CurrentTime)
    except Exception:
        pass
    session.warp_to_window_center()
    xtest.fake_input(session.dpy, X.ButtonPress, 1)
    xtest.fake_input(session.dpy, X.ButtonRelease, 1)
    session.dpy.sync()
    time.sleep(0.3)


def title_scene(session, win, name, failures, dump):
    clean = "%s - jot" % name
    marked = "%s * - jot" % name
    base = wait_for_title(session, win, clean)
    if dump:
        print("   opened title: %r" % base)
    if base != clean:
        failures.append("the window opened as %r, not '<file> - jot'" % base)
        return
    click(session, win)
    # A digit is text in every mode and no shortcut anywhere: typing one is what
    # the title's unsaved marker is about.
    session.key(XK.XK_1)
    edited = wait_for_title(session, win, marked, 3.0)
    if dump:
        print("   after typing: %r" % edited)
    if edited != marked:
        failures.append("an edit left the title at %r (no unsaved marker)" % edited)
        return
    # Saving the change takes the marker off again.
    session.key(XK.XK_s, ctrl=True)
    saved = wait_for_title(session, win, clean, 3.0)
    if dump:
        print("   after save:   %r" % saved)
    if saved != clean:
        failures.append("a save left the title at %r (marker not cleared)" % saved)


def minimum_size_scene(session, win, failures, dump):
    size = session.cell_size()
    if size is None:
        failures.append("JOT_GUI_DEBUG did not report the cell size")
        return
    cell_w, cell_h = size
    want = (math.ceil(cell_w * 20), math.ceil(cell_h * 5))
    hints = win.get_wm_normal_hints()
    if hints is None or hints.min_width is None or hints.min_height is None:
        failures.append("the window carries no minimum size hint")
        return
    got = (hints.min_width, hints.min_height)
    if dump:
        print("   minimum size: %s (cell %.0fx%.0f, want 20x5 cells)"
              % (str(got), cell_w, cell_h))
    if got != want:
        failures.append("minimum size hint is %s, not the 20x5 cells of this font %s"
                        % (str(got), str(want)))
    # The hint is only worth anything if the window is at least that big.
    geom = win.get_geometry()
    if geom.width < got[0] or geom.height < got[1]:
        failures.append("the window is %dx%d, below its own minimum %s"
                        % (geom.width, geom.height, str(got)))


def clipboard_scene(binary, clip_path, tmp, failures, dump):
    """Ctrl+A + Ctrl+C with no xclip/wl-copy on the frontend's PATH."""
    reader = shutil.which("xclip")
    if reader is None:
        print("gui window probe: SKIP clipboard scene - no xclip to read the selection")
        return
    session = GuiSession(binary, [clip_path], cfg=os.path.join(tmp, "clip_cfg"),
                         env_extra={"PATH": os.path.join(tmp, "emptybin")})
    try:
        win = attach_window(session)
        name = os.path.basename(clip_path)
        base = wait_for_title(session, win, "%s - jot" % name)
        if base != "%s - jot" % name:
            failures.append("clipboard scene window opened as %r" % base)
            return
        click(session, win)
        session.key(XK.XK_a, ctrl=True)  # select all
        session.key(XK.XK_c, ctrl=True)  # copy
        # The frontend is the selection's owner now, so xclip's request for it is
        # served by jot's own SDL connection -- the path a machine without the
        # helper tools has left.
        text = ""
        deadline = time.time() + 5.0
        while time.time() < deadline:
            try:
                text = subprocess.run(
                    [reader, "-selection", "clipboard", "-o"],
                    env={**os.environ, "DISPLAY": session.display_name},
                    stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                    timeout=3.0).stdout.decode("utf-8", "replace")
            except Exception:
                text = ""
            if CLIP_MARK in text:
                break
            time.sleep(0.2)
        if dump:
            print("   clipboard read back: %r" % text[:80])
        if CLIP_MARK not in text:
            failures.append("with no xclip/wl-copy on PATH the yank never reached the "
                            "CLIPBOARD selection (read back %r)" % text[:80])
    finally:
        session.stop()


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    dump = "--dump" in sys.argv
    if not os.path.exists(binary):
        print("gui window probe: SKIP - no binary at %s" % binary)
        return 2
    if GuiSession is None:
        print("gui window probe: SKIP - python-xlib is not installed")
        return 2

    tmp = "/tmp/jot_gui_window_probe"
    main_path, clip_path = workspace(tmp)
    failures = []
    session = GuiSession(binary, [main_path], cfg=os.path.join(tmp, "main_cfg"))
    try:
        win = attach_window(session)
        title_scene(session, win, os.path.basename(main_path), failures, dump)
        minimum_size_scene(session, win, failures, dump)
    finally:
        session.stop()
    clipboard_scene(binary, clip_path, tmp, failures, dump)

    if failures:
        for failure in failures:
            print("gui window probe: FAIL - %s" % failure)
        return 1
    print("gui window probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
