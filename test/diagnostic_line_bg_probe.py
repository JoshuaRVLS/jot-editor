#!/usr/bin/env python3
"""Probe: error and warning rows are banded edge to edge, and the message is
the message alone.

A unit test can read the cell grid, but this is a claim about the terminal: a
server's finding must tint the *whole* row -- the line number, the code, and
the empty space out to the pane's right edge -- not only the cells under the
squiggle, and the end-of-line message must start with the message: no severity
icon in front of it, which is what the row's own colours (the squiggle, the
number, the band) already say.

This drives the real binary against a scripted `clangd` (a shim on PATH) that
publishes one error on line 1, one warning on line 3 and nothing on line 2,
under a probe theme whose bands are unmistakable palette colours (52 and 58
against a 0 background). The screen is then read back cell by cell.

Scenes:
  * the error row wears the error band from its line number out past the end of
    its text, and the inline message sits on that band,
  * the warning row wears the warning band, not the error's,
  * the untouched row wears neither,
  * no severity icon (U+F057 / U+F071 / U+F05A / U+F0EB) leads the message:
    the text starts two cells after the code, where the icon used to sit.

Usage: test/diagnostic_line_bg_probe.py [path-to-jot] [--dump]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary).
"""
from __future__ import annotations

import os
import shutil
import stat
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

COLS, ROWS = 110, 24
# The probe theme's bands, as palette indices: the background is index 0, so a
# band cell is unambiguous.
ERROR_BG, WARNING_BG, PLAIN_BG = 52, 58, 0
# The four marks the module used to lead each message with (see
# runtime/lua/features/decorations.lua): times-circle, exclamation-triangle,
# info-circle, lightbulb.
MARKS = ("\uf057", "\uf071", "\uf05a", "\uf0eb")

ERROR_TEXT = "PROBE-ERROR-ONE"
WARNING_TEXT = "PROBE-WARN-TWO"
CLEAN_LINE, ERROR_LINE, WARNING_LINE = "int fine = 2;", "int broken = 1;", "int warned = 3;"

SOURCE = ERROR_LINE + "\n" + CLEAN_LINE + "\n" + WARNING_LINE + "\n"

# The probe theme: a plain palette, so the pane background is index 0, plus the
# two bands. Everything else stays on what jot sets.
THEME = """{
  "extends": "jot",
  "Normal": {"fg": 7, "bg": 0},
  "DiagnosticError": {"bg": 52},
  "DiagnosticWarn": {"bg": 58}
}
"""

# The scripted server: a full LSP handshake, then one publish carrying the two
# findings the probe reads. `character` columns are byte columns in JOT, and
# the ranges only have to cover a cell each.
FAKE_CLANGD = r'''#!/usr/bin/env python3
import json, os, sys

if "--version" in sys.argv:
    print("fake clangd 1.0.0")
    sys.exit(0)


def send(obj):
    body = json.dumps(obj).encode()
    sys.stdout.buffer.write(b"Content-Length: %d\r\n\r\n" % len(body) + body)
    sys.stdout.buffer.flush()


def publish(uri):
    send({"jsonrpc": "2.0", "method": "textDocument/publishDiagnostics",
          "params": {"uri": uri, "diagnostics": [
              {"range": {"start": {"line": 0, "character": 0},
                         "end": {"line": 0, "character": 1}},
               "severity": 1, "message": "PROBE-ERROR-ONE"},
              {"range": {"start": {"line": 2, "character": 0},
                         "end": {"line": 2, "character": 1}},
               "severity": 2, "message": "PROBE-WARN-TWO"},
          ]}})


EMPTY = {
    "textDocument/completion": {"isIncomplete": False, "items": []},
    "textDocument/documentSymbol": [],
    "textDocument/inlayHint": [],
    "textDocument/codeAction": [],
    "textDocument/references": [],
    "textDocument/formatting": [],
}

def read_more():
    chunk = os.read(0, 65536)
    if not chunk:
        sys.exit(0)
    return chunk


pending = b""
while True:
    while b"\r\n\r\n" not in pending:
        pending += read_more()
    header, pending = pending.split(b"\r\n\r\n", 1)
    length = 0
    for line in header.split(b"\r\n"):
        if line.lower().startswith(b"content-length:"):
            length = int(line.split(b":", 1)[1])
    while len(pending) < length:
        pending += read_more()
    body, pending = pending[:length], pending[length:]
    msg = json.loads(body)
    method = msg.get("method")

    if method == "initialize":
        send({"jsonrpc": "2.0", "id": msg["id"], "result": {"capabilities": {
            "textDocumentSync": {"openClose": True, "change": 1},
        }}})
    elif method == "shutdown":
        send({"jsonrpc": "2.0", "id": msg["id"], "result": None})
    elif method == "exit":
        sys.exit(0)
    elif method == "textDocument/didOpen":
        publish(msg["params"]["textDocument"]["uri"])
    elif "id" in msg:
        send({"jsonrpc": "2.0", "id": msg["id"],
              "result": EMPTY.get(method)})
'''


def workspace(tmp: str) -> str:
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(os.path.join(tmp, "configs", "colors"))
    os.makedirs(os.path.join(tmp, "bin"))
    os.makedirs(os.path.join(tmp, "data"))
    with open(os.path.join(tmp, "configs", "settings.conf"), "w") as fh:
        fh.write("# jot configuration file\n\ncolor_scheme=diagbgprobe\n")
    with open(os.path.join(tmp, "configs", "colors", "diagbgprobe.json"), "w") as fh:
        fh.write(THEME)
    shim = os.path.join(tmp, "bin", "clangd")
    with open(shim, "w") as fh:
        fh.write(FAKE_CLANGD)
    os.chmod(shim, os.stat(shim).st_mode | stat.S_IEXEC)
    path = os.path.join(tmp, "probe.cpp")
    with open(path, "w") as fh:
        fh.write(SOURCE)
    return path


