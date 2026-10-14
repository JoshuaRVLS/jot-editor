#!/usr/bin/env python3
"""Probe: the modeline a query file opens with decides how it merges.

A query file can say what it is relative to the ones around it:

    ;; extends                  add to the query this file would replace
    ;; inherits: a,b,(c)        pull those languages' queries in ahead of it

Read as a plain comment, both do nothing, and the failure is silent in opposite
directions: an extension file never takes effect (the bundled query wins the
race and the file is never even compiled), and an inheriting file highlights
only its own patterns instead of its parents' plus its own.

The probe drives the real binary with its own query files and reads the colour
of each word off the reconstructed screen:

  * `;; extends` (python, which has a bundled query to extend): without the
    modeline the file is ignored and `marker` keeps the colour the bundled
    query gives every identifier; with it, `marker` is painted as a comment and
    the bundled query's own patterns still apply (the comment line is still
    painted as a comment, which only the base query does).
  * `;; inherits:` (cmake, whose bundled query is empty): the language's own
    file names a parent that has no query (in parentheses, so it is skipped)
    and one the probe supplies; a word only the parent's patterns name is
    painted, and so is a word only the file's own patterns name.

Usage: test/treesitter_modeline_probe.py [path-to-jot] [--dump]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary, or no python/cmake parser).
"""
from __future__ import annotations

import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

ROOT = "/tmp/jot_treesitter_modeline_probe"
CFG = "/tmp/jot_treesitter_modeline_probe_cfg"
QUERY_DIR = os.path.join(ROOT, "queries")

PY_SOURCE = "# note\nmarker = other\n"

# Only the extending file's own pattern: every other colour on the screen has to
# come from the query it is merged with.
EXTEND_BODY = '((identifier) @comment\n  (#eq? @comment "marker"))\n'

CMAKE_SOURCE = "cmd_one(INHERITED MINE)\n"
CMAKE_QUERY = """;; inherits: (probe_absent), probe_parent
((unquoted_argument) @keyword
  (#eq? @keyword "MINE"))
"""
# The parent query the probe supplies (no language in the registry has to own it:
# a parent is a source of patterns, and this one names cmake's own node types).
PARENT_QUERY = '((unquoted_argument) @string\n  (#eq? @string "INHERITED"))\n'


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


def parser_installed(language: str) -> bool:
    names = [f"libtree-sitter-{language}.so", f"libtree-sitter-{language}.dylib",
             f"tree-sitter-{language}.dll"]
    return any(os.path.exists(os.path.join(d, name)) for d in parser_dirs() for name in names)


def reset_workspace() -> None:
    shutil.rmtree(ROOT, ignore_errors=True)
    os.makedirs(QUERY_DIR, exist_ok=True)
    # No language server: an attached one paints the identifier it complains
    # about in the diagnostic colour, over the syntax colour this probe reads.
    os.makedirs(os.path.join(CFG, "configs"), exist_ok=True)
    with open(os.path.join(CFG, "configs", "settings.conf"), "w") as fh:
        fh.write("lsp_enabled=false\n")


def write_query(language: str, source: str) -> None:
    os.makedirs(os.path.join(QUERY_DIR, language), exist_ok=True)
    with open(os.path.join(QUERY_DIR, language, "highlights.scm"), "w") as fh:
        fh.write(source)


def write_source(name: str, source: str) -> str:
    path = os.path.join(ROOT, name)
    with open(path, "w") as fh:
        fh.write(source)
    return path


def row_with_needle(screen, needle: str):
    for row in range(screen.rows):
        text = "".join(screen.cells[row])
        col = text.find(needle)
        if col >= 0:
            return row, col
    return -1, -1


def word_colors(screen, needle: str):
    row, col = row_with_needle(screen, needle)
    if row < 0:
        return None
    return [screen.fg[row][col + i] for i in range(len(needle))]


def uniform_color(screen, needle: str, scene: str, failures: list[str]):
    """The single colour a word is painted in, or None (reported)."""
    colors = word_colors(screen, needle)
    if colors is None:
        failures.append(f"{scene}: {needle!r} is not on screen at all")
        return None
    if len(set(colors)) != 1:
        failures.append(f"{scene}: {needle!r} is painted in {len(set(colors))} colours {colors}")
        return None
    return colors[0]


def dump(screen) -> None:
    for row in range(screen.rows):
        text = "".join(screen.cells[row]).rstrip()
        if not text or text.startswith("~"):
            continue
        print(f"  {row:>2} {text!r}")
        print("     ", " ".join(
            f"{ch}:{screen.fg[row][col]}"
            for col, ch in enumerate("".join(screen.cells[row])) if ch != " "))


