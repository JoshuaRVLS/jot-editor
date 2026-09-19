#!/usr/bin/env python3
"""Probe: folding paints the *current* ranges on a real screen.

Every fold question is now answered from an index cached on the buffer, and the
store invalidates that index on every write. The unit tests pin the invalidation
(revision and checksum); what only a real session shows is that a write is
visible on the next frame -- fold, unfold, fold-all and unfold-all each have to
repaint the new shape, including after a scroll repainted the buffer from the
old one.

Commands go through the Ctrl+P palette, the way a user runs them: `:fold` is
not the spelling this editor's input mode uses.

Usage: test/fold_probe.py [path-to-jot]
Exit codes: 0 pass, 1 fail, 2 binary missing.
"""
from __future__ import annotations

import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

CTRL_P = b"\x10"  # palette
ENTER = b"\r"
DOWN = b"\x1b[B"
PAGE_DOWN = b"\x1b[6~"

SOURCE = """#include <string>

int helper_one() {
  int a = 1;
  int b = 2;
  return a + b;
}

int helper_two() {
  int c = 3;
  return c;
}
"""


def command(name: str) -> bytes:
    return CTRL_P + name.encode() + ENTER


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    if not os.path.exists(binary):
        print(f"fold probe: SKIP - no binary at {binary}")
        return 2

    work = "/tmp/jot_fold_probe"
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work, exist_ok=True)
    path = os.path.join(work, "foldme.cpp")
    with open(path, "w") as fh:
        fh.write(SOURCE)
    cfg = "/tmp/jot_fold_probe_cfg"
    shutil.rmtree(cfg, ignore_errors=True)

    failures = 0

    def check(name: str, ok: bool, detail: str = ""):
        nonlocal failures
        print(f"fold probe: {'ok' if ok else 'FAIL'} - {name}{(': ' + detail) if (detail and not ok) else ''}")
        if not ok:
            failures += 1

    def session(keys: bytes) -> str:
        return run_in_pty(binary, [path], keys, settle=3.0, after=2.0, cols=110, rows=30,
                          cfg=cfg, cwd=work).text()

    # 1. Fold at the cursor: the block under it collapses on screen at once,
    #    and the header carries the hidden count.
    text = session(DOWN * 2 + command("fold"))
    check("fold hides the body", "int a = 1;" not in text, "body line still drawn")
    check("fold keeps the header", "int helper_one() {" in text)
    check("fold shows the hidden count", "\u2026 4 lines" in text,
          "no '... 4 lines' suffix on the header row")
    check("the other block is untouched",
          "int helper_two() {" in text and "int c = 3;" in text)

    # 2. Unfold brings the body back: the reopened range must not be answered
    #    from the index that still had it collapsed.
    text = session(DOWN * 2 + command("fold") + command("unfold"))
    check("unfold restores the body", "int a = 1;" in text and "int b = 2;" in text)
    check("unfold clears the count suffix", "\u2026" not in text)

    # 3. Fold-all, then scroll: the scroll repaints the buffer from the store,
    #    and every row of that frame has to come from the folded shape.
    text = session(command("foldall") + PAGE_DOWN * 3)
    check("fold-all hides every body", "int a = 1;" not in text and "int c = 3;" not in text)
    check("fold-all counts each block", "\u2026 4 lines" in text and "\u2026 3 lines" in text)

    # 4. Unfold-all after that scroll: the bodies return, so the last write is
    #    what the new frame answers from.
    text = session(command("foldall") + PAGE_DOWN * 2 + command("unfoldall"))
    check("unfold-all restores every body", "int a = 1;" in text and "int c = 3;" in text)

    if failures:
        print(f"fold probe: FAIL ({failures})")
        return 1
    print("fold probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
