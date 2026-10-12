#!/usr/bin/env python3
"""Statusline time probe: the local clock and the coding-time chip on a pty.

The coding chip reports how long was coded today in the workspace (the local
store while WakaTime is off, features/coding_time.h). Three things about the
two chips cannot be checked without a real run:

  * they are painted at the right-hand end of the status row with their own
    glyphs, and the clock reads the *local* time -- not just two digits and a
    colon, which any placeholder would satisfy;
  * the coding chip moves with nothing typed at all while the editor has not
    been left idle, which is the whole point of it and is the one thing a
    screenshot cannot show: the unit tests plant a total through a hook, and
    this samples the same row *repeatedly* on a live run. A series, not two
    samples: with nothing buying the frame, the bar is only repainted when
    something else happens to dirty the screen -- an autosave or a watcher tick
    -- so it sits frozen and then jumps. Reading the chip every 2 s and demanding
    it advance *every* time is what pins the frame loop's own ask
    (Editor::status_time_due_soon); a start-and-end pair passes on that one
    unrelated jump alone. The samples all land inside the idle cap, so what they
    measure is the gate rather than the cap (which the unit tests pin);
  * the two config keys are independent and both are off on a fresh install, so
    a user can put either back (the settings are read per frame, so a config
    file is all it takes).

Usage: test/status_clock_probe.py [path-to-jot-binary]
Set JOT_PROBE_DUMP=1 to print the sampled rows.
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

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import Screen  # noqa: E402

# Nerd Fonts codepoints the chips lead with (nf-md-clock_outline /
# nf-md-timer_outline), the same pair the C++ test pins.
CLOCK_GLYPH = "\U000F0150"
TIMER_GLYPH = "\U000F051B"

COLS, ROWS = 120, 30
# The chips are only painted once the editor has booted its UI kit and drawn a
# frame, so the first sample waits for that; the rest run at a steady 2 s so the
# chip has a whole second to move between any two of them, and the whole series
# stays well inside the coding store's two-minute idle cap.
FIRST_SAMPLE_S = 3.0
SAMPLE_STEP_S = 2.0
IDLE_SAMPLES = 5

CLOCK_RE = re.compile(r"(\d{2}):(\d{2})")
CODING_RE = re.compile(r"\s(\d+)(s|m|h )\s")


def write_settings(cfg: str, lines: list[str]) -> None:
    os.makedirs(os.path.join(cfg, "configs"), exist_ok=True)
    with open(os.path.join(cfg, "configs", "settings.conf"), "w") as fh:
        for line in lines:
            fh.write(line + "\n")


def run_jot(binary: str, cfg: str, samples: dict[str, float], timeout_s: float):
    """Run jot in a pty and return {label: status row text} at each timestamp."""
    os.makedirs(cfg, exist_ok=True)
    pid, fd = pty.fork()
    if pid == 0:  # child
        os.environ["TERM"] = "xterm-256color"
        os.environ["COLORTERM"] = "truecolor"
        os.environ["JOT_KITTY_PROTOCOL"] = "1"
        os.environ["JOT_CONFIG_HOME"] = cfg
        os.environ["JOT_CACHE_HOME"] = cfg
        try:
            os.execv(binary, [binary])
        except OSError:
            os._exit(127)

    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", ROWS, COLS, 0, 0))
    screen = Screen(COLS, ROWS)
    taken: dict[str, str] = {}
    start = time.monotonic()
    try:
        while time.monotonic() - start < timeout_s:
            r, _, _ = select.select([fd], [], [], 0.1)
            if r:
                try:
                    data = os.read(fd, 65536)
                except OSError:
                    break
                if not data:
                    break
                screen.feed(data)
            elapsed = time.monotonic() - start
            for label, at in samples.items():
                if label not in taken and elapsed >= at:
                    taken[label] = "".join(screen.cells[ROWS - 1])
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
    if os.environ.get("JOT_PROBE_DUMP"):
        for label, row in taken.items():
            print(f"    [{label}] {row!r}")
    return taken


def clock_matches_local(row: str) -> bool:
    """The HH:MM on the row is the machine's local time (modulo a boundary)."""
    m = CLOCK_RE.search(row)
    if not m:
        return False
    now = time.localtime()
    allowed = {(f"{now.tm_hour:02d}", f"{now.tm_min:02d}")}
    before = time.localtime(time.time() - 60)
    allowed.add((f"{before.tm_hour:02d}", f"{before.tm_min:02d}"))
    return (m.group(1), m.group(2)) in allowed


