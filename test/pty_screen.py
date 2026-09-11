"""Terminal-screen reconstruction for the pty probes.

jot renders by absolute cursor positioning with row diffs, so the raw byte stream
is not readable as text: the same screen arrives as cursor moves plus short runs.
These probes need to ask "what was actually on screen after this keypress", which
means replaying the stream into a cell grid.

Only the sequences jot emits are handled: CUP/relative cursor moves, erase in
line/display, and printable text (UTF-8, including the nerd-font icons). That is
enough for the editor's own output; it is not a complete terminal emulator.
"""
from __future__ import annotations

import fcntl
import os
import pty
import re
import select
import signal
import struct
import termios
import time

CSI = re.compile(rb"\x1b\[([0-9;? ]*)([a-zA-Z])")


class Screen:
    """A cell grid fed from a terminal byte stream."""

    def __init__(self, cols: int, rows: int):
        self.cols = cols
        self.rows = rows
        self.cells = [[" "] * cols for _ in range(rows)]
        self.x = 0
        self.y = 0
        # An escape sequence can be split across reads; whatever follows a lone
        # ESC is kept here until the rest arrives, or it would print as text.
        self.pending = b""

    def feed(self, data: bytes) -> None:
        data = self.pending + data
        self.pending = b""
        i = 0
        while i < len(data):
            b = data[i]
            if b == 0x1B:
                if i + 1 >= len(data):
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
                i += 2  # two-byte escape (charset select, keypad mode, ...)
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


def run_in_pty(binary: str, args, keys: bytes, settle: float = 2.5, after: float = 3.0,
               cols: int = 100, rows: int = 30, cfg: str = "/tmp/jot_probe_cfg"):
    """Runs `binary args...` in a pty, sends `keys`, and returns the Screen.

    `settle` is how long the editor gets to start before the keys are sent (LSP
    servers need seconds to attach), `after` how long to keep reading afterwards.
    """
    os.makedirs(cfg, exist_ok=True)
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.environ["COLORTERM"] = "truecolor"
        os.environ["JOT_CONFIG_HOME"] = cfg
        os.environ["JOT_CACHE_HOME"] = cfg
        try:
            os.execv(binary, [binary] + list(args))
        except OSError:
            os._exit(127)

    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
    screen = Screen(cols, rows)
    time.sleep(settle)
    try:
        os.write(fd, keys)
    except OSError:
        pass
    end = time.time() + after
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
