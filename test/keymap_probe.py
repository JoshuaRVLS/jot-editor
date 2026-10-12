#!/usr/bin/env python3
"""Probe: the keymap grammar is discoverable, and its sequences act.

The panel that used to print the possible next chords after a prefix was removed
on purpose (it appeared on every prefix press and was more distraction than
help), so a prefix alone paints nothing now and there is nothing to measure on
screen. What is left is what the user actually depends on:

  * the grammar is written down where it can be found -- `:help`'s `Code & edit`
    block (the reference for the whole prefix vocabulary) and the command
    palette, which lists the commands by name with what they do;
  * the sequences act: Alt+D a f deletes around the function, Alt+V w selects
    the word under the caret, Alt+X j joins the line, Alt+X w \" wraps the word
    in quotes, Alt+C z a folds the block;
  * the two actions that were palette-only reach the buffer too: Alt+C b marks a
    line and says so, Alt+C p walks a bracket pair so the caret lands on the
    closing bracket, and Alt+] b walks back to a bookmark.

Alt chords are sent as kitty CSI-u reports (code;modifier), the spelling jot asks
terminals for: Alt is bitmask 2, so the protocol's modifier field is 3.

Usage: test/keymap_probe.py [path-to-jot]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary).
"""
from __future__ import annotations

import os
import re
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

CTRL_P = b"\x10"  # command palette
ENTER = b"\r"
PAGE_DOWN = b"\x1b[6~"
DOWN = b"\x1b[B"


def alt(letter: str) -> bytes:
    """The kitty report for Alt+<letter> (Alt = bitmask 2, so modifier = 3)."""
    return b"\x1b[" + str(ord(letter)).encode() + b";3u"


def line_text(row: str) -> str:
    """A buffer row with its gutter taken off: the line number, then the single
    mark cell a line with a diagnostic carries, then the text. Only good enough
    to read a row's leading characters, which is all it is used for."""
    return re.sub(r"^\s*\d+\s+[^\w\s]?\s*", "", row)


SOURCE = (
    "int helper() {\n"
    "  return 1;\n"
    "}\n"
    "\n"
    "int target_function() {\n"
    "  return 2;\n"
    "}\n"
    "\n"
    "int third_function() {\n"
    "  return 3;\n"
    "}\n"
)

