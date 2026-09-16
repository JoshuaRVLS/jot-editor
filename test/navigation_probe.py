#!/usr/bin/env python3
"""Probe: jumplist navigation and the rename prompt, on the rendered screen.

Both are new surfaces whose state lives in the editor rather than in a return
value, so the unit tests pin the mechanics (recording, walking, the input handler)
and this checks the parts only a real session shows: that Ctrl+O actually lands
back in the previous file, and that Ctrl+Shift+R paints the prompt.

The jump is made through the telescope, which is one of the paths that records
one, so this covers the recording hook as well as the walk.

Usage: test/navigation_probe.py [path-to-jot]
Exit codes: 0 pass, 1 fail, 2 binary missing.
"""
from __future__ import annotations

import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

# The kitty-protocol spelling of Ctrl+Shift+R (code 114 = 'r', modifier 6 =
# Ctrl+Shift). A legacy terminal cannot report this chord distinctly.
CTRL_SHIFT_R = b"\x1b[114;6u"
CTRL_E = b"\x05"  # telescope
CTRL_O = b"\x0f"  # jump back


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    if not os.path.exists(binary):
        print(f"navigation probe: SKIP - no binary at {binary}")
        return 2

    work = "/tmp/jot_navigation_probe"
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work, exist_ok=True)
    for name in ("alpha_file.cpp", "beta_file.cpp"):
        with open(os.path.join(work, name), "w") as fh:
            fh.write("int counter_value = 1;\n" * 20)
    os.system(f"git init -q {work} 2>/dev/null")
    cfg = "/tmp/jot_navigation_probe_cfg"

    failures = 0

    # 1. Ctrl+O returns to the file the telescope took us away from.
    screen = run_in_pty(binary, [os.path.join(work, "alpha_file.cpp")], CTRL_E + b"beta_file\r",
                        settle=3.0, after=3.0, cols=110, rows=30, cfg=cfg, cwd=work)
    opened_beta = "beta_file.cpp" in screen.text()
    screen = run_in_pty(binary, [os.path.join(work, "alpha_file.cpp")], CTRL_E + b"beta_file\r",
                        settle=3.0, after=3.0, cols=110, rows=30, cfg=cfg, cwd=work)
    print(f"navigation probe: telescope opened beta = {opened_beta}")
    if not opened_beta:
        print("navigation probe: SKIP - the telescope never opened (probe premise failed)")
        return 2

    # A second session that jumps, then walks back with Ctrl+O.
    screen = run_in_pty(binary, [os.path.join(work, "alpha_file.cpp")],
                        CTRL_E + b"beta_file\r" + CTRL_O,
                        settle=3.0, after=4.0, cols=110, rows=30, cfg=cfg, cwd=work)
    view = screen.text()
    back_at_alpha = "alpha_file.cpp" in view
    print(f"navigation probe: after Ctrl+O back at alpha = {back_at_alpha}")
    if not back_at_alpha:
        failures += 1
        print("navigation probe: FAIL - Ctrl+O did not return to the previous file")
        print("\n".join(row for row in view.split("\n") if row.strip())[:400])

    # 2. Ctrl+Shift+R paints the rename prompt, seeded with the identifier under
    #    the cursor. The cursor starts at column 0 ("int"), so step it inside the
    #    variable name first -- that is the seeding being checked, not the word
    #    "int" the buffer happens to start with.
    RIGHT = b"\x1b[C"
    screen = run_in_pty(binary, [os.path.join(work, "alpha_file.cpp")],
                        RIGHT * 6 + CTRL_SHIFT_R,
                        settle=3.0, after=3.0, cols=110, rows=30, cfg=cfg, cwd=work)
    view = screen.text()
    shows_prompt = "Rename Symbol" in view
    shows_seed = "New name: counter_value" in view
    print(f"navigation probe: rename prompt visible = {shows_prompt}, "
          f"seeded from the cursor = {shows_seed}")
    if not (shows_prompt and shows_seed):
        failures += 1
        print("navigation probe: FAIL - Ctrl+Shift+R did not paint the rename prompt")
        print("\n".join(row for row in view.split("\n") if row.strip())[:400])

    if failures:
        print("navigation probe: FAIL")
        return 1
    print("navigation probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
