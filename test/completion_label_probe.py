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

import os
import shutil
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

# Completing "pri" offers printf, whose parameter list and include path clangd
# sends in labelDetails rather than in the label.
SOURCE = """#include <cstdio>

int main() {
  pri
}
"""

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

    # Point the caret at the completion site before asking: Alt+Shift+G is the
    # end of the file, Up the "pri" line, End the end of that line.
    screen = run_in_pty(binary,
                        [path],
                        b"\x1bG\x1b[A\x1b[F\x00",
                        settle=6.0,
                        after=8.0,
                        cols=110,
                        rows=34,
                        cfg="/tmp/jot_completion_probe_cfg")
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
