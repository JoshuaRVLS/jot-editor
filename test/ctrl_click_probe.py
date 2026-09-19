#!/usr/bin/env python3
"""Probe: Ctrl+click jumps to the part of a chain the pointer is on.

A member access `a.b`, an arrow `a->b` and a qualified name `a::b` are two (or
three) symbols in one run of characters. clangd resolves each separately -- the
receiver on its own name, the member on the dot, the arrow or the member name,
the namespace on `outer`, the function on `thing` -- so the editor's job is to
ask about the element the pointer is over rather than the pointer's raw cell.

This drives the real binary and a real clangd over a pty, because the position
comes from the rendered grid (gutter, inlay hints, horizontal scroll), which no
unit test covers end to end. The one-character receiver is the sharp case: there
is no room to be a cell off before the answer belongs to the member instead.

Each scene opens the file, ctrl+clicks one cell, and reads the caret back out of
the status line's `line:column` indicator -- the landing message is transient,
but the caret the jump left behind stays on screen.

Usage: test/ctrl_click_probe.py [path-to-jot] [--dump]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary or no clangd).
"""
from __future__ import annotations

import os
import re
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

MAIN = """struct Counter {
  int stored = 0;
};

namespace outer {
int thing();
};

struct Ptr {
  int field = 0;
};

int add(int value, int other) {
  return value + other;
}

int hinted() {
  Counter boxed;
  return add(boxed.stored, 1);
}

int one() {
  Counter c;
  return c.stored;
}

int use() {
  Counter counter;
  return counter.stored;
}

int call() {
  return outer::thing();
}

int deref(Ptr *p) {
  return p->field;
}
"""

# label: (text on screen, character offset into it, caret it must land on)
# The caret is 1-based `line:column`, as the status line reports it.
SCENES = {
    "receiver":     ("counter.stored", 0, (28, 11)),
    "member":       ("counter.stored", 8, (2, 7)),
    "one-char":     ("c.stored", 0, (23, 11)),
    "one-char mem": ("c.stored", 2, (2, 7)),
    "qualifier":    ("outer::thing", 0, (5, 11)),
    "scope colon":  ("outer::thing", 5, (5, 11)),
    "qualified":    ("outer::thing", 7, (6, 5)),
    "arrow left":   ("p->field", 1, (10, 7)),
    "arrow right":  ("p->field", 2, (10, 7)),
    "arrow member": ("p->field", 3, (10, 7)),
    # A parameter hint (`value:` from clangd) sits in front of the argument, so
    # the receiver's cells are shifted right of its logical column: the click
    # mapping has to subtract the hint, or this asks about the member instead.
    "hinted recv":  ("boxed.stored", 0, (18, 11), "value:"),
    "hinted member": ("boxed.stored", 6, (2, 7), "value:"),
}

# label: (text on screen, character offset, the only text the Ctrl+hover
# underline may cover on that line). One underline across the whole run is the
# bug this pins: the affordance has to promise the jump the click will make.
UNDERLINES = {
    "receiver":  ("counter.stored", 0, "counter"),
    "member":    ("counter.stored", 8, "stored"),
    "dot":       ("counter.stored", 7, "stored"),
    "one-char":  ("c.stored", 0, "c"),
    "one-char m": ("c.stored", 2, "stored"),
    "qualifier": ("outer::thing", 0, "outer"),
    "scope colon": ("outer::thing", 5, "outer"),
    "qualified": ("outer::thing", 7, "thing"),
    "arrow":     ("p->field", 1, "field"),
    "arrow name": ("p->field", 3, "field"),
    "hinted":    ("boxed.stored", 0, "boxed"),
}


def write_workspace(root: str) -> None:
    shutil.rmtree(root, ignore_errors=True)
    os.makedirs(root, exist_ok=True)
    with open(os.path.join(root, "main.cpp"), "w") as fh:
        fh.write(MAIN)
    # So clangd parses as C++17 with no compile database to find. -xc++ is not
    # optional: without it clangd falls back to treating a .cpp as C.
    with open(os.path.join(root, "compile_flags.txt"), "w") as fh:
        fh.write("-xc++\n-std=c++17\n")


def ctrl_click(col: int, row: int) -> bytes:
    """SGR ctrl+left press and release on a 0-based screen cell.

    The modifier bits live in the button code: 16 is Ctrl and a left button is
    0, so 16 is a ctrl+left press. Whether the editor asked the terminal for
    mouse reporting does not matter here -- it parses whatever arrives.
    """
    press = b"\x1b[<16;%d;%dM" % (col + 1, row + 1)
    release = b"\x1b[<16;%d;%dm" % (col + 1, row + 1)
    return press + release


def ctrl_motion(col: int, row: int) -> bytes:
    """SGR ctrl+motion (button 32 is motion, +16 is Ctrl) on a screen cell.

    Sent twice so the second one lands on an editor that has already drawn the
    frame the first one caused.
    """
    return b"\x1b[<48;%d;%dM" % (col + 1, row + 1) * 2


