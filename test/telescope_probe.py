#!/usr/bin/env python3
"""Probe: the telescope picker's two-box design, on both render paths.

Checks the contract the redesign is built on, from a real pty:

  * two boxes with their own borders and exactly one column between them;
  * the list holds files only -- where a file lives is the dimmed folder path
    on its row, never a folder row;
  * the leading dot is lit for a file that already has a tab and hollow
    otherwise;
  * the selection (and the hovered row, which selects it) is a filled band
    across the list's interior, with the border cells left on the panel's own
    background;
  * the file view is a small editor: the file name in its top border, line
    numbers, the file's own text, and a line-count / size chip in its bottom
    border;
  * the workspace is the folder floor: `:find src` scopes the picker down to
    that folder and backspace cannot climb above the root it opened at.

Both render paths are probed: the bundled Lua UI kit (the default) and the
native renderer, reached by unregistering the Lua telescope handler from the
probe's own init.lua.

Usage: test/telescope_probe.py [path-to-jot]
Exit codes: 0 pass, 1 fail, 2 binary missing.
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

WS = "/tmp/jot_telescope_probe_ws"
CFG = "/tmp/jot_telescope_probe_cfg"
COLS, ROWS = 150, 32

BOX = set("│┌┐└┘─┬┴├┤╭╮╯╰")
DOT_OPEN = "\uf111"
DOT_CLOSED = "\uf10c"
# Footer hints the picker used to spell out; the two-box design has no key-hint
# footer, so any of these on screen is stale chrome.
STALE = ["Enter open", "Esc close", "↑/↓ move", "Up/Down move", "PgUp/PgDn"]


def write_workspace() -> str:
    shutil.rmtree(WS, ignore_errors=True)
    os.makedirs(os.path.join(WS, "src", "render"), exist_ok=True)
    os.makedirs(os.path.join(WS, "src", "input"), exist_ok=True)
    with open(os.path.join(WS, "src", "render", "frame.cpp"), "w") as fh:
        fh.write("// frame paint\nint paint_frame(int w) {\n  return w + 1;\n}\n")
    with open(os.path.join(WS, "src", "render", "buffer.cpp"), "w") as fh:
        fh.write("int draw_buffer() { return 0; }\n")
    with open(os.path.join(WS, "src", "input", "keys.cpp"), "w") as fh:
        fh.write("int route_key() { return 1; }\n")
    with open(os.path.join(WS, "notes.txt"), "w") as fh:
        fh.write("alpha\nbeta\ngamma\n")
    with open(os.path.join(WS, "README.md"), "w") as fh:
        fh.write("# probe\n")
    if not os.path.isdir(os.path.join(WS, ".git")):
        subprocess.run(["git", "init", "-q", WS], check=False)
    return WS


def config_dir(native: bool) -> str:
    """A probe-owned config home. `native` unregisters the Lua telescope
    handler so the native renderer is the one on screen."""
    cfg = f"{CFG}_{'native' if native else 'lua'}"
    os.makedirs(cfg, exist_ok=True)
    with open(os.path.join(cfg, "init.lua"), "w") as fh:
        if native:
            fh.write('jot.ui.handler("telescope", nil)\n')
    return cfg


def rows(screen):
    return ["".join(row) for row in screen.cells]


def caret(screen, needle: str):
    """(row, first column) of `needle`, or None."""
    for y, row in enumerate(rows(screen)):
        if needle in row:
            return y, row.index(needle)
    return None


def border_columns(screen, y: int):
    return [x for x, ch in enumerate(rows(screen)[y]) if ch in BOX]


def band_bg(screen, y: int, x: int) -> int:
    return screen.bg[y][x]


class Checks:
    def __init__(self, label: str):
        self.label = label
        self.failures: list[str] = []

    def check(self, name: str, ok: bool, detail: str = "") -> None:
        if not ok:
            self.failures.append(f"{name}{(' -- ' + detail) if detail else ''}")

    def report(self) -> int:
        if self.failures:
            print(f"telescope probe [{self.label}]: FAIL")
            for f in self.failures:
                print(f"  - {f}")
            return 1
        print(f"telescope probe [{self.label}]: PASS")
        return 0


def geometry(screen):
    """The two boxes' columns from the top border row: (x, list_right, view_x,
    view_right), or None when the picker is not up."""
    for y, row in enumerate(rows(screen)):
        if row.count("┌") == 2 and "┐" in row:
            left = row.index("┌")
            list_right = row.index("┐", left)
            view_x = row.index("┌", list_right)
            view_right = row.rindex("┐")
            return y, left, list_right, view_x, view_right
    return None


def probe_boxes(screen, c: Checks) -> None:
    """Two frames, one column apart, neither sharing a border."""
    geo = geometry(screen)
    c.check("the picker is on screen", geo is not None)
    if geo is None:
        return
    top, left, list_right, view_x, view_right = geo
    c.check("the list box has a right border of its own", list_right > left + 2)
    c.check("the file view box has a left border of its own", view_right > view_x + 2)
    c.check("one column between the two borders",
            view_x - list_right == 2,
            f"list right {list_right}, view left {view_x}")
    gap = rows(screen)[top][list_right + 1]
    c.check("the gap is not a border", gap not in BOX, repr(gap))
    # A body row: four border cells, and the gap between the boxes stays empty.
    body = top + 1
    for y in range(body, min(body + 12, screen.rows)):
        if rows(screen)[y].count("│") >= 3:
            body = y
            break
    glyphs = border_columns(screen, body)
    c.check("both boxes are drawn on a body row",
            glyphs == [left, list_right, view_x, view_right],
            str(glyphs))


def list_rows(screen):
    """(row index, text) for the picker's result rows, list box only.

    The tab strip also spells the open file's name, so every row question has to
    be asked inside the list box: from the first result row down, up to the
    border that closes the box.
    """
    geo = geometry(screen)
    if geo is None:
        return []
    top, left, list_right, _view_x, _view_right = geo
    out = []
    for y in range(top + 1, screen.rows):
        row = rows(screen)[y]
        if not row or left >= len(row):
            continue
        name = row[left:list_right]
        if DOT_CLOSED in name or DOT_OPEN in name:
            out.append((y, name))
    return out


def row_entry(name_cell: str) -> str:
    """The entry text of a list row: everything after the dot and the icon."""
    for dot in (DOT_OPEN, DOT_CLOSED):
        at = name_cell.find(dot)
        if at >= 0:
            rest = name_cell[at + len(dot):].strip()
            return rest.split(" ")[-1] if False else rest
    return ""


def probe_list_rows(screen, c: Checks) -> None:
    """Files only, each with its dimmed folder path; the open file's dot is lit."""
    text = "\n".join(rows(screen))
    c.check("files are listed", "frame.cpp" in text and "notes.txt" in text)
    listed = list_rows(screen)
    c.check("the list has result rows", len(listed) >= 3, f"{len(listed)} rows")
    # No row is a folder: every entry is a file name (the icons are glyphs, so a
    # folder row would read as the bare folder name).
    entries = [row_entry(cell) for _y, cell in listed]
    for folder in ("src", "input", "render", "src/render/", "src/input/"):
        c.check(f"no folder row for {folder}", folder not in entries, str(entries))
    # The dimmed folder keeps the deepest part of the path, on the file's row.
    frame_row = [y for y, cell in listed if "frame.cpp" in cell]
    c.check("the file's row is in the list", bool(frame_row))
    if frame_row:
        c.check("the row carries the file's folder",
                "src/render/" in rows(screen)[frame_row[0]])
    # The dot: lit on the file that already has a tab, hollow on the others.
    open_row = [cell for _y, cell in listed if "buffer.cpp" in cell]
    c.check("the open file's row is on screen", bool(open_row))
    if open_row:
        c.check("the open file's dot is lit", DOT_OPEN in open_row[0], open_row[0].strip())
    other_row = [cell for _y, cell in listed if "notes.txt" in cell]
    if other_row:
        c.check("a closed file's dot is hollow", DOT_CLOSED in other_row[0], other_row[0].strip())


