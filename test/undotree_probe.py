#!/usr/bin/env python3
"""Probe: the undo tree panel on a real screen.

The tree itself is native (src/buffers/undo.cpp) and the C++ tests pin the model.
What only a real session shows is the panel built on top of it:

  * the dock's plugin tab under the panel's own name, listing the states the
    buffer has been in, newest first, with the state in front starred;
  * the cursor, which j/k move without touching the buffer, and the changes the
    state under it would bring back, listed below it;
  * Enter, which is what walks the buffer onto that state, and Shift+J / Shift+K,
    which move the buffer a step at a time and apply where they land;
  * the keys it does not use, which still reach the buffer, and the ones a
    prompt needs, which the panel must never take: opening the command palette
    over the panel and typing into the palette has to fill the palette's own
    query box, not move the tree's cursor.

Usage: test/undotree_probe.py [path-to-jot] [--dump]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary).
"""
from __future__ import annotations

import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

COLS, ROWS = 120, 32
PANEL_W = 42  # the default right_panel_width
PANEL_X = COLS - PANEL_W
ROOT = "/tmp/jot_undotree_probe"
CFG = "/tmp/jot_undotree_probe_cfg"

PALETTE = b"\x1b[112;6u"  # Ctrl+Shift+P
ENTER = b"\r"
CTRL_Z = b"\x1a"
# A plain letter reaches a panel uppercased, so the shifted forms are the ones
# that carry the apply half of the table (J forward, K back).
SHIFT_J = b"\x1b[74;2u"
SHIFT_K = b"\x1b[75;2u"
BAR = "\u258c"  # the cursor bar the dock's lists draw
STAR = "*"
TIP = "o"
# Alt+Shift+U T, the chord form of the open command: kitty reports it as a
# letter carrying the shift and alt bits.
CHORD = b"\x1b[117;4u" + b"T"

# Two typed lines, undone twice, then a third run of typing. The third run
# starts from the state the undo went back to, so the tree forks there instead
# of growing a single line -- which is the whole point of the panel.
EDITS = [b"alpha", ENTER, b"beta", CTRL_Z + CTRL_Z, b"gamma"]

FAILURES: list[str] = []


def check(ok: bool, message: str) -> bool:
    if not ok:
        FAILURES.append(message)
    return ok


def dump(screen, label: str) -> None:
    print("=== %s ===" % label)
    print(screen.text())
    print("-" * 78)


class TreeRow:
    """One row of the dock's tree: its glyph, state number and cursor bar."""

    def __init__(self, glyph: str, seq: int, selected: bool):
        self.glyph = glyph
        self.seq = seq
        self.selected = selected

    @property
    def current(self) -> bool:
        return self.glyph == STAR

    def __repr__(self) -> str:
        return "TreeRow(%r, %d, selected=%r)" % (self.glyph, self.seq, self.selected)

    # Without this, a list of rows only equals itself, so a check comparing two
    # frames fails whatever the panel did.
    def __eq__(self, other: object) -> bool:
        return (isinstance(other, TreeRow) and self.glyph == other.glyph
                and self.seq == other.seq and self.selected == other.selected)


def dock_rows(rows: list[str]) -> list[str]:
    return [row[PANEL_X:] for row in rows]


def pane_text(rows: list[str]) -> str:
    """The editor pane's own text: everything left of the dock."""
    return "\n".join(row[: PANEL_X - 1] for row in rows)


def dock_text(rows: list[str]) -> str:
    return "\n".join(dock_rows(rows))


def tree_rows(rows: list[str]) -> list[TreeRow]:
    """The tree rows the dock lists, top to bottom."""
    out = []
    for row in dock_rows(rows):
        text = row.strip()
        selected = text.startswith(BAR)
        if selected:
            text = text[len(BAR) :].strip()
        cells = text.split()
        if len(cells) >= 2 and cells[0] in (STAR, "|", TIP) and cells[1].isdigit():
            out.append(TreeRow(cells[0], int(cells[1]), selected))
    return out


def change_lines(rows: list[str]) -> list[str]:
    """The `-`/`+` rows the dock lists under the cursor."""
    return [row.strip() for row in dock_rows(rows)
            if row.strip().startswith("- ") or row.strip().startswith("+ ")]


def scene(binary: str, phases) -> object:
    return run_in_pty(binary, [os.path.join(ROOT, "editme.txt")], b"", settle=3.0, after=1.0,
                      cols=COLS, rows=ROWS, cfg=CFG, cwd=ROOT, phases=phases)


def open_panel(extra=(), opener="palette"):
    """Type the edits, then open the panel -- from the palette or the chord."""
    phases = [(0.4, keys) for keys in EDITS]
    if opener == "palette":
        phases += [(0.5, PALETTE), (0.4, b"undotree"), (0.5, ENTER)]
    else:
        phases += [(0.5, CHORD)]
    phases.append((0.7, lambda s: "Undotree" in s.text() or True))
    return phases + list(extra)


