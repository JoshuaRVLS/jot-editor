#!/usr/bin/env python3
"""Probe: a smart paste lands the block where Enter would open it.

The paste re-bases a block onto the caret's own context with the editor's indent
rules (src/edit/clipboard.cpp, smart_indent_paste_text), so what lands is a
question about real bytes, not about a painter: each scene pastes into a
prepared file, saves with Ctrl+S (the on-save formatters and the trailing-space
trim are off, so the file is the buffer), and compares the whole file.

  * after `    if (x) {` an unindented block belongs one level inside the body,
    on its own lines, with the brace line left exactly as it was;
  * after `    x = ` the first pasted line joins the line (no indent of its own,
    which is what used to land here as `x =     foo();`) and the rest take the
    statement's indent;
  * after a Python `if x:` the block steps in, because that is what Enter does
    there, rather than staying at the header's own level.

Usage: test/paste_indent_probe.py [path-to-jot]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary).
"""
from __future__ import annotations

import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

SETTLE_S = 8.0
RIGHT = b"\x1b[C"
SAVE = b"\x13"
PASTE_START = b"\x1b[200~"
PASTE_END = b"\x1b[201~"


def paste(block: str) -> bytes:
    return PASTE_START + block.encode() + PASTE_END


# name, file body, keys before the paste, pasted block, file after the save. A
# step is tab_size, which is 2 by default; the block's landing line keeps it (the
# scene turns the trailing-space trim off so the file is the buffer).
SCENES = [
    ("after an opener", "    if (x) {\n}\n", RIGHT * 12, "foo();\nbar();\n",
     "    if (x) {\n      foo();\n      bar();\n      \n}\n"),
    ("mid-line", "    x = \n}\n", RIGHT * 8, "foo();\nbar();\n",
     "    x = foo();\n    bar();\n    \n}\n"),
    ("python header", "if x:\n    pass\n", RIGHT * 5, "one()\ntwo()\n",
     "if x:\n  one()\n  two()\n  \n    pass\n"),
]


def run_scene(binary: str, root: str, scene) -> str:
    name, body, before, block, _ = scene
    work = os.path.join(root, name.replace(" ", "_"))
    cfg = os.path.join(work, "cfg")
    os.makedirs(os.path.join(cfg, "configs"))
    # The save must be the paste and nothing else: no trailing-space trim (the
    # block's landing line carries the indent) and no external formatter.
    with open(os.path.join(cfg, "configs", "settings.conf"), "w") as fh:
        fh.write("trim_trailing_whitespace_on_save=false\n")
        fh.write("clang_format_on_save=false\nprettier_on_save=false\n")

    path = os.path.join(work, "probe.cpp" if not name.startswith("python") else "probe.py")
    with open(path, "w") as fh:
        fh.write(body)

    run_in_pty(binary, [path], b"", settle=SETTLE_S, after=1.0, cols=100, rows=24,
               cfg=cfg, cwd=work, phases=[(0.4, before + paste(block) + SAVE)])
    with open(path, "r") as fh:
        return fh.read()


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    if not os.path.exists(binary):
        print(f"paste indent probe: SKIP - no binary at {binary}")
        return 2

    root = "/tmp/jot_paste_indent_probe"
    shutil.rmtree(root, ignore_errors=True)
    binary = os.path.abspath(binary)

    failures = 0
    for scene in SCENES:
        name, _, _, _, expected = scene
        got = run_scene(binary, root, scene)
        ok = got == expected
        if not ok:
            failures += 1
        print(f"paste indent probe: {name} {'ok' if ok else 'FAIL'}")
        if not ok:
            print(f"  want {expected!r}")
            print(f"  got  {got!r}")

    print(f"paste indent probe: {'PASS' if failures == 0 else 'FAIL'}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
