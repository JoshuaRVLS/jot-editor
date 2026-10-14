#!/usr/bin/env python3
"""Probe: harpoon marks files per project and jumps between them.

The feature is a port of harpoon2 (runtime/lua/features/harpoon.lua), and what
only a real session shows is the whole loop: a mark taken on the file in front,
the menu drawn over the pane with the project's own paths in it, a jump that
lands on the right buffer at the right line, and a list that is still there when
the editor starts again.

A mark also carries the position its file was last left at, which is the one
place this port had to invent behaviour: harpoon2 refreshes it on BufLeave and
there is no BufLeave here, so it follows CursorMoved. That scene is the one
where a file marked on line 3, left on line 1, has to come back to line 1.

Every scene but the persistence pair runs on its own store, so what a scene sees
is only what that scene marked.

The last scenes are the API and its edges: a config.lua of the probe's own binds
a chord to `jot.harpoon.list("term"):add()` and opens the menu on it, which is
where the named-list half of the port is either reachable or is not, and the
last scene presses plain Alt+H, the word motion the feature's own prefix shares
its letter with.

Usage: test/harpoon_probe.py [path-to-jot] [--dump]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary).
"""
from __future__ import annotations

import os
import re
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

COLS, ROWS = 100, 26
ROOT = "/tmp/jot_harpoon_probe"
CFG_ROOT = "/tmp/jot_harpoon_probe_cfg"

# The chord prefix, Alt+Shift+H (kitty: h with the alt and shift bits). Alt+H
# would be the mnemonic prefix and is not it: Alt+H is the previous-word motion
# and focuses the pane to the left, so the last scene below also checks that the
# feature's prefix left plain Alt+H alone.
ALT_SHIFT_H = b"\x1b[104;4u"
# The chord the probe's own config.lua binds, so the callback is the config's
# and not the feature's.
ALT_SHIFT_P = b"\x1b[112;4u"

# Written into that scene's config dir. config.lua is read before the bundled
# features load, so the callback reads jot.harpoon when the chord fires -- the
# earliest moment the table exists.
API_CONFIG_LUA = """
jot.keymap.set("Alt+Shift+P y", function()
  local harpoon = jot.harpoon
  if not harpoon then
    jot.ui.show_message("probe: jot.harpoon is missing")
    return
  end
  harpoon.list("term"):add()
  harpoon.list("term"):toggle_quick_menu()
end, "probe: mark this file on the term list")

jot.keymap.set("Alt+Shift+P o", function()
  if not jot.harpoon then
    jot.ui.show_message("probe: jot.harpoon is missing")
    return
  end
  jot.harpoon.list("other"):toggle_quick_menu()
end, "probe: show the other list")
"""
PALETTE = b"\x1b[112;6u"  # Ctrl+Shift+P
ENTER = b"\r"
UP = b"\x1b[A"
DOWN = b"\x1b[B"
CTRL_HOME = b"\x1b[1;5H"
CTRL_END = b"\x1b[1;5F"
MENU = ALT_SHIFT_H + b"m"
MARK = ALT_SHIFT_H + b"a"
NEXT = ALT_SHIFT_H + b"n"
# Plain Alt+H, the previous-word motion the feature's prefix must leave alone,
# and Alt+B, which hides the explorer so there is no pane to focus that way.
ALT_H = b"\x1b[104;3u"
ALT_B = b"\x1b[98;3u"
# A workspace start leaves the keyboard in the explorer, so every scene clicks
# into the pane first; the click also drops the cursor where it lands, which is
# why the file is re-topped before any line is counted.
FOCUS = ("\x1b[<0;46;7M\x1b[<0;46;7m").encode()
TOP = FOCUS + CTRL_HOME

FILES = {
    "a.c": "one\ntwo\nthree\nfour\n",
    "b.c": "alpha\nbeta\ngamma\n",
    "c.c": "solo\n",
    # Two words on one line, for the last scene's check that the feature's chord
    # prefix did not take the plain Alt+H word motion.
    "w.c": "hello world\n",
}

FAILURES: list[str] = []


def check(ok: bool, message: str) -> bool:
    if not ok:
        FAILURES.append(message)
    return ok


def dump(screen, label: str) -> None:
    print("=== %s ===" % label)
    print(screen.text())
    print("-" * 78)


