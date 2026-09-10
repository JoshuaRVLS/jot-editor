#!/usr/bin/env python3
"""Live-resize probe for the terminal frontend.

The terminal half of "the editor does not follow the window size" cannot be
checked headlessly: it needs a real pty whose winsize changes mid-run, plus the
SIGWINCH that goes with it. This drives the real binary that way and asserts:

  * the process survives shrink and grow cycles (no crash, no hang),
  * the rendering actually follows each new size -- jot addresses cells
    absolutely ("ESC [row;colH"), so the rows it paints reveal the geometry it
    believes it has,
  * after shrinking, no cell outside the new window is addressed (which would
    mean the grid lagged behind the pty).

Usage: test/resize_probe.py [path-to-jot-binary]
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
import subprocess
import sys
import termios
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
# Resolved to an absolute path: the child runs with its own cwd (the scratch
# workspace), so a relative binary path would not resolve there.
BIN = (Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build/apps/jot/jot").resolve()

ROW_MOVE = re.compile(rb"\x1b\[(\d+);(\d+)H")
# jot's size probe parks the cursor at (999,999) before asking for the position
# with DSR (see Terminal::cursor_probe_size). That is not a paint, so it must be
# stripped before judging which rows the renderer addressed.
CURSOR_PROBE = re.compile(rb"\x1b\[999;999H\x1b\[6n")
# Full-screen clear. Resizes used to emit this on every step (UI::invalidate)
# because the newly exposed cells could not be trusted; the renderer now does
# one forced full paint instead, which leaves the screen in place.
SCREEN_CLEAR = re.compile(rb"\x1b\[2J")


def set_winsize(fd: int, rows: int, cols: int) -> None:
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))


def drain(fd: int, seconds: float) -> bytes:
    """Reads from the pty for `seconds`, returning everything received."""
    out = bytearray()
    deadline = time.time() + seconds
    while time.time() < deadline:
        ready, _, _ = select.select([fd], [], [], max(0.0, deadline - time.time()))
        if not ready:
            break
        try:
            chunk = os.read(fd, 65536)
        except OSError:
            break
        if not chunk:
            break
        out += chunk
    return bytes(out)


def painted_rows(data: bytes) -> list[int]:
    """Rows the renderer positioned the cursor on, excluding probe traffic."""
    return [int(m.group(1)) for m in ROW_MOVE.finditer(CURSOR_PROBE.sub(b"", data))]


def max_row_addressed(data: bytes) -> int:
    rows = painted_rows(data)
    return max(rows) if rows else 0


def main() -> int:
    if not BIN.is_file() or not os.access(BIN, os.X_OK):
        print(f"resize probe: binary not found: {BIN}", file=sys.stderr)
        return 2

    work = Path(subprocess.run(["mktemp", "-d"], capture_output=True, text=True,
                               check=True).stdout.strip())
    (work / "home").mkdir()
    (work / "proj").mkdir()
    (work / "proj" / "main.cpp").write_text("int main() { return 0; }\n")
    (work / "proj" / "notes.txt").write_text("hello\n")

    master, slave = pty.openpty()
    set_winsize(master, 30, 100)

    env = dict(os.environ)
    env["JOT_CONFIG_HOME"] = str(work / "home")
    env["JOT_CACHE_HOME"] = str(work / "home")
    env["TERM"] = env.get("TERM", "xterm-256color")
    # A pty renders at whatever size it is told; keep the renderer's self-heal
    # from being the only thing that repaints.
    env.pop("JOT_SAFE_MODE", None)

    proc = subprocess.Popen(
        [str(BIN), str(work / "proj" / "main.cpp")],
        stdin=slave,
        stdout=slave,
        stderr=slave,
        cwd=str(work / "proj"),
        env=env,
        start_new_session=True,
    )
    os.close(slave)

    failures: list[str] = []
    try:
        first = drain(master, 2.5)
        if proc.poll() is not None:
            failures.append(f"jot exited early (rc={proc.returncode})")
            print(first[-800:].decode("utf-8", "replace"), file=sys.stderr)
            return 1
        if max_row_addressed(first) < 25:
            failures.append(
                f"initial frame did not use the 30-row window (max row {max_row_addressed(first)})"
            )
        # Startup legitimately clears once; remember how many so only resizes are
        # judged below.
        clears_at_start = len(SCREEN_CLEAR.findall(first))

        # Shrink: 30x100 -> 20x60.
        set_winsize(master, 20, 60)
        os.killpg(os.getpgid(proc.pid), signal.SIGWINCH)
        shrunk = drain(master, 2.0)
        if proc.poll() is not None:
            failures.append(f"jot died on shrink (rc={proc.returncode})")
        else:
            rows = painted_rows(shrunk)
            if not rows:
                failures.append("no repaint after shrinking")
            else:
                # Everything painted from now on must fit inside 20 rows.
                too_far = [r for r in rows if r > 20]
                if too_far:
                    failures.append(
                        f"addressed row {max(too_far)} after shrinking to 20 rows"
                    )

        # Grow well past the original size: 20x60 -> 45x140.
        set_winsize(master, 45, 140)
        os.killpg(os.getpgid(proc.pid), signal.SIGWINCH)
        grown = drain(master, 2.0)
        if proc.poll() is not None:
            failures.append(f"jot died on grow (rc={proc.returncode})")
        elif max_row_addressed(grown) < 40:
            failures.append(
                "the frame did not follow the grown window "
                f"(max row {max_row_addressed(grown)}, expected >= 40)"
            )

        # A short burst of resizes, as a drag produces, must not desync or die.
        # Each step's repaint lands in its own drain window, so the burst output
        # is accumulated: what matters is that the final size was painted.
        burst = bytearray()
        final_rows = 0
        for rows, cols in ((41, 120), (38, 110), (42, 130)):
            set_winsize(master, rows, cols)
            os.killpg(os.getpgid(proc.pid), signal.SIGWINCH)
            burst += drain(master, 0.5)
            final_rows = rows
        burst += drain(master, 1.0)
        if proc.poll() is not None:
            failures.append(f"jot died during a resize burst (rc={proc.returncode})")
        else:
            rows_seen = painted_rows(bytes(burst))
            if not rows_seen:
                failures.append("no repaint after a resize burst")
            elif final_rows not in rows_seen:
                # The bottom row is addressed by every full paint, so seeing it
                # proves the frame followed the last size of the burst.
                failures.append(
                    f"the burst's final size ({final_rows} rows) was never painted "
                    f"(max row {max(rows_seen)})"
                )

        # Resizing must not blank the screen: the old path called
        # UI::invalidate() per step, which emits a full clear and is what made a
        # live drag flash. Everything after startup is expected to leave the
        # screen in place and repaint over it.
        resize_clears = len(SCREEN_CLEAR.findall(shrunk + grown + bytes(burst)))
        if resize_clears > clears_at_start:
            failures.append(
                f"{resize_clears - clears_at_start} full-screen clear(s) during "
                "resizes (a live resize should repaint, not blank)"
            )
    finally:
        if proc.poll() is None:
            os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
        os.close(master)
        subprocess.run(["rm", "-rf", str(work)], check=False)

    if failures:
        for failure in failures:
            print(f"resize probe: FAIL — {failure}", file=sys.stderr)
        return 1
    print("resize probe: PASS — the frame followed shrink, grow and a resize burst")
    return 0


if __name__ == "__main__":
    sys.exit(main())
