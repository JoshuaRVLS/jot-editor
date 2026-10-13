#!/usr/bin/env python3
"""Probe: the scripting dialect a session runs on.

The embedded runtime is LuaJIT, which is Lua 5.1 plus the JIT, and the bundled
files are written to one dialect on top of it: Lua 5.1 plus utf8, which LuaJIT
does not ship, so the host installs the bundled module before any feature runs.
What only a real session proves is that the dialect is there at runtime: that
the JIT is engaged, that utf8 is the bundled implementation rather than a nil
global three features deep, and that it answers what Lua 5.4 answers down to
the byte, including where it refuses malformed bytes.

The probe's config dir carries an init.lua that walks the cases Lua 5.4 answers
(their expected answers are written down next to them) and paints the outcome on
the screen, so every claim below is read off a real terminal:

  1. jot.engine names the runtime and a jit library is on the session;
  2. utf8 is the bundled runtime/lua/compat/utf8.lua (debug.getinfo says which);
  3. all 32 dialect cases answer exactly what Lua 5.4 answers;
  4. the session boots without a Lua failure in its log.

Usage: test/lua_engine_probe.py [path-to-jot] [--dump]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary).
"""
from __future__ import annotations

import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

BINARY = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith("-") else "build/apps/jot/jot"
DUMP = "--dump" in sys.argv
CFG = "/tmp/jot_lua_engine_probe_cfg"
WORK = "/tmp/jot_lua_engine_probe_work"

# The dialect cases. Each one answers with a single string, so the answer stock
# Lua 5.4 gives can sit next to it: the two engines have to agree byte for byte,
# including where they refuse (guarded() folds the error text down to its kind,
# since the wording is the library's and not ours).
ENGINE_LUA = r"""
local function hex(s)
  local out = {}
  for i = 1, #s do out[#out + 1] = string.format("%02X", s:byte(i)) end
  return table.concat(out, " ")
end

local function show(...)
  local n = select("#", ...)
  local out = {}
  for i = 1, n do out[#out + 1] = tostring((select(i, ...))) end
  return table.concat(out, "|")
end

local function guarded(f)
  local ok, err = pcall(f)
  if ok then return "ok:" .. show(err) end
  local msg = tostring(err)
  if msg:find("invalid UTF-8 code", 1, true) then return "err:invalid" end
  if msg:find("out of bounds", 1, true) then return "err:bounds" end
  if msg:find("value out of range", 1, true) then return "err:range" end
  return "err:other"
end

local cases = {
  {"len-ascii", "ok:5", function() return utf8.len("hello") end},
  {"len-mb", "ok:11", function() return utf8.len("héllo wörld") end},
  {"len-cjk", "ok:7", function() return utf8.len("日本語テキスト") end},
  {"len-emoji", "ok:3", function() return utf8.len("a😀b") end},
  {"len-slice", "ok:4|3|2", function() return show(utf8.len("héllo", 2), utf8.len("héllo", 2, 5), utf8.len("héllo", -2)) end},
  {"len-invalid", "ok:nil|3", function() return show(utf8.len("ab\xFFcd")) end},
  {"len-invalid-at", "ok:nil|7", function() return show(utf8.len("héllo\xC3")) end},
  {"codes-walk", "ok:1:97 2:233 4:128512 8:26085", function()
    local out = {}
    for pos, cp in utf8.codes("aé😀日") do out[#out + 1] = pos .. ":" .. cp end
    return table.concat(out, " ")
  end},
  {"codes-count", "ok:7", function()
    local n = 0
    for _ in utf8.codes("日本語テキスト") do n = n + 1 end
    return n
  end},
  {"codes-invalid", "err:invalid", function() for _ in utf8.codes("a\xFFb") do end return "walked" end},
  {"codepoint-one", "ok:233", function() return utf8.codepoint("héllo", 2) end},
  {"codepoint-range", "err:bounds", function() return utf8.codepoint("aé😀", 1, 8) end},
  {"codepoint-neg", "ok:111", function() return utf8.codepoint("héllo", -1) end},
  {"codepoint-invalid", "err:invalid", function() return utf8.codepoint("h\xC3", 2) end},
  {"char-single", "ok:C3 A9", function() return hex(utf8.char(0xE9)) end},
  {"char-many", "ok:41 E2 82 AC F0 9F 98 80 E6 97 A5", function()
    return hex(utf8.char(0x41, 0x20AC, 0x1F600, 0x65E5))
  end},
  {"char-empty", "ok:[]", function() return "[" .. utf8.char() .. "]" end},
  {"char-four-byte", "ok:F4 90 80 80", function() return hex(utf8.char(0x110000)) end},
  {"char-wide", "ok:FD BF BF BF BF BF", function() return hex(utf8.char(0x7FFFFFFF)) end},
  {"char-over-range", "err:range", function() return utf8.char(0x80000000) end},
  {"char-fraction", "err:other", function() return utf8.char(1.5) end},
  {"offset-1", "ok:1", function() return utf8.offset("héllo", 1) end},
  {"offset-forward", "ok:4|7|10", function()
    return show(utf8.offset("日本語", 2), utf8.offset("日本語", 3), utf8.offset("日本語", 4))
  end},
  {"offset-after-end", "ok:6|5", function()
    return show(utf8.offset("héllo", -1), utf8.offset("héllo", -2))
  end},
  {"offset-past-end", "ok:nil", function() return show(utf8.offset("héllo", 7)) end},
  {"offset-in-char", "ok:2", function() return utf8.offset("héllo", 0, 3) end},
  {"offset-from", "ok:7", function() return utf8.offset("日本語", 2, 4) end},
  {"offset-oob", "err:bounds", function() return utf8.offset("héllo", 1, 99) end},
  -- The truncation the toast and the sidebar do: cut on a character boundary.
  {"cut-at-boundary", "ok:10|日本語|3", function()
    local s = "日本語テキスト"
    local b = utf8.offset(s, 4)
    return show(b, s:sub(1, b - 1), utf8.len(s:sub(1, b - 1)))
  end},
  {"cut-past-end", "ok:nil|5", function()
    local s = "héllo"
    return show(utf8.offset(s, 99), utf8.len(s))
  end},
  {"round-trip", "ok:same", function()
    local s = "aé😀日"
    local out = {}
    for _, cp in utf8.codes(s) do out[#out + 1] = utf8.char(cp) end
    return table.concat(out) == s and "same" or "different"
  end},
  -- The pattern is what a caller uses to find the next character, and it is the
  -- one constant of this library that is data rather than behaviour.
  {"charpattern", "ok:same", function()
    return utf8.charpattern == "[\0-\x7F\xC2-\xFD][\x80-\xBF]*" and "same" or "different"
  end},
}

local passed, total, first_bad = 0, 0, "none"
for _, case in ipairs(cases) do
  total = total + 1
  local got = guarded(case[3])
  if got == case[2] then
    passed = passed + 1
  elseif first_bad == "none" then
    first_bad = case[1] .. " got " .. got .. " want " .. case[2]
  end
end

local engine = tostring(jot.engine or "")
local jit_version = (type(jit) == "table" and jit.version) or "none"
local utf8_source = "?"
local info = debug.getinfo(utf8.len, "S")
if info then
  utf8_source = info.what or "?"
end

jot.screen.on_paint(function(w, h)
  if w < 60 or h < 12 then
    return
  end
  jot.screen.write(2, 3, "ENGINE=" .. engine)
  jot.screen.write(2, 4, "JIT=" .. jit_version)
  jot.screen.write(2, 5, "UTF8SRC=" .. utf8_source)
  jot.screen.write(2, 6, "CASES=" .. passed .. "/" .. total)
  jot.screen.write(2, 7, "BAD=" .. first_bad:sub(1, 110))
end)
"""

