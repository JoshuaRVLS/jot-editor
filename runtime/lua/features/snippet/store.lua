-- Snippet registry.
--
-- Holds the snippets per filetype (plus the `extends` graph and the autosnippet
-- bucket) that `expand.lua` searches. Mirrors LuaSnip's `ls.add_snippets` /
-- `ls.filetype_extend` / `ls.get_snippets` / `ls.cleanup` surface.
local M = {}

-- filetype -> { snippets = { ... }, autosnippets = { ... } }
local by_ft = {}
local extends = {}
local listeners = {}

local function bucket(ft)
  local entry = by_ft[ft]
  if not entry then
    entry = { snippets = {}, autosnippets = {} }
    by_ft[ft] = entry
  end
  return entry
end

local function is_autosnippet(snippet)
  return snippet and (snippet.snippetType == "autosnippet" or snippet.snippetType == "auto")
end

local function normalize(snippet)
  if type(snippet) ~= "table" then
    return nil
  end
  if snippet.type == "table" and snippet.trigger then
    -- Already a snippet definition table ({"trig", s(...)} shape is handled
    -- by the callers); nothing to normalize.
    return snippet
  end
  return snippet
end

-- add_snippets(ft, snippets[, opts]) — `ft` is a filetype or a list of them.
-- `snippets` is either a list or a `{ [key] = snippet }` map (the keyed form
-- LuaSnip 2.x uses for `snip_env` collections).
function M.add_snippets(ft, snippets, opts)
  if type(snippets) ~= "table" then
    return
  end
  local filetypes = type(ft) == "table" and ft or { ft }
  local list = {}
  if snippets[1] ~= nil then
    for _, snippet in ipairs(snippets) do
      list[#list + 1] = snippet
    end
  else
    for key, snippet in pairs(snippets) do
      if type(snippet) == "table" and (snippet.trig or snippet.trigger) then
        snippet.key = snippet.key or key
        list[#list + 1] = snippet
      end
    end
  end

  for _, filetype in ipairs(filetypes) do
    local entry = bucket(filetype)
    for _, snippet in ipairs(list) do
      local node = normalize(snippet)
      if node then
        if is_autosnippet(node) then
          entry.autosnippets[#entry.autosnippets + 1] = node
        else
          entry.snippets[#entry.snippets + 1] = node
        end
      end
    end
    M.refresh_notify(filetype)
  end
  if opts and opts.refresh_notify then
    M.refresh_notify(opts.refresh_notify)
  end
end

-- filetype_extend(ft, parents) — snippets for `ft` also include the parents'.
function M.filetype_extend(ft, parents)
  local list = extends[ft]
  if not list then
    list = {}
    extends[ft] = list
  end
  for _, parent in ipairs(type(parents) == "table" and parents or { parents }) do
    list[#list + 1] = parent
  end
  M.refresh_notify(ft)
end

function M.extends_of(ft)
  local seen = {}
  local out = {}
  local function visit(name)
    if seen[name] then
      return
    end
    seen[name] = true
    out[#out + 1] = name
    for _, parent in ipairs(extends[name] or {}) do
      visit(parent)
    end
  end
  visit(ft)
  return out
end

-- Every snippet reachable from `ft`, including extended filetypes.
function M.get_snippets(ft, include_auto)
  local out = {}
  for _, name in ipairs(M.extends_of(ft)) do
    local entry = by_ft[name]
    if entry then
      for _, snippet in ipairs(entry.snippets) do
        out[#out + 1] = snippet
      end
      if include_auto then
        for _, snippet in ipairs(entry.autosnippets) do
          out[#out + 1] = snippet
        end
      end
    end
  end
  return out
end

function M.get_autosnippets(ft)
  local out = {}
  for _, name in ipairs(M.extends_of(ft)) do
    local entry = by_ft[name]
    if entry then
      for _, snippet in ipairs(entry.autosnippets) do
        out[#out + 1] = snippet
      end
    end
  end
  return out
end

-- cleanup(ft) drops a filetype's snippets (and its extends edge).
function M.cleanup(ft)
  by_ft[ft] = nil
  extends[ft] = nil
  M.refresh_notify(ft)
end

function M.cleanup_all()
  by_ft = {}
  extends = {}
end

function M.known_filetypes()
  local out = {}
  for ft in pairs(by_ft) do
    out[#out + 1] = ft
  end
  table.sort(out)
  return out
end

-- Change notifications: `jot.events`-style fan-out, so a consumer (status
-- line, picker) can re-read the store.
function M.on_change(fn)
  listeners[#listeners + 1] = fn
end

function M.refresh_notify(ft)
  for _, fn in ipairs(listeners) do
    pcall(fn, ft)
  end
end

return M
