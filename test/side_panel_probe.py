#!/usr/bin/env python3
"""Probe: the right dock's tab strips are readable in the shipped themes.

The panel tab strip and the git/debugger view tabs are drawn by the bundled Lua
side panel, which reads its colours from the editor. A fallback that pairs one
theme slot with another can land two equal colours in one cell (a scheme that
inks its keyword accent and its selection background with one colour does),
which paints the active tab in its own background: the text is on the grid and
invisible. Checked the way a reader would see it: every inked cell in the dock
has to carry a foreground that differs from its background.

One scene: a git repository with one modified file, the git panel opened through
the palette so both strips (the panel tabs and the view tabs) are on screen.

Usage: test/side_panel_probe.py [path-to-jot] [--dump]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary or no git).
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

COLS, ROWS = 130, 36
PANEL_W = 42  # the default right_panel_width
ROOT = "/tmp/jot_side_panel_probe"
CFG = "/tmp/jot_side_panel_probe_cfg"
SOURCE = os.path.join(ROOT, "sample.cpp")
PALETTE = b"\x1b[112;6u"
ENTER = b"\r"


def write_workspace() -> None:
    shutil.rmtree(ROOT, ignore_errors=True)
    shutil.rmtree(CFG, ignore_errors=True)
    os.makedirs(ROOT)
    with open(SOURCE, "w") as fh:
        fh.write("int apples = 1;\nint bananas = 2;\nint cherries = 3;\n")
    subprocess.run(["git", "init", "-q"], cwd=ROOT, check=True)
    subprocess.run(["git", "add", "-A"], cwd=ROOT, check=True)
    subprocess.run(["git", "-c", "user.email=probe@local", "-c", "user.name=probe",
                    "commit", "-qm", "init"], cwd=ROOT, check=True)
    # One modified file, so the panel has a row (and so a selected row) to draw.
    with open(SOURCE, "a") as fh:
        fh.write("int grapes = 7;\n")


def colour(v: int) -> str:
    if v < 0:
        return "default"
    if v >= 1000:
        rgb = v - 1000
        return "#%02x%02x%02x" % ((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255)
    return str(v)


def invisible_cells(screen) -> list[str]:
    """Inked cells in the dock's columns whose foreground is their background."""
    first_col = COLS - PANEL_W
    found: list[str] = []
    lines = screen.text().split("\n")
    for y in range(1, min(ROWS - 1, len(lines) - 1)):
        line = lines[y]
        for x in range(first_col, min(COLS, len(line))):
            if line[x] == " ":
                continue
            fg = screen.fg[y][x]
            bg = screen.bg[y][x]
            if fg >= 0 and fg == bg:
                found.append("row %d col %d %r fg=bg=%s" % (y, x, line[x], colour(fg)))
    return found


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    dump = "--dump" in sys.argv
    if not os.path.exists(binary):
        print("side panel probe: SKIP - no binary at %s" % binary)
        return 2
    if shutil.which("git") is None:
        print("side panel probe: SKIP - git is not on PATH")
        return 2

    write_workspace()
    os.environ.setdefault("GIT_CONFIG_GLOBAL", "/dev/null")
    # A plain shell and an empty HOME, so nothing a startup file prints lands in
    # the dock or moves the shell.
    home = "/tmp/jot_side_panel_probe_home"
    shutil.rmtree(home, ignore_errors=True)
    os.makedirs(home)
    os.environ["HOME"] = home
    if os.path.exists("/bin/bash"):
        os.environ["SHELL"] = "/bin/bash"

    screen = run_in_pty(binary, [SOURCE], b"", settle=5.0, after=0.8, cols=COLS, rows=ROWS,
                        cfg=CFG, cwd=ROOT,
                        phases=[(0.6, PALETTE), (0.8, b"gitpanel"), (0.6, ENTER),
                                # The view tabs are the panel's own proof that it
                                # is up: waiting on them, not on a timer.
                                (1.2, lambda s: "Branches" in s.text())])
    if dump:
        print(screen.text())
        print("-" * 70)

    failures: list[str] = []
    text = screen.text()
    if "Branches" not in text or "Files" not in text:
        failures.append("the git panel's view tabs are not on screen")
    invisible = invisible_cells(screen)
    if invisible:
        failures.append("%d dock cell(s) are painted in their own background colour: %s"
                        % (len(invisible), "; ".join(invisible[:6])))

    if failures:
        print("side panel probe: FAIL")
        for failure in failures:
            print("  - " + failure)
        return 1
    print("side panel probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
