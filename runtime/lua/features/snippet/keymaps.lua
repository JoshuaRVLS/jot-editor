-- Engine keymaps.
--
-- Two groups:
--   * `Tab` / `Shift+Tab` — always registered (so a trigger under the cursor
--     expands), with an exact fallback to the editor's own Tab behavior via
--     `jot.edit.tab` / `jot.edit.shift_tab`, and a hand-off to the LSP
--     completion popup when one is open (the Lua keymap shadows the native
--     accept-on-Tab otherwise).
--   * the choice keys — registered only while a session is live, so Ctrl+E is
--     not stolen from the file picker during normal editing.
local session = require("jot_snip.session")
local expand = require("jot_snip.expand")
local config = require("jot_snip.config")

local M = {}

local installed = false
local session_keys = nil

local function completion_visible()
  local ok, state = pcall(jot.lsp.completions)
  return ok and type(state) == "table" and state.visible == true
end

-- True when the session is at a stop the user can navigate away from.
local function session_at_stop()
  return session.active()
end

local function tab()
  if session_at_stop() and (session.jumpable(1) or session.jumpable(-1)) then
    if expand.expand_or_jump() then
      return
    end
  end
  if completion_visible() then
    if jot.lsp.accept_completion() then
      return
    end
  end
  if expand.expand() then
    return
  end
  if session.active() and session.jump(1) then
    return
  end
  -- Not a snippet: exactly the editor's own Tab.
  jot.edit.tab()
end

local function backtab()
  if session.active() then
    if expand.expandable() then
      expand.expand()
      return
    end
    if session.jumpable(-1) and session.jump(-1) then
      return
    end
  end
  jot.edit.shift_tab()
end

local function choice_next()
  if session.active() then
    session.change_choice(1)
  end
end

local function choice_prev()
  if session.active() then
    session.change_choice(-1)
  end
end

-- Registers the transient choice keymaps.
local function install_session_keys()
  if session_keys then
    return
  end
  local next_key = config.get("choice_next_key")
  local prev_key = config.get("choice_prev_key")
  session_keys = {}
  if next_key and next_key ~= "" then
    jot.keymap.set(next_key, choice_next, "Snippet: next choice", "editor")
    session_keys[#session_keys + 1] = next_key
  end
  if prev_key and prev_key ~= "" then
    jot.keymap.set(prev_key, choice_prev, "Snippet: previous choice", "editor")
    session_keys[#session_keys + 1] = prev_key
  end
end

local function remove_session_keys()
  if not session_keys then
    return
  end
  for _, key in ipairs(session_keys) do
    pcall(jot.keymap.remove, key, "editor")
  end
  session_keys = nil
end

function M.install()
  if installed then
    return
  end
  if not config.get("enabled") then
    return
  end
  local keymaps = config.runtime.keymaps or {}
  local tab_key = keymaps.tab or config.get("tab_key")
  local backtab_key = keymaps.backtab or config.get("backtab_key")
  if tab_key and tab_key ~= "" then
    jot.keymap.set(tab_key, keymaps.tab_fn or tab, "Snippet: expand or jump", "editor")
  end
  if backtab_key and backtab_key ~= "" then
    jot.keymap.set(backtab_key, keymaps.backtab_fn or backtab, "Snippet: jump back", "editor")
  end
  session.on_start(install_session_keys)
  session.on_exit(remove_session_keys)
  installed = true
end

function M.uninstall()
  if not installed then
    return
  end
  local keymaps = config.runtime.keymaps or {}
  local tab_key = keymaps.tab or config.get("tab_key")
  local backtab_key = keymaps.backtab or config.get("backtab_key")
  if tab_key and tab_key ~= "" then
    pcall(jot.keymap.remove, tab_key, "editor")
  end
  if backtab_key and backtab_key ~= "" then
    pcall(jot.keymap.remove, backtab_key, "editor")
  end
  remove_session_keys()
  installed = false
end

-- Re-applies the bindings after a config change (`:reload`, :settings).
function M.refresh()
  if installed then
    M.uninstall()
  end
  M.install()
end

return M
