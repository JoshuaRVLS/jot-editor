#!/usr/bin/env python3
"""Probe: a query's predicates and directives decide what gets highlighted.

Tree-sitter compiles a pattern but does not apply it: the `(#match? ...)` and
`(#set! ...)` forms come back as steps on the match, and the editor is the one
that has to decide whether a pattern counts at all and where its captures land.
An editor that ignores them paints every node each pattern *could* match, which
is exactly how a query written for one grammar ends up colouring whole buffers.

The probe drives the real binary with its own highlight query for `cmake` (the
language ships an empty bundled query, so the runtime query an install supplies
is the one in charge) and reads the colour of each word off the reconstructed
screen. Three scenes:

  * predicates decide: four words, four different predicates (#eq? on a literal,
    #lua-match? on a Lua pattern, #match? on a regex, #any-of? on a set), each
    colouring exactly one word, plus one word no pattern mentions. If the
    predicates were ignored every word would be painted.
  * a predicate that fails paints nothing: the same shapes with predicates no
    node can satisfy. Every word keeps the default colour.
  * directives: a pattern-wide `(#set! priority ...)` and a capture-scoped one
    both outrank a later lower-priority pattern (the command name is painted in
    the colour of the raising capture, and a third command with no directive is
    not), and `(#offset! @number 0 0 0 -1)` drops the last character of the only
    argument of the third command.

Usage: test/treesitter_predicates_probe.py [path-to-jot] [--dump]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary or no cmake parser).
"""
from __future__ import annotations

import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

ROOT = "/tmp/jot_treesitter_predicates_probe"
CFG = "/tmp/jot_treesitter_predicates_probe_cfg"
QUERY_DIR = os.path.join(ROOT, "queries")
SOURCE_NAME = "probe.cmake"
SOURCE_PATH = os.path.join(ROOT, SOURCE_NAME)

# One command per predicate, one word per command, so each colour on the screen
# names exactly one predicate. `epsilon` and the quoted word of the second
# command are named by no pattern at all: they are the reference for "this word
# was left alone", which is what makes "painted the same colour" different from
# "painted", and a query that ignores its predicates has to fail on.
PREDICATE_SOURCE = '# a comment\ncmd_one(ALPHA beta gamma delta epsilon)\ncmd_two("untouched")\n'

PREDICATE_QUERY = """(line_comment) @comment

((unquoted_argument) @string
  (#eq? @string "ALPHA"))

((unquoted_argument) @type
  (#lua-match? @type "^beta$"))

((unquoted_argument) @comment
  (#match? @comment "^gam"))

((unquoted_argument) @keyword
  (#any-of? @keyword "delta" "omega"))
"""

# The same four shapes with predicates nothing can satisfy. A language that
# applied them paints four words here; these must stay at the default colour.
FAILING_QUERY = """((unquoted_argument) @string
  (#eq? @string "ZZZ"))

((unquoted_argument) @type
  (#lua-match? @type "^zzz"))

((unquoted_argument) @comment
  (#match? @comment "zzz"))

((unquoted_argument) @keyword
  (#any-of? @keyword "zzzz"))

(normal_command (identifier) @function)
"""

DIRECTIVE_SOURCE = '# a note\ncmd_one(ALPHA)\ncmd_two("beta")\ncmd_three(gamma)\n'

# Each command is selected by its own shape or name, so exactly one directive
# applies to each: cmd_one by name (the pattern-wide form), cmd_two by its
# quoted argument (the capture-scoped form), cmd_three by nothing, which is the
# control the other two have to differ from.
DIRECTIVE_QUERY = """(line_comment) @comment

; Pattern-wide: raises every capture in this pattern above @function (95).
((normal_command (identifier) @comment
                 (argument_list (argument (unquoted_argument))))
  (#eq? @comment "cmd_one")
  (#set! priority 200))

; Capture-scoped: same effect, written the other way, on the quoted form.
((normal_command (identifier) @constant
                 (argument_list (argument (quoted_argument) @string)))
  (#set! @constant priority 200))

(normal_command (identifier) @function)

; #offset! trims the last character off the argument of the third command.
((normal_command (identifier) @_cmd
                 (argument_list (argument (unquoted_argument) @number)))
  (#eq? @_cmd "cmd_three")
  (#offset! @number 0 0 0 -1))
"""


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


def write_scene_files(source: str, query: str) -> None:
    shutil.rmtree(ROOT, ignore_errors=True)
    os.makedirs(os.path.join(QUERY_DIR, "cmake"), exist_ok=True)
    with open(os.path.join(QUERY_DIR, "cmake", "highlights.scm"), "w") as fh:
        fh.write(query)
    with open(SOURCE_PATH, "w") as fh:
        fh.write(source)


def row_with_needle(screen, needle: str):
    for row in range(screen.rows):
        text = "".join(screen.cells[row])
        col = text.find(needle)
        if col >= 0:
            return row, col
    return -1, -1


def word_colors(screen, needle: str):
    """The foreground colour of every cell of the first `needle` on screen."""
    row, col = row_with_needle(screen, needle)
    if row < 0:
        return None
    return [screen.fg[row][col + i] for i in range(len(needle))]


