#!/usr/bin/env python3
"""Probe: the left column's stacked EXPLORER, OUTLINE and TIMELINE sections.

The unit tests drive the section layout arithmetic headlessly; what only a real
session proves is the picture and the wiring under it. This probe opens a fresh
git repo in a pty and checks that the three headers stack in the column with the
explorer's file row between the first two and the outline's kind-icon symbol
rows between the outline and the timeline; that the timeline lists both commits
newest first with a compact age on each row ("now" for the fresh one, "3 wks"
for the backdated one); that the footer on the column's last row speaks for the
workspace (the git summary) and never repeats the selected file, whose glyph and
name read as a stray label under the timeline; that clicking an outline row
lands the caret on the symbol's line; and that clicking a timeline row opens
that commit's diff in the right dock. Each of those is a step the layout could
get right on paper and still get wrong on screen.

Every scene is its own session (one screen read per run), because a click needs
the row the previous screen actually painted. A scene that clicks after opening
the file first waits out the transient Tree-sitter toast, which is up for a few
seconds and would otherwise swallow the click.

Usage: test/sidebar_sections_probe.py [path-to-jot] [--dump]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary).
"""
from __future__ import annotations

import os
import re
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

COLS, ROWS = 120, 40
SETTLE, AFTER = 2.5, 0.6

FIRST_SUBJECT = "Add probe functions"
SECOND_SUBJECT = "Tweak beta"
# Ride-side needles: the sidebar's separator column ends the column's text, and a
# long commit subject is truncated in a narrow column, so rows are looked up by a
# prefix that survives both.
FIRST_NEEDLE = "Add probe"
SECOND_NEEDLE = "Tweak beta"

FUNCTION_ICON = "\uf121"  # nf-fa-code, Winbar::symbol_icon("function")

# Lines are kept few and their shapes known so the outline's symbol lines and
# columns are computable: alpha is on the third line, at column 5.
SOURCE = """\
#include <cstdio>

int alpha()
{
  return 1;
}

int beta()
{
  return 2;
}

int main()
{
  return alpha() + beta();
}
"""


def git(root: str, *args: str, env=None) -> str:
    out = subprocess.run(["git", "-C", root, *args], check=True, capture_output=True,
                         text=True, env=env)
    return out.stdout.strip()


def click(x: int, y: int) -> bytes:
    """A press+release at the 0-based screen cell (x, y), as SGR mouse bytes."""
    x, y = x, y + 1  # CSI mouse rows are 1-based
    return ("\x1b[<0;%d;%dM\x1b[<0;%d;%dm" % (x, y, x, y)).encode()


def row_of(text: str, needle: str) -> int:
    """The first screen row whose sidebar half carries `needle`.

    The pane shares every screen row with the column, so a symbol name or a
    commit subject also appears in the buffer's text; splitting on the column's
    separator keeps the search on the sidebar's side of the screen.
    """
    for index, line in enumerate(text.splitlines()):
        if needle in line.split("\u2502")[0]:
            return index
    return -1


def wait_for(*needles: str):
    def check(screen) -> bool:
        text = screen.text()
        return all(needle in text for needle in needles)
    return check


# The transient startup toast shown when a bundled highlight query cannot load.
# It swallows a click while it is on screen, and it appears a moment after the
# buffer is first highlighted, so a scene that clicks just after opening the
# file waits it out (a machine whose queries all load never shows it, and the
# wait returns at once).
TRANSIENT_TOAST = "Tree-sitter:"


def wait_for_toast_gone():
    def check(screen) -> bool:
        return TRANSIENT_TOAST not in screen.text()
    return check


