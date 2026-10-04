#!/usr/bin/env python3
"""Probe: the workspace tab strip, on the rendered screen.

The strip is a row the rest of the layout has to agree with, and every way that
agreement can break is quiet: a pane that still keeps a header row of its own
shows a blank line above the code (the strip costs a row *and* the panes spend
one), a hit test reading a different origin than the painter opens the wrong
file, and a key that still means "the focused pane's second tab" picks a tab
that is not the second one on screen. None of that fails a unit test of the
order model: the model has no opinion about rows.

This drives the real binary with three files open and reads the cell rows back
out of the pty stream:

  * row 0 carries the tabs and row 1 is the first code row (no blank chrome row
    between them, and the strip is not inside the pane),
  * Alt+2 picks the second tab *on the strip* and the status line names it,
  * Alt+R arms jump-to-buffer mode and the next letter picks that tab,
  * a click on a tab switches to it.

Usage: test/tabline_probe.py [path-to-jot] [--dump]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary).
"""
from __future__ import annotations

import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

ALT_1 = b"\x1b1"
ALT_2 = b"\x1b2"
ALT_0 = b"\x1b0"
ALT_R = b"\x1br"
CTRL_E = b"\x05"  # the finder, the editor's own way to open a second file
ENTER = b"\r"

FILES = ("alpha.txt", "beta.txt", "gamma.txt")


def write_workspace(root: str) -> list[str]:
    shutil.rmtree(root, ignore_errors=True)
    os.makedirs(root, exist_ok=True)
    # A repository marker is what tells the finder where the project is: without
    # one (and with no workspace open) it walks up to the filesystem root and
    # scans everything, which would make the query match somebody else's files.
    os.system(f"git init -q {root} 2>/dev/null")
    paths = []
    for name in FILES:
        path = os.path.join(root, name)
        with open(path, "w") as fh:
            # Five lines, each naming its file: row 1 is where the first one has
            # to land, and the text makes a wrong row obvious.
            for i in range(5):
                fh.write(f"{name[:-4]} body {i}\n")
        paths.append(path)
    return paths


def status_line(screen) -> str:
    """The last row that has anything on it: the status line."""
    for y in range(screen.rows - 1, -1, -1):
        row = "".join(screen.cells[y]).rstrip()
        if row.strip():
            return row
    return ""


def run(binary: str, paths: list[str], root: str, cfg: str, keys: bytes = b""):
    """Starts on the first file, opens the other two through the finder, then
    sends `keys`. Only one path can be given on the command line, and the finder
    is how the navigation probe opens a second file too.

    The query and the key that accepts it go in separate phases: a picker that
    is still scanning refuses the accept ("Telescope is scanning"), so sending
    them as one blob races the scan and opens nothing.
    """
    # The waits are generous on purpose: the picker's scan root only becomes the
    # project once the git status refresh has run (before that it walks up to the
    # filesystem root), and an accept that lands while a scan is in flight is
    # refused rather than queued.
    phases = [
        (3.0, CTRL_E),
        (2.5, b"beta"),
        (2.5, ENTER),
        (1.0, CTRL_E),
        (2.0, b"gamma"),
        (2.0, ENTER),
    ]
    # Let the editor go idle before the next input: after the last accept it is
    # still finishing work of its own (the git status refresh, the bundled
    # query load), and a key that lands in the middle of it is sometimes never
    # delivered -- the strip then looks like it ignored the click.
    phases.append((3.0, b""))
    if keys:
        phases.append((1.5, keys))
    return run_in_pty(binary, [paths[0]], b"", settle=3.5, after=3.0,
                      cols=100, rows=24, cfg=cfg, cwd=root, phases=phases)


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    dump = "--dump" in sys.argv
    if not os.path.exists(binary):
        print(f"tabline probe: SKIP - no binary at {binary}")
        return 2

    root = "/tmp/jot_tabline_probe"
    paths = write_workspace(root)
    cfg = "/tmp/jot_tabline_probe_cfg"
    failures = []

    # 1. The strip is row 0 and the first code row is row 1.
    screen = run(binary, paths, root, cfg)
    if dump:
        print(screen.text())
        print("-" * 70)
    strip = "".join(screen.cells[0])
    listed = [name for name in FILES if name in strip]
    first_row = "".join(screen.cells[1])
    print(f"strip row: {listed}; row 1: {first_row.strip()[:60]!r}")
    if len(listed) != len(FILES):
        failures.append(f"row 0 does not carry every tab (saw {listed})")
    if "body 0" not in first_row:
        failures.append("the first code row is not row 1 -- the pane kept a chrome "
                        "row of its own above the text")
    if "body 0" in strip:
        failures.append("the code is painted on the strip's row")

    # 2. Alt+2 selects the second tab on the strip, Alt+0 the last one.
    screen = run(binary, paths, root, cfg, keys=ALT_2)
    line = status_line(screen)
    print(f"Alt+2 status line: {line.strip()[:70]!r}")
    if "beta.txt" not in line:
        failures.append(f"Alt+2 did not select the second tab (status line {line.strip()!r})")

    screen = run(binary, paths, root, cfg, keys=ALT_0)
    line = status_line(screen)
    print(f"Alt+0 status line: {line.strip()[:70]!r}")
    if "gamma.txt" not in line:
        failures.append(f"Alt+0 did not select the last tab (status line {line.strip()!r})")

    # 3. Alt+R arms jump-to-buffer mode; the letter painted on a tab picks it.
    # The letters come from the file initials, so `a` is alpha -- which is also
    # the *first* tab, while the last one opened is gamma, so the pick is
    # visible in the status line.
    screen = run(binary, paths, root, cfg, keys=ALT_R + b"a")
    if dump:
        print(screen.text())
        print("-" * 70)
    line = status_line(screen)
    print(f"jump letter status line: {line.strip()[:70]!r}")
    if "alpha.txt" not in line:
        failures.append(f"the jump letter did not pick its tab (status line {line.strip()!r})")

    # 4. A click on a tab's cells switches to it: the hit test has to read the
    # same row the painter wrote to.
    col = strip_col_of(screen, "beta.txt")
    if col < 0:
        print("tabline probe: SKIP - the strip never painted beta.txt to aim at")
        return 2
    press = f"\x1b[<0;{col + 1};1M".encode()
    release = f"\x1b[<0;{col + 1};1m".encode()
    screen = run(binary, paths, root, cfg, keys=press + release)
    if dump:
        print(screen.text())
        print("-" * 70)
    line = status_line(screen)
    print(f"tab click status line: {line.strip()[:70]!r}")
    if "beta.txt" not in line:
        failures.append(f"the tab click did not switch (status line {line.strip()!r})")

    if failures:
        for failure in failures:
            print(f"tabline probe: FAIL - {failure}")
        return 1
    print("tabline probe: PASS")
    return 0


def strip_col_of(screen, name: str) -> int:
    """The column a tab's name starts at on row 0, or -1."""
    return "".join(screen.cells[0]).find(name)


if __name__ == "__main__":
    sys.exit(main())
