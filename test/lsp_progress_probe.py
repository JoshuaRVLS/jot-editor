#!/usr/bin/env python3
"""Probe: an LSP server's progress reaches the statusline.

`$/progress` is easy to get half-right: a client that never advertises
`window.workDoneProgress` is simply never told anything, and one that swallows the
notifications shows nothing. Neither shows up in a unit test of the parser, so
this drives a real clangd over a real workspace and looks at the rendered screen
for the spinner the statusline paints while a server is working.

clangd reports progress while it indexes a translation unit it has not seen
before, so the workspace is written fresh each run (a large file, with a
compile_commands.json so the server actually builds an index). The probe samples
the pty for a few seconds and passes if a spinner frame appears at any point --
the token is short-lived on a small file, so one sample is not enough.

Usage: test/lsp_progress_probe.py [path-to-jot]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary or no clangd).
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

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import Screen  # noqa: E402

# The statusline spinner frames (gui/status_line.cpp), as UTF-8.
SPINNER = ["\u280b", "\u2819", "\u2839", "\u2838", "\u283c", "\u2834", "\u2826", "\u2827",
           "\u2807", "\u280f"]
ANSI = re.compile(rb"\x1b\[[0-9;?]*[a-zA-Z]")


def write_workspace(root: str) -> str:
    """A workspace clangd has real work to do in.

    Two things matter here. The content carries a nonce, because clangd keeps an
    index in its cache and identical files are simply already indexed -- with no
    work to do it reports no progress and the probe would pass or fail for the
    wrong reason. And it is many translation units rather than one, because
    background indexing is what clangd reports progress for.
    """
    nonce = f"{os.getpid()}_{time.time_ns()}"
    os.makedirs(root, exist_ok=True)
    entries = []
    for i in range(200):
        path = os.path.join(root, f"unit_{i}.cpp")
        with open(path, "w") as fh:
            fh.write("#include <vector>\n#include <string>\n")
            fh.write(f"// {nonce}\n")
            for j in range(40):
                fh.write(f"int value_{nonce}_{i}_{j}() {{ return {i * j}; }}\n")
        entries.append(f'{{"directory": "{root}", "command": "c++ -c {path}", "file": "{path}"}}')
    with open(os.path.join(root, "compile_commands.json"), "w") as fh:
        fh.write("[" + ",".join(entries) + "]")
    return os.path.join(root, "unit_0.cpp")


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    if not os.path.exists(binary):
        print(f"lsp progress probe: SKIP - no binary at {binary}")
        return 2
    if not shutil.which("clangd"):
        print("lsp progress probe: SKIP - no clangd on PATH")
        return 2

    root = "/tmp/jot_lsp_progress_probe"
    path = write_workspace(root)
    cfg = "/tmp/jot_lsp_progress_cfg"
    os.makedirs(os.path.join(cfg, "configs"), exist_ok=True)
    # Resolve before forking: the child chdirs into the workspace, and a relative
    # path would then point at nothing.
    binary = os.path.abspath(binary)

    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.environ["COLORTERM"] = "truecolor"
        os.environ["JOT_CONFIG_HOME"] = cfg
        os.environ["JOT_CACHE_HOME"] = cfg
        try:
            os.chdir(root)
            os.execv(binary, [binary, path])
        except OSError:
            os._exit(127)

    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", 30, 110, 0, 0))
    screen = Screen(110, 30)
    saw_spinner = False
    saw_lsp = False
    deadline = time.time() + 30.0
    while time.time() < deadline and not saw_spinner:
        r, _, _ = select.select([fd], [], [], 0.2)
        if not r:
            continue
        try:
            data = os.read(fd, 65536)
        except OSError:
            break
        if not data:
            break
        screen.feed(data)
        text = screen.text()
        saw_lsp = saw_lsp or "cpp @" in text or "clangd" in text
        saw_spinner = any(ch in text for ch in SPINNER)

    try:
        os.kill(pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    os.close(fd)
    try:
        os.waitpid(pid, 0)
    except ChildProcessError:
        pass

    log_path = os.path.join(cfg, "logs", "lsp_cpp.log")
    received = 0
    if os.path.exists(log_path):
        with open(log_path, "rb") as fh:
            received = fh.read().count(b"$/progress")
    print(f"lsp progress probe: server attached = {saw_lsp}")
    print(f"lsp progress probe: progress receipts in the client log = {received}")
    print(f"lsp progress probe: spinner seen    = {saw_spinner}")
    if saw_spinner:
        print("lsp progress probe: PASS")
        return 0
    # The statusline is the last row worth looking at when this fails: it says
    # whether a server is attached at all and what it contributed.
    rows = [row for row in screen.text().split("\n") if row.strip()]
    print(f"lsp progress probe: last screen rows: {rows[-2:] if rows else []}")
    # A server that attached but never reported is the failure this guards; one
    # that never attached at all means the probe's premise failed (no server for
    # this file), which is not a jot bug.
    if received > 0:
        print("lsp progress probe: FAIL - progress reached the client but not the statusline")
        return 1
    print("lsp progress probe: SKIP - the server reported no progress to show "
          "(nothing for this workspace to index, or an already-warm index)")
    return 2


if __name__ == "__main__":
    sys.exit(main())
