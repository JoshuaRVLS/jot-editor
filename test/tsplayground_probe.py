#!/usr/bin/env python3
"""Probe: :tsplayground docks in the right sidebar and follows the cursor.

The panel answers "why is this line coloured like that" for the line the cursor
is on, so it is a dock tab rather than a modal: it sits beside the buffer while
you move through it, and every cursor move or edit under it updates the tree,
the captures and the byte ranges it reports.

What this drives and asserts, over the real binary:

1. The palette suggests `tsplayground` while typing `tsplay`, so the command is
   discoverable without knowing its spelling.
2. `:tsplayground` opens it in the right dock: the panel's text sits in the
   right-hand columns while the buffer stays on the left, and the header names
   the cursor line.
3. Moving the cursor updates it in place: two lines down, the header names the
   new line and the old one is gone. Editing the line it is showing updates it
   too, which is what the byte ranges make visible.
4. The tab survives :e and describes the file in front, then `:tsplayground`
   again (or q with the panel focused) closes it.

Usage: test/tsplayground_probe.py [path-to-jot-binary] [--dump]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary, or the grammar is missing).
"""
from __future__ import annotations

import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

PALETTE = b"\x1b[112;6u"  # Ctrl+Shift+P
DOWN = b"\x1b[B"
ENTER = b"\r"
COLUMNS, ROWS = 110, 30
SOURCE = """int call_it(int n);

Range normalized_selection_range(const FileBuffer &buf)
{
  Range r;
  return r;
}
"""
# Another file whose line 6 holds the same text, so only the buffer - not the
# text under the cursor - changed when the panel was left open across :e.
OTHER_SOURCE = """def total_of(n):

    total = normalized(n)
    for i in range(n):
        total += i
  return r;
"""
# `  return r;` is line 6, and its captures are few enough to fit the panel.
CURSOR_LINE = 6
# The dock is 42 columns by default, so its text starts well past the middle.
PANEL_COLUMN = 44


def scene(binary, work, keys, phases=None):
    path = os.path.join(work, "playground.cpp")
    with open(path, "w") as fh:
        fh.write(SOURCE)
    screen = run_in_pty(binary, [path], keys, settle=2.5, after=1.5,
                        cfg=os.path.join(work, "cfg"), cwd=work,
                        cols=COLUMNS, rows=ROWS, phases=phases)
    return screen.text()


def row_column(text, needle):
    """Column of `needle` in the first row carrying it, or -1."""
    for row in text.splitlines():
        at = row.find(needle)
        if at >= 0:
            return at
    return -1


def any_column_at_most(text, needle, limit):
    """True when some row carries `needle` starting at or before `limit`."""
    return any(0 <= row.find(needle) <= limit for row in text.splitlines())


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    dump = "--dump" in sys.argv
    if not os.path.exists(binary):
        print("tsplayground probe: SKIP - no binary at %s" % binary)
        return 2

    work = "/tmp/jot_tsplayground_probe"
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)

    to_line = DOWN * (CURSOR_LINE - 1)
    open_keys = to_line + PALETTE + b"tsplayground\r"
    suggestions = scene(binary, work, to_line + PALETTE + b"tsplay")
    opened = scene(binary, work, open_keys)

    if "No tree yet" in opened or "Nothing to show" in opened:
        print("tsplayground probe: SKIP - the cpp grammar is not installed here")
        if dump:
            print(opened)
        return 2

    failures = []
    if "tsplayground" not in suggestions:
        failures.append("the palette does not suggest tsplayground while typing tsplay")
    checks = [
        ("the panel is docked on the right", row_column(opened, "Tree (") >= PANEL_COLUMN),
        ("the buffer keeps its columns", any_column_at_most(opened, "return r;", PANEL_COLUMN)),
        ("the header names the cursor line", "Line %d" % CURSOR_LINE in opened),
        ("the tree shows a node type", "return_statement" in opened),
        ("the highlights section is listed", "Highlights (3)" in opened),
        ("a capture is named", "@keyword.control" in opened),
        ("the capture's range is shown", "[2,8)" in opened),
        # The other four query kinds a language can ship, so the panel also
        # answers why a fold or an indent is not what the query file asked for.
        ("the fold query is reported", "Folds" in opened),
        ("the indent query is reported", "Indents" in opened),
        ("the injections query is reported", "Injections" in opened),
        ("the textobjects query is reported", "Text objects" in opened),
    ]
    for label, ok in checks:
        print("%-36s %s" % (label, "ok" if ok else "MISSING"))
        if not ok:
            failures.append(label)

    if "Tree (" in opened:
        # Down to the closing brace: the panel follows the cursor without being
        # reopened, and the line it was showing is gone from its header.
        last_line = CURSOR_LINE + 1
        moved = scene(binary, work, open_keys, phases=[(0.3, DOWN), (0.4, DOWN)])
        live = ("Line %d (" % last_line in moved
                and "Line %d (" % CURSOR_LINE not in moved)
        print("%-36s %s" % ("the panel follows the cursor", "ok" if live else "MISSING"))
        if not live:
            failures.append("the panel did not follow the cursor")
            if dump:
                print(moved)

        # Two spaces typed in front of `return` move its capture from [2,8) to
        # [4,10): the ranges the panel prints are the buffer's own bytes.
        edited = scene(binary, work, open_keys, phases=[(0.6, b"  ")])
        followed = "[4,10)" in edited and "[2,8)" not in edited
        print("%-36s %s" % ("the panel follows the edit", "ok" if followed else "MISSING"))
        if not followed:
            failures.append("the panel did not follow the edit")
            if dump:
                print(edited)

        # The tab outlives the file it was opened on: another file whose line 6
        # holds the very same text must not keep the old tree on screen, so the
        # panel names the language of the buffer in front.
        with open(os.path.join(work, "other.py"), "w") as fh:
            fh.write(OTHER_SOURCE)
        switched = scene(binary, work, open_keys,
                         phases=[(0.5, PALETTE), (0.6, b"e other.py"), (0.6, ENTER),
                                 (0.8, DOWN * (CURSOR_LINE - 1))])
        follows_file = "python  line %d" % CURSOR_LINE in switched and "cpp  line" not in switched
        print("%-36s %s" % ("the panel follows the buffer", "ok" if follows_file else "MISSING"))
        if not follows_file:
            failures.append("the panel kept the previous buffer's tree")
            if dump:
                print(switched)

        closed = scene(binary, work, open_keys, phases=[(0.4, PALETTE + b"tsplayground\r")])
        gone = "Tree (" not in closed
        print("%-36s %s" % ("the command closes the tab again", "ok" if gone else "MISSING"))
        if not gone:
            failures.append("the tab stayed open")

    if failures:
        print("tsplayground probe: FAIL")
        for failure in failures:
            print("  - %s" % failure)
        if dump:
            print(opened)
        return 1
    print("tsplayground probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