def scene(binary: str, store: str, phases, config: str | None = None):
    """A session on the probe workspace. `store` is the config home it uses, so
    a scene with a store of its own starts with an empty list and a scene that
    repeats another's store is a restart. `config` is written as that home's
    config.lua first, for a scene that drives the feature from Lua."""
    cfg = os.path.join(CFG_ROOT, store)
    if config is not None:
        os.makedirs(cfg, exist_ok=True)
        with open(os.path.join(cfg, "config.lua"), "w") as handle:
            handle.write(config)
    return run_in_pty(binary, [ROOT], b"", settle=3.0, after=1.5, cfg=cfg, cwd=ROOT,
                      cols=COLS, rows=ROWS, phases=phases)


def open_file(name: str) -> bytes:
    return PALETTE + ("e " + name).encode() + ENTER


def lines(screen) -> list[str]:
    return screen.text().split("\n")


def find(screen, needle: str) -> int:
    """The row carrying `needle`, or -1."""
    for index, row in enumerate(lines(screen)):
        if needle in row:
            return index
    return -1


def row_text(screen, needle: str) -> str:
    at = find(screen, needle)
    return lines(screen)[at] if at >= 0 else ""


def cursor(screen) -> str:
    """The line:column the status line reports, e.g. "3:1"."""
    match = re.search(r"(\d+):(\d+)", lines(screen)[-1])
    return "%s:%s" % match.groups() if match else ""


def report(label: str, ok: bool, screen, dump_screens: bool) -> None:
    if ok:
        print("  %-50s ok" % label)
        return
    print("  %-50s MISSING" % label)
    check(False, label)
    if dump_screens:
        dump(screen, label)


