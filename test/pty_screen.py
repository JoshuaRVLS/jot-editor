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

# The real CSI grammar: parameter bytes (0x30-0x3f, which also covers colon
# subparameters like SGR 4:3 and 58:5:n, and private prefixes like the kitty
# protocol's `(ESC [ > 1 u`), then optional intermediate bytes, then the final
# byte. Anything narrower silently swallows the sequences it does not know: the
# old [0-9;:? ]* pattern never matched `(ESC [ > 1 u`, so the sequence stayed
# "unfinished" and every byte after it was withheld from the screen.
CSI = re.compile(rb"\x1b\[([0-?]*)([ -/]*)([@-~])")


class Screen:
    """A cell grid fed from a terminal byte stream."""

    def __init__(self, cols: int, rows: int):
        self.cols = cols
        self.rows = rows
        self.cells = [[" "] * cols for _ in range(rows)]
        # Background colour per cell (a palette index, or -1 for "terminal
        # default"). Tracked so a probe can tell one region's fill from another's
        # -- the bottom bar is meant to carry the status line's background, and
        # that is invisible in the text alone.
        self.bg = [[-1] * cols for _ in range(rows)]
        # Underline style per cell (0 none, 1 straight, 3 wavy), tracked the way
        # bg is so a probe can read *which* cells carry an underline -- the
        # Ctrl+hover affordance, for one, is only visible this way.
        self.underline = [[0] * cols for _ in range(rows)]
        # Every byte read after the child started, for the checks that are about
        # the escape stream itself rather than the reconstructed screen (e.g. a
        # colour the theme only emits as 38;2 rather than a palette index).
        self.raw = bytearray()
        self.cur_bg = -1
        self.cur_underline = 0
        self.x = 0
        self.y = 0
        # DECSC/DECRC (ESC 7 / ESC 8): the editor parks the cursor far away to
        # probe the terminal and restores it, so a reconstruction that ignores
        # the save keeps writing at the parked cell and loses that text.
        self.saved_xy = None
        # An escape sequence can be split across reads; whatever follows a lone
        # ESC is kept here until the rest arrives, or it would print as text.
        self.pending = b""

    def feed(self, data: bytes) -> None:
        self.raw.extend(data)
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
                        self._csi((m.group(1) + m.group(2)).decode(), m.group(3).decode())
                        i = m.end()
                        continue
                    # A later escape means this one never got its final byte:
                    # the sequence was truncated (the editor writes frames with
                    # one write() per frame, and a pty that could not take it all
                    # leaves a partial sequence behind). Holding it would
                    # withhold every byte after it, so drop the fragment and
                    # resume at the next escape.
                    resume = data.find(b"\x1b", i + 2)
                    if resume >= 0:
                        i = resume
                        continue
                    # Otherwise this is a sequence split across reads: hold the
                    # whole thing, however long. One cell can carry a colour, a
                    # background, an underline style and its colour at once (an
                    # inlay hint does), so a small bound would print the tail of
                    # a long SGR as text and shift every following cell.
                    if len(data) - i < 4096:
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
                if nxt == ord("7"):
                    self.saved_xy = (self.x, self.y)
                elif nxt == ord("8") and self.saved_xy is not None:
                    self.x, self.y = self.saved_xy
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
                self.bg[self.y][self.x] = self.cur_bg
                self.underline[self.y][self.x] = self.cur_underline
            self.x += 1
            i += length

    def _sgr(self, params: str) -> None:
        # A wavy underline is SGR 4:3, a colon subparameter that the integer
        # split below drops, so read that form off the raw groups first.
        if not params:
            self.cur_underline = 0
        for group in params.split(";"):
            if group.startswith("4:") and group[2:].isdigit():
                self.cur_underline = int(group[2:])
        args = [int(p) for p in params.split(";") if p.isdigit()] if params else [0]
        if not args:
            self.cur_bg = -1
            return
        # The underline is handled in this walk and not by scanning the groups,
        # because "24" is both underline-off and the blue channel of a
        # truecolour background -- the consumption below is what tells them
        # apart (48;2;30;27;24 must not read as a reset).
        k = 0
        while k < len(args):
            a = args[k]
            if a == 0:
                self.cur_bg = -1
                self.cur_underline = 0
            elif a == 24:
                self.cur_underline = 0
            elif a == 4:
                self.cur_underline = 1
            elif a == 49:
                self.cur_bg = -1
            elif a == 48 and k + 2 < len(args) and args[k + 1] == 5:
                self.cur_bg = args[k + 2]
                k += 2
            elif a == 48 and k + 4 < len(args) and args[k + 1] == 2:
                # Truecolour: keep the channels, tagged so it cannot be mistaken
                # for a palette index.
                self.cur_bg = 1000 + ((args[k + 2] << 16) | (args[k + 3] << 8) | args[k + 4])
                k += 4
            k += 1

    def _csi(self, params: str, final: str) -> None:
        if final == "m":
            self._sgr(params)
            return
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
                self.bg = [[-1] * self.cols for _ in range(self.rows)]
                self.underline = [[0] * self.cols for _ in range(self.rows)]
            elif mode == 0:
                for cx in range(self.x, self.cols):
                    self.cells[self.y][cx] = " "
                    self.bg[self.y][cx] = self.cur_bg
                    self.underline[self.y][cx] = self.cur_underline
                for cy in range(self.y + 1, self.rows):
                    self.cells[cy] = [" "] * self.cols
                    self.bg[cy] = [-1] * self.cols
                    self.underline[cy] = [0] * self.cols
        elif final == "K":
            mode = args[0] if args else 0
            if mode == 0:
                for cx in range(self.x, self.cols):
                    self.cells[self.y][cx] = " "
                    self.bg[self.y][cx] = self.cur_bg
            elif mode == 2:
                self.cells[self.y] = [" "] * self.cols
                self.bg[self.y] = [-1] * self.cols

    def text(self) -> str:
        return "\n".join("".join(row).rstrip() for row in self.cells)

    def underline_runs(self, row: int):
        """[(start_col, end_col, text), ...] for the underlined cells of a row."""
        runs = []
        col = 0
        while col < self.cols:
            if not self.underline[row][col]:
                col += 1
                continue
            start = col
            text = ""
            while col < self.cols and self.underline[row][col]:
                text += self.cells[row][col]
                col += 1
            runs.append((start, col, text.rstrip()))
        return runs


