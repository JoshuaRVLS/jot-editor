-- Menu — part of the Lua UI kit.
-- Split out of features/ui.lua so each surface stays small and
-- focused; features/ui.lua is the orchestrator that requires
-- every module and registers the handlers.
local h = require("jot_ui.helpers")
local close = h.close
local trunc_cells = h.trunc_cells
local present_panel = h.present_panel
local function menu_rows(p, list, selected)
  -- Renders an item list as a bordered panel: enabled rows are selectable,
  -- disabled rows are dimmed and skip the selection bar.
  local colors = p.colors or {}
  local fg = colors.fg or 7
  local bg = colors.panel_bg or colors.bg or 0
  local selection_fg = colors.selection_fg or 0
  local selection_bg = colors.selection_bg or 6
  local comment = colors.comment or 8
  local inner_w = math.max(1, p.w - 2)
  local rows = {}
  for i, item in ipairs(list) do
    local enabled = item.enabled ~= false
    local sel = enabled and (i - 1) == (selected or 0)
    local label = trunc_cells(item.label or "", math.max(1, inner_w - 4))
    rows[#rows + 1] = {
      text = " " .. label,
      fg = sel and selection_fg or (enabled and fg or comment),
      bg = sel and selection_bg or bg,
    }
  end
  return rows
end

local function context_menu(p)
  if not p then
    close("context_menu")
    return true
  end
  local rows = menu_rows(p, p.items or {}, p.selected)
  return present_panel("context_menu", p, rows, {})
end

-- ---------------------------------------------------------------------------
-- Menu bar dropdown
-- ---------------------------------------------------------------------------

local function menu_dropdown(p)
  if not p then
    close("menu_dropdown")
    return true
  end
  local colors = p.colors or {}
  local accent = colors.accent or colors.fg or 7
  local rows = menu_rows(p, p.items or {}, p.selected)
  local label = (p.menu_label or "") ~= "" and (" " .. p.menu_label .. " ") or nil
  return present_panel("menu_dropdown",
                       p,
                       rows,
                       { border = "single", title = label, title_fg = accent })
end


return {
  menu_rows = menu_rows,
  context_menu = context_menu,
  menu_dropdown = menu_dropdown,
}
