#!/usr/bin/env python3
"""Probe: the workspace strip shares its row with the side columns.

The strip used to be a full-width row: it painted over the explorer's header
(the explorer started a row lower to make room) and its TabLine background ran
to the screen edge, which read as a gap beside the last tab. Now the strip spans
only the pane area between the side columns -- the explorer and the right dock
are full height and own their top row -- and it paints nothing but the tabs
themselves.

Both halves are layout agreements that no model test can see, so this drives the
real binary and reads the cells back:

  * scene 1 - a file open with the explorer up: the explorer's EXPLORER header
    sits on row 0 with its root row under it, the strip's first tab starts right
    of the explorer's border, and past the last tab the row is the editor
    background, not the TabLine band;
  * scene 2 - the right dock opened with the strip up: the dock's title row is
    row 0 and its tab strip is row 1 (the strip does not push the dock down a
    row), while the workspace tabs stay left of the dock.

Usage: test/chrome_row0_probe.py [path-to-jot-binary] [--dump]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary).
"""
from __future__ import annotations

import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

CTRL_P = b"\x10"
ENTER = b"\r"
DOCK = b"\x1b[98;6u"  # Ctrl+Shift+B, the right dock toggle

# jot-dark's chrome pair, tagged as truecolour cells (1000 + 0xRRGGBB).
TAB_BG = 1000 + 0x282C34  # TabLineSel / the editor's own surface
BAND_BG = 1000 + 0x21252B  # TabLine / the sidebar's chrome surface


def row_text(screen, y: int) -> str:
    return "".join(screen.cells[y])


def toast_gone(screen) -> bool:
    """The startup query notice floats in the top-right corner until it times
    out, right over the dock's rows; every reading waits it out."""
    return "Tree-sitter" not in screen.text()


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    dump = "--dump" in sys.argv
    if not os.path.exists(binary):
        print("chrome row 0 probe: SKIP - no binary at %s" % binary)
        return 2

    work = "/tmp/jot_chrome_row0"  # short enough for the explorer's header, which ellipsizes
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    alpha = os.path.join(work, "alpha.txt")
    with open(alpha, "w") as fh:
        fh.write("CHROME_ROW0_ALPHA\nsecond\n")
    cfg = os.path.join(work, "cfg")

    failures = []

    def scene(keys: bytes = b""):
        """Opens the workspace with a file open (the strip up), optionally sends
        `keys` after that, and returns the settled screen."""
        return run_in_pty(binary, [work], b"", settle=2.5, after=0.5,
                          cols=100, rows=24, cfg=cfg, cwd=work,
                          phases=[(0.4, CTRL_P),
                                  (0.4, ("open " + alpha).encode()),
                                  (0.4, ENTER),
                                  (0.0, lambda s: "CHROME_ROW0_ALPHA" in s.text()),
                                  (0.0, toast_gone),
                                  (0.4, keys),
                                  (0.4, toast_gone),
                                  (0.4, b"")])

    # Scene 1: the explorer owns row 0 and the strip starts after its border.
    screen = scene()
    if dump:
        print(screen.text())
        print("-" * 70)
    r0 = row_text(screen, 0)
    r1 = row_text(screen, 1)
    sep = r0.find("│")  # the explorer's right edge
    tab_x = r0.find("▌")  # the first tab's leading edge
    if "CHROME_ROW0_ALPHA" not in r1:
        failures.append("the first code row is not row 1 (%r)" % r1.strip()[:40])
    if "alpha.txt" not in r0:
        failures.append("the workspace strip lost its tab (%r)" % r0)
    if sep < 0 or tab_x < 0 or tab_x <= sep:
        failures.append("the strip starts at %d, not right of the explorer's border %d (%r)"
                        % (tab_x, sep, r0))
    if "EXPLORER" not in r0[:max(0, sep)]:
        failures.append("the explorer's section header is not on row 0 (%r)" % r0[:50])
    # The pinned root row under the header carries the workspace's name.
    if os.path.basename(work) not in row_text(screen, 1)[:max(0, sep)]:
        failures.append("the explorer root row is not under the header (%r)"
                        % row_text(screen, 1)[:50])
    # Past the tab's close cell the row keeps the editor background: the old
    # full-width TabLine band ran to the screen edge and read as a gap.
    close_x = r0.rfind("×")
    if close_x < 0:
        failures.append("the tab has no close cell on row 0 (%r)" % r0)
    elif BAND_BG in screen.bg[0][close_x + 1:]:
        failures.append("a TabLine band runs past the last tab (%s)"
                        % [x for x, c in enumerate(screen.bg[0][close_x + 1:], close_x + 1)
                           if c == BAND_BG][:5])

    # Scene 2: the right dock owns row 0 too: its title row is 0 and its tab
    # strip is row 1, beside the workspace strip rather than under it.
    screen = scene(DOCK)
    if dump:
        print(screen.text())
        print("-" * 70)
    r0 = row_text(screen, 0)
    r1 = row_text(screen, 1)
    border = r1.rfind("│")  # the pane's right border: the dock starts after it
    if border < 0:
        failures.append("no separator between the pane and the dock on row 1 (%r)" % r1)
        border = len(r0)
    # The dock's title row has no close cell; its tab strip row does, and the
    # active tab's label carries the name of the panel it shows.
    def dock_region(y: int) -> str:
        return row_text(screen, y)[border + 1:]

    title_row = next((y for y in range(4) if "Debug" in dock_region(y)), -1)
    strip_row = next((y for y in range(4)
                      if "Debug" in dock_region(y) and "×" in dock_region(y)), -1)
    if title_row != 0:
        failures.append("the dock title is on row %d, not row 0" % title_row)
    if strip_row != 1:
        failures.append("the dock tab strip is on row %d, not row 1" % strip_row)
    if "alpha.txt" not in r0[:border]:
        failures.append("the workspace strip lost its tab when the dock opened (%r)"
                        % r0[:border])
    if r0.rfind("×") >= border:
        failures.append("a workspace tab paints into the dock's columns")

    if failures:
        print("chrome row 0 probe: FAIL")
        for failure in failures:
            print("  - %s" % failure)
        return 1
    print("chrome row 0 probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
