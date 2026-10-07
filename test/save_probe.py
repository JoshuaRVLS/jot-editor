#!/usr/bin/env python3
"""Probe: a real save leaves the bytes the buffer was showing.

The unit rules (src/features/save_hygiene.*) and the write helper
(src/base/file_util.*) are covered by test_save_hygiene.cpp; this drives the
binary's own save (Ctrl+S in the modeless editing mode) and then reads the file
off disk, because that is where a hygiene rule and the bytes on disk can still
disagree: the buffer is text the editor holds, the file is what the next `git
diff` sees.

Cases:
  * trailing whitespace is dropped, and a missing final newline is not (a file
    that did not end in one is written back with one, as before),
  * the setting off keeps every space,
  * markdown keeps its hard line breaks with the setting on.

Usage: test/save_probe.py [path-to-jot] [--dump]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary).
"""
from __future__ import annotations

import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

COLS, ROWS = 100, 24


def workspace(tmp: str, name: str, text: str, config: str = "") -> str:
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(os.path.join(tmp, "configs"))
    with open(os.path.join(tmp, "configs", "settings.conf"), "w") as fh:
        fh.write("# jot configuration file\n\n" + config)
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
        print("save probe: SKIP - no binary at %s" % binary)
        return 2

    trailing = "int a = 1;   \nint b = 2;\t"
    cases = [
        # label, file name, what is in the file, config, what the save leaves
        ("trailing whitespace", "a.cpp", trailing, "",
         "int a = 1;\nint b = 2;\n"),
        ("setting off", "a.cpp", trailing,
         "trim_trailing_whitespace_on_save=false\n",
         "int a = 1;   \nint b = 2;\t\n"),
        ("markdown hard break", "notes.md", "first  \nsecond", "",
         "first  \nsecond\n"),
    ]

    failures = []
    for index, (label, name, text, config, want) in enumerate(cases):
        got = save_and_read(binary, "/tmp/jot_save_probe_%d" % index, name, text,
                            config, dump)
        ok = got == want
        print("%-22s file after save = %r %s" % (label, got, "ok" if ok else "FAILED"))
        if not ok:
            failures.append("%s: file holds %r, wanted %r" % (label, got, want))

    if failures:
        print("save probe: FAIL")
        for failure in failures:
            print("  - " + failure)
        return 1
    print("save probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
