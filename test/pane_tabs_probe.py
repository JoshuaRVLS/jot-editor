#!/usr/bin/env python3
"""Probe: per-pane tab strips (VSCode editor groups) in the real terminal.

`pane_tabs=auto` spends a pane's first row on its own strip only where the
workspace strip cannot speak for the pane - a split, or `tabline = false` - so
a lone pane keeps row 0 as its only strip and never paints the same list twice.
The browser drives the real binary over a pty and reads the truecolour cells
back, so the renderer's paint and the mouse routing are checked, not a model:

  * scene 1 - one pane, two files: only the workspace strip on row 0 lists
    them, and the active file's text starts on row 1 with no second strip;
  * scene 2 - after a split both groups paint their own strip on row 1; a click
    on the left strip's first tab switches that group alone (its text and inks
    swap) while the right group keeps the file it had;
  * scene 3 - a drag of pane 0's `b.txt` tab onto pane 1's strip tail moves it
    (the source group falls back to one tab and hides its strip, the target
    shows the tab it received as the active one);
  * scene 4 - the same drag onto pane 1's right edge splits it instead: the
    third group holds only the dragged tab, so the source's text row and the
    new group's text both appear beside the middle group.

Usage: test/pane_tabs_probe.py [path-to-jot-binary] [--dump]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary).
"""
from __future__ import annotations

import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

# jot-dark's tab inks, tagged as truecolour cells (1000 + 0xRRGGBB).
TAB_INACTIVE_FG = 1000 + 0x7F848E  # TabLine   fg
TAB_ACTIVE_FG = 1000 + 0xDCDCDC  # TabLineSel fg
TAB_BG = 1000 + 0x21252B  # TabLine   bg
BODY_BG = 1000 + 0x282C34  # Normal    bg

# Ex commands run through the command palette (Ctrl+P, the query, Enter): the
# editor has no `:` command line of its own.
CTRL_P = b"\x10"
ENTER = b"\r"

A = "alpha\nsecond\n"
B = "beta\nsecond\n"
C = "gamma\nsecond\n"


def ex(command: str) -> bytes:
    return CTRL_P + command.encode() + ENTER


def row_text(screen, row: int) -> str:
    return "".join(screen.cells[row])


def find_row(screen, needle: str, first: int = 0, last: int = None) -> int:
    """The first screen row in [first, last) holding the needle, or -1."""
    for y in range(first, screen.rows if last is None else last):
        if needle in row_text(screen, y):
            return y
    return -1


def toast_gone(screen) -> bool:
    """The startup query notice is a toast in the top-right corner: it covers
    the strip row until it times out."""
    return "Tree-sitter" not in screen.text()


def click(x: int, y: int) -> bytes:
    """SGR left press and release on a 0-based screen cell."""
    return (b"\x1b[<0;%d;%dM" % (x + 1, y + 1)) + (b"\x1b[<0;%d;%dm" % (x + 1, y + 1))


