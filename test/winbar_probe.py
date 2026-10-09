#!/usr/bin/env python3
"""Probe: the breadcrumb winbar, on the rendered screen.

The row is chrome the rest of the layout has to agree with, and the ways that
agreement can break are quiet: a pane that keeps a *blank* chrome row while the
breadcrumb paints somewhere else pushes the code down for nothing, a hit test
reading a different origin than the painter opens the wrong crumb's menu, and a
menu whose rows do not line up with the pointer picks a file nobody clicked.
None of that fails a unit test of the chain model: the model has no opinion
about cells.

This drives the real binary with a small C++ workspace and reads the cell rows
back out of the pty stream:

  * row 0 is the tab strip, row 1 is the pane's breadcrumb
    (root / folders / file), row 2 is the file's first line of code,
  * moving the caret into a symbol adds that symbol to the row,
  * a click on the file crumb opens its menu over the folder's other files,
  * a click on a row of that menu opens the file it names,
  * Esc, a press outside the menu and a press on its own crumb all take the
    menu off the screen -- it is a Lua float, so a state that closes without
    telling the handler leaves the panel painted with nothing behind it,
  * a folder row wears a chevron and opens its own panel *beside* the level
    that offered it (the cascade), and a row in that panel opens its file,
  * `winbar=off` gives the row back to the code.

Usage: test/winbar_probe.py [path-to-jot] [--dump]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary).
"""
from __future__ import annotations

import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

DOWN = b"\x1b[B"
# The chevron a folder row wears (nf-fa-chevron_right, U+F054).
CHEVRON = "\uf054"
# The Winbar group's background and Normal's in jot-dark (the default scheme),
# as the harness records a truecolour cell (1000 + rgb). The first bug here was
# the whole group resolving to nothing, which left the row on the struct default
# (palette 0, black) -- no band at all on the theme's panel surface.
WINBAR_BG, PANE_BG = 1000 + 0x1C1710, 1000 + 0x14100B

SOURCE = """// winbar probe
class Widget
{
  void render_thing()
  {
    int alpha = 1;
  }
};

int free_function()
{
  return 2;
}
"""

OTHER = """// the sibling file
int sibling()
{
  return 0;
}
"""


def write_workspace(root: str) -> str:
    shutil.rmtree(root, ignore_errors=True)
    os.makedirs(os.path.join(root, "src", "render"), exist_ok=True)
    # A repository marker keeps the finder inside this tree; the winbar itself
    # only needs the workspace root, which the command line file implies through
    # its parent chain once open_workspace has run.
    os.system(f"git init -q {root} 2>/dev/null")
    tabs = os.path.join(root, "src", "render", "tabs.cpp")
    with open(tabs, "w") as fh:
        fh.write(SOURCE)
    with open(os.path.join(root, "src", "render", "buffer.cpp"), "w") as fh:
        fh.write(OTHER)
    return tabs


def write_config(cfg: str, extra: str = "") -> None:
    shutil.rmtree(cfg, ignore_errors=True)
    os.makedirs(os.path.join(cfg, "configs"), exist_ok=True)
    with open(os.path.join(cfg, "configs", "settings.conf"), "w") as fh:
        fh.write(extra)


def row_text(screen, y: int) -> str:
    return "".join(screen.cells[y])


def status_line(screen) -> str:
    for y in range(screen.rows - 1, -1, -1):
        row = row_text(screen, y).rstrip()
        if row.strip():
            return row
    return ""


def run(binary: str, tabs: str, root: str, cfg: str, keys: bytes = b"", phases=None):
    return run_in_pty(binary, [tabs], keys, settle=3.5, after=3.0, cols=100, rows=24, cfg=cfg,
                      cwd=root, phases=phases)


def click(col: int, y: int) -> bytes:
    """A press (and release) at 1-based (col, y), the SGR mouse encoding."""
    return f"\x1b[<0;{col};{y}M\x1b[<0;{col};{y}m".encode()


def motion(col: int, y: int) -> bytes:
    """A bare motion at 1-based (col, y): button 35, no buttons held."""
    return f"\x1b[<35;{col};{y}M".encode()