def probe_selection_band(screen, c: Checks) -> None:
    """The selected row is a filled band; the border cells keep the box's fill."""
    geo = geometry(screen)
    if geo is None:
        return
    _top, left, list_right, _view_x, _view_right = geo
    listed = list_rows(screen)
    selected = [y for y, cell in listed if DOT_OPEN in cell]
    plain = [y for y, cell in listed if DOT_CLOSED in cell]
    c.check("a selected result row is on screen", bool(selected))
    c.check("an unselected result row is on screen", bool(plain))
    if not selected or not plain:
        return
    band = band_bg(screen, selected[0], left + 2)
    other = band_bg(screen, plain[0], left + 2)
    c.check("the selected row carries a band", band != other, f"selected {band}, plain {other}")
    c.check("the band stops at the border",
            band_bg(screen, selected[0], left) != band_bg(screen, selected[0], left + 2),
            f"border {band_bg(screen, selected[0], left)}, band {band}")


def probe_file_view(screen, c: Checks) -> None:
    """The right box is a small editor: title, line numbers, content, status."""
    geo = geometry(screen)
    if geo is None:
        return
    top, left, list_right, view_x, view_right = geo
    title_row = rows(screen)[top]
    c.check("the file name is the view's title", "buffer.cpp" in title_row[view_x:view_right],
            title_row[view_x:view_right].strip())
    content = None
    for y in range(top + 1, screen.rows):
        if "draw_buffer" in rows(screen)[y]:
            content = rows(screen)[y]
            break
    c.check("the file's text is in the view", content is not None)
    if content is not None:
        inner = content[view_x + 1:view_right]
        c.check("the view has a line-number gutter", inner.lstrip().startswith("1 "),
                repr(inner[:12]))
    status = None
    for y in range(screen.rows - 1, top, -1):
        row = rows(screen)[y]
        if "└" in row and "lines" in row:
            status = row
            break
    c.check("the view's bottom border carries the line count", status is not None)
    c.check("the list's bottom border carries the file count", "files" in "\n".join(rows(screen)))


