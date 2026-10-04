#!/usr/bin/env python3
"""Probe: every pane of a split paints its own breadcrumb row.

The breadcrumb winbar is one Lua surface for the whole split: the native side
hands it every pane's row in one payload. It used to be keyed on a single row,
so each pane's emit re-configured the same float and the pane that emitted last
owned it -- a stacked split showed a breadcrumb above the lower pane and an
empty band above the upper one, which still paid for the row.

This drives the real binary, splits the pane, and reads both rows back off the
screen. The unit cases cover the layout and the Lua painter separately; only
this sees the two together.

Usage: test/winbar_split_probe.py [path-to-jot] [--dump]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary).
"""
from __future__ import annotations

import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

COLS, ROWS = 100, 30
SPLIT_DOWN = b"\x1b[74;4u"  # Alt+Shift+J
# Alt+Shift+L is a documented split key, but the bundled LeetCode feature claims
# it as a leader (Alt+Shift+L L / D / R / ...), so the side-by-side split is
# driven through the palette command the feature cannot capture.
SPLIT_RIGHT = b"\x10vsp\r"  # Ctrl+P, "vsp", Enter
CLOSE_PANE = b"\x1b[81;4u"  # Alt+Shift+Q
# The crumb row's own glyph: the separator the layout puts between crumbs.
SEPARATOR = "\u203a"

SOURCE = """namespace app {
struct Counter {
  int stored = 0;
};
int add(int a, int b) {
  return a + b;
}
}
"""


def workspace(tmp: str) -> str:
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp)
    path = os.path.join(tmp, "a.cpp")
    with open(path, "w") as fh:
        fh.write(SOURCE)
    return path


def run(binary: str, tmp: str, path: str, keys: bytes, phases=None):
    # The startup query notice floats in the top-right corner, over the second
    # pane's breadcrumb row: wait it out before reading the screen.
    wait_toast = [(0.0, lambda s: "Tree-sitter" not in s.text())]
    return run_in_pty(binary, [path], keys, settle=2.5, after=1.0,
                      cfg=tmp + "_cfg", cwd=tmp, cols=COLS, rows=ROWS,
                      phases=wait_toast + list(phases or []))


def crumb_rows(screen) -> list[int]:
    """The screen rows that carry a breadcrumb: the row shows the crumb glyph."""
    rows = []
    for y, row in enumerate(screen.text().split("\n")):
        if SEPARATOR in row and "a.cpp" in row:
            rows.append(y)
    return rows


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    dump = "--dump" in sys.argv
    if not os.path.exists(binary):
        print("winbar split probe: SKIP - no binary at %s" % binary)
        return 2

    tmp = "/tmp/jot_winbar_split_probe"
    path = workspace(tmp)
    failures = []

    # One pane, one row: the control, so a split that paints nothing is not read
    # as "the rows moved somewhere unexpected".
    single = crumb_rows(run(binary, tmp, path, b""))
    print("single pane: crumb rows %s" % single)
    if len(single) != 1:
        failures.append("a single pane painted %d breadcrumb rows, expected 1" % len(single))

    # Stacked: both panes own a row, on their own screen row.
    screen = run(binary, tmp, path, SPLIT_DOWN)
    if dump:
        print(screen.text())
        print("-" * 70)
    stacked = crumb_rows(screen)
    print("split down: crumb rows %s" % stacked)
    if len(stacked) != 2:
        failures.append("a stacked split painted %d breadcrumb rows, expected 2 "
                        "(the earlier pane reserves one and leaves it blank)" % len(stacked))

    # Side by side: both rows sit on the same screen row, one per pane.
    screen = run(binary, tmp, path, SPLIT_RIGHT)
    if dump:
        print(screen.text())
        print("-" * 70)
    side_by_side = screen.text().split("\n")
    on_one_row = [row for row in side_by_side if row.count("a.cpp") >= 2 and SEPARATOR in row]
    print("split right: rows carrying two breadcrumbs: %d" % len(on_one_row))
    if len(on_one_row) != 1:
        failures.append("a side-by-side split put %d rows with both panes' breadcrumbs "
                        "on them, expected 1" % len(on_one_row))

    # A pane that closes takes its row with it: the survivor's row is the only
    # one left, and no float is stranded where the closed pane was.
    screen = run(binary, tmp, path, SPLIT_DOWN, phases=[(0.4, CLOSE_PANE)])
    if dump:
        print(screen.text())
        print("-" * 70)
    after_close = crumb_rows(screen)
    print("after closing a pane: crumb rows %s" % after_close)
    if len(after_close) != 1:
        failures.append("closing a pane left %d breadcrumb rows, expected 1"
                        % len(after_close))

    if failures:
        print("winbar split probe: FAIL")
        for failure in failures:
            print("  - %s" % failure)
        return 1
    print("winbar split probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
