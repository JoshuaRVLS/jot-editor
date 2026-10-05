#!/usr/bin/env python3
"""Probe: Alt chords are live wherever the focus sits, and only type when they are text.

Alt chords are shortcuts, so the interesting question is not whether the terminal
delivers them -- it does, as `ESC <letter>` on a plain terminal and as a kitty
CSI-u report on one that speaks the protocol, and both decode to the same
modifier bit -- but whether the editor still acts on them once the focus has left
the buffer. It used to not, in two places:

  * the file explorer, which is exactly where `jot <folder>` starts and where a
    click on any file in the tree leaves the focus. Alt+W and Alt+N were dropped
    there while Ctrl chords were routed through, so Alt looked broken in a way
    that pointed at the terminal;
  * the Problems panel, which returned "handled" for every key, so Ctrl+S and
    every Alt chord did nothing while its list had focus.

Both encodings are exercised for one of the chords: a probe that only spoke
CSI-u would pass on a terminal the editor never asked for.

Usage: test/alt_chords_probe.py [path-to-jot]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary).
"""
from __future__ import annotations

import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

FAILURES: list[str] = []


def check(condition: bool, message: str) -> bool:
    if not condition:
        FAILURES.append(message)
    return condition


def csi(code: int, modifier: int = 3) -> bytes:
    """A kitty CSI-u report: Alt is bitmask 2, so Alt+<letter> is modifier 3."""
    return b"\x1b[" + str(code).encode() + b";" + str(modifier).encode() + b"u"


def legacy(letter: str) -> bytes:
    """What a terminal without the protocol sends for Alt+<letter>."""
    return b"\x1b" + letter.encode()


def click(x: int, y: int) -> bytes:
    """SGR mouse press+release at a cell (1-based on the wire)."""
    seq = b"\x1b[<0;%d;%dM\x1b[<0;%d;%dm" % (x + 1, y + 1, x + 1, y + 1)
    return seq


SOURCE = "alpha\nbeta\ngamma\ndelta\n"
ROWS = 26
COLS = 100


def workspace(tmp: str) -> str:
    os.makedirs(tmp, exist_ok=True)
    with open(os.path.join(tmp, "a.txt"), "w") as fh:
        fh.write(SOURCE)
    return tmp


def run(binary: str, tmp: str, phases, rows: int = ROWS):
    return run_in_pty(binary, [tmp], b"", settle=2.5, after=2.0, cfg=tmp + "_cfg",
                      cwd=tmp, cols=COLS, rows=rows, phases=phases)


def run_editor(binary: str, tmp: str, phases):
    """The same, opened on the file so the editor -- not the explorer -- has focus."""
    return run_in_pty(binary, [os.path.join(tmp, "a.txt")], b"", settle=2.5, after=2.0,
                      cfg=tmp + "_cfg", cwd=tmp, cols=COLS, rows=ROWS, phases=phases)


def rows_of(screen) -> list[str]:
    return screen.text().split("\n")


def tab_strip(screen) -> str:
    return rows_of(screen)[0].strip()


def pane_text(screen) -> str:
    """The whole screen: the checks below look for the buffer's own text, which
    only the editor pane carries, and the explorer's column is a different shape
    in the two runs (there is no sidebar at all when a file is the argument)."""
    return screen.text()


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    if not os.path.exists(binary):
        print("no binary at %s" % binary)
        return 2
    binary = os.path.abspath(binary)
    tmp = workspace("/tmp/jot_alt_probe_ws")

    # --- the explorer has focus, no buffer open ---------------------------------
    base = run(binary, tmp, [])
    check("alt_probe_ws" in "\n".join(rows_of(base)), "workspace did not open on the explorer")
    check("a.txt" in "\n".join(rows_of(base)), "the tree did not list the file")

    # Alt+N must reach the editor from there: a scratch tab appears. This is also
    # the tab that used to throw while the strip was laid out, so a screen with no
    # new tab is what "the keybind did nothing" looked like.
    for label, keys in (("kitty", csi(ord("n"))), ("legacy", legacy("n"))):
        screen = run(binary, tmp, [(0.2, keys)])
        check("[No Name]" in tab_strip(screen),
              "Alt+N (%s) did not open a scratch buffer while the explorer had focus" % label)

    # Alt+B hides the explorer, again from the explorer.
    screen = run(binary, tmp, [(0.2, csi(ord("b")))])
    check("alt_probe_ws" not in rows_of(screen)[0],
          "Alt+B did not hide the explorer while it had focus")

    # Alt+Shift+J splits down with the explorer focused (the pane focus and the
    # split chords share these letters, and Shift is what separates them). The
    # divider row is the proof: the same file then draws twice, one pane per
    # half of the editor column.
    screen = run(binary, tmp, [(0.3, click(4, 2)), (0.5, csi(ord("J"), 4))])
    body = rows_of(screen)
    check(any(row.count("─") > 40 for row in body),
          "Alt+Shift+J left no rule between the stacked panes while the explorer had focus")

    # --- a file is open and the explorer still has focus (clicking leaves it) ----
    opened = run(binary, tmp, [(0.3, click(4, 2))])
    check("alpha" in pane_text(opened), "clicking the tree row did not open the file")
    check(len(tab_strip(opened)) > 0, "the tab strip lost the open file's tab")

    # Alt+W closes that tab from the explorer, which is the gesture that used to
    # be swallowed whole. The explorer keeps the file open in its own tab strip
    # afterwards, so what is asserted is the file's text leaving the pane.
    screen = run(binary, tmp, [(0.3, click(4, 2)), (0.5, csi(ord("w")))])
    check("alpha" not in pane_text(screen),
          "Alt+W did not close the buffer while the explorer had focus")

    # --- the explorer keeps its own plain keys --------------------------------
    # A chord is a shortcut; a bare letter belongs to the tree. Forwarding Alt
    # must not have opened the door for typing under the explorer's cursor.
    screen = run(binary, tmp, [(0.3, click(4, 2)), (0.5, b"x")])
    check("xalpha" not in pane_text(screen),
          "a plain letter typed into the buffer while the explorer had focus")

    # --- an unclaimed Alt chord must not type either ---------------------------
    screen = run(binary, tmp, [(0.3, click(4, 2)), (0.5, csi(ord("x")))])
    check("xalpha" not in pane_text(screen),
          "Alt+X typed a letter into the buffer while the explorer had focus")

    # ...and not with the editor focused, where the chord reaches the typing path
    # and the only thing standing between Alt+X and an "x" is the modifier check.
    screen = run_editor(binary, tmp, [(0.4, csi(ord("x")))])
    check("xalpha" not in pane_text(screen),
          "Alt+X typed a letter into the buffer with the editor focused")

    # ...while a plain letter there still is text.
    screen = run_editor(binary, tmp, [(0.4, b"x")])
    check("xalpha" in pane_text(screen), "a plain letter stopped typing with the editor focused")

    # --- the Problems panel is not a keyboard dead zone -------------------------
    problems = csi(ord("m"), 6)  # Ctrl+Shift+M
    screen = run(binary, tmp, [(0.3, click(4, 2)), (0.5, problems), (0.5, csi(ord("n")))])
    check("[No Name]" in tab_strip(screen),
          "Alt+N did not open a buffer while the Problems list had focus")

    if FAILURES:
        print("alt chords probe: FAIL")
        for failure in FAILURES:
            print("  - %s" % failure)
        return 1
    print("alt chords probe: pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
