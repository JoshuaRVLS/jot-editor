#!/usr/bin/env python3
"""jot.lua: writing a feature as Lua files and reloading them in place.

The point of the module surface is that a feature can be more than one file
without a rebuild. The probe's config dir holds its own `lua/` directory with a
module in it; a user init.lua requires it, reloads it, and paints what happened
on the screen, so every claim below is read off a real terminal:

  1. `require("probe_mod")` resolves out of the config dir, not the process CWD;
  2. the module body runs again after `jot.lua.reload`, and the reload hands
     back the module's new value;
  3. a module that cannot be found reports (false, error) instead of throwing;
  4. `jot.lua.dir()` / `jot.lua.path()` name the config dir and the search path.

Exit 0 pass, 1 fail, 2 skip.
"""
from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

BINARY = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
CFG = "/tmp/jot_module_probe_cfg"
WORK = "/tmp/jot_module_probe_work"

# Runs once per load: the counter is how the probe sees that reload re-ran the
# file instead of handing back package.loaded's cached table.
MODULE_LUA = """
_G.probe_runs = (_G.probe_runs or 0) + 1
return { tag = "probe-42", runs = _G.probe_runs }
"""

INIT_LUA = r"""
local ok, mod = pcall(require, "probe_mod")
local tag = ok and mod.tag or ("REQERR:" .. tostring(mod))
local first_runs = _G.probe_runs or 0

-- jot.lua.reload re-runs the file: the body increments the counter again, and
-- the module value comes back.
local rok, rmod = jot.lua.reload("probe_mod")
local second_runs = _G.probe_runs or 0
local same = rok and type(rmod) == "table" and rmod.tag == tag

-- A module that does not exist reports instead of throwing.
local miss_ok, miss_err = jot.lua.reload("definitely_missing_mod")

local dir = jot.lua.dir()
local path = jot.lua.path()
local dir_ok = dir and dir:find("jot_module_probe_cfg", 1, true) ~= nil
local path_ok = path and path:find("jot_module_probe_cfg", 1, true) ~= nil

jot.screen.on_paint(function(w, h)
  if w < 40 or h < 12 then
    return
  end
  jot.screen.write(2, 3, "TAG=" .. tostring(tag))
  jot.screen.write(2, 4, "RUNS=" .. tostring(first_runs) .. "," .. tostring(second_runs))
  jot.screen.write(2, 5, "RELOAD=" .. (same and "ok" or "bad:" .. tostring(rmod)))
  jot.screen.write(2, 6, "MISS=" .. (miss_ok and "threw-nothing" or "err"))
  jot.screen.write(2, 7, "DIR=" .. (dir_ok and "ok" or tostring(dir)))
  jot.screen.write(2, 8, "PATH=" .. (path_ok and "ok" or "missing"))
end)
"""


def main() -> int:
    if not os.path.exists(BINARY):
        print(f"skip: {BINARY} is not built")
        return 2
    for path in (CFG + "/lua", CFG + "/plugins", WORK):
        os.makedirs(path, exist_ok=True)
    with open(os.path.join(CFG, "lua", "probe_mod.lua"), "w") as fh:
        fh.write(MODULE_LUA)
    with open(os.path.join(CFG, "init.lua"), "w") as fh:
        fh.write(INIT_LUA)
    target = os.path.join(WORK, "probe.txt")
    with open(target, "w") as fh:
        fh.write("module probe\n")

    screen = run_in_pty(BINARY, [target], b"", settle=3.0, after=2.0, cfg=CFG, cwd=WORK)
    rows = ["".join(cells).rstrip() for cells in screen.cells]

    def row(index: int) -> str:
        return rows[index]

    wanted = {
        3: "TAG=probe-42",
        4: "RUNS=1,2",
        5: "RELOAD=ok",
        6: "MISS=err",
        7: "DIR=ok",
        8: "PATH=ok",
    }
    failures = [
        f"row {index} is {row(index)!r}, wanted it to hold {text!r}"
        for index, text in wanted.items()
        if text not in row(index)
    ]
    if failures:
        for line in failures:
            print("FAIL:", line)
        print("--- screen ---")
        print(screen.text())
        return 1
    print("ok: config-dir modules, reload re-runs the file, missing module reports")
    return 0


if __name__ == "__main__":
    sys.exit(main())
