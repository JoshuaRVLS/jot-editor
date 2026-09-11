-- Command Palette — part of the Lua UI kit.
-- Split out of features/ui.lua so each surface stays small and
-- focused; features/ui.lua is the orchestrator that requires
-- every module and registers the handlers.
--
-- Integrated into the statusline like Neovim's cmdline: no popup box, no
-- border, no title. The match list renders on the plain background and the
-- prompt row is the LAST row -- the very bottom of the screen, in the
-- statusline slot (the palette paints over the statusline while open).
local h = require("jot_ui.helpers")
local close = h.close
local truncate = h.truncate
local pad = h.pad
local present_panel = h.present_panel
local function command_palette(p)
  if not p then
    close("command_palette")
    return true
  end
  local colors = p.colors or {}
  local fg = colors.fg or 7
  local bg = colors.panel_bg or colors.bg or 0
  local selection_fg = colors.selection_fg or 0
  local selection_bg = colors.selection_bg or 6
  local accent = colors.accent or 6

  local inner_w = math.max(1, p.w)
  local rows = {}
  local selected = math.max(0, p.selected or 0)
  local results = p.results or {}
  -- The prompt row is the box's LAST row (the statusline slot), which is where
  -- place_command_palette_cursor puts the caret. Reserve that row and clamp the
  -- list to what is left, or a short window would fill every row with matches
  -- and drop the prompt off the bottom.
  local box_h = math.max(1, p.h or 1)
  local list_h = box_h - 1
  local max_items = math.min(8, #results, list_h)
  local start_idx = 0
  if #results > 0 and max_items > 0 then
    start_idx = math.max(0, selected - max_items + 1)
    if start_idx + max_items > #results then
      start_idx = math.max(0, #results - max_items)
    end
  end
  -- Match rows first: the list floats above the cmdline prompt.
  for row = 1, max_items do
    local idx = start_idx + row
    if idx > #results then
      break
    end
    local item = results[idx]
    local is_selected = idx - 1 == selected
    local row_fg = is_selected and selection_fg or fg
    local row_bg = is_selected and selection_bg or bg
    local label = truncate(item.label or "", math.max(1, inner_w - 2))
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
  -- Blank headroom so the prompt still lands on the last row when there are
  -- fewer matches than the box is tall. The box keeps one row of headroom even
  -- with no matches at all (the native renderer puts its "No matches" line
  -- there), and without this the prompt would be drawn on that first row while
  -- the caret stayed on the last one.
  while #rows < list_h do
    rows[#rows + 1] = { text = pad("", inner_w), fg = fg, bg = bg }
  end
  -- The cmdline prompt row as the LAST row -- the statusline slot at the
  -- very bottom of the screen. Truncation matches the native caret math
  -- (layout.w - 3 cells) so the caret sits exactly after the typed text.
  local query = p.query or ""
  if query == "" or query:sub(1, 1) ~= ":" then
    query = ":" .. query
  end
  rows[#rows + 1] = {
    text = pad(truncate(query, math.max(1, inner_w - 3)), inner_w),
    fg = selection_fg,
    bg = selection_bg,
  }

  -- strip=true lets the float occupy the statusline rows, so the prompt
  -- row sits at the very bottom of the screen where the caret is placed.
  return present_panel("command_palette", p, rows, { border = "none", strip = true })
end


return {
  command_palette = command_palette,
}