#!/usr/bin/env python3
"""Probe: the home screen, on the rendered screen.

The screen is the way into a project: it lists the folders the editor
remembers and the ones you pinned, the folder you are in, and it prints on
every row the key that opens it. The ways that goes wrong are all quiet on a
unit test of the model -- a key painted next to one row but dispatched to
another, a query that filters the list but not the rows Enter acts on, a
selection band that runs the full width of a panel much wider than its names --
so the probe drives the real binary and reads the cells back out of the pty
stream:

  * the launch folder is named on the context line and the remembered folders
    are listed with a key each,
  * a pinned folder is listed with its star where nothing else knows it, and
    unpinning it from the screen takes the row off again,
  * the selection band covers the label and stops, so the key at the far edge
    keeps its own background,
  * typing filters the rows and the query line says what by, without dismissing
    the screen,
  * the first Esc takes the query back, the second closes the screen,
  * the digit a row prints is the digit that opens it,
  * '/' lets a shortcut letter start a query instead of firing,
  * the header paints the big block wordmark with one accent per letter, and a
    list row leads with a file icon inked on its own,
  * a query and its Enter in one read still open the row the query left, and a
    query that matches nothing opens nothing,
  * a click on a row opens what it names,
  * a file already open does not leave its tab on the home screen: the
    workspace strip is workspace chrome, not part of the launcher,
  * a bare `jot` lands on the home screen even when the recent-workspace list
    would have something to resume.

Usage: test/home_open_probe.py [path-to-jot] [--dump]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary).
"""
from __future__ import annotations

import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

ESC = b"\x1b"
ENTER = b"\r"
CTRL_P = b"\x10"

# The launch folder, the two projects the editor remembers beside it, and the
# project only a pin knows about. The parent path holds no letters the queries
# below use, so "beta" can only mean the project.
ROOT = "/tmp/jhx"

# The transient startup toast shown when a bundled highlight query cannot load
# paints the top-right corner for a few seconds and covers the wordmark row's
# "/ filter" legend. A machine whose queries all load never shows it, and the
# wait returns at once.
TRANSIENT_TOAST = "Tree-sitter:"


def write_tree(root: str) -> str:
    shutil.rmtree(root, ignore_errors=True)
    for name in ("work", "alpha", "beta", "gamma"):
        os.makedirs(os.path.join(root, name), exist_ok=True)
    with open(os.path.join(root, "alpha", "package.json"), "w") as fh:
        fh.write('{"name": "alpha"}\n')
    with open(os.path.join(root, "alpha", "index.js"), "w") as fh:
        fh.write("console.log('alpha');\n")
    with open(os.path.join(root, "beta", "go.mod"), "w") as fh:
        fh.write("module beta\n")
    with open(os.path.join(root, "gamma", "Cargo.toml"), "w") as fh:
        fh.write("[package]\nname = \"gamma\"\n")
    with open(os.path.join(root, "work", "notes.txt"), "w") as fh:
        fh.write("hello\n")
    return os.path.join(root, "work")


def write_config(cfg: str, work: str) -> None:
    """A config home holding what the screen lists: the folders and the file the
    editor remembers, and the one folder it only knows because it is pinned."""
    shutil.rmtree(cfg, ignore_errors=True)
    configs = os.path.join(cfg, "configs")
    os.makedirs(configs, exist_ok=True)
    with open(os.path.join(configs, "settings.conf"), "w") as fh:
        fh.write("")
    with open(os.path.join(configs, "recent_workspaces.txt"), "w") as fh:
        fh.write(os.path.join(ROOT, "alpha") + "\n" + os.path.join(ROOT, "beta") + "\n")
    with open(os.path.join(configs, "recent_files.txt"), "w") as fh:
        fh.write(os.path.join(work, "notes.txt") + "\n")
    with open(os.path.join(configs, "home_pins.txt"), "w") as fh:
        fh.write(os.path.join(ROOT, "gamma") + "\n")


def row_text(screen, y: int) -> str:
    return "".join(screen.cells[y])


def find_row(screen, needle: str, start: int = 0) -> int:
    """The first row holding `needle`, or -1. The last row is the status line."""
    for y in range(start, screen.rows - 1):
        if needle in row_text(screen, y):
            return y
    return -1


def row_key(screen, y: int) -> str:
    """The key a row prints at its right edge."""
    for ch in reversed(row_text(screen, y)):
        if ch == " ":
            continue
        return ch if ch.isdigit() else ""
    return ""


