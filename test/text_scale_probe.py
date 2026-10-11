#!/usr/bin/env python3
"""Probe: the terminal text scale, on the bytes the real binary writes.

A terminal program cannot change its own font: the emulator owns the glyphs. So a
scale above 1 means asking the terminal to draw each cell in a block -- kitty's
text sizing protocol (OSC 66) -- and laying the grid out that many times smaller.
Whether that is on is a property of the byte stream, and a pty is the honest judge
of one half of it: the frames are there once the zoom is in effect, and nothing is
framed when the terminal cannot draw them. The refusal is the half that matters
most, because a grid drawn at scaled coordinates a terminal does not scale comes
out in pieces -- text in every other cell, the rest stale.

  * a persisted `tui_text_scale = 2` frames the grid on the way up (startup path),
  * Alt+= and Ctrl+wheel-up both zoom in once the editor is up, and the frames
    appear in the stream only after those bytes,
  * on a terminal that does not report the protocol, the same chords leave the
    stream unframed and say why.

The terminal's answer is forced where the framing is what is being pinned
(JOT_TEXT_SCALE_FORCE=1, the same kind of escape hatch as JOT_RENDER_CHUNK_BYTES:
asking a pty for a real answer can only ever produce "no").

Usage: test/text_scale_probe.py [path-to-jot] [--dump]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary).
"""
from __future__ import annotations

import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

ROOT = "/tmp/jtscale"
CFG = "/tmp/jtscale_cfg"

# The SGR encoding of a Ctrl+wheel-up report over the middle of the screen:
# button 64 (wheel up) plus the 16 bit that says Ctrl.
CTRL_WHEEL_UP = b"\x1b[<80;40;12M"
ALT_EQUALS = b"\x1b="

HOME_UP = "Recent Folders"


def write_workspace() -> str:
    shutil.rmtree(ROOT, ignore_errors=True)
    work = os.path.join(ROOT, "work")
    os.makedirs(work)
    with open(os.path.join(work, "notes.txt"), "w") as fh:
        fh.write("hello\n")
    return work


def write_config(scale: int) -> None:
    shutil.rmtree(CFG, ignore_errors=True)
    os.makedirs(os.path.join(CFG, "configs"))
    with open(os.path.join(CFG, "configs", "settings.conf"), "w") as fh:
        # The persisted zoom, the way the zoom keys or :textzoom leave it.
        fh.write(f"tui_text_scale = {scale}\n")


def run(binary: str, work: str, scale: int, force: bool, phases=None, keys: bytes = b""):
    """One run, returning (screen, the raw bytes the child wrote)."""
    write_config(scale)
    chunks: list[bytes] = []
    env = {"JOT_TEXT_SCALE_FORCE": "1"} if force else None
    screen = run_in_pty(binary, [], keys, settle=1.5, after=1.0, cols=110, rows=30,
                        cfg=CFG, cwd=work, env=env, phases=phases,
                        on_output=lambda _fd, chunk: chunks.append(chunk))
    return screen, b"".join(chunks)


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    dump = "--dump" in sys.argv
    if not os.path.exists(binary):
        print(f"text scale probe: SKIP - no binary at {binary}")
        return 2

    work = write_workspace()
    failures = []
    started = (0.5, lambda s: HOME_UP in s.text())

    def shot(screen, raw, label):
        print(f"{label}: {raw.count(b'\x1b]66;s=2;')} frames, {len(raw)} bytes")
        if dump:
            print(screen.text())

    # 1. A persisted scale, on a terminal that reports the protocol: the grid is
    # framed from the first frame on, and rows are addressed on block boundaries
    # (grid row 1 is terminal row 3).
    screen, raw = run(binary, work, scale=2, force=True, phases=[started])
    shot(screen, raw, "startup at 2x")
    if b"\x1b]66;s=2;" not in raw:
        failures.append("a persisted scale was not framed for the terminal")
    if b"\x1b[3;1H" not in raw:
        failures.append("a scaled row is not addressed on its block boundary")

    # 2. Alt+= zooms in: the frames can only be in the stream after the chord.
    screen, raw = run(binary, work, scale=1, force=True, phases=[started, (0.1, ALT_EQUALS)])
    shot(screen, raw, "Alt+= zooms in")
    if b"\x1b]66;s=2;" not in raw:
        failures.append("Alt+= did not scale the grid")

    # 3. Ctrl+wheel-up is the mouse form of the same chord.
    screen, raw = run(binary, work, scale=1, force=True, phases=[started, (0.1, CTRL_WHEEL_UP)])
    shot(screen, raw, "Ctrl+wheel-up zooms in")
    if b"\x1b]66;s=2;" not in raw:
        failures.append("Ctrl+wheel-up did not scale the grid")

    # 4. The same chord on a terminal that does not report the protocol: nothing
    # is framed, the editor is still on screen, and it says why.
    screen, raw = run(binary, work, scale=1, force=False, phases=[started, (0.1, ALT_EQUALS)])
    shot(screen, raw, "refused on a plain terminal")
    if b"\x1b]66;" in raw:
        failures.append("a terminal without the protocol was framed for it anyway")
    if HOME_UP not in screen.text():
        failures.append("the editor is not on the screen at all after the refusal")
    # What the refusal *says* is pinned by the unit tests: the wording goes out
    # through the toast channel, whose queue order and life make a read here a
    # race against whatever toast the editor already has up.

    if failures:
        print("text scale probe: FAIL")
        for failure in failures:
            print(f"  - {failure}")
        return 1
    print("text scale probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
