#!/usr/bin/env python3
"""Probe: Enter indents the new line by the language's own indents.scm.

A language that ships an indent query says where its blocks start, which branch
lines pull back out, and which nodes own their own indentation. Those answers
replace the text heuristics for that language, so what has to be checked is
where the caret lands: this drives the real binary, presses Enter at the end of
a line, and reads the caret out of the status line. The column the new line
starts at is its indent.

The expectations are nvim-treesitter's, taken from its own indenter over the
same file and the same query (`lua/nvim-treesitter/indent.lua`), with
shiftwidth 2. It reads the status line rather than the saved file because this
editor reindents a buffer's text rules on save, which would answer a different
question than the one the query decides.

Usage: test/treesitter_indent_probe.py [path-to-jot] [--dump]
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
END = b"\x1b[F"
DOWN = b"\x1b[B"
ENTER = b"\r"

# The caret chip of the status line ("<row>:<column>").
CARET_RE = re.compile(r"(\d+):(\d+)")

# The language's own indent query, the file nvim-treesitter ships for Lua.
QUERY = """[
  (function_definition)
  (function_declaration)
  (field)
  (do_statement)
  (method_index_expression)
  (while_statement)
  (repeat_statement)
  (if_statement)
  "then"
  (for_statement)
  (return_statement)
  (table_constructor)
  (arguments)
] @indent.begin

[
  "end"
  "}"
  "]]"
] @indent.end

[
  "end"
  "then"
  "until"
  "}"
  ")"
  "elseif"
  (elseif_statement)
  "else"
  (else_statement)
] @indent.branch

(comment) @indent.auto
(string) @indent.auto
"""

SOURCE = """local function f()
  local x = 1
  if x then
    print(x)
  else
    print("no")
  end
end

local t = {
  a = 1,
  b = 2,
}

local function g() end
"""

# label, the line Enter is pressed at the end of, the indent the query gives the
# new line (nvim-treesitter's answer over the same file)
CASES = [
    ("a function body opens", 1, 2),
    ("a control statement opens", 3, 4),
    ("a branch opens its own body", 5, 4),
    ("the end of an if closes a body", 7, 2),
    ("the end of a function closes all of them", 8, 0),
    ("a table literal opens", 10, 2),
    ("a field's line keeps the table's level", 12, 2),
    ("the closing brace closes the table", 13, 0),
    ("a one-line function opens nothing", 15, 0),
]


def caret_after_enter(binary: str, work: str, line_number: int, dump: bool, query: str = QUERY):
    """The (row, column) the caret lands on after Enter at `line_number`'s end.

    An empty `query` leaves the language with no indent query at all, which is
    the control: the same key then gets the text rules' answer.
    """
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(os.path.join(work, "queries", "lua"))
    if query:
        with open(os.path.join(work, "queries", "lua", "indents.scm"), "w") as fh:
            fh.write(query)
    path = os.path.join(work, "sample.lua")
    with open(path, "w") as fh:
        fh.write(SOURCE)
    keys = DOWN * (line_number - 1) + END + ENTER
    screen = run_in_pty(binary, [path], keys, settle=3.0, after=1.5, cfg=os.path.join(work, "cfg"),
                        cwd=work, cols=COLS, rows=ROWS,
                        env={"JOT_TREESITTER_QUERY_PATH": os.path.join(work, "queries")})
    if dump:
        print(screen.text())
        print("-" * 70)
    status = screen.text().splitlines()[ROWS - 1]
    match = CARET_RE.search(status)
    return (-1, -1) if not match else (int(match.group(1)), int(match.group(2))), status


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    dump = "--dump" in sys.argv
    if not os.path.exists(binary):
        print("indent probe: SKIP - no binary at %s" % binary)
        return 2

    failures = []
    for index, (label, line_number, indent) in enumerate(CASES):
        (row, column), status = caret_after_enter(
            binary, "/tmp/jot_indent_probe_%d" % index, line_number, dump)
        ok = row == line_number + 1 and column == indent + 1
        print("%-48s %s" % (label, "ok" if ok else "FAILED"))
        if not ok:
            failures.append("%s: caret is at %d:%d, wanted %d:%d (status: %r)"
                            % (label, row, column, line_number + 1, indent + 1, status.strip()))

    # The control: with no indent query in the language's query path the text
    # rules answer, and the one-line function is exactly where the two disagree.
    # Without this the cases above could pass on the heuristics alone.
    (row, column), status = caret_after_enter(binary, "/tmp/jot_indent_probe_none", 15, dump,
                                              query="")
    fallback_ok = row == 16 and column == 3
    print("%-48s %s" % ("no query falls back to the text rules", "ok" if fallback_ok else "FAILED"))
    if not fallback_ok:
        failures.append("without an indent query the caret is at %d:%d, wanted 16:3 (status: %r)"
                        % (row, column, status.strip()))

    if failures:
        print("indent probe: FAIL")
        for failure in failures:
            print("  - %s" % failure)
        return 1
    print("indent probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