def screen_has(screen, needle: str) -> bool:
    return any(needle in row_text(screen, y) for y in range(screen.rows))


def content_x(screen) -> int:
    """The panel's left edge, as the renderer centres it."""
    width = max(1, min(screen.cols - 4, 118))
    return max(1, (screen.cols - width) // 2)


def band(screen, y: int, x_from: int, x_to: int, plain_bg) -> tuple[int, int]:
    """First and last cell of `y` wearing a background other than `plain_bg`."""
    first = -1
    last = -1
    for x in range(x_from, x_to):
        if screen.bg[y][x] != plain_bg:
            if first < 0:
                first = x
            last = x
    return first, last


def home_gone(screen) -> bool:
    """Waits for the frame the closed screen leaves: the panel is a float, so
    the repaint of the cells under it is what takes the rows off the screen."""
    return (find_row(screen, "Recent Folders") < 0
            and not screen_has(screen, "JOT Developer workspace"))


def click(col: int, y: int) -> bytes:
    """A press (and release) at 1-based (col, y), the SGR mouse encoding."""
    return f"\x1b[<0;{col};{y}M\x1b[<0;{col};{y}m".encode()


def toast_gone(screen) -> bool:
    return TRANSIENT_TOAST not in screen.text()


def run(binary: str, work: str, cfg: str, keys: bytes = b"", phases=None, args=()):
    return run_in_pty(binary, list(args), keys, settle=3.5, after=2.5, cols=110, rows=30, cfg=cfg,
                      cwd=work, phases=phases)


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    dump = "--dump" in sys.argv
    if not os.path.exists(binary):
        print(f"home probe: SKIP - no binary at {binary}")
        return 2

    work = write_tree(ROOT)
    cfg = "/tmp/jhx_cfg"
    write_config(cfg, work)
    failures = []

    def shot(screen, label: str):
        if dump:
            print(f"--- {label} ---")
            print(screen.text())

    # 1. The launch folder names itself, the folders the editor remembers are
    # listed under a title with a key on every row, and the pinned folder leads
    # them with its star -- nothing else on the machine knows it is a project.
    base = run(binary, work, cfg, phases=[(0.2, toast_gone)])
    shot(base, "startup")
    context_y = find_row(base, "Here")
    folders_y = find_row(base, "Recent Folders")
    # The folder list, not the whole screen: the Resume row names a path of its
    # own, and it can hold one of these folders' names.
    alpha_y = find_row(base, "alpha", max(folders_y, 0))
    beta_y = find_row(base, "beta", max(folders_y, 0))
    gamma_y = find_row(base, "gamma", max(folders_y, 0))
    print(f"context row {context_y}: {row_text(base, context_y).strip()[:70]!r}")
    print(f"folders row {folders_y}, alpha row {alpha_y}: "
          f"{row_text(base, alpha_y).strip()[:70]!r}")
    if context_y < 0 or "work" not in row_text(base, context_y):
        failures.append("the context line does not name the launch folder")
    if folders_y < 0:
        failures.append("the remembered folders have no section of their own")
    for name, y in (("alpha", alpha_y), ("beta", beta_y)):
        if y < 0:
            failures.append(f"the remembered folder {name!r} is not listed")
        elif not row_key(base, y):
            failures.append(f"the {name!r} row prints no key "
                            f"(row: {row_text(base, y).rstrip()!r})")
    if gamma_y < 0:
        failures.append("the pinned folder is not listed")
    elif gamma_y != folders_y + 1:
        failures.append("the pinned folder does not lead the list it belongs to")
    elif "\u2605" not in row_text(base, gamma_y):
        failures.append(f"the pinned row carries no star "
                        f"(row: {row_text(base, gamma_y).rstrip()!r})")
    if not screen_has(base, "/ filter"):
        failures.append("the key legend is not on the screen")

    # 1b. The header: three rows of block letters above the lists, one accent
    # per letter, and a list row whose icon carries a color of its own.
    left = content_x(base)
    art_rows = [y for y in range(0, 6) if "\u2588" in row_text(base, y)]
    accents = {base.fg[y][x] for y in art_rows for x in range(left, left + 12)
               if base.cells[y][x] == "\u2588"}
    print(f"wordmark rows {art_rows}, accents {len(accents)}")
    if len(art_rows) != 3:
        failures.append(f"the big wordmark is not three block rows (saw {len(art_rows)})")
    elif len(accents) < 3:
        failures.append(f"the wordmark letters share one color ({len(accents)} accents)")
    notes_y = find_row(base, "notes.txt")
    if notes_y > 0:
        label_x = row_text(base, notes_y).index("notes.txt")
        icon_x = label_x - 2
        if base.cells[notes_y][icon_x] == " ":
            failures.append("a list row has no icon before its name")
        elif base.fg[notes_y][icon_x] == base.fg[notes_y][label_x]:
            failures.append("the file icon does not carry a color of its own")

    # 1c. Unpinning from the screen: the rows in front of the list are the
    # editor's own commands, so the walk to a folder row is that many j's, and
    # the pin was the only thing listing gamma -- 'f' on it takes the row off.
    actions = 8
    screen = run(binary, work, cfg, keys=b"j" * actions + b"f")
    shot(screen, "gamma unpinned")
    print(f"after unpinning: gamma {find_row(screen, 'gamma')}")
    if find_row(screen, "gamma") >= 0:
        failures.append("unpinning from the screen left the row up")
    if find_row(screen, "Recent Folders") < 0:
        failures.append("unpinning dismissed the screen with it")

    # 2. The selection band hugs the label: the row's first cell and the key at
    # its far edge stay on the panel background. The selected row is the first
    # one whose label cell is not on the panel's own background.
    x0 = content_x(base)
    plain_bg = base.bg[base.rows - 2][x0]  # an empty row under the lists
    sel_y = -1
    for y in range(4, base.rows - 2):
        for x in range(x0, x0 + 34):
            if base.cells[y][x] != " ":
                if base.bg[y][x] != plain_bg:
                    sel_y = y
                break
        if sel_y >= 0:
            break
    if sel_y < 0:
        failures.append("no row carries a selection band")
    else:
        first, last = band(base, sel_y, x0, x0 + 34, plain_bg)
        banded = "".join(base.cells[sel_y][x] for x in range(first, last + 1))
        print(f"band on row {sel_y}: cols {first}..{last} {banded.strip()!r}")
        if "Open Folder / File" not in banded and "Resume" not in banded:
            failures.append(f"the band is not on the label (saw {banded.strip()!r})")
        if first != x0 + 1:
            failures.append(f"the band starts at {first}, not on the label at {x0 + 1}")
        key_x = len(row_text(base, sel_y).rstrip()) - 1
        if base.bg[sel_y][key_x] != plain_bg:
            failures.append("the key at the right edge sits on the selection band")

    # 3. Typing filters the rows and says so, without dismissing the screen.
    screen = run(binary, work, cfg, keys=b"beta")
    shot(screen, "typed beta")
    print(f"typed beta: alpha {find_row(screen, 'alpha')}, beta {find_row(screen, 'beta')}")
    if not screen_has(screen, "Filter  beta"):
        failures.append("the query line does not show the typed filter")
    if find_row(screen, "alpha") >= 0:
        failures.append("a filtered-out project is still on the screen")
    if find_row(screen, "beta") < 0:
        failures.append("the matching project was filtered off the screen")

    # 4. Esc with a query takes the query back and leaves the screen up.
    screen = run(binary, work, cfg, keys=b"beta",
                 phases=[(0.8, ESC), (0.1, lambda s: not screen_has(s, "Filter"))])
    shot(screen, "query taken back")
    if find_row(screen, "alpha") < 0 or find_row(screen, "Recent Folders") < 0:
        failures.append("Esc with a query dismissed the screen with it")

    # ... and Esc on its own closes it. The two presses are pinned apart like
    # this on purpose: two Esc bytes read in the same pass are Alt+Esc, one
    # chord, so a probe that sent them together would be pinning the decoder,
    # not the screen. The handler's own two-press order is the unit test's.
    screen = run(binary, work, cfg, phases=[(1.0, ESC), (0.1, home_gone)])
    shot(screen, "closed with Esc")
    print(f"closed with Esc: folders row {find_row(screen, 'Recent Folders')}")
    if find_row(screen, "Recent Folders") >= 0 or find_row(screen, "alpha") >= 0:
        failures.append("Esc did not close the home screen")

    # 5. The digit a row prints is the digit that opens it: the explorer for
    # that project takes the frame.
    key = row_key(base, alpha_y)
    if not key:
        failures.append("no key was printed on the project row to press")
    else:
        screen = run(binary, work, cfg, keys=key.encode())
        shot(screen, f"after pressing {key}")
        print(f"opened with {key}: {row_text(screen, 0).strip()[:60]!r}")
        if screen_has(screen, "Recent Folders"):
            failures.append("the home screen stayed up after its key was pressed")
        if not screen_has(screen, "package.json") and not screen_has(screen, "index.js"):
            failures.append("the project the key named did not open")

    # 6. '/' arms a query, so a letter that is also a shortcut types instead of
    # firing (t is the theme chooser).
    screen = run(binary, work, cfg, keys=b"/t")
    shot(screen, "armed query")
    if not screen_has(screen, "Filter  t"):
        failures.append("'/' did not arm the query for a shortcut letter")
    if find_row(screen, "Recent Folders") < 0:
        failures.append("the armed query dismissed the screen")

    # 7. A query and its Enter in one read: nothing renders between the two, so
    # the rows Enter acts on have to be rebuilt for the query that was typed.
    screen = run(binary, work, cfg, keys=b"beta" + ENTER)
    shot(screen, "beta and Enter in one read")
    print(f"beta+Enter: {row_text(screen, 0).strip()[:60]!r}")
    if screen_has(screen, "Recent Folders"):
        failures.append("Enter in the same read as the query did not open the match")
    if not screen_has(screen, "go.mod"):
        failures.append("Enter opened something other than the queried project")

    # ... and a query that matches nothing opens nothing, not the first row of
    # the list from before the query. An empty filter leaves no section to
    # paint, so the screen staying up is what says nothing was opened.
    screen = run(binary, work, cfg, keys=b"zzz" + ENTER)
    shot(screen, "zzz and Enter in one read")
    print(f"zzz+Enter: {row_text(screen, 3).strip()[:60]!r}")
    if not screen_has(screen, "JOT Developer workspace"):
        failures.append("a query with no match still opened a row")
    if screen_has(screen, "go.mod") or screen_has(screen, "Cargo.toml"):
        failures.append("a query with no match opened a project")

    # 8. A click on a row opens what it names.
    click_y = beta_y
    click_col = row_text(base, click_y).index("beta") + 1
    screen = run(binary, work, cfg, phases=[(2.5, click(click_col, click_y + 1))])
    shot(screen, "clicked the beta row")
    print(f"click at ({click_col}, {click_y + 1}): {row_text(screen, 0).strip()[:60]!r}")
    if not screen_has(screen, "go.mod"):
        failures.append(f"the click at ({click_col}, {click_y + 1}) did not open the project")

    # 9. The home screen is not the workspace: a file that is already open must
    # not leave its tab painted on row 0. The screen is raised from the editor
    # through the palette (Ctrl+P, "home"), so a buffer is on screen when it
    # opens; the tab strip would sit on the top row, above the wordmark.
    screen = run(binary, work, cfg,
                 phases=[(0.6, CTRL_P),
                         (1.0, b"home"),
                         (0.8, ENTER),
                         (1.0, lambda s: screen_has(s, "JOT Developer workspace"))],
                 args=("notes.txt",))
    shot(screen, "home raised with a file open")
    print(f"row 0 with home up: {row_text(screen, 0).strip()[:50]!r}")
    if not screen_has(screen, "JOT Developer workspace"):
        failures.append("the home screen did not open over the open buffer")
    elif row_text(screen, 0).strip():
        failures.append("the opened file's tab is painted on the home screen "
                        f"(row 0: {row_text(screen, 0).strip()[:40]!r})")

    # 10. A bare `jot` lands on the home screen even when the recent-workspace
    # list would have something to resume: the previous session is offered as a
    # row on the screen, not reopened under the user.
    with open(os.path.join(cfg, "configs", "recent_workspaces.txt"), "w") as fh:
        fh.write(os.path.join(ROOT, "beta") + "\n")
    screen = run(binary, work, cfg, phases=[(0.2, toast_gone)])
    shot(screen, "bare jot with a recent workspace on disk")
    print(f"bare jot with a recent workspace: row 3 {row_text(screen, 3).strip()[:50]!r}")
    if not screen_has(screen, "JOT Developer workspace"):
        failures.append("a bare `jot` did not land on the home screen")
    if screen_has(screen, "go.mod") or screen_has(screen, "index.js"):
        failures.append("a bare `jot` reopened the recent workspace instead of home")

    if failures:
        print("home probe: FAIL")
        for failure in failures:
            print(f"  - {failure}")
        return 1
    print("home probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
