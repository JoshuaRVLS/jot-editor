-- LSP / VSCode snippet-text parser.
--
-- Turns `for (${1:int} ${2:i} = ${3:0}; ...) { $0 }` into the node tree the
-- rest of the engine renders. Everything the snippet formats define is
-- covered: escapes, `$1` / `${1}` / `${1:default}` / `${1|a,b|}` forms,
-- variables (`$TM_FILENAME`, `${VAR:default}`) and transforms
-- (`${1/(.*)/\1/}` with `\u`/`\l`/`\U`/`\L`/`\E` and the `/upcase`,
-- `/downcase`, `/capitalize`, `/camelcase`, `/pascalcase` format field).
-- It is pure: no editor state is touched here.
local nodes = require("jot_snip.nodes")
local env = require("jot_snip.env")

local M = {}

-- Rewrites the PCRE-flavoured escapes VSCode snippets use into Lua patterns.
-- Constructs that already are valid Lua patterns pass through untouched.
function M.to_lua_pattern(pattern)
  local out = {}
  local i = 1
  local n = #pattern
  while i <= n do
    local c = pattern:sub(i, i)
    if c == "\\" and i < n then
      local nxt = pattern:sub(i + 1, i + 1)
      local map = {
        d = "%d", D = "%D", w = "%w", W = "%W", s = "%s", S = "%S",
        a = "%a", l = "%l", u = "%u", n = "\n", t = "\t",
      }
      if map[nxt] then
        out[#out + 1] = map[nxt]
      else
        out[#out + 1] = "%" .. nxt
      end
      i = i + 2
    else
      out[#out + 1] = c
      i = i + 1
    end
  end
  return table.concat(out)
end

local CASE_FUNCS = {
  upcase = string.upper,
  downcase = string.lower,
}

local function capitalize(word)
  return (word:gsub("^%l", string.upper))
end

local function words(text)
  local out = {}
  for word in text:gmatch("[%w]+") do
    out[#out + 1] = word
  end
  return out
end

CASE_FUNCS.capitalize = function(text)
  return (capitalize(text))
end
CASE_FUNCS.camelcase = function(text)
  local parts = words(text)
  local out = {}
  for index, word in ipairs(parts) do
    out[index] = index == 1 and word:lower() or capitalize(word:lower())
  end
  return table.concat(out)
end
CASE_FUNCS.pascalcase = function(text)
  local out = {}
  for _, word in ipairs(words(text)) do
    out[#out + 1] = capitalize(word:lower())
  end
  return table.concat(out)
end
CASE_FUNCS.snakecase = function(text)
  local out = {}
  for _, word in ipairs(words(text)) do
    out[#out + 1] = word:lower()
  end
  return table.concat(out, "_")
end
CASE_FUNCS.kebabcase = function(text)
  local out = {}
  for _, word in ipairs(words(text)) do
    out[#out + 1] = word:lower()
  end
  return table.concat(out, "-")
end

M.case_funcs = CASE_FUNCS

-- Expands `\1`/`$1` references (and `\u`/`\l`/`\U`/`\L`/`\E` case markers)
-- against the matches of one transform evaluation.
local function expand_replacement(repl, matches, whole)
  local out = {}
  local upper_next, lower_next = false, false
  local forced_upper, forced_lower = false, false
  local i = 1
  local n = #repl
  while i <= n do
    local c = repl:sub(i, i)
    if c == "\\" or c == "$" then
      local nxt = repl:sub(i + 1, i + 1)
      if nxt:match("%d") then
        local j = i + 1
        while j <= n and repl:sub(j, j):match("%d") do
          j = j + 1
        end
        local index = tonumber(repl:sub(i + 1, j - 1))
        local value
        if index == 0 then
          value = whole
        else
          value = matches[index] or ""
        end
        if upper_next or forced_upper then
          value = value:upper()
        elseif lower_next or forced_lower then
          value = value:lower()
        end
        out[#out + 1] = value
        upper_next, lower_next = false, false
        i = j
      elseif c == "\\" and nxt == "u" then
        upper_next = true
        i = i + 2
      elseif c == "\\" and nxt == "l" then
        lower_next = true
        i = i + 2
      elseif c == "\\" and nxt == "U" then
        forced_upper, forced_lower = true, false
        i = i + 2
      elseif c == "\\" and nxt == "L" then
        forced_lower, forced_upper = false, true
        i = i + 2
      elseif c == "\\" and nxt == "E" then
        forced_upper, forced_lower = false, false
        i = i + 2
      elseif c == "\\" then
        out[#out + 1] = nxt
        i = i + 2
      else
        out[#out + 1] = c
        i = i + 1
      end
    else
      out[#out + 1] = c
      i = i + 1
    end
  end
  return table.concat(out)
end

M.expand_replacement = expand_replacement

-- Builds the function node implementing `${N/regex/repl/flags}` (and the
-- `${N/regex/format/}` shape, where the third field names a case function).
local function transform_node(pattern, repl, flags, format, argnode)
  local case_fn = format and CASE_FUNCS[format] or nil
  local lua_pattern = M.to_lua_pattern(pattern or "")
  local global = flags and flags:find("g", 1, true) ~= nil

  return nodes.f(function(args)
    local text = tostring(args and args[1] or "")
    if case_fn then
      return case_fn(text)
    end
    local first = true
    return (text:gsub(lua_pattern, function(...)
      if not global and not first then
        return nil -- leave later matches alone
      end
      first = false
      local matches = { ... }
      return expand_replacement(repl or "", matches, matches[0] or "")
    end))
  end, { argnode })
end

M.transform_node = transform_node

-- Splits the body of a `${N|a,b,c|}` choice, honouring `\,` escapes.
local function split_choice(body)
  local items = {}
  local current = {}
  local i = 1
  while i <= #body do
    local c = body:sub(i, i)
    if c == "\\" and i < #body then
      current[#current + 1] = body:sub(i + 1, i + 1)
      i = i + 2
    elseif c == "," then
      items[#items + 1] = table.concat(current)
      current = {}
      i = i + 1
    else
      current[#current + 1] = c
      i = i + 1
    end
  end
  items[#items + 1] = table.concat(current)
  return items
end

-- Finds the `}` closing the `${` at `start`, skipping escaped and nested ones.
local function matching_brace(text, start)
  local depth = 0
  local i = start
  while i <= #text do
    local c = text:sub(i, i)
    if c == "\\" then
      i = i + 2
    elseif c == "$" and text:sub(i + 1, i + 1) == "{" then
      depth = depth + 1
      i = i + 2
    elseif c == "}" then
      depth = depth - 1
      if depth == 0 then
        return i
      end
      i = i + 1
    else
      i = i + 1
    end
  end
  return nil
end

local parse_into

-- Splits the three `/`-separated parts of a transform body, honouring `\/`.
local function split_transform(body)
  local parts = {}
  local current = {}
  local i = 1
  while i <= #body do
    local c = body:sub(i, i)
    if c == "\\" and body:sub(i + 1, i + 1) == "/" then
      current[#current + 1] = "\\/"
      i = i + 2
    elseif c == "/" then
      parts[#parts + 1] = table.concat(current)
      current = {}
      i = i + 1
    else
      current[#current + 1] = c
      i = i + 1
    end
  end
  parts[#parts + 1] = table.concat(current)
  return parts
end

-- The insert node standing for tabstop `pos` (created on first reference, so
-- `$1` and `${1:...}` and `${1/(.*)/x/}` all share one node).
local function tabstop(state, pos)
  local node = state.tabstops[pos]
  if not node then
    node = nodes.i(pos)
    state.tabstops[pos] = node
  end
  return node
end

-- `$NAME` / `${NAME:default}` / `${NAME/.../.../}`.
local function variable_node(state, name, default_text)
  local node = {
    type = "variable",
    name = name,
    default = default_text and parse_into(default_text, state) or nil,
  }
  return nodes.copy(node)
end

-- Parses a `${N...}` / `${VAR...}` body.
local function parse_braced(state, inner)
  local number = inner:match("^(%d+)")
  if number then
    local rest = inner:sub(#number + 1)
    local pos = tonumber(number)
    if rest == "" then
      return tabstop(state, pos)
    end
    local marker = rest:sub(1, 1)
    if marker == ":" then
      local node = tabstop(state, pos)
      node.default = parse_into(rest:sub(2), state)
      return node
    elseif marker == "|" then
      local body = rest:match("^|(.-)|")
      local items = split_choice(body or rest:sub(2))
      -- The choice shares the tabstop index, so a later `$1` mirrors it.
      local choice = nodes.c(pos, items)
      state.choices[pos] = choice
      return choice
    elseif marker == "/" then
      local parts = split_transform(rest:sub(2))
      local pattern, repl, flags = parts[1], parts[2], parts[3]
      local format
      if repl and repl:match("^/[%a]+$") and CASE_FUNCS[repl:sub(2)] then
        format, repl = repl:sub(2), nil
      end
      return transform_node(pattern, repl, flags, format, tabstop(state, pos))
    end
    return nodes.t("${" .. inner .. "}")
  end

  local name = inner:match("^([%a_][%w_]*)")
  if not name then
    return nodes.t("${" .. inner .. "}")
  end
  local rest = inner:sub(#name + 1)
  if rest == "" then
    return variable_node(state, name)
  end
  local marker = rest:sub(1, 1)
  if marker == ":" then
    return variable_node(state, name, rest:sub(2))
  elseif marker == "/" then
    local parts = split_transform(rest:sub(2))
    local pattern, repl, flags = parts[1], parts[2], parts[3]
    local format
    if repl and repl:match("^/[%a]+$") and CASE_FUNCS[repl:sub(2)] then
      format, repl = repl:sub(2), nil
    end
    return transform_node(pattern, repl, flags, format, variable_node(state, name))
  end
  return nodes.t("${" .. inner .. "}")
end

-- Scans `text` into a node list, merging adjacent literals.
parse_into = function(text, state)
  local out = {}
  local literal = {}
  local i = 1
  local n = #text

  local function flush()
    if #literal > 0 then
      out[#out + 1] = nodes.t(table.concat(literal))
      literal = {}
    end
  end

  while i <= n do
    local c = text:sub(i, i)
    if c == "\\" and i < n then
      local nxt = text:sub(i + 1, i + 1)
      if nxt == "$" or nxt == "}" or nxt == "\\" or nxt == "," then
        literal[#literal + 1] = nxt
        i = i + 2
      else
        literal[#literal + 1] = c
        i = i + 1
      end
    elseif c == "$" then
      local nxt = text:sub(i + 1, i + 1)
      if nxt == "{" then
        local close = matching_brace(text, i)
        if not close then
          literal[#literal + 1] = c
          i = i + 1
        else
          local inner = text:sub(i + 2, close - 1)
          flush()
          out[#out + 1] = parse_braced(state, inner)
          i = close + 1
        end
      elseif nxt:match("%d") then
        local j = i + 1
        while j <= n and text:sub(j, j):match("%d") do
          j = j + 1
        end
        flush()
        out[#out + 1] = tabstop(state, tonumber(text:sub(i + 1, j - 1)))
        i = j
      elseif nxt:match("[%a_]") then
        local j = i + 1
        while j <= n and text:sub(j, j):match("[%w_]") do
          j = j + 1
        end
        flush()
        out[#out + 1] = variable_node(state, text:sub(i + 1, j - 1))
        i = j
      else
        literal[#literal + 1] = c
        i = i + 1
      end
    else
      literal[#literal + 1] = c
      i = i + 1
    end
  end

  flush()
  return out
end

-- Parses a plain snippet body (no trigger); returns the node list.
function M.parse(text, opts)
  local state = { tabstops = {}, choices = {} }
  local body = parse_into(text or "", state)
  if opts and opts.prepend then
    local out = {}
    for _, node in ipairs(opts.prepend) do
      out[#out + 1] = node
    end
    for _, node in ipairs(body) do
      out[#out + 1] = node
    end
    body = out
  end
  return body
end

-- `parser.parse_snippet(context, text, opts)` — the LuaSnip entry point.
function M.parse_snippet(c, text, opts)
  local snodes = M.parse(text)
  local snippet = nodes.s(c, snodes, opts)
  -- Keep the raw body so loaders/serialisation can round-trip it.
  snippet.snippet_text = text
  return snippet
end

return M