def palette_open_phases(extra=()):
    """No edits at all: open the panel on a buffer nothing has touched."""
    phases = [(0.5, PALETTE), (0.4, b"undotree"), (0.5, ENTER)]
    phases.append((0.8, lambda s: "Undotree" in s.text() or True))
    return phases + list(extra)


def starred_seq(rows: list[str]) -> object:
    """The state the buffer is on, as the tree draws it."""
    hit = [row.seq for row in tree_rows(rows) if row.current]
    return hit[0] if len(hit) == 1 else None


def capture_when(seen: dict, key: str, ready, timeout: float = 8.0):
    """A phase that waits for the screen to say `ready`, then records it.

    A fixed sleep races the frame on a loaded machine: reading the walk before
    the editor painted it looks exactly like the walk not happening.
    """

    def phase(screen) -> bool:
        rows = screen.text().split("\n")
        if not ready(rows):
            return False
        seen[key] = rows
        return True

    return (timeout, phase)


def holds_the_typed_text(rows: list[str]) -> bool:
    """The edits under test are still what the buffer holds."""
    return "begammaone" in pane_text(rows)


def walked_away(rows: list[str]) -> bool:
    """A state other than the typed one is in front."""
    return "begammaone" not in pane_text(rows)


def shows_changes(rows: list[str]) -> bool:
    """The panel is up with the cursor's state diffed under it."""
    return "Changes" in dock_text(rows)


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    show = "--dump" in sys.argv
    if not os.path.exists(binary):
        print("undotree probe: SKIP - no binary at %s" % binary)
        return 2
    shutil.rmtree(ROOT, ignore_errors=True)
    shutil.rmtree(CFG, ignore_errors=True)
    os.makedirs(ROOT)
    with open(os.path.join(ROOT, "editme.txt"), "w") as fh:
        fh.write("one\ntwo\nthree\n")

    # 1. Nothing edited yet: the panel says so instead of drawing an empty list.
    screen = scene(binary, palette_open_phases())
    rows = screen.text().split("\n")
    if show:
        dump(screen, "empty")
    check("Undotree" in dock_text(rows), "the dock's tab does not carry the panel's name")
    check("No states yet" in dock_text(rows), "an unedited buffer did not report an empty tree")
    check(not tree_rows(rows), "an unedited buffer already listed states")

    # 2. The tree of an edited buffer: numbers newest first, one star on the
    #    state in front, the cursor starting there.
    screen = scene(binary, open_panel())
    rows = screen.text().split("\n")
    if show:
        dump(screen, "tree")
    tree = tree_rows(rows)
    check(len(tree) > 3, "the tree listed %d states after the edits" % len(tree))
    seqs = [row.seq for row in tree]
    check(seqs == sorted(seqs, reverse=True), "the tree is not listed newest first")
    star_rows = [row for row in tree if row.current]
    check(len(star_rows) == 1, "the tree did not star exactly one state: %r" % tree)
    check(bool(star_rows) and star_rows[0].seq == seqs[0], "the star is not on the newest state")
    check([row for row in tree if row.selected] == star_rows,
          "the cursor did not start on the state in front")
    check("alpha" in pane_text(rows), "the editor pane lost the edited text")

    # 3. j moves the cursor one state back without touching the buffer, and the
    #    changes that state would bring back are listed under it.
    screen = scene(binary, open_panel([(0.5, b"j"), (0.8, lambda s: True)]))
    rows = screen.text().split("\n")
    if show:
        dump(screen, "select")
    tree = tree_rows(rows)
    cursor = [row for row in tree if row.selected]
    check(len(cursor) == 1 and cursor[0].seq == tree[1].seq,
          "j did not move the cursor one row down the list")
    check(bool(cursor) and not cursor[0].current, "j left the cursor on the state in front")
    check("Changes" in dock_text(rows), "the state under the cursor listed no changes")
    changes = change_lines(rows)
    check(any(line.startswith("- ") for line in changes) and any(line.startswith("+ ") for line in changes),
          "the change list is missing the removed or the added line: %r" % changes)
    check("begammaone" in pane_text(rows), "j edited the buffer instead of moving the cursor")

    # 4. Enter walks the buffer onto the state under the cursor: the text that
    #    state holds is back in the pane, the star follows, and the branch the
    #    walk left behind turns into a tip.
    seen: dict = {}
    phases = open_panel([
        (0.5, b"j"),
        capture_when(seen, "before", shows_changes),
        (0.5, ENTER),
        capture_when(seen, "after", walked_away),
    ])
    screen = scene(binary, phases)
    rows = seen.get("after") or screen.text().split("\n")
    before = seen.get("before") or []
    if show:
        dump(screen, "walk")
    added = [line[2:].strip() for line in change_lines(before) if line.startswith("+ ")]
    check(bool(added), "the state under the cursor listed no added line before the walk")
    check(added and added[0] in pane_text(rows),
          "Enter did not put the state's own text back in the buffer: %r" % added)
    tree = tree_rows(rows)
    cursor = [row for row in tree if row.selected]
    check(len(cursor) == 1 and cursor[0].current,
          "the star did not follow the walk onto the state under the cursor: %r" % tree)
    left = [row for row in tree if cursor and row.seq > cursor[0].seq]
    check(bool(left) and left[0].glyph == TIP,
          "the branch the walk left behind is not drawn as a tip: %r" % tree)
    check("begammaone" not in pane_text(rows), "the buffer still holds the text the walk replaced")

    # 5. A letter the panel does not use still reaches the buffer, and the chord
    #    form of the open command works as well as the palette's.
    screen = scene(binary, open_panel([(0.5, b"z"), (0.8, lambda s: True)]))
    rows = screen.text().split("\n")
    if show:
        dump(screen, "passthrough")
    check("begammazone" in pane_text(rows),
          "a key the panel does not use never reached the buffer")
    check("Undotree" in dock_text(rows), "typing closed the panel")
    screen = scene(binary, open_panel([(0.8, lambda s: True)], opener="chord"))
    rows = screen.text().split("\n")
    if show:
        dump(screen, "chord")
    check("Undotree" in dock_text(rows), "the chord did not open the panel")
    check(len(tree_rows(rows)) > 3, "the chord opened an empty panel")

    # 6. Shift+K and Shift+J move the buffer itself, applying the state they
    #    land on: the tree's own undo and redo, with the star following the
    #    buffer. S is a re-read, so the tree is exactly where it was after it.
    seen: dict = {}
    phases = open_panel([
        capture_when(seen, "before", holds_the_typed_text),
        (0.5, SHIFT_K),
        capture_when(seen, "back", walked_away),
        (0.5, SHIFT_J),
        capture_when(seen, "retake", holds_the_typed_text),
        (0.4, b"S"),
        capture_when(seen, "reread", holds_the_typed_text),
    ])
    screen = scene(binary, phases)
    before = seen.get("before") or screen.text().split("\n")
    back = seen.get("back") or []
    retake = seen.get("retake") or []
    reread = seen.get("reread") or []
    if show:
        dump(screen, "step")
    check("begammaone" in pane_text(before), "the edits under test never reached the buffer")
    check("begammaone" not in pane_text(back),
          "Shift+K did not apply the parent state to the buffer")
    check(starred_seq(back) is not None and starred_seq(back) < starred_seq(before),
          "the star did not follow Shift+K back to the parent: %r" % starred_seq(back))
    check(starred_seq(retake) == starred_seq(before),
          "Shift+J did not step forward onto the newest child: %r" % starred_seq(retake))
    check("begammaone" in pane_text(retake), "Shift+J did not bring the child's text back")
    check(len(tree_rows(retake)) > 3, "S dropped the tree: %r" % tree_rows(retake))
    check(tree_rows(reread) == tree_rows(retake) and starred_seq(reread) == starred_seq(retake),
          "S changed the tree: %r then %r" % (tree_rows(retake), tree_rows(reread)))

    # 7. S re-reads the history: the tree, the star and the cursor come back the
    #    same, which is what a key for a stale panel has to do.
    plain = scene(binary, open_panel([(0.8, lambda s: True)])).text().split("\n")
    screen = scene(binary, open_panel([(0.5, b"S"), (0.8, lambda s: True)]))
    rows = screen.text().split("\n")
    if show:
        dump(screen, "reread")
    check(tree_rows(rows) == tree_rows(plain), "S did not read the same tree back")
    check(starred_seq(rows) == starred_seq(plain), "S moved the buffer")
    check([row.seq for row in tree_rows(rows) if row.selected]
          == [row.seq for row in tree_rows(plain) if row.selected],
          "S moved the cursor")

    # 8. The palette is not the panel's: every letter typed into it lands in its
    #    query box, including the ones the tree binds (j and k move the cursor,
    #    S re-reads). The panel stays where it was.
    before = scene(binary, open_panel([(0.8, lambda s: True)])).text().split("\n")
    after = scene(binary, open_panel([(0.5, PALETTE), (0.5, b"jj"), (0.9, lambda s: True)]))
    rows = after.text().split("\n")
    if show:
        dump(after, "palette")
    check("jj" in after.text(), "the letters typed into the palette never reached its query box")
    check([row.seq for row in tree_rows(before) if row.selected]
          == [row.seq for row in tree_rows(rows) if row.selected],
          "the panel moved its cursor on the palette's keystrokes")
    check("Undotree" in dock_text(rows), "the palette's own command closed the panel")

    if FAILURES:
        print("undotree probe: FAIL")
        for failure in FAILURES:
            print("  - %s" % failure)
        return 1
    print("undotree probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
