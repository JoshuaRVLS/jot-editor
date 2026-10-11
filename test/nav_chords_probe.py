#!/usr/bin/env python3
"""Probe: the navigation chords, through the real binary over a pty.

The unit cases drive the editor's own entry points, so they cannot see whether
a terminal's key sequence reaches them: `Ctrl+Right` arrives as an escape
(`ESC[1;5C`), is decoded to a key code plus a ctrl flag, and only then is asked
what it means. Each scene below sends the bytes a terminal actually sends and
reads the caret back out of the status line's `line:column` indicator.

What the scenes pin, in order of how easily it goes wrong:

  * `Ctrl+Right` moves by word (5, not 2): the chord is not a one-cell step.
  * It stops at the hump inside an identifier, so `alpha_beta` is walked name by
    name (11, not 15).
  * A second one from the end of a line crosses into the next line's first word,
    indentation skipped (2:3).
  * `Ctrl+Left` walks the same stops backwards (24, the hump's own first
    character).
  * `Ctrl+End` / `Ctrl+Home` reach the file's ends; `Ctrl+Home` after it comes
    back to 1:1.
  * `Alt+L` -- the other word chord in the docs -- is the same motion with one
    pane (the pane-focus block used to swallow it).
  * `Ctrl+Down` scrolls the view a line and leaves the caret where it was --
    the reading chord. The file is taller than the pane, so the view really can
    move; the row the second line sits on is what tells that it did.

Usage: test/nav_chords_probe.py [path-to-jot] [--dump]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary).
"""
from __future__ import annotations

import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

# The caret starts on 1:1. Line 1 mixes the shapes the motion has to tell
# apart: plain words, an underscored name and a camel-cased one, then a space,
# an operator and a lone digit. Line 2 is indented, so the line-crossing scene
# can tell "the next line's first word" from "the next line's first cell".
FIRST = "int alpha_beta = fooBarBaz + 1;"
# The indentation of line 2 is a guide cell on screen, so the needle to find it
# by is its text, not the source line it came from.
SECOND = "int gamma = 2;"
LINES = [FIRST, "  " + SECOND] + ["// pad %d" % i for i in range(58)]

# The escape sequences a terminal sends for the chords (xterm modifiers:
# 5 = Ctrl, 6 = Ctrl+Shift, with the arrow's final letter).
CTRL_RIGHT = b"\x1b[1;5C"
CTRL_LEFT = b"\x1b[1;5D"
CTRL_UP = b"\x1b[1;5A"
CTRL_DOWN = b"\x1b[1;5B"
CTRL_HOME = b"\x1b[1;5H"
CTRL_END = b"\x1b[1;5F"
CTRL_SHIFT_RIGHT = b"\x1b[1;6C"
END = b"\x1b[F"  # bare End: the line-crossing scenes start from a line's end
# Alt+L in the kitty form (108 = 'l', modifier 3 = Alt). The other probes send
# their Alt chords this way; Alt+H / Alt+L are also the pane-focus chords, and
# this is the one that says they still reach the motion with a single pane.
ALT_L = b"\x1b[108;3u"

LAST_LINE = len(LINES)
LAST_COL = len(LINES[-1]) + 1

# label: (keys, caret it must leave behind)
SCENES = {
    "ctrl+right word":     (CTRL_RIGHT, (1, 5)),
    "hump stop":           (CTRL_RIGHT * 2, (1, 11)),
    "across the line":     (END + CTRL_RIGHT, (2, 3)),
    "ctrl+left humps":     (END + CTRL_RIGHT + CTRL_LEFT * 2, (1, 24)),
    "ctrl+end":            (CTRL_END, (LAST_LINE, LAST_COL)),
    "ctrl+home":           (CTRL_END + CTRL_HOME, (1, 1)),
    "alt+l word":          (ALT_L, (1, 5)),
    # Ctrl+Shift is the resize chord (the single pane cannot resize, and the
    # toast says so): what this scene pins is that it is not a word motion.
    "ctrl+shift+right":    (CTRL_SHIFT_RIGHT, (1, 1)),
}


def write_file(root: str) -> str:
    os.makedirs(root, exist_ok=True)
    path = os.path.join(root, "nav.txt")
    with open(path, "w") as fh:
        fh.write("\n".join(LINES) + "\n")
    return path


def run(binary: str, path: str, keys: bytes, cfg: str):
    return run_in_pty(binary, [path], keys, settle=2.5, after=2.5,
                      cols=100, rows=30, cfg=cfg, cwd=os.path.dirname(path))