def cell_of(screen, needle: str, offset: int = 0):
    """The 0-based (col, row) of `needle` on screen, plus a character offset."""
    for row, line in enumerate(screen.text().split("\n")):
        idx = line.find(needle)
        if idx >= 0:
            return idx + offset, row
    return None


def run(binary: str, root: str, keys: bytes, cfg: str, settle: float = 9.0,
        after: float = 5.0):
    # Tall enough for every line of the workspace file: an off-screen line gets
    # no inlay hints, which would silently skip the hinted scenes.
    return run_in_pty(binary, [os.path.join(root, "main.cpp")], keys,
                      settle=settle, after=after, cols=110, rows=44, cfg=cfg,
                      cwd=root)


def run_settled(binary: str, root: str, keys: bytes, cfg: str, needle: str,
                attempts: int = 3):
    """Run a scene, retrying while the last frame came out cut short.

    The editor writes a frame in one write() and this probe kills the process to
    end a scene, so a pty that could not take the whole frame leaves the last
    one's tail unwritten -- a partially redrawn row. The scene is only valid
    when the row it is about reconstructed, so retry until it did.
    """
    screen = run(binary, root, keys, cfg)
    for _ in range(attempts - 1):
        if needle in screen.text():
            return screen
        screen = run(binary, root, keys, cfg)
    return screen


def caret(screen):
    """The (line, column) the status line reports, 1-based, or None."""
    for line in screen.text().split("\n"):
        if " @ " not in line:
            continue
        match = re.search(r"\s(\d+):(\d+)\s", line)
        if match:
            return int(match.group(1)), int(match.group(2))
    return None


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    dump = "--dump" in sys.argv
    if not os.path.exists(binary):
        print(f"ctrl click probe: SKIP - no binary at {binary}")
        return 2
    if not shutil.which("clangd"):
        print("ctrl click probe: SKIP - no clangd on PATH")
        return 2

    root = "/tmp/jot_ctrl_click_probe"
    write_workspace(root)

    # First run maps the rendered grid: where each symbol actually sits on
    # screen (the sidebar width decides the code pane's left edge, so this
    # cannot be assumed). The longer wait is for clangd's inlay hints, which
    # arrive after the diagnostics and are part of that grid.
    #
    # Retried, because the editor writes a frame in one write() and this run is
    # killed mid-session: a pty that could not take the whole frame leaves the
    # last one's tail unwritten (a partially redrawn row), which would make a
    # scene look like it can't find its symbol. The grid that matters is the one
    # where every needle reconstructed.
    needles = ("counter.stored", "c.stored", "outer::thing", "p->field", "boxed.stored")
    base = None
    for attempt in range(3):
        base = run(binary, root, b"",
                   f"/tmp/jot_ctrl_click_probe_cfg_base{attempt}",
                   settle=11.0, after=6.0)
        if all(needle in base.text() for needle in needles):
            break
    if dump:
        print(base.text())
        print("-" * 70)

    failures = []
    screen_text = base.text()
    for index, entry in enumerate(SCENES.items()):
        label, (needle, offset, want) = entry[0], entry[1][:3]
        needs = entry[1][3] if len(entry[1]) > 3 else None
        if needs and needs not in screen_text:
            print(f"ctrl+click {label:<13} SKIPPED - no {needs!r} inlay hint on "
                  f"screen")
            continue
        cell = cell_of(base, needle, offset)
        if cell is None:
            failures.append(f"{label}: {needle!r} is not visible on screen")
            continue
        col, row = cell
        cfg = f"/tmp/jot_ctrl_click_probe_cfg_{index}"
        screen = run_settled(binary, root, ctrl_click(col, row), cfg, needle)
        if dump:
            print(screen.text())
            print("-" * 70)
        got = caret(screen)
        print(f"ctrl+click {label:<13} {needle}[{offset}] at ({col},{row}) -> "
              f"caret {got if got else '(none)'} (want {want[0]}:{want[1]})")
        if not got:
            failures.append(f"{label}: the caret indicator is gone")
        elif got != want:
            failures.append(f"{label}: expected {want[0]}:{want[1]}, landed on "
                            f"{got[0]}:{got[1]}")

    # Second half: the same element choice, but read off the underline the
    # Ctrl+hover affordance draws -- the click's target and the thing it
    # promises have to be the same symbol.
    for index, (label, (needle, offset, want)) in enumerate(UNDERLINES.items()):
        cell = cell_of(base, needle, offset)
        if cell is None:
            failures.append(f"underline {label}: {needle!r} is not visible")
            continue
        col, row = cell
        cfg = f"/tmp/jot_ctrl_click_probe_hover_{index}"
        screen = run_settled(binary, root, ctrl_motion(col, row), cfg, needle)
        if dump:
            print(screen.text())
            print("-" * 70)
        runs = [text for _start, _end, text in screen.underline_runs(row)]
        print(f"ctrl+hover {label:<13} {needle}[{offset}] at ({col},{row}) -> "
              f"underlines {runs} (want ['{want}'])")
        if runs != [want]:
            failures.append(f"underline {label}: expected exactly ['{want}'], "
                            f"got {runs}")

    if failures:
        for failure in failures:
            print(f"ctrl click probe: FAIL - {failure}")
        return 1
    print("ctrl click probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
