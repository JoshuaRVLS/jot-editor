-- Dims the branches of C/C++ preprocessor conditionals that are not taken, so
-- a wall of #ifdef scaffolding stops reading as live code.
--
-- Disable with: jot.config.set("cpp_dim_inactive", false)
-- Which symbols count as defined: jot.config.set("cpp_defined_macros", "FOO,BAR")
--   (in addition to the #defines found earlier in the same file).
--
-- The directives are found by scanning lines rather than by parsing: a
-- preprocessor directive is line-oriented, so the scan is exact for real code
-- and costs no parser handle. The one thing it cannot tell is a "#if" that sits
-- inside a block comment.
--
-- A condition that cannot be evaluated confidently counts as TAKEN, so a
-- branch is dimmed only when the editor is sure it is dead code: dimming live
-- code is a much worse failure than leaving dead code alone.

local jot = jot

-- This feature deletes only the decorations it created: jot.decoration.clear()
-- wipes every decoration on the buffer, which would take the diagnostics'
-- squiggles (or, the other way round, wipe these dims the moment the language
-- server publishes).
local owned = {}

-- Buffers whose dims are already painted. Reading a buffer costs one crossing
-- into the editor per line, and CursorMoved fires on every key without the
-- text changing, so the answer is only recomputed when the text can have
-- moved: BufChange, or the first time a buffer is seen.
local seen = {}

local function release(buffer)
  local ids = owned[buffer]
  if not ids then
    return
  end
  for _, id in ipairs(ids) do
    jot.decoration.delete(buffer, id)
  end
  owned[buffer] = {}
end

local function keep(buffer, id)
  if type(id) == "number" and id ~= 0 then
    local ids = owned[buffer]
    if not ids then
      ids = {}
      owned[buffer] = ids
    end
    ids[#ids + 1] = id
  end
end

local ENABLED_KEY = "cpp_dim_inactive"
local MACROS_KEY = "cpp_defined_macros"

local C_EXTENSIONS = {
  c = true,
  h = true,
  cc = true,
  cpp = true,
  cxx = true,
  "c++" and true or false,
  hh = true,
  hpp = true,
  hxx = true,
  inl = true,
  ipp = true,
}

-- Low priority: the squiggle for a real diagnostic (priority 10), selections
-- and search hits all have to stay legible on top of a dimmed branch.
local DIM_PRIORITY = 1

local retry = function() end

local function enabled()
  return jot.config.get(ENABLED_KEY, "true") ~= "false"
end

local function configured_macros()
  local out = {}
  for name in tostring(jot.config.get(MACROS_KEY, "") or ""):gmatch("[^,%s]+") do
    out[name] = true
  end
  return out
end

local function is_c_family(path)
  local ext = tostring(path or ""):match("%.([%w+]+)$")
  return ext ~= nil and C_EXTENSIONS[ext:lower()] == true
end

-- One directive line -> kind and rest, e.g. "#  ifdef FOO // why" -> "ifdef",
-- "FOO". Returns nil for lines that are not directives.
local function directive_of(line)
  local body = line:match("^%s*#%s*(.-)%s*$")
  if not body then
    return nil
  end
  -- Trailing line comments are not part of the condition.
  body = body:gsub("//.*$", ""):gsub("/%*.*$", "")
  local kind, rest = body:match("^(%a+)(.*)$")
  if not kind then
    return nil
  end
  return kind:lower(), (rest or ""):gsub("^%s+", "")
end

-- Evaluates a #if expression against the known-defined set. Returns true for
-- anything it cannot decide, and only ever returns false for a condition it
-- fully understands and knows to be false.
local function evaluate_condition(expr, defined)
  expr = tostring(expr or "")
  if expr == "" then
    return true
  end

  -- defined(X) / defined X first: they are the common case and the only form
  -- worth being clever about.
  local ok = true
  expr = expr:gsub("defined%s*%(%s*([%w_]+)%s*%)", function(name)
    return defined[name] and "1" or "0"
  end)
  expr = expr:gsub("defined%s+([%w_]+)", function(name)
    return defined[name] and "1" or "0"
  end)

  -- Remaining identifiers: a bare symbol in #if means "is it defined", which
  -- is what the preprocessor does with it (it expands to 0 or 1).
  expr = expr:gsub("[%a_][%w_]*", function(name)
    if name == "true" then
      return "1"
    end
    if name == "false" then
      return "0"
    end
    return defined[name] and "1" or "0"
  end)

  -- Numeric suffixes (1UL) and anything else exotic: if it does not survive
  -- this cleanup, the condition is not one we claim to understand.
  expr = expr:gsub("(%d)[uUlL]+", "%1")
  if expr:find("[%a_]", 1) then
    return true -- an identifier we could not resolve
  end
  if expr:find("[<>%^~%?%%:]") then
    return true -- bitwise, shifts, ternaries: not evaluated
  end

  -- C truthiness is not Lua's: 0 is false to the preprocessor and true to Lua,
  -- so every operand is turned into a Lua boolean before the operators are
  -- translated. Without this, "0 || 1" evaluates to false and "!0" to false --
  -- both backwards.
  expr = expr:gsub("%d+", "(%0 ~= 0)")
  -- "!=" before "!" so a not-equal does not become "not =".
  expr = expr:gsub("!=", "~="):gsub("!", " not ")
  expr = expr:gsub("&&", " and "):gsub("||", " or ")

  local chunk = load("return (" .. expr .. ")")
  if not chunk then
    return true
  end
  local ran, value = pcall(chunk)
  if not ran then
    return true
  end
  -- Coerce by type, never with "and 1 or 0": 0 is truthy in Lua, so that would
  -- turn every condition into true.
  if type(value) == "boolean" then
    return value
  end
  if type(value) == "number" then
    return value ~= 0
  end
  return true
end

-- Walks the file's directives and returns the inclusive line ranges whose code
-- the preprocessor would skip.
local function inactive_ranges(lines, defined)
  local ranges = {}
  local stack = {}
  local defines = {}

  local function active()
    for _, frame in ipairs(stack) do
      if not frame.active then
        return false
      end
    end
    return true
  end

  for index, line in ipairs(lines) do
    local kind, rest = directive_of(line)
    -- Judged before the directive runs as well as after: an #endif closes the
    -- branch it sits in, and leaving it undimmed fragments the block.
    local dead_before = #stack > 0 and not active()
    if kind == "define" then
      if active() then
        local name = rest:match("^([%w_]+)")
        if name then
          defines[name] = true
          defined[name] = true
        end
      end
    elseif kind == "undef" then
      local name = rest:match("^([%w_]+)")
      if name then
        defined[name] = nil
      end
    elseif kind == "ifdef" or kind == "ifndef" or kind == "if" then
      local condition
      if kind == "ifdef" then
        condition = defined[rest:match("^([%w_]+)") or ""] == true
      elseif kind == "ifndef" then
        condition = defined[rest:match("^([%w_]+)") or ""] ~= true
      else
        condition = evaluate_condition(rest, defined)
      end
      stack[#stack + 1] = { active = condition, taken = condition }
    elseif kind == "elif" then
      local frame = stack[#stack]
      if frame then
        local condition = (not frame.taken) and evaluate_condition(rest, defined)
        frame.active = condition
        frame.taken = frame.taken or condition
      end
    elseif kind == "else" then
      local frame = stack[#stack]
      if frame then
        frame.active = not frame.taken
        frame.taken = true
      end
    elseif kind == "endif" then
      stack[#stack] = nil
    end

    -- A line is dimmed when it sits inside a skipped branch. The directives
    -- themselves are dimmed with it, so the whole block reads as one region.
    if dead_before or (#stack > 0 and not active()) then
      local last = ranges[#ranges]
      if last and last[2] == index - 1 then
        last[2] = index
      else
        ranges[#ranges + 1] = { index, index }
      end
    end
  end
  return ranges, defines
end

-- jot.buffer.get_line is 1-based and takes the buffer. It is empty while a
-- file is still being opened (BufOpen fires before the text is in), so the
-- caller falls back to reading the file.
local function buffer_lines(buffer)
  local lines = {}
  for index = 1, 200000 do
    local text = jot.buffer.get_line(index, buffer)
    if text == nil then
      break
    end
    lines[#lines + 1] = tostring(text)
  end
  return lines
end

local function file_lines(path)
  local ok, text = pcall(jot.file.read, path)
  if not ok or type(text) ~= "string" or text == "" then
    return {}
  end
  local lines = {}
  for line in (text .. "\n"):gmatch("([^\n]*)\n") do
    lines[#lines + 1] = line
  end
  return lines
end

local apply = function(info, force)
  if not enabled() then
    return
  end
  -- Buffer ids and rows are both 1-based here: the event payload hands out
  -- buffer + 1 (resolve_buffer_arg subtracts the 1 itself), and decoration_set
  -- parses "1-based row/col". Passing 0-based values silently targets the
  -- wrong buffer or an out-of-range row and every call is rejected.
  local buffer = info and info.buffer
  if not buffer or buffer < 0 then
    retry()
    return
  end
  if not force and seen[buffer] then
    return
  end
  local path = (info and (info.filepath or info.path)) or ""
  if path == "" then
    retry()
    return
  end
  if not is_c_family(path) then
    return
  end
  -- The buffer first (it has unsaved edits), the file when it is still empty
  -- because BufOpen ran before the text was loaded.
  local lines = buffer_lines(buffer)
  if #lines == 0 then
    lines = file_lines(path)
  end
  if #lines == 0 then
    retry()
    return
  end
  retries = 0

  local defined = configured_macros()
  local ranges = inactive_ranges(lines, defined)

  seen[buffer] = true
  release(buffer)
  local painted = 0
  for _, range in ipairs(ranges) do
    for index = range[1], range[2] do
      local text = lines[index] or ""
      if #text > 0 then
        keep(buffer, jot.decoration.set(buffer, {
          row = index,
          col = 1,
          width = #text,
          dim = true,
          priority = DIM_PRIORITY,
        }))
      end
    end
  end
end

-- The event that announces a file can arrive before its text is readable, and
-- then the run paints nothing: BufOpen carries no buffer id at all (-1), and
-- the first CursorMoved can land while the buffer is still empty. Those runs
-- used to be the end of it, so the fade only appeared once some unrelated event
-- forced a repaint -- which made it look like the feature took a minute to
-- start. Retry briefly instead of waiting for the next keystroke.
local retries = 0

local function current_buffer()
  local ok, value = pcall(jot.buffer.current)
  if not ok or value == nil then
    return nil
  end
  if type(value) == "number" then
    return value
  end
  if type(value) == "table" then
    return value.id or value.buffer or value.index
  end
  return nil
end

-- jot.buffer.current_file is the shipped spelling (features/keymaps.lua uses
-- it); the others are tried in case it moves.
local function current_path()
  local candidates = {}
  if jot.buffer and type(jot.buffer.current_file) == "function" then
    candidates[#candidates + 1] = jot.buffer.current_file
  end
  if type(jot.current_file) == "function" then
    candidates[#candidates + 1] = jot.current_file
  end
  if jot.editor and type(jot.editor.current_file) == "function" then
    candidates[#candidates + 1] = jot.editor.current_file
  end
  for _, fn in ipairs(candidates) do
    local ok, value = pcall(fn)
    if ok and type(value) == "string" and value ~= "" then
      return value
    end
  end
  return ""
end

retry = function()
  if retries >= 8 or type(jot.set_timeout) ~= "function" then
    return
  end
  retries = retries + 1
  local function run()
    local id = current_buffer()
    if id then
      apply({ buffer = id, path = current_path() }, false)
    end
  end
  if not pcall(jot.set_timeout, run, 120) then
    pcall(jot.set_timeout, 120, run)
  end
end

-- The text only moves on an edit, so a cursor move is worth a full rescan
-- once per buffer and never again.
jot.autocmd("CursorMoved", function(info)
  apply(info, false)
end)
jot.autocmd("BufChange", function(info)
  apply(info, true)
end)
-- A file arriving is the moment the fade should show up, without touching
-- anything, so this one is worth the retry loop.
jot.autocmd("BufOpen", function()
  apply({ buffer = current_buffer(), path = current_path() }, true)
  retry()
end)

-- Exposed so the preprocessor logic can be exercised directly, without an
-- editor: test/cpp_inactive_probe.lua drives these.
return {
  evaluate_condition = evaluate_condition,
  inactive_ranges = inactive_ranges,
  directive_of = directive_of,
}
