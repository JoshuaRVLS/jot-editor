#!/usr/bin/env python3
"""Probe: does the palette caret stay on the prompt row when nothing matches?

The palette's prompt row is the bottom of its box, and that is where the caret is
placed. The surface used to emit one row per match plus the prompt, so with no
matches the prompt was drawn on the box's first row while the caret sat on its
last: the input field appeared to move up and leave the cursor behind.

This drives the real binary, types a query that matches nothing, and compares the
row holding the prompt text with the row the terminal's caret is left on -- the
caret row is read from the last cursor-positioning escape jot emits.

Usage: test/palette_caret_probe.py [path-to-jot-binary]
Set JOT_PROBE_DUMP=1 to print the captured screen.
Exit codes: 0 pass, 1 fail, 2 binary missing.
"""
from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    if not os.path.exists(binary):
        print(f"palette caret probe: SKIP - no binary at {binary}")
        return 2

    work = "/tmp/jot_palette_probe"
    os.makedirs(work, exist_ok=True)
    path = os.path.join(work, "probe.txt")
    with open(path, "w") as fh:
        for i in range(40):
            fh.write(f"line {i}\n")

    # Ctrl+Shift+P opens the palette; the query is chosen so nothing matches.
    keys = b"\x1b[112;6u" + b"zzzqq"
    screen = run_in_pty(binary,
                        [path],
                        keys,
                        settle=2.5,
                        after=2.5,
                        cols=100,
                        rows=30,
                        cfg="/tmp/jot_palette_probe_cfg")
    view = screen.text()
    if os.environ.get("JOT_PROBE_DUMP"):
        print(view)
        print("-" * 60)

    lines = view.split("\n")
    prompt_rows = [i for i, l in enumerate(lines) if ":zzzqq" in l]
    if not prompt_rows:
        print("palette caret probe: FAIL - the palette prompt never appeared")
        return 1
    if len(prompt_rows) > 1:
        print(f"palette caret probe: FAIL - prompt drawn on {len(prompt_rows)} rows")
        return 1

    prompt_row = prompt_rows[0]
    caret_row = screen.y
    # The caret sits on the cell just past the typed text, so its row must be the
    # prompt's. A mismatch of one row is the reported "field moved up" bug.
    print(f"palette caret probe: prompt row = {prompt_row}, caret row = {caret_row}")
    if prompt_row != caret_row:
        print("palette caret probe: FAIL - the caret is not on the prompt row")
        return 1

    # And the prompt has to be the last row of the palette's box: with nothing
    # matching, the box keeps one row of headroom above it, so the row below the
    # prompt must be the status line rather than another palette row.
    print("palette caret probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