def probe_hover(screen, c: Checks) -> None:
    """The pointer's row becomes the selection."""
    geo = geometry(screen)
    if geo is None:
        return
    _top, left, _list_right, _view_x, _view_right = geo
    listed = list_rows(screen)
    target = [y for y, cell in listed if "notes.txt" in cell]
    c.check("a row to hover is on screen", bool(target))
    if not target:
        return
    band = band_bg(screen, target[0], left + 2)
    others = {band_bg(screen, y, left + 2) for y, cell in listed if "notes.txt" not in cell}
    c.check("hovering selects that row", band not in others, f"hovered {band}, others {others}")


def probe_filter(screen, c: Checks) -> None:
    text = "\n".join(rows(screen))
    c.check("typing narrows the list to the match", "keys.cpp" in text)
    c.check("the non-matching files are gone", "notes.txt" not in text)
    c.check("the count chip follows the filter", "1 file" in text, text[-120:])


def probe_scope(screen, c: Checks, expect_folder: str | None, expect_path: str | None) -> None:
    text = "\n".join(rows(screen))
    if expect_folder is None:
        c.check("the header drops the folder scope at the root", "· src" not in text)
    else:
        c.check(f"the header names the folder scope {expect_folder}", f"· {expect_folder}" in text,
                text.split("\n")[3] if screen.rows > 3 else "")
    if expect_path:
        c.check("rows are relative to the scope", expect_path in text)


def run(binary: str, native: bool, keys: bytes, after: float = 4.0, phases=None):
    return run_in_pty(binary, [os.path.join(WS, "src", "render", "buffer.cpp")], keys,
                      settle=3.0, after=after, cols=COLS, rows=ROWS,
                      cfg=config_dir(native), cwd=WS, phases=phases)


def probe_renderer(binary: str, native: bool) -> int:
    label = "native renderer" if native else "lua kit"
    c = Checks(label)

    # --- the picker, its list and the file view ------------------------------
    screen = run(binary, native, b"\x05")
    probe_boxes(screen, c)
    probe_list_rows(screen, c)
    probe_selection_band(screen, c)
    probe_file_view(screen, c)
    text = "\n".join(rows(screen))
    for stale in STALE:
        c.check(f"no stale hint {stale!r}", stale not in text)

    # --- hover ---------------------------------------------------------------
    geo = geometry(screen)
    hover_row = None
    if geo is not None:
        for y in range(geo[0] + 1, screen.rows):
            if "notes.txt" in rows(screen)[y]:
                hover_row = y
    if hover_row is not None and geo is not None:
        motion = b"\x1b[<35;%d;%dM" % (geo[1] + 4, hover_row + 1)
        # The motion has to arrive after the picker's asynchronous scan has
        # filled the list: a hover over an empty list selects nothing.
        probe_hover(run(binary, native, b"\x05", phases=[(1.0, motion * 2)]), c)

    # --- the query -----------------------------------------------------------
    probe_filter(run(binary, native, b"\x05keys"), c)

    # --- the workspace is the folder floor -----------------------------------
    # Commands run through the palette (Ctrl+P): the editor's own input mode
    # takes ':' as text.
    scoped = run(binary, native, b"\x10find src\r")
    probe_scope(scoped, c, "src", "render/")
    subtree = list_rows(scoped)
    c.check("the scope lists only its own files",
            all("notes.txt" not in cell for _y, cell in subtree), str(subtree))
    up_once = run(binary, native, b"\x10find src\r\x7f")
    probe_scope(up_once, c, None, None)
    root_files = "\n".join(rows(up_once))
    c.check("backspace returns to the workspace root", "notes.txt" in root_files)
    up_twice = run(binary, native, b"\x10find src\r\x7f\x7f")
    back = "\n".join(rows(up_twice))
    c.check("backspace cannot climb above the workspace",
            "notes.txt" in back and "· src" not in back)

    return c.report()


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    if not os.path.exists(binary):
        print(f"telescope probe: SKIP - no binary at {binary}")
        return 2
    write_workspace()
    status = 0
    for native in (False, True):
        status |= probe_renderer(binary, native)
    return status


if __name__ == "__main__":
    raise SystemExit(main())
