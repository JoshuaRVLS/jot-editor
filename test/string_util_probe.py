#!/usr/bin/env python3
"""Probe: a git popup marks its own truncation, and nothing else.

The shared helper (src/base/string_util.h) is pinned by test_string_util.cpp.
What only the real binary shows is the popup it feeds. `:gitstatus` runs
`limit_lines(status, 18)`, and that helper used to exist three times over, once
behind each popup that needed one; the copies disagreed on the shape that ends
on its own limit, so this walks both sides of it:
  * 17 changed files, 18 rows -- the last row on screen is a status row, and no
    row is a bare "...";
  * 25 changed files, 26 rows -- the popup stops at 18 and the "..." row is
    there, because those lines really are missing.

Usage: test/string_util_probe.py [path-to-jot] [--dump]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary or no git).
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

COLS, ROWS = 130, 40
ROOT = "/tmp/jot_string_util_probe"
CFG = "/tmp/jot_string_util_probe_cfg"
FILES = 40
LIMIT = 18
PALETTE = b"\x1b[112;6u"
ENTER = b"\r"


def git(*args) -> str:
    return subprocess.run(["git", *args], cwd=ROOT, check=True, capture_output=True,
                          text=True).stdout


def write_repo(changed: int) -> int:
    """A committed workspace with the first `changed` files touched after it."""
    shutil.rmtree(ROOT, ignore_errors=True)
    shutil.rmtree(CFG, ignore_errors=True)
    os.makedirs(ROOT)
    for i in range(FILES):
        with open(os.path.join(ROOT, "f%02d.cpp" % i), "w") as fh:
            fh.write("int value_%d = %d;\n" % (i, i))
    git("init", "-q")
    git("add", "-A")
    git("-c", "user.email=probe@jot", "-c", "user.name=probe", "commit", "-q", "-m", "base")
    for i in range(changed):
        with open(os.path.join(ROOT, "f%02d.cpp" % i), "a") as fh:
            fh.write("// touched\n")
    return len(git("status", "--short", "--branch").splitlines())


def status_popup(binary: str, changed: int, dump: bool) -> str:
    rows = write_repo(changed)
    if rows != changed + 1:
        raise AssertionError("git reported %d status rows for %d changed files"
                             % (rows, changed))
    # Each phase waits for the editor to answer the last one rather than
    # guessing a sleep: the prompt is only there once its completion list is,
    # and the popup only once the command has run. Reading the screen too early
    # is what makes a probe like this flap.
    screen = run_in_pty(binary, [ROOT], b"", settle=3.0, after=0.8, cols=COLS, rows=ROWS,
                        cfg=CFG, cwd=ROOT,
                        phases=[(0.8, PALETTE),
                                (0.4, lambda s: "theme" in s.text()),
                                (0.2, b"gitstatus"),
                                (0.2, lambda s: "gitstatus" in s.text()),
                                (0.2, ENTER),
                                (0.5, lambda s: "## master" in s.text()),
                                (0.5, b"")])
    if dump:
        print(screen.text())
    return screen.text()


def truncation_rows(screen: str) -> list[str]:
    """Rows where the popup's own content cell is the truncation marker.

    The marker cell is the box border followed straight by the dots; the
    sidebar writes two spaces after its border, so a file-list row on the same
    screen line cannot be mistaken for the popup's.
    """
    return [row for row in screen.splitlines() if "\u2502..." in row]


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    dump = "--dump" in sys.argv
    if not os.path.exists(binary):
        print(f"string util probe: SKIP - no binary at {binary}")
        return 2
    if not shutil.which("git"):
        print("string util probe: SKIP - no git")
        return 2

    failures = []
    # The popup body reads ` M f00.cpp`; the sidebar's own list reads
    # `f00.cpp   M`, so the cell text tells the two apart on one screen.
    last_row = "M f%02d.cpp" % (LIMIT - 2)
    dropped_row = "M f%02d.cpp" % (LIMIT - 1)

    exact = status_popup(binary, LIMIT - 1, dump)
    if "## master" not in exact:
        failures.append("the git status popup never opened")
    if last_row not in exact:
        failures.append("the last status row is not on screen, so the popup is short")
    if truncation_rows(exact):
        failures.append("a popup holding exactly %d rows marked itself truncated" % LIMIT)

    longer = status_popup(binary, LIMIT + 7, dump)
    if last_row not in longer or dropped_row in longer:
        failures.append("the popup did not stop at %d rows" % LIMIT)
    if not truncation_rows(longer):
        failures.append("a popup that dropped 7 rows did not mark them")

    if failures:
        for line in failures:
            print("FAIL: " + line)
        return 1
    print("string util probe: PASS - %d rows show no marker, %d rows truncate to one"
          % (LIMIT, LIMIT + 8))
    return 0


if __name__ == "__main__":
    sys.exit(main())
