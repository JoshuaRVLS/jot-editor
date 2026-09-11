-- Snippet pack loaders.
--
-- Three formats, mirroring LuaSnip's `loaders` module:
--   * `from_lua`     — `<root>/snippets/*.lua`, run with the snippet symbols
--                      as globals (`snip_env`); may return a list of snippets
--                      or a `{ filetype = snippets }` map.
--   * `from_vscode`  — `<root>/snippets/**/*.json` and `*.code-snippets`
--                      (plus `package.json` manifests).
--   * `from_snipmate`— `<root>/snippets/<filetype>.snippets`.
--
-- Roots are `<config>/snippets`, `<workspace>/.jot/snippets` and anything in
-- `snippet_paths` (see config.lua).
local nodes = require("jot_snip.nodes")
local parser = require("jot_snip.parser")
local store = require("jot_snip.store")
local config = require("jot_snip.config")
local json = require("jot_snip.json")

local M = {}

local function dirname(path)
  return tostring(path or ""):match("^(.*)[/\\][^/\\]*$") or ""
end

-- The jot config root (`~/.config/jot`), derived from the settings path.
function M.config_root()
  local ok, path = pcall(jot.config.path)
  if not ok or type(path) ~= "string" or path == "" then
    return nil
  end
  local dir = dirname(path)
  if dir:match("[/\\]configs$") then
    dir = dirname(dir)
  end
  return dir
end