def scene(binary: str, root: str, cfg: str, phases):
    return run_in_pty(binary, [root], b"", settle=SETTLE, after=AFTER, cols=COLS,
                      rows=ROWS, cfg=cfg, cwd=root, phases=phases)


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    if not os.path.isfile(binary):
        print("sidebar sections probe: SKIP - no binary at", binary)
        return 2

    dump = bool(os.environ.get("JOT_PROBE_DUMP")) or "--dump" in sys.argv
    failures: list = []
    layout: dict = {}

    def fail(what: str, detail: str = "") -> None:
        failures.append(what)
        print("sidebar sections probe: FAIL - %s%s" % (what, (" (%s)" % detail) if detail else ""))

    with tempfile.TemporaryDirectory(prefix="jot_sections_probe_") as base:
        root = os.path.join(base, "workspace")
        os.makedirs(root)
        cfg = os.path.join(base, "cfg")
        source_path = os.path.join(root, "probe.cpp")
        with open(source_path, "w") as fh:
            fh.write(SOURCE)

        commit_env = dict(os.environ)
        commit_env["GIT_AUTHOR_NAME"] = commit_env["GIT_COMMITTER_NAME"] = "probe"
        commit_env["GIT_AUTHOR_EMAIL"] = commit_env["GIT_COMMITTER_EMAIL"] = "probe@example.com"
        git(root, "init", "-q", env=commit_env)
        git(root, "add", "-A", env=commit_env)
        # Backdate the first commit so its row's age has to compact to weeks.
        old_env = dict(commit_env)
        stamp = "%d +0000" % int(time.time() - 21 * 86400)
        old_env["GIT_AUTHOR_DATE"] = stamp
        old_env["GIT_COMMITTER_DATE"] = stamp
        git(root, "commit", "-q", "-m", FIRST_SUBJECT, env=old_env)
        with open(source_path, "a") as fh:
            fh.write("\n// touched\n")
        git(root, "add", "-A", env=commit_env)
        git(root, "commit", "-q", "-m", SECOND_SUBJECT, env=commit_env)
        head = git(root, "rev-parse", "--short", "HEAD", env=commit_env)

        # Scene 1: open probe.cpp from the tree and read the settled column.
        def capture(screen) -> bool:
            text = screen.text()
            layout["text"] = text
            for name in ("EXPLORER", "OUTLINE", "TIMELINE", "alpha", "beta", "main",
                         FIRST_NEEDLE, SECOND_NEEDLE):
                layout[name] = row_of(text, name)
            return True

        screen = scene(binary, root, cfg, [
            (0.3, click(5, 2)),  # while no buffer is open: 0 header, 1 root, 2 file
            (0.4, wait_for("alpha", FIRST_NEEDLE, SECOND_NEEDLE)),
            (0.2, capture),
        ])
        if dump:
            print(screen.text())
            print("-" * 70)

        text = layout.get("text", "")
        if any(layout[name] < 0 for name in ("EXPLORER", "OUTLINE", "TIMELINE", "alpha",
                                              "beta", "main", FIRST_NEEDLE, SECOND_NEEDLE)):
            fail("a section header, symbol or commit subject is missing",
                 ", ".join("%s@%d" % (n, layout[n]) for n in ("EXPLORER", "OUTLINE", "TIMELINE",
                                                              "alpha", "beta", "main",
                                                              FIRST_NEEDLE, SECOND_NEEDLE)))
        else:
            if not layout["EXPLORER"] < layout["OUTLINE"] < layout["TIMELINE"]:
                fail("the headers do not stack EXPLORER, OUTLINE, TIMELINE",
                     "rows %s" % {k: layout[k] for k in ("EXPLORER", "OUTLINE", "TIMELINE")})
            file_row = next((i for i in range(layout["EXPLORER"] + 1, layout["OUTLINE"])
                             if "probe.cpp" in text.splitlines()[i]), -1)
            if file_row < 0:
                fail("the explorer paints no file row under its header")
            if not layout["OUTLINE"] < layout["alpha"] < layout["beta"] < layout["main"] < layout["TIMELINE"]:
                fail("the outline rows do not sit inside the outline section",
                     "rows %s" % {k: layout[k] for k in ("OUTLINE", "alpha", "beta", "main",
                                                         "TIMELINE")})
            if not layout["TIMELINE"] < layout[SECOND_NEEDLE] < layout[FIRST_NEEDLE]:
                fail("the timeline does not list the commits newest first",
                     "rows %s" % {k: layout[k] for k in ("TIMELINE", SECOND_NEEDLE,
                                                         FIRST_NEEDLE)})
            lines = text.splitlines()
            if FUNCTION_ICON not in lines[layout["alpha"]]:
                fail("the outline row carries no function kind icon", repr(lines[layout["alpha"]]))
            if "now" not in lines[layout[SECOND_NEEDLE]].split(SECOND_NEEDLE)[-1]:
                fail("the fresh commit shows no 'now' age", repr(lines[layout[SECOND_NEEDLE]]))
            if "3 wks" not in lines[layout[FIRST_NEEDLE]].split(FIRST_NEEDLE)[-1]:
                fail("the backdated commit does not compact to '3 wks'",
                     repr(lines[layout[FIRST_NEEDLE]]))
            if "No symbols found" in text or "Not a Git repository" in text:
                fail("a section fell back to its empty note")

            # The footer is the column's last row, directly under the timeline:
            # it used to echo the selected row (a glyph and the file name), which
            # read as a stray label there. It belongs to the workspace now.
            if len(lines) > ROWS - 2:
                footer = lines[ROWS - 2].split("\u2502")[0]
                if "probe.cpp" in footer:
                    fail("the footer repeats the selected file's name under the timeline",
                         repr(footer))
                elif "\ue725" not in footer:
                    fail("the footer lost the workspace's git summary", repr(footer))

        # Scene 2: a click on the outline's alpha row lands the caret there.
        if layout.get("alpha", -1) >= 0:
            status: dict = {}

            def capture_status(screen) -> bool:
                status["line"] = screen.text().splitlines()[-1]
                return True

            scene(binary, root, cfg, [
                (0.3, click(5, 2)),
                (0.4, wait_for("alpha", FIRST_NEEDLE, SECOND_NEEDLE)),
                (0.2, wait_for_toast_gone()),
                (0.2, click(5, layout["alpha"])),
                (0.8, capture_status),
            ])
            alpha_line = SOURCE.splitlines().index("int alpha()") + 1
            alpha_col = SOURCE.splitlines()[alpha_line - 1].index("alpha") + 1
            status_line = status.get("line", "")
            # Either the jump's message ("probe.cpp:3  function  alpha") or the
            # buffer's cursor segment ("probe.cpp <rule> 3:5") proves the caret
            # moved; the status line separates its segments with a glyph, so the
            # match must not require a plain gap between the name and the caret.
            if not (("probe.cpp:%d" % alpha_line) in status_line
                    or re.search(r"probe\.cpp.*?%d:%d\b" % (alpha_line, alpha_col),
                                 status_line)):
                fail("the outline click did not move the caret to the symbol",
                     "%r" % status_line)

        # Scene 3: a click on the timeline's newest row opens the commit diff.
        if layout.get(SECOND_NEEDLE, -1) >= 0:
            diff: dict = {}

            def capture_diff(screen) -> bool:
                diff["text"] = screen.text()
                return True

            screen = scene(binary, root, cfg, [
                (0.3, click(5, 2)),
                (0.4, wait_for(FIRST_NEEDLE, SECOND_NEEDLE)),
                (0.2, wait_for_toast_gone()),
                (0.2, click(5, layout[SECOND_NEEDLE])),
                (1.0, wait_for("commit " + head)),
                (0.3, capture_diff),
            ])
            if dump:
                print(screen.text())
                print("-" * 70)
            if ("commit " + head) not in diff.get("text", ""):
                fail("the timeline click opened no commit diff", "%r missing" % ("commit " + head))

    if failures:
        print("sidebar sections probe: FAIL")
        return 1
    print("sidebar sections probe: PASS - headers, outline rows, commit ages, jump and diff")
    return 0


if __name__ == "__main__":
    sys.exit(main())
