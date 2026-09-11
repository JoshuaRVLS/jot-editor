-- Snippet engine configuration.
--
-- Every scalar option lives under the `snippet_*` keys of the normal jot
-- config store, so `:settings`, `config.lua` and `jot.snip.setup{}` all edit
-- the same values live. Non-scalar options (keymaps table, extra loaders)
-- live on the module through `setup`.
local M = {}

M.defaults = {
  enabled = true,                -- register keymaps and the LSP snippet hook
  auto_expand = false,            -- expand triggers as they are typed
  tab_key = "Tab",                -- expand_or_jump forward
  backtab_key = "Shift+Tab",      -- jump backward
  choice_next_key = "Ctrl+E",     -- next choice
  choice_prev_key = "Ctrl+Shift+E",
  highlight = true,               -- placeholder highlight + selection
  paths = "",                     -- extra snippet roots (comma separated)
  load_vscode = true,             -- load VSCode/JSON snippet packs
  load_snipmate = true,           -- load snipMate snippet files
  filetypes = "",                 -- "ext=ft,ext=ft" extension overrides
  region_check_events = "CursorMoved",
  update_events = "BufChange",
  delete_check_events = "BufChange,BufSave",
  history = true,                 -- remember the last expanded snippet (rep)
  history_size = 32,
}

-- Only ever set from Lua (`jot.snip.setup{ keymaps = ... }`).
M.runtime = {
  keymaps = nil,   -- { [key] = function } overrides the defaults
  snip_env = nil,  -- extra symbols exposed to Lua snippet files
}

local function key(name)
  return "snippet_" .. name
end

function M.get(name)
  local default = M.defaults[name]
  if type(default) == "boolean" then
    return jot.config.get_bool(key(name), default)
  elseif type(default) == "number" then
    return jot.config.get_number(key(name), default)
  end
  return jot.config.get(key(name), default)
end

-- Applies `jot.snip.setup{ ... }`; scalars go into the config store, the
-- non-scalars stay on the module.
function M.setup(opts)
  if type(opts) ~= "table" then
    return
  end
  for name, value in pairs(opts) do
    if name == "keymaps" or name == "snip_env" then
      M.runtime[name] = value
    elseif M.defaults[name] ~= nil then
      jot.config.set(key(name), value)
    end
  end
end

-- The comma/whitespace separated list of extra snippet roots.
function M.paths()
  local out = {}
  for entry in tostring(M.get("paths") or ""):gmatch("[^,%s]+") do
    out[#out + 1] = entry
  end
  return out
end

-- `extension -> filetype` overrides ("cs=c_sharp,.tsx=typescriptreact").
function M.filetype_overrides()
  local out = {}
  for entry in tostring(M.get("filetypes") or ""):gmatch("[^,%s]+") do
    local ext, ft = entry:match("^(.-)=(.*)$")
    if ext and ft and ft ~= "" then
      out[(ext:gsub("^%.", "")):lower()] = ft
    end
  end
  return out
end

return M