def row_col_of(screen, needle: str):
    """The 0-based (row, col) of `needle` on the reconstructed screen."""
    for row, line in enumerate(screen.text().split("\n")):
        col = line.find(needle)
        if col >= 0:
            return row, col
    return None


def band_run(row_bgs, color: int):
    """The longest run of `color` in a row: (start, end) columns, or None."""
    best = None
    start = None
    for col, value in enumerate(list(row_bgs) + [None]):
        if value == color and start is None:
            start = col
        elif value != color and start is not None:
            if best is None or col - start > best[1] - best[0]:
                best = (start, col)
            start = None
    return best


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    dump = "--dump" in sys.argv
    if not os.path.exists(binary):
        print("diagnostic line bg probe: SKIP - no binary at %s" % binary)
        return 2

    tmp = "/tmp/jot_diagnostic_line_bg_probe"
    path = workspace(tmp)
    backup = {
        "PATH": os.environ.get("PATH", ""),
        "XDG_DATA_HOME": os.environ.get("XDG_DATA_HOME", ""),
    }
    os.environ["PATH"] = os.path.join(tmp, "bin") + os.pathsep + backup["PATH"]
    # A managed clangd install must not shadow the shim.
    os.environ["XDG_DATA_HOME"] = os.path.join(tmp, "data")
    try:
        screen = run_in_pty(binary, [path], b"", settle=3.0, after=1.5,
                            cfg=tmp, cwd=tmp, cols=COLS, rows=ROWS,
                            phases=[(0.0, lambda s: ERROR_TEXT in s.text())],
                            until_timeout=25.0)
    finally:
        for key, value in backup.items():
            os.environ[key] = value

    if dump:
        print(screen.text())
        print("-" * 70)

    failures = []
    lines = screen.text().split("\n")
    marks = {name: (row_col_of(screen, name), name) for name in
             (ERROR_LINE, CLEAN_LINE, WARNING_LINE)}
    if any(cell is None for cell, _ in marks.values()):
        print("diagnostic line bg probe: FAIL - the probe file is not on screen")
        return 1

    err_row, err_col = marks[ERROR_LINE][0]
    clean_row, clean_col = marks[CLEAN_LINE][0]
    warn_row, warn_col = marks[WARNING_LINE][0]

    # The error row: one run of the error band that starts at the line number's
    # own cell (two cells left of the code) and reaches well past the text.
    err_run = band_run(screen.bg[err_row], ERROR_BG)
    if err_run is None:
        failures.append("the error row carries no error band at all")
    else:
        start, end = err_run
        if start > err_col - 2:
            failures.append("the error band starts at %d, right of the line "
                            "number (code at %d): the gutter is not covered"
                            % (start, err_col))
        if end < err_col + 30:
            failures.append("the error band ends at %d, only %d cells past the "
                            "start of the code: the row is not covered to its "
                            "end" % (end, end - err_col))

    # The warning row wears its own band, not the error's.
    warn_run = band_run(screen.bg[warn_row], WARNING_BG)
    if warn_run is None:
        failures.append("the warning row carries no warning band at all")
    else:
        start, end = warn_run
        if start > warn_col - 2:
            failures.append("the warning band starts at %d, right of the line "
                            "number (code at %d)" % (start, warn_col))
        if end < warn_col + 30:
            failures.append("the warning band ends at %d, only %d cells past "
                            "the start of the code" % (end, end - warn_col))
    if band_run(screen.bg[warn_row], ERROR_BG) is not None:
        failures.append("the warning row is painted with the error band")

    # The untouched row wears neither band, anywhere along it.
    for color, name in ((ERROR_BG, "error"), (WARNING_BG, "warning")):
        if band_run(screen.bg[clean_row], color) is not None:
            failures.append("the clean row carries the %s band" % name)

    # The inline message: it must be on the row it describes, on that row's
    # band, and it must begin two cells after the code -- no icon in front.
    msg_col = lines[err_row].find(ERROR_TEXT)
    if msg_col < 0:
        failures.append("the inline message is not on the error row")
    else:
        if msg_col != err_col + len(ERROR_LINE) + 2:
            failures.append("the message starts at %d, expected %d: the text "
                            "between the code and the message is not the two "
                            "cells the virtual text leads with"
                            % (msg_col, err_col + len(ERROR_LINE) + 2))
        if screen.bg[err_row][msg_col] != ERROR_BG:
            failures.append("the message sits on %d, not the error band: it "
                            "punches a hole through the row"
                            % screen.bg[err_row][msg_col])
        if lines[err_row][msg_col - 2] != " ":
            failures.append("a glyph sits two cells before the message (that is "
                            "where a severity icon would be)")
        for mark in MARKS:
            if mark in lines[err_row]:
                failures.append("a severity icon (%s) is drawn on the message "
                                "row" % repr(mark))
    if WARNING_TEXT not in lines[warn_row]:
        failures.append("the warning's message is not on the warning row")

    if failures:
        for failure in failures:
            print("diagnostic line bg probe: FAIL - %s" % failure)
        return 1
    print("diagnostic line bg probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
