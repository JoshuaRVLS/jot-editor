-- Expansion: matching a trigger against the line before the cursor and
-- handing the snippet over to session.lua.
--
-- Mirrors LuaSnip's `ls.expand` / `ls.expand_or_jump` / `ls.expandable` /
-- `ls.expand_auto`, including word-boundary and regex triggers, priorities,
-- `condition` / `show_condition` and function triggers.
local nodes = require("jot_snip.nodes")
local parser = require("jot_snip.parser")
local session = require("jot_snip.session")
local store = require("jot_snip.store")
local config = require("jot_snip.config")

local M = {}

-- ---------------------------------------------------------------------------
-- Editor context
-- ---------------------------------------------------------------------------

function M.filetype()
  local ext = (jot.buffer.filetype() or ""):lower()
  local overrides = config.filetype_overrides()
  local bare = ext:gsub("^%.", "")
  if overrides[bare] then
    return overrides[bare]
  end
  local ok, language = pcall(jot.treesitter.language_for_extension, ext)
  if ok and language and language ~= "" then
    return language
  end
  return bare ~= "" and bare or "text"
end

function M.line_before_cursor()
  local line, col = jot.cursor.get()
  local text = jot.buffer.get_line(line) or ""
  return text:sub(1, col - 1), line, col
end

function M.context(filetype)
  local line, col = jot.cursor.get()
  local meta = jot.buffer.meta() or {}
  local info = jot.editor.info() or {}
  local selected = jot.buffer.get_selection() or ""
  local workspace = jot.workspace.path() or ""
  local path = meta.path or ""
  local relative = path
  if workspace ~= "" and path:sub(1, #workspace) == workspace then
    relative = path:sub(#workspace + 2)
  end
  return {
    path = path,
    name = meta.name,
    filetype = filetype,
    line = line,
    col = col,
    lines = (jot.viewport.info() or {}).window and (jot.viewport.info() or {}).window.height or 24,
    columns = (jot.viewport.info() or {}).window and (jot.viewport.info() or {}).window.width or 80,
    line_text = jot.buffer.get_line(line) or "",
    word = info.word,
    selected_text = selected,
    tab_size = jot.config.get_number("tab_size", 4),
    workspace = workspace,
    relative_path = relative,
    snippet_name = nil,
  }
end

-- ---------------------------------------------------------------------------
-- Trigger matching
-- ---------------------------------------------------------------------------

local function active_snippets(filetype, include_auto)
  local list = store.get_snippets(filetype, include_auto)
  table.sort(list, function(a, b)
    local pa = a.priority or 1000
    local pb = b.priority or 1000
    if pa ~= pb then
      return pa < pb
    end
    return tostring(a.trig or a.trigger or "") < tostring(b.trig or b.trigger or "")
  end)
  return list
end

local function trigger_text(snippet)
  return snippet.trig or snippet.trigger or ""
end

-- Tries one snippet against the line prefix. Returns matched text + captures.
local function match_one(snippet, prefix)
  local trig = trigger_text(snippet)
  local matched, captures

  if type(trig) == "function" then
    local ok, result = pcall(trig, prefix)
    if not ok or type(result) ~= "string" or result == "" then
      return nil
    end
    matched = result
    if prefix:sub(-#matched) ~= matched then
      return nil
    end
    captures = {}
  elseif snippet.regTrig then
    local s, e = prefix:find(trig .. "$")
    if not s then
      return nil
    end
    matched = prefix:sub(s, e)
    captures = { prefix:match("(" .. trig .. ")$") }
    captures = captures[1] and captures or {}
  else
    if trig == "" or prefix:sub(-#trig) ~= trig then
      return nil
    end
    matched = trig
    captures = {}
    local word = snippet.wordTrig
    if word == nil then
      word = true
    end
    if word then
      local before = prefix:sub(1, #prefix - #trig)
      if before:sub(-1):match("[%w_]") then
        return nil
      end
    end
  end

  if snippet.show_condition then
    local ok, visible = pcall(snippet.show_condition, prefix)
    if not ok or not visible then
      return nil
    end
  end
  if snippet.condition then
    local ok, allowed = pcall(snippet.condition, prefix, matched, captures)
    if not ok or not allowed then
      return nil
    end
  end
  return matched, captures
end

-- `match(prefix, filetype)` -> snippet, matched_text, captures
function M.match(prefix, filetype, include_auto)
  for _, snippet in ipairs(active_snippets(filetype, include_auto)) do
    local matched, captures = match_one(snippet, prefix)
    if matched then
      return snippet, matched, captures
    end
  end
  return nil
end

-- ---------------------------------------------------------------------------
-- Expansion
-- ---------------------------------------------------------------------------

-- Builds a fresh node body for `snippet` (re-parsing keeps choices, variables
-- and transforms live between expansions).
function M.body_for(snippet)
  if snippet.snippet_text then
    return parser.parse(snippet.snippet_text)
  end
  return nodes.copy(snippet.nodes or {})
end

function M.expand_snippet(snippet, opts)
  opts = opts or {}
  local filetype = opts.filetype or M.filetype()
  local prefix, line, col = M.line_before_cursor()
  local matched = opts.matched
  if matched == nil then
    local _, found = match_one(snippet, prefix)
    matched = found
  end
  matched = matched or ""

  local ctx = M.context(filetype)
  ctx.snippet_name = snippet.name or snippet.dscr or matched
  local body = opts.body or M.body_for(snippet)
  local start_col = col - #matched

  local ok = session.start(body, {
    snippet = snippet,
    filetype = filetype,
    ctx = ctx,
    start_line = line,
    start_col = math.max(1, start_col),
    end_line = line,
    end_col = col,
  })
  if not ok then
    return false
  end
  if config.get("history") then
    session.push_history({ snippet = snippet, matched = matched, filetype = filetype })
  end
  return true
end

-- `expand(opts)`; `opts.snippet` expands that snippet regardless of triggers.
function M.expand(opts)
  opts = opts or {}
  if opts.snippet or opts.body then
    local snippet = opts.snippet or { trig = "", nodes = opts.body }
    snippet.nodes = snippet.nodes or opts.body
    return M.expand_snippet(snippet, opts)
  end
  local filetype = opts.filetype or M.filetype()
  local prefix = M.line_before_cursor()
  local snippet, matched, captures = M.match(prefix, filetype, false)
  if not snippet then
    return false
  end
  opts.matched = matched
  opts.captures = captures
  return M.expand_snippet(snippet, opts)
end

function M.expandable(opts)
  opts = opts or {}
  local filetype = opts.filetype or M.filetype()
  local prefix = M.line_before_cursor()
  return M.match(prefix, filetype, false) ~= nil
end

-- Autosnippets (and, with snippet_auto_expand, every snippet) expand as soon
-- as their trigger matches; called on BufChange.
function M.expand_auto()
  local filetype = M.filetype()
  local prefix = M.line_before_cursor()
  local snippet, matched = M.match(prefix, filetype, true)
  if not snippet then
    return false
  end
  local is_auto = snippet.snippetType == "autosnippet" or snippet.snippetType == "auto"
  if not is_auto and not config.get("auto_expand") then
    return false
  end
  return M.expand_snippet(snippet, { matched = matched, filetype = filetype })
end

-- The Tab key: expand when a trigger is under the cursor, else jump forward.
function M.expand_or_jump()
  if session.active() and not M.expandable() then
    return session.jump(1)
  end
  if session.active() and M.expandable() then
    return M.expand()
  end
  if M.expand() then
    return true
  end
  return false
end

return M
