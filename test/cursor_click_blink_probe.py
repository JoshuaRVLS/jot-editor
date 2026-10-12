#!/usr/bin/env python3
"""Probe: a click restarts the caret blink without a dead pause.

Clicking a new caret position runs through the mouse dispatcher, which opened
the same 700 ms input hold a keystroke does - on press and again on release. The
caret therefore sat solid for the hold plus the first visible half, ~1.2 s at the
default 500 ms period, so the blink looked like it had stopped instead of
restarting. A pointer move now re-anchors the phase with no hold: the caret
shows at the clicked cell and the first hidden half lands one period later.

This drives the real binary in a pty, clicks once and times the caret's flips in
the terminal byte stream. A flip to hidden is a read carrying ?25l and no ?25h
(the per-frame churn is a read carrying both), and the click is non-vacuous
because the first show after it must be preceded by a CUP to the clicked row.

Usage: test/cursor_click_blink_probe.py [path-to-jot-binary] [--dump]
Exit codes: 0 pass, 1 fail, 2 binary missing.
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

HIDE = b"\x1b[?25l"
SHOW = b"\x1b[?25h"
CUP = re.compile(rb"\x1b\[(\d+);(\d+)H")

PERIOD_S = 0.5  # the default cursor_blink_ms: one blink half is 500 ms
# Old behaviour: the 700 ms hold plus the first half put the first hide ~1.2 s
# after the click. New behaviour: one period. The bound sits between them, with
# room for pty and frame jitter.
MAX_FIRST_HIDE_S = 0.85
MIN_FIRST_HIDE_S = 0.25  # the caret has to be shown at the new cell first
HALF_TOLERANCE_S = 0.3

CLICK_COL = 40  # 1-based SGR coordinates, inside the pane's text area
CLICK_ROW = 14
SETTLE_S = 1.4  # startup frames done and the blink clock running
WATCH_S = 2.4  # long enough for the first two halves after the click


def run(binary: str):
    """Runs jot in a pty, clicks once, and returns (reads, click time)."""
    workdir = "/tmp/jot_cursor_click_probe_work"
    os.makedirs(workdir, exist_ok=True)
    target = os.path.join(workdir, "click_probe.txt")
    with open(target, "w") as fh:
        for i in range(200):
            fh.write("line %d of the click probe\n" % i)

    pid, fd = pty.fork()
    if pid == 0:  # child
        os.environ["TERM"] = "xterm-256color"
        os.environ["JOT_KITTY_PROTOCOL"] = "1"
        os.environ["JOT_CONFIG_HOME"] = "/tmp/jot_cursor_click_probe_cfg"
        os.environ["JOT_CACHE_HOME"] = "/tmp/jot_cursor_click_probe_cfg"
        try:
            os.execv(binary, [binary, target])
        except OSError:
            os._exit(127)

    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", 30, 100, 0, 0))
    reads = []
    click_time = None
    started = time.time()
    deadline = started + SETTLE_S + WATCH_S
    try:
        while time.time() < deadline:
            if click_time is None and time.time() - started >= SETTLE_S:
                press = "\x1b[<0;%d;%dM" % (CLICK_COL, CLICK_ROW)
                release = "\x1b[<0;%d;%dm" % (CLICK_COL, CLICK_ROW)
                try:
                    os.write(fd, press.encode())
                    time.sleep(0.03)
                    os.write(fd, release.encode())
                except OSError:
                    break
                click_time = time.time()
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
    return reads, click_time


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    dump = "--dump" in sys.argv
    if not os.path.exists(binary):
        print("cursor click blink probe: SKIP - no binary at %s" % binary)
        return 2
    os.makedirs("/tmp/jot_cursor_click_probe_cfg", exist_ok=True)

    reads, click_time = run(binary)
    if click_time is None:
        print("cursor click blink probe: FAIL - the click was never sent")
        return 1
    after = b"".join(data for t, data in reads if t >= click_time)
    if dump:
        sys.stdout.buffer.write(after)
        print()
        print("-" * 70)
    if not after:
        print("cursor click blink probe: FAIL - the click produced no output")
        return 1

    # The click has to have moved the caret: the first show after it must be
    # preceded by a CUP placing the hardware cursor on the clicked row.
    shows = list(re.finditer(re.escape(SHOW), after))
    if not shows:
        print("cursor click blink probe: FAIL - the caret was never shown after the click")
        return 1
    moves = list(CUP.finditer(after, 0, shows[0].start()))
    row = int(moves[-1].group(1)) if moves else 0
    if row != CLICK_ROW:
        print("cursor click blink probe: FAIL - first show after the click was at row %d, "
              "not the clicked row %d" % (row, CLICK_ROW))
        return 1
    print("cursor click blink probe: ok - the click moved the caret to row %d" % row)

    # Flips: a read carrying both is a frame with the caret visible (per-frame
    # churn, no phase information); a read carrying one is a phase flip.
    flip_off = sorted(t for t, data in reads if t > click_time and HIDE in data and SHOW not in data)
    flip_on = sorted(t for t, data in reads if t > click_time and SHOW in data and HIDE not in data)
    if not flip_off:
        print("cursor click blink probe: FAIL - the caret stopped blinking after the click")
        return 1

    first = flip_off[0] - click_time
    print("cursor click blink probe: first hidden half %.2fs after the click" % first)
    if first > MAX_FIRST_HIDE_S:
        print("cursor click blink probe: FAIL - the caret stayed solid for %.2fs after the click "
              "(the typing hold came back)" % first)
        return 1
    if first < MIN_FIRST_HIDE_S:
        print("cursor click blink probe: FAIL - the caret was hidden %.2fs after the click; "
              "it never showed at the new cell" % first)
        return 1

    # And the rhythm keeps going: the caret must come back a whole period later.
    next_on = next((t for t in flip_on if t > flip_off[0]), None)
    if next_on is None:
        print("cursor click blink probe: FAIL - the caret never came back after hiding")
        return 1
    half = next_on - flip_off[0]
    print("cursor click blink probe: next visible half %.2fs" % half)
    if abs(half - PERIOD_S) > HALF_TOLERANCE_S:
        print("cursor click blink probe: FAIL - the half after the click was %.2fs, "
              "not the %.1fs period" % (half, PERIOD_S))
        return 1

    print("cursor click blink probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
