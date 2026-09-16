-- Settings menu — part of the Lua UI kit.
-- Split out of features/ui.lua so each surface stays small and
-- focused; features/ui.lua is the orchestrator that requires
-- every module and registers the handlers.
--
-- The :settings surface (also Ctrl+, in the GUI) lists every config
-- key with its current value. Rendering it as a Lua float keeps it on
-- top of the modal scrim and above the sidebar, exactly like the other
-- modal surfaces (quick pick, telescope, ...). Native code still owns
-- layout, selection, editing and input; this module is purely visual.
local h = require("jot_ui.helpers")
local close = h.close
local cell_len = h.cell_len
local trunc_cells = h.trunc_cells
local pad_cells = h.pad_cells
local title_with_count = h.title_with_count
local present_panel = h.present_panel

local function settings(p)
  if not p then
    close("settings")
    return true
  end
  local colors = p.colors or {}
  local fg = colors.fg or 7
  local bg = colors.panel_bg or colors.bg or 0
  local border = colors.border or fg
  local selection_fg = colors.selection_fg or 0
  local selection_bg = colors.selection_bg or 6
  local comment = colors.comment or 8
  local accent = colors.accent or 6

  local inner_w = math.max(1, p.w - 2)
  local title = (p.title and p.title ~= "" and " " .. p.title) or " Settings"
  title = title_with_count(title, tostring(p.all_count or 0) .. " keys", inner_w)

  local rows = {}
  rows[1] = { text = string.rep("─", inner_w), fg = border, bg = bg }

  local items = p.items or {}
  local list_h = math.max(0, p.h - 4) -- divider + footer
  local key_w = math.max(18, math.floor(p.w * 0.55))
  local val_w = math.max(1, inner_w - key_w)

  for row = 1, list_h do
    local item = items[row]
    if not item then
      break
    end
    local is_selected = item.selected
    local is_editing = is_selected and item.editing
    local row_fg = is_selected and selection_fg or fg
    local row_bg = is_selected and selection_bg or bg
    -- Nerd Font chevron (nf-fa-chevron-right) marks the selected row.
    local prefix = is_selected and " \u{F054}" or "  "

    local label = trunc_cells(item.label or "", key_w - 3)
    local value = item.value or ""
    if is_editing then
      value = "> " .. (item.edit_input or "")
    end
    value = trunc_cells(value, val_w - 1)

    local gap = math.max(1, inner_w - #prefix - cell_len(label) - cell_len(value))
    local line = prefix .. label .. string.rep(" ", gap) .. value
    local spans = {
      { start = 0, len = 65535, fg = row_fg, bg = row_bg },
    }
    if is_editing then
      -- Editing row: the prompt + typed value pop in the selection colors
      -- while the rest of the row keeps the selection background.
      spans[#spans + 1] = { start = inner_w - cell_len(value), len = #value,
                            fg = selection_fg, bg = selection_bg, bold = true }
    elseif not is_selected then
      -- Value column in the accent color (bools read as on/off there).
      spans[#spans + 1] = { start = inner_w - cell_len(value), len = #value,
                            fg = accent, bg = bg }
    end
    rows[#rows + 1] = { text = pad_cells(line, inner_w), fg = row_fg, bg = row_bg,
                        spans = spans }
  end

  return present_panel("settings", p, rows, { title = title, title_fg = accent })
end


return {
  settings = settings,
}