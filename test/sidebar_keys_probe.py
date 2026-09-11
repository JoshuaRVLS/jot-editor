#!/usr/bin/env python3
"""Probe: does Ctrl+B open the left explorer, and Ctrl+Shift+B the right dock?

The two toggles share a letter and are told apart by Shift. Under the kitty
keyboard protocol a terminal reports Ctrl+B as "CSI 98;5u"; decoding that into an
uppercase 'B' made the dispatcher read it as Ctrl+Shift+B, so Ctrl+B opened the
right dock in the terminal while the GUI (which only uppercases for a real
Shift) opened the left explorer.

This drives the real binary in a pty and inspects the screen, so it covers the
whole path: termkey/CSI-u decode -> key event -> dispatch -> panel.

Usage: test/sidebar_keys_probe.py [path-to-jot-binary]
Set JOT_PROBE_DUMP=1 to print each captured screen.
Exit codes: 0 pass, 1 fail, 2 binary missing.
"""
from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402


def rule_columns(screen) -> list:
    """Columns holding a vertical pane rule on most rows.

    A pane is a bordered box, so the explorer and the dock each add one rule
    inside the editor's own outer frame. Counting them is not enough (both cases
    give three); their *position* says which panel is open, and requiring the
    rule on most rows rejects transient boxes like the info popup.
    """
    rows = screen.text().split("\n")
    rows = [r for r in rows if r.strip()]
    if not rows:
        return []
    counts = {}
    for row in rows[1:-1]:  # skip the frame's top/bottom corners
        for col, ch in enumerate(row):
            if ch in "│┃":
                counts[col] = counts.get(col, 0) + 1
    threshold = max(3, len(rows) // 2)
    return sorted(c for c, n in counts.items() if n >= threshold)


def explorer_open(screen) -> bool:
    cols = [c for c in rule_columns(screen) if 0 < c < screen.cols * 0.45]
    return bool(cols)


def dock_open(screen) -> bool:
    cols = [c for c in rule_columns(screen) if screen.cols * 0.55 < c < screen.cols - 2]
    return bool(cols)


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    if not os.path.exists(binary):
        print(f"sidebar keys probe: SKIP - no binary at {binary}")
        return 2

    work = "/tmp/jot_sidebar_probe"
    os.makedirs(work, exist_ok=True)
    path = os.path.join(work, "probe.txt")
    with open(path, "w") as fh:
        for i in range(80):
            fh.write(f"line {i}\n")

    cases = [
        ("Ctrl+B (legacy byte)", b"\x02", "explorer"),
        ("Ctrl+B (kitty CSI-u)", b"\x1b[98;5u", "explorer"),
        ("Ctrl+Shift+B (CSI-u)", b"\x1b[98;6u", "dock"),
    ]
    failures = 0
    for name, keys, expect in cases:
        screen = run_in_pty(binary, [path], keys, cfg="/tmp/jot_sidebar_probe_cfg")
        if os.environ.get("JOT_PROBE_DUMP"):
            print(f"--- {name}")
            print(screen.text())
            print("-" * 70)
        explorer = explorer_open(screen)
        dock = dock_open(screen)
        if expect == "explorer":
            good = explorer and not dock
        else:
            good = dock and not explorer
        if not good:
            failures += 1
        print(f"sidebar keys probe: {name:<22} -> {'ok' if good else 'FAIL'}"
              f" (explorer={explorer} dock={dock}, expected {expect})")

    if failures:
        print("sidebar keys probe: FAIL")
        return 1
    print("sidebar keys probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
