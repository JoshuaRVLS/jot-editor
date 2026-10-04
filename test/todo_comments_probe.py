#!/usr/bin/env python3
"""Probe: TODO/FIXME comments wear the todo-comments.nvim chip and colour.

The bundled feature (runtime/lua/features/todo_comments.lua, a port of
folke/todo-comments.nvim) paints a background chip behind the keyword, hides
the colon by painting it in the theme's own background ink, and re-inks the
text after it, in the family colour the live theme names (error ->
DiagnosticError, info -> DiagnosticInfo). The browser drives the real binary
over a Lua file and reads the truecolour cells back out of the pty stream, so
it is the renderer's paint being checked, not a config value:

  * `-- TODO: ...` carries the DiagnosticInfo chip on exactly "TODO" - the
    `--` and the space before the word stay clean - the colon is invisible (its
    cell wears the dark normal background ink), and "fix this later" takes the
    family fg;
  * `-- FIXME: ...` carries the DiagnosticError chip;
  * `-- NOTODO: ...` and `-- TODO no colon` stay clean (the word boundary and
    the colon are both required).

The second scene drives `:Todo` through the command palette and checks the
workspace picker lists the same two comments with file and line, each unselected
row inked in its family colour, the selected row on the theme's selection pair -
and that the list stays clean: no accent bar on the selection, no "Plugin"
detail repeated on every row and no footer repeating the selected row. That is
also the path that exercises the picker's label/value split. The list is probed
on both render paths: the bundled Lua UI kit (the default) and the native
fallback, reached by unregistering the Lua quick-pick handler from the probe's
own init.lua. The third backspaces the hidden colon: the character is still
real, so deleting it takes the chip with it.

Usage: test/todo_comments_probe.py [path-to-jot-binary] [--dump]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary).
"""
from __future__ import annotations

import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

# The jot-dark groups the feature resolves: DiagnosticInfo for TODO,
# DiagnosticError for FIXME. A truecolour cell is tagged as 1000 + 0xRRGGBB.
INFO = 1000 + 0x61AFEF
ERROR = 1000 + 0xE06C75
# jot-dark's PmenuSel pair: the selected picker row keeps the theme's own
# selection ink, because the family colours are too close to it to read.
SELECTION_FG = 1000 + 0xD7DAE0
SELECTION_BG = 1000 + 0x3E4452
# The theme's normal background. Both bands are light enough that the contrast
# pick (upstream's maximize_contrast) lands on this dark ink, not on #abb2bf.
# Tagged like every truecolour cell (1000 + 0xRRGGBB).
INK = 1000 + 0x282C34

SOURCE = """\
local a = 1
-- TODO: fix this later
local b = 2
-- FIXME: broken
local c = 3
-- NOTODO: not a keyword
-- TODO no colon
local d = 4
"""

CTRL_P = b"\x10"
ENTER = b"\r"


def find_row(screen, needle: str):
    """(row index, row text) of the first screen row holding the needle."""
    for y in range(screen.rows):
        text = "".join(screen.cells[y])
        if needle in text:
            return y, text
    return None, ""


def band_at(screen, row: int, x: int) -> int:
    return screen.bg[row][x]


