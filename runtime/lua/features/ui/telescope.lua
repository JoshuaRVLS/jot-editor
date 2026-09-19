-- Telescope — part of the Lua UI kit.
-- Split out of features/ui.lua so each surface stays small and
-- focused; features/ui.lua is the orchestrator that requires
-- every module and registers the handlers.
--
-- The picker is two boxes, not one panel with a divider:
--
--   ┌─  Files · src/render ───────────────┐ ┌─ frame.cpp ─────────────┐
--   │   query                             │ │     1 #include <x>      │
--   │                                     │ │     2 ...               │
--   │   name.cpp              src/render/ │ │                         │
--   └─ 12 files ──────────────────────────┘ └─ 240 lines  8.1 KB ─────┘
--
-- * the list holds files only: where a file lives is the dimmed folder path
--   on its own row, clipped from the left so the deepest part survives;
-- * the dot in front of the name is the file's tab state -- lit when the file
--   is already open, hollow when it is not;
-- * the selection (and the row under the pointer, which selects it) is a
--   filled background band, not a gutter marker;
-- * the file view is drawn like a small editor: the file name in its own top
--   border, line numbers in the gutter, content in syntax colors.
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
-- Nerd Fonts glyphs, built from their code points so this file stays ASCII:
-- the tab-state dot, the magnifier in the list title and the chevron in the
-- query field. They mirror the native renderer's kDotOpen / kDotClosed /
-- kSearchGlyph / kPromptGlyph.
local DOT_OPEN = utf8.char(0xf111)   -- nf-fa-circle: the file has a tab
local DOT_CLOSED = utf8.char(0xf10c) -- nf-fa-circle_o: it does not
local FIND_GLYPH = utf8.char(0xf002)
local PROMPT_GLYPH = utf8.char(0xf054)

