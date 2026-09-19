#!/usr/bin/env python3
"""Probe: the C++ location lookups and the header/source flip, on a real session.

clangd answers the four navigation requests with different places: a declaration
lives in the header, a definition in the .cpp, a type definition behind an
`auto`, and an implementation behind a virtual call. A unit test can pin the
method mapping and the landing policy, but only a real clangd proves each
command is wired to the right request and that the URI it answers with is opened.

Each scene opens a file, puts the cursor on a symbol, runs the command through
the command palette (the editor is modeless, so commands are not typed at a
prompt), and then reads which file the editor is showing from the status line --
the tab strip and the explorer both list every file, so those cannot say which
one is current.

Usage: test/cpp_nav_probe.py [path-to-jot] [--dump]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary or no clangd).
"""
from __future__ import annotations

import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

HEADER = """#pragma once

struct Counter {
  int value();
  int stored = 0;
};

struct Base {
  virtual int run();
};

struct Derived : Base {
  int run() override;
};

auto make_counter() -> Counter;
"""

SOURCE = """#include "util.h"

int Counter::value() { return stored; }

int Base::run() { return 0; }
int Derived::run() { return 1; }

int use() {
  auto c = make_counter();
  return c.stored;
}
"""

MAIN = """#include "util.h"

int main() {
  Derived d;
  Base &b = d;
  return b.run();
}
"""

RIGHT = b"\x1b[C"
DOWN = b"\x1b[B"
PALETTE = b"\x1b[112;6u"  # Ctrl+Shift+P
ENTER = b"\r"


def write_workspace(root: str) -> None:
    shutil.rmtree(root, ignore_errors=True)
    os.makedirs(root, exist_ok=True)
    files = {"util.h": HEADER, "util.cpp": SOURCE, "main.cpp": MAIN}
    for name, text in files.items():
        with open(os.path.join(root, name), "w") as fh:
            fh.write(text)
    # So clangd parses as C++17 with no compile database to find. -xc++ is not
    # optional: without it clangd falls back to treating a .cpp as C, which it
    # then reports as a diagnostic and mistranslates.
    with open(os.path.join(root, "compile_flags.txt"), "w") as fh:
        fh.write("-xc++\n-std=c++17\n")


def path_for(root: str, name: str) -> str:
    return os.path.join(root, name)


def run_command(binary: str, root: str, path: str, cursor: bytes, command: str,
                cfg: str, settle: float = 9.0):
    """Opens `path`, moves the cursor with `cursor`, then runs `command`."""
    keys = cursor + PALETTE + command.encode() + ENTER
    return run_in_pty(binary, [path], keys, settle=settle, after=4.0,
                      cols=110, rows=32, cfg=cfg, cwd=root)


def alt(letter: str) -> bytes:
    """The kitty report for Alt+<letter> (Alt is bitmask 2, so modifier = 3)."""
    return b"\x1b[" + str(ord(letter)).encode() + b";3u"


def run_chord(binary: str, root: str, path: str, keys: bytes, cfg: str,
              settle: float = 9.0):
    """Opens `path` and presses a keymap chord, with no command line involved."""
    return run_in_pty(binary, [path], keys, settle=settle, after=4.0,
                      cols=110, rows=32, cfg=cfg, cwd=root)


def current_file(screen) -> str:
    """The file the editor is showing, read from the status line.

    The status line is the one row carrying the language-server indicator; the
    file it names is the current buffer. The tab strip and the explorer both
    show every open or listed file, so neither can answer this.
    """
    for line in screen.text().split("\n"):
        if " @ " not in line:
            continue
        for name in ("main.cpp", "util.cpp", "util.h"):
            if name in line:
                return name
    return ""


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    dump = "--dump" in sys.argv
    if not os.path.exists(binary):
        print(f"cpp nav probe: SKIP - no binary at {binary}")
        return 2
    if not shutil.which("clangd"):
        print("cpp nav probe: SKIP - no clangd on PATH")
        return 2

    root = "/tmp/jot_cpp_nav_probe"
    write_workspace(root)
    source = path_for(root, "util.cpp")
    header = path_for(root, "util.h")
    main_cpp = path_for(root, "main.cpp")

    # (label, file, cursor keys, command, expected landing file, one-line why)
    scenes = [
        ("declaration", source, DOWN * 2 + RIGHT * 13, "lspdecl", "util.h",
         "the definition of Counter::value asks where it is declared"),
        ("type definition", source, DOWN * 8 + RIGHT * 7, "lsptypedef", "util.h",
         "an `auto` asks what its type really is"),
        # clangd answers `implementation` with every implementation of the
        # virtual method -- the override declaration in the header and the
        # bodies in the source -- so the jump lands somewhere in the pair; what
        # the scene proves is that the virtual call left the calling file.
        ("implementation", main_cpp, DOWN * 5 + RIGHT * 11, "lspimpl",
         ("util.cpp", "util.h"),
         "a call through a base reference asks for the bodies that implement it"),
        ("switch header/source", source, b"", "switchheader", "util.h",
         "the source flips to its paired header"),
        ("switch header/source back", header, b"", "switchheader", "util.cpp",
         "and the header flips back to the source"),
    ]

    failures = []
    for index, (label, path, cursor, command, expected, why) in enumerate(scenes):
        cfg = f"/tmp/jot_cpp_nav_probe_cfg_{index}"
        screen = run_command(binary, root, path, cursor, command, cfg)
        if dump:
            print(screen.text())
            print("-" * 70)
        landed = current_file(screen)
        print(f"{label}: {command} landed in {landed or '(unknown)'} ({why})")
        expected_files = (expected,) if isinstance(expected, str) else expected
        if landed not in expected_files:
            failures.append(
                f"{label}: expected one of {', '.join(expected_files)}, "
                f"got {landed or 'nothing'}")

    # The keymap family, not the command: Alt+C h is the shipped binding for
    # :switchheader, so this proves the chords reach the same code the palette
    # does (the previous scene already proved the command on its own).
    cfg = f"/tmp/jot_cpp_nav_probe_cfg_key_{len(scenes)}"
    screen = run_chord(binary, root, source, alt("c") + b"h", cfg)
    if dump:
        print(screen.text())
        print("-" * 70)
    landed = current_file(screen)
    print(f"Alt+C h: landed in {landed or '(unknown)'} (the keymap chord, not the "
          "command line)")
    if landed != "util.h":
        failures.append(f"Alt+C h: expected util.h, got {landed or 'nothing'}")

    if failures:
        for failure in failures:
            print(f"cpp nav probe: FAIL - {failure}")
        return 1
    print("cpp nav probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
