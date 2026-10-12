#!/usr/bin/env python3
"""A Lua command takes a built-in command's name, and new names still work.

The ex dispatcher used to consult the Lua registry last, so a plugin could add
a command but never replace one: `jot.command("settings", fn)` was dead code
next to the native `:settings`, and the only way to change a built-in was to
shadow its keybinding. Two runs through the command palette, exactly as a user
would, settle both halves:

  1. a Lua command registered under a name the core owns runs instead of the
     built-in (its body's message is on screen and the built-in's panel is not);
  2. a Lua command under a name the core does not have still runs.

On the pre-change build case 1 writes nothing and the settings panel opens
instead, which is what makes this a check rather than a restatement.

Exit 0 pass, 1 fail, 2 skip.
"""
from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

BINARY = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
CFG = "/tmp/jot_command_override_cfg"
WORK = "/tmp/jot_command_override_work"
MARKER_SETTINGS = os.path.join(CFG, "lua_ran_settings")
MARKER_PROBE = os.path.join(CFG, "lua_ran_probe")

INIT_LUA = r"""
-- A name the editor already dispatches natively.
jot.command("settings", function()
  jot.notify("lua took the name")
  jot.file.write("%MARKER_SETTINGS%", "lua")
end)
-- A name it does not have.
jot.command("luaprobe", function()
  jot.notify("lua probe ran")
  jot.file.write("%MARKER_PROBE%", "lua")
end)
""".replace("%MARKER_SETTINGS%", MARKER_SETTINGS).replace("%MARKER_PROBE%", MARKER_PROBE)


def run(keys: bytes):
    return run_in_pty(BINARY, [os.path.join(WORK, "probe.txt")], keys,
                      settle=3.0, after=2.5, cfg=CFG, cwd=WORK)


def main() -> int:
    if not os.path.exists(BINARY):
        print(f"skip: {BINARY} is not built")
        return 2
    for path in (CFG + "/plugins", WORK):
        os.makedirs(path, exist_ok=True)
    with open(os.path.join(CFG, "init.lua"), "w") as fh:
        fh.write(INIT_LUA)
    with open(os.path.join(WORK, "probe.txt"), "w") as fh:
        fh.write("command override probe\n")
    for marker in (MARKER_SETTINGS, MARKER_PROBE):
        if os.path.exists(marker):
            os.remove(marker)

    failures: list[str] = []

    # Case 1: the name belongs to a built-in (`:settings` opens the settings menu).
    screen = run(b"\x10settings\r")
    text = screen.text()
    if not os.path.exists(MARKER_SETTINGS):
        failures.append("the Lua command registered as \"settings\" never ran")
    if "lua took the name" not in text:
        failures.append("the Lua body's message is not on the status line")
    if "settings" in text.lower():
        failures.append(
            "the settings panel is on screen (or the palette stayed open), so the "
            "built-in body ran as well as, or instead of, the Lua one")

    # Case 2: a name the core does not own still reaches Lua.
    screen = run(b"\x10luaprobe\r")
    text = screen.text()
    if not os.path.exists(MARKER_PROBE):
        failures.append("a Lua command under a new name never ran")
    if "lua probe ran" not in text:
        failures.append("the new name's message is not on the status line")

    if failures:
        for line in failures:
            print("FAIL:", line)
        print("--- screen ---")
        print(screen.text())
        return 1
    print("ok: a Lua command replaces a built-in of the same name, and new names still run")
    return 0


if __name__ == "__main__":
    sys.exit(main())
