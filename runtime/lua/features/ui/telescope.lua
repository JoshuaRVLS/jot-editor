-- Telescope - part of the Lua UI kit.
-- Split out of features/ui.lua so each surface stays small and
-- focused; features/ui.lua is the orchestrator that requires
-- every module and registers the handlers.
--
-- The picker is one flat surface with two straight-bordered boxes in it:
--
--   ┌────────  Results  ─────────┐ ┌────────  Preview  ─────────┐
--   │   src/render/frame.cpp     │ │    1 // frame paint        │
--   │ > src/input/keys.cpp       │ │    2 int paint_frame(...)  │
--   │   notes.txt                │ │                            │
--   │ > autosave     12 / 48     │ │                            │
--   └─────  Find Files · src  ───┘ └────  240 lines  8.1 KB  ───┘
--
-- * one background inside each border and none outside one -- the editor's
--   (TelescopeNormal supplies ink only, so the frames sit on the buffer's own
--   ground): the panes are told apart by their frames, and the column between
--   them keeps the float's own colour -- the picker's fill stops where its
--   border does;
-- * a row is the file's path from the picker's scope -- the name alone cannot
--   tell src/render/frame.cpp from src/input/frame.cpp -- after its file icon;
-- * the selection is a `>` pointer in the rows' gutter plus the row's band:
--   the two cells are reserved on every row, so the names never shift;
-- * the prompt is the list box's last interior row, with `N / M` at its right
--   end, so what was typed sits under what it filtered;
-- * the labels are centred in the borders: the pane's name on top, and the
--   finder plus its folder scope at the foot.
--
-- The float covers both boxes and carries no border of its own: each box draws
-- its frame as text, which is exactly what keeps the two borders separate.
-- Rows are composed left to right, one screen row at a time, so every span's
-- byte offset is final by the time it is recorded.
local h = require("jot_ui.helpers")
local close = h.close
local cell_len = h.cell_len
local take_cells = h.take_cells
local trunc_cells = h.trunc_cells
local pad_cells = h.pad_cells
local present_panel = h.present_panel

local BOX = { tl = "┌", tr = "┐", bl = "└", br = "┘", hz = "─", vt = "│" }
-- The prompt's marker and the row gutter's pointer. ASCII on purpose: a Nerd
-- Font chevron only renders on a patched font, and the picker's rows are the
-- one place a patchy font costs the most.
local PROMPT = "> "
local POINTER = "> "
local GUTTER = "  "
local ELLIPSIS = "…"

