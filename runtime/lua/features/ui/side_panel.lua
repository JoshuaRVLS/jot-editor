-- Side Panel — part of the Lua UI kit.
-- Split out of features/ui.lua so each surface stays small and
-- focused; features/ui.lua is the orchestrator that requires
-- every module and registers the handlers.
local h = require("jot_ui.helpers")
local close = h.close
local cell_len = h.cell_len
local trunc_cells = h.trunc_cells
local pad = h.pad
local present_panel = h.present_panel
local function side_panel(p)
  if not p then
    close("side_panel")
    return true
  end
  local colors = p.colors or {}
  local fg = colors.fg or 7
  local bg = colors.panel_bg or colors.bg or 0
  local comment = colors.comment or 8
  local accent = colors.accent or colors.fg or 7
  local selection_fg = colors.selection_fg or 0
  local selection_bg = colors.selection_bg or 6

  local inner_w = math.max(1, (p.w or 2) - 2)
  local inner_h = math.max(1, (p.h or 2) - 2)
  local rows = {}
  local function add(text, f, b, bold)
    if #rows >= inner_h then
      return
    end
    local t = text or ""
    rows[#rows + 1] = {
      text = trunc_cells(t, inner_w),
      fg = f,
      bg = b,
      spans = { { start = 0, len = 65535, fg = f, bg = b, bold = bold } },
    }
  end

  -- Debugger session tabs on the first interior row.
  if p.tabs and #p.tabs > 0 then
    local line, spans, col = "", {}, 0
    local active_fg = colors.fg_terminal_tab_focused or accent
    local active_bg = colors.bg_terminal_tab_focused or selection_bg
    local inactive_fg = colors.fg_terminal_tab_inactive or comment
    local inactive_bg = colors.bg_terminal_tab_inactive or bg
    for _, tab in ipairs(p.tabs) do
      local label = trunc_cells(tab.label or "", math.max(1, inner_w - col))
      local at = #line
      line = line .. label
      spans[#spans + 1] = { start = at, len = #label, fg = tab.active and active_fg or inactive_fg,
                            bg = tab.active and active_bg or inactive_bg, bold = tab.active }
      col = col + cell_len(label) + 1
      if col >= inner_w then
        break
      end
    end
    rows[#rows + 1] = { text = trunc_cells(line, inner_w), fg = fg, bg = bg,
                        spans = { { start = 0, len = 65535, fg = fg, bg = bg } } }
    for _, sp in ipairs(spans) do
      rows[#rows].spans[#rows[#rows].spans + 1] = sp
    end
  end

  -- Header row (path + counts / file + symbol count), bold.
  if p.header and p.header ~= "" then
    add(p.header, p.header_fg or 6, bg, true)
  end
  -- Empty-state note.
  if p.note and p.note ~= "" then
    add(p.note, p.note_fg or comment, bg, false)
  end

  -- Content rows: selection rows get the selection bar; outline rows carry a
  -- right-aligned line number detail.
  local line_w = math.min(math.floor(inner_w / 4), 7)
  for _, r in ipairs(p.rows or {}) do
    if #rows >= inner_h then
      break
    end
    local sel = r.selected
    local f = sel and selection_fg or (r.fg or fg)
    local b = sel and selection_bg or (r.bg or bg)
    local text = r.text or ""
    local detail = r.detail or ""
    if detail ~= "" then
      local name_w = math.max(1, inner_w - line_w)
      local name = trunc_cells(text, name_w)
      local pad = math.max(0, name_w - cell_len(name))
      local line = name .. string.rep(" ", pad) .. trunc_cells(detail, line_w)
      rows[#rows + 1] = {
        text = line,
        fg = f,
        bg = b,
        spans = {
          { start = 0, len = 65535, fg = f, bg = b, bold = sel },
          { start = 0, len = #name, fg = f, bg = b, bold = sel },
          { start = #name + pad, len = #line - #name - pad, fg = sel and selection_fg or comment,
            bg = b, bold = false },
        },
      }
    else
      rows[#rows + 1] = {
        text = trunc_cells(text, inner_w),
        fg = f,
        bg = b,
        spans = { { start = 0, len = 65535, fg = f, bg = b, bold = r.bold } },
      }
    end
  end

  -- Debugger error line (native draws it over the bottom border; here it
  -- becomes the last content row in error colors).
  if p.error and p.error ~= "" and #rows < inner_h then
    add(p.error, colors.error or 15, colors.status_error_bg or 1, true)
  end

  return present_panel("side_panel",
                       p,
                       rows,
                       {
                         border = "single",
                         title = p.title or nil,
                         title_fg = accent,
                       })
end


return {
  side_panel = side_panel,
}