# a.c is marked from line 3, b.c from its first line.
MARK_TWO = [(0.6, open_file("a.c")), (0.6, TOP), (0.5, DOWN + DOWN), (0.5, MARK),
            (0.7, open_file("b.c")), (0.6, TOP), (0.6, MARK)]


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    dump_screens = "--dump" in sys.argv
    if not os.path.exists(binary):
        print("harpoon probe: SKIP - no binary at %s" % binary)
        return 2

    shutil.rmtree(ROOT, ignore_errors=True)
    shutil.rmtree(CFG_ROOT, ignore_errors=True)
    os.makedirs(ROOT)
    for name, text in FILES.items():
        with open(os.path.join(ROOT, name), "w") as handle:
            handle.write(text)

    # Scene 1: two marks, one of them taken from line 3, then the menu.
    marked = scene(binary, "menu", MARK_TWO + [(0.7, MENU)])
    report("the menu names its surface", "Harpoon" in marked.text(), marked, dump_screens)
    report("the first mark is listed with its path", "1  a.c" in marked.text(), marked, dump_screens)
    report("the second mark is listed", "2  b.c" in marked.text(), marked, dump_screens)
    report("a mark past line one shows its line", ":3" in row_text(marked, "1  a.c"),
           marked, dump_screens)
    report("the footer names the menu's keys", "enter jumps" in marked.text(),
           marked, dump_screens)
    report("taking a mark is reported", "Harpoon: marked" in marked.text(),
           marked, dump_screens)

    # Scene 2: a digit jumps to that slot and closes the menu.
    jumped = scene(binary, "digit", MARK_TWO + [(0.7, MENU), (0.6, b"1")])
    report("the digit closes the menu", "enter jumps" not in jumped.text(), jumped, dump_screens)
    report("the digit lands on the first mark", "a.c" in lines(jumped)[0], jumped, dump_screens)
    report("it lands on the line the mark holds", cursor(jumped) == "3:1", jumped, dump_screens)

    # Scene 3: Enter jumps to the row the cursor is on.
    entered = scene(binary, "enter", MARK_TWO + [(0.7, MENU), (0.5, DOWN), (0.6, ENTER)])
    report("Enter jumps to the row under the cursor", "b.c" in lines(entered)[0],
           entered, dump_screens)

    # Scene 4: the position follows the cursor, this port's stand-in for the
    # BufLeave refresh harpoon2 does.
    moved = scene(binary, "moved", [(0.6, open_file("a.c")), (0.6, TOP), (0.5, DOWN + DOWN),
                                    (0.5, MARK), (0.5, UP), (0.4, UP),
                                    (0.7, open_file("b.c")), (0.6, TOP), (0.6, MARK),
                                    (0.7, MENU), (0.6, b"1")])
    report("the mark follows the cursor out of the file", cursor(moved) == "1:1",
           moved, dump_screens)

    # Scene 5: a click on a row jumps to it.
    menu_at = find(marked, "2  b.c")
    if menu_at < 0:
        check(False, "the menu has no second row to click")
    else:
        column = lines(marked)[menu_at].find("2  b.c")
        click = ("\x1b[<0;%d;%dM\x1b[<0;%d;%dm" % (column + 1, menu_at + 1,
                                                   column + 1, menu_at + 1)).encode()
        clicked = scene(binary, "click", MARK_TWO + [(0.7, MENU), (0.7, click)])
        report("a click on a row jumps to it", "b.c" in lines(clicked)[0],
               clicked, dump_screens)

    # Scene 6: stepping with the chords, clamped at the end like harpoon2.
    stepped = scene(binary, "step", [(0.6, open_file("a.c")), (0.6, TOP), (0.5, MARK),
                                     (0.7, open_file("b.c")), (0.6, TOP), (0.6, MARK),
                                     (0.7, NEXT), (0.7, NEXT)])
    report("stepping past the last mark stays on it", "b.c" in lines(stepped)[0],
           stepped, dump_screens)

    # Scene 7: d drops the row under the cursor.
    removed = scene(binary, "remove", MARK_TWO + [(0.7, MENU), (0.6, b"d")])
    report("d drops the row it is on", "1  a.c" not in removed.text(), removed, dump_screens)
    report("the dropped mark is gone from the menu", "1  b.c" in removed.text(),
           removed, dump_screens)
    report("how many went is reported", "removed" in removed.text(), removed, dump_screens)

    # Scene 8 and 9: two sessions on one store, which is a restart.
    first = scene(binary, "restart", MARK_TWO)
    restarted = scene(binary, "restart", [(0.6, MENU)])
    report("the first session marked both", "Harpoon: marked" in first.text(),
           first, dump_screens)
    report("the list survives a restart", "1  a.c" in restarted.text(), restarted, dump_screens)
    report("the second mark survives too", "2  b.c" in restarted.text(), restarted, dump_screens)
    report("the store kept the position", ":3" in row_text(restarted, "1  a.c"),
           restarted, dump_screens)

    # Scene 10: clearing empties the list and the menu says so.
    cleared = scene(binary, "clear", [(0.6, open_file("a.c")), (0.6, TOP), (0.5, MARK),
                                      (0.7, open_file("b.c")), (0.6, TOP), (0.6, MARK),
                                      (0.7, ALT_SHIFT_H + b"c"), (0.7, MENU)])
    report("clearing says how many went", "cleared" in cleared.text(), cleared, dump_screens)
    report("an empty list says it is empty", "nothing is marked here yet" in cleared.text(),
           cleared, dump_screens)

    # Scene 11 and 12: the API, driven from a config of the probe's own. The
    # first chord marks the file on the named list "term" and opens the menu on
    # it; the second opens the list "other", which proves the two lists are two.
    # Both scenes would show "jot.harpoon is missing" if the table were not on
    # `jot` by the time a chord of the user's own fires.
    named = scene(binary, "api", [(0.6, open_file("a.c")), (0.6, TOP),
                                  (0.7, ALT_SHIFT_P + b"y")], config=API_CONFIG_LUA)
    report("a chord of the config's own reaches the feature",
           "jot.harpoon is missing" not in named.text(), named, dump_screens)
    report("it marked the file on the list it named", "Harpoon: marked" in named.text(),
           named, dump_screens)
    report("the menu it opened is that list's", "1  a.c" in named.text(), named, dump_screens)

    other = scene(binary, "api2", [(0.6, open_file("a.c")), (0.6, TOP),
                                   (0.6, ALT_SHIFT_P + b"y"), (0.5, b"q"),
                                   (0.7, ALT_SHIFT_P + b"o")], config=API_CONFIG_LUA)
    report("a second named list is its own list",
           "nothing is marked here yet" in other.text(), other, dump_screens)
    report("the mark on the first list is not on it", "1  a.c" not in other.text(),
           other, dump_screens)

    # Scene 13: plain Alt+H still moves back a word. The prefix the feature put
    # one modifier up must not have swallowed the motion it shares a letter with.
    # The explorer is hidden first: while it is up Alt+H is the focus-the-left-
    # pane chord, and the word motion is what is left once there is nothing to
    # focus that way.
    words = scene(binary, "alt_h", [(0.6, open_file("w.c")), (0.6, TOP),
                                     (0.5, ALT_B), (0.5, CTRL_END), (0.6, ALT_H)])
    report("plain Alt+H still moves back a word", cursor(words) == "1:7", words, dump_screens)

    if FAILURES:
        print("harpoon probe: FAIL")
        for failure in FAILURES:
            print("  - %s" % failure)
        return 1
    print("harpoon probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