-- Keeps the tail of a path, prefixed with an ellipsis, within `w` cells.
local function path_tail(s, w)
  if w <= 0 or s == "" then
    return ""
  end
  if cell_len(s) <= w then
    return s
  end
  if w <= 1 then
    return trunc_cells(s, w)
  end
  local starts = {}
  for pos in utf8.codes(s) do
    starts[#starts + 1] = pos
  end
  local keep = math.min(#starts, w - 1)
  return "…" .. s:sub(starts[#starts - keep + 1])
end

local function telescope(p)
  if not p then
    close("telescope")
    return true
  end
  local colors = p.colors or {}
  local t_fg = colors.t_fg or colors.fg or 7
  local t_bg = colors.t_bg or colors.bg or 0
  local t_sel_fg = colors.t_sel_fg or colors.selection_fg or 0
  local t_sel_bg = colors.t_sel_bg or colors.selection_bg or 6
  local t_prev_fg = colors.t_prev_fg or t_fg
  local t_prev_bg = colors.t_prev_bg or t_bg
  local t_query_fg = colors.t_query_fg or colors.fg_command or t_fg
  local t_query_bg = colors.t_query_bg or colors.bg_command or t_bg
  local border = colors.border or t_fg
  local comment = colors.comment or 8
  local accent = colors.accent or 6
  local info = colors.info or accent
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
    local fresh = { text = "", spans = {}, bands = {} }
    rows[i] = fresh
    return fresh
  end

  -- Appends `text` at cell column `col`, recording its span, and returns the
  -- byte offset it landed at. nil fg/bg leave those attributes to the float, so
  -- a nil bg keeps whatever background span is underneath.
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
  -- front (as this used to) pushed every glyph after it to the end of the row --
  -- the query field and the selected row rendered half a box wide.
  local function band(rw, col, cells, bg)
    if cells > 0 and bg ~= nil and col >= 0 then
      rw.bands[#rw.bands + 1] = { col = col, cells = cells, bg = bg }
    end
  end

  -- Applies a row's bands. Called once the row's text is complete, because the
  -- band's byte range has to be measured against the finished line (and the
  -- spans it covers have to take its background: a span with no bg is painted on
  -- the float's own background, which would punch holes in the band). Returns
  -- the band spans to append -- appending them here would run after the glyph
  -- spans they sit under, and a stable sort by start would then draw them on top.
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

  -- Left end of a top border: corner, a bit of line, then the title.
  local function frame_top_left(rw, col, title, title_fg)
    put(rw, col, BOX.tl, border)
    put(rw, col + 1, BOX.hz, border)
    if title and title ~= "" then
      put(rw, col + 2, " " .. title .. " ", title_fg or t_fg)
    end
  end

  -- Runs the top border out to its right corner from wherever the row's text
  -- currently ends (the title, or the corner and its leading line).
  local function frame_top_corner(rw, col, width)
    local cur = cell_len(rw.text)
    local fill = math.max(0, (col + width - 2) - cur + 1)
    if fill > 0 then
      put(rw, cur, string.rep(BOX.hz, fill), border)
    end
    put(rw, col + width - 1, BOX.tr, border)
  end

  local function frame_bottom_left(rw, col)
    put(rw, col, BOX.bl, border)
  end

  -- Bottom border: the rule, the status chip at its right end, the corner.
  local function frame_bottom_corner(rw, col, width, status)
    local cur = cell_len(rw.text)
    local chip = (status and status ~= "") and (" " .. status .. " ") or ""
    if cell_len(chip) > width - 4 then
      chip = ""
    end
    local chip_w = cell_len(chip)
    local run = math.max(0, (col + width - 1 - chip_w) - cur)
    if run > 0 then
      put(rw, cur, string.rep(BOX.hz, run), border)
    end
    if chip_w > 0 then
      put(rw, col + width - 1 - chip_w, chip, comment)
    end
    put(rw, col + width - 1, BOX.br, border)
  end

  local function frame_side_left(rw, col)
    put(rw, col, BOX.vt, border)
  end

  local function frame_side_right(rw, col, width)
    put(rw, col + width - 1, BOX.vt, border)
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
  local list_title = FIND_GLYPH .. " Files" .. (folder ~= "" and (" · " .. folder) or "")
  local count = (p.result_count or 0) .. ((p.result_count or 0) == 1 and " file" or " files")
  if p.scan_pending then
    count = "scanning…"
  end

  local selected_name = ""
  local selected_icon = ""
  if (p.selected or -1) >= 0 and (p.selected or -1) < #results then
    selected_name = results[(p.selected or 0) + 1].name or ""
    selected_icon = results[(p.selected or 0) + 1].icon or ""
  end
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

    -- ---- the list box: left edge, contents, right edge ----
    if i == 1 then
      frame_top_left(rw, 0, list_title, t_fg)
    elseif i == height then
      frame_bottom_left(rw, 0)
    else
      frame_side_left(rw, 0)
    end
    -- The list box sits on its own background (TelescopeNormal) -- frame
    -- included, exactly like the native renderer's fill -- so the two boxes read
    -- as two surfaces: the picker's panel on the left, the file's own editor
    -- background on the right, with the float's colour in the column between
    -- them.
    band(rw, 0, box_w, t_bg)

    if i == query_row_idx then
      local prompt = PROMPT_GLYPH .. " "
      local typed = trunc_cells(query, math.max(0, list_w - cell_len(prompt) - 1))
      local field_bg = query_focus and t_query_bg or nil
      put(rw, query_col, prompt .. typed, query_focus and t_query_fg or comment, field_bg)
      if query == "" then
        put(rw,
            query_col + cell_len(prompt),
            trunc_cells("type to filter files", math.max(0, list_w - cell_len(prompt) - 1)),
            comment,
            field_bg)
      end
      -- The field's own band, inset from both borders like the native renderer
      -- (query_w = list_w - 2).
      band(rw, query_col, list_w - 2, field_bg)
      if query_focus and jot.ui.set_cursor then
        local caret = query_col + cell_len(prompt) + cell_len(query)
        jot.ui.set_cursor(math.min(query_col + list_w - 1, caret), p.query_y or 0)
      end
    elseif i >= list_row0 and i < list_row0 + #results then
      local index = i - list_row0          -- 0-based within the window
      local result = results[index + 1]
      local is_selected = (p.selected or -1) == (p.list_scroll or 0) + index
      local row_fg = is_selected and t_sel_fg or t_fg
      local row_bg = is_selected and t_sel_bg or nil
      local dot = result.opened and (DOT_OPEN .. " ") or (DOT_CLOSED .. " ")
      local dot_fg = is_selected and t_sel_fg or (result.opened and info or comment)
      local icon = (result.icon ~= nil and result.icon ~= "" and (result.icon .. " ")) or "  "
      local icon_fg = is_selected and t_sel_fg
        or (result.icon_fg ~= nil and result.icon_fg >= 0 and result.icon_fg or t_fg)
      local parent = result.parent_path or ""
      local parent_w = parent ~= ""
          and math.min(cell_len(parent), math.max(0, math.floor(list_w / 3)))
        or 0
      local fixed = cell_len(dot) + cell_len(icon)
      local name_budget = math.max(1, list_w - fixed - parent_w - (parent_w > 0 and 1 or 0))
      local name = trunc_cells(result.name or "", name_budget)
      put(rw, list_col, dot, dot_fg, row_bg)
      put(rw, list_col + cell_len(dot), icon, icon_fg, row_bg)
      local name_off = put(rw, list_col + fixed, name, row_fg, row_bg)
      -- Highlight the characters the query consumed: byte offsets into the raw
      -- name, and the rendered name is a prefix of it.
      if name_off >= 0 and result.match and #result.match > 0 then
        for _, m in ipairs(result.match) do
          if m >= 0 and m < #name then
            rw.spans[#rw.spans + 1] = { start = name_off + m, len = 1, fg = accent }
          end
        end
      end
      if parent_w > 0 then
        put(rw,
            list_col + list_w - parent_w,
            path_tail(parent, parent_w),
            is_selected and t_sel_fg or comment,
            row_bg)
      end
      -- The selection band spans the list's interior, borders excluded, exactly
      -- like the native renderer's row fill.
      band(rw, list_col, list_w, row_bg)
    end

    -- Nothing matched (or the scan is still running): one dim line where the
    -- first result would have been, near the middle of the list.
    if #results == 0 and i == list_row0 + math.max(0, math.floor((p.list_h or 1) / 2)) then
      local scan_error = p.scan_error or ""
      local empty = p.scan_pending and "Scanning files..."
        or (scan_error ~= "" and scan_error
            or (query == "" and "No files found in this workspace."
                or "No files match the current query."))
      put(rw, list_col, trunc_cells(empty, math.max(1, list_w)), comment)
    end

    if i == 1 then
      frame_top_corner(rw, 0, box_w)
    elseif i == height then
      frame_bottom_corner(rw, 0, box_w, count)
    else
      frame_side_right(rw, 0, box_w)
    end

    -- ---- the file view box (after the list, so rows stay left to right) ----
    if show_view then
      -- The whole view box sits on the preview background (the editor's own),
      -- so the code pane does not read as part of the list.
      local cur_col = cell_len(rw.text)
      if cur_col < view_col then
        rw.text = rw.text .. string.rep(" ", view_col - cur_col)
      end

      local text_col = view_col + 1
      local text_w = math.max(1, view_w - 2)
      local line_idx = i - view_text_row0
      local lines = preview.lines or {}
      if i == 1 then
        local view_title = (selected_icon ~= "" and (selected_icon .. " ") or "") .. selected_name
        frame_top_left(rw, view_col, view_title, t_prev_fg)
        frame_top_corner(rw, view_col, view_w)
      elseif i == height then
        frame_bottom_left(rw, view_col)
        frame_bottom_corner(rw, view_col, view_w, view_status)
      else
        frame_side_left(rw, view_col)
        if line_idx >= 0 and line_idx < #lines then
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
        frame_side_right(rw, view_col, view_w)
      end
      -- The file view sits on the editor's own background, chrome included: the
      -- band covers the frame and its content, and every span it overlaps takes
      -- that background -- the syntax spans included -- so the code pane does
      -- not read as part of the list.
      band(rw, view_col, view_w, t_prev_bg)
    end

    -- Band spans go to the front: the renderer sorts spans by start byte and a
    -- band shares its start with the glyph that opens the range (the dot of a
    -- selected row, the prompt of the query field), so the glyph spans must be
    -- drawn after it to keep their own colours over the band.
    local band_spans = apply_bands(rw)
    for i = #band_spans, 1, -1 do
      table.insert(rw.spans, 1, band_spans[i])
    end
  end

  local body_list = {}
  local spans_by_line = {}
  for i = 1, height do
    local rw = rows[i]
    body_list[i] = pad_cells(rw and rw.text or "", p.w or 1)
    if rw then
      spans_by_line[i] = rw.spans
    end
  end

  return present_panel("telescope", p, {}, { border = "none" }, body_list, spans_by_line)
end

return {
  telescope = telescope,
  path_tail = path_tail,
}
