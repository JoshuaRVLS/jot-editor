#!/usr/bin/env python3
"""Probe: each telescope box is painted with the theme slots it stands for.

The redesign made the picker two surfaces: the list box is a panel (the
theme's TelescopeNormal background, with TelescopeSelection as the band on the
selected row and TelescopeQuery on the query field), and the file view is an
editor (its own background, TelescopePreviewNormal). This guards that mapping
on both render paths -- a later change that fills both boxes with one colour,
or drops the selection band back to a caret, fails here.

The theme is jot-light, where the four slots are four distinct colours
(#f1eadd list, #e2d8c6 selection, #f9f4ea view, #f9f4ea query), so "which slot
is this cell using" is a decidable question. The file view's background is also
required to be the editor's own Normal background: the right box is meant to
read as a normal editor, not as a preview pane in a different palette.

Usage: test/telescope_theme_probe.py [path-to-jot]
Exit codes: 0 pass, 1 fail, 2 binary missing.
"""
from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

THEME = "jot-light"
WS = "/tmp/jot_telescope_theme_probe"
CFG = "/tmp/jot_telescope_theme_probe_cfg"
COLS, ROWS = 120, 32
BOX = set("│┌┐└┘─┬┴├┤╭╮╯╰")
# Chrome the picker used to spell out: the key-hint footer it no longer has, and
# the "Preview" label the right box no longer is. The probe files are named so
# none of these can arrive as content by accident.
STALE = ["Enter open", "Esc close", "↑/↓ move", "Up/Down move", "PgUp/PgDn",
         "Up/Down scroll", "Preview"]


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
    path = os.path.join(".configs", "configs", "colors", f"{THEME}.json")
    data = json.load(open(path))
    return {
        "list": code(data["TelescopeNormal"]["bg"]),
        "selection": code(data["TelescopeSelection"]["bg"]),
        "view": code(data["TelescopePreviewNormal"]["bg"]),
        "query": code(data["TelescopeQuery"]["bg"]),
        "editor": code(data["Normal"]["bg"]),
        # The float's own colour, which is what the picker paints the column
        # between the boxes with (theme.panel_border / the kit's float fill).
        "backdrop": code(data["FloatBorder"]["bg"]),
    }


def write_workspace() -> str:
    shutil.rmtree(WS, ignore_errors=True)
    os.makedirs(os.path.join(WS, "src", "render"), exist_ok=True)
    with open(os.path.join(WS, "src", "render", "frame.cpp"), "w") as fh:
        fh.write("// frame paint\nint paint_frame(int w) {\n  return w + 1;\n}\n")
    with open(os.path.join(WS, "notes.txt"), "w") as fh:
        fh.write("alpha\nbeta\ngamma\n")
    if not os.path.isdir(os.path.join(WS, ".git")):
        subprocess.run(["git", "init", "-q", WS], check=False)
    return WS


def config_dir(native: bool) -> str:
    """A probe-owned config home pinned to the probe theme. `native` unregisters
    the Lua telescope handler so the native renderer is the one on screen."""
    cfg = f"{CFG}_{'native' if native else 'lua'}"
    os.makedirs(os.path.join(cfg, "configs"), exist_ok=True)
    with open(os.path.join(cfg, "configs", "settings.conf"), "w") as fh:
        fh.write(f"color_scheme={THEME}\n")
    with open(os.path.join(cfg, "init.lua"), "w") as fh:
        if native:
            fh.write('jot.ui.handler("telescope", nil)\n')
    return cfg


def rows(screen):
    return ["".join(row) for row in screen.cells]


def geometry(screen):
    """(top, left, list_right, view_x, view_right, bottom) from the borders, or
    None when the picker is not up."""
    top = bottom = None
    for y, row in enumerate(rows(screen)):
        if top is None and row.count("┌") == 2:
            top = y
            left = row.index("┌")
            list_right = row.index("┐", left)
            view_x = row.index("┌", list_right)
            view_right = row.rindex("┐")
        if top is not None and "└" in row:
            bottom = y
    if top is None or bottom is None:
        return None
    return top, left, list_right, view_x, view_right, bottom


class Checks:
    def __init__(self, label: str):
        self.label = label
        self.failures: list[str] = []

    def check(self, name: str, ok: bool, detail: str = "") -> None:
        if not ok:
            self.failures.append(f"{name}{(' -- ' + detail) if detail else ''}")

    def report(self) -> int:
        if self.failures:
            print(f"telescope theme probe [{self.label}]: FAIL")
            for f in self.failures:
                print(f"  - {f}")
            return 1
        print(f"telescope theme probe [{self.label}]: PASS")
        return 0


