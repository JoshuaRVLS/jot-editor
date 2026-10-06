#!/usr/bin/env python3
"""Probe: typing and pasting keep the caret on screen.

A caret walked past the pane's right edge, or a block that landed below the
fold, used to leave the viewport behind: insert_char and the paste / insert-text
paths moved the caret without the reveal every other edit path runs, so the rest
of a long line was typed off screen and a pasted block ended up past the bottom.
The unit cases pin both through the editor's own render; what only a terminal can
show is whether the caret's own cell is in the pane, so this drives the real
binary in a pty:

  * a long line is typed with a marker at each end: the tail has to be on screen
    with the head gone, which is the viewport having followed the caret, and the
    caret's line has to be one of the lines the gutter shows;
  * a thirty-line block is pasted into an empty file: its last row has to be on
    screen, and the statusline's cursor line has to be among the visible ones.

Usage: test/typing_scroll_probe.py [path-to-jot]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary).
"""
from __future__ import annotations

import os
import re
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

COLS, ROWS = 100, 24
# The startup Tree-sitter toast covers rows for a few seconds.
SETTLE_S = 8.0
TYPE_AFTER_S = 0.4
HEAD_MARK = "HEADMARK"
TAIL_MARK = "TAILMARK"
TYPE_AWAY = 160
PASTE_LINES = 30

# A pane row opens with its absolute line number in the gutter.
GUTTER_RE = re.compile(r"^\s*(\d+)\s")
# The statusline's `line:col` chip. The clock's `hh:mm` reads the same way but
# sits further right, so the first match on the row is the caret's.
CURSOR_RE = re.compile(r"\s(\d+):(\d+)\s")


def write_workspace(root: str) -> tuple[str, str]:
    shutil.rmtree(root, ignore_errors=True)
    os.makedirs(root)
    typed = os.path.join(root, "typed.txt")
    with open(typed, "w") as fh:
        fh.write("start\n")
    pasted = os.path.join(root, "pasted.txt")
    open(pasted, "w").close()
    return typed, pasted


def pane_lines(screen) -> set[int]:
    """The buffer lines the gutter puts on screen."""
    rows = screen.text().split("\n")
    return {int(m.group(1)) for row in rows if (m := GUTTER_RE.match(row))}


def caret_line(screen) -> int | None:
    """The line the statusline reports the caret on, or None if it is not there."""
    status = screen.text().split("\n")[-1]
    m = CURSOR_RE.search(status)
    return int(m.group(1)) if m else None


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    if not os.path.exists(binary):
        print(f"typing scroll probe: SKIP - no binary at {binary}")
        return 2

    ws = "/tmp/jot_typing_scroll_work"
    cfg = "/tmp/jot_typing_scroll_cfg"
    typed, pasted = write_workspace(ws)
    binary = os.path.abspath(binary)

    failures = 0

    def check(name, ok, detail):
        nonlocal failures
        if not ok:
            failures += 1
        print(f"typing scroll probe: {name} {'ok' if ok else 'FAIL'} - {detail}")

    # Scene 1: type a line much wider than the pane, with a marker at each end.
    # Following the caret puts the tail on screen and takes the head off it.
    screen = run_in_pty(binary, [typed], b"", settle=SETTLE_S, after=1.0, cols=COLS, rows=ROWS,
                        cfg=cfg, cwd=ws,
                        phases=[(TYPE_AFTER_S, (HEAD_MARK + "a" * TYPE_AWAY
                                                + TAIL_MARK).encode())])
    text = screen.text()
    check("the typed tail is on screen", TAIL_MARK in text,
          "the viewport followed the caret" if TAIL_MARK in text
          else f"only the head of the line is visible: {text.splitlines()[-2][:60]!r}")
    check("the typed head scrolled off", HEAD_MARK not in text,
          "the line panned" if HEAD_MARK not in text else "the viewport never moved")
    line = caret_line(screen)
    check("the caret's line is drawn", line is not None and line in pane_lines(screen),
          f"caret line {line}, gutter shows {sorted(pane_lines(screen))[:6]}")

    # Scene 2: paste a block taller than the pane into an empty file. Its last
    # row and the caret on it have to be on screen, not the first screenful.
    body = "\n".join(f"row {i}" for i in range(1, PASTE_LINES + 1))
    keys = b"\x1b[200~" + body.encode() + b"\x1b[201~"
    screen = run_in_pty(binary, [pasted], b"", settle=SETTLE_S, after=1.0, cols=COLS, rows=ROWS,
                        cfg=cfg, cwd=ws, phases=[(TYPE_AFTER_S, keys)])
    text = screen.text()
    rows = [row.strip() for row in text.split("\n")]
    check("the pasted block's last row is on screen", any(row.endswith(f"row {PASTE_LINES}") for row in rows),
          "the viewport followed the paste"
          if any(row.endswith(f"row {PASTE_LINES}") for row in rows)
          else f"visible rows end at {rows[-2][:40]!r}")
    line = caret_line(screen)
    check("the caret's line is drawn", line is not None and line in pane_lines(screen),
          f"caret line {line}, gutter shows {sorted(pane_lines(screen))[:6]}")

    print(f"typing scroll probe: {'PASS' if failures == 0 else 'FAIL'}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
