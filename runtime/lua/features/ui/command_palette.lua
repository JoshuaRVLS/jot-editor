-- Command Palette — part of the Lua UI kit.
-- Split out of features/ui.lua so each surface stays small and
-- focused; features/ui.lua is the orchestrator that requires
-- every module and registers the handlers.
local h = require("jot_ui.helpers")
local close = h.close
local truncate = h.truncate
local pad = h.pad
local present_panel = h.present_panel
local title_with_count = h.title_with_count
local function command_palette(p)
  if not p then
    close("command_palette")
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
  local count = tostring(#(p.results or {})) .. (#(p.results or {}) == 1 and " result" or " results")
  local title = title_with_count(" Command Palette", count, inner_w)

  local rows = {}
  local query = p.query or ""
  if query == "" or query:sub(1, 1) ~= ":" then
    query = ":" .. query
  end
  rows[1] = { text = pad(truncate(query, math.max(0, inner_w - 1)), inner_w), fg = selection_fg, bg = selection_bg }
  rows[2] = { text = string.rep("─", inner_w), fg = border, bg = bg }

  local selected = math.max(0, p.selected or 0)
  local results = p.results or {}
  local max_items = math.min(8, #results)
  local start_idx = 0
  if #results > 0 then
    start_idx = math.max(0, selected - max_items + 1)
    if start_idx + max_items > #results then
      start_idx = math.max(0, #results - max_items)
    end
  end
  for row = 1, max_items do
    local idx = start_idx + row
    if idx > #results then
      break
    end
    local item = results[idx]
    local is_selected = idx - 1 == selected
    local row_fg = is_selected and selection_fg or fg
    local row_bg = is_selected and selection_bg or bg
    local label = truncate(item.label or "", inner_w - 2)
    -- item.match holds byte offsets into label; the row text is " " .. label,
    -- so spans must be shifted by the 1-byte prefix or the highlight lands on
    -- the wrong character (and can split a multibyte label rune).
    local spans = {}
    for _, m in ipairs(item.match or {}) do
      if m >= 0 and m + 1 <= #label then
        spans[#spans + 1] = { start = m + 1, len = 1, fg = is_selected and selection_fg or accent }
      end
    end
    rows[#rows + 1] = { text = pad(" " .. label, inner_w), fg = row_fg, bg = row_bg, spans = spans }
  end
  if #rows < p.h - 2 then
    rows[#rows + 1] = { text = string.rep(" ", inner_w), fg = fg, bg = bg }
  end

  return present_panel("command_palette", p, rows, { title = title, title_fg = accent })
end


return {
  command_palette = command_palette,
}
