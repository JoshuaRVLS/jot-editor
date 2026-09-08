-- Telescope — part of the Lua UI kit.
-- Split out of features/ui.lua so each surface stays small and
-- focused; features/ui.lua is the orchestrator that requires
-- every module and registers the handlers.
local h = require("jot_ui.helpers")
local close = h.close
local rune_len = h.rune_len
local cell_len = h.cell_len
local trunc_cells = h.trunc_cells
local pad_cells = h.pad_cells
local truncate = h.truncate
local present_panel = h.present_panel
local function tail_runes(s, n)
  local starts = {}
  for pos in utf8.codes(s) do
    starts[#starts + 1] = pos
  end
  if #starts <= n then
    return s
  end
  return s:sub(starts[#starts - n + 1])
end

local function left_clip(s, w)
  if rune_len(s) <= w then
    return s
  end
  return "…" .. truncate(tail_runes(s, w - 1), w - 1)
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
  -- Query row colors: a calm input background (command-bar style) rather than
  -- the loud selection highlight, so typed text stays readable on every theme.
  local t_query_fg = colors.t_query_fg or colors.fg_command or t_fg
  local t_query_bg = colors.t_query_bg or colors.bg_command or t_bg
  local border = colors.border or t_fg
  local comment = colors.comment or 8
  local accent = colors.accent or 6

  local inner_w = math.max(1, p.w - 2)
  local inner_h = math.max(1, p.h - 2)
  local body = {}
  local spans = {}

  local function bof(abs_row)
    return abs_row - (p.y or 0)
  end

  local function span(b, col, len, fg, bg)
    if b < 1 or b > inner_h or len <= 0 then
      return
    end
    spans[b] = spans[b] or {}
    spans[b][#spans[b] + 1] = { start = col, len = len, fg = fg, bg = bg }
  end

  -- Appends text at a *cell* column and returns the *byte* offset where it
  -- was placed (or -1 when dropped). Callers that build derived spans (e.g.
  -- syntax highlighting inside the appended text) must use this byte offset
  -- as their origin -- columns and bytes diverge once a row contains a
  -- multibyte glyph like the list/preview separator.
  local function put(b, col, text, fg, bg)
    if b < 1 or b > inner_h then
      return -1
    end
    if col < 0 then
      -- Left of the float's interior: the border owns those cells and a
      -- partial slice would cut a UTF-8 glyph in half, so drop it.
      return -1
    end
    if text == "" then
      return -1
    end
    body[b] = body[b] or ""
    -- Pad by *cells* (wide glyphs count 2) so content lands at the exact
    -- column and vertical lines stay straight across rows; wide glyphs are
    -- dropped only when they would overshoot.
    local cur = cell_len(body[b])
    if cur < col then
      body[b] = body[b] .. string.rep(" ", col - cur)
    end
    -- Span offsets are bytes and must match the actual append position.
    local start = #body[b]
    span(b, start, #text, fg, bg)
    body[b] = body[b] .. text
    return start
  end

  -- Full-row background: emits a start==0 span that the float renderer
  -- treats as the row's background across the whole interior width. Used for
  -- the input bar. Does not touch body text, so put() after it still lands
  -- at the requested column.
  local function fill_row_span(b, row_fg, row_bg)
    if b < 1 or b > inner_h then
      return
    end
    span(b, 0, 65535, row_fg, row_bg)
  end

  -- Bounded background over the results-list band of one row: from cell
  -- column `col0` for `width` cells. Spans use byte offsets, so the band
  -- edges are mapped from cells to bytes by walking the row (wide glyphs
  -- make cells and bytes diverge). Must run AFTER the row text is placed
  -- (padding before put would push the text past its column). The span
  -- starts at col0 > 0, so the float renderer treats it as a segment
  -- background, not the whole-row background (which it infers only from
  -- start==0 spans) — the selection highlight therefore stays inside the
  -- results column and never floods under the separator or preview pane.
  local function fill_col(b, col0, width, row_fg, row_bg)
    if b < 1 or b > inner_h or width <= 0 then
      return
    end
    body[b] = body[b] or ""
    -- Ensure the row is at least col0+width cells so the walk below finds a
    -- full band of spaces when the text is shorter.
    local cur = cell_len(body[b])
    if cur < col0 + width then
      body[b] = body[b] .. string.rep(" ", col0 + width - cur)
    end
    local function cell_to_byte(row, col)
      local byte = 0
      local cell = 0
      for _, cp in utf8.codes(row) do
        if cell >= col then
          break
        end
        local w = h.rune_width(cp)
        if cell + w > col then
          break
        end
        byte = byte + utf8.len(utf8.char(cp))
        cell = cell + w
      end
      return byte
    end
    local s = cell_to_byte(body[b], col0)
    local e = cell_to_byte(body[b], col0 + width)
    if e > s then
      span(b, s, e - s, row_fg, row_bg)
    end
  end

  -- Root line + query row.
  local root_row = bof(p.inner_y or 0)
  put(root_row, 1, left_clip(p.root or "", math.max(1, inner_w - 2)), comment, t_bg)
  local query_row = bof(p.query_y or 0)
  local query = p.query or ""
  local query_text = "  → " .. query
  if query == "" then
    query_text = query_text .. "type to filter files"
  end
  local query_focus = (p.focus or "results") == "query"
  local query_bg = query_focus and t_query_bg or t_bg
  local query_fg = query_focus and t_query_fg or comment
  fill_row_span(query_row, query_fg, query_bg)
  local q_off = put(query_row,
                    (p.query_x or 0) - (p.x or 0) - 1,
                    truncate(query_text, math.max(1, inner_w - 1)),
                    query_fg,
                    query_bg)
  -- Accent the prompt arrow (3-byte rune starting at byte offset 2 of
  -- "  → …") without shifting any later column.
  if q_off >= 0 then
    span(query_row, q_off + 2, 3, accent, query_bg)
  end
  if query_focus and jot.ui.set_cursor then
    -- "  → " is 4 cells (2 spaces + arrow + 1 space); the caret column is the
    -- prompt plus the typed query in *cells* (wide glyphs count 2), clamped
    -- to the query box. #() counts bytes — the 3-byte arrow alone would push
    -- the cursor 2 columns past where the next character lands, and wide
    -- query text drifts further.
    local caret_col = cell_len("  → " .. query)
    local caret = (p.query_x or 0)
                  + math.min(math.max(0, (p.query_w or 1) - 1), math.max(0, caret_col))
    jot.ui.set_cursor(caret, p.query_y or 0)
  end

  -- Result list.
  local list_row0 = bof(p.list_y or 0)
  local list_col = (p.list_x or 0) - (p.x or 0) - 1
  local list_w = math.max(1, p.list_w or 1)
  local results = p.results or {}
  if #results == 0 then
    local scan_error = p.scan_error or ""
    local empty = p.scan_pending and "Scanning files..."
      or (scan_error ~= "" and scan_error
          or (query == "" and "No files found in this workspace."
              or "No files match the current query."))
    put(list_row0 + math.max(0, math.floor((p.list_h or 0) / 2)),
        list_col,
        trunc_cells(empty, list_w),
        comment,
        t_bg)
  end
  for i, r in ipairs(results) do
    local b = list_row0 + i - 1
    local is_selected = (p.selected or -1) == (p.list_scroll or 0) + i - 1
    -- ASCII-safe directory marker: "▸" (U+25B8) is missing from several
    -- terminal fonts and renders as "??", so use ">" which is universally
    -- present. Files keep a two-space indent so names still align.
    local icon = r.is_directory and "> " or "  "
    local icon_fg = is_selected and t_sel_fg
      or (r.is_directory and (colors.sidebar_directory or accent) or t_fg)
    local parent = (r.parent_path or "") == "." and "" or r.parent_path or ""
    local parent_w = parent == ""
        and 0
        or math.min(cell_len(parent), math.max(0, math.floor(list_w / 2)))
    -- Reserve a 1-cell gap between the name and a right-aligned parent so a
    -- long name never glues onto the dimmed path (which reads as a broken,
    -- ragged layout even when the separator column itself is straight).
    local gap = parent_w > 0 and 1 or 0
    local name_budget = math.max(1, list_w - cell_len(icon) - parent_w - gap)
    local raw_name = r.name or ""
    local name = trunc_cells(raw_name, name_budget)
    if parent_w > 0 and cell_len(raw_name) > name_budget and name_budget >= 2 then
      -- Signal truncation with an ellipsis inside the budget.
      name = trunc_cells(raw_name, name_budget - 1) .. "…"
    end
    local row_fg = is_selected and t_sel_fg or t_fg
    local row_bg = is_selected and t_sel_bg or t_bg
    local row_off = put(b, list_col, icon .. name, row_fg, row_bg)
    -- Re-tint the icon on the selected row so it stays readable against the
    -- highlight; row_off is the byte offset where the icon was appended.
    if is_selected and row_off >= 0 then
      span(b, row_off, #icon, icon_fg, row_bg)
    end
    if parent_w > 0 and r.parent_path then
      put(b,
          list_col + math.max(0, list_w - parent_w),
          trunc_cells(left_clip(r.parent_path, parent_w), parent_w),
          -- On the selected row the dimmed location must still read against
          -- the highlight: reuse the selection fg (usually a dark-on-light
          -- pair) rather than the grey comment color.
          is_selected and t_sel_fg or comment,
          row_bg)
    end
    -- Selection highlight: bounded to the results-list band so it never
    -- paints over the separator gutter or the preview pane. Runs after the
    -- text is placed (fill_col must not pad the row before put, or the text
    -- would be pushed past its column).
    if is_selected then
      fill_col(b, list_col, list_w, row_fg, row_bg)
    end
  end

  -- Preview pane.
  if p.show_preview then
    local sep_col = (p.preview_x or 0) - 2 - (p.x or 0) - 1
    for ar = p.body_y or 0, (p.body_y or 0) + (p.body_h or 0) - 1 do
      put(bof(ar), sep_col, "│", border, t_bg)
    end
    local prev_row = bof(p.preview_y or 0)
    local prev_col = (p.preview_x or 0) - (p.x or 0) - 1
    local prev_inner_w = math.max(1, (p.preview_w or 1))
    local preview = p.preview or {}
    local prev_focus = (p.focus or "") == "preview"
    put(prev_row,
        prev_col,
        "Preview",
        prev_focus and (colors.t_sel_fg or accent) or t_fg,
        t_bg)
    local title = preview.title or ""
    put(prev_row + 1, prev_col, trunc_cells(left_clip(title, prev_inner_w), prev_inner_w), t_prev_fg,
        t_prev_bg)
    if preview.detail and preview.detail ~= "" then
      put(prev_row + 2, prev_col, trunc_cells(preview.detail, prev_inner_w), comment, t_prev_bg)
    end
    local code_row = prev_row + 3
    local line_no = preview.start_line or 0
    local ext = preview.extension or ""
    local plain = preview.is_directory or preview.skipped or ext == ""
    for _, ln in ipairs(preview.lines or {}) do
      local b = code_row
      code_row = code_row + 1
      local row_bg = t_prev_bg
      local row_fg = plain and comment or t_prev_fg
      if not plain then
        put(b, prev_col, string.format("%3d ", line_no + 1), comment, t_prev_bg)
        local text_col = prev_col + 4
        local clipped = trunc_cells(ln, math.max(1, prev_inner_w - 4))
        local code_start = put(b, text_col, clipped, t_prev_fg, t_prev_bg)
        if jot.syntax and jot.syntax.highlight then
          local ok, caps = pcall(jot.syntax.highlight, ext, clipped)
          if ok and type(caps) == "table" then
            for _, cap in ipairs(caps) do
              local cap_fg = colors[cap.kind]
              if cap_fg and code_start >= 0 then
                -- cap.start is a byte offset into clipped; the row origin is
                -- where clipped was actually appended (code_start), which is
                -- NOT the same as the cell column once the separator or any
                -- wide glyph precedes it.
                span(b, code_start + (cap.start or 0), cap.len or 0, cap_fg, t_prev_bg)
              end
            end
          end
        end
      else
        put(b, prev_col, trunc_cells(ln, prev_inner_w), row_fg, t_prev_bg)
      end
      line_no = line_no + 1
    end
  end

  -- Footer: selected path on the bottom border row (native geometry).
  local footer = p.scan_pending and "Searching"
    or (#results == 0 and "No selection" or "")
  -- Key hints with accent-highlighted bindings (built with byte offsets so
  -- the spans survive any wide glyphs elsewhere in the row).
  local keys_accent = nil
  if footer == "" then
    local parts = { "Enter", "Esc", "Tab", "↑/↓" }
    footer = ""
    local acc = 0
    keys_accent = {}
    for i, k in ipairs(parts) do
      if i > 1 then
        footer = footer .. "  "
        acc = acc + 2
      end
      keys_accent[i] = { start = acc, len = #k }
      footer = footer .. k
      acc = acc + #k
    end
  end
  local footer_b = bof(p.footer_y or 0)
  if footer_b >= 1 and footer_b <= inner_h then
    local f_off = put(footer_b, 1, truncate(footer, math.max(1, inner_w - 2)), comment, t_bg)
    if f_off >= 0 and keys_accent then
      for _, k in ipairs(keys_accent) do
        span(footer_b, f_off + k.start, k.len, accent, t_bg)
      end
    end
  end

  local body_list = {}
  for i = 1, inner_h do
    body_list[i] = pad_cells(body[i] or "", inner_w)
  end
  return present_panel("telescope",
                       p,
                       {},
                       {
                         border = "rounded",
                         title = p.title or " ",
                         title_fg = t_fg,
                       },
                       body_list,
                       spans)
end


return {
  telescope = telescope,
  tail_runes = tail_runes,
  left_clip = left_clip,
}