-- Keeps the tail of a path within `w` cells, prefixed with an ellipsis, and
-- returns that text plus the byte index into `s` it starts at. The index is what
-- translates the query highlights -- whose offsets index the original label --
-- onto what is actually drawn.
local function tail_keep(s, w)
  if w <= 0 or s == "" then
    return 1, ""
  end
  if cell_len(s) <= w then
    return 1, s
  end
  if w <= 1 then
    return #s + 1, ELLIPSIS
  end
  local starts = {}
  for pos in utf8.codes(s) do
    starts[#starts + 1] = pos
  end
  local keep = math.min(#starts, w - 1)
  local from = starts[#starts - keep + 1]
  return from, ELLIPSIS .. s:sub(from)
end

local function telescope(p)
  if not p then
    close("telescope")
    return true
  end
  local colors = p.colors or {}
  local t_fg = colors.t_fg or colors.fg or 7
  -- The picker is drawn on the editor's own background rather than on a plate
  -- of its own, so its frames sit on the same ground as the buffer behind the
  -- float and the border stays a line on the text. `t_bg` is the theme's
  -- TelescopeNormal slot, which now supplies ink only.
  local t_bg = colors.default_bg or colors.t_bg or colors.bg or 0
  local t_sel_fg = colors.t_sel_fg or colors.selection_fg or 0
  local t_sel_bg = colors.t_sel_bg or colors.selection_bg or 6
  local t_prev_fg = colors.t_prev_fg or t_fg
  local border = colors.border or t_fg
  local comment = colors.comment or 8
  local accent = colors.accent or 6
  local line_nr = colors.line_nr or comment

  local height = math.max(2, p.h or 2)
  local list_w = math.max(1, p.list_w or 1)  -- list interior (cells)
  local box_w = list_w + 2                   -- list box, border included
  local view_w = math.max(0, p.preview_w or 0)
  local view_col = (p.preview_x or 0) - (p.x or 0)
  local show_view = p.show_preview and view_w >= 6 and view_col > box_w
  local list_col = (p.list_x or 0) - (p.x or 0)
  local query_col = (p.query_x or 0) - (p.x or 0)

  -- Absolute screen row -> row inside the float. The float is borderless, so
  -- row 1 is p.y.
  local function row_index(absolute_y)
    return absolute_y - (p.y or 0) + 1
  end

  local rows = {}
  local function row(i)
    local existing = rows[i]
    if existing then
      return existing
    end
    -- Each box paints the background across the row -- border included -- so
    -- nothing of the picker's fill shows outside a border: the column between
    -- the two boxes keeps the float's own fill, the way two separate windows
    -- would. A band per box rather than one span across the float, because a
    -- span that wide would also be the span the selection band repaints -- and
    -- the selection band is the list's interior, not the screen.
    local fresh = { text = "", spans = {}, bands = {} }
    fresh.bands[1] = { col = 0, cells = box_w, bg = t_bg }
    if show_view then
      fresh.bands[2] = { col = view_col, cells = view_w, bg = t_bg }
    end
    rows[i] = fresh
    return fresh
  end

  -- Appends `text` at cell column `col`, recording its span, and returns the
  -- byte offset it landed at. nil fg/bg leave those attributes to the picker's
  -- background span, so only the selection band has to name its own bg.
  local function put(rw, col, text, fg, bg)
    if col < 0 or text == "" then
      return -1
    end
    local cur = cell_len(rw.text)
    if cur < col then
      rw.text = rw.text .. string.rep(" ", col - cur)
    end
    local start = #rw.text
    rw.spans[#rw.spans + 1] = { start = start, len = #text, fg = fg, bg = bg }
    rw.text = rw.text .. text
    return start
  end

  -- Records a background band over `cells` cells starting at `col`. A band is a
  -- span, not a write: `put` only ever appends, so reserving the band's cells up
  -- front would push every glyph after it to the end of the row.
  local function band(rw, col, cells, bg)
    if cells > 0 and bg ~= nil and col >= 0 then
      rw.bands[#rw.bands + 1] = { col = col, cells = cells, bg = bg }
    end
  end

  -- Applies a row's bands. Called once the row's text is complete, because the
  -- band's byte range has to be measured against the finished line (and the
  -- spans it covers have to take its background: a span with no bg is painted on
  -- the picker's own background, which would punch holes in the band). Returns
  -- the band spans to append -- appending them here would run after the glyph
  -- spans they sit under, and a stable sort by start would then draw them on
  -- top.
  local function apply_bands(rw)
    local extra = {}
    for _, b in ipairs(rw.bands) do
      local from = #take_cells(rw.text, b.col)
      local to = from + #take_cells(rw.text:sub(from + 1), b.cells)
      if to > from then
        for _, sp in ipairs(rw.spans) do
          if sp.start < to and (sp.start + sp.len) > from then
            sp.bg = b.bg
          end
        end
        extra[#extra + 1] = { start = from, len = to - from, bg = b.bg }
      end
    end
    return extra
  end

  -- Everything below writes strictly left to right: `put` appends, so a cell
  -- drawn out of order would land past the end of the row instead of in it.

  -- A box row is written in three ordered pieces -- left cap, contents, right
  -- cap -- because `put` only appends: the right cap is the *last* cell of the
  -- row, so drawing it before the contents pushes them past the box.
  local function frame_cap_left(rw, col, glyph)
    put(rw, col, glyph, border)
  end

  local function frame_cap_right(rw, col, width, glyph)
    put(rw, col + width - 1, glyph, border)
  end

  -- The middle of a border row: the rule with its label centred in it, as in
  -- `────  Results  ────`.
  local function frame_rule(rw, col, width, title, title_fg)
    local span = math.max(0, width - 2)
    local text = (title and title ~= "") and (" " .. title .. " ") or ""
    if cell_len(text) > span then
      text = trunc_cells(text, span)
    end
    local text_w = cell_len(text)
    local lead = math.max(0, math.floor((span - text_w) / 2))
    if lead > 0 then
      put(rw, col + 1, string.rep(BOX.hz, lead), border)
    end
    if text_w > 0 then
      put(rw, col + 1 + lead, text, title_fg or t_fg)
    end
    local tail = span - lead - text_w
    if tail > 0 then
      put(rw, col + 1 + lead + text_w, string.rep(BOX.hz, tail), border)
    end
  end

  local function cap_glyph(is_top, is_bottom, left)
    if is_top then
      return left and BOX.tl or BOX.tr
    end
    if is_bottom then
      return left and BOX.bl or BOX.br
    end
    return BOX.vt
  end

  local results = p.results or {}
  local query = p.query or ""
  local query_focus = (p.focus or "results") == "query"
  local query_row_idx = row_index(p.query_y or 0)
  local list_row0 = row_index(p.list_y or 0)
  local view_text_row0 = row_index(p.preview_text_y or 0)
  local preview = p.preview or {}
  local extension = preview.extension or ""
  local preview_plain = preview.is_directory or preview.skipped or extension == ""

  local folder = p.folder or ""
  -- The foot of the list box names the finder and the folder it is looking in,
  -- so `:find src` says where it is scoped without a chrome row of its own.
  local list_footer = "Find Files" .. (folder ~= "" and (" · " .. folder) or "")

  -- The prompt's own right end: which match of how many, exactly as the native
  -- renderer reports it.
  local count = ""
  if p.scan_pending then
    count = "scanning..."
  elseif #results > 0 then
    local pos = math.min(math.max((p.selected or 0) + 1, 1), p.result_count or 1)
    count = pos .. " / " .. (p.result_count or 0)
  else
    count = (p.result_count or 0) .. ((p.result_count or 0) == 1 and " file" or " files")
  end
  local count_w = cell_len(count)
  local prompt_w = cell_len(PROMPT)
  local query_budget = math.max(1, list_w - prompt_w - (count_w > 0 and (count_w + 1) or 0))
  local show_count = count_w > 0 and count_w <= math.max(0, list_w - prompt_w - 2)

  -- Bottom border of the file view: the same summary the native renderer puts
  -- there (line count, size, and why there is nothing to show).
  local view_status = ""
  if p.selected == nil or (p.selected or -1) < 0 or (p.selected or -1) >= #results then
    view_status = "No selection"
  elseif preview.is_directory then
    view_status = "Folder"
  elseif preview.skipped then
    view_status = "Not previewed"
  elseif preview.is_binary then
    view_status = "Binary file"
  elseif #(preview.lines or {}) > 0 then
    view_status = tostring(#(preview.lines or {}) + (preview.start_line or 0)) .. " lines"
    local bytes = preview.size_bytes or 0
    if bytes > 0 and bytes < 1024 then
      view_status = view_status .. string.format("  %d B", bytes)
    elseif bytes >= 1024 then
      local kb = bytes / 1024
      if kb >= 1024 then
        view_status = view_status .. string.format("  %.1f MB", kb / 1024)
      else
        view_status = view_status .. string.format("  %.1f KB", kb)
      end
    end
    if preview.truncated then
      view_status = view_status .. "  truncated"
    end
  end

  for i = 1, height do
    local rw = row(i)
    local is_top = i == 1
    local is_bottom = i == height

    -- ---- the list box: left cap, contents, right cap ----
    frame_cap_left(rw, 0, cap_glyph(is_top, is_bottom, true))
    if is_top then
      frame_rule(rw, 0, box_w, "Results", t_fg)
    elseif is_bottom then
      frame_rule(rw, 0, box_w, list_footer, comment)
    end

    if i == query_row_idx then
      -- The prompt shares the picker's background -- it is a row of the box,
      -- not an inset field -- and the row above it is the last result.
      local typed = trunc_cells(query, query_budget)
      put(rw, query_col, PROMPT, accent)
      put(rw, query_col + prompt_w, typed, t_fg)
      if query == "" then
        put(rw, query_col + prompt_w,
            trunc_cells("type to filter files", query_budget), comment)
      end
      if show_count then
        put(rw, list_col + list_w - count_w, count, comment)
      end
      if query_focus and jot.ui.set_cursor then
        -- Absolute screen cells: the caret is placed by the terminal, not drawn
        -- into the row, so a float-relative column parks it outside the picker
        -- (it landed on whatever the editor had there).
        local caret = (p.query_x or 0) + prompt_w + cell_len(typed)
        jot.ui.set_cursor(math.min((p.list_x or 0) + list_w - 1, caret), p.query_y or 0)
      end
    elseif i >= list_row0 and i < list_row0 + #results then
      local index = i - list_row0          -- 0-based within the window
      local result = results[index + 1]
      local is_selected = (p.selected or -1) == (p.list_scroll or 0) + index
      local row_fg = is_selected and t_sel_fg or t_fg
      local row_bg = is_selected and t_sel_bg or nil
      local pointer = is_selected and POINTER or GUTTER
      local icon = (result.icon ~= nil and result.icon ~= "" and (result.icon .. " ")) or "  "
      local icon_fg = is_selected and t_sel_fg
        or (result.icon_fg ~= nil and result.icon_fg >= 0 and result.icon_fg or t_fg)
      -- The row is the file's path, keeping its tail when the row is too narrow:
      -- the name is the half of a path worth reading.
      local label = result.path or result.name or ""
      local name = result.name or ""
      local shared = cell_len(pointer) + cell_len(icon)
      local label_start, shown = tail_keep(label, math.max(1, list_w - shared))
      put(rw, list_col, pointer, is_selected and t_sel_fg or comment, row_bg)
      put(rw, list_col + cell_len(pointer), icon, icon_fg, row_bg)
      local label_off = put(rw, list_col + shared, shown, row_fg, row_bg)
      -- Highlight the characters the query consumed. They are byte offsets into
      -- the name, which is the tail of the label, so they land on the drawn text
      -- only once the folder prefix and any front the row clipped away are
      -- accounted for: `dropped` bytes of label are gone, and the ellipsis that
      -- replaced them takes three of its own.
      if label_off >= 0 and result.match and #result.match > 0 then
        local match_fg = is_selected and t_sel_fg or accent
        local name_at = #label - #name
        local dropped = label_start - 1
        local ellipsis_bytes = #shown - (#label - dropped)
        for _, m in ipairs(result.match) do
          local drawn = name_at + m - dropped + ellipsis_bytes
          if m >= 0 and m < #name and drawn >= 0 and drawn < #shown then
            rw.spans[#rw.spans + 1] = { start = label_off + drawn, len = 1, fg = match_fg }
          end
        end
      end
      -- The band spans the list's interior, borders excluded.
      band(rw, list_col, list_w, row_bg)
    end

    -- Nothing matched (or the scan is still running): one dim line where the
    -- first result would have been, near the middle of the list.
    if not is_top and not is_bottom and #results == 0
        and i == list_row0 + math.max(0, math.floor((p.list_h or 1) / 2)) then
      local scan_error = p.scan_error or ""
      local empty = p.scan_pending and "Scanning files..."
        or (scan_error ~= "" and scan_error
            or (query == "" and "No files found in this workspace."
                or "No files match the current query."))
      put(rw, list_col + cell_len(GUTTER), trunc_cells(empty, math.max(1, list_w - 2)), comment)
    end

    frame_cap_right(rw, 0, box_w, cap_glyph(is_top, is_bottom, false))

    -- ---- the file view box (after the list, so rows stay left to right) ----
    if show_view then
      local cur_col = cell_len(rw.text)
      if cur_col < view_col then
        rw.text = rw.text .. string.rep(" ", view_col - cur_col)
      end

      local text_col = view_col + 1
      local text_w = math.max(1, view_w - 2)
      local line_idx = i - view_text_row0
      local lines = preview.lines or {}
      frame_cap_left(rw, view_col, cap_glyph(is_top, is_bottom, true))
      if is_top then
        frame_rule(rw, view_col, view_w, "Preview", t_fg)
      elseif is_bottom then
        frame_rule(rw, view_col, view_w, view_status, comment)
      elseif line_idx >= 0 and line_idx < #lines then
        local line = lines[line_idx + 1]
        if preview_plain then
          put(rw, text_col, trunc_cells(line, text_w), comment)
        else
          local line_no = (preview.start_line or 0) + line_idx + 1
          put(rw, text_col, string.format("%5d ", line_no), line_nr)
          local clipped = trunc_cells(line, math.max(1, text_w - 6))
          local code_off = put(rw, text_col + 6, clipped, t_prev_fg)
          if code_off >= 0 and jot.syntax and jot.syntax.highlight then
            local ok, caps = pcall(jot.syntax.highlight, extension, clipped)
            if ok and type(caps) == "table" then
              for _, cap in ipairs(caps) do
                local cap_fg = colors[cap.kind]
                if cap_fg then
                  rw.spans[#rw.spans + 1] = { start = code_off + (cap.start or 0), len = cap.len or 0, fg = cap_fg }
                end
              end
            end
          end
        end
      end
      frame_cap_right(rw, view_col, view_w, cap_glyph(is_top, is_bottom, false))
    end

    -- The row is padded before its bands are measured: the background band has
    -- to cover the padding as well, and a band's byte range is taken from the
    -- finished line. Band spans then go in ahead of the glyphs -- the renderer
    -- sorts spans by start byte and a band shares its start with the glyph that
    -- opens the range (the pointer of a selected row), so the glyph spans, which
    -- carry their own colours, have to be drawn after it.
    rw.text = pad_cells(rw.text, p.w or 1)
    local band_spans = apply_bands(rw)
    for b = #band_spans, 1, -1 do
      table.insert(rw.spans, 2, band_spans[b])
    end
  end

  local body_list = {}
  local spans_by_line = {}
  for i = 1, height do
    local rw = rows[i]
    body_list[i] = rw and rw.text or ""
    if rw then
      spans_by_line[i] = rw.spans
    end
  end

  return present_panel("telescope", p, {}, { border = "none" }, body_list, spans_by_line)
end

return {
  telescope = telescope,
}
