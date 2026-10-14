#!/usr/bin/env python3
"""Probe: a session ended by a signal still writes out what it holds.

SIGTERM, SIGINT and SIGHUP all mean the session is over (a service manager, a
script, a terminal being closed), and the editor's answer to them is to leave
through the same door a `:q` uses rather than dying where it stands. Three
things follow from that, and none of them can be seen without a real run:

  * the process exits *normally* -- a reaped child that was signalled is
    WIFSIGNALED, and the default action is exactly what this replaces;
  * the day's coding total reaches `configs/coding_time.tsv`. It is written once
    a minute while the session runs, and that first write lands in the first
    second, so a killed session used to leave a few dozen milliseconds behind
    instead of the minutes actually coded -- which is what the chip then showed
    on the next launch;
  * the terminal is handed back (the alternate screen is left, so the shell
    prompt is not painted into a screen the editor still owns).

The store is checked for the workspace the editor was opened on, under today's
date, because that is the key a next launch reads.

Usage: test/signal_exit_probe.py [path-to-jot]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary).
"""
from __future__ import annotations

import fcntl
import os
import pty
import select
import shutil
import signal
import struct
import sys
import termios
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import Screen  # noqa: E402

COLS, ROWS = 120, 30
# Long enough that the store's live window holds seconds rather than the
# millisecond the first flush catches, short enough to keep the probe quick.
RUN_SECONDS = 6.5
# How long the editor gets to leave after the signal before it counts as not
# leaving at all.
EXIT_TIMEOUT_S = 6.0
# The floor: a session killed mid-run used to leave under a tenth of a second of
# credit behind however long it had been open.
MIN_TOTAL_MS = 2500
# The ceiling: 6 s of a real run cannot be four minutes (the store's own idle cap
# is what a counter credited against the wrong clock banked per second).
MAX_TOTAL_MS = 20000
# Leaving the alternate screen, which is how a terminal is handed back.
LEAVE_ALT_SCREEN = "\x1b[?1049l"


def run_scene(binary: str, cfg: str, workspace: str, sig: int) -> tuple[str, str, int | None]:
    """Runs the editor on `workspace`, signals it, and reports what happened.

    Returns (dimension, detail, total_ms or None): `dimension` is the store file
    when the session wrote one.
    """
    os.makedirs(workspace, exist_ok=True)
    with open(os.path.join(workspace, "notes.txt"), "w") as fh:
        fh.write("alpha\nbeta\n")

    pid, fd = pty.fork()
    if pid == 0:
        os.environ.update(TERM="xterm-256color", COLORTERM="truecolor",
                          JOT_KITTY_PROTOCOL="1", JOT_CONFIG_HOME=cfg, JOT_CACHE_HOME=cfg)
        os.execv(binary, [binary, workspace])
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", ROWS, COLS, 0, 0))

    output = bytearray()
    screen = Screen(COLS, ROWS)
    start = time.monotonic()
    signalled_at = None
    exited_at = None
    status = None
    while True:
        r, _, _ = select.select([fd], [], [], 0.05)
        if r:
            try:
                data = os.read(fd, 65536)
            except OSError:
                data = b""
            if data:
                output += data
                screen.feed(data)
        elapsed = time.monotonic() - start
        if signalled_at is None and elapsed >= RUN_SECONDS:
            # The editor paints before the store has anything worth writing, so
            # the signal is sent once the session is really up.
            signalled_at = elapsed
            os.kill(pid, sig)
        if signalled_at is not None:
            done, wait_status = os.waitpid(pid, os.WNOHANG)
            if done == pid:
                exited_at = elapsed
                status = wait_status
                break
            if elapsed - signalled_at > EXIT_TIMEOUT_S:
                os.kill(pid, signal.SIGKILL)
                os.waitpid(pid, 0)
                break
        if elapsed > RUN_SECONDS + EXIT_TIMEOUT_S + 2:
            os.kill(pid, signal.SIGKILL)
            os.waitpid(pid, 0)
            break
    try:
        os.close(fd)
    except OSError:
        pass

    if exited_at is None:
        return "timeout", "the editor was killed by the probe, not by the signal", None
    if os.WIFSIGNALED(status):
        return "signalled", f"the signal killed it (SIG {os.WTERMSIG(status)})", None
    if not os.WIFEXITED(status) or os.WEXITSTATUS(status) != 0:
        return "exit status", f"the editor exited with status {status >> 8}", None
    if LEAVE_ALT_SCREEN not in output.decode("utf-8", "replace"):
        return "terminal", "the terminal was not handed back (no leave-alt-screen)", None

    store = os.path.join(cfg, "configs", "coding_time.tsv")
    if not os.path.exists(store):
        return "store", "no coding-time store was written", None
    want_key = os.path.realpath(workspace)
    with open(store) as fh:
        for line in fh:
            parts = line.rstrip("\n").split("\t")
            if len(parts) != 3 or parts[1] != time.strftime("%Y-%m-%d"):
                continue
            if os.path.realpath(parts[0]) != want_key:
                continue
            return "ok", f"{exited_at - signalled_at:.2f}s after the signal", int(parts[2])
    return "store", f"no line for {want_key} today", None


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    if not os.path.exists(binary):
        print(f"signal exit probe: SKIP - no binary at {binary}")
        return 2
    binary = os.path.abspath(binary)

    failures: list[str] = []
    root = "/tmp/jot_signal_exit_probe"
    for name, sig in (("term", signal.SIGTERM), ("hup", signal.SIGHUP)):
        cfg = f"{root}_{name}_cfg"
        workspace = f"{root}_{name}_ws"
        shutil.rmtree(cfg, ignore_errors=True)
        shutil.rmtree(workspace, ignore_errors=True)
        dimension, detail, total = run_scene(binary, cfg, workspace, sig)
        label = "SIGTERM" if sig == signal.SIGTERM else "SIGHUP"
        if dimension != "ok":
            failures.append(f"{label}: {dimension} - {detail}")
            continue
        if total is None or not (MIN_TOTAL_MS <= total <= MAX_TOTAL_MS):
            failures.append(
                f"{label}: the store holds {total} ms for the session, outside "
                f"{MIN_TOTAL_MS}..{MAX_TOTAL_MS} (a signalled session used to keep "
                f"only the first flush, a few dozen ms)")
            continue
        print(f"signal exit probe: ok - {label} left {total} ms behind, exited {detail}")

    if failures:
        for failure in failures:
            print(f"signal exit probe: FAIL - {failure}")
        return 1
    print("signal exit probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
