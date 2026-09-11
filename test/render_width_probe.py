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
    """Rightmost column index carrying ink, or -1 for a blank screen.

    tmux-style trailing-blank trimming is not available here, so the screen is
    read as a grid: the parser keeps every cell, blanks included, and this walks
    the rows looking for the last non-space cell.
    """
    widest = -1
    for row in range(screen.rows):
        line = screen.text().split("\n")[row]
        for col in range(len(line) - 1, widest, -1):
            if line[col] != " ":
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

    # The bottom bar is the row the user actually notices: it has to run to the
    # last column so it meets the right edge of the frame.
    cols, rows = 80, 24
    cfg = "/tmp/jot_width_probe_cfg_bar"
    screen = run_case(binary, work, cols, rows, cfg)
    lines = screen.text().split("\n")
    bar_row = None
    for row in range(rows - 1, -1, -1):
        if sum(1 for ch in lines[row] if ch in HORIZONTAL_RULES) > cols // 2:
            bar_row = row
            break
    if bar_row is None:
        print("render width probe: no bottom bar found FAIL")
        failures += 1
    else:
        # The bar is a rule with a junction where a divider meets it, so the last
        # cell is the rule itself.
        last = max(c for c, ch in enumerate(lines[bar_row]) if ch != " ")
        ok = last == cols - 1
        if not ok:
            failures += 1
        print(f"render width probe: bottom bar ends at column {last + 1} of {cols} "
              f"{'ok' if ok else 'FAIL'}")

    # Teeth: the setting must still reproduce the old one-column gap, so a
    # regression that hardwires full width would be caught too.
    cfg = "/tmp/jot_width_probe_cfg_margin"
    os.makedirs(os.path.join(cfg, "configs"), exist_ok=True)
    with open(os.path.join(cfg, "configs", "settings.conf"), "w") as fh:
        fh.write("render_margin=1\n")
    screen = run_case(binary, work, 80, 24, cfg)
    last = widest_painted_column(screen)
    ok = last == 79 - 1  # 79 columns wide, so the last painted index is 77
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
