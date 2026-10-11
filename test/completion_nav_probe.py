#!/usr/bin/env python3
"""End-to-end check: Up/Down move the completion popup's selected row.

The popup's list is rebuilt on every frame from the word under the caret, and
the selection used to be re-derived from the selected row's *label* during that
rebuild. A server's overloads share one label -- clangd answers `incl` with a
family of `std::includes` rows, all labeled "std::includes" -- so the rebuild
snapped the selection back to the family's first row before the frame was even
painted: every Down moved the highlight for a moment and the next frame put it
back, and the popup could never be browsed past the first overload.

This drives the real binary against the real clangd, types `incl` in a C++
file, and reads the popup's own footer (its "N/total" position counter) and
selection band off the terminal stream -- the two things a user sees move. The
same run also checks the other half: Up walks back. The scenes after that aim
SGR mouse events at the box the popup painted, because the pointer belongs to
the list there: a wheel notch walks it and a press takes the clicked row, both
with the popup staying up until the press takes it (before, either one fell
through to the buffer -- the wheel dismissed the popup, a press moved the caret).

Usage: test/completion_nav_probe.py [path-to-jot-binary]
Exit codes: 0 pass, 1 fail, 2 binary or clangd missing.
"""
from __future__ import annotations

import os
import re
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

SOURCE = """#include <cstdio>

int main() {

}
"""

# The popup footer reads `1/15  incl  filtered`: the selected index over the
# listed total, right after the box's left border. Searched with the border in
# the pattern so the editor's own `1/…` counters can never match it.
FOOTER = re.compile(r"│\s*(\d+)/(\d+)\s")

# The popup's top border: `┌` at the box's left column, `┐` at its right.
BOX_TOP = re.compile(r"┌─+┐")

# jot's selection background (PmenuSel), where the selected row's band is
# painted. Truecolour cells are stored as 1000 + rgb.
SELECTION_BG = 1000 + 0x453D41


def rows(screen):
    return ["".join(screen.cells[row]) for row in range(screen.rows)]


def popup_or_none(screen):
    try:
        return popup_position(screen)
    except AssertionError:
        return None


def popup_box(screen) -> tuple[int, int, int, int]:
    """(top row, left col, right col, bottom row) of the popup's box.

    The bottom row is found by the `└` under the top-left corner's column, not
    by a matching right corner: the screen reader decodes cell by cell, and a
    border glyph split across pty reads comes back as `?` cells, which says
    nothing about the frame jot painted (the byte stream has the corner).
    """
    for y in range(screen.rows):
        match = BOX_TOP.search("".join(screen.cells[y]))
        if not match:
            continue
        left = match.start()
        right = match.end() - 1
        for bottom in range(y + 1, screen.rows):
            text = "".join(screen.cells[bottom])
            if len(text) > left and text[left] == "└":
                return y, left, right, bottom
        break
    raise AssertionError("the completion popup box is not on screen")


def popup_position(screen) -> tuple[int, int, int]:
    """(selected row index, position on the list, list length) of the popup.

    The position comes from the footer the surface draws itself ("3/15"), the
    selected row from the band it paints behind the highlight. Both are read off
    the screen, so neither can pass by the state being right while the frame is
    not.
    """
    box_top, _left, _right, box_bottom = popup_box(screen)
    footer_y = -1
    position = length = 0
    for y in range(box_top + 1, box_bottom):
        match = FOOTER.search("".join(screen.cells[y]))
        if match and int(match.group(2)) > 1:
            footer_y = y
            position = int(match.group(1))
            length = int(match.group(2))
            break

    if footer_y < 0:
        raise AssertionError("completion popup footer not found on screen")

    # The band inside the box, between its top border and the footer: the
    # selected row is the only one painted with the selection colour there.
    band_y = -1
    for y in range(box_top + 1, footer_y):
        run = 0
        for x in range(screen.cols):
            if screen.bg[y][x] == SELECTION_BG:
                run += 1
                if run >= 20 and band_y < 0:
                    band_y = y
            else:
                run = 0
    if band_y < 0:
        raise AssertionError("no selection band painted in the popup")
    return band_y, position, length


def run_retrying(binary: str, after_typing: bytes, cfg: str, attempts: int = 2, extra=()):
    """A run from `run`, retried once over a cold server's slow first answer."""
    last = None
    for attempt in range(attempts):
        last = run(binary,
                   after_typing,
                   cfg if attempt == 0 else cfg + "_retry",
                   extra=extra)
        if popup_or_none(last) is not None:
            return last
    return last


