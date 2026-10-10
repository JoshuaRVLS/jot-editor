#!/usr/bin/env python3
"""Probe: a save keeps the folds the user made, formatter or not.

The save path rewrites the buffer twice over: the built-in cleanup (reindent,
trailing-space trim) replaces the lines, and the on-save formatter replaces them
again from the bytes it just wrote. Both are content mutations, and the formatter
path used to answer with `fold_ranges.clear()` -- dropping the folded set and
reopening every fold in the file on a save that may not even have changed a byte
(`clang-format --write` is not a clang-format flag, so the default C++ runner
fails and the buffer is dropped all the same). The built-in cleanup path instead
marks the buffer edited, which re-derives the ranges with the folded set carried
over; the formatter paths now do the same.

What only a real session proves is that the fold is still on the repainted screen
after the save completes, with the on-save formatter enabled the way it ships.

Three scenes:
  * default settings (clang-format on save): fold, save, the block stays folded
    and the count stays on its header row;
  * a stub formatter first on PATH that really rewrites the file in place, with
    the body badly indented so the bytes change under the fold: still folded;
  * unfold, save: the save must not put the fold back.

Usage: test/fold_save_probe.py [path-to-jot] [--dump]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary).
"""
from __future__ import annotations

import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

CTRL_P = b"\x10"  # palette
ENTER = b"\r"
DOWN = b"\x1b[B"
SAVE = b"\x13"

CLEAN = """#include <string>

int helper_one() {
  int a = 1;
  int b = 2;
  return a + b;
}

int helper_two() {
  int c = 3;
  return c;
}
"""

# The same lines with the body indented wrong: a formatter rewrites the bytes
# under the fold without adding or removing a line, so the folded block keeps
# its span while its contents change.
UNFORMATTED = """#include <string>

int helper_one() {
        int a = 1;
  int b = 2;
            return a + b;
}

int helper_two() {
  int c = 3;
  return c;
}
"""


def command(name: str) -> bytes:
    return CTRL_P + name.encode() + ENTER


def folded(text: str) -> bool:
    """The block is collapsed: its body is gone and its header is on screen."""
    return "int a = 1;" not in text and "int helper_one() {" in text


def write_stub(bin_dir: str, real: str) -> None:
    """A clang-format that takes the editor's `--write` spelling.

    The editor runs `clang-format --write <file>` and the real tool rejects
    `--write` (`-i` is its in-place flag), so without this the rewrite scene
    would pass with the formatter never having run.
    """
    os.makedirs(bin_dir, exist_ok=True)
    stub = os.path.join(bin_dir, "clang-format")
    with open(stub, "w") as fh:
        fh.write("#!/bin/sh\n"
                 "file=''\n"
                 'for arg in "$@"; do\n'
                 '  case "$arg" in --write|-i|-style=*) ;; *) file="$arg" ;; esac\n'
                 "done\n"
                 'exec "%s" -i "$file"\n' % real)
    os.chmod(stub, 0o755)


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    dump = "--dump" in sys.argv
    if not os.path.exists(binary):
        print("fold save probe: SKIP - no binary at %s" % binary)
        return 2

    work = "/tmp/jot_fold_save_probe"
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    cfg = os.path.join(work, "cfg")
    os.makedirs(os.path.join(cfg, "configs"), exist_ok=True)

    failures = []

    def check(name: str, ok: bool, detail: str = "") -> None:
        print("fold save probe: %s - %s%s" % ("ok" if ok else "FAIL", name,
                                              (": " + detail) if (detail and not ok) else ""))
        if not ok:
            failures.append(name)

    def run(text: str, keys: bytes, env=None, name: str = "probe.cpp") -> str:
        path = os.path.join(work, name)
        with open(path, "w") as fh:
            fh.write(text)
        # The save writes and the formatter then rewrites from the worker thread,
        # so the screen is read only after the save has announced itself and had
        # time to answer.
        phases = [(1.2, SAVE), (0.0, wait_saved()), (0.0, b""), (0.0, b"")]
        return run_in_pty(binary, [path], keys, settle=3.0, after=1.5, cols=110, rows=30,
                          cfg=cfg, cwd=work, env=env, phases=phases).text()

    def saved_text(name: str = "probe.cpp") -> str:
        with open(os.path.join(work, name)) as fh:
            return fh.read()

    def wait_saved():
        def check_ready(screen) -> bool:
            return "Saved:" in screen.text()

        return check_ready

    # 1. The shipped settings: on-save formatting is on (clang_format_on_save
    #    defaults true for a .cpp), the fold survives the save.
    text = run(CLEAN, DOWN * 2 + command("fold"))
    if dump:
        print("--- scene 1 ---")
        print(text)
    check("the block folds before the save", folded(text))
    check("the count rides its header after the save", "\u2026 4 lines" in text,
          "no count on the folded header row")
    check("the saved bytes keep the body", "int a = 1;" in saved_text())

    # 2. A formatter that really rewrites the file, with the body's indentation
    #    wrong so the bytes under the fold change while its span does not.
    real = shutil.which("clang-format")
    if real is None:
        print("fold save probe: skip - no clang-format for the rewrite scene")
    else:
        bin_dir = os.path.join(work, "bin")
        write_stub(bin_dir, real)
        env = {"PATH": bin_dir + os.pathsep + os.environ.get("PATH", "")}
        text = run(UNFORMATTED, DOWN * 2 + command("fold"), env=env, name="rewritten.cpp")
        if dump:
            print("--- scene 2 ---")
            print(text)
        check("a formatter's rewrite leaves the block folded", folded(text))
        check("the formatter scene really rewrote the file",
              saved_text("rewritten.cpp") != UNFORMATTED,
              "the bytes on disk never changed, so the scene proved nothing")

    # 3. The other direction: a save must not put back a fold the user opened.
    text = run(CLEAN, DOWN * 2 + command("fold") + command("unfold"))
    if dump:
        print("--- scene 3 ---")
        print(text)
    check("the body comes back on unfold", "int a = 1;" in text)
    check("a save does not refold it", "int a = 1;" in text and "\u2026" not in text)

    if failures:
        print("fold save probe: FAIL (%d)" % len(failures))
        return 1
    print("fold save probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
