#!/usr/bin/env python3
"""The settings menu's catalog comes from Lua, and a config can replace it.

The menu labelled its rows and picked how each one is edited from tables
compiled into src/surfaces/settings.cpp. Those tables are the fallback now, and
runtime/lua/features/settings/catalog.lua is the catalog the menu asks first
through jot.policy. Three sessions settle it:

  1. the shipped module claims the name and holds the entries (jot.policy.names()
     plus the table's own count, written from a probe command);
  2. with nothing declared by the config, the Tab size row reads its shipped
     label and steps by the shipped amount;
  3. with the config declaring its own entry for that one key, the same row
     reads the declared label and steps by the declared amount.

Case 3 is the check: a row label and a step size that exist nowhere in the
native tables can only come from the declared catalog. Case 2 is its control,
and it also pins the shipped file's own step, so a catalog that failed to load
would show up as a row that steps by something else.

Exit 0 pass, 1 fail, 2 skip.
"""
from __future__ import annotations

import os
import re
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

BINARY = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
CFG = "/tmp/jot_settings_catalog_cfg"
WORK = "/tmp/jot_settings_catalog_work"
REPORT = "/tmp/jot_settings_catalog_names"

# Ctrl+P opens the command palette; Enter runs the typed `:command`.
PALETTE = b"\x10"
ENTER = b"\r"
RIGHT = b"\x1b[C"

# The config's own entry: a label and a step that no native table has. The
# default tab_size is 2, so one step lands on 9 (the shipped step of 1 would
# land on 3).
DECLARED_LABEL = "LUA INDENT"
DECLARED_STEP = 7
DECLARED_LANDING = 9

REPORT_COMMAND = """\
-- Reports what owns the settings catalog once the editor is up: the name it
-- registered under, and how many entries the shipped table holds.
jot.command("catalogprobe", function()
  local catalog = require("jot_settings.catalog")
  local count = 0
  for _ in pairs(catalog) do
    count = count + 1
  end
  jot.file.write("%REPORT%", table.concat(jot.policy.names(), ",") .. "|" .. tostring(count))
end)
""".replace("%REPORT%", REPORT)

DECLARED = """\
jot.policy.set("settings.catalog", {
  tab_size = { label = "%LABEL%", type = "int", step = %STEP%, min = 1, max = 90 },
})
""".replace("%LABEL%", DECLARED_LABEL).replace("%STEP%", str(DECLARED_STEP))


def write_config(source: str) -> None:
    """A fresh config for one scene: editing a row saves it, so a scene that
    stepped the value would otherwise be the next scene's starting point."""
    shutil.rmtree(CFG, ignore_errors=True)
    os.makedirs(os.path.join(CFG, "plugins"), exist_ok=True)
    with open(os.path.join(CFG, "init.lua"), "w") as fh:
        fh.write(source)


def run(phases: list):
    return run_in_pty(BINARY, [os.path.join(WORK, "probe.txt")], b"", settle=3.0, after=2.0,
                      cfg=CFG, cwd=WORK, phases=phases)


def settings_row(screen, needle: str) -> str:
    """The painted row carrying `needle`, or "" when no row does."""
    for line in screen.text().split("\n"):
        if needle in line:
            return line.rstrip()
    return ""


def open_settings_row(query: bytes, expect: str):
    """`:settings`, filter to one row, then step it right once."""
    return run([(0.8, PALETTE), (0.6, b"settings"), (0.6, ENTER), (0.6, query),
                (1.2, lambda screen: expect in screen.text()), (0.5, RIGHT), (0.8, b"")])


def main() -> int:
    if not os.path.exists(BINARY):
        print(f"skip: {BINARY} is not built")
        return 2
    for path in (CFG, WORK):
        os.makedirs(path, exist_ok=True)
    with open(os.path.join(WORK, "probe.txt"), "w") as fh:
        fh.write("settings catalog probe\n")
    if os.path.exists(REPORT):
        os.remove(REPORT)

    failures: list[str] = []

    # 1. The shipped module claims the name and carries the entries.
    write_config(REPORT_COMMAND)
    run([(0.8, PALETTE), (0.6, b"catalogprobe"), (0.6, ENTER), (2.0, b"")])
    if not os.path.exists(REPORT):
        failures.append("the probe command never reported (no catalog registered?)")
    else:
        names, _, count = open(REPORT).read().partition("|")
        if "settings.catalog" not in names.split(","):
            failures.append(f"the shipped settings catalog is not registered "
                            f"(jot.policy.names() is {names!r})")
        entries = int(count) if count.isdigit() else 0
        if entries < 150:
            failures.append(f"the shipped catalog holds {entries} entries, "
                            "which is too few to be the settings menu's")

    # 2. Control: nothing declared, so the shipped catalog labels and steps the
    # row. The default is 2 and the shipped step is 1.
    write_config("-- nothing declared: the shipped catalog owns the menu\n")
    row = settings_row(open_settings_row(b"tabsize", "Tab size"), "Tab size")
    if not row:
        failures.append("the Tab size row is not on screen with the shipped catalog")
    elif re.findall(r"-?\d+", row) != ["3"]:
        failures.append(f"the Tab size row reads {row!r}, wanted it stepped by the "
                        "shipped amount from 2 to 3")

    # 3. The config's own entry: that label and that step are nowhere in the
    # native tables, so seeing them can only mean the declared catalog answered.
    write_config(DECLARED)
    screen = open_settings_row(b"tabsize", DECLARED_LABEL)
    row = settings_row(screen, DECLARED_LABEL)
    if not row:
        failures.append(f"the declared label {DECLARED_LABEL!r} is not on the row: "
                        "the menu did not ask the config's catalog")
    elif re.findall(r"-?\d+", row) != [str(DECLARED_LANDING)]:
        failures.append(f"the row reads {row!r}, wanted it stepped by the declared "
                        f"{DECLARED_STEP} from 2 to {DECLARED_LANDING}")
    if "Tab size" in screen.text():
        failures.append("the native label is still painted, so the declared entry "
                        "did not replace it")

    if failures:
        for line in failures:
            print("FAIL:", line)
        print("--- screen ---")
        print(screen.text())
        return 1
    print("ok: the settings menu's catalog is Lua, and a config's own entry "
          "replaces a row's label and step")
    return 0


if __name__ == "__main__":
    sys.exit(main())
