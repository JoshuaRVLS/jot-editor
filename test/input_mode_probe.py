#!/usr/bin/env python3
"""jot.input.push_mode: a stack of keyboard owners, innermost first.

An overlay that takes the keyboard used to be a single hook: every surface had
to hand-consume the keys it did not want, two of them could not nest, and there
was no way to tell which one was on top. This probe pushes two modes that both
claim the same key, pops the inner one from inside its own handler, and types a
key nobody claims:

  z   both modes want it          -> the inner one handles it (top down)
  p   the inner mode pops itself  -> the stack depth drops to 1
  z   the outer mode is now top   -> the outer one handles it
  w   nobody claims it            -> it reaches the buffer

The mode that handled each z is painted on screen from a module-level variable,
so the check reads a real frame rather than trusting a counter.

Exit 0 pass, 1 fail, 2 skip.
"""
from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

BINARY = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
CFG = "/tmp/jot_input_mode_cfg"
WORK = "/tmp/jot_input_mode_work"

INIT_LUA = r"""
local handled_by = "none"
local inner = nil
local depth = 0

local function report()
  local modes = jot.input.modes()
  depth = #modes
end

-- Outer mode: pushed first, so it sits below the inner one.
jot.input.push_mode({
  name = "outer",
  on_key = function(event)
    if event.char == "z" then
      handled_by = "outer"
      return true
    end
    return false
  end,
})

-- Inner mode: pushed second, so it is asked first. It also owns 'p', which pops
-- itself, so the same key afterwards belongs to the outer mode again.
inner = jot.input.push_mode({
  name = "inner",
  on_key = function(event)
    if event.char == "z" then
      handled_by = "inner"
      return true
    end
    if event.char == "p" then
      jot.input.pop_mode()
      return true
    end
    return false
  end,
})

report()

jot.screen.on_paint(function(w, h)
  if w < 40 or h < 12 then
    return
  end
  report()
  jot.screen.write(2, 3, "Z=" .. handled_by .. " DEPTH=" .. depth)
end)
"""


def main() -> int:
    if not os.path.exists(BINARY):
        print(f"skip: {BINARY} is not built")
        return 2
    for path in (CFG + "/plugins", WORK):
        os.makedirs(path, exist_ok=True)
    with open(os.path.join(CFG, "init.lua"), "w") as fh:
        fh.write(INIT_LUA)
    target = os.path.join(WORK, "probe.txt")
    with open(target, "w") as fh:
        fh.write("mode probe\n")

    # z (top mode takes it), p (it pops itself), z (the outer mode is top now),
    # w (nobody claims it).
    screen = run_in_pty(BINARY, [target], b"zpzw", settle=3.0, after=2.5, cfg=CFG, cwd=WORK)

    rows = ["".join(cells).rstrip() for cells in screen.cells]
    marked = next((r for r in rows if "Z=" in r), "")
    failures: list[str] = []
    if "Z=outer" not in marked:
        failures.append(
            f"the painted row is {marked!r}, wanted Z=outer: after the inner mode popped "
            "itself, the key must reach the mode below it")
    if "DEPTH=1" not in marked:
        failures.append(f"the painted row is {marked!r}, wanted DEPTH=1 after the pop")

    buffer_row = next((r for r in rows if "mode probe" in r), "")
    if "wmode probe" not in buffer_row:
        failures.append(
            f"the buffer's first line is {buffer_row!r}, wanted wmode probe: the key no "
            "mode claimed must still be typed, and the claimed ones must not")

    if failures:
        for line in failures:
            print("FAIL:", line)
        print("--- screen ---")
        print(screen.text())
        return 1
    print("ok: the top mode wins, a pop hands the key back to the mode below, "
          "and an unclaimed key still reaches the buffer")
    return 0


if __name__ == "__main__":
    sys.exit(main())
