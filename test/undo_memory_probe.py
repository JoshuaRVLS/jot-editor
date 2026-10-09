#!/usr/bin/env python3
"""Probe: typing does not put a copy of the buffer on the undo stack.

An undo snapshot holds the text a command is about to change, and the history
keeps 500 of them. A snapshot used to copy every line of the buffer, so one
keystroke in a 2000-line file cost ~190 KB and the history grew to ~94 MB (the
cap) while it was typed into. Snapshots now share the lines they did not change
with the snapshot underneath, which puts the same keystroke at one pointer per
line (~32 KB).

This drives the real binary, types into a 2000-line file and reads the
process's own RSS around it. Three scenes: the growth per keystroke has to
stay under half of what a per-keystroke copy would cost, a long typing run has
to stop growing once the byte budget is reached instead of climbing to the
500-state cap (a 4000-line file held ~32 MB of such states), and an undo after
typing has to put the text back, which is what tells the sharing never aliased
the live buffer.

Usage: test/undo_memory_probe.py [path-to-jot] [--dump]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary, no /proc).
"""
from __future__ import annotations

import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

# A 2000-line file of this width is ~180 KB of text, so a snapshot that copies
# every line costs ~190 KB per keystroke. Sharing one pointer per line instead
# is ~32 KB. The bound sits between them: half the old cost, three times the
# new one.
LINES = 2000
KEYS = 300
MAX_KB_PER_KEY = 96.0

# The history also has a byte budget, and this scene is what pins it: a
# 4000-line snapshot is ~64 KB of line pointers alone, so a long typing run
# used to fill the 500-state cap with ~32 MB per file and keep it. The budget
# holds the whole history to a couple of megabytes; 12 MB leaves room for the
# editor's ordinary ramp while staying far below what the cap reached.
SOAK_LINES = 4000
SOAK_KEYS = 600
MAX_HISTORY_MB = 12.0

CTRL_Z = b"\x1a"


def write_file(path: str, lines: int) -> None:
    with open(path, "w") as fh:
        for i in range(lines):
            text = "line %d:" % i
            while len(text) < 80:
                text += " filler"
            fh.write(text + "\n")


def child_pid() -> int:
    """The jot the harness forked: the direct child of this process."""
    me = os.getpid()
    for entry in os.listdir("/proc"):
        if not entry.isdigit():
            continue
        try:
            with open("/proc/%s/stat" % entry) as fh:
                fields = fh.read().split()
        except OSError:
            continue
        if len(fields) > 3 and fields[3] == str(me) and "jot" in fields[1]:
            return int(entry)
    return 0


def rss_kb(pid: int) -> int:
    try:
        with open("/proc/%d/status" % pid) as fh:
            for line in fh:
                if line.startswith("VmRSS:"):
                    return int(line.split()[1])
    except OSError:
        pass
    return -1


def growth_scene(binary: str, work: str) -> float:
    """Types into a big file and returns the RSS growth per keystroke, in KB."""
    path = os.path.join(work, "big.txt")
    write_file(path, LINES)
    seen = []

    def sample(_screen) -> bool:
        pid = child_pid()
        if pid:
            seen.append(rss_kb(pid))
        return True

    run_in_pty(binary, [path], b"", settle=4.0, after=1.0, cols=110, rows=34,
               cfg=os.path.join(work, "cfg"), phases=[
                   (2.0, sample),
                   (0.0, b"x" * KEYS),
                   (3.0, sample),
               ])
    if len(seen) < 2 or seen[0] < 0 or seen[-1] < 0:
        return -1.0
    return (seen[-1] - seen[0]) / float(KEYS)


def history_soak_scene(binary: str, work: str) -> float:
    """Types a long run into a big file and returns the total RSS growth, in MB."""
    path = os.path.join(work, "soak.txt")
    write_file(path, SOAK_LINES)
    seen = []

    def sample(_screen) -> bool:
        pid = child_pid()
        if pid:
            seen.append(rss_kb(pid))
        return True

    run_in_pty(binary, [path], b"", settle=4.0, after=1.0, cols=110, rows=34,
               cfg=os.path.join(work, "cfg_soak"), phases=[
                   (2.0, sample),
                   (0.0, b"x" * SOAK_KEYS),
                   (3.0, sample),
                   (2.0, sample),
               ])
    if len(seen) < 3 or seen[0] < 0 or seen[-1] < 0:
        return -1.0
    return (seen[-1] - seen[0]) / 1024.0


def undo_scene(binary: str, work: str):
    """One edit, one undo: the typed text has to be gone from the screen."""
    path = os.path.join(work, "undo.txt")
    write_file(path, LINES)
    screen = run_in_pty(binary, [path], b"", settle=4.0, after=1.0, cols=110, rows=34,
                        cfg=os.path.join(work, "cfg_undo"), phases=[
                            (1.0, b"QQ"),
                            (1.0, CTRL_Z + CTRL_Z),
                        ])
    view = screen.text()
    return ("QQline 0:" not in view and "line 0:" in view), view


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    dump = "--dump" in sys.argv
    if not os.path.exists(binary):
        print("undo memory probe: SKIP - no binary at %s" % binary)
        return 2
    if not os.path.isdir("/proc"):
        print("undo memory probe: SKIP - no /proc to read the process's RSS")
        return 2

    work = "/tmp/jot_undo_memory_probe"
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)

    failures = []
    per_key = growth_scene(binary, work)
    if per_key < 0:
        print("undo memory probe: SKIP - the process's RSS could not be read")
        return 2
    print("growth: %d keys in a %d-line file, %.1f KB per keystroke (bound %.0f)"
          % (KEYS, LINES, per_key, MAX_KB_PER_KEY))
    if per_key > MAX_KB_PER_KEY:
        failures.append("typing grew the history by %.1f KB per keystroke" % per_key)

    soaked = history_soak_scene(binary, work)
    if soaked < 0:
        print("undo memory probe: SKIP - the process's RSS could not be read")
        return 2
    print("soak:   %d keys in a %d-line file, %.1f MB total (bound %.0f)"
          % (SOAK_KEYS, SOAK_LINES, soaked, MAX_HISTORY_MB))
    if soaked > MAX_HISTORY_MB:
        failures.append("a %d-key run grew the process by %.1f MB" % (SOAK_KEYS, soaked))

    undone, view = undo_scene(binary, work)
    print("undo:    %s" % ("ok" if undone else "FAIL"))
    if not undone:
        failures.append("undo left the typed text on screen")
    if dump:
        print(view)
        print("-" * 70)
    if failures:
        print("undo memory probe: FAIL")
        for failure in failures:
            print("  - %s" % failure)
        return 1
    print("undo memory probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
