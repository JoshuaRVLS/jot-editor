#!/usr/bin/env python3
"""Probe: a save rebases the indentation on the settings step.

test_save_hygiene.cpp pins the pure rules (src/features/save_hygiene.*); this
drives the binary's own save (Ctrl+S) and reads the file off disk, because the
built-in reindent is a claim about the bytes a save leaves behind, not about a
painter. Each scene opens a prepared file with the external formatters and the
trailing-space trim off, saves, and compares the whole file.

Cases:
  * a C++ body written one level out is pulled back in,
  * the step in settings is what the nesting is measured in (2 -> 4),
  * a Python file's blocks are re-based the same way (4 -> 2),
  * the setting off leaves every leading space where it was.

Usage: test/reindent_probe.py [path-to-jot] [--dump]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary).
"""
from __future__ import annotations

import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

COLS, ROWS = 100, 24
# The save has to be the reindent and nothing else.
BASE_CONFIG = ("trim_trailing_whitespace_on_save=false\n"
               "clang_format_on_save=false\n"
               "prettier_on_save=false\n")


def workspace(tmp: str, name: str, text: str, config: str) -> str:
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(os.path.join(tmp, "configs"))
    with open(os.path.join(tmp, "configs", "settings.conf"), "w") as fh:
        fh.write("# jot configuration file\n\n" + BASE_CONFIG + config)
    path = os.path.join(tmp, name)
    with open(path, "w") as fh:
        fh.write(text)
    return path


def save_and_read(binary: str, tmp: str, name: str, text: str, config: str,
                  dump: bool) -> str:
    path = workspace(tmp, name, text, config)
    screen = run_in_pty(binary, [path], b"", settle=2.0, after=0.6, cfg=tmp,
                        cwd=tmp, cols=COLS, rows=ROWS,
                        phases=[(0.8, b"\x13")])  # Ctrl+S
    if dump:
        print(screen.text())
        print("-" * 70)
    with open(path) as fh:
        return fh.read()


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    dump = "--dump" in sys.argv
    if not os.path.exists(binary):
        print("reindent probe: SKIP - no binary at %s" % binary)
        return 2

    cases = [
        # label, file name, what is in the file, extra config, what the save
        # leaves
        ("body pulled in", "a.cpp", "void f()\n{\nfoo();\n}\n", "",
         "void f()\n{\n  foo();\n}\n"),
        ("step widened", "a.cpp", "void f()\n{\n  foo();\n}\n", "tab_size=4\n",
         "void f()\n{\n    foo();\n}\n"),
        ("python narrowed", "a.py", "def f():\n    if x:\n        return 1\n", "",
         "def f():\n  if x:\n    return 1\n"),
        ("setting off", "a.cpp", "void f()\n{\nfoo();\n}\n",
         "reindent_on_save=false\n", "void f()\n{\nfoo();\n}\n"),
    ]

    failures = []
    for index, (label, name, text, config, want) in enumerate(cases):
        got = save_and_read(binary, "/tmp/jot_reindent_probe_%d" % index, name,
                            text, config, dump)
        ok = got == want
        print("%-18s file after save = %r %s" % (label, got, "ok" if ok else "FAILED"))
        if not ok:
            failures.append("%s: file holds %r, wanted %r" % (label, got, want))

    if failures:
        print("reindent probe: FAIL")
        for failure in failures:
            print("  - " + failure)
        return 1
    print("reindent probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