def box_on_screen(screen) -> bool:
    """Whether a menu panel's frame is still painted anywhere on screen."""
    body = "".join(row_text(screen, y) for y in range(1, min(screen.rows, 18)))
    return "┌" in body and "└" in body


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    dump = "--dump" in sys.argv
    if not os.path.exists(binary):
        print(f"winbar probe: SKIP - no binary at {binary}")
        return 2

    root = "/tmp/jot_winbar_probe"
    tabs = write_workspace(root)
    cfg = "/tmp/jot_winbar_probe_cfg"
    write_config(cfg)
    failures = []

    # 1. The breadcrumb owns row 1, and row 2 is the first line of code.
    screen = run(binary, tabs, root, cfg)
    if dump:
        print(screen.text())
        print("-" * 70)
    strip = row_text(screen, 0)
    crumbs = row_text(screen, 1)
    code = row_text(screen, 2)
    print(f"strip: {strip.strip()[:60]!r}")
    print(f"winbar: {crumbs.strip()[:70]!r}")
    print(f"row 2: {code.strip()[:60]!r}")
    for part in ("jot_winbar_probe", "src", "render", "tabs.cpp"):
        if part not in crumbs:
            failures.append(f"the breadcrumb row does not name {part!r} (row: {crumbs.strip()!r})")
    # The folders are the breadcrumb's own: the strip names files, never paths.
    for part in ("src", "render"):
        if part in strip:
            failures.append(f"the strip carries the breadcrumb's {part!r}")
    if "\u203a" not in crumbs:
        failures.append("the breadcrumb row has no crumb separator")
    # The row's own band: every cell the crumbs sit on wears the theme's Winbar
    # background, so the breadcrumb reads as chrome rather than as labels
    # floating on the editor's own background.
    first = len(crumbs) - len(crumbs.lstrip())
    last = len(crumbs.rstrip())
    unbanned = [c for c in range(first, last) if screen.bg[1][c] != WINBAR_BG]
    print(f"winbar band: row 1 cols {first}..{last} at bg {WINBAR_BG}, "
          f"pane bg {PANE_BG}, unbanded {len(unbanned)}")
    if unbanned:
        failures.append(f"the breadcrumb row is not banded at {unbanned[:5]} "
                        f"(bg {[screen.bg[1][c] for c in unbanned[:5]]}, want {WINBAR_BG})")
    if "winbar probe" not in code:
        failures.append("the first code row is not below the breadcrumb "
                        f"(row 2: {code.strip()!r})")

    # 2. The symbol half follows the caret: five lines down is inside
    # render_thing(), whose class and name both have to show up.
    screen = run(binary, tabs, root, cfg, keys=DOWN * 5)
    if dump:
        print(screen.text())
        print("-" * 70)
    crumbs = row_text(screen, 1)
    print(f"winbar with the caret in a symbol: {crumbs.strip()[:80]!r}")
    for part in ("Widget", "render_thing"):
        if part not in crumbs:
            failures.append(f"the caret's symbol {part!r} is not on the breadcrumb "
                            f"(row: {crumbs.strip()!r})")

    # 3. A click on the file crumb opens its menu, listing the folder's files
    # with the one the chain is on marked.
    col = crumbs.index("tabs.cpp")
    screen = run(binary, tabs, root, cfg, phases=[(3.0, click(col + 1, 2))])
    if dump:
        print(screen.text())
        print("-" * 70)
    menu_box = "".join(row_text(screen, y) for y in range(2, 8))
    menu_rows = [name for name in ("buffer.cpp", "tabs.cpp") if name in menu_box]
    print(f"menu title row: {row_text(screen, 2).strip()[:60]!r}; rows: {menu_rows}")
    if "tabs.cpp" not in row_text(screen, 2):
        failures.append("the crumb's menu is not titled by the crumb")
    if len(menu_rows) != 2:
        failures.append(f"the crumb's menu does not list the folder (saw {menu_rows})")

    # 4. Clicking the *other* file's row opens that file: the strip loses
    # tabs.cpp and the breadcrumb follows the new file.
    sibling_row = None
    for y in range(2, 12):
        if "buffer.cpp" in row_text(screen, y):
            sibling_row = y
            break
    if sibling_row is None:
        failures.append("the menu never painted the sibling file")
    else:
        screen = run(binary, tabs, root, cfg,
                     phases=[(3.0, click(col + 1, 2)), (1.5, click(col + 3, sibling_row + 1))])
        if dump:
            print(screen.text())
            print("-" * 70)
        strip = row_text(screen, 0)
        crumbs = row_text(screen, 1)
        print(f"after picking the sibling: strip {strip.strip()[:50]!r}, "
              f"winbar {crumbs.strip()[:60]!r}")
        if "buffer.cpp" not in strip:
            failures.append("the sibling never opened (its tab is not on the strip)")
        if "buffer.cpp" not in crumbs:
            failures.append("the breadcrumb did not follow the file the menu opened")

    # 5. The menu comes off the screen however it is dismissed. Each of these
    # leaves the panel painted if the close never reaches its Lua handler: the
    # box stays over the pane with no state behind it, so its rows answer
    # nothing and nothing on screen dismisses it.
    for label, dismiss in (("Esc", b"\x1b"),
                           ("a press outside it", click(4, 8)),
                           ("a press on its own crumb", click(col + 1, 2))):
        screen = run(binary, tabs, root, cfg, phases=[(3.0, click(col + 1, 2)), (1.5, dismiss)])
        body = " ".join(row_text(screen, y).strip() for y in range(2, 10))
        print(f"after {label}: {body[:60]!r}")
        if box_on_screen(screen):
            failures.append(f"the menu stayed on screen after {label}")

    # 6. A folder row opens a panel of its own *beside* the level that offered
    # it (the cascade), and the chevron is what says so.
    base = run(binary, tabs, root, cfg)
    crumb_row = row_text(base, 1)
    src_col = crumb_row.index("src") + 1
    screen = run(binary, tabs, root, cfg, phases=[(3.0, click(src_col, 2))])
    parent_rows = "".join(row_text(screen, y) for y in range(2, 9))
    print(f"src crumb menu: {row_text(screen, 3).strip()[:60]!r}")
    if "render" not in parent_rows:
        failures.append("the src crumb's menu does not offer the render folder")
    if CHEVRON not in parent_rows:
        failures.append("the folder row wears no chevron")

    # Hovering the folder row opens its listing beside the first panel.
    screen = run(binary, tabs, root, cfg,
                 phases=[(3.0, click(src_col, 2)), (1.2, motion(src_col + 2, 4))])
    cascade = "\n".join(row_text(screen, y) for y in range(2, 10))
    print(f"cascade: {cascade.splitlines()[1].strip()[:70]!r}")
    if cascade.count("render") < 2:
        failures.append("the folder row did not open a panel of its own")
    for name in ("buffer.cpp", "tabs.cpp"):
        if name not in cascade:
            failures.append(f"the folder's own panel does not list {name!r}")

    # A row in that panel opens *its* file, and the whole cascade goes with it.
    child_x = None
    child_y = None
    for y in range(2, 12):
        text = row_text(screen, y)
        if "buffer.cpp" in text:
            child_x = text.index("buffer.cpp") + 1
            child_y = y + 1
            break
    if child_x is None:
        failures.append("the folder's panel never painted a row to pick")
    else:
        screen = run(binary, tabs, root, cfg,
                     phases=[(3.0, click(src_col, 2)), (1.2, motion(src_col + 2, 4)),
                             (1.2, click(child_x, child_y))])
        strip = row_text(screen, 0)
        crumbs = row_text(screen, 1)
        print(f"after picking in the folder's panel: strip {strip.strip()[:50]!r}, "
              f"winbar {crumbs.strip()[:60]!r}")
        if "buffer.cpp" not in strip:
            failures.append("a row in the folder's own panel did not open its file")
        if "buffer.cpp" not in crumbs:
            failures.append("the breadcrumb did not follow the file the cascade opened")
        if box_on_screen(screen):
            failures.append("the cascade stayed on screen after picking a file")

    # 7. `winbar=off` hands the row back: the code starts on row 1.
    off_cfg = "/tmp/jot_winbar_probe_off_cfg"
    write_config(off_cfg, extra="winbar=off\n")
    screen = run(binary, tabs, root, off_cfg)
    if dump:
        print(screen.text())
        print("-" * 70)
    first = row_text(screen, 1)
    print(f"winbar=off, row 1: {first.strip()[:60]!r}")
    if "winbar probe" not in first:
        failures.append(f"winbar=off kept the row (row 1: {first.strip()!r})")

    if failures:
        print("winbar probe: FAIL")
        for failure in failures:
            print(f"  - {failure}")
        return 1
    print("winbar probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
