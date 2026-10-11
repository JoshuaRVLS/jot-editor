#!/usr/bin/env python3
"""Probe: the telescope picker's flat two-box design, on both render paths.

Checks the contract the design is built on, from a real pty:

  * two boxes with their own straight borders and exactly one column between
    them, neither sharing an edge;
  * each box names itself centred in its top border (Results / Preview), the
    finder and its folder scope are centred in the list box's bottom border, and
    the file view's bottom border carries the line count and size;
  * a row is the file's path from the picker's scope, after its file icon -- two
    same-named files in different folders are told apart by their rows;
  * the selection is a `>` pointer in the rows' gutter plus the row's band, and
    the pointer's two cells are reserved on every row so the names never shift;
  * the prompt is the list box's LAST interior row, with `N / M` at its right
    end, and the caret is in that row (a float-relative caret column parks the
    hardware cursor outside the picker);
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
import re
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

WS = "/tmp/jot_telescope_probe_ws"
CFG = "/tmp/jot_telescope_probe_cfg"
COLS, ROWS = 150, 32

BOX = set("│┌┐└┘─┬┴├┤╭╮╯╰")
POINTER = "> "
# Footer hints the picker used to spell out; the two-box design has no key-hint
# footer, so any of these on screen is stale chrome.
STALE = ["Enter open", "Esc close", "↑/↓ move", "Up/Down move", "PgUp/PgDn"]
# The files the probe's workspace holds: every row has to name one of them.
FILES = ["frame.cpp", "buffer.cpp", "keys.cpp", "notes.txt", "README.md"]


def write_workspace() -> str:
    shutil.rmtree(WS, ignore_errors=True)
    os.makedirs(os.path.join(WS, "src", "render"), exist_ok=True)
    os.makedirs(os.path.join(WS, "src", "input"), exist_ok=True)
    with open(os.path.join(WS, "src", "render", "frame.cpp"), "w") as fh:
        fh.write("// frame paint\nint paint_frame(int w) {\n  return w + 1;\n}\n")
    with open(os.path.join(WS, "src", "render", "buffer.cpp"), "w") as fh:
        fh.write("int draw_buffer() { return 0; }\n")
    # The same name in a second folder: the row has to say which one it is, which
    # is why the label is the path and not the bare name.
    with open(os.path.join(WS, "src", "input", "frame.cpp"), "w") as fh:
        fh.write("int read_frame() { return 2; }\n")
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


def border_columns(screen, y: int):
    return [x for x, ch in enumerate(rows(screen)[y]) if ch in BOX]


def band_bg(screen, y: int, x: int) -> int:
    return screen.bg[y][x]


def caret(screen):
    """(row, col), 0-based, of the hardware caret as the terminal last showed it.

    The editor parks the cursor with an absolute CUP and then shows it, so the
    last `?25h` in the stream is preceded by the cells the caret sits on. Nothing
    else about a frame says where the caret is: it is placed, not drawn.
    """
    raw = bytes(screen.raw)
    at = raw.rfind(b"\x1b[?25h")
    if at < 0:
        return None
    hit = None
    for hit in re.finditer(rb"\x1b\[(\d+);(\d+)H", raw[:at]):
        pass
    if hit is None:
        return None
    return int(hit.group(1)) - 1, int(hit.group(2)) - 1


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
    """The two boxes' columns from the top border row: (top, left, list_right,
    view_x, view_right, bottom), or None when the picker is not up."""
    top = bottom = None
    left = list_right = view_x = view_right = None
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


def centred(row: str, first: int, last: int, label: str) -> tuple[bool, str]:
    """Is `label` inside the box `first..last` and horizontally centred in it?"""
    if label not in row:
        return False, "not on the row"
    at = row.index(label) + len(label) // 2
    if not (first < at < last):
        return False, f"outside the box ({first}..{last})"
    middle = (first + last) / 2
    if abs(at - middle) > 3:
        return False, f"at {at}, box centre {middle}"
    return True, ""


def probe_boxes(screen, c: Checks) -> None:
    """Two frames, one column apart, neither sharing a border."""
    geo = geometry(screen)
    c.check("the picker is on screen", geo is not None)
    if geo is None:
        return
    top, left, list_right, view_x, view_right, _bottom = geo
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
            glyphs[:4] == [left, list_right, view_x, view_right],
            str(glyphs[:6]))


def prompt_row(screen):
    """The row index of the list box's prompt -- its last interior row -- or None.

    The prompt is the row under the results, closed by the box's bottom border:
    with the results above it, that is the row just before the border row.
    """
    geo = geometry(screen)
    if geo is None:
        return None
    _top, left, list_right, _view_x, _view_right, bottom = geo
    for y in range(bottom - 1, _top, -1):
        if rows(screen)[y][left:list_right].startswith("│"):
            return y if POINTER in rows(screen)[y][left:list_right + 1] else None
    return None


def list_rows(screen):
    """(row index, text) for the picker's result rows, list box only.

    A result row is a file's path: a row of the list whose interior has content,
    minus the prompt (the box's last interior row) and the empty rows under the
    last result.
    """
    geo = geometry(screen)
    if geo is None:
        return []
    top, left, list_right, _view_x, _view_right, bottom = geo
    prompt = prompt_row(screen)
    out = []
    for y in range(top + 1, bottom):
        if y == prompt:
            continue
        row = rows(screen)[y]
        if not row or left >= len(row):
            continue
        cell = row[left:list_right]
        if cell.startswith("│") and cell[1:].strip():
            out.append((y, cell))
    return out


def probe_labels(screen, c: Checks) -> None:
    """Each box names itself, centred in its borders."""
    geo = geometry(screen)
    if geo is None:
        return
    top, left, list_right, view_x, view_right, bottom = geo
    for (first, last, y, label) in ((left, list_right, top, "Results"),
                                    (view_x, view_right, top, "Preview"),
                                    (left, list_right, bottom, "Find Files")):
        ok, detail = centred(rows(screen)[y], first, last, label)
        c.check(f"{label!r} is centred in its box's border", ok, detail)


def probe_row_labels(screen, c: Checks) -> None:
    """A row is the file's path, so two same-named files read differently."""
    listed = list_rows(screen)
    text = " ".join(cell for _y, cell in listed)
    c.check("the list has result rows", len(listed) >= 3, f"{len(listed)} rows")
    c.check("a row carries the file's whole path", "src/render/frame.cpp" in text, text[:200])
    c.check("the same name in another folder is its own row",
            "src/input/frame.cpp" in text, text[:200])
    # Nothing but files: a folder is walked, never listed, so every row names a
    # file rather than stopping at a folder.
    c.check("every row names a file",
            all(any(name in cell for name in FILES) for _y, cell in listed),
            str([cell.strip() for _y, cell in listed])[:200])


def probe_pointer(screen, c: Checks) -> None:
    """The selection is a `>` in the gutter plus the row's band."""
    geo = geometry(screen)
    if geo is None:
        return
    _top, left, _list_right, _view_x, _view_right, _bottom = geo
    prompt = prompt_row(screen)
    listed = list_rows(screen)
    pointed = [y for y, cell in listed if cell[1:3] == POINTER]
    plain = [y for y, cell in listed if cell[1:3] == "  "]
    c.check("exactly one row carries the pointer", len(pointed) == 1, str(pointed))
    c.check("the other rows keep the gutter", len(plain) >= 1, str(plain))
    c.check("the prompt row has its own marker",
            prompt is not None and rows(screen)[prompt][left + 1:left + 3] == POINTER)
    if not pointed or not plain:
        return
    band = band_bg(screen, pointed[0], left + 3)
    other = band_bg(screen, plain[0], left + 3)
    c.check("the pointed row carries a band", band != other, f"pointed {band}, plain {other}")
    c.check("the band stops at the border",
            band_bg(screen, pointed[0], left) != band,
            f"border {band_bg(screen, pointed[0], left)}, band {band}")


def probe_prompt(screen, c: Checks, typed: str = "") -> None:
    """The prompt is the box's last interior row, count at its right end."""
    geo = geometry(screen)
    if geo is None:
        return
    _top, left, list_right, _view_x, _view_right, bottom = geo
    prompt = prompt_row(screen)
    c.check("the prompt is the box's last interior row", prompt == bottom - 1,
            f"prompt {prompt}, bottom border {bottom}")
    if prompt is None:
        return
    row = rows(screen)[prompt]
    c.check("the prompt is marked", row[left + 1:left + 3] == POINTER, repr(row[left:left + 12]))
    c.check("what was typed follows the marker", row[left + 3:left + 3 + len(typed)] == typed,
            repr(row[left:left + 20]))
    # The count sits against the box's last interior cell.
    tail = row[list_right - 8:list_right]
    c.check("the prompt carries the match count", re.search(r"\d+ / \d+", tail) is not None, repr(tail))


def probe_caret(screen, c: Checks) -> None:
    """The caret is in the prompt row, inside the list box."""
    geo = geometry(screen)
    if geo is None:
        return
    _top, left, list_right, _view_x, _view_right, bottom = geo
    at = caret(screen)
    c.check("a caret is placed", at is not None)
    if at is None:
        return
    row, col = at
    c.check("the caret is on the prompt row", row == bottom - 1, f"caret row {row}")
    c.check("the caret is inside the list box", left + 1 <= col <= list_right - 1,
            f"col {col}, box {left + 1}..{list_right - 1}")


def probe_file_view(screen, c: Checks) -> None:
    """The right box is a small editor: label, gutter, content, status."""
    geo = geometry(screen)
    if geo is None:
        return
    top, _left, _list_right, view_x, view_right, bottom = geo
    content = None
    for y in range(top + 1, bottom):
        if "draw_buffer" in rows(screen)[y]:
            content = rows(screen)[y]
            break
    c.check("the file's text is in the view", content is not None)
    if content is not None:
        inner = content[view_x + 1:view_right]
        c.check("the view has a line-number gutter", inner.lstrip().startswith("1 "),
                repr(inner[:12]))
    status = rows(screen)[bottom][view_x:view_right]
    c.check("the view's bottom border carries the line count", "lines" in status, repr(status))
    c.check("the view's bottom border carries the size", "B" in status, repr(status))


def probe_filter(screen, c: Checks) -> None:
    text = "\n".join(rows(screen))
    c.check("typing narrows the list to the match", "keys.cpp" in text)
    c.check("the non-matching files are gone", "notes.txt" not in text)
    listed = list_rows(screen)
    c.check("a query leaves one row", len(listed) == 1, f"{len(listed)} rows")
    c.check("the count follows the filter", "1 / 1" in text, text[-200:])


def probe_scope(screen, c: Checks, expect_folder: str | None, expect_path: str | None) -> None:
    text = "\n".join(rows(screen))
    if expect_folder is None:
        c.check("the foot drops the folder scope at the root",
                "Find Files · " not in text)
    else:
        c.check(f"the foot names the folder scope {expect_folder}",
                f"Find Files · {expect_folder}" in text, text.split("\n")[-3] if screen.rows > 3 else "")
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
    probe_labels(screen, c)
    probe_row_labels(screen, c)
    probe_pointer(screen, c)
    probe_prompt(screen, c)
    probe_caret(screen, c)
    probe_file_view(screen, c)
    text = "\n".join(rows(screen))
    for stale in STALE:
        c.check(f"no stale hint {stale!r}", stale not in text)

    # --- the query -----------------------------------------------------------
    # The query is typed once the picker is up: sent with the key that opens it,
    # the first character lands in the editor instead (it is not in the field).
    typed = run(binary, native, b"\x05", phases=[(1.0, b"keys")])
    probe_prompt(typed, c, "keys")
    probe_caret(typed, c)
    probe_filter(typed, c)

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
            "notes.txt" in back and "Find Files · src" not in back)

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
