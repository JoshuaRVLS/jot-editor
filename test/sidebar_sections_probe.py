#!/usr/bin/env python3
"""Probe: the left column -- one explorer section, its tree and its footer.

The unit tests drive the section layout arithmetic headlessly; what only a real
session proves is the picture and the wiring under it. This probe opens a fresh
git repo in a pty and checks that the column carries one section (the EXPLORER
header, its tree and the workspace footer), with no OUTLINE or TIMELINE header
and no branch name: the branch belongs to the window title and the status line,
not to a list about files. It then checks the footer on the column's last row,
which speaks for the workspace (the git change summary) and never repeats the
selected file, whose glyph and name read as a stray label there; that a click
on a file row opens that file; and that the state a session leaves behind --
the explorer collapsed and a folder open -- comes back on the next launch,
which is only visible in a second real session. Each of those is a step the
layout could get right on paper and still get wrong on screen.

Every scene is its own session (one screen read per run), because a click needs
the row the previous screen actually painted. The restart scene quits with
Ctrl+Q instead of being killed: the session file is only written on a clean
exit.

Usage: test/sidebar_sections_probe.py [path-to-jot] [--dump]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary).
"""
from __future__ import annotations

import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

COLS, ROWS = 120, 40
SETTLE, AFTER = 2.5, 0.6

