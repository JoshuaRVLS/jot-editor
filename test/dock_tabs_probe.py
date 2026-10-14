#!/usr/bin/env python3
"""Probe: the undo tree and the tree-sitter playground open in the dock together.

The dock hosts several panels as tabs (git, diff, symbols, debugger, plugin,
tree-sitter) and the strip at its top switches between them. Every one of those
tabs hands the Lua kit a `side_panel` view, and the kit paints it in its own
float -- every one except the tree-sitter playground, which draws itself in
C++. Nothing told the kit to put its float away when the playground became the
active tab, so a dock that had shown a Lua panel first (the undo tree here) kept
painting the old panel over the playground: the strip listed both tabs, the
playground was the active one, and the screen still showed the undo tree. The
playground was only visible if it happened to be opened first.

What this drives and asserts, over the real binary:

1. Undo tree, then `:tsplayground`: the strip carries both tabs, the playground
   is the active one, its tree is on screen, and the undo tree is gone.
2. The other order: both tabs again, the undo tree active and painted, the
   playground's own rows gone.
3. Clicking the other tab switches to it -- the panel the strip names is the one
   on screen, in both directions.

Usage: test/dock_tabs_probe.py [path-to-jot] [--dump]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary, or no cpp grammar installed).
"""
from __future__ import annotations

import os
import re
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

COLS, ROWS = 110, 30
PANEL_W = 42  # the default right_panel_width
PANEL_X = COLS - PANEL_W
ROOT = "/tmp/jot_dock_tabs_probe"
CFG = "/tmp/jot_dock_tabs_probe_cfg"

PALETTE = b"\x1b[112;6u"  # Ctrl+Shift+P
CTRL_Z = b"\x1a"
UNDOTREE = b"Undotree\r"
PLAYGROUND = b"tsplayground\r"

# The dock's title row is the screen's top row and its tab strip the one below,
# the same rows every panel renderer paints them on.
STRIP_ROW = 1
# The cursor bar the dock's lists draw, starred: the row the undo tree marks as
# the state the buffer is on. No other dock panel draws this.
UNDO_ROW = re.compile(r"\u258c\* \d+")
# Rows only the playground paints: the header naming the cursor line, the
# section counting the line's nodes, and the header of the captures list.
PLAY_HEADER = re.compile(r"Line \d+ \(")
PLAY_TREE = re.compile(r"Tree \(\d+ node")
PLAY_HIGHLIGHTS = "Highlights ("

FAILURES: list[str] = []


def check(ok: bool, message: str) -> bool:
    if not ok:
        FAILURES.append(message)
    return ok


def dump(screen, label: str) -> None:
    print("=== %s ===" % label)
    print(screen.text())
    print("-" * 78)


def rows(text: str) -> list[str]:
    return text.split("\n")


def strip(text: str) -> str:
    return rows(text)[STRIP_ROW]


def dock(text: str) -> str:
    """The dock's own columns, every row: what the right sidebar is showing."""
    return "\n".join(row[PANEL_X:] for row in rows(text))


def active(text: str, label: str) -> bool:
    """True when the strip marks `label` as the tab in front (it carries a x)."""
    return (label + " \u00d7") in strip(text)


def scene(binary: str, work: str, phases=None):
    path = os.path.join(work, "playground.cpp")
    with open(path, "w") as fh:
        fh.write("int main()\n{\n  return 0;\n}\n")
    return run_in_pty(binary, [path], b"", settle=3.0, after=1.5, cfg=CFG, cwd=work,
                      cols=COLS, rows=ROWS, phases=phases)


def open_pair(binary, work, first: bytes, second: bytes, extra=()):
    """Type an edit history, then open `first` and `second` from the palette."""
    phases = [(0.6, b"alpha" + CTRL_Z + CTRL_Z), (0.5, PALETTE), (0.5, first),
              (0.8, PALETTE), (0.5, second)]
    if extra:
        phases.append((0.8, extra))
    return scene(binary, work, phases)


