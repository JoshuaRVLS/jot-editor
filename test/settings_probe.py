#!/usr/bin/env python3
"""Probe: the settings panel's search bar, sections, steppers and choices drop-down.

The unit tests drive the panel's model headlessly (grouping, filtering,
stepping, the drop-down's cursor). What only a real session proves is the
picture: that the search bar is on screen with a live match count, that typing
lands in it, that the keys are listed under their group's heading, that an int
row wears its nf-fa-minus/plus steppers right where the mouse hit test put
them, that an enum row shows its cycling chevrons, and that Enter opens the
choices as a list that applies what the cursor is on -- with the glyphs the
native layout recorded rather than whatever Lua felt like drawing.

The panel's two text fields are checked the same way a hand would: a bracketed
paste -- the escape pair every terminal wraps a paste in -- has to land in the
row being edited (and be saved from there) or in the search bar, and never in
the buffer behind the panel.

Every scene is its own session (one screen read per run), opening this probe's
workspace and then :settings from the command palette.

Usage: test/settings_probe.py [path-to-jot] [--dump]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary).
"""
from __future__ import annotations

import os
import re
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

COLS, ROWS = 120, 34

# Ctrl+Shift+P opens the command palette; Enter runs what is typed in it.
PALETTE = b"\x1b[112;6u"
ENTER = b"\r"
ESC = b"\x1b"
RIGHT = b"\x1b[C"
DOWN = b"\x1b[B"

# A bracketed paste, the escape pair a terminal wraps a paste body in.
PASTE_START, PASTE_END = b"\x1b[200~", b"\x1b[201~"

TOGGLE_ON = "\uf205"   # nf-fa-toggle_on
TOGGLE_OFF = "\uf204"  # nf-fa-toggle_off
MINUS = "\uf068"       # nf-fa-minus, the int row's decrease stepper
PLUS = "\uf067"        # nf-fa-plus, its increase stepper
CHECK = "\uf00c"       # nf-fa-check, the choice in force in the drop-down
CHEV_L = "\u2039"
CHEV_R = "\u203a"


# The panel is a modal float over a modal palette, and a keystroke delivered
# while either is still settling is simply dropped: the gaps are generous
# (verified against a run of four sessions) rather than tight.
SETTLE, AFTER = 5.0, 0.8
INIT, TYPE, STEP, TAIL = 1.4, 1.2, 1.5, 1.5


def settings_run(binary: str, root: str, cfg: str, extra, tail: float = TAIL):
    """Opens the workspace, runs :settings from the palette, types `extra`."""
    phases = [(INIT, PALETTE), (0.8, b"settings"), (0.8, ENTER)]
    phases.extend(extra)
    phases.append((tail, b""))
    return run_in_pty(binary, [root], b"", settle=SETTLE, after=AFTER, cols=COLS,
                      rows=ROWS, cfg=cfg, cwd=root, phases=phases)


def row_of(view: str, needle: str) -> str:
    for line in view.splitlines():
        if needle in line:
            return line
    return ""


def row_index_of(view: str, needle: str) -> int:
    for index, line in enumerate(view.splitlines()):
        if needle in line:
            return index
    return -1


def divider_row(view: str) -> int:
    """The index of the rule between the search bar and the list, or -1.

    A long run of box rule between two sides, with no corner glyph in it.
    Reading the search bar's text instead would only work while it was showing
    its placeholder, which is exactly what a query replaces.
    """
    for at, line in enumerate(view.splitlines()):
        if re.search("─{20,}", line) and not any(corner in line for corner in "┌┐└┘"):
            return at
    return -1


def search_row_of(view: str):
    """(row index, text) of the search bar: the line above the divider."""
    at = divider_row(view)
    lines = view.splitlines()
    if at <= 0:
        return -1, ""
    return at - 1, lines[at - 1]


def first_list_row(view: str) -> str:
    """The first row of the list, whichever chrome sits above it."""
    at = divider_row(view)
    lines = view.splitlines()
    return lines[at + 1] if 0 <= at < len(lines) - 1 else ""


