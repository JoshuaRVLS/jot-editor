#!/usr/bin/env python3
"""Probe: typing pairs in a real editor leaves the right text and caret.

The unit rules (src/features/autoclose.*) are covered by test_autoclose.cpp;
this drives the real binary's input path in insert mode, so a pair that the
rules allow still has to reach the buffer, an apostrophe still has to stay one
character, and a quote typed before an existing word must not double.

Usage: test/autoclose_probe.py [path-to-jot] [--dump]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary).
"""
from __future__ import annotations

import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

COLS, ROWS = 100, 24


def alt(letter: str) -> bytes:
    """The kitty report for Alt+<letter> (Alt = bitmask 2, so modifier = 3)."""
    return b"\x1b[" + str(ord(letter)).encode() + b";3u"


def workspace(tmp: str) -> str:
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp)
    path = os.path.join(tmp, "a.cpp")
    with open(path, "w") as fh:
        fh.write("int main() { return 0; }\n")
    return path


def type_keys(binary: str, tmp: str, keys: bytes, dump: bool) -> str:
    path = workspace(tmp)
    screen = run_in_pty(binary, [path], keys, settle=2.0, after=0.4,
                        cfg=tmp + "_cfg", cwd=tmp, cols=COLS, rows=ROWS)
    if dump:
        print(screen.text())
        print("-" * 70)
    # The whole screen, not one row: a code file's first row is its breadcrumb
    # bar, so which row holds the buffer moves with the layout.
    return screen.text()


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    dump = "--dump" in sys.argv
    if not os.path.exists(binary):
        print("autoclose probe: SKIP - no binary at %s" % binary)
        return 2

    failures = []
    cases = [
        # (label, keys typed, expected fragment, fragment that must not appear)
        ("apostrophe in a word", b"don't", "don't", "don''t"),
        ("quote before a word", b'"int', '"int', '""int'),
        ("bracket pair", b"(", "()int", None),
        ("bracket pair then its closer", b"()", "()int", "())int"),
        # Typing the opener again while the caret sits inside the pair it just
        # opened: the closer under the caret is that pair's, so the second one
        # nests rather than swallowing it.
        ("opener nested in a pair", b"((", "(())int", "(()int"),
        ("text inside a nested pair", b"((x", "((x))int", "((x)int"),
        ("quote pair inside a bracket", b'("', '("")', '("""'),
        # A closer whose neighbour is a bracket, not a word: it must not drag a
        # partner in. `{` opens its own pair, `}` steps over it, then the quote
        # closes the string. Typed at the end of the line, so the word after the
        # caret is not what refuses the pair.
        ("closing quote after a brace", b'\x1b[Ff"{x}"', 'f"{x}"', 'f"{x}""'),
        ("closing single quote after a brace", b"\x1b[Ff'{x}'", "f'{x}'", "f'{x}''"),
    ]
    for index, (label, keys, want, reject) in enumerate(cases):
        got = type_keys(binary, "/tmp/jot_autoclose_probe_%d" % index, keys, dump)
        ok = want in got and (reject is None or reject not in got)
        print("%-38s want %-10r %s" % (label, want, "ok" if ok else "FAILED"))
        if ok:
            continue
        if want not in got:
            failures.append("%s: %r never reached the buffer" % (label, want))
        if reject is not None and reject in got:
            failures.append("%s: %r was typed as well" % (label, reject))

    # A pair typed over a selection goes around it: the word the user marked is
    # quoted or bracketed rather than replaced by the one character. These go
    # through the key path a typed character takes (the cases above are the pair
    # rules on their own), which is where the selection used to be dropped
    # before anything was inserted. The caret is left inside the pair, so the
    # closer typed next steps over its twin -- the same two keys as a pair typed
    # with nothing selected -- and no selection is left for the next key to eat.
    wraps = [
        # (label, keys, expected fragment, fragment that must not appear, status)
        ("quote around a selected word", alt("v") + b'w"', '"int" main()', '""int"', "1:5"),
        ("single quote around a selected word", alt("v") + b"w'", "'int' main()", "''int'", "1:5"),
        ("brackets around a selected word", alt("v") + b"w(", "(int) main()", None, "1:5"),
        ("the closer steps out of the pair it wrapped", alt("v") + b"w()",
         "(int) main()", "() main()", "1:6"),
    ]
    for index, (label, keys, want, reject, status) in enumerate(wraps):
        got = type_keys(binary, "/tmp/jot_autoclose_wrap_%d" % index, keys, dump)
        row = got.split("\n")[-1]
        ok = want in got and (reject is None or reject not in got) and status in row
        print("%-38s want %-12r %s" % (label, want, "ok" if ok else "FAILED"))
        if ok:
            continue
        if want not in got:
            failures.append("%s: %r never reached the buffer" % (label, want))
        if reject is not None and reject in got:
            failures.append("%s: %r was typed as well" % (label, reject))
        if status not in row:
            failures.append("%s: the caret is not at %s; the bar reads %r" % (label, status, row))

    if failures:
        print("autoclose probe: FAIL")
        for failure in failures:
            print("  - %s" % failure)
        return 1
    print("autoclose probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
