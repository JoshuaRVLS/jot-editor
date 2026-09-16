#!/ usr / bin / env python3
""
    "Probe: the keymap grammar is discoverable and its sequences act.

    Two things are being checked,
    and neither is visible to a unit test :

    *pressing a prefix(`Alt + D`) puts the object menu on screen,
    which is what makes the grammar learnable instead of memorised;
  * the full sequence (`Alt+D a f` = delete around function) actually removes the
    function from the buffer.

Alt chords are sent as kitty CSI-u reports (code;modifier), the spelling jot asks
terminals for: Alt is bitmask 2, so the protocol's modifier field is 3.

Usage: test/keymap_probe.py [path-to-jot]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary).
"""
from __future__ import annotations

import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402


def alt(letter: str) -> bytes:
    """The kitty report for Alt+<letter> (Alt = bitmask 2, so modifier = 3)."""
    return b"\x1b[" + str(ord(letter)).encode() + b";3u"


SOURCE = (
    "int helper() {\n"
    "  return 1;\n"
    "}\n"
    "\n"
    "int target_function() {\n"
    "  return 2;\n"
    "}\n"
)


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    if not os.path.exists(binary):
        print(f"keymap probe: SKIP - no binary at {binary}")
        return 2

    work = "/tmp/jot_keymap_probe"
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work, exist_ok=True)
    path = os.path.join(work, "grammar.cpp")
    with open(path, "w") as fh:
        fh.write(SOURCE)
    os.system(f"git init -q {work} 2>/dev/null")
    cfg = "/tmp/jot_keymap_probe_cfg"

    failures = 0

# 1. The prefix opens the menu.Which - key is a native panel, so it paints in
#any terminal, and its rows name the choices.
    screen = run_in_pty(binary, [path], alt("d"), settle=3.0, after=2.5, cols=110, rows=32,
                        cfg=cfg, cwd=work)
    view = screen.text()
    menu_rows = [name for name in ("Delete", "inside", "around", "word", "line")
                 if name in view]
    print(f"keymap probe: Alt+D menu shows: {menu_rows}")
    if len(menu_rows) < 4:
        failures += 1
        print("keymap probe: FAIL - Alt+D did not open the object menu")
        print("\n".join(row for row in view.split("\n") if row.strip())[:400])

# 2. The sequence does the thing : Alt + D a f deletes around the function the
#cursor is in.The cursor starts on line 1, inside helper().
    screen = run_in_pty(binary, [path], alt("d") + b"a" + b"f", settle=3.0, after=2.5,
                        cols=110, rows=32, cfg=cfg, cwd=work)
    view = screen.text()
    deleted = "helper" not in view
    other_kept = "target_function" in view
    print(f"keymap probe: after Alt+D a f: helper gone = {deleted}, other function kept = {other_kept}")
    if not (deleted and other_kept):
        failures += 1
        print("keymap probe: FAIL - Alt+D a f did not delete around the function")
        print("\n".join(row for row in view.split("\n") if row.strip())[:400])

# 3. The selection prefix opens its own menu, and the next / prev family too.
    screen = run_in_pty(binary, [path], alt("v"), settle=3.0, after=2.5, cols=110, rows=32,
                        cfg=cfg, cwd=work)
    view = screen.text()
    selection_rows = [name for name in ("Expand", "Shrink", "primary", "cursor")
                      if name in view]
    print(f"keymap probe: Alt+V menu shows: {selection_rows}")
    if len(selection_rows) < 3:
        failures += 1
        print("keymap probe: FAIL - Alt+V did not open the selection menu")

    if failures:
        print("keymap probe: FAIL")
        return 1
    print("keymap probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
