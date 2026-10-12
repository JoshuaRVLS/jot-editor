#!/usr/bin/env python3
"""jot.screen / jot.input / jot.term: the raw layer, driven through the binary.

A user init.lua registers a per-frame painter that writes cells straight into
the grid, a key hook that consumes one chord and lets another through, and a
raw terminal write. Every check reads the reconstructed screen or the byte
stream, so what is asserted is what a terminal showed, not what Lua believes it
did:

  1. the painter's text lands at the cell it named, with the exact colour it
     asked for (which reaches the terminal as 38;2;...);
  2. jot.screen.text / jot.screen.cell read back what the painter just wrote;
  3. a key the hook consumes never reaches the buffer, while a key it declines
     still types;
  4. jot.term.write's bytes are in the frame's stream;
  5. the painter is not limited to an editor frame: it paints on the home screen
     and over an open picker, which are the frame paths that return early.

Exit 0 pass, 1 fail, 2 skip.
"""
from __future__ import annotations

import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

BINARY = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
CFG = "/tmp/jot_screen_api_cfg"
WORK = "/tmp/jot_screen_api_work"

MARK_ROW = 3
READ_ROW = 5
# The painter runs once the editor has drawn, so its cells land over the pane.
# The counter it paints next to the marker is how the probe tells that the key
# hook ran and consumed the key: LUALAYER1 on the row means one consumed chord.
INIT_LUA = r"""
local consumed = 0

jot.screen.on_paint(function(w, h)
  if w < 40 or h < 12 then
    return
  end
  jot.screen.write(2, 3, "LUALAYER", { fg = "#ff8800", bg = 0, bold = true })
  jot.screen.write(10, 3, tostring(consumed), { fg = 7, bg = 0 })
  -- Read back what this frame just wrote: text() answers the row's cells and
  -- cell() the attribute table of one of them.
  jot.screen.write(2, 5, "READBACK")
  local back = jot.screen.text(2, 5, 8)
  local cell = jot.screen.cell(2, 5)
  local ok = (back == "READBACK") and cell ~= nil and cell.text == "R"
  -- The '!' and the C/X sit in the two cells after the word, so the row reads
  -- READBACK!C when both the text and the attribute read back.
  -- size() and the painter's own (w, h) answer the same grid, and term.info()
  -- reports the terminal behind it.
  local sw, sh = jot.screen.size()
  local ti = jot.term.info()
  jot.screen.write(2, 10, "SIZE=" .. sw .. "x" .. sh .. " ARGS=" .. w .. "x" .. h)
  jot.screen.write(2, 11, "TERM=" .. ti.columns .. "x" .. ti.rows
    .. " TC=" .. tostring(ti.truecolor) .. " SCALE=" .. ti.cell_scale)
  local caps = jot.capabilities()
  jot.screen.write(2, 12, "CAP=" .. tostring(caps.screen and caps.input and caps.term and caps.lua))
  jot.screen.write(2, 5, ok and "READBACK!" or "READBACK?")
  jot.screen.write(11, 5, ok and "C" or "X")
  jot.term.write("\027]0;jotrawmarker\007")
end)

jot.input.on_key(function(event)
  if event.name == "F2" or event.char == "x" then
    consumed = consumed + 1
    return true
  end
  return false
end)
"""


def row_text(screen, row: int) -> str:
    return "".join(screen.cells[row]).rstrip()


def check_other_paths(binary: str) -> list[str]:
    """The frame paths that return before the shared tail.

    A painter that only ran on a normal editor frame would leave a script's
    overlay missing exactly when the editor is showing something of its own, so
    both of the early paths are checked on screen: the home screen (a bare run)
    and an open picker.
    """
    failures: list[str] = []
    home = run_in_pty(binary, [], b"", settle=3.0, after=1.5, cfg=CFG, cwd=WORK)
    if not any("LUALAYER" in "".join(cells) for cells in home.cells):
        failures.append("the painter's cells are missing from the home screen frame")

    picker = run_in_pty(binary,
                        [os.path.join(WORK, "probe.txt")],
                        b"\x10find probe\r",
                        settle=3.0,
                        after=3.5,
                        cfg=CFG,
                        cwd=WORK)
    picker_text = picker.text()
    if "LUALAYER" not in picker_text:
        failures.append("the painter's cells are missing from the frame with the picker open")
    if "Results" not in picker_text and "results" not in picker_text:
        failures.append("the picker never opened, so the picker frame was not exercised")
    return failures


def main() -> int:
    if not os.path.exists(BINARY):
        print(f"skip: {BINARY} is not built")
        return 2
    os.makedirs(CFG, exist_ok=True)
    os.makedirs(CFG + "/plugins", exist_ok=True)
    os.makedirs(WORK, exist_ok=True)
    with open(os.path.join(CFG, "init.lua"), "w") as fh:
        fh.write(INIT_LUA)
    target = os.path.join(WORK, "probe.txt")
    with open(target, "w") as fh:
        fh.write("line 0 of the raw layer probe\n")
        for i in range(1, 40):
            fh.write(f"line {i} of the raw layer probe\n")

    # "xy": the hook consumes 'x' and declines 'y'.
    screen = run_in_pty(BINARY, [target], b"xy", settle=3.0, after=2.5, cfg=CFG, cwd=WORK)

    failures: list[str] = []
    marked = row_text(screen, MARK_ROW)
    # The row carries the pane's own gutter in the first two columns, so the
    # painter's cells are found inside the row rather than at its start.
    if "LUALAYER1" not in marked:
        failures.append(
            f"row {MARK_ROW} is {marked!r}, wanted LUALAYER1 in it "
            "(the painter's cells, plus one consumed key counted by the hook)")
    read_back = row_text(screen, READ_ROW)
    if "READBACK!C" not in read_back:
        failures.append(
            f"row {READ_ROW} is {read_back!r}, wanted READBACK!C "
            "(the grid did not read back what the painter wrote)")
    if b"38;2;255;136;0" not in bytes(screen.raw):
        failures.append("the painter's exact colour never reached the terminal as 38;2;255;136;0")
    if b"\x1b]0;jotrawmarker\x07" not in bytes(screen.raw):
        failures.append("jot.term.write's bytes are not in the frame stream")

    size_row = row_text(screen, 10)
    if re.search(r"SIZE=(\d+)x(\d+) ARGS=\1x\2", size_row) is None:
        failures.append(
            f"row 10 is {size_row!r}, wanted jot.screen.size() to agree with the "
            "painter's own width and height")
    term_row = row_text(screen, 11)
    if "TC=true" not in term_row or "SCALE=1" not in term_row:
        failures.append(f"row 11 is {term_row!r}, wanted TERM=... TC=true SCALE=1")
    if "CAP=true" not in row_text(screen, 12):
        failures.append(
            f"row 12 is {row_text(screen, 12)!r}, wanted jot.capabilities() to list "
            "screen, input, term and lua")

    buffer_row = next((r.rstrip() for r in ("".join(c) for c in screen.cells) if "line 0 of" in r), "")
    if "yline 0 of" not in buffer_row:
        failures.append(
            f"the buffer's first line reads {buffer_row!r}, wanted it to hold "
            "\"yline 0 of\": the consumed 'x' must not be typed and the declined 'y' must be")

    failures += check_other_paths(BINARY)

    if failures:
        for line in failures:
            print("FAIL:", line)
        print("--- screen ---")
        print(screen.text())
        return 1
    print("ok: painter cells (editor frame, home and picker), grid read-back, "
          "key consumption and raw terminal bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
