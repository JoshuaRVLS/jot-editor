-- Snippet engine — a full port of the LuaSnip feature set onto jot.
--
-- Public surface (`jot.snip`), mirroring LuaSnip's `ls`:
--   * constructors  s / sn / t / i / f / d / c / r / rep / fmt / isn / ms
--                   (plus the long names) and `parser.parse_snippet`
--   * registry      add_snippets, filetype_extend, get_snippets, cleanup,
--                   refresh_notify, invalidate_snippets, reload
--   * expansion     expand, expand_or_jump, expandable, expand_auto
--   * navigation    jump, jumpable, locally_jumpable, jump_destination,
--                   change_choice, choice_active, current_node, locate_node,
--                   exit_out_of_region, active
--   * environment   env (TM_* variables, namespaces, user vars)
--   * loaders       from_lua / from_vscode / from_snipmate, load_all, reload
--
-- Commands:
--   :Snippets           pick a snippet for the current filetype and expand it
--   :Snippet <trigger>  expand a literal trigger
--   :SnippetReload      re-read every snippet pack
--   :SnippetList        list the loaded filetypes
--   :SnippetToggle      enable/disable the engine
--
-- Keymaps: `Tab` expands a trigger or jumps forward, `Shift+Tab` jumps back,
-- and `Ctrl+E` / `Ctrl+Shift+E` cycle choices while a session is live. All
-- four fall back to the editor's own behavior when no snippet applies.
local config = require("jot_snip.config")
local nodes = require("jot_snip.nodes")
local parser = require("jot_snip.parser")
local env = require("jot_snip.env")
local store = require("jot_snip.store")
local session = require("jot_snip.session")
local expand = require("jot_snip.expand")
local loaders = require("jot_snip.loaders")
local keymaps = require("jot_snip.keymaps")
local lsp = require("jot_snip.lsp")

local M = {}

local log = {
  info = function(message)
    jot.ui.show_message("[snip] " .. tostring(message))
  end,
  warn = function(message)
    jot.ui.show_message("[snip] " .. tostring(message))
  end,
}

-- ---------------------------------------------------------------------------
-- Setup
-- ---------------------------------------------------------------------------

-- Installs the engine: keymaps, the LSP hook and the snippet packs. Safe to
-- call repeatedly (`:SnippetReload`, config changes, :reload).
function M.setup(opts)
  if opts then
    config.setup(opts)
    keymaps.refresh()
  end
  if not config.get("enabled") then
    keymaps.uninstall()
    lsp.uninstall()
    return false
  end
  keymaps.install()
  lsp.install()
  return true
end

-- Loads (or reloads) every configured snippet pack.
function M.reload()
  local count = loaders.reload()
  log.info(("loaded %d snippet pack entries"):format(count))
  return count
end

-- ---------------------------------------------------------------------------
-- Registry
-- ---------------------------------------------------------------------------

function M.add_snippets(ft, snippets, opts)
  return store.add_snippets(ft, snippets, opts)
end

function M.filetype_extend(ft, parents)
  return store.filetype_extend(ft, parents)
end

function M.get_snippets(ft)
  return store.get_snippets(ft or expand.filetype())
end

function M.get_autosnippets(ft)
  return store.get_autosnippets(ft or expand.filetype())
end

function M.cleanup(ft)
  return store.cleanup(ft)
end

function M.refresh_notify(ft)
  return store.refresh_notify(ft)
end

function M.invalidate_snippets()
  return store.refresh_notify(nil)
end

-- ---------------------------------------------------------------------------
-- Expansion / navigation
-- ---------------------------------------------------------------------------

function M.expand(opts)
  if type(opts) == "table" and opts.type == "snippet" then
    return expand.expand({ snippet = opts })
  end
  return expand.expand(opts)
end

function M.expand_or_jump()
  return expand.expand_or_jump()
end

function M.expandable()
  return expand.expandable()
end

function M.expand_auto()
  return expand.expand_auto()
end

function M.jump(dir, absolute)
  if not session.active() then
    return false
  end
  return session.jump(dir or 1, absolute)
end

function M.jumpable(dir)
  return session.jumpable(dir)
end

function M.locally_jumpable(dir)
  return session.locally_jumpable(dir)
end

function M.jump_destination(dir)
  return session.jump_destination(dir)
end

function M.change_choice(dir)
  return session.change_choice(dir)
end

function M.choice_active()
  return session.choice_active()
end

function M.current_node()
  local entry = session.current()
  return entry and entry.node or nil
end

function M.locate_node(pos)
  return session.locate(pos)
end

function M.exit_out_of_region()
  return session.check_region()
end

function M.active()
  return session.active()
end

function M.current_index()
  return session.current_index()
end

function M.filetype()
  return expand.filetype()
