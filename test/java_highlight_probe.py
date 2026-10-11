#!/usr/bin/env python3
"""Probe: a java buffer is highlighted, not just parsed.

`java` shipped an empty bundled query. That does not fail loudly: an empty query
compiles, so the language reported itself loaded (and :tsstatus listed it as
active in the open buffer) while the parser's own installed query -- the one
:tsinstall copies into the per-user query directory -- was never reached. Every
cell of the buffer painted in the plain text colour.

This drives the real binary with a small class and reads the ink of four
constructs that must not share a colour: a keyword, a type, a comment and a
string literal. A buffer with the parser attached but no query paints all four
in one colour, which is the failure this pins; the language's own "is it active"
is covered by test/treesitter_query_probe.py.

Usage: test/java_highlight_probe.py [path-to-jot] [--dump]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary or no java parser).
"""
from __future__ import annotations

import os
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

SOURCE = """package com.example;

public class Hello {
  private static final int LIMIT = 42;

  public static void main(String[] args) {
    // greet the world
    String name = "world";
    System.out.println("hello " + name + LIMIT);
  }
}
"""


def parser_dirs() -> list[str]:
    home = os.environ.get("HOME", "")
    data = os.environ.get("XDG_DATA_HOME") or os.path.join(home, ".local", "share")
    cache = os.environ.get("XDG_CACHE_HOME") or os.path.join(home, ".cache")
    return [
        os.path.join(data, "jot", "treesitter", "parsers"),
        os.path.join(cache, "jot", "treesitter", "parsers"),
        "/usr/lib",
        "/usr/local/lib",
    ]


def parser_installed() -> bool:
    names = ["libtree-sitter-java.so", "libtree-sitter-java.dylib", "tree-sitter-java.dll"]
    return any(os.path.exists(os.path.join(d, name)) for d in parser_dirs() for name in names)


def ink(screen, anchor: str, inside: str = None):
    """The ink of `anchor` -- or of `inside` within that row -- where painted."""
    for y, row in enumerate(screen.text().splitlines()):
        at = row.find(anchor)
        if at < 0:
            continue
        if inside is None:
            return screen.fg[y][at]
        within = row.find(inside, at)
        if within >= 0:
            return screen.fg[y][within]
    return None


def main() -> int:
    positional = [arg for arg in sys.argv[1:] if not arg.startswith("--")]
    binary = positional[0] if positional else "build/apps/jot/jot"
    dump = "--dump" in sys.argv
    if not os.path.exists(binary):
        print(f"java highlight probe: SKIP - no binary at {binary}")
        return 2
    if not parser_installed():
        print("java highlight probe: SKIP - no java parser installed")
        return 2

    root = tempfile.mkdtemp(prefix="jot_java_highlight_")
    cfg = tempfile.mkdtemp(prefix="jot_java_highlight_cfg_")
    path = os.path.join(root, "Hello.java")
    with open(path, "w") as fh:
        fh.write(SOURCE)

    try:
        screen = run_in_pty(binary, [path], b"", settle=4.0, after=1.5,
                            cols=120, rows=24, cfg=cfg, cwd=root)
        if dump:
            print(screen.text())

        # Anchored on the code, not on the tab strip or the winbar: "Hello"
        # alone also names the tab, and only the declaration line proves the
        # class name itself is what got a type's ink.
        keyword = ink(screen, "public class")
        type_ink = ink(screen, "public class", "Hello")
        comment = ink(screen, "// greet the world")
        string = ink(screen, '"world"')
        number = ink(screen, "= 42")
        missing = [name for name, value in (("keyword", keyword), ("type", type_ink),
                                            ("comment", comment), ("string", string),
                                            ("number", number))
                   if value is None]
        if missing:
            print(f"java highlight probe: FAIL - nothing painted for {', '.join(missing)}")
            return 1
        if dump:
            print("inks:", keyword, type_ink, comment, string, number)
        # A parser with no query -- or a query that never compiled -- paints one
        # colour for all of them, which is exactly what this rejects.
        if keyword == comment or keyword == string:
            print("java highlight probe: FAIL - keyword, comment and string share one "
                  f"ink (keyword={keyword} comment={comment} string={string})")
            return 1
        if len({keyword, type_ink, comment, string, number}) < 3:
            print("java highlight probe: FAIL - the java buffer is painted in "
                  f"too few inks (keyword={keyword} type={type_ink} comment={comment} "
                  f"string={string} number={number})")
            return 1
    finally:
        shutil.rmtree(root, ignore_errors=True)
        shutil.rmtree(cfg, ignore_errors=True)

    print("java highlight probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
