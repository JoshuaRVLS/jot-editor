#!/usr/bin/env python3
"""Probe: a language embedded in another is coloured by its own grammar.

`injections.scm` says which nodes of a host file hold a different language. The
host grammar can only call that text a string, so without the injection a Python
file's C++ snippet is one flat string colour; with it, the snippet's keywords and
numbers come from the C++ query, painted over the host's cells at their true
columns.

The cells are read from the reconstructed screen's foreground colours, so this
checks what the renderer painted rather than a query lookup. Three sessions over
a Python file:

  1. a constant language (`(#set! injection.language "cpp")`) colours both a
     region that starts on its own row and one that starts mid-row;
  2. a language that comes from a capture's own text (`@injection.language` on a
     variable whose name is the language) colours the region the same way;
  3. a language with no installed parser leaves the region as the host painted
     it, and the editor keeps running.

Usage: test/treesitter_injection_probe.py [path-to-jot] [--dump]
Exit codes: 0 pass, 1 fail, 2 skip (no binary).
"""
from __future__ import annotations

import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

# Two regions, one starting on its own row and one starting after a quote on a
# row that holds host code, plus a control string that must stay the host's.
SET_SOURCE = '''inline = "int other = 3;"

block = """
int flag = 1;
return flag;
"""

plain = "string text"
'''

CAPTURE_SOURCE = '''cpp = """
int flag = 1;
return flag;
"""

plain = "string text"
'''

SET_QUERY = """((assignment
   left: (identifier) @_l
   right: (string (string_content) @injection.content)
   (#eq? @_l "block"))
  (#set! injection.language "cpp"))

((assignment
   left: (identifier) @_l
   right: (string (string_content) @injection.content)
   (#eq? @_l "inline"))
  (#set! injection.language "cpp"))
"""

CAPTURE_QUERY = """((assignment
   left: (identifier) @injection.language
   right: (string (string_content) @injection.content)))
"""

MISSING_QUERY = """((assignment
   left: (identifier) @_l
   right: (string (string_content) @injection.content)
   (#eq? @_l "block"))
  (#set! injection.language "rust"))
"""


def session(binary: str, work: str, source: str, query: str, dump: bool):
    """Runs one session with `source` open and `query` as injections.scm."""
    queries = os.path.join(work, "queries")
    shutil.rmtree(queries, ignore_errors=True)
    os.makedirs(os.path.join(queries, "python"))
    with open(os.path.join(queries, "python", "injections.scm"), "w") as fh:
        fh.write(query)
    path = os.path.join(work, "embed.py")
    with open(path, "w") as fh:
        fh.write(source)
    screen = run_in_pty(binary, [path], b"", settle=3.0, after=2.0, cols=100, rows=30,
                        cfg=os.path.join(work, "cfg"), cwd=work,
                        env={"JOT_TREESITTER_QUERY_PATH": queries})
    if dump:
        print(screen.text())
        print("-" * 70)
    return screen


def row_of(screen, needle: str) -> int:
    for row, line in enumerate(screen.text().splitlines()):
        if needle in line:
            return row
    return -1


def col_of(screen, row: int, needle: str, offset: int = 0) -> int:
    if row < 0:
        return -1
    col = screen.text().splitlines()[row].find(needle)
    return -1 if col < 0 else col + offset


def fg_at(screen, row: int, col: int) -> int:
    if row < 0 or col < 0 or col >= screen.cols:
        return -1
    return screen.fg[row][col]


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    dump = "--dump" in sys.argv
    if not os.path.exists(binary):
        print("injection probe: SKIP - no binary at %s" % binary)
        return 2

    work = "/tmp/jot_injection_probe"
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)

    failures = 0

    def check(name: str, ok: bool, detail: str = ""):
        nonlocal failures
        print("injection probe: %s - %s" % ("ok" if ok else "FAIL", name))
        if not ok:
            failures += 1
            if detail:
                print("    %s" % detail)

    # 1. The constant language, for both region shapes.
    screen = session(binary, work, SET_SOURCE, SET_QUERY, dump)
    plain_row = row_of(screen, "plain =")
    host_string = fg_at(screen, plain_row, col_of(screen, plain_row, '"') + 1)
    block_row = row_of(screen, "return flag;")
    keyword = fg_at(screen, block_row, col_of(screen, block_row, "return"))
    int_row = row_of(screen, "int flag = 1;")
    int_col = col_of(screen, int_row, "int flag = ")
    number = fg_at(screen, int_row, int_col + len("int flag = "))
    check("a constant language colours the region's keywords",
          keyword >= 0 and keyword != host_string,
          "keyword colour %s vs host string colour %s" % (keyword, host_string))
    check("every keyword of the region takes it",
          int_col >= 0 and fg_at(screen, int_row, int_col) == keyword,
          "int colour %s vs return %s" % (fg_at(screen, int_row, int_col), keyword))
    check("the region's numbers come from the child query too",
          number >= 0 and number != host_string and number != keyword,
          "number colour %s vs keyword %s and host string %s"
          % (number, keyword, host_string))
    check("the control string keeps the host's colour",
          fg_at(screen, plain_row, col_of(screen, plain_row, '"') + 2) == host_string,
          "the whole control string should read as one host string colour")
    check("the host keeps painting outside the region",
          fg_at(screen, plain_row, col_of(screen, plain_row, "plain")) != host_string)

    # A region that starts on a row that also holds host code: the child's
    # columns have to land after the opening quote, not on the first cell of the
    # row and not one cell early.
    inline_row = row_of(screen, "int other")
    content_col = col_of(screen, inline_row, 'inline = "') + len('inline = "')
    check("a region on the host's own row is not shifted",
          content_col > 0 and fg_at(screen, inline_row, content_col) == keyword
          and fg_at(screen, inline_row, content_col - 1) != keyword
          and fg_at(screen, inline_row, content_col + len("int")) == host_string,
          "cells: quote=%s first keyword cell=%s space after it=%s"
          % (fg_at(screen, inline_row, content_col - 1),
             fg_at(screen, inline_row, content_col),
             fg_at(screen, inline_row, content_col + len("int"))))

    # 2. A language taken from a capture: the query reads the variable's name.
    named = session(binary, work, CAPTURE_SOURCE, CAPTURE_QUERY, dump)
    named_row = row_of(named, "return flag;")
    check("a captured language name colours the region",
          fg_at(named, named_row, col_of(named, named_row, "return")) == keyword,
          "keyword colour %s vs %s from the constant form"
          % (fg_at(named, named_row, col_of(named, named_row, "return")), keyword))

    # 3. No parser for the named language: the host's colours stay, nothing breaks.
    missing = session(binary, work, SET_SOURCE, MISSING_QUERY, dump)
    missing_row = row_of(missing, "return flag;")
    missing_plain = row_of(missing, "plain =")
    check("an uninstalled language leaves the host's colour",
          fg_at(missing, missing_row, col_of(missing, missing_row, "return"))
          == fg_at(missing, missing_plain, col_of(missing, missing_plain, '"') + 1),
          "the region should read as one host string colour when no parser exists")
    check("the session survives it", "return flag;" in missing.text())

    if failures:
        print("injection probe: FAIL (%d)" % failures)
        return 1
    print("injection probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