def drag(x0: int, y0: int, x1: int, y1: int) -> bytes:
    """SGR left press, motions, release: one tab dragged to another spot."""
    return ((b"\x1b[<0;%d;%dM" % (x0 + 1, y0 + 1))
            + (b"\x1b[<32;%d;%dM" % (x0 + 1, y0 + 1)) * 2
            + (b"\x1b[<32;%d;%dM" % (x1 + 1, y1 + 1)) * 2
            + (b"\x1b[<0;%d;%dm" % (x1 + 1, y1 + 1)))


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    dump = "--dump" in sys.argv
    if not os.path.exists(binary):
        print("pane tabs probe: SKIP - no binary at %s" % binary)
        return 2

    work = "/tmp/jot_pane_tabs_probe"
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    a = os.path.join(work, "a.txt")
    b = os.path.join(work, "b.txt")
    c = os.path.join(work, "c.txt")
    for path, text in ((a, A), (b, B), (c, C)):
        with open(path, "w") as fh:
            fh.write(text)
    cfg = os.path.join(work, "cfg")

    failures = []

    # Scene 1: a lone pane leaves the tab row to the workspace strip.
    screen = run_in_pty(binary, [a], b"", settle=2.5, after=0.5, cols=100, rows=30,
                        cfg=cfg, cwd=work,
                        phases=[(0.6, ex("e b.txt")),
                                (0.0, lambda s: "beta" in s.text()),
                                (0.0, toast_gone),
                                (0.6, b"")])
    if dump:
        print(screen.text())
        print("-" * 70)
    top = row_text(screen, 0)
    if "a.txt" not in top or "b.txt" not in top:
        failures.append("the workspace strip lost a tab (%r)" % top)
    else:
        # The strip paints only its tabs: past the last one the row keeps the
        # editor background, not a full-width TabLine band (which used to run
        # to the screen edge and read as a gap beside the last tab).
        band_cells = [x for x, c in enumerate(screen.bg[0]) if c == TAB_BG]
        tail = screen.bg[0][band_cells[-1] + 1:] if band_cells else []
        if set(tail) - {BODY_BG}:
            failures.append("row 0 keeps a TabLine band right of the tabs (%s)"
                            % sorted(set(tail)))
    second = find_row(screen, "a.txt", 1, 5)
    if second >= 0:
        failures.append("a lone pane painted a second strip on row %d (%r)"
                        % (second, row_text(screen, second)))
    if find_row(screen, "beta", 1, 5) < 0:
        failures.append("the active file's text does not start under the strip")
    elif screen.bg[1][screen.cols - 1] != BODY_BG:
        failures.append("row 1 lost the buffer's background (%s)"
                        % screen.bg[1][screen.cols - 1])

    # Scene 2: the split paints a strip per group; a click on one group's tab
    # switches that group alone. The drag scenes build on the same phases.
    def two_files_phases():
        return [(0.6, ex("e b.txt")),
                (0.0, lambda s: "beta" in s.text())]

    def split_phases():
        return two_files_phases() + [
            (0.6, ex("vsp")),
            # Both groups carry the same pair, so row 1 holds the name twice
            # once the split has landed.
            (0.0, lambda s: s.rows > 1 and row_text(s, 1).count("a.txt") >= 2),
            (0.0, toast_gone),
            (0.6, b"")]

    def drags_phases():
        return split_phases()[:-1] + [
            (0.6, ex("e c.txt")),
            (0.0, lambda s: find_row(s, "c.txt", 1, 5) >= 0),
            (0.0, toast_gone),
            (0.6, b"")]

    screen = run_in_pty(binary, [a], b"", settle=2.5, after=0.5, cols=100, rows=30,
                        cfg=cfg, cwd=work, phases=split_phases())
    if dump:
        print(screen.text())
        print("-" * 70)
    strip = find_row(screen, "a.txt", 1, 5)
    if strip < 0:
        failures.append("the split never painted both pane strips")
        strip = 1
    row = row_text(screen, strip)
    if row.count("a.txt") < 2:
        failures.append("the split did not give each group its own strip (%r)" % row)
    x_a = row.find("a.txt")
    x_b = row.find("b.txt")
    if x_a < 0 or x_b < 0:
        failures.append("a pane strip is missing a tab (%r)" % row)
        x_a, x_b = 3, 9
    else:
        # The right group opened the second file last, so it is the active tab
        # there: TabLineSel's ink on the right, TabLine's on the left.
        right = row.find("b.txt", 50)
        if right >= 0 and screen.fg[strip][right] != TAB_ACTIVE_FG:
            failures.append("the right group's active tab is %s, want TabLineSel's ink"
                            % screen.fg[strip][right])

    screen = run_in_pty(binary, [a], b"", settle=2.5, after=0.5, cols=100, rows=30,
                        cfg=cfg, cwd=work,
                        phases=split_phases() + [(0.6, click(x_a, strip)),
                                                 (0.0, lambda s: "alpha" in s.text()),
                                                 (0.6, b"")])
    if dump:
        print(screen.text())
        print("-" * 70)
    row = row_text(screen, strip)
    x_a = row.find("a.txt")
    x_b = row.find("b.txt")
    if x_a >= 0 and screen.fg[strip][x_a] != TAB_ACTIVE_FG:
        failures.append("the clicked tab did not take TabLineSel's ink (%s)"
                        % screen.fg[strip][x_a])
    if x_b >= 0 and screen.fg[strip][x_b] != TAB_INACTIVE_FG:
        failures.append("the tab left behind kept the active ink (%s)"
                        % screen.fg[strip][x_b])
    if find_row(screen, "alpha", strip + 1, strip + 5) < 0:
        failures.append("the click did not switch the left group to the first file")
    if find_row(screen, "beta", strip + 1, strip + 5) < 0:
        failures.append("the click took the file away from the right group")

    # The drag scenes need the drag's coordinates, which depend on where the
    # split lands; one setup run measures them, then the drags run on the same
    # deterministic layout. Pane 0 holds [a, b], pane 1 [a, b, c] with c in
    # front, and both strips are on screen.
    screen = run_in_pty(binary, [a], b"", settle=2.5, after=0.5, cols=100, rows=30,
                        cfg=cfg, cwd=work, phases=drags_phases())
    if dump:
        print(screen.text())
        print("-" * 70)
    strip = find_row(screen, "a.txt", 1, 5)
    if strip < 0:
        failures.append("the split never painted both pane strips")
        strip = 1
    left = row_text(screen, strip)[:50]
    src = left.find("b.txt")
    if src < 0:
        failures.append("pane 0's strip is missing the tab to drag (%r)" % left)
        src = 4
    if find_row(screen, "gamma") < 0:
        failures.append("the third file never opened in the right pane")

    # Scene 3: drag pane 0's b.txt onto pane 1's strip tail (a move).
    screen = run_in_pty(binary, [a], b"", settle=2.5, after=0.5, cols=100, rows=30,
                        cfg=cfg, cwd=work,
                        phases=drags_phases() + [(0.6, drag(src, strip, 98, strip)),
                                                 (0.6, b"")])
    if dump:
        print(screen.text())
        print("-" * 70)
    row = row_text(screen, strip)
    if "alpha" not in row[:50]:
        failures.append("the emptied pane did not fall back to its remaining tab (%r)"
                        % row[:50])
    if "b.txt" in row[:50]:
        failures.append("the source pane still draws the moved tab (%r)" % row[:50])
    if "beta" not in row_text(screen, strip + 1)[50:]:
        failures.append("the target pane does not show the moved tab (%r)"
                        % row_text(screen, strip + 1)[50:])
    moved_x = row.find("b.txt", 50)
    if moved_x < 0:
        failures.append("the moved tab is missing from the target's strip")
    elif screen.fg[strip][moved_x] != TAB_ACTIVE_FG:
        failures.append("the moved tab is not the target's active one (%s)"
                        % screen.fg[strip][moved_x])
    c_x = row.find("c.txt", 50)
    if c_x >= 0 and screen.fg[strip][c_x] != TAB_INACTIVE_FG:
        failures.append("the tab the move displaced kept the active ink (%s)"
                        % screen.fg[strip][c_x])

    # Scene 4: the same drag onto pane 1's right edge splits instead.
    screen = run_in_pty(binary, [a], b"", settle=2.5, after=0.5, cols=100, rows=30,
                        cfg=cfg, cwd=work,
                        phases=drags_phases() + [(0.6, drag(src, strip, 98, strip + 5)),
                                                 (0.6, b"")])
    if dump:
        print(screen.text())
        print("-" * 70)
    # The source and the new group fell back to one tab each, so their text
    # (not a strip) owns row `strip` on the far left and far right; the middle
    # group still has its strip, so its own text sits a row below.
    first = row_text(screen, strip)
    if "alpha" not in first[:50]:
        failures.append("the source group lost its remaining tab (%r)" % first[:50])
    if "beta" not in first[75:]:
        failures.append("the dropped tab's own group did not land on the right (%r)"
                        % first[75:])
    gamma_x = row_text(screen, strip + 1).find("gamma")
    if not (50 <= gamma_x < 75):
        failures.append("the middle group is not the middle one (%d)" % gamma_x)

    if failures:
        print("pane tabs probe: FAIL")
        for failure in failures:
            print("  - %s" % failure)
        return 1
    print("pane tabs probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
