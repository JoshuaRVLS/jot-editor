#!/usr/bin/env python3
"""Probe: the command palette paints Helix-style -- a multi-column command grid
with an explanation box for the selected row.

Ctrl+P opens the palette. The results have to fill the width column by column
(not one narrow list), and the selected command's explanation -- its sentence,
its aliases and its argument shape -- has to sit in a bordered box above the
grid. The prompt keeps the last row, with the caret on it, and moving the
selection has to move the box with it.

The suggestion rows also have to sit on their own surface (the theme's Pmenu
background), padded to the screen edge, instead of blending into the buffer
behind them; only the selected cell keeps the selection band. Both render paths
are checked: the shipped Lua kit and the native fallback.

Usage: test/command_palette_grid_probe.py [path-to-jot-binary]
Set JOT_PROBE_DUMP=1 to print the captured screen.
Exit codes: 0 pass, 1 fail, 2 binary missing.
"""
from __future__ import annotations

import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

# Ctrl+Shift+P (kitty CSI-u), the palette shortcut the other probes use.
OPEN = b"\x1b[112;6u"
RIGHT = b"\x1b[C"

# A command name as the grid paints it: a lowercase word (some carry a "!" or
# digits, e.g. "q!" or "term2").
TOKEN = re.compile(r"[a-z][a-z0-9]*")

THEME = "jot"
CFG = "/tmp/jot_palette_grid_probe_cfg"


def code(value) -> int:
    """A theme colour as the screen records it: an index, or 1000+rgb for the
    truecolor form the hex themes use."""
    if isinstance(value, int):
        return value
    if not value:
        return -1
    if value.startswith("#"):
        h = value[1:]
        if len(h) == 3:
            h = "".join(c * 2 for c in h)
        return 1000 + int(h[:6], 16)
    return int(value)


def theme_slots() -> dict:
    """The three backgrounds this probe is about, from the pinned theme: the
    suggestion rows (Pmenu -> the command surface), the selected cell
    (PmenuSel) and the buffer behind the palette (Normal)."""
    path = os.path.join(".configs", "configs", "colors", f"{THEME}.json")
    data = json.load(open(path))
    return {
        "suggest": code(data["Pmenu"]["bg"]),
        "selection": code(data["PmenuSel"]["bg"]),
        "editor": code(data["Normal"]["bg"]),
    }


def config_dir(native: bool) -> str:
    """A probe-owned config home pinned to the probe theme. `native` drops the
    Lua palette and status line handlers so render/palette.cpp paints the
    surface instead of the kit."""
    cfg = f"{CFG}_{'native' if native else 'lua'}"
    os.makedirs(os.path.join(cfg, "configs"), exist_ok=True)
    with open(os.path.join(cfg, "configs", "settings.conf"), "w") as fh:
        fh.write(f"color_scheme={THEME}\n")
    with open(os.path.join(cfg, "init.lua"), "w") as fh:
        if native:
            fh.write('jot.ui.handler("command_palette", nil)\n')
            fh.write('jot.ui.handler("status_line", nil)\n')
    return cfg


def dump(screen) -> None:
    if not os.environ.get("JOT_PROBE_DUMP"):
        return
    for i, line in enumerate(screen.text().split("\n")):
        print(f"{i:02d}|{line.rstrip()}")
    print("-" * 70)


def lines_of(screen):
    return [line.rstrip() for line in screen.text().split("\n")]


def prompt_row(lines):
    """The last row of the palette: the prompt, drawn as ":" plus the query.
    The native fallback insets the ":" one cell (statusline style) while the
    Lua kit draws it from column 0, so the marker is read after the inset."""
    rows = [i for i, line in enumerate(lines) if line.strip().startswith(":")]
    return max(rows) if rows else -1