def probe_picker(screen, label: str):
    """The :Todo list as both render paths (Lua kit and native fallback) draw
    it: rows in their family inks, the selected row on the theme's selection
    pair, and no accent bar / repeated detail / duplicated footer."""
    failures = []
    text = screen.text()
    if "Todo Comments" not in text:
        failures.append(":Todo did not open the workspace picker (%s)" % label)
    for needle in ("todo.lua:2", "-- TODO: fix this later", "todo.lua:4", "-- FIXME: broken"):
        if needle not in text:
            failures.append(":Todo list is missing %r (%s)" % (needle, label))
    if "▎" in text:
        failures.append("the picker still draws the selected-row accent bar (%s)" % label)
    if "Plugin" in text:
        failures.append("the picker still repeats a Plugin detail on every row (%s)" % label)
    for needle in ("todo.lua:2", "-- FIXME: broken"):
        if text.count(needle) != 1:
            failures.append(":Todo prints %r %d times (a duplicated footer?) (%s)"
                            % (needle, text.count(needle), label))
    # Each unselected row wears its family ink end to end (path and raw line).
    row, row_text = find_row(screen, "todo.lua:4")
    if row is None:
        failures.append("the FIXME picker row never reached the screen (%s)" % label)
    else:
        x = row_text.find("todo.lua:4")
        if screen.fg[row][x] != ERROR:
            failures.append("the FIXME picker row is %s, want the DiagnosticError ink (%s)"
                            % (screen.fg[row][x], label))
        fixme_x = row_text.find("-- FIXME")
        if screen.fg[row][fixme_x] != ERROR:
            failures.append("the FIXME picker row's text is %s, want the family ink (%s)"
                            % (screen.fg[row][fixme_x], label))
    # The selected row stays on the theme's selection pair: the family inks are
    # too close to the selection background to read on it.
    row, row_text = find_row(screen, "todo.lua:2")
    if row is None:
        failures.append("the TODO picker row never reached the screen (%s)" % label)
    else:
        x = row_text.find("todo.lua:2")
        if screen.fg[row][x] != SELECTION_FG or screen.bg[row][x] != SELECTION_BG:
            failures.append("the selected TODO row is %s on %s, want the selection pair (%s)"
                            % (screen.fg[row][x], screen.bg[row][x], label))
    return failures


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    dump = "--dump" in sys.argv
    if not os.path.exists(binary):
        print("todo comments probe: SKIP - no binary at %s" % binary)
        return 2

    work = "/tmp/jot_todo_comments_probe"
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    path = os.path.join(work, "todo.lua")
    with open(path, "w") as fh:
        fh.write(SOURCE)
    cfg = os.path.join(work, "cfg")
    # A probe-owned config home whose init.lua drops the Lua quick-pick handler,
    # so the native fallback renderer is the one on screen.
    native_cfg = os.path.join(work, "cfg_native")
    os.makedirs(native_cfg, exist_ok=True)
    with open(os.path.join(native_cfg, "init.lua"), "w") as fh:
        fh.write('jot.ui.handler("quick_pick", nil)\n')

    failures = []

    # Scene 1: the paint. Wait until the BufOpen retry has had its chance by
    # draining a little extra; the band is read off the final grid.
    screen = run_in_pty(binary, [path], b"", settle=2.5, after=1.5, cols=100, rows=30,
                        cfg=cfg, cwd=work, phases=[(0.5, b"")])
    if dump:
        print(screen.text())
        print("-" * 70)

    todo_row, todo_text = find_row(screen, "-- TODO: fix this later")
    fixme_row, fixme_text = find_row(screen, "-- FIXME: broken")
    notodo_row, _ = find_row(screen, "-- NOTODO: not a keyword")
    nocolon_row, _ = find_row(screen, "-- TODO no colon")

    if todo_row is None:
        failures.append("the TODO comment row never reached the screen")
    else:
        x = todo_text.find("TODO")
        # The chip is exactly the keyword: four cells, nothing before them.
        for col in range(x, x + 4):
            if band_at(screen, todo_row, col) != INFO:
                failures.append(
                    "TODO chip: cell %d is %s, want the DiagnosticInfo fill"
                    % (col, band_at(screen, todo_row, col)))
                break
        # Nothing left of the word may be filled: not the `--`, not the space
        # between the marker and the keyword.
        for col in range(x - 3, x):
            if band_at(screen, todo_row, col) == INFO:
                failures.append("TODO chip: cell %d left of the keyword is filled" % col)
                break
        if screen.fg[todo_row][x] != INK:
            failures.append("TODO chip ink is %s, want the dark normal background"
                            % screen.fg[todo_row][x])
        colon = x + 4
        if screen.fg[todo_row][colon] != INK or band_at(screen, todo_row, colon) == INFO:
            failures.append("the TODO colon is not hidden (fg %s, bg %s)"
                            % (screen.fg[todo_row][colon], band_at(screen, todo_row, colon)))
        for col in range(x + 6, x + 9):  # "fix"
            if screen.fg[todo_row][col] != INFO:
                failures.append("text after TODO: cell %d lost the keyword colour" % col)
                break
        if any(band_at(screen, todo_row, col) == INFO for col in range(x + 4, len(todo_text))):
            failures.append("the colour after TODO: came as a chip, not as text")
    if fixme_row is None:
        failures.append("the FIXME comment row never reached the screen")
    else:
        x = fixme_text.find("FIXME")
        for col in range(x, x + 5):
            if band_at(screen, fixme_row, col) != ERROR:
                failures.append(
                    "FIXME chip: cell %d is %s, want the DiagnosticError fill"
                    % (col, band_at(screen, fixme_row, col)))
                break
        for col in range(x - 3, x):
            if band_at(screen, fixme_row, col) == ERROR:
                failures.append("FIXME chip: cell %d left of the keyword is filled" % col)
                break
        if screen.fg[fixme_row][x] != INK:
            failures.append("FIXME chip ink is %s, want the dark normal background"
                            % screen.fg[fixme_row][x])
        colon = x + 5
        if screen.fg[fixme_row][colon] != INK or band_at(screen, fixme_row, colon) == ERROR:
            failures.append("the FIXME colon is not hidden (fg %s, bg %s)"
                            % (screen.fg[fixme_row][colon], band_at(screen, fixme_row, colon)))
        for col in range(x + 7, x + 13):  # "broken"
            if screen.fg[fixme_row][col] != ERROR:
                failures.append("text after FIXME: cell %d lost the keyword colour" % col)
                break

    for row, label in ((notodo_row, "NOTODO:"), (nocolon_row, "TODO without a colon")):
        if row is None:
            failures.append("%s row never reached the screen" % label)
            continue
        for col in range(screen.cols):
            if screen.bg[row][col] in (INFO, ERROR):
                failures.append("%s got a band" % label)
                break
            if screen.fg[row][col] in (INFO, ERROR):
                failures.append("%s got the keyword colour" % label)
                break

    # Scene 2: :Todo lists the workspace's comments. The palette's Enter can
    # dispatch late in the harness, so wait for the picker title instead of
    # asserting a fixed delay.
    screen = run_in_pty(binary, [path], b"", settle=2.5, after=1.0, cols=100, rows=30,
                        cfg=cfg, cwd=work,
                        phases=[(0.6, CTRL_P), (0.6, b"Todo"), (0.6, ENTER),
                                (0.0, lambda s: "Todo Comments" in s.text()),
                                (0.8, b"")])
    if dump:
        print(screen.text())
        print("-" * 70)
    failures += probe_picker(screen, "lua kit")

    # Scene 2b: the same list on the native fallback renderer, reached by
    # unregistering the Lua quick-pick handler from the probe's own init.lua.
    screen = run_in_pty(binary, [path], b"", settle=2.5, after=1.0, cols=100, rows=30,
                        cfg=native_cfg, cwd=work,
                        phases=[(0.6, CTRL_P), (0.6, b"Todo"), (0.6, ENTER),
                                (0.0, lambda s: "Todo Comments" in s.text()),
                                (0.8, b"")])
    if dump:
        print(screen.text())
        print("-" * 70)
    failures += probe_picker(screen, "native renderer")

    # Scene 3: the colon is hidden, not removed. Put the caret after it (line 2
    # column 9, `-- TODO|:`) and backspace: the real character goes and the chip
    # goes with it, so the hidden colon is still one backspace away.
    DOWN = b"\x1b[B"
    RIGHT = b"\x1b[C"
    BACKSPACE = b"\x7f"
    screen = run_in_pty(binary, [path], b"", settle=2.5, after=0.5, cols=100, rows=30,
                        cfg=cfg, cwd=work,
                        phases=[(0.6, DOWN + RIGHT * 8), (0.6, BACKSPACE),
                                (0.0, lambda s: "TODO fix this later" in s.text()),
                                (0.8, b"")])
    if dump:
        print(screen.text())
        print("-" * 70)
    row, text = find_row(screen, "TODO fix this later")
    if row is None:
        failures.append("backspace on the hidden colon did not delete the colon")
    else:
        # From the comment's own first column: the caret sits on this line, so
        # the gutter's number wears the theme's accent blue - the same value
        # DiagnosticInfo takes - and it is not evidence either way.
        for col in range(text.index("--"), screen.cols):
            if screen.bg[row][col] == INFO or screen.fg[row][col] == INFO:
                failures.append("the chip survived the colon's deletion")
                break

    if failures:
        print("todo comments probe: FAIL")
        for failure in failures:
            print("  - %s" % failure)
        return 1
    print("todo comments probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
