-- Snippet node model and the constructors snippets are written with.
--
-- The surface mirrors LuaSnip's: s / sn / t / i / f / d / c / r / rep / fmt /
-- multi_snippet / isn (plus the long names), 1.x and 2.x call signatures where
-- they differ, and a few helpers the engine needs (copy, equals, walk).
--
-- A node is a plain table with a `type` field; session.lua renders it.
-- Tabstop identity is `node.pos`: a number (classic LuaSnip position) or a
-- string key. Nodes sharing a tabstop index share their text, which is what
-- makes `i(1, "x")` mirroring work.
local M = {}

local next_id = 0
local function assign_id(node)
  next_id = next_id + 1
  node.id = next_id
  return node
end

function M.is_node(value)
  return type(value) == "table" and type(value.type) == "string"
end

-- Accepts a string, a node, or a list mixing both and returns a node list.
function M.to_nodes(value)
  if value == nil then
    return {}
  end
  if type(value) == "string" or type(value) == "number" then
    return { assign_id({ type = "text", text = tostring(value) }) }
  end
  if M.is_node(value) then
    return { value }
  end
  local out = {}
  for _, item in ipairs(value) do
    if type(item) == "string" or type(item) == "number" then
      out[#out + 1] = assign_id({ type = "text", text = tostring(item) })
    elseif M.is_node(item) then
      out[#out + 1] = item
    end
  end
  return out
end

-- Deep copy (snippets are expanded more than once, and a session must never
-- write back into the definition).
function M.copy(node)
  if type(node) ~= "table" then
    return node
  end
  local out = {}
  for key, value in pairs(node) do
    if key == "fn" or key == "id" then
      out[key] = value
    elseif type(value) == "table" then
      out[key] = M.copy(value)
    else
      out[key] = value
    end
  end
  return out
end

function M.equals(a, b)
  if a == b then
    return true
  end
  if type(a) ~= "table" or type(b) ~= "table" then
    return false
  end
  for key, value in pairs(a) do
    if key ~= "id" and not M.equals(value, b[key]) then
      return false
    end
  end
  for key in pairs(b) do
    if a[key] == nil then
      return false
    end
  end
  return true
end

-- Visits every node depth-first; `fn(node, parent_list, index)`.
function M.walk(nodes, fn)
  if type(nodes) ~= "table" then
    return
  end
  for index, node in ipairs(nodes) do
    if M.is_node(node) then
      fn(node, nodes, index)
      for _, field in ipairs({ "nodes", "default", "items" }) do
        local child = node[field]
        if type(child) == "table" then
          if field == "items" then
            for _, choice in ipairs(child) do
              M.walk(choice.nodes or choice, fn)
            end
          else
            M.walk(child.nodes or child, fn)
          end
        end
      end
    end
  end
end

-- Snippet context: LuaSnip 2.x passes a table, 1.x a plain trigger string.
local function context(value)
  if type(value) == "table" then
    return value
  end
  return { trig = tostring(value or "") }
end

local function with_opts(target, opts)
  if type(opts) ~= "table" then
    return target
  end
  for key, value in pairs(opts) do
    if target[key] == nil then
      target[key] = value
    end
  end
  return target
end

-- s(context, nodes, opts) / s(trigger, nodes, opts)
function M.s(c, snodes, opts)
  local ctx = context(c)
  -- 1.x nested form: s({...}, nodes) plus a string first element is a trigger.
  local node = assign_id({
    type = "snippet",
    nodes = M.to_nodes(snodes),
    trig = ctx.trig or ctx.trigger or "",
    name = ctx.name,
    dscr = ctx.dscr or ctx.description,
    wordTrig = ctx.wordTrig,
    regTrig = ctx.regTrig,
    snippetType = ctx.snippetType,
    priority = ctx.priority or ctx.priority,
    condition = ctx.condition,
    show_condition = ctx.show_condition,
    docstring = ctx.docstring,
    key = ctx.key,
    opts = opts or {},
  })
  return node
end

-- sn(context, nodes, opts) — a snippet node, only meaningful nested inside
-- another snippet (or returned from a function/dynamic node).
function M.sn(c, snodes, opts)
  local ctx = context(c)
  local node = assign_id({
    type = "nested",
    nodes = M.to_nodes(snodes),
    trig = ctx.trig,
    dscr = ctx.dscr or ctx.description,
    opts = opts or {},
  })
  if type(c) == "number" then
    node.pos = c
  end
  return node
end

-- t(text, opts) — literal text (an opts.indent marks the text for reindenting).
function M.t(text, opts)
  return assign_id({ type = "text", text = text, indent = opts and opts.indent })
end

-- i(pos, default, opts)
function M.i(pos, default, opts)
  local node = assign_id({
    type = "insert",
    pos = pos,
    default = default,
    opts = opts or {},
    key = opts and opts.key,
  })
  if node.key ~= nil then
    node.pos = node.key
  end
  return node
end

-- f(fn, argnodes, opts); opts.user_args carries the extra arguments.
function M.f(fn, argnodes, opts)
  opts = opts or {}
  if type(argnodes) == "table" and argnodes.user_args ~= nil and opts.user_args == nil then
    -- 1.x shape: f(fn, user_args, argnodes)
    opts = { user_args = argnodes.user_args or argnodes }
    if type(opts.user_args) == "table" and opts.user_args[1] and M.is_node(opts.user_args[1]) then
      argnodes, opts.user_args = opts.user_args, nil
    end
  end
  return assign_id({
    type = "function",
    fn = fn,
    argnodes = argnodes or {},
    user_args = opts.user_args,
    docstring = opts.docstring,
    opts = opts,
    evaluated = false,
    value = nil,
  })
end

-- d(pos, fn, opts)
function M.d(pos, fn, opts)
  opts = opts or {}
  if type(pos) == "function" then
    fn, pos = pos, nil
  end
  local user_args = opts.user_args
  if user_args == nil and type(opts) == "table" and opts.user_args == nil then
    -- 1.x shape: d(pos, fn, user_args) — anything that is not an option table.
    local looks_like_opts = false
    for _, key in ipairs({ "user_args", "docstring", "show_condition", "condition", "key" }) do
      if opts[key] ~= nil then
        looks_like_opts = true
        break
      end
    end
    if not looks_like_opts then
      user_args = opts
    end
  end
  return assign_id({
    type = "dynamic",
    pos = pos,
    fn = fn,
    argnodes = opts.argnodes or {},
    user_args = user_args,
    docstring = opts.docstring,
    opts = opts,
    state = nil,
    evaluated = false,
    value = nil,
  })
end

-- c(pos, items, opts) — items are strings or node lists.
function M.c(pos, items, opts)
  local choices = {}
  for index, item in ipairs(items or {}) do
    choices[index] = { nodes = M.to_nodes(item) }
  end
  return assign_id({
    type = "choice",
    pos = pos,
    items = choices,
    index = 1,
    restore = (opts or {}).restore,
    docstring = (opts or {}).docstring,
    opts = opts or {},
  })
end

-- r(nodes, opts)
function M.r(snodes, opts)
  return assign_id({
    type = "restore",
    nodes = M.to_nodes(snodes),
    restore = (opts or {}).restore or "last",
    opts = opts or {},
    stored = nil,
  })
end

-- rep(nodes, times, opts); also accepts the 1.x rep(1, nodes, times).
function M.rep(a, b, c)
  local snodes, times
  if type(a) == "table" then
    snodes, times = a, b
  else
    snodes, times = b, c
  end
  return assign_id({
    type = "repeat",
    nodes = M.to_nodes(snodes),
    times = tonumber(times) or 2,
    opts = {},
    expanded = 0,
  })
end

-- fmt(parts, opts) — "a {:>10} b" style formatting over text/node parts.
function M.fmt(parts, opts)
  return assign_id({
    type = "format",
    parts = M.to_nodes(parts),
    opts = opts or {},
  })
end

-- multi_snippet(delimiter, nodes, opts) / ms
function M.multi_snippet(delimiter, snodes, opts)
  return assign_id({
    type = "multi",
    delimiter = tostring(delimiter or ""),
    nodes = M.to_nodes(snodes),
    opts = opts or {},
  })
end

-- isn(pos, text, opts, indent) — insert node whose default is the given text
-- reindented relative to the snippet start.
function M.isn(pos, text, opts, indent)
  return assign_id({
    type = "insert",
    pos = pos,
    default = text,
    indent_snippet = true,
    indent_text = indent,
    opts = opts or {},
  })
end

-- Aliases: LuaSnip exports both the short and the long names.
M.snippet = M.s
M.snippet_node = M.sn
M.text_node = M.t
M.insert_node = M.i
M.function_node = M.f
M.dynamic_node = M.d
M.choice_node = M.c
M.restore_node = M.r
M.repeat_node = M.rep
M.format_node = M.fmt
M.indent_snippet_node = M.isn
M.ms = M.multi_snippet

-- The symbols a snippet file sees as globals (`snip_env`).
function M.env()
  local env = {}
  for _, name in ipairs({
    "s", "sn", "t", "i", "f", "d", "c", "r", "rep", "fmt", "isn",
    "multi_snippet", "ms",
    "snippet", "snippet_node", "text_node", "insert_node", "function_node",
    "dynamic_node", "choice_node", "restore_node", "repeat_node",
    "format_node", "indent_snippet_node",
  }) do
    env[name] = M[name]
  end
  return env
end

return M
