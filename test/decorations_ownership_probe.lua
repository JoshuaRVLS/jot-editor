-- Two features write decorations on the same buffer: the inline-diagnostic
-- squiggles and the preprocessor dimming. Neither may call
-- jot.decoration.clear(), because that deletes every span on the buffer --
-- whoever runs second erases the first. In a real session that is silent:
-- clangd publishes right after a file opens, so the dims appeared and were
-- wiped a moment later, which reads exactly like "the feature does nothing".
--
-- Run: lua test/decorations_ownership_probe.lua

local script = arg and arg[0] or "test/decorations_ownership_probe.lua"
local root = script:match("^(.-)test/") or "./"

local handlers = {}
local spans = {}
local next_id = 0
local set_calls = 0
local clear_calls = 0

local LINES = {
  "#include <cstdio>",
  "#ifdef _WIN32",
  "int dead = 1;",
  "#else",
  "int live = 1;",
  "#endif",
}
local DIAGNOSTICS = {
  { line = 3, col = 1, end_col = 10, severity = 1, message = "boom" },
}

local function stub()
  local by_buffer = { [1] = LINES }
  return {
    config = {
      get = function(_, fallback) return fallback end,
    },
    buffer = {
      get_line = function(index, buffer)
        local lines = by_buffer[buffer]
        return lines and lines[index] or nil
      end,
    },
    file = {
      read = function() error("the buffer fallback should not be needed") end,
    },
    decoration = {
      set = function(buffer, deco)
        next_id = next_id + 1
        spans[next_id] = { buffer = buffer, deco = deco }
        set_calls = set_calls + 1
        by_buffer[buffer] = by_buffer[buffer] or LINES
        return next_id
      end,
      delete = function(buffer, id)
        if spans[id] and spans[id].buffer == buffer then
          spans[id] = nil
        end
      end,
      clear = function()
        clear_calls = clear_calls + 1
        spans = {}
      end,
    },
    diagnostics = {
      get = function(_, _) return DIAGNOSTICS end,
    },
    autocmd = function(event, fn)
      handlers[event] = handlers[event] or {}
      handlers[event][#handlers[event] + 1] = fn
    end,
  }
end

local function fire(event, info)
  for _, fn in ipairs(handlers[event] or {}) do
    fn(info)
  end
end

-- Only the ids matching pred: a feature re-running legitimately deletes and
-- recreates its own spans, so a snapshot of everything would flag that too.
local function ids_where(pred)
  local out = {}
  for id, span in pairs(spans) do
    if pred(span.deco) then
      out[id] = true
    end
  end
  return out
end

local function count_where(pred)
  local n = 0
  for _, span in pairs(spans) do
    if pred(span.deco) then
      n = n + 1
    end
  end
  return n
end

jot = stub()

local cpp = assert(loadfile(root .. "runtime/lua/features/cpp_inactive.lua"))()
assert(type(cpp.inactive_ranges) == "function", "cpp_inactive did not load")
assert(loadfile(root .. "runtime/lua/features/decorations.lua"))()

fire("CursorMoved", { buffer = 1, filepath = "/tmp/probe.cpp" })
local is_dim = function(d) return d.hl == "comment" end
local dims = ids_where(is_dim)
assert(count_where(is_dim) > 0, "the dimming feature painted nothing")

local is_squiggle = function(d) return d.underline ~= nil end
fire("DiagnosticChanged", { buffer = 1 })
assert(count_where(is_squiggle) > 0, "the diagnostics feature painted nothing")

local lost = 0
for id in pairs(dims) do
  if not spans[id] then
    lost = lost + 1
  end
end
assert(lost == 0,
  lost .. " dimmed line(s) were deleted when the diagnostics were applied -- " ..
  "a buffer-wide clear() is still in one of the features")

local squiggles = ids_where(is_squiggle)
fire("CursorMoved", { buffer = 1, filepath = "/tmp/probe.cpp" })
local lost_again = 0
for id in pairs(squiggles) do
  if not spans[id] then
    lost_again = lost_again + 1
  end
end
assert(lost_again == 0,
  lost_again .. " diagnostic decoration(s) were deleted when the dimming ran")

assert(clear_calls == 0,
  "jot.decoration.clear() was called " .. clear_calls .. " time(s); " ..
  "each feature must delete only the ids it created")

print(("ok - %d dim(s) and %d diagnostic decoration(s) coexist, no clear()"):format(
  count_where(is_dim), count_where(is_squiggle)))