def dump(screen) -> None:
    for row in range(screen.rows):
        cells = "".join(screen.cells[row]).rstrip()
        if not cells:
            continue
        colors = {fg for fg in screen.fg[row][: len(cells)] if fg >= 0}
        print(f"  {row:>2} {sorted(colors)} {cells}")


def check_groups(scene: str, screen, groups: dict[str, list[str]], failures: list[str]) -> None:
    """Every word carries one colour, its group shares it, groups do not.

    A word painted in two colours is a failure on its own: it means the capture
    landed on part of the node only (an offset that should not be there, or a
    predicate that let a longer node through).
    """
    seen: dict[str, int] = {}
    for group, words in groups.items():
        for word in words:
            fgs = word_colors(screen, word)
            if fgs is None:
                failures.append(f"{scene}: {word!r} is not on screen at all")
                continue
            distinct = set(fgs)
            if len(distinct) != 1:
                failures.append(
                    f"{scene}: {word!r} is painted in {len(distinct)} colours {fgs}; "
                    f"the capture covers part of the node only"
                )
                continue
            color = fgs[0]
            if group in seen and seen[group] != color:
                failures.append(
                    f"{scene}: {word!r} is colour {color} but the rest of {group!r} is "
                    f"colour {seen[group]}"
                )
            seen[group] = color
    for group, color in seen.items():
        for other, other_color in seen.items():
            if group != other and color == other_color:
                failures.append(
                    f"{scene}: {group!r} and {other!r} share colour {color}; "
                    f"the groups should be told apart"
                )


def run_scene(binary: str, source: str, query: str, dump_screen: bool):
    write_scene_files(source, query)
    screen = run_in_pty(
        binary,
        [SOURCE_PATH],
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
        print(f"--- {SOURCE_NAME} with the scene's query")
        dump(screen)
    return screen


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    dump_screen = "--dump" in sys.argv
    if not os.path.exists(binary):
        print(f"treesitter predicates probe: SKIP - no binary at {binary}")
        return 2
    if not cmake_parser_installed():
        print("treesitter predicates probe: SKIP - no cmake parser installed")
        return 2

    failures: list[str] = []

    # 1. Predicates decide which captures count, each on the node's own text.
    screen = run_scene(binary, PREDICATE_SOURCE, PREDICATE_QUERY, dump_screen)
    check_groups(
        "predicates",
        screen,
        {
            # The comment line is the reference for the comment colour, so
            # "gamma" is proved to be painted as a comment and not just "painted".
            "comment": ["# a comment", "gamma"],
            "string": ["ALPHA"],
            "type": ["beta"],
            "keyword": ["delta"],
            "none": ["epsilon", '"untouched"'],
        },
        failures,
    )

    # 2. A predicate nothing satisfies paints nothing: same shapes, same
    #    captures, only the predicate text differs from scene 1.
    screen = run_scene(binary, PREDICATE_SOURCE, FAILING_QUERY, dump_screen)
    check_groups(
        "failing predicates",
        screen,
        {
            # Nothing here is painted by a predicate-carrying pattern: the five
            # arguments would take a colour each if the predicates were ignored,
            # and the quoted word has no pattern to take one from at all.
            "untouched": ["ALPHA", "beta", "gamma", "delta", "epsilon", '"untouched"'],
            "function": ["cmd_one", "cmd_two"],
        },
        failures,
    )

    # 3. Directives: priority chooses between two captures of the same node, and
    #    offset! moves the end of a capture.
    screen = run_scene(binary, DIRECTIVE_SOURCE, DIRECTIVE_QUERY, dump_screen)
    check_groups(
        "directives",
        screen,
        {
            "comment": ["# a note", "cmd_one"],
            "constant": ["cmd_two"],
            "function": ["cmd_three"],
            "string": ['"beta"'],
            "none": ["ALPHA"],
        },
        failures,
    )
    # gamma is the one capture an #offset! touches: its first four characters
    # carry the capture's colour and the fifth is back to the colour of the word
    # no pattern captures (ALPHA is that word, in the "none" group above).
    gamma = word_colors(screen, "gamma")
    untouched = word_colors(screen, "ALPHA")
    if gamma is None or len(gamma) != 5:
        failures.append(f"directives: 'gamma' is not on screen as one five-character word: {gamma}")
    elif untouched is None or len(set(untouched)) != 1:
        failures.append(f"directives: 'ALPHA' is not the untouched colour reference: {untouched}")
    elif len(set(gamma[:4])) != 1:
        failures.append(f"directives: the first four characters of 'gamma' are {gamma}")
    elif gamma[0] == untouched[0]:
        failures.append(
            "directives: 'gamma' is not painted at all; the pattern's capture (or its "
            "#offset! range) did not land on the argument"
        )
    elif gamma[4] != untouched[0]:
        failures.append(
            f"directives: the last character of 'gamma' is colour {gamma[4]}, but the "
            f"untouched word is {untouched[0]}; (#offset! 0 0 0 -1) dropped more than one "
            f"character"
        )

    if failures:
        for failure in failures:
            print(f"treesitter predicates probe: FAIL - {failure}")
        return 1
    print("treesitter predicates probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
