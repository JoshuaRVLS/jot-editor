-- Snippet environment: the `TM_*`-style variables `$VAR` / `${VAR}` resolve
-- to, plus user-registered values.
--
-- Mirrors LuaSnip's `ls.env`: a default `TM` namespace with the standard
-- filename/line/selection variables, user variables via `env.extend`, extra
-- namespaces via `env.namespace`.
local M = {}

local function join(...)
  local sep = package.config and package.config:sub(1, 1) or "/"
  local parts = {}
  for _, p in ipairs({ ... }) do
    if p and p ~= "" then
      parts[#parts + 1] = p
    end
  end
  return table.concat(parts, sep)
end

local function basename(path)
  return (tostring(path or ""):gsub("\\", "/"):match("[^/]*$")) or ""
end

local function dirname(path)
  local p = tostring(path or ""):gsub("\\", "/")
  local dir = p:match("^(.*)/[^/]*$")
  return dir or ""
end

-- The variables the editor provides for every expansion. `ctx` carries the
-- live editor state (path, line, column, line text, word, selection, ...).
function M.builtin(name, ctx)
  ctx = ctx or {}
  local path = ctx.path or ""
  local filename = basename(path)
  local selected = ctx.selected_text or ""
  local line_text = ctx.line_text or ""
  local word = ctx.word or ""
  local tab_size = ctx.tab_size or 4

  if name == "TM_FILENAME" then
    return filename
  elseif name == "TM_FILENAME_BASE" then
    return (filename:gsub("%.[^%.]*$", ""))
  elseif name == "TM_DIRECTORY" then
    return dirname(path)
  elseif name == "TM_FILEPATH" then
    return path
  elseif name == "TM_RELATIVE_FILEPATH" or name == "WORKSPACE_RELATIVE_FILEPATH" then
    return ctx.relative_path or path
  elseif name == "TM_LINE_INDEX" then
    return tostring(math.max(0, (ctx.line or 1) - 1))
  elseif name == "TM_LINE_NUMBER" then
    return tostring(ctx.line or 1)
  elseif name == "TM_CURRENT_LINE" then
    return line_text
  elseif name == "TM_CURRENT_WORD" then
    return word
  elseif name == "TM_SELECTED_TEXT" then
    return selected
  elseif name == "TM_SOFT_TABS" then
    return "YES"
  elseif name == "TM_TAB_SIZE" then
    return tostring(tab_size)
  elseif name == "TM_COLUMN_NUMBER" then
    return tostring(ctx.col or 1)
  elseif name == "TM_COLUMNS" then
    return tostring(ctx.columns or 80)
  elseif name == "TM_LINES" then
    return tostring(ctx.lines or 24)
  elseif name == "LS_WORKSPACE" then
    return ctx.workspace or ""
  elseif name == "LS_SNIPPET_NAME" or name == "SNIP_NAME" then
    return ctx.snippet_name or ""
  end
  return nil
end

-- name -> value for user-registered variables.
M.vars = {}

-- Filetype-extended variables, mirroring `ls.env_namespace("TM", {vars=...})`
-- applied per filetype; kept simple: one flat table plus one per filetype.
M.filetype_vars = {}

function M.get(name, ctx)
  name = tostring(name or ""):upper()
  if ctx and ctx.filetype and M.filetype_vars[ctx.filetype] then
    local ft = M.filetype_vars[ctx.filetype][name]
    if ft ~= nil then
      return tostring(ft)
    end
  end
  if M.vars[name] ~= nil then
    return tostring(M.vars[name])
  end
  return M.builtin(name, ctx)
end

function M.extend(name, value)
  M.vars[tostring(name):upper()] = value
end

function M.get_namespaces()
  local out = { "TM" }
  for ns in pairs(M._namespaces or {}) do
    out[#out + 1] = ns
  end
  return out
end

-- `env.namespace("TM", { vars = { ... } })` returns a factory that prefixes
-- every name with the namespace, like LuaSnip's `env_namespace`.
function M.namespace(name, opts)
  M._namespaces = M._namespaces or {}
  M._namespaces[name] = true
  opts = opts or {}
  for key, value in pairs(opts.vars or {}) do
    M.extend(key, value)
  end
  return function(trigger, nodes, opts2)
    return { trigger = name .. "_" .. tostring(trigger), nodes = nodes, opts = opts2 }
  end
end

-- Resolves `$VAR` / `${VAR}` occurrences inside a plain string.
function M.resolve(text, ctx)
  if not text or not text:find("$", 1, true) then
    return text
  end
  local out = text:gsub("%${([%u%d_]+)}", function(name)
    return M.get(name, ctx) or ""
  end)
  out = out:gsub("%$([%u%d_]+)", function(name)
    if name:match("^%d+$") then
      return "$" .. name -- a tabstop, leave it alone
    end
    return M.get(name, ctx) or ""
  end)
  return out
end

M.join = join

return M
