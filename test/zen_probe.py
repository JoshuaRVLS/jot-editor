#!/usr/bin/env python3
"""Probe: zen mode's layout, through the real binary over a pty.

The unit cases read the editor's own flags; this reads the screen. F12 has to
travel as a terminal key, and the four things zen claims are only observable as
painted rows: the workspace strip leaves row 0, the status line (and with it the
`line:column` indicator) leaves the last row, the explorer's column is gone and
the pane's gutter starts at the left edge instead of beside it.

  * Alt+B puts the sidebar up: the pane's line 1 starts at the sidebar's width,
    row 0 is the tab strip, the status line is there with `1:1` on it.
  * F12 (and F12 again) drops all three and gives them back.
  * A start with `zen_mode = true` (what F12 writes) arrives in zen already.

Usage: test/zen_probe.py [path-to-jot] [--dump]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary).
"""
from __future__ import annotations

import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

ROOT = "/tmp/jot_zen_probe"
CFG = "/tmp/jot_zen_probe_cfg"

# A first line wider than the pane, so where it starts on screen is a column a
# hidden sidebar shows up in.
MARK = "ZENMARKSTART"
LINES = [MARK + "z" * 200, "second line", "third line"]

ALT_B = b"\x1b[98;3u"  # Alt+B: kitty CSI-u for the sidebar toggle
F12 = b"\x1b[24~"      # xterm's F12


def write_workspace() -> str:
    shutil.rmtree(ROOT, ignore_errors=True)
    os.makedirs(ROOT)
    path = os.path.join(ROOT, "zen.txt")
    with open(path, "w") as fh:
        fh.write("\n".join(LINES) + "\n")
    return path


def run(binary: str, path: str, keys: bytes, cfg: str, zen: bool):
    shutil.rmtree(cfg, ignore_errors=True)
    os.makedirs(os.path.join(cfg, "configs"))
    with open(os.path.join(cfg, "configs", "settings.conf"), "w") as fh:
        fh.write(f"zen_mode = {'true' if zen else 'false'}\n")
    return run_in_pty(binary, [path], keys, settle=2.0, after=2.0,
                      cols=100, rows=30, cfg=cfg, cwd=ROOT)


def row_with(screen, needle: str):
    """The first screen row containing `needle`, or None."""
    for row, line in enumerate(screen.text().split("\n")):
        if needle in line:
            return row, line
    return None, None


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    dump = "--dump" in sys.argv
    if not os.path.exists(binary):
        print(f"zen probe: SKIP - no binary at {binary}")
        return 2

    path = write_workspace()
    failures = []

    # 1. Chrome up: the strip, the status line's caret indicator and the
    #    explorer's column, with the marker pushed right of the gutter.
    screen = run(binary, path, ALT_B, f"{CFG}_chrome", zen=False)
    if dump:
        print(screen.text())
        print("-" * 70)
    rows = screen.text().split("\n")
    mark_row, mark_line = row_with(screen, MARK)
    mark_col = mark_line.find(MARK) if mark_line else -1
    strip_row = row_with(screen, "zen.txt")[0]
    has_caret = "1:1" in rows[screen.rows - 1]
    print(f"chrome: marker on row {mark_row} at column {mark_col}, strip on row "
          f"{strip_row}, caret indicator {'yes' if has_caret else 'no'}")
    if mark_col < 0:
        failures.append("the file's first line is not on screen")
        return 1
    if strip_row != 0:
        failures.append(f"the workspace strip is not row 0 (row {strip_row})")
    if not has_caret:
        failures.append("the status line's caret indicator is missing")
    if mark_col < 20:
        failures.append(f"the marker is at column {mark_col}: the sidebar is not up")

    # 2. F12: strip, status line and sidebar gone, the marker at the left edge.
    screen = run(binary, path, ALT_B + F12, f"{CFG}_zen", zen=False)
    if dump:
        print(screen.text())
        print("-" * 70)
    rows = screen.text().split("\n")
    zen_row, zen_line = row_with(screen, MARK)
    zen_col = zen_line.find(MARK) if zen_line else -1
    zen_strip = row_with(screen, "zen.txt")[0]
    zen_caret = "1:1" in rows[screen.rows - 1]
    print(f"zen:    marker on row {zen_row} at column {zen_col}, strip on row "
          f"{zen_strip}, caret indicator {'yes' if zen_caret else 'no'}")
    if zen_row != 0:
        failures.append(f"the pane does not start at row 0 (row {zen_row})")
    if zen_col > 8:
        failures.append(f"the marker is at column {zen_col}: the pane is not full width")
    if zen_caret:
        failures.append("the status line is still on screen under zen")

    # 3. F12 twice puts the chrome back exactly as 1. read it.
    screen = run(binary, path, ALT_B + F12 + F12, f"{CFG}_back", zen=False)
    if dump:
        print(screen.text())
        print("-" * 70)
    rows = screen.text().split("\n")
    back_row, back_line = row_with(screen, MARK)
    back_col = back_line.find(MARK) if back_line else -1
    back_caret = "1:1" in rows[screen.rows - 1]
    print(f"back:   marker on row {back_row} at column {back_col}, strip on row "
          f"{row_with(screen, 'zen.txt')[0]}, caret indicator "
          f"{'yes' if back_caret else 'no'}")
    if (back_row, back_col) != (mark_row, mark_col) or not back_caret:
        failures.append("leaving zen did not restore the layout it was entered from")

    # 4. Persistence: the key F12 writes is what a start reads.
    screen = run(binary, path, b"", f"{CFG}_persist", zen=True)
    if dump:
        print(screen.text())
        print("-" * 70)
    rows = screen.text().split("\n")
    p_row, p_line = row_with(screen, MARK)
    p_col = p_line.find(MARK) if p_line else -1
    p_caret = "1:1" in rows[screen.rows - 1]
    print(f"start:  marker on row {p_row} at column {p_col}, caret indicator "
          f"{'yes' if p_caret else 'no'} (zen_mode = true)")
    if p_row != 0 or p_col > 8:
        failures.append("a start with zen_mode = true did not arrive in zen")
    if p_caret:
        failures.append("a start with zen_mode = true still painted the status line")

    if failures:
        for failure in failures:
            print(f"zen probe: FAIL - {failure}")
        return 1
    print("zen probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