ROWS = 32


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    if not os.path.exists(binary):
        print(f"keymap probe: SKIP - no binary at {binary}")
        return 2

    work = "/tmp/jot_keymap_probe"
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work, exist_ok=True)
    path = os.path.join(work, "grammar.cpp")
    with open(path, "w") as fh:
        fh.write(SOURCE)
    cfg = "/tmp/jot_keymap_probe_cfg"
    shutil.rmtree(cfg, ignore_errors=True)

    failures = 0

    def check(name: str, ok: bool, view: str = ""):
        nonlocal failures
        print(f"keymap probe: {'ok' if ok else 'FAIL'} - {name}")
        if not ok:
            failures += 1
            if view:
                print("\n".join(row for row in view.split("\n") if row.strip())[:400])

    def session(keys: bytes, phases=None, after: float = 2.0, target: str = None):
        return run_in_pty(binary, [target or path], keys, settle=3.0, after=after,
                          cols=110, rows=ROWS, cfg=cfg, cwd=work, phases=phases)

    # 1. The grammar is written down. `:help` ends with the prefix vocabulary --
    # the whole reference for Alt+D/V/X/C/] and [ -- and a few page-downs put
    # that block and the sections after it on screen. (Counting pages is the
    # fragile part; the block spans rows on both sides of the popup's own page,
    # so a marker from either end is accepted.)
    view = session(CTRL_P + b"help" + ENTER,
                   phases=[(0.3, PAGE_DOWN), (0.3, PAGE_DOWN), (0.3, PAGE_DOWN),
                           (0.3, PAGE_DOWN), (0.3, PAGE_DOWN)]).text()
    marks = [name for name in ("Code & edit", "Alt+C z", "Alt+X j / u / w",
                               "Alt+] / Alt+[", "Next / previous", "Macros & prompt")
             if name in view]
    check("the keybind help documents the prefix vocabulary", len(marks) >= 2, view)

    # 2. ...and the palette names the commands (the descriptions live in the box
    # above the grid, one row per selected command), which is the other half of
    # finding a binding without memorising it.
    view = session(CTRL_P + b"fold").text()
    found = [name for name in ("togglefold", "foldall", "unfoldall", "unfold")
             if name in view]
    check("the palette lists the fold commands", len(found) >= 3, view)

    # 3. The sequence does the thing: Alt+D a f deletes around the function the
    # caret is in. The caret starts on line 1, inside helper().
    view = session(alt("d") + b"a" + b"f").text()
    check("Alt+D a f deletes around the function",
          "helper" not in view and "target_function" in view, view)

    # 4. Alt+V w selects the word, and typing over it replaces it: the selection
    # is a real one, not a highlight. The caret starts on "int".
    view = session(alt("v") + b"w" + b"X").text()
    check("Alt+V w selects the word under the caret",
          any("X helper()" in row for row in view.split("\n")), view)

    # 4b. Alt+V Shift+L takes the whole line: typing over it replaces the line,
    # not the word (the shifted step is a step of its own, which is what lets it
    # sit beside Alt+V L, one cursor per selected line). The line is read
    # through the gutter: a line clangd has a diagnostic for carries an extra
    # mark cell, so the row is "1 <mark>X" and a fixed column would read the
    # report rather than the text.
    view = session(alt("v") + b"L" + b"X").text()
    check("Alt+V Shift+L selects the whole line",
          any(line_text(row).startswith("X") for row in view.split("\n")) and "helper" not in view,
          view)

    # 5. Alt+X j joins the caret's line with the next one.
    view = session(alt("x") + b"j").text()
    check("Alt+X j joins the line with the next",
          any("helper" in row and "return 1;" in row for row in view.split("\n")), view)

    # 6. The wrap chord: the pair is typed as the last key, so the gesture reads
    # as it is written.
    view = session(alt("x") + b"w" + b'"').text()
    check("Alt+X w \" wraps the word in quotes",
          any('"int"' in row for row in view.split("\n")), view)

    # 7. Alt+C z a folds the block the caret is in: the body leaves the screen
    # while the line that opened it stays.
    view = session(alt("c") + b"z" + b"a").text()
    check("Alt+C z a folds the block at the caret",
          "return 1;" not in view and "helper" in view, view)

    # 7b. The same fold in one chord: Alt+C Shift+Z, which is the one pressed
    # while editing rather than while skimming.
    view = session(alt("c") + b"Z").text()
    check("Alt+C Shift+Z folds the block at the caret",
          "return 1;" not in view and "helper" in view, view)

    # 8. Alt+C b marks the line and reports it. The report is the visible half --
    # the gutter marker is one cell -- so the text is the check.
    view = session(alt("c") + b"b").text()
    check("Alt+C b bookmarks the line and reports it", "Bookmark added" in view, view)

    # 9. ...and Alt+] b walks back to it: the caret is moved off line 1 first, so
    # the jump has somewhere to land. The status row's line:col is the check
    # (its tail carries the diagnostic chip, so the position is looked for rather
    # than read off the end of the row).
    view = session(alt("c") + b"b" + DOWN + alt("]") + b"b").text()
    status = view.split("\n")[-1].strip()
    check("Alt+] b walks to the bookmark", "1:1" in status, view)

    # 10. Alt+C p walks the bracket pair: the caret starts on the "(" of "(a)"
    # and has to land on the "). The report says the walk happened and the
    # status line's column says where it landed.
    pairs = os.path.join(work, "pairs.cpp")
    with open(pairs, "w") as fh:
        fh.write("(a)\n")
    view = session(alt("c") + b"p", target=pairs).text()
    status = view.split("\n")[-1].strip()
    check("Alt+C p jumps to the matching bracket",
          "Jumped to matching bracket" in view and "1:3" in status, view)

    # 11. A count typed between the prefix and the chord that ends it is Vim's
    # 3dd: the digits are read by the prefix walk (they are not children of a
    # group whose keys are letters), and the command the sequence ends on reads
    # them as "that many". The caret starts on line 1, so Alt+D 3 l takes the
    # first three lines -- helper() whole, with no blank line left in its place.
    view = session(alt("d") + b"3" + b"l").text()
    check("Alt+D 3 l deletes three lines",
          "helper" not in view and "return 1;" not in view
          and "target_function" in view
          # The count was typing feedback, not a report: the bar goes back to
          # the line:col it showed before the sequence started.
          and "Count" not in view.split("\n")[-1], view)

    # 11b. The same count on the selection: Alt+V 3 Shift+L is three whole
    # lines, and typing over a selection replaces all of it -- which is the
    # multi-line replacement, in one gesture.
    view = session(alt("v") + b"3" + b"L" + b"X").text()
    check("Alt+V 3 Shift+L selects three lines, and typing replaces them",
          any(line_text(row).startswith("X") for row in view.split("\n"))
          and "helper" not in view and "return 1;" not in view
          and "target_function" in view, view)

    # 11c. Digits accumulate, and the count is on the status line while it is
    # typed: 12 is two digits, Backspace takes the last one back, and the chord
    # then deletes the one line it was left with.
    view = session(alt("d") + b"1" + b"2" + b"\x7f").text()
    check("the count shows as it is typed, and Backspace takes a digit back",
          "Count: 1" in view, view)
    view = session(alt("d") + b"1" + b"2" + b"\x7f" + b"l").text()
    check("Alt+D 12 then Backspace deletes one line",
          "helper" not in view and "return 1;" in view, view)

    # 11d. The walks read it too: two functions on from the first is the third
    # definition, so the status line's line:col is what says where it landed.
    view = session(alt("]") + b"2" + b"f").text()
    status = view.split("\n")[-1].strip()
    check("Alt+] 2 f walks two functions", "9:1" in status, view)

    # 11e. The yank half of the line count: Alt+Y 3 l puts three lines in the
    # register, and pasting them back is what proves it was three (a bare
    # Alt+Y l would paste one). A plain .txt so the counts below are the
    # buffer's own words rather than anything a grammar or a diagnostic says.
    plain = os.path.join(work, "lines.txt")
    with open(plain, "w") as fh:
        fh.write("alpha\nsecond\nthird\nfourth\n")
    view = session(alt("y") + b"3" + b"l" + b"\x16", target=plain).text()
    rows = view.split("\n")
    check("Alt+Y 3 l yanks three lines",
          sum(1 for row in rows if "alpha" in row) == 2
          and sum(1 for row in rows if "second" in row) == 2
          and sum(1 for row in rows if "fourth" in row) == 1, view)

    if failures:
        print(f"keymap probe: FAIL ({failures})")
        return 1
    print("keymap probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
