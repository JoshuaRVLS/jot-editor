#!/usr/bin/env python3
"""Probe: an edit must not shift the syntax colours of the lines it moves down.

Pressing Enter splits a line and pushes the rest of the file down. The tree that
paints the colours is kept current by diffing the buffer against the text it was
parsed from, and Enter asks for that tree in the window between the snapshot
(save_state, before the mutation) and the split itself (the indent query, which
reads the line as it still stands). A rebuild in that window used to drop the
snapshot and call the tree in sync, so the split went unseen and every highlight
past it sat one byte off per inserted newline: "Range" painted as "Rang" plus a
stray "e", "const" as "cons" plus "t", compounding with each Enter.

Text alone cannot see this; the characters are all correct and only the colours
land one cell off, so this probe reads the foreground colour of every cell of a
line the edit pushed down and compares the runs against the same line in an
unedited run. One Enter, two Enters in a single key blob, and Enter followed by
typing are all covered: a command that mutates, re-reads the tree and mutates
again is what left the second mutation unseen.

Usage: test/treesitter_edit_colors_probe.py [path-to-jot-binary] [--dump]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary).
"""
from __future__ import annotations

import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

# A line with several differently coloured tokens on it: a type, a function
# name, a keyword, another type and a variable. The edit happens above it and
# moves it down, which is where the offset error landed.
NEEDLE = "normalized_selection_range(const FileBuffer"

SOURCE = """int call_it(int n);

Range normalized_selection_range(const FileBuffer &buf)
{
  Range r;
  return r;
}
"""


def code_runs(screen, needle):
    """[(text, fg), ...] for the code part of the row carrying `needle`.

    The needle spans several colour runs by design (that is what is being
    checked), so the run holding its first character is where the sample
    starts. It ends at the closing paren of the signature: a diagnostic the
    language server appends to the right of the line comes and goes between
    runs and says nothing about where the highlight spans land.
    """
    for row, line in enumerate(screen.text().splitlines()):
        if needle not in line:
            continue
        cells = "".join(screen.cells[row]).rstrip()
        fgs = screen.fg[row]
        at = cells.find(needle)
        runs = []
        start = 0
        for col in range(1, len(cells)):
            if fgs[col] != fgs[start]:
                runs.append((start, cells[start:col], fgs[start]))
                start = col
        runs.append((start, cells[start:], fgs[start]))
        kept = []
        for start_col, text, fg in runs:
            if start_col + len(text) <= at:
                continue
            kept.append((text, fg))
            if start_col >= at and ")" in text:
                break
        return kept or None
    return None


def colours_after(binary, work, path, keys):
    screen = run_in_pty(binary, [path], keys, settle=2.5, after=2.0,
                        cfg=os.path.join(work, "cfg"), cwd=work, cols=100, rows=20)
    return screen, code_runs(screen, NEEDLE)


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    dump = "--dump" in sys.argv
    if not os.path.exists(binary):
        print("treesitter edit colors probe: SKIP - no binary at %s" % binary)
        return 2

    work = "/tmp/jot_treesitter_edit_colors_probe"
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    path = os.path.join(work, "edit_colors.cpp")
    with open(path, "w") as fh:
        fh.write(SOURCE)

    baseline_screen, baseline = colours_after(binary, work, path, b"")
    if not baseline or len(baseline) < 3:
        print("treesitter edit colors probe: SKIP - the line was never highlighted")
        if dump:
            print(baseline_screen.text())
        return 2

    # Only the fg values are compared, not the colours themselves: the baseline
    # is this binary and this theme, so a theme change moves both sides.
    scenarios = [
        (b"\r", "one Enter above the line"),
        (b"\r\r", "two Enters in one key blob"),
        (b"\rx", "Enter then a typed character"),
        (b"x", "a typed character"),
    ]
    failures = []
    for keys, label in scenarios:
        screen, runs = colours_after(binary, work, path, keys)
        status = "same" if runs == baseline else "SHIFTED"
        print("%-32s %s" % (label, status))
        if runs != baseline:
            failures.append((label, runs, screen))

    if failures:
        print("treesitter edit colors probe: FAIL")
        for label, runs, screen in failures:
            print("  - %s: the colours moved" % label)
            print("    before: %s" % " ".join("%r:%s" % r for r in baseline))
            print("    after : %s" % (" ".join("%r:%s" % r for r in runs) if runs else "<line missing>"))
            if dump:
                print(screen.text())
        return 1
    print("treesitter edit colors probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
