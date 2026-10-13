#!/usr/bin/env python3
"""Ctrl+/ reads the comment markers from Lua, and a config can add a type.

The markers a file type gets came from a table compiled into the editor
(src/syntax/comment_style_data.h). That table is the fallback now, and
runtime/lua/features/comments/styles.lua is what the toggle asks first through
jot.policy. Two sessions, on a file type the editor has never heard of:

  0. the shipped module claims the name and holds the types (jot.policy.names()
     plus the table's own count, written from a probe command);
  1. nothing declared: the unknown type gets the C-family form, "//" -- the
     native default, which is also the control for the scene after it;
  2. the config declaring [".probe"] = { line = "#!" }: the same toggle stamps
     "#!" instead.

The declared marker exists nowhere in the native table, so seeing it can only
mean the declared catalog answered. The chord is spelled the way the kitty
keyboard protocol reports Ctrl+/ (CSI-u), which is what the editor decodes a
real terminal's `Ctrl+/` into.

Exit 0 pass, 1 fail, 2 skip.
"""
from __future__ import annotations

import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

BINARY = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
CFG = "/tmp/jot_comment_style_cfg"
WORK = "/tmp/jot_comment_style_work"

# Ctrl+P opens the command palette; Enter runs the typed `:command`.
PALETTE = b"\x10"
ENTER = b"\r"

# Ctrl+/ as CSI-u (kitty): 47 is '/', 5 is the ctrl modifier alone.
TOGGLE = b"\x1b[47;5u"

NATIVE_MARKER = "//"
DECLARED_MARKER = "#!"

DECLARED = """\
jot.policy.set("text.comments", {
  [".probe"] = { line = "%MARKER%" },
})
""".replace("%MARKER%", DECLARED_MARKER)

REPORT = "/tmp/jot_comment_style_names"

# Reports what owns the comment styles once the editor is up: the name it
# registered under, and how many file types the shipped table holds.
REPORT_COMMAND = """\
jot.command("styleprobe", function()
  local styles = require("jot_comments.styles")
  local count = 0
  for _ in pairs(styles) do
    count = count + 1
  end
  jot.file.write("%REPORT%", table.concat(jot.policy.names(), ",") .. "|" .. tostring(count))
end)
""".replace("%REPORT%", REPORT)


def write_config(source: str) -> None:
    shutil.rmtree(CFG, ignore_errors=True)
    os.makedirs(os.path.join(CFG, "plugins"), exist_ok=True)
    with open(os.path.join(CFG, "init.lua"), "w") as fh:
        fh.write(source)


def toggle_once():
    """Opens the workspace file and presses Ctrl+/ over its first line."""
    return run_in_pty(BINARY, [os.path.join(WORK, "notes.probe")], b"", settle=3.0, after=1.0,
                      cfg=CFG, cwd=WORK, phases=[(0.8, TOGGLE), (0.8, b"")])


def main() -> int:
    if not os.path.exists(BINARY):
        print(f"skip: {BINARY} is not built")
        return 2
    os.makedirs(WORK, exist_ok=True)
    with open(os.path.join(WORK, "notes.probe"), "w") as fh:
        fh.write("a line to comment\n")

    if os.path.exists(REPORT):
        os.remove(REPORT)

    failures: list[str] = []

    # 0. The shipped module claims the name and carries the file types.
    write_config(REPORT_COMMAND)
    run_in_pty(BINARY, [os.path.join(WORK, "notes.probe")], b"", settle=3.0, after=1.0, cfg=CFG, cwd=WORK,
               phases=[(0.8, PALETTE), (0.6, b"styleprobe"), (0.6, ENTER), (1.5, b"")])
    if not os.path.exists(REPORT):
        failures.append("the probe command never reported (no styles registered?)")
    else:
        names, _, count = open(REPORT).read().partition("|")
        if "text.comments" not in names.split(","):
            failures.append(f"the shipped comment styles are not registered "
                            f"(jot.policy.names() is {names!r})")
        types = int(count) if count.isdigit() else 0
        if types < 150:
            failures.append(f"the shipped table holds {types} file types, which is too "
                            "few to be the comment styles")

    # 1. Control: with nothing declared the unknown extension gets the native
    # C-family form.
    write_config("-- nothing declared: the native table answers\n")
    screen = toggle_once().text()
    if f"{NATIVE_MARKER}a line to comment" not in screen:
        failures.append(f"the unknown type was not commented with {NATIVE_MARKER!r}: "
                        "the native fallback did not answer")

    # 2. The declared catalog: a marker no native table holds for this type.
    write_config(DECLARED)
    screen = toggle_once().text()
    if f"{DECLARED_MARKER}a line to comment" not in screen:
        failures.append(f"the declared marker {DECLARED_MARKER!r} is not on the line: "
                        "the toggle did not ask the config's catalog")
    if f"{NATIVE_MARKER}a line to comment" in screen:
        failures.append("the native marker is on the line, so the declared catalog "
                        "did not replace it")

    if failures:
        for line in failures:
            print("FAIL:", line)
        print("--- screen ---")
        print(screen)
        return 1
    print("ok: the comment markers come from Lua, and a config can teach the "
          "editor a new file type")
    return 0


if __name__ == "__main__":
    sys.exit(main())