def number_in(row: str, needle: str):
    """The integer that follows `needle` on a panel row, or None."""
    at = row.find(needle)
    if at < 0:
        return None
    match = re.search(r"(-?\d+)", row[at + len(needle):])
    return int(match.group(1)) if match else None


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    dump = "--dump" in sys.argv
    if not os.path.exists(binary):
        print(f"settings probe: SKIP - no binary at {binary}")
        return 2

    root = "/tmp/jot_settings_probe_ws"
    shutil.rmtree(root, ignore_errors=True)
    os.makedirs(root)
    # Without a workspace the editor paints the home screen and never reaches
    # the panel at all.
    with open(os.path.join(root, "notes.txt"), "w") as fh:
        fh.write("probe workspace\n")

    failures: list[str] = []
    raws: dict[str, bytes] = {}
    cfgs: dict[str, str] = {}
    screens: dict[str, object] = {}

    def scene(name: str, extra, tail: float = TAIL, seed: str = "") -> str:
        # A fresh config home per scene per run: the panel *writes* settings
        # (a stepped number is saved), so a reused directory would hand the
        # next run a starting point left over from this one.
        cfg = f"/tmp/jot_settings_probe_cfg_{name}_{os.getpid()}"
        shutil.rmtree(cfg, ignore_errors=True)
        cfgs[name] = cfg
        if seed:
            # A config that already exists, so the scene starts on it: what a
            # scene asserts about a setting the editor *reads* at startup needs
            # the file in place before the session opens.
            os.makedirs(os.path.join(cfg, "configs"))
            with open(os.path.join(cfg, "configs", "settings.conf"), "w") as fh:
                fh.write(seed)
        screen = settings_run(binary, root, cfg, extra, tail)
        view = screen.text()
        # The escape stream as written: the glyph checks read this, because a
        # multi-byte glyph split across two reads of the pty lands in the grid
        # as "?" cells (a harness artifact, not something the editor did).
        raws[name] = bytes(screen.raw)
        screens[name] = screen
        if dump:
            print(f"--- scene {name} ---")
            print(view)
        return view

    def fail(message: str) -> None:
        failures.append(message)

    # Scene 1: the panel itself -- search bar, keys count, a bool's toggle.
    view = scene("bar", [])
    print("scene 1: :settings shows the search bar, the count and typed rows")
    if not re.search(r"Search \d+ settings", view):
        fail("the search bar's placeholder is not on the panel")
    if "keys" not in view:
        fail("the title row's key count is missing")
    if TOGGLE_ON not in view and TOGGLE_OFF not in view:
        fail("no boolean row shows its toggle glyph")
    if "─" not in view:
        fail("the divider under the search bar is missing")
    # The first group's first key, under the heading scene 8 reads.
    if not row_of(view, "Auto-detect indent"):
        fail("the unfiltered list does not open on the first config key")

    # The search bar's ink: the prompt mark and the placeholder are one run of
    # the panel's quiet colour. Spans are byte offsets into the line, so a
    # multi-byte glyph before a span (the magnifier the bar used to carry) must
    # not push it short -- the placeholder's tail then escaped its span and
    # painted in the float's default ink (the "gs" of "settings" went white).
    screen = screens["bar"]
    lines = view.splitlines()
    srow, sline = search_row_of(view)
    placeholder = re.search(r"Search \d+ settings", sline)
    if srow < 0 or not placeholder:
        fail("the search bar is not on the panel")
    else:
        inks = {screen.fg[srow][x] for x in range(placeholder.start(), placeholder.end())}
        if len(inks) != 1:
            fail(f"the placeholder is painted in more than one ink: {sorted(inks)}")
        elif screen.fg[srow][sline.index(">")] not in inks:
            fail("the prompt mark and the placeholder do not share an ink")

    # The selected row's toggle: one run of ink from the glyph through the
    # state text. The row's chevron is a three-byte glyph, so a cell-advanced
    # span started two bytes early and dropped the tail ("ff" of "off") into
    # the panel's default ink.
    value = TOGGLE_OFF + " off"
    vrow = row_index_of(view, "Auto-detect indent")
    if vrow < 0 or value not in lines[vrow]:
        fail("the selected boolean row does not show its toggle")
    else:
        at = lines[vrow].index(value)
        inks = {screen.fg[vrow][x] for x in range(at, at + len(value))}
        if len(inks) != 1:
            fail(f"the selected row's value is painted in more than one ink: {sorted(inks)}")

    # Scene 2: typing filters, and the count turns into matches/total.
    view = scene("filter", [(TYPE, b"tab")])
    print("scene 2: typing narrows the list and the count")
    counter = re.search(r"(\d+)/(\d+)", view)
    if not counter:
        fail("the search row shows no match count while a query is active")
    elif int(counter.group(1)) >= int(counter.group(2)):
        fail(f"the query did not narrow the list ({counter.group(0)})")
    if "Tab size" not in view:
        fail("`tab` did not keep the Tab size row")
    if row_of(view, "Auto-detect indent"):
        fail("`tab` left a row that cannot match the query")

    # The query and the match count own their inks in the search bar: the query
    # in the accent ink, the count in the quiet one, and neither bleeding into
    # the other (a span offset off by a few bytes swaps them).
    screen = screens["filter"]
    srow, sline = search_row_of(view)
    at = sline.find("tab")
    count_on_row = re.search(r"\d+/\d+", sline)
    if srow < 0 or at < 0 or not count_on_row:
        fail("the filtered search bar is not on the panel")
    else:
        query_inks = {screen.fg[srow][x] for x in range(at, at + 3)}
        count_inks = {screen.fg[srow][x] for x in range(count_on_row.start(), count_on_row.end())}
        if len(query_inks) != 1 or len(count_inks) != 1:
            fail("the query or the match count is painted in more than one ink")
        elif query_inks == count_inks:
            fail("the query and the match count share one ink")

    # Scenes 3 and 4: an int row wears its steppers and Right moves the number
    # by exactly one notch.
    before_view = scene("steppers", [(TYPE, b"tabsize")])
    print("scene 3: an int row shows its steppers")
    before_row = row_of(before_view, "Tab size")
    if not before_row:
        fail("the Tab size row is not on screen")
    else:
        if MINUS not in before_row or PLUS not in before_row:
            fail(f"the selected int row has no steppers: {before_row!r}")
        if CHEV_L in before_row:
            fail("an int row is wearing the enum chevrons")
    before_value = number_in(before_row, "Tab size")
    if before_value is None:
        fail("the Tab size row shows no number")

    after_view = scene("stepped", [(TYPE, b"tabsize"), (STEP, RIGHT)])
    print("scene 4: Right steps the number")
    after_row = row_of(after_view, "Tab size")
    after_value = number_in(after_row, "Tab size")
    if after_value is None:
        fail("the Tab size row shows no number after stepping")
    elif before_value is not None and after_value != before_value + 1:
        fail(f"Right stepped {before_value} -> {after_value}, not by one")
    if MINUS not in after_row or PLUS not in after_row:
        fail("the steppers vanished after stepping")

    # Scene 5: an enum row shows the choice between its cycling chevrons.
    view = scene("enum", [(TYPE, b"viewerbackend")])
    print("scene 5: an enum row shows its chevrons and current choice")
    enum_row = row_of(view, "Image viewer backend")
    if CHEV_L.encode() not in raws["enum"] or CHEV_R.encode() not in raws["enum"]:
        fail("the enum row's cycling chevrons were never painted")
    if not enum_row:
        fail("the image-viewer row is not on screen")
    elif "auto" not in enum_row:
        fail(f"the enum row does not show its current choice: {enum_row!r}")
    else:
        # Bracketing, not just present: the chevrons sit one blank cell outside
        # the choice, on both sides (a split glyph reads as "?" in the grid).
        at = enum_row.find("auto")
        if enum_row[at - 2] not in ("?", CHEV_L) or enum_row[at + 5] not in ("?", CHEV_R):
            fail(f"the chevrons do not bracket the choice: {enum_row!r}")

    # Scene 6: Enter lists the choices, marking the one in force.
    view = scene("options", [(TYPE, b"viewerbackend"), (STEP, ENTER)])
    print("scene 6: Enter opens the choices as a list")
    if "kitty" not in view or "sixel" not in view:
        fail("the open drop-down does not list the choices")
    if CHECK not in view:
        fail("the drop-down does not mark the choice in force")
    if "auto" not in row_of(view, "Image viewer backend"):
        fail("the row lost its value while the list is open")

    # Scene 7: Down/Down then Enter applies the choice under the cursor.
    view = scene("picked", [(TYPE, b"viewerbackend"), (STEP, ENTER),
                            (0.8, DOWN), (0.8, DOWN), (STEP, ENTER)])
    print("scene 7: the choice under the cursor is applied")
    picked_row = row_of(view, "Image viewer backend")
    if "sixel" not in picked_row:
        fail(f"the picked choice is not in force: {picked_row!r}")
    if "kitty" in view:
        fail("the drop-down is still on screen after taking a choice")

    # Scene 8: the panel is grouped -- a heading over the keys it files, the
    # heading on its own row and the group's first key under it.
    view = scene("sections", [])
    print("scene 8: the keys are listed under section headings")
    first = first_list_row(view)
    if "Editor" not in first:
        fail(f"the list does not open on its first group's heading: {first!r}")
    at = row_index_of(view, "Editor")
    lines = view.splitlines()
    second = lines[at + 1] if 0 <= at < len(lines) - 1 else ""
    if "Auto-detect indent" not in second:
        fail(f"the heading is not followed by its group's first key: {second!r}")
    # The window holds fifteen rows and the first group is ten keys, so the
    # group after it is on screen too -- the headings are not one big heading.
    if not row_of(view, "Appearance"):
        fail("the second group's heading is missing")

    # Scene 9: a query announces the groups it kept and only those.
    view = scene("grouped", [(TYPE, b"tabsize")])
    print("scene 9: a query keeps the heading of the group it matched")
    first = first_list_row(view)
    if "Editor" not in first:
        fail(f"the matched group's heading is gone: {first!r}")
    if not row_of(view, "Tab size"):
        fail("the matched row itself is gone")
    if row_of(view, "Appearance"):
        fail("a group the query emptied is still announced")

    # Scene 10: Esc puts the list away without changing anything.
    view = scene("dismissed", [(TYPE, b"viewerbackend"), (STEP, ENTER), (STEP, ESC)])
    print("scene 10: Esc dismisses the list without applying anything")
    dismissed_row = row_of(view, "Image viewer backend")
    if "auto" not in dismissed_row:
        fail(f"Esc applied a choice: {dismissed_row!r}")
    if "sixel" in view:
        fail("the drop-down survived Esc")
    if "viewerbackend" not in view:
        fail("Esc closed the whole panel instead of just the list")

    # Scene 11: a paste lands in the row being edited, and is saved from there.
    # `/` filters to the row, Enter opens its inline editor, the bracketed paste
    # is the key, and the last Enter applies it (config.save writes the file).
    pasted = "probe-pasted-key"
    view = scene("pasted", [(TYPE, b"wakatimeapikey"), (STEP, ENTER),
                            (TYPE, PASTE_START + pasted.encode() + PASTE_END),
                            (STEP, ENTER)])
    print("scene 11: a bracketed paste lands in the row being edited")
    pasted_row = row_of(view, "WakaTime API key")
    if not pasted_row:
        fail("the WakaTime API key row is not on screen after filtering to it")
    elif pasted not in pasted_row:
        fail(f"the pasted value is not on the row: {pasted_row!r}")
    written = os.path.join(cfgs["pasted"], "configs", "settings.conf")
    saved = open(written).read() if os.path.exists(written) else ""
    if f"wakatime_api_key={pasted}" not in saved:
        fail(f"the pasted value was not the one saved: {saved!r}")

    # Scene 12: the same paste with the panel put away leaves the buffer alone.
    # The query and the value both went to the panel; if either had reached the
    # buffer behind it, the text would be on screen once the panel is gone.
    view = scene("kept-out", [(TYPE, b"wakatimeapikey"), (STEP, ENTER),
                              (TYPE, PASTE_START + pasted.encode() + PASTE_END),
                              (STEP, ENTER), (STEP, ESC), (STEP, ESC)])
    print("scene 12: the panel keeps a paste out of the buffer behind it")
    if pasted in view or "wakatimeapikey" in view:
        fail("a pasted value reached the buffer behind the panel")
    # The probe opens the workspace itself, so the surface behind the panel is
    # the empty scratch buffer: the statusline naming it is what says the read
    # above was of the buffer rather than of a panel still painting.
    if "Search" in view:
        fail("the panel is still up, so the buffer behind it was never read")
    elif "[No Name]" not in view:
        fail("the editor's statusline is not on screen after the panel closed")

    # Scene 13: a settings change must not blank the screen, including on a
    # config that names a legacy theme alias. `color_scheme = jot-dark`
    # resolves to `jot`, the name the active theme reports, so a live compare of
    # the raw config value against it read every settings change as a request to
    # switch theme: the theme was re-applied over itself, which clears the
    # screen -- and the frame that followed then diffed every cell as identical
    # and wrote nothing back, so the panel and the buffer sat black until an
    # input changed a cell (a mouse drag repaints the rows it passes). The
    # control scene is the same session minus the toggle: its full-screen clears
    # are the baseline the toggled session must not exceed, because a re-applied
    # theme is what put one there.
    seed_alias = "color_scheme=jot-dark\n"
    control = scene("alias-control", [(TYPE, b"autosave")], seed=seed_alias)
    view = scene("alias", [(TYPE, b"autosave"), (STEP, ENTER)], seed=seed_alias)
    print("scene 13: a legacy theme alias does not blank the screen on a change")
    row = row_of(view, "Auto save")
    baseline_clears = raws["alias-control"].count(b"\x1b[2J")
    clears = raws["alias"].count(b"\x1b[2J")
    if "Settings" not in control or not row_of(control, "Auto save"):
        fail("the control session never opened the panel on the row")
    elif "Settings" not in view or not row:
        fail("the panel is gone after a settings change (a re-applied theme cleared it)")
    elif TOGGLE_ON not in row:
        fail(f"the toggled row is not showing its new state: {row!r}")
    elif clears != baseline_clears:
        fail(f"the change cleared the screen: {baseline_clears} clears without it, {clears} with")

    if failures:
        print()
        for message in failures:
            print(f"FAIL: {message}")
        return 1
    print("\nsettings probe: all scenes passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
