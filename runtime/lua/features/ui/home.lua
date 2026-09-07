-- Home — part of the Lua UI kit.
-- Split out of features/ui.lua so each surface stays small and
-- focused; features/ui.lua is the orchestrator that requires
-- every module and registers the handlers.
local h = require("jot_ui.helpers")
local close = h.close
local cell_len = h.cell_len
local trunc_cells = h.trunc_cells
local pad_cells = h.pad_cells
local function home_screen(p)
  if not p then
    close("home_screen")
    return true
  end
  local colors = p.colors or {}
  local default_fg = colors.default_fg or colors.fg or 7
  local default_bg = colors.default_bg or colors.bg or 0
  local comment = colors.comment or 8
  local accent = colors.accent or colors.fg or 7
  local dir = colors.sidebar_dir or comment
  local sel_fg = colors.sidebar_sel_fg or colors.selection_fg or 0
  local sel_bg = colors.sidebar_sel_bg or colors.selection_bg or 6

  local panel_x = p.panel_x or 0
  local panel_y = p.panel_y or 0
  local panel_w = math.max(1, p.panel_w or 1)
  local panel_h = math.max(1, p.panel_h or 1)
  local rows = p.rows or {}

  close("home_screen")
  local body = {}
  local spans = {}
  for i = 1, panel_h do
    body[i] = ""
  end
  local function span_at(line, col, len, fg, bg)
    if line < 1 or line > panel_h or len <= 0 then
      return
    end
    spans[line] = spans[line] or {}
    spans[line][#spans[line] + 1] = { start = col, len = len, fg = fg, bg = bg }
  end
  -- Places text at an absolute screen column on an absolute screen row,
  -- converting to float-relative cells and tracking byte offsets.
  local function put(abs_x, abs_y, text, fg, bg)
    local line = abs_y - panel_y + 1
    if line < 1 or line > panel_h or text == "" then
      return
    end
    local rel_col = abs_x - panel_x
    local cur = cell_len(body[line])
    if cur < rel_col then
      body[line] = body[line] .. string.rep(" ", rel_col - cur)
    end
    local start = #body[line]
    span_at(line, start, #text, fg, bg)
    body[line] = body[line] .. text
  end
  local function fill_row(abs_x, abs_y, w)
    local line = abs_y - panel_y + 1
    if line < 1 or line > panel_h then
      return
    end
    local rel = math.max(0, abs_x - panel_x)
    body[line] = body[line] or ""
    span_at(line, 0, 65535, sel_fg, sel_bg)
    if cell_len(body[line]) < rel then
      body[line] = body[line] .. string.rep(" ", rel - cell_len(body[line]))
    end
  end

  -- Header: wordmark + tagline, then the context line.
  put(panel_x, panel_y, p.wordmark or "", accent, default_bg)
  if p.tagline and p.tagline ~= "" then
    put(panel_x + (p.wordmark and cell_len(p.wordmark) or 0) + 1, panel_y, p.tagline, comment, default_bg)
  end
  if p.context and p.context ~= "" then
    put(panel_x, panel_y + 1, trunc_cells(p.context, math.max(1, panel_w - 2)), default_fg, default_bg)
  end

  -- Sections and item rows (absolute rects come from the native layout so
  -- mouse hover / clicks keep hitting the same rows).
  for _, r in ipairs(rows) do
    if r.y and r.y >= panel_y and r.y < panel_y + panel_h then
      if r.section then
        put(r.x, r.y, trunc_cells(r.label or "", math.max(1, panel_w)), dir, default_bg)
      elseif r.selected then
        fill_row(r.x, r.y, r.w or panel_w)
        put(r.x + 1, r.y, r.label or "", sel_fg, sel_bg)
        if r.secondary and r.secondary ~= "" then
          put(r.x + (r.w or panel_w) - cell_len(r.secondary) - 1,
              r.y,
              trunc_cells(r.secondary, math.max(1, math.floor((r.w or panel_w) / 2))),
              sel_fg,
              sel_bg)
        end
      else
        put(r.x + 1, r.y, r.label or "", default_fg, default_bg)
        if r.secondary and r.secondary ~= "" then
          put(r.x + (r.w or panel_w) - cell_len(r.secondary) - 1,
              r.y,
              trunc_cells(r.secondary, math.max(1, math.floor((r.w or panel_w) / 2))),
              comment,
              default_bg)
        end
      end
    end
  end
  for i = 1, panel_h do
    body[i] = pad_cells(body[i], panel_w)
  end

  local buf = jot.ui.buffer.create(false, true)
  jot.ui.buffer.set_lines(buf, 0, -1, true, body)
  local win = jot.ui.float.open(buf, {
    col = panel_x,
    row = panel_y,
    width = panel_w,
    height = panel_h,
    relative = "editor",
    anchor = "NW",
    border = "none",
    focusable = false,
    mouse = false,
    hide = false,
    fg = default_fg,
    bg = default_bg,
  })
  if not win or win == 0 then
    jot.ui.buffer.delete(buf)
    return false
  end
  for i = 1, panel_h do
    if spans[i] then
      jot.ui.float.set_spans(win, i, spans[i])
    end
  end
  return true
end


return {
  home_screen = home_screen,
}
