#!/usr/bin/env python3
"""Probe: the runtime's own library (runtime/lua/lib), as a session sees it.

The library exists so that a feature does not write its third copy of a JSON
decoder, a line diff or a path join. What only a real session can show is that
it is installed before anything else runs and that the modules answer what the
features and the plugins are told they answer:

  1. `require("jot.lib.json")` and `jot.lib.json` are the same table, in a
     session whose init.lua is the first file to run;
  2. every case below (json, base64, text, fs, inspect, iter, version, table)
     answers what docs/LUA_API.md says it answers;
  3. the library is in the API surface a plugin gates on, and the two feature
     copies of JSON are the one module rather than a second implementation.

The cases are written down with the answer next to them and run inside init.lua,
so the screen carries the count and the first mismatch: the probe reads the
count off a real terminal instead of trusting the Lua to report its own success.

Exit 0 pass, 1 fail, 2 skip.
"""
from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

BINARY = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
CFG = "/tmp/jot_lua_lib_probe_cfg"
WORK = "/tmp/jot_lua_lib_probe_work"

# Every case answers one string, so its stock answer can sit beside it. The
# library is reached through require() here, because that is the spelling a
# plugin uses; the jot.lib identity is asserted in its own row.
LIB_LUA = r"""
local json = require("jot.lib.json")
local base64 = require("jot.lib.base64")
local text = require("jot.lib.text")
local fs = require("jot.lib.fs")
local inspect = require("jot.lib.inspect")
local iter = require("jot.lib.iter")
local version = require("jot.lib.version")
local tbl = require("jot.lib.table")

local function show(...)
  local n = select("#", ...)
  local out = {}
  for i = 1, n do out[#out + 1] = tostring((select(i, ...))) end
  return table.concat(out, "|")
end

local cases = {
  -- json: the one encoder and decoder, with the null sentinel and the rule
  -- that a document is the whole input.
  {"json-encode", '{"a":[1,2],"b":false}', function() return json.encode({ b = false, a = { 1, 2 } }) end},
  {"json-decode", "1|true|x", function()
    local v = json.decode('{"a":[1,true,"x"]} ' .. "\n")
    return show(v.a[1], v.a[2], v.a[3])
  end},
  {"json-null", "nil|table", function()
    -- A null folds into nil unless the caller asks for the sentinel, which is
    -- how a streamed delta and an explicit null are told apart.
    return show(json.decode('{"x":null}').x, type(json.decode('{"x":null}', { keep_null = true }).x))
  end},
  {"json-surrogate", "f0 9f 98 80", function()
    -- The surrogate pair an answer about emoji arrives as: two escapes, one
    -- character, and it is the four bytes it should be.
    local s = json.decode('"\\ud83d\\ude00"')
    local out = {}
    for i = 1, #s do out[#out + 1] = string.format("%02x", s:byte(i)) end
    return table.concat(out, " ")
  end},
  {"json-trailing", "invalidx", function()
    local v, e = json.decode('{"a":1} 2')
    return (v == nil and "invalid" or "accepted") .. (e and "x" or "")
  end},
  {"json-bad", "invalid", function()
    local v, e = json.decode("{oops")
    return (v == nil and tostring(e):find("invalid", 1, true) and "invalid") or "no"
  end},

  -- base64: the standard alphabet, padded, so another tool decodes it.
  {"base64-round", "hello, world|aGVsbG8sIHdvcmxk", function()
    return show(base64.decode(base64.encode("hello, world")), base64.encode("hello, world"))
  end},
  {"base64-pad", "f|Zg==|Zm8=", function() return show(base64.decode("Zg=="), base64.encode("f"), base64.encode("fo")) end},
  {"base64-bad", "not", function()
    local v, e = base64.decode("!!!!")
    return v == nil and e and "not" or "accepted"
  end},

  -- text: the line forms, the hex spelling, and the diff both features walk.
  {"lines-trailing-newline", "2|b", function()
    local lines = text.split_lines("a\nb\n")
    return show(#lines, lines[2])
  end},
  -- An empty text is one empty line, not none: a buffer always has a caret in
  -- a line. The trailing-newline rule above only fires past one line.
  {"lines-empty", "1", function() return tostring(#text.split_lines("")) end},
  {"split", "3|a|b", function()
    local parts = text.split("a,,b", ",")
    return show(#parts, parts[1], parts[3])
  end},
  {"trim", "[x]", function() return "[" .. text.trim("  x\n ") .. "]" end},
  {"affix", "true|true|false", function()
    return show(text.starts_with("probe.lua", "probe"), text.ends_with("probe.lua", ".lua"),
                text.ends_with("probe.lua", ".txt"))
  end},
  {"hex", "00ff|ff00", function()
    -- Whitespace is allowed in hex so a wrapped listing can be pasted back.
    return show(text.hexencode(text.hexdecode("00Ff")), text.hexencode(text.hexdecode("ff 00")))
  end},
  {"hex-bad", "not", function()
    local v, e = text.hexdecode("abc")
    return v == nil and e and "not" or "accepted"
  end},
  {"diff-kinds", "same:1|del:2|add:B", function()
    local rows = text.diff({ "1", "2" }, { "1", "B" })
    local out = {}
    for _, row in ipairs(rows) do out[#out + 1] = row.kind .. ":" .. row.text end
    return table.concat(out, "|")
  end},
  {"diff-budget", "refused", function()
    local rows = text.diff({ "a" }, { "b" }, { max_cells = 0 })
    return rows == nil and "refused" or "walked"
  end},

  -- fs: the paths, over the file bindings. The workspace is the probe's own
  -- directory, so relative() and glob() have something of their own to answer.
  {"fs-join", "a/b/c", function() return fs.join("a/", "/b", "c") end},
  {"fs-join-abs", "/a/b", function() return fs.join("/a", "b") end},
  {"fs-normalize", "a/c|b", function() return show(fs.normalize("a/./b/../c"), fs.normalize("b/")) end},
  {"fs-parts", "d.lua|a/b|lua", function()
    return show(fs.name("a/b/d.lua"), fs.parent("a/b/d.lua"), fs.extension("a/b/d.lua"))
  end},
  {"fs-exists", "true|true|false", function()
    return show(fs.exists("target.txt"), fs.is_dir("sub"), fs.exists("nope.txt"))
  end},
  {"fs-relative", "target.txt", function() return fs.relative(fs.workspace() .. "/target.txt") end},
  {"fs-glob", "deep.txt,target.txt", function()
    -- The walk is over the workspace, name-matched and sorted, so both the
    -- root file and the one a directory down are found.
    local names = {}
    for _, path in ipairs(fs.glob("%.txt$")) do names[#names + 1] = fs.name(path) end
    return table.concat(names, ",")
  end},
  {"fs-read", "body", function() return fs.read("sub/deep.txt") end},

  -- inspect: a table printed as Lua reads it, with a cycle noted.
  {"inspect-simple", '{a = 1, s = "x"}', function() return inspect.inspect({ s = "x", a = 1 }) end},
  {"inspect-cycle", "true", function()
    local t = {}
    t.self = t
    return tostring(inspect.inspect(t):find("cycle", 1, true) ~= nil)
  end},

  -- iter: the loops the features write out by hand otherwise.
  {"iter-map", "2,4,6", function() return table.concat(iter.map({ 1, 2, 3 }, function(n) return n * 2 end), ",") end},
  {"iter-filter", "2", function() return table.concat(iter.filter({ 1, 2, 3 }, function(n) return n % 2 == 0 end), ",") end},
  {"iter-reduce", "6|true", function()
    return show(iter.reduce({ 1, 2, 3 }, function(a, b) return a + b end, 0), iter.find({ 1, 2 }, function(n) return n == 2 end) == 2)
  end},
  {"iter-sorted", "a|b|c", function()
    return table.concat(iter.sorted({ "c", "a", "b" }), "|")
  end},
  {"iter-slice", "2,3", function() return table.concat(iter.slice({ 1, 2, 3, 4 }, 2, 3), ",") end},

  -- version: the release numbers tools print.
  {"version-compare", "-1|1|0", function()
    return show(version.compare("1.2.3", "1.2.4"), version.compare("2.0.0", "1.9.9"),
                version.compare("1.2", "1.2.0"))
  end},
  {"version-prerelease", "-1", function() return tostring(version.compare("1.0.0-rc.1", "1.0.0")) end},
  {"version-range", "true|false|true", function()
    return show(version.range(">=1.2 <2.0.0", "1.5.1"), version.range(">=1.2 <2.0.0", "2.0.0"),
                version.range("1.2.0 - 2.0.0", "2.0.0"))
  end},
  {"version-range-caret", "true|false", function()
    return show(version.range("^1.2", "1.9.0"), version.range("^1.2", "2.0.0"))
  end},
  {"version-tostring", "1.2.3-rc.1", function() return version.tostring(version.parse("1.2.3-rc.1")) end},

  -- table: the shapes, and the copy that keeps a false.
  {"table-is-list", "true|false|false", function()
    return show(tbl.is_list({ 1, 2 }), tbl.is_list({}), tbl.is_list({ a = 1 }))
  end},
  {"table-deepcopy", "2|2|true", function()
    local original = { list = { 1, 2 } }
    local copy = tbl.deepcopy(original)
    copy.list[2] = 9
    return show(original.list[2], #original.list, tostring(original.list[2] ~= copy.list[2]))
  end},
  {"table-keys", "1|2|b", function()
    local keys = tbl.keys({ b = 1, [2] = 1, [1] = 1 })
    return show(keys[1], keys[2], keys[3])
  end},
  {"table-defaults", "false|5", function()
    local merged = tbl.defaults({ on = false }, { on = true, n = 5 })
    return show(merged.on, merged.n)
  end},
}

local passed, total, first_bad = 0, 0, "none"
for _, case in ipairs(cases) do
  total = total + 1
  local ok, got = pcall(case[3])
  if not ok then
    got = "error:" .. tostring(got)
  end
  if got == case[2] then
    passed = passed + 1
  elseif first_bad == "none" then
    first_bad = case[1] .. " got " .. tostring(got) .. " want " .. case[2]
  end
end

-- The two spellings of the library, and the feature module that used to be a
-- second copy of the decoder. The feature modules are loaded by the host after
-- this file runs, so the shared-table check happens on the first paint rather
-- than here (the AI assistant ships disabled in this build, so its module is
-- not on package.loaded at all; test/test_ai.cpp loads it directly and pins the
-- same identity).
local identity = tostring(jot.lib.json == json and jot.lib.fs == fs and jot.lib.text == text)
local shared = "?"
local function check_shared()
  local ok, mod = pcall(require, "jot_lc.json")
  shared = (ok and mod == json) and "y" or "n"
end

jot.screen.on_paint(function(w, h)
  if w < 60 or h < 12 then
    return
  end
  if shared == "?" then
    check_shared()
  end
  jot.screen.write(2, 3, "CASES=" .. passed .. "/" .. total)
  jot.screen.write(2, 4, "BAD=" .. tostring(first_bad):sub(1, 110))
  jot.screen.write(2, 5, "IDENTITY=" .. identity)
  jot.screen.write(2, 6, "SHARED=" .. shared)
  jot.screen.write(2, 7, "CAP=" .. (jot.capabilities().lib and "y" or "n")
                   .. (jot.api.has("lib.json.encode") and "y" or "n")
                   .. (jot.api.has("lib.text.diff") and "y" or "n"))
end)
"""

