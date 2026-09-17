-- Exercises the C/C++ "which branch is dead" logic of
-- runtime/lua/features/cpp_inactive.lua without an editor: the interesting part
-- of that feature is preprocessor evaluation, and it must be provable on its
-- own. Run with any Lua 5.3+:  lua test/cpp_inactive_probe.lua
--
-- The rule under test throughout: a branch is dimmed only when the condition is
-- fully understood and known false. Anything unclear counts as taken.

-- Stub the parts of the editor API the feature touches at load time.
local decorations = {}
jot = {
  config = { get = function(_, default) return default end },
  autocmd = function() end,
  decoration = {
    set = function(_, spec) decorations[#decorations + 1] = spec end,
    clear = function() decorations = {} end,
  },
  buffer = { get_line = function() return nil end },
}

local path = arg[1] or "runtime/lua/features/cpp_inactive.lua"
local feature = dofile(path)

local failures = 0
local function check(label, got, want)
  if got ~= want then
    failures = failures + 1
    print(string.format("FAIL %-46s got %s, want %s", label, tostring(got), tostring(want)))
  end
end

--------------------------------------------------------------- conditions
local ev = feature.evaluate_condition
local def = { WITH_MACRO = true, VALUE = true }

check("#if 0", ev("0", def), false)
check("#if 1", ev("1", def), true)
check("#ifdef with macro", ev("WITH_MACRO", def), true)
check("#ifdef without macro", ev("MISSING", def), false)
check("defined() with macro", ev("defined(WITH_MACRO)", def), true)
check("defined() without macro", ev("defined(MISSING)", def), false)
check("defined X (no parens)", ev("defined WITH_MACRO", def), true)
check("!defined missing", ev("!defined(MISSING)", def), true)
check("!defined present", ev("!defined(WITH_MACRO)", def), false)
check("a && 1", ev("WITH_MACRO && 1", def), true)
check("a && missing", ev("WITH_MACRO && MISSING", def), false)
check("missing || 1", ev("MISSING || 1", def), true)
check("parens", ev("(MISSING || WITH_MACRO) && 1", def), true)
check("not equal", ev("1 != 0", def), true)
check("equal", ev("0 == 1", def), false)
check("1UL suffix", ev("1UL", def), true)
check("empty condition", ev("", def), true)
-- Unknowable conditions must count as taken, never as dead code.
check("shift operator", ev("MISSING << 2", def), true)
check("function-like macro", ev("__has_include(<stdio.h>)", def), true)
check("comparison against macro", ev("VERSION > 3", def), true)

----------------------------------------------------------------- ranges
local ranges = feature.inactive_ranges
local function dimmed(lines, defined)
  local out = {}
  for _, r in ipairs((ranges(lines, defined or {}))) do
    for i = r[1], r[2] do out[#out + 1] = i end
  end
  return table.concat(out, ",")
end

check("#if 0 block", dimmed({
  "#if 0",
  "dead();",
  "#endif",
  "live();",
}), "1,2,3")

check("#if 1 block keeps everything", dimmed({
  "#if 1",
  "live();",
  "#endif",
}), "")

check("#ifdef taken, #else dead", dimmed({
  "#ifdef WITH_MACRO",
  "live();",
  "#else",
  "dead();",
  "#endif",
}, { WITH_MACRO = true }), "3,4,5")

check("#ifdef untaken, #else live", dimmed({
  "#ifdef MISSING",
  "dead();",
  "#else",
  "live();",
  "#endif",
}), "1,2,3")

check("#ifndef of a defined macro is dead", dimmed({
  "#ifndef WITH_MACRO",
  "dead();",
  "#endif",
}, { WITH_MACRO = true }), "1,2,3")

check("#define turns a later #ifdef on", dimmed({
  "#define LATER",
  "#ifdef LATER",
  "live();",
  "#endif",
}), "")

check("nested: inner dead only", dimmed({
  "#if 1",
  "#if 0",
  "dead();",
  "#endif",
  "live();",
  "#endif",
}), "2,3,4")

check("nested inside a dead branch stays dead", dimmed({
  "#if 0",
  "#if 1",
  "dead();",
  "#endif",
  "#endif",
}), "1,2,3,4,5")

check("#elif chain picks the first true", dimmed({
  "#if 0",
  "dead();",
  "#elif 1",
  "live();",
  "#elif 1",
  "dead();",
  "#else",
  "dead();",
  "#endif",
}), "1,2,3,5,6,7,8,9")  -- line 4 is the branch that IS taken

check("unterminated #if does not dim", dimmed({
  "#if 0",
  "dead();",
}), "1,2")

check("a comment is not a directive", dimmed({
  "// #if 0",
  "live();",
}), "")

if failures > 0 then
  print(string.format("cpp_inactive probe: FAIL (%d)", failures))
  os.exit(1)
end
print("cpp_inactive probe: PASS")
