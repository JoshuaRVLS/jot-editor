#!/usr/bin/env python3
"""End-to-end check: does a live clangd's labelDetails reach the completion popup?

The richer completion rows are built from `labelDetails`, which the spec only
sends when the client advertises labelDetailsSupport. This drives the real
binary against the real clangd, reconstructs the screen from the terminal
stream, and inspects the completion popup itself.

The source never mentions the parameter list clangd sends (it lives only in
labelDetails.detail), so seeing it in the popup proves the capability, the parse
and the plumbing all work against a live server. The include path is reported
too, but not asserted: clangd versions differ on whether it lands in
labelDetails.description or in the documentation.

Usage: test/completion_label_probe.py [path-to-jot-binary]
Set JOT_PROBE_DUMP=1 to print the captured screen.
Exit codes: 0 pass, 1 fail, 2 binary or clangd missing.
"""
from __future__ import annotations

import fcntl
import os
import pty
import re
import select
import shutil
import signal
import struct
import sys
import termios
import time

# Completing "pri" offers printf, whose parameter list and include path clangd
# sends in labelDetails rather than in the label.
SOURCE = """#include <cstdio>

int main() {
  pri
}
"""

CSI = re.compile(rb"\x1b\[([0-9;? ]*)([a-zA-Z])")


class Screen:
    """Just enough of a terminal to reconstruct what was on screen.

    jot writes absolute cursor moves plus text runs, so positioning, erasing and
    printable text cover everything the popup involves.
    """

    def __init__(self, cols: int, rows: int):
        self.cols = cols
        self.rows = rows
        self.cells = [[" "] * cols for _ in range(rows)]
        self.x = 0
        self.y = 0
        # An escape sequence can be split across reads; whatever follows a lone
        # ESC is kept here until the rest arrives, or it would be printed as text.
        self.pending = b""

    def feed(self, data: bytes) -> None:
        data = self.pending + data
        self.pending = b""
        i = 0
        while i < len(data):
            b = data[i]
            if b == 0x1B:
                if i + 1 >= len(data):
                    # A lone ESC at the end of a read: the rest is still coming.
                    self.pending = data[i:]
                    return
                nxt = data[i + 1]
                if nxt == ord("["):
                    m = CSI.match(data, i)
                    if m:
                        self._csi(m.group(1).decode(), m.group(2).decode())
                        i = m.end()
                        continue
                    if len(data) - i < 32:
                        self.pending = data[i:]
                        return
                    i += 2
                    continue
                if nxt == ord("]"):
                    # OSC: runs to BEL or ST.
                    end = data.find(b"\x07", i + 2)
                    st = data.find(b"\x1b\\", i + 2)
                    if end == -1 and st == -1:
                        if len(data) - i < 512:
                            self.pending = data[i:]
                            return
                        i += 2
                        continue
                    i = (end + 1) if (end != -1 and (st == -1 or end < st)) else (st + 2)
                    continue
                # Two-byte escape (charset select, keypad mode, ...).
                i += 2
                continue
            if b == 0x0D:
                self.x = 0
                i += 1
                continue
            if b == 0x0A:
                self.y = min(self.rows - 1, self.y + 1)
                i += 1
                continue
            if b < 0x20:
                i += 1
                continue
            length = 1
            if b >= 0xF0:
                length = 4
            elif b >= 0xE0:
                length = 3
            elif b >= 0xC0:
                length = 2
            try:
                ch = data[i : i + length].decode("utf-8")
            except UnicodeDecodeError:
                ch = "?"
            if self.y < self.rows and self.x < self.cols:
                self.cells[self.y][self.x] = ch
            self.x += 1
            i += length

    def _csi(self, params: str, final: str) -> None:
        args = [int(p) for p in params.split(";") if p.isdigit()] if params else []
        if final in ("H", "f"):
            self.y = max(0, (args[0] if args else 1) - 1)
            self.x = max(0, (args[1] if len(args) > 1 else 1) - 1)
        elif final == "A":
            self.y = max(0, self.y - (args[0] if args else 1))
        elif final == "B":
            self.y = min(self.rows - 1, self.y + (args[0] if args else 1))
        elif final == "C":
            self.x = min(self.cols - 1, self.x + (args[0] if args else 1))
        elif final == "D":
            self.x = max(0, self.x - (args[0] if args else 1))
        elif final == "J":
            mode = args[0] if args else 0
            if mode == 2:
                self.cells = [[" "] * self.cols for _ in range(self.rows)]
            elif mode == 0:
                for cx in range(self.x, self.cols):
                    self.cells[self.y][cx] = " "
                for cy in range(self.y + 1, self.rows):
                    self.cells[cy] = [" "] * self.cols
        elif final == "K":
            mode = args[0] if args else 0
            if mode == 0:
                for cx in range(self.x, self.cols):
                    self.cells[self.y][cx] = " "
            elif mode == 2:
                self.cells[self.y] = [" "] * self.cols

    def text(self) -> str:
        return "\n".join("".join(row).rstrip() for row in self.cells)


def run_jot(binary: str, path: str, seconds: float, cols: int, rows: int):
    cfg = "/tmp/jot_completion_probe_cfg"
    os.makedirs(cfg, exist_ok=True)
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.environ["COLORTERM"] = "truecolor"
        os.environ["JOT_CONFIG_HOME"] = cfg
        os.environ["JOT_CACHE_HOME"] = cfg
        try:
            os.execv(binary, [binary, path])
        except OSError:
            os._exit(127)

    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
    screen = Screen(cols, rows)
    # clangd needs a moment to attach before it answers a completion request.
    time.sleep(6.0)
    try:
        # jot is modeless, so navigation is by shortcut: Alt+Shift+G to the end
        # of the file, Up one line (the "pri" line), End to sit after "pri".
        os.write(fd, b"\x1bG")
        time.sleep(0.4)
        os.write(fd, b"\x1b[A")
        time.sleep(0.3)
        os.write(fd, b"\x1b[F")
        time.sleep(0.3)
        os.write(fd, b"\x00")   # Ctrl+Space
    except OSError:
        pass
    end = time.time() + seconds
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.1)
        if not r:
            continue
        try:
            data = os.read(fd, 65536)
        except OSError:
            break
        if not data:
            break
        screen.feed(data)
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
    return screen


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    if not os.path.exists(binary):
        print(f"completion label probe: SKIP - no binary at {binary}")
        return 2
    if shutil.which("clangd") is None:
        print("completion label probe: SKIP - clangd not installed")
        return 2

    work = "/tmp/jot_completion_probe"
    os.makedirs(work, exist_ok=True)
    path = os.path.join(work, "probe.cpp")
    with open(path, "w") as fh:
        fh.write(SOURCE)

    screen = run_jot(binary, path, seconds=8.0, cols=110, rows=34)
    view = screen.text()
    if os.environ.get("JOT_PROBE_DUMP"):
        print(view)
        print("-" * 60)

    saw_label = "printf" in view
    saw_params = "(const char *" in view
    saw_include = "stdio.h" in view

    print(f"completion label probe: label on screen      = {saw_label}")
    print(f"completion label probe: labelDetails params  = {saw_params}")
    print(f"completion label probe: include path (info)  = {saw_include}")
    if not saw_label:
        print("completion label probe: FAIL - the popup never appeared on screen")
        return 1
    if not saw_params:
        print("completion label probe: FAIL - clangd's labelDetails.detail never reached the row")
        return 1
    print("completion label probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