# Boot failures the editor logs to the pty: a Lua error from any of these means
# the runtime did not come up whole, whatever the screen says.
BOOT_ERRORS = (
    "Lua runtime failed",
    "embedded Lua runtime failed",
    "Lua callback error",
    "attempt to index",
    "attempt to call",
    "nil value",
)


def main() -> int:
    if not os.path.exists(BINARY):
        print(f"skip: {BINARY} is not built")
        return 2
    for path in (CFG, CFG + "/plugins", WORK):
        os.makedirs(path, exist_ok=True)
    with open(os.path.join(CFG, "init.lua"), "w", encoding="utf-8") as fh:
        fh.write(ENGINE_LUA)
    target = os.path.join(WORK, "probe.txt")
    with open(target, "w", encoding="utf-8") as fh:
        fh.write("dialect probe\n")

    log: list[str] = []
    text = bytearray()

    def on_output(fd, chunk):
        text.extend(chunk)

    screen = run_in_pty(BINARY, [target], b"", settle=3.0, after=2.0, cfg=CFG,
                        cwd=WORK, on_output=on_output)

    rows = ["".join(cells).rstrip() for cells in screen.cells]
    plain = re.sub(r"\x1b\[[0-9;?]*[a-zA-Z]", "", text.decode("utf-8", "replace"))
    for line in plain.splitlines():
        for bad in BOOT_ERRORS:
            if bad in line:
                log.append(line.strip()[:160])

    def value(row: int, key: str) -> str:
        """The text after `key=` on that row, up to whatever else shares it.

        The toast box is drawn over the right of these rows, so a value ends at
        a run of spaces or at the first piece of box chrome; the engine name
        itself keeps its single spaces."""
        line = rows[row]
        marker = key + "="
        at = line.find(marker)
        if at < 0:
            return ""
        rest = line[at + len(marker):]
        cut = len(rest)
        for match in re.finditer(r"  +|[│┌┐└┘─╭╮╯╰]", rest):
            cut = min(cut, match.start())
        return rest[:cut].strip()

    engine = value(3, "ENGINE")
    jit_version = value(4, "JIT")
    utf8_source = value(5, "UTF8SRC")
    cases = value(6, "CASES")
    first_bad = value(7, "BAD")

    failures: list[str] = []
    if log:
        failures.append("the boot log carried a Lua failure: " + " | ".join(log[:3]))
    if not engine:
        failures.append(f"row 3 is {rows[3]!r}, wanted it to report the engine")
    elif not engine.startswith("LuaJIT"):
        failures.append(f"the session reports {engine!r}, wanted a LuaJIT release")
    if jit_version == "none":
        failures.append(f"engine {engine!r} but no jit library on the session")
    if utf8_source != "Lua":
        failures.append(f"LuaJIT ships no utf8, so the bundled module should be in"
                        f" place, not a {utf8_source} function")
    if cases != "32/32":
        failures.append(f"the dialect cases answered {cases!r}, first mismatch: {first_bad or 'unknown'}")

    if failures:
        for line in failures:
            print("FAIL:", line)
        print("--- screen ---")
        print(screen.text())
        return 1
    print(f"ok: {engine} runs the bundled utf8, and 32 dialect cases match Lua 5.4")
    if DUMP:
        print("--- screen ---")
        print(screen.text())
    return 0


if __name__ == "__main__":
    sys.exit(main())
