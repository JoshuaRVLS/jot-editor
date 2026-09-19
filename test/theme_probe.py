#!/usr/bin/env python3
"""Probe: jot's own colour schemes, on the rendered screen.

The bundled themes are files the editor looks up by name, and every way that
lookup can fail is quiet: a theme name that no longer resolves leaves the editor
on its built-in ANSI defaults (background 0) with no error in a non-announcing
startup, and a catalog that was supposed to shrink can quietly grow back. Neither
shows up in a unit test of the palette -- the file is copied into a temp config
dir there -- so this drives the real binary with only the shipped themes on disk
and reads the cell backgrounds back out of the pty stream.

Scenes: the default scheme (jot-dark), jot-light, the legacy `dark` name, and the
theme chooser's list.

Usage: test/theme_probe.py [path-to-jot] [--dump]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary).
"""
from __future__ import annotations

import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

PALETTE = b"\x1b[112;6u"  # Ctrl+Shift+P

# The backgrounds the bundled themes set for the editor body and the status line.
JOT_DARK_BODY = 234
JOT_DARK_STATUS = 236
JOT_LIGHT_BODY = 230
JOT_LIGHT_STATUS = 253
# What an editor with no theme applied paints (the built-in ANSI default).
UNTHEMED_BODY = 0


def write_workspace(root: str) -> str:
    shutil.rmtree(root, ignore_errors=True)
    os.makedirs(root, exist_ok=True)
    path = os.path.join(root, "notes.txt")
    # Few lines on purpose: the filler rows below the buffer are the easiest
    # place to read the editor's own background out of.
    with open(path, "w") as fh:
        for i in range(5):
            fh.write(f"line {i}\n")
    return path


def write_config(cfg: str, scheme: str) -> None:
    os.makedirs(os.path.join(cfg, "configs"), exist_ok=True)
    with open(os.path.join(cfg, "configs", "settings.conf"), "w") as fh:
        fh.write(f"color_scheme={scheme}\n")


def body_background(screen) -> int:
    """The background of the editor body, read from a filler row."""
    for y in range(screen.rows):
        row = "".join(screen.cells[y])
        if row.strip().startswith("~"):
            x = row.index("~")
            return screen.bg[y][x]
    return -1


def status_background(screen) -> int:
    """The background of the status line, found by its memory chip."""
    for y in range(screen.rows):
        row = "".join(screen.cells[y])
        if "mem " in row:
            return screen.bg[y][row.index("mem ")]
    return -1


def run(binary: str, path: str, root: str, cfg: str, keys: bytes = b""):
    return run_in_pty(binary, [path], keys, settle=3.0, after=3.0,
                      cols=100, rows=30, cfg=cfg, cwd=root)


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    dump = "--dump" in sys.argv
    if not os.path.exists(binary):
        print(f"theme probe: SKIP - no binary at {binary}")
        return 2

    root = "/tmp/jot_theme_probe"
    path = write_workspace(root)
    # The data directory is where an install keeps its copy of the bundled
    # themes, and an install that predates this change still has the whole old
    # catalog there. Point it at a scratch dir so the probe measures the themes
    # this build ships, not whatever a previous install left behind.
    os.environ["JOT_DATA_HOME"] = "/tmp/jot_theme_probe_data"
    failures = []

    scenes = [
        ("default", None, JOT_DARK_BODY, JOT_DARK_STATUS),
        ("jot-light", "jot-light", JOT_LIGHT_BODY, JOT_LIGHT_STATUS),
        ("jot-dark", "jot-dark", JOT_DARK_BODY, JOT_DARK_STATUS),
        # The name the removed catalog used: it must still resolve, and to the
        # jot theme that replaced it rather than to the ANSI fallback.
        ("dark (legacy name)", "dark", JOT_DARK_BODY, JOT_DARK_STATUS),
        ("light (legacy name)", "light", JOT_LIGHT_BODY, JOT_LIGHT_STATUS),
    ]
    for index, (label, scheme, want_body, want_status) in enumerate(scenes):
        cfg = f"/tmp/jot_theme_probe_cfg_{index}"
        if scheme is not None:
            write_config(cfg, scheme)
        screen = run(binary, path, root, cfg)
        if dump:
            print(screen.text())
            print("-" * 70)
        body = body_background(screen)
        status = status_background(screen)
        print(f"{label}: body bg {body} (want {want_body}), status bg {status} (want {want_status})")
        if body != want_body:
            failures.append(f"{label}: body background {body}, expected {want_body}"
                            + (" (the theme did not apply)" if body == UNTHEMED_BODY else ""))
        if status != want_status:
            failures.append(f"{label}: status background {status}, expected {want_status}")

    # The chooser offers the installed themes as argument completions of
    # `:theme`. The palette only switches to argument completion once the query
    # has an argument, so the query letter is part of the key sequence; `j`
    # matches both jot names.
    cfg = "/tmp/jot_theme_probe_cfg_chooser"
    screen = run(binary, path, root, cfg, keys=PALETTE + b"theme j")
    if dump:
        print(screen.text())
        print("-" * 70)
    view = screen.text()
    listed = [name for name in ("jot-dark", "jot-light") if name in view]
    print(f"chooser: lists {listed}")
    if len(listed) != 2:
        failures.append(f"chooser did not list both jot themes (saw {listed})")

    if failures:
        for failure in failures:
            print(f"theme probe: FAIL - {failure}")
        return 1
    print("theme probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