def run_in_pty(binary: str, args, keys: bytes, settle: float = 2.5, after: float = 3.0,
               cols: int = 100, rows: int = 30, cfg: str = "/tmp/jot_probe_cfg",
               cwd: str = None, phases=None):
    """Runs `binary args...` in a pty, sends `keys`, and returns the Screen.

    `settle` is how long the editor gets to start before the keys are sent (LSP
    servers need seconds to attach), `after` how long to keep reading afterwards.
    `cwd` sets the child's working directory, which is what decides the workspace
    root the file explorer and telescope open on.

    `phases` sends further input once the screen has settled: a list of
    (delay, bytes) pairs, each written after draining for `delay` seconds and
    followed by a short drain so its frame arrives. A probe that needs the UI to
    finish something asynchronous first -- a mouse hover over a picker whose file
    scan is still running, say -- uses this instead of racing one key blob.
    """
    os.makedirs(cfg, exist_ok=True)
    # Resolve before forking: the child may chdir to `cwd`, and a relative
    # binary path would then no longer point at anything.
    binary = os.path.abspath(binary)
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.environ["COLORTERM"] = "truecolor"
        os.environ["JOT_CONFIG_HOME"] = cfg
        os.environ["JOT_CACHE_HOME"] = cfg
        try:
            if cwd:
                os.chdir(cwd)
            os.execv(binary, [binary] + list(args))
        except OSError:
            os._exit(127)

    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
    screen = Screen(cols, rows)
    # Drain while waiting for the editor to settle instead of sleeping through
    # it: a real terminal reads as the frames arrive, and a stalled reader fills
    # the pty -- the editor then can't finish a frame, drops its tail and never
    # repaints those cells, so a probe that slept here measured a corrupted
    # screen it had caused itself.
    settle_end = time.time() + settle
    while time.time() < settle_end:
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
    def drain(seconds: float) -> None:
        end = time.time() + seconds
        while time.time() < end:
            r, _, _ = select.select([fd], [], [], 0.1)
            if not r:
                continue
            try:
                data = os.read(fd, 65536)
            except OSError:
                return
            if not data:
                return
            screen.feed(data)

    try:
        os.write(fd, keys)
    except OSError:
        pass
    drain(after)
    for delay, extra in phases or []:
        drain(delay)
        try:
            os.write(fd, extra)
        except OSError:
            pass
        drain(0.4)
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