def run(binary: str, after_typing: bytes, cfg: str, extra=()):
    work = "/tmp/jot_completion_nav_probe"
    os.makedirs(work, exist_ok=True)
    path = os.path.join(work, "probe.cpp")
    with open(path, "w") as fh:
        fh.write(SOURCE)
    # Alt+Shift+G -> end of file, Up twice -> the blank body line, End -> its end.
    # Then type the prefix and, once the popup has landed, the extra keys. The
    # gap before the extra keys is generous on purpose: a clangd whose index is
    # cold answers noticeably later, and the keys have to arrive while the popup
    # is up (they are what is being measured).
    return run_in_pty(binary,
                      [path],
                      b"\x1bG\x1b[A\x1b[A\x1b[F",
                      settle=8.0,
                      after=4.0,
                      cols=110,
                      rows=32,
                      cfg=cfg,
                      phases=[
                          (0.5, b"incl"),
                          (6.0, after_typing),
                      ] + list(extra))


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    if not os.path.exists(binary):
        print(f"completion nav probe: SKIP - no binary at {binary}")
        return 2
    if shutil.which("clangd") is None:
        print("completion nav probe: SKIP - clangd not installed")
        return 2

    typed = run_retrying(binary, b"", "/tmp/jot_completion_nav_probe_cfg_a")
    found = popup_or_none(typed)
    if found is None:
        print("completion nav probe: FAIL - the completion popup never opened")
        return 1
    band0, position0, length = found
    print(f"popup after typing: row {band0}, position {position0}/{length}")
    if position0 != 1:
        print("completion nav probe: FAIL - the popup did not open on the first row")
        return 1

    down = run_retrying(binary, b"\x1b[B\x1b[B", "/tmp/jot_completion_nav_probe_cfg_b")
    band2, position2, length2 = popup_position(down)
    print(f"popup after two Downs: row {band2}, position {position2}/{length2}")
    if (position2, length2) != (position0 + 2, length):
        print("completion nav probe: FAIL - Down did not move the selection "
              f"(was {position0}/{length}, now {position2}/{length2})")
        return 1
    if band2 != band0 + 2:
        print(f"completion nav probe: FAIL - the selection band is at row {band2}, "
              f"not two rows below {band0}")
        return 1

    # Up, from the same place: back to the first row.
    up = run_retrying(binary, b"\x1b[B\x1b[B\x1b[A\x1b[A", "/tmp/jot_completion_nav_probe_cfg_c")
    band_up, position_up, length_up = popup_position(up)
    print(f"popup after two Downs and two Ups: row {band_up}, position {position_up}/{length_up}")
    if (position_up, length_up) != (position0, length):
        print("completion nav probe: FAIL - Up did not walk the selection back")
        return 1
    if band_up != band0:
        print(f"completion nav probe: FAIL - the selection band ended at row {band_up}, "
              f"not {band0}")
        return 1

    # The wheel over the popup's own box: a notch walks three rows (the step the
    # palette and the quick pick take) and the popup stays up. The aim comes from
    # the box the first scene painted -- a wheel is a pointer event, so its
    # coordinates are part of the case -- and SGR reports the cell 1-based. The
    # notch is sent twice because a cold clangd can answer after the first one,
    # which then lands on the code instead: the walk is asserted to be one or two
    # notches, not exactly one.
    box_top, box_left, _right, _bottom = popup_box(typed)
    wheel = f"\x1b[<65;{box_left + 5};{box_top + 3}M".encode()
    scrolled = run_retrying(binary,
                            wheel,
                            "/tmp/jot_completion_nav_probe_cfg_d",
                            extra=[(5.0, wheel)])
    found = popup_or_none(scrolled)
    if found is None:
        print("completion nav probe: FAIL - a wheel over the popup dismissed it "
              "instead of walking the list")
        return 1
    band_wheel, position_wheel, length_wheel = found
    print(f"popup after wheel notches: row {band_wheel}, "
          f"position {position_wheel}/{length_wheel}")
    one_notch = (position0 + 3, length)
    two_notches = (position0 + 6, length)
    if (position_wheel, length_wheel) not in (one_notch, two_notches):
        print("completion nav probe: FAIL - the wheel over the popup did not walk "
              f"the selection (was {position0}/{length}, now {position_wheel}/{length_wheel})")
        return 1
    box_top_wheel = popup_box(scrolled)[0]
    if band_wheel != box_top_wheel + position_wheel:
        print(f"completion nav probe: FAIL - the selection band is at row {band_wheel}, "
              f"not the row of position {position_wheel} in the box at {box_top_wheel + 1}")
        return 1

    # A press on a row takes it, the way a click in the palette's list does: the
    # item's text lands where the typed word was, and the popup goes away. Sent
    # twice for the same reason as the notch, over two runs: a press that arrives
    # before the popup only moves the caret, and the second one then lands on a
    # live popup.
    click = (f"\x1b[<0;{box_left + 5};{box_top + 3}M"
             f"\x1b[<0;{box_left + 5};{box_top + 3}m").encode()
    for attempt in range(2):
        clicked = run(binary,
                      click,
                      f"/tmp/jot_completion_nav_probe_cfg_e{attempt}",
                      extra=[(5.0, click)])
        if popup_or_none(clicked) is not None:
            continue  # the presses never caught a live popup: try again
        line = next((row for row in rows(clicked) if "int main()" in row), "")
        if "includes" not in line:
            print("completion nav probe: FAIL - the click dismissed the popup "
                  f"without taking the row (the line reads {line.strip()!r})")
            return 1
        print(f"popup after a click on a row: gone, line {line.strip()!r}")
        break
    else:
        print("completion nav probe: FAIL - the click left the popup up")
        return 1

    print("completion nav probe: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
