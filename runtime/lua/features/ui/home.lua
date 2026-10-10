-- Home - part of the Lua UI kit.
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
--
-- The header is the big wordmark: three rows of block letters, one accent per
-- letter, then the tagline row (with the key legend at its right edge) and the
-- context line. The native model owns that shape (src/surfaces/home.cpp
-- spends kHomeHeaderRows rows on it before the first section title); a payload
-- without wordmark_art falls back to the old one-line wordmark, so a bare
-- caller still gets a usable surface.
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
    tostring(r.icon_fg or -1),
    tostring(r.fg or -1),
    r.icon or "",
    r.label or "",
    r.secondary or "",
    r.key or "",
  }, "\1")
end

-- Signature of the wordmark art: one part per row, one entry per letter.
local function art_sig(art)
  local rows = {}
  for i = 1, #art do
    rows[i] = table.concat(art[i], "\1")
  end
  return table.concat(rows, "\2")
end

-- "Filter  <query>" wraps the query in the screen's own label. The prefix is
-- set off in the accent color so the line reads as state, not as text.
local FILTER_PREFIX = "Filter"
local CONTEXT_PREFIX = "Here"

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
  local border = colors.border or comment
  local sel_fg = colors.sidebar_sel_fg or colors.selection_fg or 0
  local sel_bg = colors.sidebar_sel_bg or colors.selection_bg or 6
  local key_fg = colors.accent or comment

  local panel_x = p.panel_x or 0
  local panel_y = p.panel_y or 0
  local panel_w = math.max(1, p.panel_w or 1)
  local panel_h = math.max(1, p.panel_h or 1)
  local rows = p.rows or {}
  local art = p.wordmark_art or {}
  local has_art = #art > 0
  -- The header rows follow the art; with no art the wordmark and its tagline
  -- share one line, exactly like the surface before the big mark.
  local tagline_y = has_art and (panel_y + #art) or panel_y
  local context_y = tagline_y + 1
  -- The bottom strip: the verse of the day centered over its reference, with the
  -- footer on the panel's last row. The model leaves these rows to the lists, so
  -- nothing else composes into them.
  local quote_lines = p.quote_lines or {}
  local quote_ref = p.quote_ref or ""
  local last_updated = p.last_updated or ""
  local developed_by = p.developed_by or ""
  local footer_y = nil
  if last_updated ~= "" or developed_by ~= "" then
    footer_y = panel_y + panel_h - 1
  end
  local strip_below = footer_y and (footer_y - 1) or (panel_y + panel_h - 1)
  local ref_y = nil
  if quote_ref ~= "" then
    ref_y = strip_below
    strip_below = strip_below - 1
  end
  local quote_first, quote_last = nil, nil
  if #quote_lines > 0 then
    quote_last = strip_below
    quote_first = strip_below - #quote_lines + 1
  end
  -- One accent per wordmark letter. The mark spells the editor's initials, so
  -- the letters read best in three different syntax slots instead of one flat
  -- color; a theme that leaves one unset falls back to the accent.
  local letter_fg = {
    colors.keyword or accent,
    colors["function"] or accent, -- `function` is a Lua keyword, so index it
    colors.type or accent,
  }

  -- Composes one absolute screen row (the big wordmark, the tagline row, the
  -- context line, a section label, or an item with its optional secondary
  -- path) into a line padded to the panel width plus its byte-offset spans.
  -- Calls are ordered left to right, exactly like the one-shot compositor this
  -- replaces: a screen row can carry a row from the action column and one from
  -- the recent column.
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
    -- The selection band hugs the label rather than the whole row: the panel is
    -- much wider than the names it lists, and a bar run edge to edge reads as a
    -- rule across the screen instead of a highlight on the row.
    local function select_label(abs_x, label)
      local rel = math.max(0, abs_x - panel_x)
      if cell_len(text) < rel then
        text = text .. string.rep(" ", rel - cell_len(text))
      end
      span_at(#text, #label, sel_fg, sel_bg)
      text = text .. label
    end

    if footer_y and y == footer_y then
      -- The footer: the build's date at the left, the credit at the right.
      if last_updated ~= "" then
        put(panel_x + 2, last_updated, comment, default_bg)
      end
      if developed_by ~= "" then
        put(panel_x + panel_w - cell_len(developed_by) - 2, developed_by, comment, default_bg)
      end
    elseif ref_y and y == ref_y then
      put(panel_x + math.max(0, math.floor((panel_w - cell_len(quote_ref)) / 2)),
          quote_ref,
          accent,
          default_bg)
    elseif quote_first and y >= quote_first and y <= quote_last then
      local line = quote_lines[y - quote_first + 1]
      put(panel_x + math.max(0, math.floor((panel_w - cell_len(line)) / 2)),
          line,
          comment,
          default_bg)
    elseif has_art and y >= panel_y and y < panel_y + #art then
      -- One row of the big wordmark: every letter is its own span, so the
      -- accents meet inside the row and the gaps stay on the panel background.
      local letters = art[y - panel_y + 1]
      local x = panel_x
      for i = 1, #letters do
        local letter = letters[i]
        if letter ~= "" then
          put(x, letter, letter_fg[i] or accent, default_bg)
        end
        x = x + cell_len(letter) + 1
      end
    elseif y == panel_y and not has_art then
      put(panel_x, p.wordmark or "", accent, default_bg)
      if p.tagline and p.tagline ~= "" then
        put(panel_x + (p.wordmark and cell_len(p.wordmark) or 0) + 1, p.tagline, comment, default_bg)
      end
      -- Key legend pinned to the right edge of the wordmark row, only when it
      -- still leaves the wordmark and tagline room.
      if p.hint and p.hint ~= "" then
        local hint_x = panel_x + panel_w - cell_len(p.hint) - 1
        if hint_x > panel_x + 26 then
          put(hint_x, p.hint, comment, default_bg)
        end
      end
    elseif y == tagline_y then
      -- The tagline row: a code glyph, the words, and the key legend pinned to
      -- the right edge only when it still leaves the tagline room.
      if p.tagline and p.tagline ~= "" then
        put(panel_x, "\u{F121}", accent, default_bg)
        put(panel_x + 2, p.tagline, comment, default_bg)
      end
      if p.hint and p.hint ~= "" then
        local hint_x = panel_x + panel_w - cell_len(p.hint) - 1
        if hint_x > panel_x + 26 then
          put(hint_x, p.hint, comment, default_bg)
        end
      end
    elseif y == context_y then
      local filtering = p.filter ~= nil and p.filter ~= ""
      local line = filtering and p.filter or p.context
      if line and line ~= "" then
        if filtering and string.sub(line, 1, #FILTER_PREFIX) == FILTER_PREFIX then
          put(panel_x, FILTER_PREFIX, accent, default_bg)
          put(panel_x + #FILTER_PREFIX, string.sub(line, #FILTER_PREFIX + 1), default_fg, default_bg)
        else
          local x = panel_x
          local icon = p.context_icon or ""
          if icon ~= "" then
            put(x, icon, accent, default_bg)
            x = x + cell_len(icon) + 1
          end
          if string.sub(line, 1, #CONTEXT_PREFIX) == CONTEXT_PREFIX then
            put(x, CONTEXT_PREFIX, default_fg, default_bg)
            local rest = trunc_cells(string.sub(line, #CONTEXT_PREFIX + 1),
                                     math.max(1, panel_w - (x - panel_x) - #CONTEXT_PREFIX))
            put(x + #CONTEXT_PREFIX, rest, comment, default_bg)
          else
            put(x, trunc_cells(line, math.max(1, panel_w - (x - panel_x))), default_fg, default_bg)
          end
        end
      end
    else
      for _, r in ipairs(rows) do
        if r.y == y then
          local row_w = r.w or panel_w
          local key_w = (r.key and r.key ~= "") and (cell_len(r.key) + 2) or 0
          if r.section then
            -- Section titles carry their own glyph and accent, so the eye
            -- finds the lists without reading their names.
            local sx = r.x
            if r.icon and r.icon ~= "" then
              put(sx, r.icon, r.icon_fg or dir, default_bg)
              sx = sx + cell_len(r.icon) + 1
            end
            put(sx,
                trunc_cells(r.label or "", math.max(1, row_w - (sx - r.x))),
                r.fg or dir,
                default_bg)
          elseif r.selected then
            local lx = r.x + 1
            if r.icon and r.icon ~= "" then
              -- The band covers the glyph too, but the glyph keeps its own
              -- color: the row is highlighted, not repainted.
              put(lx, r.icon .. " ", r.icon_fg or sel_fg, sel_bg)
              lx = lx + cell_len(r.icon) + 1
            end
            select_label(lx, r.label or "")
            if r.secondary and r.secondary ~= "" then
              put(r.x + row_w - cell_len(r.secondary) - 1 - key_w,
                  trunc_cells(r.secondary, math.max(1, math.floor(row_w / 2))),
                  comment,
                  default_bg)
            end
            if r.key and r.key ~= "" then
              put(r.x + row_w - cell_len(r.key) - 1, r.key, key_fg, default_bg)
            end
          else
            local lx = r.x + 1
            if r.icon and r.icon ~= "" then
              put(lx, r.icon, r.icon_fg or r.fg or default_fg, default_bg)
              lx = lx + cell_len(r.icon) + 1
            end
            put(lx, r.label or "", r.fg or default_fg, default_bg)
            if r.secondary and r.secondary ~= "" then
              put(r.x + row_w - cell_len(r.secondary) - 1 - key_w,
                  trunc_cells(r.secondary, math.max(1, math.floor(row_w / 2))),
                  comment,
                  default_bg)
            end
            if r.key and r.key ~= "" then
              put(r.x + row_w - cell_len(r.key) - 1, r.key, key_fg, default_bg)
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
    art = art_sig(art),
    tagline = p.tagline or "",
    context = p.context or "",
    context_icon = p.context_icon or "",
    filter = p.filter or "",
    hint = p.hint or "",
    quote = table.concat(quote_lines, "\2"),
    quote_ref = quote_ref,
    last_updated = last_updated,
    developed_by = developed_by,
    fg = default_fg,
    bg = default_bg,
    comment = comment,
    accent = accent,
    dir = dir,
    border = border,
    key = key_fg,
    letter_j = letter_fg[1],
    letter_o = letter_fg[2],
    letter_t = letter_fg[3],
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