def body_bgs(screen, y: int, first: int, last: int) -> set:
    """Background codes across a cell range on one row."""
    return {screen.bg[y][x] for x in range(first, last + 1)}


def probe(screen, slots: dict, c: Checks) -> None:
    geo = geometry(screen)
    c.check("the picker is on screen", geo is not None)
    if geo is None:
        return
    top, left, list_right, view_x, view_right, bottom = geo
    query_y = top + 1
    # The query field is inset one cell from either border (query_w = list_w-2).
    query_first, query_last = left + 2, list_right - 2
    list_first, list_last = left + 1, list_right - 1
    view_first, view_last = view_x + 1, view_right - 1

    # --- the query field ------------------------------------------------------
    c.check("the query field carries the TelescopeQuery band",
            body_bgs(screen, query_y, query_first, query_last) == {slots["query"]},
            str(body_bgs(screen, query_y, query_first, query_last)))

    # --- the list box: a panel, with the selection as a band ------------------
    list_rows_bgs = {}
    for y in range(query_y + 1, bottom):
        list_rows_bgs[y] = body_bgs(screen, y, list_first, list_last)
    plain = {y: b for y, b in list_rows_bgs.items() if b == {slots["list"]}}
    banded = {y: b for y, b in list_rows_bgs.items() if b == {slots["selection"]}}
    c.check("the list box is drawn on TelescopeNormal",
            len(plain) >= 1, f"bgs by row: {list_rows_bgs}")
    c.check("exactly one row is the selection band", len(banded) == 1,
            f"banded rows: {sorted(banded)}")
    c.check("the selection band is TelescopeSelection, not the list colour",
            slots["selection"] != slots["list"])
    c.check("the band spans the whole interior row",
            all(len(b) == 1 for b in banded.values()),
            f"banded: {banded}")
    c.check("no list row mixes backgrounds",
            all(len(b) == 1 for b in list_rows_bgs.values()),
            f"bgs by row: {list_rows_bgs}")

    # --- the file view: an editor surface of its own --------------------------
    view_bgs = {y: body_bgs(screen, y, view_first, view_last)
                for y in range(top + 1, bottom)}
    flat = {y: b for y, b in view_bgs.items() if b == {slots["view"]}}
    c.check("the file view is drawn on TelescopePreviewNormal",
            len(flat) == len(view_bgs), f"bgs by row: {view_bgs}")
    c.check("the view's background is the editor's own",
            slots["view"] == slots["editor"],
            f"view {slots['view']}, editor {slots['editor']}")
    c.check("the two boxes are two surfaces", slots["list"] != slots["view"])

    # --- between the boxes ----------------------------------------------------
    gap_cols = range(list_right + 1, view_x)
    glyphs = [rows(screen)[y][x] for y in range(top + 1, bottom) for x in gap_cols]
    c.check("the column between the boxes holds no border",
            not any(g in BOX for g in glyphs), "".join(sorted(set(glyphs))))
    gap_bgs = {screen.bg[y][x] for y in range(top + 1, bottom) for x in gap_cols}
    c.check("the list box's fill stops at its border",
            slots["list"] not in gap_bgs, str(gap_bgs))
    c.check("the gap is the picker's own float colour",
            gap_bgs == {slots["backdrop"]}, str(gap_bgs))

    # --- no retired chrome ----------------------------------------------------
    text = "\n".join(rows(screen))
    for stale in STALE:
        c.check(f"no stale chrome {stale!r}", stale not in text)


def run(binary: str, native: bool):
    return run_in_pty(binary, [os.path.join(WS, "src", "render", "frame.cpp")],
                      b"\x05", settle=3.0, after=4.0, cols=COLS, rows=ROWS,
                      cfg=config_dir(native), cwd=WS)


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    if not os.path.exists(binary):
        print(f"telescope theme probe: SKIP - no binary at {binary}")
        return 2
    slots = theme_slots()
    print(f"telescope theme probe: {THEME} slots {slots}")
    write_workspace()

    failures = 0
    for native in (False, True):
        label = "native renderer" if native else "lua kit"
        c = Checks(label)
        probe(run(binary, native), slots, c)
        failures += c.report()
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