end

function M.region()
  return session.region()
end

function M.install_keymaps()
  keymaps.install()
end

function M.uninstall_keymaps()
  keymaps.uninstall()
end

-- ---------------------------------------------------------------------------
-- Commands
-- ---------------------------------------------------------------------------

local function snippet_picker()
  local filetype = expand.filetype()
  local snippets = store.get_snippets(filetype, true)
  if #snippets == 0 then
    log.info("no snippets for " .. filetype)
    return
  end
  local names = {}
  for _, snippet in ipairs(snippets) do
    local trig = snippet.trig or snippet.trigger or ""
    local description = snippet.dscr or snippet.name or ""
    names[#names + 1] = ("%s\t%s"):format(tostring(trig), description)
  end
  jot.ui.picker("Snippets (" .. filetype .. ")", function()
    return names
  end, function(label)
    local trig = label:match("^([^\t]*)")
    for _, snippet in ipairs(store.get_snippets(filetype, true)) do
      if tostring(snippet.trig or snippet.trigger or "") == trig then
        expand.expand_snippet(snippet, { matched = "" })
        return
      end
    end
  end)
end

local function snippet_list()
  local filetypes = store.known_filetypes()
  if #filetypes == 0 then
    log.info("no snippet packs loaded")
    return
  end
  local parts = {}
  for _, ft in ipairs(filetypes) do
    local count = #store.get_snippets(ft, true)
    parts[#parts + 1] = ("%s(%d)"):format(ft, count)
  end
  log.info(table.concat(parts, " "))
end

local function expand_by_trigger(argument)
  local trigger = tostring(argument or ""):gsub("^%s+", ""):gsub("%s+$", "")
  local filetype = expand.filetype()
  for _, snippet in ipairs(store.get_snippets(filetype, true)) do
    if tostring(snippet.trig or snippet.trigger or "") == trigger then
      local body = parser.parse(snippet.snippet_text or "")
      if #body == 0 then
        body = nodes.copy(snippet.nodes or {})
      end
      local ctx = expand.context(filetype)
      ctx.snippet_name = snippet.name or snippet.dscr or trigger
      local line, col = jot.cursor.get()
      return session.start(body, {
        snippet = snippet,
        filetype = filetype,
        ctx = ctx,
        start_line = line,
        start_col = col,
        end_line = line,
        end_col = col,
      })
    end
  end
  log.info("no snippet '" .. trigger .. "' for " .. filetype)
  return false
end

-- ---------------------------------------------------------------------------
-- Wiring
-- ---------------------------------------------------------------------------

local last_workspace = nil

command("Snippets", function()
  snippet_picker()
end, "Pick a snippet for the current filetype")

command("Snippet", function(argument)
  expand_by_trigger(argument)
end, "Expand a snippet by trigger")

command("SnippetReload", function()
  M.reload()
end, "Reload snippet packs")

command("SnippetList", function()
  snippet_list()
end, "List loaded snippet filetypes")

command("SnippetToggle", function()
  local enabled = not config.get("enabled")
  jot.config.set("snippet_enabled", enabled)
  M.setup()
  log.info(enabled and "snippets enabled" or "snippets disabled")
end, "Toggle the snippet engine")

autocmd("BufChange", function(event)
  if not config.get("enabled") then
    return
  end
  -- The engine's own mirror writes echo back through BufChange; on_change
  -- recognizes and swallows them.
  session.on_change(event)
  if not session.active() then
    expand.expand_auto()
  end
end)

autocmd("CursorMoved", function()
  session.check_region()
end)

autocmd("WorkspaceEnter", function(event)
  local workspace = event and event.path or jot.workspace.path()
  if workspace and workspace ~= last_workspace then
    last_workspace = workspace
    if config.get("enabled") then
      loaders.load_all()
    end
  end
end)

-- Status line: the active tabstop (and, while nothing is running, the snippet
-- count for the current filetype).
pcall(function()
  jot.status.register("snippet", {
    side = "right",
    priority = 18,
    text = function()
      if not config.get("enabled") then
        return ""
      end
      if session.active() then
        local index = session.current_index()
        return " snip:" .. tostring(index)
      end
      return ""
    end,
  })
end)

-- ---------------------------------------------------------------------------
-- Public table
-- ---------------------------------------------------------------------------

M.config = config
M.env = env
M.store = store
M.session = session
M.expand_module = expand
M.loaders = loaders
M.keymaps = keymaps
M.lsp = lsp
M.log = log
M.snip_env = nodes.env()

-- Constructors (short and long names).
for name, value in pairs(nodes.env()) do
  M[name] = value
end

M.parse_snippet = parser.parse_snippet

jot.snip = M

M.setup(config.runtime)

return M
