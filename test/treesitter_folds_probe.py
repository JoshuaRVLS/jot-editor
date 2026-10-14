#!/usr/bin/env python3
"""Probe: a language's own folds.scm decides which rows a fold hides.

The bracket/indent detector can only see punctuation and leading whitespace, so
a construct the grammar knows is a block but the text does not spell with braces
or indentation -- a multi-line string argument, a table literal, a here-doc --
folds nowhere. A `folds.scm` in the query path says where the blocks are, and
that answer has to reach the screen in place of the guess.

The probe drives the real binary on a cmake file whose only multi-line node is a
bracket argument, and reads which rows survive `:foldall`:

  * without a folds query, nothing folds (the heuristics have no brace pair and
    no indentation step to work with): every row stays on screen;
  * with one that captures the bracket argument, the whole argument collapses on
    the header row, which carries the hidden-line count.

Usage: test/treesitter_folds_probe.py [path-to-jot] [--dump]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary or no cmake parser).
"""
from __future__ import annotations

import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

CTRL_P = b"\x10"  # palette
ENTER = b"\r"

ROOT = "/tmp/jot_treesitter_folds_probe"
CFG = "/tmp/jot_treesitter_folds_probe_cfg"
QUERY_DIR = os.path.join(ROOT, "queries")
SOURCE_PATHS = os.path.join(ROOT, "probe.cmake")

# The bracket argument spans rows 1-3 and nothing in the text is indented or
# braced, so the only thing that can fold it is the grammar.
SOURCE = "cmd_one([[\ninside one\ninside two\n]])\ncmd_two(END)\n"

FOLD_QUERY = "(bracket_argument) @fold\n"


def parser_dirs() -> list[str]:
    home = os.environ.get("HOME", "")
    data = os.environ.get("XDG_DATA_HOME") or os.path.join(home, ".local", "share")
    cache = os.environ.get("XDG_CACHE_HOME") or os.path.join(home, ".cache")
    return [
        os.path.join(data, "jot", "treesitter", "parsers"),
        os.path.join(cache, "jot", "treesitter", "parsers"),
        "/usr/lib",
        "/usr/local/lib",
    ]


def cmake_parser_installed() -> bool:
    names = [
        "libtree-sitter-cmake.so",
        "libtree-sitter-cmake.dylib",
        "tree-sitter-cmake.dll",
    ]
    return any(os.path.exists(os.path.join(d, name)) for d in parser_dirs() for name in names)


def command(name: str) -> bytes:
    return CTRL_P + name.encode() + ENTER


def write_scene(fold_query: str | None) -> None:
    shutil.rmtree(ROOT, ignore_errors=True)
    # The config home holds the persisted fold state of every file it has seen;
    # a collapsed range saved by an earlier scene would be re-applied here and
    # answer for the query.
    shutil.rmtree(CFG, ignore_errors=True)
    os.makedirs(os.path.join(QUERY_DIR, "cmake"), exist_ok=True)
    if fold_query is not None:
        with open(os.path.join(QUERY_DIR, "cmake", "folds.scm"), "w") as fh:
            fh.write(fold_query)
    with open(SOURCE_PATHS, "w") as fh:
        fh.write(SOURCE)
    # No language server: a diagnostic's virtual text lands on the same row the
    # fold's hidden-line count does.
    os.makedirs(os.path.join(CFG, "configs"), exist_ok=True)
    with open(os.path.join(CFG, "configs", "settings.conf"), "w") as fh:
        fh.write("lsp_enabled=false\n")


def session(binary: str, keys: bytes, dump_screen: bool) -> str:
    screen = run_in_pty(
        binary,
        [SOURCE_PATHS],
        keys,
        settle=4.0,
        after=3.0,
        cols=110,
        rows=30,
        cfg=CFG,
        cwd=ROOT,
        env={"JOT_TREESITTER_QUERY_PATH": QUERY_DIR},
    )
    if dump_screen:
        print(screen.text())
        print("-" * 70)
    return screen.text()


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    dump_screen = "--dump" in sys.argv
    if not os.path.exists(binary):
        print(f"treesitter folds probe: SKIP - no binary at {binary}")
        return 2
    if not cmake_parser_installed():
        print("treesitter folds probe: SKIP - no cmake parser installed")
        return 2

    failures: list[str] = []

    # 1. No fold query: the text has no brace pair and no indent step, so
    #    nothing folds and every row stays. This is the control for scene 2.
    write_scene(None)
    text = session(binary, command("foldall"), dump_screen)
    for row in ("inside one", "inside two"):
        if row not in text:
            failures.append(
                f"no folds query: {row!r} is gone from the screen; the heuristic folder "
                f"folded a range this text has no shape for"
            )
    if "\u2026" in text:
        failures.append("no folds query: a row carries a hidden-line count but nothing should fold")

    # 2. A folds query capturing the bracket argument: the argument collapses
    #    onto its header row, which shows the count, and the rest of the file is
    #    untouched.
    write_scene(FOLD_QUERY)
    text = session(binary, command("foldall"), dump_screen)
    for row in ("inside one", "inside two", "]]"):
        if row in text:
            failures.append(f"folds query: {row!r} is still drawn; the fold did not hide it")
    if "cmd_one([[" not in text:
        failures.append("folds query: the header row is gone; the fold hid the row it belongs to")
    if "cmd_two(END)" not in text:
        failures.append("folds query: the row below the folded range is gone")
    if "\u2026 3 lines" not in text:
        failures.append(
            "folds query: the header row does not carry the '... 3 lines' count of the rows "
            "it hides"
        )

    # 3. Unfolding brings them back: the ranges the query produced are what the
    #    unfold writes back to.
    text = session(binary, command("foldall") + command("unfoldall"), dump_screen)
    for row in ("inside one", "inside two"):
        if row not in text:
            failures.append(f"unfold all: {row!r} is still hidden")

    if failures:
        for failure in failures:
            print(f"treesitter folds probe: FAIL - {failure}")
        return 1
    print("treesitter folds probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
