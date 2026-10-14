#!/usr/bin/env python3
"""Probe: the language's textobjects.scm decides what a syntax object is.

The editor carries a node-name table for the languages it knows, and a
`textobjects.scm` is what says the same thing in the language's own words: which
nodes are a function, what its "inside" is, where a parameter starts. This
drives the real binary over a Lua buffer with nvim-treesitter's own Lua
textobjects.scm in the language's query path, and reads the effect off disk:

  1. `deleteobject inside comment` removes the comment's contents and leaves the
     `--`; the bundled table, which has no idea a comment has an inside, removes
     the whole comment. That difference is the whole point of the query, so it is
     also run with no query at all as the control;
  2. `deleteobject around function` removes the function, which the query names
     as the object;
  3. `nextfunction` walks the caret to the next function the query names, read
     off the status line.

Usage: test/treesitter_textobject_probe.py [path-to-jot] [--dump]
Exit codes: 0 pass, 1 fail, 2 skip (no binary).
"""
from __future__ import annotations

import os
import re
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

COLS, ROWS = 100, 24
CTRL_P = b"\x10"  # palette
ENTER = b"\r"
DOWN = b"\x1b[B"
SAVE = b"\x13"  # Ctrl+S
CARET_RE = re.compile(r"(\d+):(\d+)")

# What nvim-treesitter ships for Lua, the part this probe drives: the comment's
# contents, the function itself and its body, and a parameter.
QUERY = """; comment
(comment
  (comment_content) @comment.inner) @comment.outer

; function
[
  (function_declaration)
  (function_definition)
] @function.outer

(function_declaration
  body: (_) @function.inner)

(function_definition
  body: (_) @function.inner)

; parameter
(parameters
  . 
  (_) @parameter.inner @parameter.outer
  .
  ","? @parameter.outer)
"""

SOURCE = """local function add(a, b)
  -- add the two
  return a + b
end

local function other(x)
  return x
end
"""

COMMENT_LINE = 1  # 0-based row of the comment


def command(line: str) -> bytes:
    return CTRL_P + line.encode() + ENTER


def run(binary: str, work: str, keys: bytes, query: str = QUERY, dump: bool = False):
    """Opens the sample, sends `keys`, saves, and returns (text seen, file)."""
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(os.path.join(work, "queries", "lua"))
    if query:
        with open(os.path.join(work, "queries", "lua", "textobjects.scm"), "w") as fh:
            fh.write(query)
    path = os.path.join(work, "sample.lua")
    with open(path, "w") as fh:
        fh.write(SOURCE)
    screen = run_in_pty(binary, [path], keys, settle=3.0, after=1.0, cfg=os.path.join(work, "cfg"),
                        cwd=work, cols=COLS, rows=ROWS, phases=[(1.0, SAVE)],
                        env={"JOT_TREESITTER_QUERY_PATH": os.path.join(work, "queries")})
    if dump:
        print(screen.text())
        print("-" * 70)
    with open(path) as fh:
        return screen, fh.read()


def to_comment() -> bytes:
    """Keys that put the caret inside the sample's comment.

    Inside, not at the line's end: the caret has to be on a byte of the comment
    for the bundled table to find one at all, which is the whole difference the
    control is there to show.
    """
    return DOWN * COMMENT_LINE + b"\x1b[F\x1b[D"


def caret(screen) -> tuple:
    match = CARET_RE.search(screen.text().splitlines()[ROWS - 1])
    return (-1, -1) if not match else (int(match.group(1)), int(match.group(2)))


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    dump = "--dump" in sys.argv
    if not os.path.exists(binary):
        print("textobject probe: SKIP - no binary at %s" % binary)
        return 2

    failures = 0

    def check(name: str, ok: bool, detail: str = ""):
        nonlocal failures
        print("textobject probe: %s - %s" % ("ok" if ok else "FAIL", name))
        if not ok:
            failures += 1
            if detail:
                print("    %s" % detail)

    # 1. The comment's inside: only the query knows a comment has one.
    _, text = run(binary, "/tmp/jot_textobj_inner",
                  to_comment() + command("deleteobject inside comment"), dump=dump)
    check("inside comment leaves the dashes the query excluded",
          "-- add the two\n" not in text and "--\n" in text,
          "file holds %r" % text)

    # 2. Around the function: the object itself, as the query names it.
    _, text = run(binary, "/tmp/jot_textobj_outer",
                  DOWN * 2 + command("deleteobject around function"), dump=dump)
    check("around function takes the whole function",
          "add(a, b)" not in text and "local function other(x)" in text,
          "file holds %r" % text)

    # 3. The jump lands on the function the query names.
    screen, _ = run(binary, "/tmp/jot_textobj_jump", command("nextfunction"), dump=dump)
    row, column = caret(screen)
    check("nextfunction lands on the next function's line",
          row == 6 and column == 1, "caret is at %d:%d, wanted 6:1" % (row, column))

    # The control: with no textobjects.scm the bundled table answers, and it has
    # no inside for a comment, so the whole comment goes.
    _, text = run(binary, "/tmp/jot_textobj_none",
                  to_comment() + command("deleteobject inside comment"), query="", dump=dump)
    check("no query falls back to the bundled table",
          "-- add the two" not in text and "--\n" not in text,
          "file holds %r" % text)

    if failures:
        print("textobject probe: FAIL (%d)" % failures)
        return 1
    print("textobject probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
