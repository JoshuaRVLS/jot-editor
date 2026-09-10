-- Home — part of the Lua UI kit.
-- Split out of features/ui.lua so each surface stays small and
-- focused; features/ui.lua is the orchestrator that requires
-- every module and registers the handlers.
--
-- Unlike the surfaces built on helpers.present_panel (which tear the float
-- down and recreate it on every emit), the home screen owns its float and its
-- scratch buffer and pushes only the lines that changed. Any-motion mouse
-- reporting sends an event per cell crossed, so a pointer hover repaints the
-- menu dozens of times per second; recreating the whole surface per frame
-- burned the frame budget (and, because this module hand-rolled
-- open/create without present_panel's registry, left one float per frame for
-- render_floats() to repaint).
local h = require("jot_ui.helpers")
local cell_len = h.cell_len
local trunc_cells = h.trunc_cells
local pad_cells = h.pad_cells

-- What the float currently shows: the open handles, the frame it was built
-- for (geometry, header text and theme colors -- a change in any of them means
-- the whole surface must be rebuilt) and one row signature per float line.
local state = {
  win = 0,
  buf = 0,
  frame = {},
  lines = {},
}

local function reset_state()
  state.win = 0
  state.buf = 0
  state.frame = {}
  state.lines = {}
end

local function teardown()
  if state.win ~= 0 then
    jot.ui.float.close(state.win, true)
  end
  if state.buf ~= 0 then
    jot.ui.buffer.delete(state.buf)
  end
  reset_state()
end

-- Signature of one model row: any change to it means its screen line has to be
-- recomposed. Fields are joined with separators that cannot appear in a label
-- or path, so re-splitting can never collide.
local function row_sig(r)
  return table.concat({
    r.section and "1" or "0",
    r.selected and "1" or "0",
    tostring(r.x or 0),
    tostring(r.y or 0),
    tostring(r.w or 0),
    r.label or "",
    r.secondary or "",
  }, "\1")
end

local function home_screen(p)
  if not p then
    teardown()
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

  -- Composes one absolute screen row (wordmark/tagline, context line, section
  -- label, or an item with its optional secondary path) into a line padded to
  -- the panel width plus its byte-offset spans. Calls are ordered left to
  -- right, exactly like the one-shot compositor this replaces: a screen row
  -- can carry a row from the action column and one from the recent column.
  local function compose(y)
    local text = ""
    local line_spans = {}
    local function span_at(col, len, fg, bg)
      if len > 0 then
        line_spans[#line_spans + 1] = { start = col, len = len, fg = fg, bg = bg }
      end
    end
    local function put(abs_x, str, fg, bg)
      if str == "" then
        return
      end
      local rel_col = abs_x - panel_x
      local cur = cell_len(text)
      if cur < rel_col then
        text = text .. string.rep(" ", rel_col - cur)
      end
      span_at(#text, #str, fg, bg)
      text = text .. str
    end
    local function fill_row(abs_x)
      local rel = math.max(0, abs_x - panel_x)
      span_at(0, 65535, sel_fg, sel_bg)
      if cell_len(text) < rel then
        text = text .. string.rep(" ", rel - cell_len(text))
      end
    end

    if y == panel_y then
      put(panel_x, p.wordmark or "", accent, default_bg)
      if p.tagline and p.tagline ~= "" then
        put(panel_x + (p.wordmark and cell_len(p.wordmark) or 0) + 1, p.tagline, comment, default_bg)
      end
    elseif y == panel_y + 1 then
      if p.context and p.context ~= "" then
        put(panel_x, trunc_cells(p.context, math.max(1, panel_w - 2)), default_fg, default_bg)
      end
    else
      for _, r in ipairs(rows) do
        if r.y == y then
          local row_w = r.w or panel_w
          if r.section then
            put(r.x, trunc_cells(r.label or "", math.max(1, panel_w)), dir, default_bg)
          elseif r.selected then
            fill_row(r.x)
            put(r.x + 1, r.label or "", sel_fg, sel_bg)
            if r.secondary and r.secondary ~= "" then
              put(r.x + row_w - cell_len(r.secondary) - 1,
                  trunc_cells(r.secondary, math.max(1, math.floor(row_w / 2))),
                  sel_fg,
                  sel_bg)
            end
          else
            put(r.x + 1, r.label or "", default_fg, default_bg)
            if r.secondary and r.secondary ~= "" then
              put(r.x + row_w - cell_len(r.secondary) - 1,
                  trunc_cells(r.secondary, math.max(1, math.floor(row_w / 2))),
                  comment,
                  default_bg)
            end
          end
        end
      end
    end
    return pad_cells(text, panel_w), line_spans
  end

  -- Everything the float was built with that is not covered by the per-line
  -- row signatures below. A change in any field forces a full rebuild.
  local frame = {
    x = panel_x,
    y = panel_y,
    w = panel_w,
    h = panel_h,
    wordmark = p.wordmark or "",
    tagline = p.tagline or "",
    context = p.context or "",
    fg = default_fg,
    bg = default_bg,
    comment = comment,
    accent = accent,
    dir = dir,
    sel_fg = sel_fg,
    sel_bg = sel_bg,
  }

  -- Row signatures bucketed per screen line for this emit.
  local sigs = {}
  for _, r in ipairs(rows) do
    local y = r.y
    if y and y >= panel_y and y < panel_y + panel_h then
      local i = y - panel_y + 1
      sigs[i] = (sigs[i] or "") .. row_sig(r) .. "\2"
    end
  end

  -- Rebuild the whole surface only when the float is gone or its frame
  -- changed (resize, theme switch); otherwise the diff below repaints just the
  -- lines whose rows moved.
  local can_reuse = state.buf ~= 0 and state.win ~= 0 and jot.ui.float.is_valid(state.win)
  if can_reuse then
    for k, v in pairs(frame) do
      if state.frame[k] ~= v then
        can_reuse = false
        break
      end
    end
  end

  if not can_reuse then
    teardown()
    local body, spans_by_line = {}, {}
    for i = 1, panel_h do
      local text, line_spans = compose(panel_y + i - 1)
      body[i] = text
      spans_by_line[i] = line_spans
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
    state.win = win
    state.buf = buf
    state.frame = frame
    state.lines = sigs
    for i = 1, panel_h do
      if #spans_by_line[i] > 0 then
        jot.ui.float.set_spans(win, i, spans_by_line[i])
      end
    end
    return true
  end

  for i = 1, panel_h do
    if sigs[i] ~= state.lines[i] then
      local text, line_spans = compose(panel_y + i - 1)
      jot.ui.buffer.set_lines(state.buf, i - 1, i, true, { text })
      jot.ui.float.set_spans(state.win, i, line_spans)
      state.lines[i] = sigs[i]
    end
  end
  return true
end

return {
  home_screen = home_screen,
}
