-- Lua UI kit: renders the editor's modal surfaces (command palette, quick
-- pick, modal popups, save/quit prompts) from Lua instead of C++.
--
-- Native code keeps deciding *when* a surface is open and how keys are
-- routed; this module owns everything visual. Each surface registers through
-- jot.ui.handler(name, fn):
--
--   fn(state)  -> truthy = consumed (native render suppressed)
--   fn(nil)    -> surface closed; tear down the float
--
-- state carries the native layout box (x/y/w/h), the content (query, items,
-- results, lines), and a `colors` table with the active theme. To restyle
-- any of these surfaces, edit the matching module under features/ui/ — no
-- recompile needed.
--
-- The kit is split into small per-surface modules (one file per surface
-- cluster, shared helpers in features/ui/helpers.lua); this file is the
-- orchestrator that pulls them in, registers every handler, and exposes the
-- combined exports. The C++ boot path loads each module into
-- package.loaded["jot_ui.<name>"] before running this file, so require()
-- here always resolves.

local helpers = require("jot_ui.helpers")
local command_palette = require("jot_ui.command_palette")
local quick_pick = require("jot_ui.quick_pick")
local popup = require("jot_ui.popup")
local tree_sitter = require("jot_ui.tree_sitter")
local lsp = require("jot_ui.lsp")
local telescope = require("jot_ui.telescope")
local home = require("jot_ui.home")
local search = require("jot_ui.search")
local statusline = require("jot_ui.statusline")
local sidebar = require("jot_ui.sidebar")
local side_panel = require("jot_ui.side_panel")
local menu = require("jot_ui.menu")
local toast = require("jot_ui.toast")

jot.ui.handler("command_palette", command_palette.command_palette)
jot.ui.handler("quick_pick", quick_pick.quick_pick)
jot.ui.handler("popup", popup.popup)
jot.ui.handler("save_prompt", popup.save_prompt)
jot.ui.handler("quit_prompt", popup.quit_prompt)
jot.ui.handler("tree_sitter_status", tree_sitter.tree_sitter_status)
jot.ui.handler("lsp_status", lsp.lsp_status)
jot.ui.handler("lsp_manager", lsp.lsp_manager)
jot.ui.handler("lsp_signature", lsp.lsp_signature)
jot.ui.handler("telescope", telescope.telescope)
jot.ui.handler("lsp_completion", lsp.lsp_completion)
jot.ui.handler("context_menu", menu.context_menu)
jot.ui.handler("menu_dropdown", menu.menu_dropdown)
jot.ui.handler("search_panel", search.search_panel)
jot.ui.handler("home_screen", home.home_screen)
jot.ui.handler("status_line", statusline.status_line)
jot.ui.handler("sidebar", sidebar.sidebar)
jot.ui.handler("side_panel", side_panel.side_panel)

-- Toasts are self-managing floats (not a per-frame surface); expose the module
-- and bridge it to the native jot.toast show/dismiss/clear API. Both steps are
-- guarded so stub test environments stay quiet.
pcall(function() jot.toast.register(toast) end)
pcall(toast.attach)

-- Exposed for tests / reuse; the loader ignores the return value.
return {
  close = helpers.close,
  present_panel = helpers.present_panel,
  match_spans = helpers.match_spans,
  toast = toast,
  status_line = statusline.status_line,
  sidebar = sidebar.sidebar,
  side_panel = side_panel.side_panel,
  command_palette = command_palette.command_palette,
  quick_pick = quick_pick.quick_pick,
  popup = popup.popup,
  save_prompt = popup.save_prompt,
  quit_prompt = popup.quit_prompt,
  tree_sitter_status = tree_sitter.tree_sitter_status,
  lsp_status = lsp.lsp_status,
  lsp_manager = lsp.lsp_manager,
  lsp_signature = lsp.lsp_signature,
  telescope = telescope.telescope,
  lsp_completion = lsp.lsp_completion,
  context_menu = menu.context_menu,
  menu_dropdown = menu.menu_dropdown,
  search_panel = search.search_panel,
  home_screen = home.home_screen,
}