BOOT_ERRORS = (
    "Lua runtime failed",
    "embedded Lua runtime failed",
    "Lua callback error",
    "Bundled Lua module",
)


def main() -> int:
    if not os.path.exists(BINARY):
        print(f"skip: {BINARY} is not built")
        return 2
    for path in (CFG, CFG + "/lua", CFG + "/plugins", WORK, WORK + "/sub"):
        os.makedirs(path, exist_ok=True)
    with open(os.path.join(CFG, "init.lua"), "w", encoding="utf-8") as fh:
        fh.write(LIB_LUA)
    # The probes of fs read real files: one in the workspace root, one a
    # directory deeper, so fn.parent is walked rather than assumed.
    with open(os.path.join(WORK, "target.txt"), "w", encoding="utf-8") as fh:
        fh.write("lib probe\n")
    with open(os.path.join(WORK, "sub", "deep.txt"), "w", encoding="utf-8") as fh:
        fh.write("body")

    log: list[str] = []
    text_out = bytearray()

    def on_output(fd, chunk):
        text_out.extend(chunk)

    screen = run_in_pty(BINARY, [os.path.join(WORK, "target.txt")], b"", settle=3.0, after=2.0,
                        cfg=CFG, cwd=WORK, on_output=on_output)
    rows = ["".join(cells).rstrip() for cells in screen.cells]
    for line in bytes(text_out).decode("utf-8", "replace").splitlines():
        for bad in BOOT_ERRORS:
            if bad in line:
                log.append(line.strip()[:160])

    def value(row: int, key: str) -> str:
        line = rows[row]
        at = line.find(key + "=")
        if at < 0:
            return ""
        return line[at + len(key) + 1:].split("  ")[0].strip()

    cases = value(3, "CASES")
    first_bad = value(4, "BAD")

    failures: list[str] = []
    if log:
        failures.append("the boot log carried a Lua failure: " + " | ".join(log[:3]))
    if not cases:
        failures.append(f"row 3 is {rows[3]!r}, wanted the case count")
    elif cases != "42/42":
        failures.append(f"the library cases answered {cases}, first mismatch: {first_bad or 'unknown'}")
    if value(5, "IDENTITY") not in ("true", ""):
        failures.append(f"require and jot.lib answered different tables: {value(5, 'IDENTITY')!r}")
    if value(6, "SHARED") not in ("y", ""):
        failures.append(f"a feature JSON module is not the shared one: {value(6, 'SHARED')!r}")
    if value(7, "CAP") not in ("yyy", ""):
        failures.append(f"the library is not in the API surface: {value(7, 'CAP')!r}")

    if failures:
        print("lua lib probe: FAIL")
        for line in failures:
            print("  - %s" % line)
        print("--- screen ---")
        print(screen.text())
        return 1
    print("ok: 42 library cases, one table for require and jot.lib, features share it")
    return 0


if __name__ == "__main__":
    sys.exit(main())