-- Every directory that may hold snippets, in priority order.
function M.roots()
  local out = {}
  local seen = {}
  local function add(path)
    if path and path ~= "" and not seen[path] then
      seen[path] = true
      out[#out + 1] = path
    end
  end
  local workspace = jot.workspace.path()
  if workspace and workspace ~= "" then
    add(workspace .. "/.jot/snippets")
    add(workspace .. "/snippets")
  end
  local root = M.config_root()
  if root then
    add(root .. "/snippets")
  end
  for _, path in ipairs(config.paths()) do
    add(path)
  end
  return out
end

local function list_dir(path)
  local ok, entries = pcall(jot.file.list, path)
  if not ok or type(entries) ~= "table" then
    return {}
  end
  return entries
end

local function read_file(path)
  local ok, text = pcall(jot.file.read, path)
  if not ok or type(text) ~= "string" then
    -- Fall back to an open buffer (the file may be the one being edited).
    local buffer_ok, buffer_text = pcall(jot.buffer.text, path)
    if buffer_ok and type(buffer_text) == "string" then
      return buffer_text
    end
    return nil
  end
  return text
end

-- Walks `dir` recursively, calling `fn(path, name, is_dir)` for every entry.
local function walk(dir, fn, depth)
  depth = depth or 0
  if depth > 6 then
    return
  end
  for _, entry in ipairs(list_dir(dir)) do
    if entry.dir then
      walk(entry.path, fn, depth + 1)
    else
      fn(entry.path, entry.name)
    end
  end
end

local function is_workspace_safe(path)
  return path and path ~= ""
end

-- ---------------------------------------------------------------------------
-- Lua files
-- ---------------------------------------------------------------------------

-- Runs one Lua snippet file with `snip_env` available as globals. Returns the
-- file's return value, or nil when it could not be loaded.
function M.load_lua_file(path, extra_env)
  local text = read_file(path)
  if not text then
    return nil
  end
  -- The snippet file runs with the snippet symbols as globals (`snip_env`),
  -- plus the plain Lua standard library and `jot`. `load` with a custom
  -- environment is Lua 5.4's `setfenv`.
  local env = nodes.env()
  env.jot = jot
  env.vim = jot
  env.require = require
  env.pairs, env.ipairs, env.type, env.tostring, env.tonumber =
      pairs, ipairs, type, tostring, tonumber
  env.string, env.table, env.math, env.os, env.io = string, table, math, os, io
  env.select, env.pcall, env.error, env.setmetatable, env.rawget, env.rawset =
      select, pcall, error, setmetatable, rawget, rawset
  env.print = print
  for key, value in pairs(extra_env or {}) do
    env[key] = value
  end
  local chunk, err = load(text, "@" .. path, "t", env)
  if not chunk then
    return nil, err
  end
  local ok, result = pcall(chunk)
  if not ok then
    return nil, result
  end
  return result
end

-- Adds the value a Lua snippet file returned. Accepted shapes:
--   { snippet1, snippet2 }                 -> the file's own filetype
--   { filetype = { snippet, ... } }        -> per-filetype collections
--   a single snippet table
function M.apply_lua_result(result, filetype, opts)
  if type(result) ~= "table" then
    return 0
  end
  local added = 0
  local function add(ft, snippets)
    if type(snippets) ~= "table" then
      return
    end
    store.add_snippets(ft, snippets, opts)
    added = added + 1
  end
  if result.trig or result.trigger then
    add(filetype, { result })
  elseif result[1] then
    add(filetype, result)
  else
    for ft, value in pairs(result) do
      if type(value) == "table" then
        if value.trig or value.trigger then
          add(ft, { value })
        else
          add(ft, value)
        end
      end
    end
  end
  return added
end

function M.from_lua(opts)
  opts = opts or {}
  local loaded = 0
  for _, root in ipairs(opts.roots or M.roots()) do
    for _, entry in ipairs(list_dir(root)) do
      local function handle(path, name)
        if not name:match("%.lua$") then
          return
        end
        local filetype = (name:gsub("%.lua$", ""):gsub("^_.*", ""))
        if filetype == "init" or filetype == "" then
          filetype = nil
        end
        if not filetype and not opts.default_filetype then
          -- `snippets.lua` / `init.lua`: the file itself declares filetypes.
          filetype = nil
        end
        local result, err = M.load_lua_file(path)
        if result then
          loaded = loaded + M.apply_lua_result(result, filetype or opts.default_filetype or "all", opts)
        elseif err and opts.warn then
          jot.ui.show_message("snippet load error: " .. tostring(err))
        end
      end
      if entry.dir then
        if opts.recursive ~= false then
          walk(entry.path, handle)
        end
      else
        handle(entry.path, entry.name)
      end
    end
  end
  return loaded
end

-- ---------------------------------------------------------------------------
-- VSCode / JSON
-- ---------------------------------------------------------------------------

local function scope_filetypes(scope, fallback)
  local out = {}
  if type(scope) == "string" then
    for ft in scope:gmatch("[^,%s]+") do
      out[#out + 1] = ft
    end
  elseif type(scope) == "table" then
    for _, ft in ipairs(scope) do
      out[#out + 1] = tostring(ft)
    end
  end
  if #out == 0 and fallback then
    out[1] = fallback
  end
  return out
end

-- One VSCode entry -> snippets (a list prefix yields one per prefix).
function M.vscode_entry(entry, fallback_ft, file)
  local snippets = {}
  if type(entry) ~= "table" then
    return snippets
  end
  local prefixes = {}
  if type(entry.prefix) == "table" then
    prefixes = entry.prefix
  elseif entry.prefix ~= nil then
    prefixes = { entry.prefix }
  elseif entry.trigger ~= nil then
    prefixes = { entry.trigger }
  end
  local body = entry.body
  if type(body) == "table" then
    body = table.concat(body, "\n")
  end
  if type(body) ~= "string" or body == "" then
    return snippets
  end
  for _, prefix in ipairs(prefixes) do
    local snippet = parser.parse_snippet({
      trig = tostring(prefix),
      dscr = entry.description or entry.dscr,
      name = entry.name or entry.description,
    }, body)
    snippet.filetype = file
    snippets[#snippets + 1] = snippet
  end
  return snippets
end

local function load_vscode_file(path, name, opts)
  local text = read_file(path)
  if not text then
    return 0
  end
  local data = json.decode(text)
  if type(data) ~= "table" then
    return 0
  end

  -- package.json manifests: contributes.snippets -> { path, language }
  if name == "package.json" then
    local contributes = data.contributes and data.contributes.snippets
    if type(contributes) == "table" then
      local dir = dirname(path)
      for _, item in ipairs(contributes) do
        if item.path then
          local snippet_path = dir .. "/" .. item.path:gsub("^%./", "")
          load_vscode_file(snippet_path, "pack.json", { fallback = item.language })
        end
      end
    end
    return 0
  end

  local fallback = opts.fallback or (name:gsub("%.code%-snippets$", ""):gsub("%.json$", ""))

  -- `{ "scopes": { "cpp": { ... } } }` (VSCode user snippets) or a flat map.
  local scoped = false
  for _, key in ipairs({ "scopes", "scope" }) do
    if type(data[key]) == "table" and not data.trig and not data.body then
      scoped = true
      for scope_name, entries in pairs(data[key]) do
        if type(entries) == "table" then
          for _, entry in pairs(entries) do
            local filetypes = scope_filetypes(scope_name, fallback)
            for _, ft in ipairs(filetypes) do
              store.add_snippets(ft, M.vscode_entry(entry, ft, ft), opts)
            end
          end
        end
      end
    end
  end
  if scoped then
    return 1
  end

  if data.trig or data.body then
    -- A single entry.
    local filetypes = scope_filetypes(data.scope, fallback)
    for _, ft in ipairs(filetypes) do
      store.add_snippets(ft, M.vscode_entry(data, ft, ft), opts)
    end
    return 1
  end

  -- A map of entries (the common `.code-snippets` shape).
  local count = 0
  for _, entry in pairs(data) do
    if type(entry) == "table" then
      local filetypes = scope_filetypes(entry.scope, fallback)
      for _, ft in ipairs(filetypes) do
        local snippets = M.vscode_entry(entry, ft, ft)
        if #snippets > 0 then
          store.add_snippets(ft, snippets, opts)
          count = count + 1
        end
      end
    end
  end
  return count
end

function M.from_vscode(opts)
  opts = opts or {}
  local loaded = 0
  for _, root in ipairs(opts.roots or M.roots()) do
    local function handle(path, name)
      if name:match("%.json$") or name:match("%.code%-snippets$") then
        loaded = loaded + load_vscode_file(path, name, opts)
      end
    end
    for _, entry in ipairs(list_dir(root)) do
      if entry.dir then
        walk(entry.path, handle)
      else
        handle(entry.path, entry.name)
      end
    end
  end
  return loaded
end

-- ---------------------------------------------------------------------------
-- snipMate
-- ---------------------------------------------------------------------------

-- Parses a snipMate file into (filetype -> { body lines }) first, so the
-- `extends` directive can be honoured.
function M.parse_snipmate(text)
  local out = {}
  local current = nil
  for line in tostring(text or ""):gmatch("[^\n]*") do
    local trigger = line:match("^snippet%s+(.+)$")
    if trigger then
      current = { trig = trigger:gsub("%s+$", ""), body = {} }
      out[#out + 1] = current
    elseif line:match("^extends") or line:match("^version") or line:match("^%s*#") then
      current = nil
    elseif current then
      local body_line = line:gsub("^\t", ""):gsub("^%s%s%s%s", "")
      if body_line ~= "" or #current.body > 0 then
        current.body[#current.body + 1] = body_line
      end
    end
  end
  return out
end

function M.from_snipmate(opts)
  opts = opts or {}
  local loaded = 0
  local function load_file(path, filetype)
    local text = read_file(path)
    if not text then
      return
    end
    for _, entry in ipairs(M.parse_snipmate(text)) do
      local body = table.concat(entry.body, "\n")
      if body:find("%S") then
        local snippet = parser.parse_snippet({
          trig = entry.trig,
          name = entry.trig,
        }, body)
        store.add_snippets(filetype, { snippet }, opts)
        loaded = loaded + 1
      end
    end
  end
  for _, root in ipairs(opts.roots or M.roots()) do
    local function handle(path, name)
      local filetype = name:match("^(.-)%.snippets$")
      if filetype then
        load_file(path, filetype)
      end
    end
    local snippets_dir = root .. "/snippets"
    local entries = list_dir(snippets_dir)
    if next(entries) == nil then
      entries = list_dir(root)
    end
    for _, entry in ipairs(entries) do
      if entry.dir then
        for _, nested in ipairs(list_dir(entry.path)) do
          if not nested.dir then
            handle(nested.path, nested.name)
          end
        end
      else
        handle(entry.path, entry.name)
      end
    end
  end
  return loaded
end

-- ---------------------------------------------------------------------------
-- Everything
-- ---------------------------------------------------------------------------

-- Loads every enabled format. Called on startup, `:SnippetReload` and after a
-- workspace change.
function M.load_all(opts)
  opts = opts or {}
  if not config.get("enabled") then
    return 0
  end
  local total = 0
  total = total + M.from_lua(opts)
  if config.get("load_vscode") then
    total = total + M.from_vscode(opts)
  end
  if config.get("load_snipmate") then
    total = total + M.from_snipmate(opts)
  end
  return total
end

function M.reload()
  store.cleanup_all()
  return M.load_all()
end

M.walk = walk
M.read_file = read_file
M.dirname = dirname

return M