def box_bottom(lines, prompt):
    """The explanation box's bottom border, or -1 when no box is up.

    The box stands directly on the grid's top edge, so its bottom border is the
    last bordered row above the prompt -- anything else with a frame (a toast,
    the status line) sits higher on the screen.
    """
    if prompt < 0:
        return -1
    rows = [i for i in range(prompt) if "\u2514" in lines[i]]
    return max(rows) if rows else -1


def box_body(lines, bottom):
    """The box's content rows: the framed region that bottom border closes."""
    if bottom < 0:
        return []
    tops = [i for i in range(bottom) if "\u250c" in lines[i]]
    if not tops:
        return []
    return [lines[i].strip(" \u2502") for i in range(max(tops) + 1, bottom)]


def surface_error(screen, lines) -> str:
    """A message when the suggestion band is not on its own surface, else None.

    Every cell of a result row -- the labels, the padding after the last column
    and the selected row's unselected cells -- has to carry the Pmenu
    background, so the grid reads as a pane instead of blending into the buffer.
    Only the selected cell itself keeps the selection background.
    """
    slots = theme_slots()
    if slots["suggest"] == slots["editor"]:
        return f"the theme paints Pmenu and Normal the same ({slots['suggest']})"
    prompt = prompt_row(lines)
    bottom = box_bottom(lines, prompt)
    if bottom < 0:
        return "the explanation box never appeared"
    grid_top = bottom + 1
    if grid_top + 1 >= prompt:
        return "the grid has too few rows to sample"
    # The selected cell (column 0 of the top grid row) is the one block on the
    # selection background; the rest of that row is already the pane.
    if screen.bg[grid_top][0] != slots["selection"]:
        return ("the selected cell is not on the selection background "
                f"({screen.bg[grid_top][0]} != {slots['selection']})")
    tokens = [m.start() for m in TOKEN.finditer(lines[grid_top])]
    if len(tokens) < 2:
        return "the selected row carries a single label"
    if screen.bg[grid_top][tokens[1]] != slots["suggest"]:
        return ("the rest of the selected row is not on the suggestion surface "
                f"({screen.bg[grid_top][tokens[1]]} != {slots['suggest']})")
    # A whole unselected row: every cell to the screen edge, including the
    # padding after the last column, on the suggestion surface.
    row = grid_top + 1
    for x in range(screen.cols):
        if screen.bg[row][x] != slots["suggest"]:
            return (f"the suggestion row falls off its surface at column {x} "
                    f"({screen.bg[row][x]} != {slots['suggest']})")
    return None


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    if not os.path.exists(binary):
        print(f"command palette grid probe: SKIP - no binary at {binary}")
        return 2

    work = "/tmp/jot_palette_grid_probe"
    os.makedirs(work, exist_ok=True)
    path = os.path.join(work, "probe.txt")
    with open(path, "w") as fh:
        for i in range(60):
            fh.write(f"line {i}\n")

    # Scene 1: the unfiltered palette. The explanation box sits on the grid's
    # top edge, and the grid below it lays the commands out in columns: a row
    # carries several names spread across the width, which a one-name-per-row
    # list can never do.
    screen = run_in_pty(binary, [path], OPEN, settle=2.5, after=2.5, cols=140, rows=35,
                        cfg=config_dir(False))
    dump(screen)
    lines = lines_of(screen)
    prompt = prompt_row(lines)
    if prompt < 0:
        print("command palette grid probe: FAIL - the palette prompt never appeared")
        return 1
    bottom = box_bottom(lines, prompt)
    box = box_body(lines, bottom)
    if not box:
        print("command palette grid probe: FAIL - the selected command has no explanation box")
        return 1
    if not any(line.startswith("Aliases:") for line in box):
        print("command palette grid probe: FAIL - the explanation box has no aliases line")
        return 1
    grid_top = bottom + 1
    if grid_top >= prompt:
        print("command palette grid probe: FAIL - the grid has no rows")
        return 1
    wide_rows = 0
    for row in range(grid_top, prompt):
        tokens = [m.start() for m in TOKEN.finditer(lines[row])]
        if len(tokens) >= 3 and max(tokens) - min(tokens) >= 40:
            wide_rows += 1
    if wide_rows < 5:
        print(f"command palette grid probe: FAIL - the results are not laid out in columns "
              f"({wide_rows} rows carry three spread-out names)")
        return 1
    # Filled column by column: the first row starts with the top command (q, the
    # highest-priority one) and, several columns later, reaches commands from the
    # bottom of the ranking. A single-column list could never put those on one
    # row.
    first_row = lines[grid_top]
    names = [m.group() for m in TOKEN.finditer(first_row)]
    tail = [w for w in ("splitleft", "splitright", "splitup", "splitdown", "taskrerun",
                        "gitstatus", "todo", "todoprev", "markdownpreview") if w in names]
    if "q" not in names or not tail or len(names) < 4:
        print("command palette grid probe: FAIL - the results are not filled column by column")
        return 1

    # The suggestion band: labels and padding on the theme's command surface,
    # the selected cell on the selection background. Checked on the shipped Lua
    # renderer, then again with the kit's handlers dropped so render/palette.cpp
    # is the one painting (its band fill has to agree cell for cell).
    err = surface_error(screen, lines)
    if err:
        print(f"command palette grid probe: FAIL - {err} (Lua renderer)")
        return 1
    native = run_in_pty(binary, [path], OPEN, settle=2.5, after=2.5, cols=140, rows=35,
                        cfg=config_dir(True))
    dump(native)
    err = surface_error(native, lines_of(native))
    if err:
        print(f"command palette grid probe: FAIL - {err} (native renderer)")
        return 1

    # Scene 2: a query the curated help knows. The box has to show the sentence
    # and the aliases, and the prompt has to keep the last row with the caret on
    # it.
    screen = run_in_pty(binary, [path], OPEN + b"wq", settle=2.5, after=2.5, cols=140, rows=35,
                        cfg=config_dir(False))
    dump(screen)
    lines = lines_of(screen)
    if not any("Write the current buffer and quit." in line for line in lines):
        print("command palette grid probe: FAIL - the explanation box never showed the sentence")
        return 1
    if not any("Aliases: x, xit" in line for line in lines):
        print("command palette grid probe: FAIL - the explanation box never showed the aliases")
        return 1
    prompt = prompt_row(lines)
    if prompt < 0 or ":wq" not in lines[prompt]:
        print("command palette grid probe: FAIL - the prompt never showed the typed query")
        return 1
    if screen.y != prompt:
        print(f"command palette grid probe: FAIL - caret row {screen.y} is not the prompt row {prompt}")
        return 1

    # Scene 3: the box follows the selection. With "q" the box shows that
    # command's sentence; moving right past its aliases (q -> quit -> q! ->
    # quit! -> wq) has to change the sentence to the newly selected command's.
    plain = run_in_pty(binary, [path], OPEN + b"q", settle=2.5, after=2.5, cols=140, rows=35,
                       cfg=config_dir(False))
    moved = run_in_pty(binary, [path], OPEN + b"q", settle=2.5, after=2.5, cols=140, rows=35,
                       cfg=config_dir(False), phases=[(0.6, RIGHT * 4)])
    dump(moved)
    before = box_body(lines_of(plain), box_bottom(lines_of(plain), prompt_row(lines_of(plain))))
    after = box_body(lines_of(moved), box_bottom(lines_of(moved), prompt_row(lines_of(moved))))
    if not before or not after:
        print("command palette grid probe: FAIL - the box disappeared while moving the selection")
        return 1
    if not any("Quit the editor" in line for line in before):
        print("command palette grid probe: FAIL - the box does not explain the selected command")
        return 1
    if before == after:
        print("command palette grid probe: FAIL - the box did not follow the selection")
        return 1

    print("command palette grid probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
