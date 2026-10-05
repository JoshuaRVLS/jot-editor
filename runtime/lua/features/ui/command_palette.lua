-- Command Palette - part of the Lua UI kit.
-- Split out of features/ui.lua so each surface stays small and
-- focused; features/ui.lua is the orchestrator that requires
-- every module and registers the handlers.
--
-- Helix-style: the prompt row is the last row of the screen (the statusline
-- slot), the results grid sits directly above it filled column by column, and
-- the selected row's explanation box floats above the grid. The geometry
-- (columns, widths, scroll window, box rect) is computed natively and pushed
-- with the payload, so this module paints exactly the cells the native mouse
-- hit-test expects.
local h = require("jot_ui.helpers")
local close = h.close
local trunc_cells = h.trunc_cells
local pad_cells = h.pad_cells
local present_panel = h.present_panel

local function command_palette(p)
  if not p then
    close("command_palette")
    close("command_palette_help")
    return true
  end
  local colors = p.colors or {}
  local fg = colors.fg or 7
  -- The suggestion rows sit on the command surface (the Pmenu slot), not on
  -- the buffer background they would otherwise blend into.
  local bg = colors.bg or colors.panel_bg or colors.default_bg or 0
  local comment = colors.comment or 8
  local selection_fg = colors.selection_fg or 0
  local selection_bg = colors.selection_bg or 6
  local accent = colors.accent or 6

  local results = p.results or {}
  local selected = math.max(0, p.selected or 0)
  local cols = math.max(1, p.grid_cols or 1)
  local col_w = math.max(1, p.grid_col_w or p.w or 1)
  local rows_h = math.max(0, p.grid_rows or 0)
  local first = math.max(0, p.grid_first_row or 0)
  local per_col = math.max(1, p.rows_per_col or #results)
  local width = math.max(1, p.w or 1)

  local rows = {}
  for r = 0, rows_h - 1 do
    local actual = first + r
    local line = ""
    local spans = {}
    for c = 0, cols - 1 do
      local idx = c * per_col + actual + 1 -- results[] is Lua 1-based
      if idx <= #results then
        local item = results[idx]
        local is_selected = (idx - 1 == selected)
        local cell_fg = is_selected and selection_fg or fg
        local cell_bg = is_selected and selection_bg or bg
        local label = trunc_cells(item.label or "", math.max(0, col_w - 2))
        local cell = " " .. label
        spans[#spans + 1] = { start = #line, len = #cell, fg = cell_fg, bg = cell_bg }
        local match_fg = is_selected and selection_fg or accent
        for _, m in ipairs(item.match or {}) do
          if m >= 0 and m + 1 <= #label then
            spans[#spans + 1] = { start = #line + m + 1, len = 1, fg = match_fg, bg = cell_bg }
          end
        end
        line = line .. pad_cells(cell, col_w)
      else
        line = line .. string.rep(" ", col_w)
      end
    end
    rows[#rows + 1] = {
      text = pad_cells(line, width),
      fg = fg,
      bg = bg,
      spans = spans,
    }
  end

  -- Nothing to show: the grid keeps a row of headroom and says why it is
  -- empty, exactly like the native renderer, so the prompt never drifts off
  -- the row the caret is placed on.
  if #rows == 0 then
    local query = p.query or ""
    local text = query == "" and "Type a command or search..."
        or ("No matches for \"" .. query .. "\"")
    rows[#rows + 1] = {
      text = pad_cells(trunc_cells(text, math.max(0, width - 4)), width),
      fg = comment,
      bg = bg,
    }
  end

  -- The prompt row is the box's LAST row (the statusline slot), which is where
  -- place_command_palette_cursor puts the caret. Truncation matches the native
  -- caret math (w - 3 cells) so the caret sits exactly after the typed text.
  local query = p.query or ""
  if query == "" or query:sub(1, 1) ~= ":" then
    query = ":" .. query
  end
  rows[#rows + 1] = {
    text = pad_cells(trunc_cells(query, math.max(1, width - 3)), width),
    fg = selection_fg,
    bg = selection_bg,
  }

  -- The grid float starts where the grid does and ends on the prompt row, so
  -- the explanation box above it can be its own frame. strip=true lets the
  -- prompt occupy the statusline row at the very bottom of the screen.
  local main = {
    x = p.x,
    y = p.grid_y or p.y,
    w = width,
    h = #rows,
    colors = p.colors,
  }
  local ok = present_panel("command_palette", main, rows, { border = "none", strip = true })

  -- The explanation box: a bordered float of its own, carrying the selected
  -- row's sentence, aliases and argument shape in the theme's command ink.
  local help_lines = p.help_lines or {}
  if p.help_for and p.help_for >= 0 and #help_lines > 0 then
    local help_rows = {}
    for _, line in ipairs(help_lines) do
      help_rows[#help_rows + 1] = { text = line, fg = p.help_fg or fg, bg = p.help_bg or bg }
    end
    local box = {
      x = p.help_x,
      y = p.help_y,
      w = p.help_w,
      h = p.help_h,
      colors = {
        fg = p.help_fg,
        bg = p.help_bg,
        panel_bg = p.help_bg,
        border = p.help_border_fg,
      },
    }
    present_panel("command_palette_help", box, help_rows, { border = "single" })
  else
    close("command_palette_help")
  end
  return ok
end


return {
  command_palette = command_palette,
}
