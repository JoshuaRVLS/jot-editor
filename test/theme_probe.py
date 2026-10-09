#!/usr/bin/env python3
"""Probe: jot's own colour schemes, on the rendered screen.

The bundled themes are files the editor looks up by name, and every way that
lookup can fail is quiet: a theme name that no longer resolves leaves the editor
on its built-in ANSI defaults (background 0) with no error in a non-announcing
startup, and a catalog that was supposed to shrink can quietly grow back. Neither
shows up in a unit test of the palette -- the file is copied into a temp config
dir there -- so this drives the real binary with only the shipped themes on disk
and reads the cell backgrounds back out of the pty stream.

Both themes name exact 24-bit colours, so the same run also pins the depth: the
backgrounds must arrive as 48;2 and the accent (the active pane border) as 38;2.
A slot that fell back to a palette index would still paint, just not the colour
the scheme was authored with -- exactly the quiet downgrade this catches.

The same value domain carries a decoration's colours, so one scene places a
wavy underline with a hex colour through init.lua and reads the SGR 58 sequence
back out of the stream: an underline is where a quantised colour would be
hardest to notice and easiest to ship.

Scenes: the default scheme (jot-dark), jot-light, the ported flexoki pair, the
legacy `dark` name, the theme chooser's list, and a decoration's exact underline
colour.

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


# pty_screen tags a 24-bit background as 1000 + its rgb, so it cannot be
# mistaken for a palette index (see Screen._sgr).
def truecolour(rgb: int) -> int:
    return 1000 + rgb


# The backgrounds the bundled themes set for the editor body and the status
# line. Both themes name exact 24-bit colours, so what has to arrive on the wire
# is a 48;2 sequence -- the themes are the one place a palette index would be a
# silent downgrade of the whole scheme. jot-dark's ground is the amber scheme's
# #14100b, so the harness's 1000 + rgb tag is also what tells the themed cell
# from the ANSI default an unthemed editor would paint (0).
JOT_DARK_BODY = truecolour(0x14100B)
JOT_DARK_STATUS = truecolour(0x1C1710)
JOT_LIGHT_BODY = truecolour(0xFCF4E3)
JOT_LIGHT_STATUS = truecolour(0xF7ECDA)
# The ported Flexoki pair: `black` and `base-950` for the dark editor, `paper`
# and `base-50` for the light one (see .configs/configs/colors/flexoki-*.json).
FLEXOKI_DARK_BODY = truecolour(0x100F0F)
FLEXOKI_DARK_STATUS = truecolour(0x1C1B1A)
FLEXOKI_LIGHT_BODY = truecolour(0xFFFCF0)
FLEXOKI_LIGHT_STATUS = truecolour(0xF2F0E5)
# The accent each theme inks its active pane border with, as it must appear in
# the escape stream: 38;2;r;g;b, never 38;5;n.
JOT_DARK_ACCENT = b"\x1b[38;2;255;180;84m"  # #ffb454, the amber
JOT_LIGHT_ACCENT = b"\x1b[38;2;168;90;0m"  # #a85a00, burnt amber
FLEXOKI_DARK_ACCENT = b"\x1b[38;2;58;169;159m"  # #3aa99f, cyan-400
FLEXOKI_LIGHT_ACCENT = b"\x1b[38;2;35;127;119m"  # #237f77, cyan-600
# A decoration's underline colour as SGR 58's 24-bit colon form.
DECO_UNDERLINE = b"\x1b[58:2::68:204:153m"  # #44cc99
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


def write_decoration_init(cfg: str) -> None:
    """Places a wavy underline whose colour is an exact hex value.

    init.lua is how a user sets one without waiting on an LSP diagnostic, and it
    exercises the same decoration path a diagnostic would.
    """
    os.makedirs(cfg, exist_ok=True)
    with open(os.path.join(cfg, "init.lua"), "w") as fh:
        # Deferred: init.lua runs before the file on the command line is opened,
        # so placing it straight away would decorate the startup buffer instead.
        fh.write("jot.timer.set_timeout(300, function()\n"
                 "  jot.decoration.set(jot.buffer.current(),\n"
                 "    { row = 1, col = 1, width = 4, underline = 2, underline_fg = \"#44cc99\" })\n"
                 "end)\n")


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
        ("default", None, JOT_DARK_BODY, JOT_DARK_STATUS, JOT_DARK_ACCENT),
        ("jot-light", "jot-light", JOT_LIGHT_BODY, JOT_LIGHT_STATUS, JOT_LIGHT_ACCENT),
        ("jot-dark", "jot-dark", JOT_DARK_BODY, JOT_DARK_STATUS, JOT_DARK_ACCENT),
        # The name the removed catalog used: it must still resolve, and to the
        # jot theme that replaced it rather than to the ANSI fallback.
        ("dark (legacy name)", "dark", JOT_DARK_BODY, JOT_DARK_STATUS, JOT_DARK_ACCENT),
        ("light (legacy name)", "light", JOT_LIGHT_BODY, JOT_LIGHT_STATUS, JOT_LIGHT_ACCENT),
        # The ported palette is a file like any other: it has to resolve, paint
        # its own 24-bit surfaces, and ink the border with its own accent.
        ("flexoki-dark", "flexoki-dark", FLEXOKI_DARK_BODY, FLEXOKI_DARK_STATUS,
         FLEXOKI_DARK_ACCENT),
        ("flexoki-light", "flexoki-light", FLEXOKI_LIGHT_BODY, FLEXOKI_LIGHT_STATUS,
         FLEXOKI_LIGHT_ACCENT),
    ]
    for index, (label, scheme, want_body, want_status, want_accent) in enumerate(scenes):
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
        if want_accent not in screen.raw:
            failures.append(f"{label}: the accent never arrived as 38;2 ({want_accent!r} "
                            f"missing from {len(screen.raw)} bytes of output)")

    # A decoration's underline colour: the exact value has to reach SGR 58's
    # 24-bit form rather than being folded onto the nearest palette entry.
    cfg = "/tmp/jot_theme_probe_cfg_deco"
    write_config(cfg, "jot-dark")
    write_decoration_init(cfg)
    screen = run(binary, path, root, cfg)
    if dump:
        print(screen.text())
        print("-" * 70)
    wavy = b"\x1b[4:3m" in screen.raw
    print(f"decoration underline: 4:3 {wavy}, 58:2 form "
          f"{DECO_UNDERLINE in screen.raw}")
    if not wavy:
        failures.append("the decoration's wavy underline never reached the screen")
    if DECO_UNDERLINE not in screen.raw:
        failures.append(f"the underline colour was not emitted as 24-bit "
                        f"({DECO_UNDERLINE!r} missing)")

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

    # ...and the Flexoki pair, which is named `flexoki-*`: the query letter has
    # to be its own (the palette filters on the argument).
    screen = run(binary, path, root, cfg, keys=PALETTE + b"theme f")
    if dump:
        print(screen.text())
        print("-" * 70)
    view = screen.text()
    listed = [name for name in ("flexoki-dark", "flexoki-light") if name in view]
    print(f"chooser: lists {listed}")
    if len(listed) != 2:
        failures.append(f"chooser did not list both flexoki themes (saw {listed})")

    if failures:
        for failure in failures:
            print(f"theme probe: FAIL - {failure}")
        return 1
    print("theme probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
