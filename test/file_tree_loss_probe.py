#!/usr/bin/env python3
"""Probe: the explorer survives the workspace changing under it.

The file tree is read from disk on a watch tick and again on every explorer
key, over a workspace that can be replaced at any moment: a checkout, a build,
a directory deleted while the editor has it open. Two crashes came out of that
in the field, both caught by the cores:

  * a key press in the explorer walked the tree with one stack frame per level
    (`flatten_nodes_mut`), so a symlinked directory that leads back into the
    workspace -- which a build tree leaves behind -- walked an endless chain of
    levels until the stack ran out (the SIGSEGV in the core dump),
  * a scan that threw escaped a libuv callback, and an exception out of a
    callback is std::terminate: the editor aborted with the terminal still in
    raw mode and mouse reporting still on, so the shell printed a mouse report
    as text on every move, which reads as a broken terminal.

This drives the real binary in a pty, inside a shell that reports the exit
status it saw. Each case expands the tree with `*` (the walk above), removes
or unreadables part of the workspace under it, and then asks the editor for
something only a live, finished walk can answer: `a` opening the explorer's
command palette. A walk still running through a symlink answers nothing, and
the editor that walked until the stack or the heap ran out is not running at
all.

Usage: test/file_tree_loss_probe.py [path-to-jot]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary).
"""
from __future__ import annotations

import os
import re
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

COLS, ROWS = 100, 30
EXIT = re.compile(r"EXIT=(\d+)")
BASE = "/tmp/jot_file_tree_loss_probe"


def force_rmtree(path: str) -> None:
    """Removes `path`, restoring the mode of anything a case made unreadable."""
    for dirpath, dirnames, _files in os.walk(path):
        for name in [dirpath] + [os.path.join(dirpath, d) for d in dirnames]:
            try:
                os.chmod(name, 0o700)
            except OSError:
                pass
    shutil.rmtree(path, ignore_errors=True)


def workspace(root: str) -> None:
    """A nested tree, a file, and a symlink to an ancestor directory."""
    force_rmtree(root)
    for i in range(3):
        deep = os.path.join(root, "d%d" % i, "a", "b", "c")
        os.makedirs(deep)
        with open(os.path.join(deep, "f.txt"), "w") as fh:
            fh.write("x\n")
    try:
        os.symlink(os.path.join(root, "d0"), os.path.join(root, "d0", "link"))
    except OSError:
        pass


def run_case(binary: str, label: str, disturb, after: list) -> list:
    """Expands the tree, runs `disturb` on the workspace, then keeps typing."""
    root = os.path.join(BASE, label, "workspace")
    base = os.path.join(BASE, label)
    workspace(root)
    cmd = "%s \"$@\"; echo EXIT=$?; sleep 3" % binary
    saw = {"palette": False}

    def palette_open(screen) -> bool:
        saw["palette"] = saw["palette"] or "mkfile" in screen.text()
        return saw["palette"]

    # `a` in the explorer opens the command palette on `mkfile `; nothing else
    # in this run puts that text on the screen.
    phases = [(0.4, b"*"), (0.4, b"j"), (0.4, disturb), (0.5, b"a"),
              (0.3, palette_open), (0.5, b"\x1b")] + after
    screen = run_in_pty("/bin/sh", ["-c", cmd, "sh", root], b"", settle=6.0, after=0.5,
                        cols=COLS, rows=ROWS, cfg=base + "_cfg", cwd=root,
                        phases=phases, until_timeout=8.0)
    text = screen.text()
    exited = EXIT.findall(text)
    failures = []
    if exited:
        failures.append("%s: the editor exited (status %s) instead of staying up"
                        % (label, exited[-1]))
    elif not saw["palette"]:
        failures.append("%s: the explorer never answered `a` after the workspace changed, "
                        "so the walk was still running or the pane stopped painting" % label)
    print("file tree loss probe: %-14s %s" % (
        label, "exited %s" % exited[-1] if exited else
        ("palette opened" if saw["palette"] else "no answer to `a`")))
    return failures


def remove_root(_screen) -> bool:
    force_rmtree(os.path.join(BASE, "removed", "workspace"))
    return True


def remove_subdir(_screen) -> bool:
    force_rmtree(os.path.join(BASE, "subdir", "workspace", "d1"))
    return True


def unreadable_subdir(_screen) -> bool:
    path = os.path.join(BASE, "unreadable", "workspace", "d2")
    os.chmod(path, 0)
    return True


def untouched(_screen) -> bool:
    return True


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    binary = os.path.abspath(binary)
    if not os.path.isfile(binary):
        print("file tree loss probe: SKIP - no binary at %s" % binary)
        return 2

    after = [(0.5, b"k"), (0.6, b"*")]
    failures = []
    failures += run_case(binary, "removed", remove_root, after)
    failures += run_case(binary, "subdir", remove_subdir, after)
    failures += run_case(binary, "unreadable", unreadable_subdir, after)
    failures += run_case(binary, "untouched", untouched, after)

    if failures:
        print("file tree loss probe: FAIL")
        for failure in failures:
            print("  - %s" % failure)
        return 1
    print("file tree loss probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
