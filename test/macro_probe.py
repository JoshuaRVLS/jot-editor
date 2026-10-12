#!/usr/bin/env python3
"""Probe: the macro chords record and replay through the real key path.

The unit tests feed the recorder through the frontend's decoded key event, which
is the seam the recorder is built on but not the bytes a terminal sends. What
only a real pty shows is the whole chain: the Alt+Z chord as the escape sequence
a terminal emits (`ESC z`), the decoder turning it into an alt key, the Lua
keymap claiming the chord, and the replay feeding the recorded keys back in.

Scenes: record two letters from the chord and play them back (the macro re-runs
the keystrokes, so the text lands twice); the chords themselves stay out of the
register (playing once repeats the letters, not the stop chord); the recorder
says it is live while it is; and `:macrolist` from the palette names the
register it took and marks it as the one `Alt+Z p` would play.

Usage: test/macro_probe.py [path-to-jot] [--dump]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary).
"""
from __future__ import annotations

import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

CTRL_P = b"\x10"  # palette
ENTER = b"\r"
# A terminal reports Alt+Z as ESC followed by the letter; that is the sequence
# the recorder's filter has to recognise, not a flag some test sets.
ALT_Z = b"\x1bz"
NOTES = "alpha\nbeta\n"


def palette(name: str) -> bytes:
    return CTRL_P + name.encode() + ENTER


def main() -> int:
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    dump = "--dump" in sys.argv
    binary = args[0] if args else "build/apps/jot/jot"
    if not os.path.exists(binary):
        print(f"macro probe: SKIP - no binary at {binary}")
        return 2

    work = "/tmp/jot_macro_probe"
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work, exist_ok=True)
    with open(os.path.join(work, "notes.txt"), "w") as fh:
        fh.write(NOTES)
    cfg = "/tmp/jot_macro_probe_cfg"
    shutil.rmtree(cfg, ignore_errors=True)

    failures = 0

    def check(name: str, ok: bool, detail: str = ""):
        nonlocal failures
        print(f"macro probe: {'ok' if ok else 'FAIL'} - {name}"
              f"{(': ' + detail) if (detail and not ok) else ''}")
        if not ok:
            failures += 1

    def session(keys: bytes, phases=None, after: float = 2.5):
        return run_in_pty(binary, [os.path.join(work, "notes.txt")], keys,
                          settle=3.0, after=after, cols=100, rows=30,
                          cfg=cfg, cwd=work, phases=phases)

    # 1. Alt+Z q starts a recording, two letters go in, Alt+Z q stops it, and
    #    Alt+Z p re-runs those two keys: the buffer holds "hihialpha", which is
    #    the letters landing a second time rather than a recorded edit replayed.
    text = session(ALT_Z + b"q" + b"hi" + ALT_Z + b"q" + ALT_Z + b"p").text()
    check("the recorded keys landed once", "hialpha" in text)
    check("playing the macro landed them again", "hihialpha" in text,
          "the replay did not re-run the two keys")
    check("the play says which register it ran", "Played @q" in text,
          "no 'Played @q' on the status line")

    # 2. The chords stay out of the register. One "x" is recorded; playing once
    #    must add exactly one more, not replay a stop-chord that would end the
    #    recording state the macro was played from.
    seen = {"recording": False}

    def recording_notice(screen) -> bool:
        if "Recording @q" in screen.text():
            seen["recording"] = True
            return True
        return False

    text = session(ALT_Z + b"q" + b"x" + ALT_Z + b"q" + ALT_Z + b"p",
                   phases=[(0.5, recording_notice)]).text()
    check("the recorder says it is live", seen["recording"],
          "no 'Recording @q' while the recording was running")
    check("one key was recorded, and only one", "xxalpha" in text,
          "the stop chord was filed into the register, or the key was not")

    # 3. :macrolist from the palette names the register and marks the last one
    #    played -- the register Alt+Z p would use next.
    text = session(ALT_Z + b"q" + b"x" + ALT_Z + b"q" + ALT_Z + b"p"
                   + palette("macrolist")).text()
    check("the list names the register and its length", "@q 1" in text,
          "no '@q 1' after :macrolist")
    check("the list marks the register Alt+Z p plays", "@q 1*" in text,
          "the played register is not marked")

    # 4. A fresh recording on the same register takes the place of the old take:
    #    two letters, then one, plays back "n" alone.
    text = session(ALT_Z + b"q" + b"ol" + ALT_Z + b"q"
                   + ALT_Z + b"q" + b"n" + ALT_Z + b"q"
                   + ALT_Z + b"p").text()
    check("a re-record replaces the take", "olnnalpha" in text,
          "the replay ran the first take as well as the second")

    # 5. The command line is the other way in, argument included: the register is
    #    chosen by name and replayed with a count, so `:macroplay q 3` runs its
    #    one key three times over. (The recording itself is made from the chord,
    #    and stopped before the palette opens: the palette is a key-driven
    #    surface, so what is typed into it *while a recording is live* is part of
    #    the macro -- which is the honest behaviour, and not what this case is
    #    about.)
    text = session(ALT_Z + b"q" + b"x" + ALT_Z + b"q" + palette("macroplay q 3"),
                   after=1.0).text()
    check("a counted replay runs the register that many times", "xxxxalpha" in text,
          "one recorded key, replayed with a count of 3, did not land three more")
    check("the count is reported", "Played @q x3" in text,
          "no 'Played @q x3' after the counted replay")

    if dump:
        print(text)

    if failures:
        print(f"macro probe: FAIL ({failures})")
        return 1
    print("macro probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
