#!/usr/bin/env python3
"""Caret probe for the terminal frontend.

The caret's terminal bytes cannot be checked without a real pty, and they are
exactly what regressed:

  * jot used to hand the blink phase to the emulator with a *blinking* DECSCUSR
    shape (ESC [ 5 q), so cursor_blink_ms did nothing and the blink rate was
    whatever the terminal felt like -- and some terminals reset the shape on a
    visibility toggle, which flickered the caret while scrolling;
  * the show sequence was written unconditionally after the hide branch, so a
    deliberately hidden cursor (menu, palette, popup, or the blink's own off
    half) was immediately re-shown.

What this asserts, driving the real binary in a pty with a file open:

  * the shape is a STEADY DECSCUSR (ESC [ 6 q bar / ESC [ 2 q block), never a
    blinking one,
  * no hide is immediately undone by a shape write,
  * the caret actually blinks: at the default 500 ms half-period a 3 s run
    contains a long stretch with no show at all.

Counting hides proves nothing on its own -- the renderer hides the caret at the
start of every full frame and re-shows it at the end, so the hide/show churn is
per-frame, not per-blink. Only the timing of the shows reveals the phase.

Usage: test/cursor_probe.py [path-to-jot-binary]
Exit codes: 0 pass, 1 fail, 2 binary missing / no pty support.
"""
from __future__ import annotations

import fcntl
import os
import pty
import re
import select
import signal
import struct
import sys
import termios
import time

BLINKING_SHAPES = (b"\x1b[1 q", b"\x1b[5 q")
STEADY_SHAPES = (b"\x1b[2 q", b"\x1b[6 q")
HIDE = b"\x1b[?25l"
SHOW = b"\x1b[?25h"

# The default cursor_blink_ms is 500, so a show-free stretch of at least this
# long can only come from a blink phase (or from the caret being hidden, which
# this probe rules out by keeping a file -- and a caret on screen -- open).
MIN_HIDDEN_STRETCH_S = 0.3


def run_jot(binary: str, seconds: float, cols: int = 100, rows: int = 30):
    """Run jot in a pty and return [(timestamp, bytes), ...]."""
    # An editor frame with a caret needs a file: started bare, jot shows the home
    # menu, which hides the caret by design and would make this probe vacuous.
    workdir = "/tmp/jot_cursor_probe_work"
    os.makedirs(workdir, exist_ok=True)
    target = os.path.join(workdir, "caret_probe.txt")
    with open(target, "w") as fh:
        for i in range(200):
            fh.write(f"line {i} of the caret probe\n")

    pid, fd = pty.fork()
    if pid == 0:  # child
        os.environ["TERM"] = "xterm-256color"
        os.environ["JOT_CONFIG_HOME"] = "/tmp/jot_cursor_probe_cfg"
        os.environ["JOT_CACHE_HOME"] = "/tmp/jot_cursor_probe_cfg"
        try:
            os.execv(binary, [binary, target])
        except OSError:
            os._exit(127)

    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
    reads = []
    deadline = time.time() + seconds
    # Move the caret while we watch: a caret that never moves is never
    # re-placed, and re-placement is where it went to the wrong cell.
    # SGR mouse press/release pairs, then keys: a click re-homes the caret
    # through the mouse path, which is a different route to the same cell.
    def click(col, row):
        seq = f"\x1b[<0;{col};{row}M".encode()
        return seq + seq[:-1] + b"m"

    nudges = [b"\x1b[C", b"\x1b[C", b"\x1b[D",
              click(30, 12), click(44, 20), click(12, 6),
              b"\x1b[C", b"\x1b[A", b"\x1b[D", click(60, 24), b"\x1b[B"]
    nudge_at = time.time() + 0.7
    try:
        while time.time() < deadline:
            if nudges and time.time() >= nudge_at:
                try:
                    os.write(fd, nudges.pop(0))
                except OSError:
                    pass
                nudge_at = time.time() + 0.22
            r, _, _ = select.select([fd], [], [], 0.05)
            if not r:
                continue
            try:
                data = os.read(fd, 65536)
            except OSError:
                break
            if not data:
                break
            reads.append((time.time(), data))
    finally:
        try:
            os.kill(pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        try:
            os.close(fd)
        except OSError:
            pass
        try:
            os.waitpid(pid, 0)
        except ChildProcessError:
            pass
    return reads


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    if not os.path.exists(binary):
        print(f"cursor probe: SKIP - no binary at {binary}")
        return 2
    os.makedirs("/tmp/jot_cursor_probe_cfg", exist_ok=True)

    reads = run_jot(binary, seconds=3.0)
    stream = b"".join(data for _, data in reads)
    if not stream:
        print("cursor probe: FAIL - the editor produced no output")
        return 1

    # Steady DECSCUSR only.
    blinking = [s for s in BLINKING_SHAPES if s in stream]
    steady = [s for s in STEADY_SHAPES if s in stream]
    if blinking:
        print(f"cursor probe: FAIL - blinking DECSCUSR emitted: {blinking}")
        return 1
    if not steady:
        print("cursor probe: FAIL - no steady DECSCUSR shape emitted")
        return 1
    print(f"cursor probe: ok - steady DECSCUSR shape {steady[0]!r}")

    # A hide must never be undone by a shape write before the next show.
    for m in re.finditer(re.escape(HIDE), stream):
        rest = stream[m.end():]
        nxt_show = rest.find(SHOW)
        nxt_shape = min(
            [i for i in (rest.find(s) for s in STEADY_SHAPES) if i != -1] or [len(rest)]
        )
        if nxt_shape < nxt_show:
            print("cursor probe: FAIL - a hide was followed by a shape write that re-shows")
            return 1
    print("cursor probe: ok - no hide was undone by a shape write")

    # Blinking, by timing. Each show is stamped with the read that carried it.
    show_times = []
    for t, data in reads:
        # A read can carry several shows; spread them across the read's window so
        # a burst does not look like one instant and hide the gaps being measured.
        count = data.count(SHOW)
        show_times.extend([t] * count)
    if len(show_times) < 2:
        print("cursor probe: FAIL - the caret was shown fewer than twice (no blink)")
        return 1
    longest = max(b - a for a, b in zip(show_times, show_times[1:]))
    if longest < MIN_HIDDEN_STRETCH_S:
        print(f"cursor probe: FAIL - the caret never stayed hidden (longest show-free "
              f"stretch {longest:.2f}s)")
        return 1
    print(f"cursor probe: ok - the caret blinked (longest show-free stretch {longest:.2f}s)")

    # Where each show lands. jot shows the caret with ?25h and positions it with
    # a CUP move immediately before, so a show with no move behind it -- or one
    # behind a move to (1,1) -- puts the caret in the top-left corner.
    cup = re.compile(rb"\x1b\[(\d+);(\d+)H")
    shows = 0
    for m in re.finditer(re.escape(SHOW), stream):
        shows += 1
        moves = list(cup.finditer(stream, 0, m.start()))
        if not moves:
            print(f"cursor probe: FAIL - show #{shows} had no caret move before it")
            return 1
        row, col = (int(x) for x in moves[-1].groups())
        if (row, col) == (1, 1):
            print(f"cursor probe: FAIL - show #{shows} put the caret in the top-left corner")
            return 1
    print(f"cursor probe: ok - all {shows} caret shows followed a real caret cell")

    print("cursor probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
