#!/usr/bin/env python3
"""Probe: the telescope paints no background of its own across its rows.

The result rows used to be filled with the theme's Telescope slots --
TelescopeNormal for every row, TelescopeSelection for the selected one, and
TelescopePreviewNormal for the preview -- so opening the telescope put a slab of
the theme's list background across the panel (a white one on the light themes:
space-light is 255 for the list, 249 selected, 15 preview). The rows now paint
no background at all and the selection is a caret.

Matching "no cell uses index 255" would not work: in a light theme the editor's
own background is one of those light indices too, so the check has to be
relative. It compares the background of a *selected* list row against an
unselected one (a band would show up as a difference) and a list row against a
preview row (a separate fill would too). Against the old renderer both differ,
which is the regression this guards.

Text is checked in the same pass: the key-hint footer the telescope used to spell
out must not be on screen either.

Usage: test/telescope_theme_probe.py [path-to-jot]
Exit codes: 0 pass, 1 fail, 2 binary missing.
"""
from __future__ import annotations

import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

THEME = "space-light"
# Key hints the surfaces used to spell out. The probe workspace files are named so
# these strings cannot arrive as content by accident.
STALE_HINTS = ["Enter open", "Esc close", "↑/↓ move", "type to filter files",
               "Up/Down move", "PgUp/PgDn", "Up/Down scroll"]


def slot_backgrounds(theme_path: str) -> dict:
    """Background indices the Telescope* highlight groups map to."""
    data = json.load(open(theme_path))
    out = {}
    for key, value in data.items():
        if key.startswith("Telescope") and isinstance(value, dict) and "bg" in value:
            out[key] = value["bg"]
    return out


def find_row(screen, needle: str):
    """(row, column) of the first cell of `needle`, or None."""
    for y in range(screen.rows):
        row = "".join(screen.cells[y])
        x = row.find(needle)
        if x >= 0:
            return y, x
    return None


def float_interior(screen):
    """(top, bottom, left, right) rows/columns strictly inside the panel border."""
    top = bottom = left = right = None
    for y in range(screen.rows):
        row = "".join(screen.cells[y])
        if top is None and "┌" in row:
            top = y
            left = row.index("┌")
            right = row.rindex("┐") if "┐" in row else None
        if "└" in row:
            bottom = y
    if top is None or bottom is None or left is None or right is None:
        return None
    return top + 1, bottom, left + 1, right


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    if not os.path.exists(binary):
        print(f"telescope theme probe: SKIP - no binary at {binary}")
        return 2

    theme_path = os.path.join(".configs", "configs", "colors", f"{THEME}.json")
    if not os.path.exists(theme_path):
        print(f"telescope theme probe: SKIP - no theme at {theme_path}")
        return 2
    slots = slot_backgrounds(theme_path)
    if not slots:
        print("telescope theme probe: SKIP - theme has no Telescope backgrounds")
        return 2

    work = "/tmp/jot_telescope_probe"
    os.makedirs(work, exist_ok=True)
    for name in ("alpha.c", "beta.lua", "gamma.py"):
        with open(os.path.join(work, name), "w") as fh:
            fh.write("-- probe\n")
    # A marker (a real repo, so the git panel has nothing to complain about) makes
    # the probe directory the detected root; without one the telescope walks up
    # to "/" and lists the filesystem instead of these files.
    if not os.path.isdir(os.path.join(work, ".git")):
        subprocess.run(["git", "init", "-q", work], check=False)

    # The theme is applied from the config the probe owns, so the user's own
    # settings are never touched. The key is `color_scheme` (read at startup as
    # well as by `:theme`); `theme` would be silently ignored.
    cfg = "/tmp/jot_telescope_probe_cfg"
    os.makedirs(os.path.join(cfg, "configs"), exist_ok=True)
    with open(os.path.join(cfg, "configs", "settings.conf"), "w") as fh:
        fh.write(f"color_scheme={THEME}\n")

    screen = run_in_pty(binary, [os.path.join(work, "alpha.c")], b"\x05",
                        settle=3.0, after=4.0, cols=110, rows=32, cfg=cfg, cwd=work)
    view = screen.text()

    box = float_interior(screen)
    listed = find_row(screen, "alpha.c") and find_row(screen, "beta.lua")
    preview = find_row(screen, "Preview")
    if not (box and listed and preview):
        print("telescope theme probe: FAIL - the telescope never opened (Ctrl+E)")
        print(view)
        return 1

    top, bottom, left, right = box
    print(f"telescope theme probe: theme {THEME}, slot backgrounds {slots}")
    print(f"telescope theme probe: panel interior rows {top}..{bottom}, "
          f"cols {left}..{right}")

    failures = 0

    # The body must be one flat background. The query row is the one exception:
    # it keeps the command-bar fill while focused, so it is skipped. The old
    # renderer filled the list with TelescopeNormal, the selected row with
    # TelescopeSelection and the preview with TelescopePreviewNormal, which is
    # three different values here (255 / 249 / 15).
    seen: dict = {}
    for y in range(top, bottom):
        row = "".join(screen.cells[y])
        if "→" in row:
            continue
        for x in range(left, right):
            bg = screen.bg[y][x]
            seen[bg] = seen.get(bg, 0) + 1
    print(f"telescope theme probe: body backgrounds: {dict(sorted(seen.items()))}")
    if len(seen) > 1:
        failures += 1
        print(f"telescope theme probe: FAIL - the panel body is not one flat "
              f"background: {dict(sorted(seen.items()))}")

    stale = [h for h in STALE_HINTS if h in view]
    for hint in stale:
        failures += 1
        print(f"telescope theme probe: FAIL - stale hint text on screen: {hint!r}")

    if failures:
        print("telescope theme probe: FAIL")
        return 1
    print("telescope theme probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