def run_scene(binary: str, source_path: str, dump_screen: bool):
    screen = run_in_pty(
        binary,
        [source_path],
        b"",
        settle=5.0,
        after=3.0,
        cols=120,
        rows=30,
        cfg=CFG,
        cwd=ROOT,
        env={"JOT_TREESITTER_QUERY_PATH": QUERY_DIR},
    )
    if dump_screen:
        print(f"--- {os.path.basename(source_path)}")
        dump(screen)
    return screen


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    dump_screen = "--dump" in sys.argv
    if not os.path.exists(binary):
        print(f"treesitter modeline probe: SKIP - no binary at {binary}")
        return 2
    if not parser_installed("python") or not parser_installed("cmake"):
        print("treesitter modeline probe: SKIP - the python and cmake parsers are not installed")
        return 2

    failures: list[str] = []
    source_path = ""

    # 1. Without the modeline the file is not the language's query at all: the
    #    bundled python query wins the race and paints `marker` (an identifier)
    #    exactly like the identifier next to it, and the comment line as a
    #    comment. This is the control for scene 2.
    reset_workspace()
    write_query("python", EXTEND_BODY)
    source_path = write_source("probe.py", PY_SOURCE)
    screen = run_scene(binary, source_path, dump_screen)
    marker = uniform_color(screen, "marker", "no modeline", failures)
    other = uniform_color(screen, "other", "no modeline", failures)
    comment = uniform_color(screen, "# note", "no modeline", failures)
    if marker is not None and other is not None and comment is not None:
        if marker != other:
            failures.append(
                f"no modeline: 'marker' is colour {marker} but the identifier beside it is "
                f"{other}; a runtime file with no modeline should not be the language's query"
            )
        if marker == comment:
            failures.append(
                f"no modeline: 'marker' is painted as a comment ({marker}) without saying "
                f";; extends"
            )

    # 2. `;; extends`: the same file, now claiming it is additive. Its pattern
    #    applies (marker becomes a comment colour) and so do the bundled query's
    #    (the comment line keeps its comment colour, which only the base paints).
    reset_workspace()
    write_query("python", ";; extends\n" + EXTEND_BODY)
    source_path = write_source("probe.py", PY_SOURCE)
    screen = run_scene(binary, source_path, dump_screen)
    marker = uniform_color(screen, "marker", "extends", failures)
    other = uniform_color(screen, "other", "extends", failures)
    comment = uniform_color(screen, "# note", "extends", failures)
    if marker is not None and other is not None and comment is not None:
        if marker == other:
            failures.append(
                f"extends: 'marker' is still the plain identifier colour ({marker}); the "
                f"extending file's pattern was not applied"
            )
        if marker != comment:
            failures.append(
                f"extends: 'marker' is colour {marker}, not the comment colour {comment} "
                f"its own pattern asks for"
            )
        if other == comment:
            failures.append(
                f"extends: the plain identifier is painted {comment} too; the comment line "
                f"is not being painted by the base query"
            )

    # 3. `;; inherits:`: cmake ships no bundled query, so this file is the
    #    language's query. A word only the parent's patterns name is painted,
    #    as is a word only this file's own patterns name. The parent in
    #    parentheses has no query anywhere and is skipped rather than fatal.
    reset_workspace()
    write_query("cmake", CMAKE_QUERY)
    write_query("probe_parent", PARENT_QUERY)
    source_path = write_source("probe.cmake", CMAKE_SOURCE)
    screen = run_scene(binary, source_path, dump_screen)
    inherited = uniform_color(screen, "INHERITED", "inherits", failures)
    mine = uniform_color(screen, "MINE", "inherits", failures)
    plain = uniform_color(screen, "cmd_one", "inherits", failures)
    if inherited is not None and mine is not None and plain is not None:
        if inherited == plain:
            failures.append(
                f"inherits: 'INHERITED' is the untouched colour {plain}; the named parent's "
                f"query was not compiled with this file's"
            )
        if mine == plain:
            failures.append(
                f"inherits: 'MINE' is the untouched colour {plain}; the file's own patterns "
                f"are missing from the merged query"
            )
        if inherited == mine:
            failures.append(
                f"inherits: the parent's colour and the file's own are both {mine}; the two "
                f"patterns are not both applied"
            )

    if failures:
        for failure in failures:
            print(f"treesitter modeline probe: FAIL - {failure}")
        return 1
    print("treesitter modeline probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
