#!/usr/bin/env python3
"""Probe: a message on the status line comes down on its own.

`message` is the bar's one text slot. The setters arm the timer that clears it,
but several older call sites -- the save path among them -- write the member
straight, so `Saved: notes.txt` used to sit on the bar until some other message
replaced it. A session proves both halves of the outcome: the text goes up when
the save happens, and it is gone a few seconds later with nothing else typed in
between.

The second save matters. A boot-time notice (the tree-sitter banner, for one)
arms its own timer, which can clear a save message early and make a single-save
run look like this works when it does not: here the second save lands well after
that window, so only the message's own timer can take it down.

Usage: test/statusline_message_probe.py [path-to-jot] [--dump]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary).
"""
from __future__ import annotations

import os
import shutil
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

SAVE = b"\x13"  # Ctrl+S
NOTE = "Saved: notes.txt"
# Longer than the 5 s the bar keeps a message, with room for a slow machine.
LINGER = 8.0


def status_row(screen) -> str:
    return screen.text().splitlines()[-1]


def wait_seconds(seconds: float):
    """A phase that ends when the wall clock says so, not when a frame does.

    The deadline starts on the phase's first call, not when the phase list is
    built: built early, it has already passed by the time the run reaches it.
    """
    deadline = {}

    def ready(_screen) -> bool:
        deadline.setdefault("at", time.time() + seconds)
        return time.time() >= deadline["at"]

    return ready


def main() -> int:
    positional = [arg for arg in sys.argv[1:] if not arg.startswith("--")]
    binary = positional[0] if positional else "build/apps/jot/jot"
    dump = "--dump" in sys.argv
    if not os.path.exists(binary):
        print(f"statusline message probe: SKIP - no binary at {binary}")
        return 2

    root = tempfile.mkdtemp(prefix="jot_status_message_")
    cfg = tempfile.mkdtemp(prefix="jot_status_message_cfg_")
    path = os.path.join(root, "notes.txt")
    with open(path, "w") as fh:
        fh.write("alpha beta gamma\nsecond line\n")

    failures = []
    try:
        # Scene 1: the save reports itself. Read while it is still up.
        first = run_in_pty(binary, [path], SAVE, settle=3.0, after=1.0,
                           cols=120, rows=24, cfg=cfg, cwd=root)
        row = status_row(first)
        if dump:
            print("--- right after the save ---")
            print(row)
        if NOTE not in row:
            failures.append(f"the save did not report itself; the bar reads {row!r}")

        # Scene 2: two saves, the second one after any boot-time notice has had
        # its say, then a wait past the message's own read time.
        shutil.rmtree(cfg, ignore_errors=True)
        os.makedirs(cfg, exist_ok=True)
        second = run_in_pty(binary, [path], SAVE, settle=3.0, after=9.0,
                            phases=[(0.2, SAVE), (0.0, wait_seconds(LINGER))],
                            until_timeout=40.0, cols=120, rows=24, cfg=cfg, cwd=root)
        row = status_row(second)
        if dump:
            print("--- after the second save, past the read time ---")
            print(row)
        if NOTE in row:
            failures.append(f"the message stayed on the bar for over {LINGER:.0f}s: {row!r}")
    finally:
        shutil.rmtree(root, ignore_errors=True)
        shutil.rmtree(cfg, ignore_errors=True)

    if failures:
        for failure in failures:
            print(f"statusline message probe: FAIL - {failure}")
        return 1
    print("statusline message probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
