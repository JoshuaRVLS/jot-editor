#!/usr/bin/env python3
"""Probe: does the chrome draw one line where regions meet, and none where it
delimits nothing?

jot used to draw a full box per region, which put two lines between any two of
them (`││` beside the sidebar, the same between split panes and before the right
dock) and framed a single pane on all four screen edges.

This drives the real binary and reads the screen back, so it checks what the user
actually sees rather than what the layout arithmetic intends.

Usage: test/sidebar_edges_probe.py [path-to-jot-binary]
Set JOT_PROBE_DUMP=1 to print each captured screen.
Exit codes: 0 pass, 1 fail, 2 binary missing.
"""
from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

VERTICAL_RULES = "│┃"
HORIZONTAL_RULES = "─━"


def doubled_verticals(screen) -> list:
    """Rows where two vertical rules sit in adjacent columns.

    Transient popups are excluded by only counting columns that carry a rule on
    most rows, so a floated box crossing a separator is not mistaken for chrome.
    """
    rows = [r for r in screen.text().split("\n") if r.strip()]
    if not rows:
        return []
    counts = {}
    for row in rows:
        for col, ch in enumerate(row):
            if ch in VERTICAL_RULES:
                counts[col] = counts.get(col, 0) + 1
    threshold = max(3, len(rows) // 2)
    persistent = {c for c, n in counts.items() if n >= threshold}
    return sorted(c for c in persistent if (c + 1) in persistent)


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    if not os.path.exists(binary):
        print(f"sidebar edges probe: SKIP - no binary at {binary}")
        return 2

    work = "/tmp/jot_edges_probe"
    os.makedirs(work, exist_ok=True)
    path = os.path.join(work, "probe.cpp")
    with open(path, "w") as fh:
        fh.write("#include <cstdio>\n\nint main() {\n  int x = 1;\n  return x;\n}\n")

    # Ctrl+B toggles the sidebar, Ctrl+Shift+B the right dock, Alt+Shift+L splits.
    cases = [
        ("single pane", b"", 0),
        ("sidebar", b"\x1b[98;5u", 1),
        ("split", b"\x1b[108;4u", 1),
        ("right dock", b"\x1b[98;6u", 1),
    ]
    failures = 0
    for name, keys, expect_rules in cases:
        screen = run_in_pty(binary, [path], keys, settle=2.5, after=1.5,
                            cols=96, rows=24, cfg="/tmp/jot_edges_probe_cfg")
        if os.environ.get("JOT_PROBE_DUMP"):
            print(f"--- {name}")
            print(screen.text())
            print("-" * 70)
        doubled = doubled_verticals(screen)
        ok = not doubled
        if not ok:
            failures += 1
        print(f"sidebar edges probe: {name:<12} -> {'ok' if ok else 'FAIL'}"
              f" (adjacent vertical rules at columns {doubled})" if doubled else
              f"sidebar edges probe: {name:<12} -> ok")

    # A lone pane, and nothing beside it: no frame on the outer edges either.
    from pty_screen import Screen  # noqa: F401  (documented in the header)
    screen = run_in_pty(binary, [path], b"", settle=2.5, after=1.5,
                        cols=96, rows=24, cfg="/tmp/jot_edges_probe_cfg")
    lines = [l.rstrip() for l in screen.text().split("\n") if l.strip()]
    top = lines[0] if lines else ""
    # The tab row is the first row; a frame would start it with a corner glyph.
    framed = top.lstrip().startswith(("┌", "╭"))
    print(f"sidebar edges probe: single pane frame -> {'FAIL' if framed else 'ok'}")
    if framed:
        failures += 1

    # The bar is the pane area's last row, and it belongs to the panels above it:
    # each draws its own bottom edge there, so the row carries the panel's own
    # background on both sides of the junction and the status line keeps its two
    # rows to itself. In the status background it read as the status line's own
    # top edge -- a status line that looked three rows tall -- and hid the fact
    # that this row was the panel's last one.
    for name, keys in [("single pane", b""), ("sidebar", b"\x1b[98;5u"), ("dock", b"\x1b[98;6u")]:
        screen = run_in_pty(binary, [path], keys, settle=2.5, after=1.5,
                            cols=80, rows=20, cfg="/tmp/jot_edges_probe_cfg")
        # The bar is the pane area's last row: status_height(2) + 1 above the end.
        bar_row = 20 - 3
        status_row = 20 - 1
        panel_row = 20 - 5
        bar_bg = screen.bg[bar_row][10]
        panel_bg = screen.bg[panel_row][10]
        status_bg = screen.bg[status_row][10]
        ok = bar_bg == panel_bg and bar_bg != status_bg
        if not ok:
            failures += 1
        print(f"sidebar edges probe: {name:<12} bar background -> {'ok' if ok else 'FAIL'}"
              f" (bar={bar_bg} panel={panel_bg} status={status_bg})")

    if failures:
        print("sidebar edges probe: FAIL")
        return 1
    print("sidebar edges probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