def coded_seconds(row: str) -> int | None:
    """The chip's seconds reading, or None when it is on a minute/hour label."""
    m = CODING_RE.search(row)
    if not m or m.group(2) != "s":
        return None
    return int(m.group(1))


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    if not os.path.exists(binary):
        print(f"status clock probe: SKIP - no binary at {binary}")
        return 2
    binary = os.path.abspath(binary)

    # ── Scene 1: both chips on (the shipped default is off), sampled over a
    # stretch of pure idle ─────────────────────────────────────────────────
    cfg = "/tmp/jot_status_clock_probe"
    write_settings(cfg, ["status_clock=true", "status_coding_time=true"])
    labels = [f"t{i}" for i in range(IDLE_SAMPLES)]
    taken = run_jot(
        binary,
        cfg,
        {label: FIRST_SAMPLE_S + i * SAMPLE_STEP_S for i, label in enumerate(labels)},
        timeout_s=FIRST_SAMPLE_S + (IDLE_SAMPLES - 1) * SAMPLE_STEP_S + 1.5,
    )
    rows = [taken.get(label, "") for label in labels]
    if not all(rows):
        print("status clock probe: FAIL - the editor painted no status row")
        return 1
    for i, row in enumerate(rows):
        if CLOCK_GLYPH not in row or TIMER_GLYPH not in row:
            print(f"status clock probe: FAIL - sample {i} row is missing a chip glyph")
            return 1
    late = rows[-1]
    if not clock_matches_local(late):
        print(f"status clock probe: FAIL - the clock chip is not the local time "
              f"({CLOCK_RE.search(late).group(0) if CLOCK_RE.search(late) else 'none'})")
        return 1
    print("status clock probe: ok - the clock chip is the local time")

    seconds = [coded_seconds(row) for row in rows]
    if any(s is None for s in seconds):
        print("status clock probe: FAIL - no coding-time chip on the status row")
        return 1
    print("status clock probe: ok - coding chip series " +
          "  ".join(f"{s}s" for s in seconds))
    # Every sample is 2 s after the one before, so a bar that is really being
    # repainted once a second moves *every* time. A frozen bar reads the same
    # number twice (or only moves on the one unrelated repaint inside the
    # window), which no amount of clock passage fixes: the local total only
    # moves when the frame loop asks, and the frame loop only paints what the
    # moved label buys.
    for i in range(1, len(seconds)):
        if seconds[i] <= seconds[i - 1]:
            print(f"status clock probe: FAIL - the coding chip stalled: "
                  f"{seconds[i - 1]}s then {seconds[i]}s after "
                  f"{SAMPLE_STEP_S:g}s of nothing\n"
                  f"  the bar is only being repainted on input; the frame loop's "
                  f"status_time_due_soon ask is what should be buying these frames")
            return 1
    print(f"status clock probe: ok - the coding chip advanced at every sample "
          f"({seconds[0]}s -> {seconds[-1]}s)")

    # ── Scene 2: the shipped default -- no settings at all, so both keys are
    # at their default and the bar carries neither chip ─────────────────────
    cfg = "/tmp/jot_status_clock_probe_off"
    write_settings(cfg, [])
    off = run_jot(binary, cfg, {"row": FIRST_SAMPLE_S}, timeout_s=FIRST_SAMPLE_S + 1.5)
    row = off.get("row", "")
    if not row:
        print("status clock probe: FAIL - the editor painted no status row")
        return 1
    if CLOCK_GLYPH in row or TIMER_GLYPH in row or CODING_RE.search(row):
        print("status clock probe: FAIL - the chips are painted with both keys off")
        return 1
    print("status clock probe: ok - the default bar carries neither chip")

    # ── Scene 3: the clock alone ─────────────────────────────────────────────
    cfg = "/tmp/jot_status_clock_probe_clock"
    write_settings(cfg, ["status_clock=true", "status_coding_time=false"])
    only = run_jot(binary, cfg, {"row": FIRST_SAMPLE_S}, timeout_s=FIRST_SAMPLE_S + 1.5)
    row = only.get("row", "")
    if CLOCK_GLYPH not in row:
        print("status clock probe: FAIL - status_coding_time=false also dropped the clock")
        return 1
    if TIMER_GLYPH in row or CODING_RE.search(row):
        print("status clock probe: FAIL - the coding chip survived its own key being off")
        return 1
    print("status clock probe: ok - the two keys are independent")

    print("status clock probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