def run_settled(binary: str, path: str, keys: bytes, cfg: str, needle: str,
                attempts: int = 3):
    """Run a scene, retrying while the last frame came out cut short.

    The editor writes a frame in one write() and this probe kills the process to
    end a scene, so a pty that could not take the whole frame leaves the last
    one's tail unwritten -- a partially redrawn row. The scene is only valid when
    the rows it is about reconstructed, so retry until they did.
    """
    screen = run(binary, path, keys, cfg)
    for _ in range(attempts - 1):
        if needle in screen.text():
            return screen
        screen = run(binary, path, keys, cfg)
    return screen


def caret(screen):
    """The (line, column) the status line reports, 1-based, or None.

    The status line is the screen's last row, as the other probes read it.
    """
    rows = screen.text().split("\n")
    if len(rows) < screen.rows:
        return None
    match = re.search(r"\b(\d+):(\d+)\b", rows[screen.rows - 1])
    if match:
        return int(match.group(1)), int(match.group(2))
    return None


def row_of(screen, needle: str) -> int:
    """The screen row `needle` is on, or -1. Rows are the pane's own, top down."""
    for row, line in enumerate(screen.text().split("\n")):
        if needle in line:
            return row
    return -1


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    dump = "--dump" in sys.argv
    if not os.path.exists(binary):
        print(f"nav chords probe: SKIP - no binary at {binary}")
        return 2

    root = "/tmp/jot_nav_chords_probe"
    path = write_file(root)

    # A run with no keys at all: the row map the scroll scenes read against, and
    # the check that the file is open (the home screen would not show it).
    base = run_settled(binary, path, b"", "/tmp/jot_nav_chords_probe_cfg_base", FIRST)
    if dump:
        print(base.text())
        print("-" * 70)
    failures = []
    if FIRST not in base.text() or SECOND not in base.text():
        print("nav chords probe: FAIL - the file's first lines are not on screen")
        return 1
    first_row = row_of(base, FIRST)
    second_row = row_of(base, SECOND)
    print(f"base: {FIRST!r} on row {first_row}, {SECOND!r} on row {second_row}")

    for index, (label, (keys, want)) in enumerate(SCENES.items()):
        screen = run_settled(binary, path, keys, f"/tmp/jot_nav_chords_probe_cfg_{index}",
                             FIRST)
        if dump:
            print(screen.text())
            print("-" * 70)
        got = caret(screen)
        print(f"{label:<18} -> caret {got if got else '(none)'} (want {want[0]}:{want[1]})")
        if not got:
            failures.append(f"{label}: the caret indicator is gone")
        elif got != want:
            failures.append(f"{label}: expected {want[0]}:{want[1]}, landed on "
                            f"{got[0]}:{got[1]}")
        if label == "ctrl+shift+right":
            noted = "Resize unavailable" in screen.text()
            print(f"{label:<18}    resize toast on screen: {'yes' if noted else 'no'}")

    # The reading chord: the view moves, the caret does not. A 60-line file in a
    # 30-row pty leaves room to scroll, so the row the second line sits on is
    # what says the view moved -- and the caret still reporting 1:1 says the
    # chord scrolled rather than walked the caret down.
    screen = run_settled(binary, path, CTRL_DOWN, "/tmp/jot_nav_chords_probe_cfg_down", FIRST)
    if dump:
        print(screen.text())
        print("-" * 70)
    got = caret(screen)
    scrolled_row = row_of(screen, SECOND)
    print(f"ctrl+down          -> caret {got if got else '(none)'} (want 1:1), "
          f"{SECOND!r} moved from row {second_row} to row {scrolled_row}, "
          f"first line on screen: {'yes' if FIRST in screen.text() else 'no'}")
    if got != (1, 1):
        failures.append(f"ctrl+down: the caret moved ({got if got else 'gone'}); "
                        "the scroll must leave it where it was")
    elif scrolled_row != second_row - 1:
        failures.append(f"ctrl+down: the view did not move (the second line is on "
                        f"row {scrolled_row}, was {second_row})")
    elif FIRST in screen.text().split("\n")[first_row]:
        failures.append("ctrl+down: the first line is still the pane's top row")

    screen = run_settled(binary, path, CTRL_DOWN + CTRL_UP,
                         "/tmp/jot_nav_chords_probe_cfg_up", FIRST)
    if dump:
        print(screen.text())
        print("-" * 70)
    got = caret(screen)
    back_row = row_of(screen, SECOND)
    print(f"ctrl+down, ctrl+up -> caret {got if got else '(none)'} (want 1:1), "
          f"{SECOND!r} back on row {back_row} (want {second_row})")
    if got != (1, 1) or back_row != second_row:
        failures.append(f"ctrl+up: the view did not come back (caret {got}, row "
                        f"{back_row}, want 1:1 and {second_row})")

    if failures:
        for failure in failures:
            print(f"nav chords probe: FAIL - {failure}")
        return 1
    print("nav chords probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