def state(text: str) -> str:
    """A one-line summary of what the dock is showing, for the dump."""
    return "strip=[%s]" % strip(text)[PANEL_X:].rstrip()


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    dump_screens = "--dump" in sys.argv
    if not os.path.exists(binary):
        print("dock tabs probe: SKIP - no binary at %s" % binary)
        return 2

    shutil.rmtree(ROOT, ignore_errors=True)
    os.makedirs(ROOT)
    shutil.rmtree(CFG, ignore_errors=True)

    undo_first = open_pair(binary, ROOT, UNDOTREE, PLAYGROUND)
    if "this build has no tree-sitter runtime" in undo_first.text() \
            or "No tree yet" in undo_first.text():
        print("dock tabs probe: SKIP - the cpp grammar is not installed here")
        if dump_screens:
            dump(undo_first, "undo tree then playground")
        return 2

    print("both tabs, opened undo-tree-first: %s" % state(undo_first.text()))
    for label, ok in [
        ("the strip carries the undo tree tab", "Undotree" in strip(undo_first.text())),
        ("the strip carries the playground tab", "Tree" in strip(undo_first.text())),
        ("the playground is the tab in front", active(undo_first.text(), "Tree")),
        ("the undo tree tab is behind it", not active(undo_first.text(), "Undotree")),
        ("the playground paints its tree", bool(PLAY_TREE.search(dock(undo_first.text())))),
        ("the playground names the cursor line", bool(PLAY_HEADER.search(dock(undo_first.text())))),
        ("the playground lists captures", PLAY_HIGHLIGHTS in dock(undo_first.text())),
        ("the undo tree is off the dock", not UNDO_ROW.search(dock(undo_first.text()))),
    ]:
        if not check(ok, label):
            print("  %-38s MISSING" % label)
            if dump_screens:
                dump(undo_first, "undo tree then playground")
        else:
            print("  %-38s ok" % label)

    # The other order, which used to work: it must keep working (the playground
    # still has no Lua view, so nothing may tear the undo tree down when it is
    # the one in front).
    play_first = open_pair(binary, ROOT, PLAYGROUND, UNDOTREE)
    print("both tabs, opened playground-first: %s" % state(play_first.text()))
    for label, ok in [
        ("the strip carries both tabs (reverse order)",
         "Undotree" in strip(play_first.text()) and "Tree" in strip(play_first.text())),
        ("the undo tree is the tab in front", active(play_first.text(), "Undotree")),
        ("the undo tree paints its states", bool(UNDO_ROW.search(dock(play_first.text())))),
        ("the playground is off the dock",
         not PLAY_TREE.search(dock(play_first.text()))
         and not PLAY_HEADER.search(dock(play_first.text()))),
    ]:
        if not check(ok, label):
            print("  %-38s MISSING" % label)
            if dump_screens:
                dump(play_first, "playground then undo tree")
        else:
            print("  %-38s ok" % label)

    # Switching by clicking the strip, the way a mouse user reaches the tab
    # behind the one in front -- and the way back: with the playground in front
    # (the first scene) a click on the undo tree's label has to bring its panel
    # up again, not leave the playground under a strip that says otherwise. The
    # click lands on the first cell of the label the strip paints for that tab.
    strip_text = strip(undo_first.text())
    undo_at = strip_text.find("Undotree")
    if undo_at < 0:
        check(False, "the strip has no undo tree tab to click")
    else:
        click = "\x1b[<0;%d;%dM\x1b[<0;%d;%dm" % (undo_at + 1, STRIP_ROW + 1,
                                                  undo_at + 1, STRIP_ROW + 1)
        clicked = open_pair(binary, ROOT, UNDOTREE, PLAYGROUND, extra=click.encode())
        print("after clicking the undo tree tab: %s" % state(clicked.text()))
        for label, ok in [
            ("the click activates the undo tree", active(clicked.text(), "Undotree")),
            ("the undo tree paints after the click",
             bool(UNDO_ROW.search(dock(clicked.text())))),
            ("the playground leaves the dock again",
             not PLAY_TREE.search(dock(clicked.text()))
             and not PLAY_HEADER.search(dock(clicked.text()))),
        ]:
            if not check(ok, label):
                print("  %-38s MISSING" % label)
                if dump_screens:
                    dump(clicked, "after clicking the undo tree tab")
            else:
                print("  %-38s ok" % label)

    if FAILURES:
        print("dock tabs probe: FAIL")
        for failure in FAILURES:
            print("  - %s" % failure)
        return 1
    print("dock tabs probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
