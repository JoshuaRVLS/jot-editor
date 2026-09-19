#!/usr/bin/env python3
"""Probe: does the frame use the last terminal column?

The renderer used to leave the rightmost column of every row unpainted as a
"pending wrap" safety margin. That column is also where a write could scroll the
screen instead of storing a character, so the margin was worth having -- but the
hazard is already handled by disabling autowrap and addressing each row with an
absolute cursor move, and the margin showed up as a permanent blank strip down
the right edge with the bottom bar stopping one cell short of the corner it
should meet.

This drives the real binary in a pty and reads the screen back, so it checks the
columns that were actually written rather than what the layout arithmetic
intends. The margin is also exercised through the `render_margin` setting, which
has to keep producing the old one-column gap.

Usage: test/render_width_probe.py [path-to-jot-binary]
Exit codes: 0 pass, 1 fail, 2 binary missing.
"""
from __future__ import annotations

import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

HORIZONTAL_RULES = "─━"


def widest_painted_column(screen) -> int:
    """Rightmost column index the frame painted, or -1 for a blank screen.

    tmux-style trailing-blank trimming is not available here, so the screen is
    read as a grid. A cell counts as painted when it carries ink *or* a
    background: the region fills -- the status line over its whole row, the pane
    body behind blank padding -- are how the frame reaches the last column, and
    the row that used to guarantee ink there (a full-width rule above the status
    line) is gone by design, its row now code.
    """
    widest = -1
    for row in range(screen.rows):
        # The reconstructed line is trailing-trimmed, so the walk has to span
        # the grid rather than the string.
        line = screen.text().split("\n")[row]
        for col in range(screen.cols - 1, widest, -1):
            ink = col < len(line) and line[col] != " "
            if ink or screen.bg[row][col] != -1:
                widest = max(widest, col)
                break
    return widest


def run_case(binary, work, cols, rows, cfg, keys=b""):
    path = os.path.join(work, "probe.cpp")
    return run_in_pty(binary, [path], keys, settle=2.5, after=1.3,
                      cols=cols, rows=rows, cfg=cfg)


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    if not os.path.exists(binary):
        print(f"render width probe: SKIP - no binary at {binary}")
        return 2

    work = "/tmp/jot_width_probe"
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work, exist_ok=True)
    with open(os.path.join(work, "probe.cpp"), "w") as fh:
        fh.write("#include <cstdio>\n\nint main() {\n  int x = 1;\n  return x;\n}\n")

    failures = 0
    for cols, rows in [(80, 24), (100, 30), (200, 40)]:
        cfg = f"/tmp/jot_width_probe_cfg_{cols}_{rows}"
        screen = run_case(binary, work, cols, rows, cfg)
        last = widest_painted_column(screen)
        ok = last == cols - 1
        if not ok:
            failures += 1
        print(f"render width probe: {cols}x{rows} -> rightmost painted column {last + 1} "
              f"of {cols} {'ok' if ok else 'FAIL'}")

    # The bottom band is the row the user actually notices: the status line is
    # the last row and has to reach the last column so it meets the right edge of
    # the frame. It is read by background, not ink: the line ends in padding, and
    # the row above it is a code row now rather than the full-width rule this
    # check used to find.
    cols, rows = 80, 24
    cfg = "/tmp/jot_width_probe_cfg_bar"
    screen = run_case(binary, work, cols, rows, cfg)
    status_row = rows - 1
    last = max(c for c in range(cols) if screen.bg[status_row][c] != -1)
    ok = last == cols - 1
    if not ok:
        failures += 1
    print(f"render width probe: status line ends at column {last + 1} of {cols} "
          f"{'ok' if ok else 'FAIL'}")
    # The row above it is code, not the rule that used to be there.
    above = "".join(screen.text().split("\n")[status_row - 1])
    ok = not (sum(1 for ch in above if ch in HORIZONTAL_RULES) > cols // 2)
    if not ok:
        failures += 1
    print(f"render width probe: no rule above the status line "
          f"{'ok' if ok else 'FAIL'}")

    # Teeth: the setting must still reproduce the old one-column gap, so a
    # regression that hardwires full width would be caught too.
    cfg = "/tmp/jot_width_probe_cfg_margin"
    os.makedirs(os.path.join(cfg, "configs"), exist_ok=True)
    with open(os.path.join(cfg, "configs", "settings.conf"), "w") as fh:
        fh.write("render_margin=1\n")
    screen = run_case(binary, work, 80, 24, cfg)
    last = widest_painted_column(screen)
    ok = last == 79 - 1  # 79 columns wide, so the last painted index is 78
    if not ok:
        failures += 1
    print(f"render width probe: render_margin=1 -> rightmost painted column {last + 1} "
          f"of 80 {'ok' if ok else 'FAIL'}")

    if failures:
        print("render width probe: FAIL")
        return 1
    print("render width probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