SOURCE = """\
#include <cstdio>

int main()
{
  return 0;
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
    """The first screen row whose sidebar half carries `needle`; -1 = none.

    A row without the column's separator is not a sidebar row at all (the
    status line is the one full-width row), so the branch name and the open
    file's label that the bar carries cannot be mistaken for column content.
    """
    for index, line in enumerate(text.splitlines()):
        if "\u2502" not in line:
            continue
        # The column's separator is the row's last one; the tree paints guide
        # glyphs of its own inside the column, left of the text.
        if needle in line.rsplit("\u2502", 1)[0]:
            return index
    return -1


def wait_for_sidebar(*needles: str):
    def check(screen) -> bool:
        text = screen.text()
        return all(row_of(text, needle) >= 0 for needle in needles)
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

    def fail(what: str, detail: str = "") -> None:
        failures.append(what)
        print("sidebar sections probe: FAIL - %s%s" % (what, (" (%s)" % detail) if detail else ""))

    with tempfile.TemporaryDirectory(prefix="jot_sidebar_probe_") as base:
        root = os.path.join(base, "workspace")
        os.makedirs(root)
        cfg = os.path.join(base, "cfg")
        with open(os.path.join(root, "probe.cpp"), "w") as fh:
            fh.write(SOURCE)
        with open(os.path.join(root, "untracked.txt"), "w") as fh:
            fh.write("kept out of the commit\n")

        commit_env = dict(os.environ)
        commit_env["GIT_AUTHOR_NAME"] = commit_env["GIT_COMMITTER_NAME"] = "probe"
        commit_env["GIT_AUTHOR_EMAIL"] = commit_env["GIT_COMMITTER_EMAIL"] = "probe@example.com"
        git(root, "init", "-q", env=commit_env)
        git(root, "add", "probe.cpp", env=commit_env)
        git(root, "commit", "-q", "-m", "Add the probe file", env=commit_env)
        branch = git(root, "rev-parse", "--abbrev-ref", "HEAD", env=commit_env)

        # Scene 1: the column at startup. The footer needs the async git status,
        # so it is waited for before the read; a clean repo would leave the
        # footer empty (counts only), so the fixture keeps one untracked file
        # for it to speak about.
        layout: dict = {}

        def capture(screen) -> bool:
            text = screen.text()
            layout["text"] = text
            for name in ("EXPLORER", "OUTLINE", "TIMELINE", "probe.cpp"):
                layout[name] = row_of(text, name)
            layout["branch"] = row_of(text, branch)
            return True

        scene(binary, root, cfg, [
            (1.0, wait_for_sidebar("EXPLORER", "?1")),
            (0.2, capture),
        ])

        lines = layout.get("text", "").splitlines()
        if layout["EXPLORER"] < 0:
            fail("the explorer header is missing")
        for gone in ("OUTLINE", "TIMELINE"):
            if layout[gone] >= 0:
                fail("the %s header is still in the column" % gone, "row %d" % layout[gone])
        if layout["branch"] >= 0:
            fail("the branch name is still in the column",
                 "%r at row %d" % (branch, layout["branch"]))
        if len(lines) > ROWS - 2:
            footer = lines[ROWS - 2].rsplit("\u2502", 1)[0]
            if "?1" not in footer:
                fail("the footer lost the workspace's git summary", repr(footer))
            if "probe.cpp" in footer or "untracked.txt" in footer:
                fail("the footer repeats a file's name", repr(footer))
        if layout["probe.cpp"] < 0:
            fail("the tree paints no file row")

        # Scene 2: a click on the file row opens it. The row comes from the
        # screen above, so the click aims where the painter really put it.
        opened: dict = {}

        def capture_status(screen) -> bool:
            opened["status"] = screen.text().splitlines()[-1]
            return True

        file_row = layout.get("probe.cpp", -1)
        if file_row >= 0:
            screen = scene(binary, root, cfg, [
                (0.2, click(5, file_row)),
                (0.8, lambda s: "probe.cpp" in s.text().splitlines()[-1]),
                (0.3, capture_status),
            ])
            if dump:
                print(screen.text())
                print("-" * 70)
            if not opened.get("status"):
                fail("the click on the file row painted no status line to read")
            elif "probe.cpp" not in opened["status"]:
                fail("the click on the file row opened no buffer", repr(opened["status"]))

        # The rows of the tree with the folders on disk, none open yet. They land
        # in the fixture only now, so the rows scene 1 was written against cannot
        # move under it; the probe reads their rows in a scene of their own
        # because the click that follows has to aim at a painted row.
        os.makedirs(os.path.join(root, "nested"))
        os.makedirs(os.path.join(root, "closed"))
        with open(os.path.join(root, "nested", "inside.txt"), "w") as fh:
            fh.write("in\n")
        with open(os.path.join(root, "closed", "shut.txt"), "w") as fh:
            fh.write("shut\n")

        folders: dict = {}

        def capture_folders(screen) -> bool:
            text = screen.text()
            folders["header"] = row_of(text, "EXPLORER")
            folders["nested"] = row_of(text, "nested")
            folders["closed"] = row_of(text, "closed")
            return (folders["header"] >= 0 and folders["nested"] >= 0
                    and folders["closed"] >= 0)

        scene(binary, root, cfg, [(1.0, capture_folders)])

        # Scene 3: leave a column behind -- the nested folder open and the
        # explorer collapsed -- and quit cleanly, which is when the session is
        # written.
        header_row = folders.get("header", -1)
        nested_row = folders.get("nested", -1)
        if header_row < 0 or nested_row < 0:
            fail("the folder and header rows never painted to click",
                 "header %d, nested %d" % (header_row, nested_row))
        else:
            scene(binary, root, cfg, [
                (0.2, click(5, nested_row)),
                (0.8, wait_for_sidebar("inside.txt")),
                (0.2, click(5, header_row)),  # the header folds the tree away
                (0.5, lambda s: "\u25b8" in s.text().splitlines()[header_row]),
                (0.4, b"\x11"),               # Ctrl+Q: quit, writing the session
                (0.8, lambda s: True),
            ])

            # Scene 4: the same workspace, a fresh process. The collapsed column
            # has to be on screen again, and expanding it back has to show the
            # folder the session left open -- and only that one.
            restored: dict = {}
            expanded: dict = {}

            def capture_restored(screen) -> bool:
                restored["text"] = screen.text()
                return True

            def capture_expanded(screen) -> bool:
                expanded["text"] = screen.text()
                return True

            screen = scene(binary, root, cfg, [
                (1.0, capture_restored),
                (0.2, click(5, header_row)),  # reopen the section
                (0.8, wait_for_sidebar("inside.txt")),
                (0.3, capture_expanded),
            ])
            if dump:
                print(screen.text())
                print("-" * 70)

            if not restored:
                fail("the resumed session painted nothing to read the column from")
            else:
                text = restored["text"]
                header = row_of(text, "EXPLORER")
                if header < 0:
                    fail("the resumed column lost its header")
                else:
                    row = text.splitlines()[header].rsplit("\u2502", 1)[0]
                    if "\u25b8" not in row:
                        fail("the collapsed explorer did not come back collapsed", repr(row))
                    if row_of(text, "probe.cpp") >= 0:
                        fail("the collapsed explorer painted its tree")

            if not expanded:
                fail("the resumed session painted nothing to expand the column in")
            else:
                text = expanded["text"]
                if row_of(text, "inside.txt") < 0:
                    fail("the folder that was left open came back collapsed")
                if row_of(text, "shut.txt") >= 0:
                    fail("the folder that was left shut came back open")

    if failures:
        print("sidebar sections probe: FAIL")
        return 1
    print("sidebar sections probe: PASS - one section, its footer, the file click and "
          "the column across a restart")
    return 0


if __name__ == "__main__":
    sys.exit(main())
