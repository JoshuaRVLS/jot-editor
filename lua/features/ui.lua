-- Lua UI kit: renders the editor's modal surfaces (command palette, quick
-- pick, modal popups, save/quit prompts) from Lua instead of C++.
--
-- Native code keeps deciding *when* a surface is open and how keys are
-- routed; this module owns everything visual. Each surface registers through
-- jot.ui.handler(name, fn):
--
--   fn(state)  -> truthy = consumed (native render suppressed)
--   fn(nil)    -> surface closed; tear down the float
--
-- state carries the native layout box (x/y/w/h), the content (query, items,
-- results, lines), and a `colors` table with the active theme. To restyle
-- any of these surfaces, edit this file — no recompile needed.

local jot = jot

-- name -> {win, buf}
local surfaces = {}

local function close(name)
  local s = surfaces[name]
  if s then
    if s.win and s.win ~= 0 then
      jot.ui.float.close(s.win, true)
    end
    if s.buf and s.buf ~= 0 then
      jot.ui.buffer.delete(s.buf)
    end
    surfaces[name] = nil
  end
end

local function rune_len(s)
  local n = 0
  for _ in utf8.codes(s) do
    n = n + 1
  end
  return n
end

-- Terminal cell width helpers. Spans are byte offsets and the renderer slices
-- by bytes, so everything must stay on rune boundaries and never exceed the
-- panel width in *cells* (wide glyphs count 2) or the renderer clips and
-- mangles the row.
local function rune_width(cp)
  if cp >= 0x1100 and (cp <= 0x115F or cp == 0x2329 or cp == 0x232A
      or (cp >= 0x2E80 and cp <= 0xA4CF and cp ~= 0x303F)
      or (cp >= 0xAC00 and cp <= 0xD7A3) or (cp >= 0xF900 and cp <= 0xFAFF)
      or (cp >= 0xFE30 and cp <= 0xFE4F) or (cp >= 0xFF00 and cp <= 0xFF60)
      or (cp >= 0xFFE0 and cp <= 0xFFE6) or (cp >= 0x1F300 and cp <= 0x1FAFF)
      or (cp >= 0x20000 and cp <= 0x3FFFD)) then
    return 2
  end
  return 1
end

local function cell_len(s)
  local n = 0
  for _, cp in utf8.codes(s) do
    n = n + rune_width(cp)
  end
  return n
end

-- Rune-safe prefix of at most n cells.
local function take_cells(s, n)
  if n <= 0 then
    return ""
  end
  local out = {}
  local cells = 0
  for pos, cp in utf8.codes(s) do
    local w = rune_width(cp)
    if cells + w > n then
      return table.concat(out)
    end
    out[#out + 1] = utf8.char(cp)
    cells = cells + w
  end
  return s
end

local function trunc_cells(s, n)
  return take_cells(s, n)
end

-- Pads with spaces up to n cells (truncating cell-safe when already wider).
local function pad_cells(s, n)
  if n < 0 then
    n = 0
  end
  local c = cell_len(s)
  if c >= n then
    return take_cells(s, n)
  end
  return s .. string.rep(" ", n - c)
end

-- Truncates to at most n runes (never splits a UTF-8 sequence).
local function truncate(s, n)
  if n < 0 then
    n = 0
  end
  local out = {}
  local count = 0
  for pos, c in utf8.codes(s) do
    if count >= n then
      return table.concat(out)
    end
    out[count + 1] = utf8.char(c)
    count = count + 1
  end
  return s
end

local function pad(s, n)
  local len = rune_len(s)
  if len >= n then
    return truncate(s, n)
  end
  return s .. string.rep(" ", n - len)
end

-- Spans for a query match inside a label: returns {{start, len, fg}, ...}
-- with byte offsets, or {} when the query is not a verbatim substring.
local function match_spans(label, query, fg)
  local spans = {}
  if not query or query == "" then
    return spans
  end
  local hay = label:lower()
  local needle = query:lower()
  local pos = hay:find(needle, 1, true)
  if not pos then
    return spans
  end
  for i = pos, pos + #needle - 1 do
    if i - 1 < #label then
      spans[#spans + 1] = { start = i - 1, len = 1, fg = fg }
    end
  end
  return spans
end

-- Opens a float panel for a surface and fills it with themed rows.
-- rows = {{text=, fg=, bg=, spans=}, ...}; the float footer renders the
-- bottom row (opts.footer) with opts.footer_fg. Surfaces with sparse layouts
-- (tree-sitter modal, LSP manager, telescope) pass prebuilt body lines and
-- spans directly instead of rows.
local function present_panel(name, p, rows, opts, body_override, spans_override)
  close(name)
  opts = opts or {}
  local colors = p.colors or {}
  local fg = colors.fg or 7
  local bg = colors.panel_bg or colors.bg or 0
  local border = colors.border or fg
  local selection_fg = colors.selection_fg or 0
  local selection_bg = colors.selection_bg or 6
  local comment = colors.comment or 8
  local accent = colors.accent or 6

  local inner_h = math.max(1, p.h - 2)
  local body
  local spans_by_line
  local shown
  if body_override then
    body = body_override
    spans_by_line = spans_override or {}
    shown = inner_h
  else
    body = {}
    spans_by_line = {}
    shown = math.min(#rows, inner_h)
    for i = 1, shown do
      local row = rows[i]
      body[i] = row.text or ""
      -- A full-line span paints the row background (e.g. the selected / input
      -- rows); text spans drawn on top override individual characters. The
      -- huge len makes it cover whatever the renderer clips the line to.
      local spans = {
        { start = 0, len = 65535, fg = row.fg or fg, bg = row.bg or bg },
      }
      if row.spans then
        for _, sp in ipairs(row.spans) do
          spans[#spans + 1] = sp
        end
      end
      spans_by_line[i] = spans
    end
  end

  -- Pad every row to the panel width in cells so the renderer never clips a
  -- line (its clip would append ".." and split long runs of text).
  for i = 1, shown do
    body[i] = pad_cells(body[i] or "", p.w - 2)
  end

  local buf = jot.ui.buffer.create(false, true)
  jot.ui.buffer.set_lines(buf, 0, -1, true, body)
  local win = jot.ui.float.open(buf, {
    col = p.x,
    row = p.y,
    width = p.w,
    height = p.h,
    relative = "editor",
    anchor = "NW",
    border = opts.border or "rounded",
    focusable = false,
    mouse = false,
    hide = false,
    fg = fg,
    bg = bg,
    border_fg = opts.border_fg or border,
    title_fg = opts.title_fg or accent,
    footer_fg = opts.footer_fg or comment,
    title = opts.title,
    footer = opts.footer,
  })
  if not win or win == 0 then
    jot.ui.buffer.delete(buf)
    return false
  end
  surfaces[name] = { win = win, buf = buf }
  for i = 1, shown do
    if spans_by_line[i] then
      jot.ui.float.set_spans(win, i, spans_by_line[i])
    end
  end
  return true
end

-- Right-aligns `right` on the title row: returns (title_text, truncated).
local function title_with_count(title, count, width)
  local t = title
  local c = count or ""
  local space = width - rune_len(t) - rune_len(c) - 2
  if space < 1 then
    return truncate(t, math.max(1, width - 2)), truncate(c, 2)
  end
  return t .. string.rep(" ", space) .. c
end

-- ---------------------------------------------------------------------------
-- Command palette
-- ---------------------------------------------------------------------------

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

-- ---------------------------------------------------------------------------
-- Quick pick
-- ---------------------------------------------------------------------------

local function quick_pick(p)
  if not p then
    close("quick_pick")
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
  local title = (p.title and p.title ~= "" and " " .. p.title) or " Quick Pick"
  title = title_with_count(title, tostring(#(p.items or {})) .. "/" .. tostring(p.all_count or 0), inner_w)

  local rows = {}
  local query = p.query or ""
  rows[1] = { text = pad(truncate("> " .. query, inner_w), inner_w), fg = selection_fg, bg = selection_bg }
  rows[2] = { text = string.rep("─", inner_w), fg = border, bg = bg }

  local items = p.items or {}
  local selected = math.max(0, p.selected or 0)
  local list_h = math.max(0, p.h - 5) -- input + divider + footer
  local start_idx = 0
  if #items > 0 then
    start_idx = math.max(0, selected - list_h + 1)
    if start_idx + list_h > #items then
      start_idx = math.max(0, #items - list_h)
    end
  end
  local detail_w = p.w >= 72 and math.max(16, math.floor(p.w / 3)) or 0
  local label_w = math.max(8, inner_w - detail_w - 3)
  for row = 1, list_h do
    local idx = start_idx + row
    if idx > #items then
      break
    end
    local item = items[idx]
    local is_selected = idx - 1 == selected
    local row_fg = is_selected and selection_fg or fg
    local row_bg = is_selected and selection_bg or bg
    local prefix = is_selected and " ▎" or "  "
    local label = truncate(item.label or "", label_w)
    local spans = match_spans(label, query, is_selected and selection_fg or accent)
    -- match_spans offsets are bytes into label; shift them past the prefix
    -- (which is multibyte when selected: " ▎") so they point into the row.
    for _, sp in ipairs(spans) do
      sp.start = sp.start + #prefix
    end
    local line = prefix .. label
    if detail_w > 0 and item.detail and item.detail ~= "" then
      line = line .. string.rep(" ", math.max(1, inner_w - rune_len(line) - detail_w))
        .. truncate(item.detail, detail_w)
    end
    rows[#rows + 1] = { text = pad(line, inner_w), fg = row_fg, bg = row_bg, spans = spans }
  end

  local footer
  if selected >= 0 and selected < #items and items[selected + 1].preview
      and items[selected + 1].preview ~= "" then
    footer = items[selected + 1].preview
  else
    footer = "Enter open   Esc close   Up/Down move   PgUp/PgDn page"
  end

  return present_panel("quick_pick", p, rows, { title = title, title_fg = accent, footer = footer })
end

-- ---------------------------------------------------------------------------
-- Modal popup (help / keymap listings)
-- ---------------------------------------------------------------------------

local function popup(p)
  if not p then
    close("popup")
    return true
  end
  local colors = p.colors or {}
  local fg = colors.fg or 7
  local bg = colors.panel_bg or colors.bg or 0
  local comment = colors.comment or 8
  local accent = colors.accent or 6

  local inner_w = math.max(1, p.w - 2)
  local inner_h = math.max(1, p.h - 2)
  local lines = p.lines or {}
  local scroll = math.max(0, p.scroll or 0)
  local rows = {}
  for i = 1, inner_h do
    local idx = scroll + i
    if idx > #lines then
      break
    end
    rows[#rows + 1] = { text = pad(truncate(lines[idx] or "", inner_w), inner_w), fg = fg, bg = bg }
  end
  local footer
  if #lines > inner_h then
    local last = math.min(#lines, scroll + inner_h)
    footer = tostring(scroll + 1) .. "-" .. tostring(last) .. "/" .. tostring(#lines)
  end
  return present_panel("popup",
                       p,
                       rows,
                       { title = p.title and p.title ~= "" and " " .. p.title or nil,
                         title_fg = accent,
                         footer = footer })
end

-- ---------------------------------------------------------------------------
-- Save-as / quit prompts
-- ---------------------------------------------------------------------------

local function save_prompt(p)
  if not p then
    close("save_prompt")
    return true
  end
  local colors = p.colors or {}
  local fg = colors.fg or 7
  local bg = colors.panel_bg or colors.bg or 0
  local inner_w = math.max(1, p.w - 2)
  local rows = {
    { text = pad(truncate(" Save As: type filename, Enter=save, Esc=cancel", inner_w), inner_w), fg = fg, bg = bg },
    { text = pad(truncate(" Filename: " .. (p.input or ""), inner_w), inner_w), fg = fg, bg = bg },
  }
  return present_panel("save_prompt", p, rows, {})
end

local function quit_prompt(p)
  if not p then
    close("quit_prompt")
    return true
  end
  local colors = p.colors or {}
  local fg = colors.fg or 7
  local bg = colors.panel_bg or colors.bg or 0
  local inner_w = math.max(1, p.w - 2)
  local rows = {
    { text = pad(truncate(" Unsaved changes! Quit anyway? (y/n)", inner_w), inner_w), fg = fg, bg = bg },
  }
  return present_panel("quit_prompt", p, rows, {})
end

-- ---------------------------------------------------------------------------

-- ---------------------------------------------------------------------------
-- Tree-sitter status modal
-- ---------------------------------------------------------------------------

local function tree_sitter_status(p)
  if not p then
    close("tree_sitter_status")
    return true
  end
  local colors = p.colors or {}
  local fg = colors.fg or 7
  local bg = colors.panel_bg or colors.bg or 0
  local comment = colors.comment or 8
  local inner_w = math.max(1, p.w - 2)

  local list_h = math.max(0, p.h - 4)
  local rows = p.rows or {}
  local scroll = math.max(0, p.scroll or 0)
  local lang_w = math.max(12, math.min(24, math.floor(p.w / 3)))
  local body = {}
  local spans = {}
  local count = 0
  for j = 0, list_h - 1 do
    local idx = scroll + j
    if idx >= #rows then
      break
    end
    local row = rows[idx + 1]
    count = count + 1
    local b = j + 2 -- rows start on the float's second inner line
    if row.section then
      local text = " " .. trunc_cells(row.label or "", inner_w - 4)
      if row.detail and row.detail ~= "" then
        text = text .. " (" .. row.detail .. ")"
      end
      body[b] = pad_cells(text, inner_w)
      spans[b] = { { start = 0, len = 65535, fg = comment, bg = bg } }
    else
      local name = pad_cells(trunc_cells(row.label or "", lang_w), lang_w)
      local detail = trunc_cells(row.detail or "", math.max(1, inner_w - lang_w - 3))
      local name_fg = (row.color and row.color ~= 0) and row.color or fg
      local line = " " .. name .. " " .. detail
      body[b] = pad_cells(line, inner_w)
      spans[b] = {
        { start = 0, len = 65535, fg = fg, bg = bg },
        { start = 1, len = #name, fg = name_fg, bg = bg },
        { start = #name + 2, len = #detail, fg = comment, bg = bg },
      }
    end
  end
  if count == 0 then
    body[2] = pad(" " .. truncate("No languages registered", inner_w - 2), inner_w)
    spans[2] = { { start = 0, len = 65535, fg = comment, bg = bg } }
  end

  local inner_h = math.max(1, p.h - 2)
  local body_list = {}
  for i = 1, inner_h do
    body_list[i] = pad_cells(body[i] or "", inner_w)
  end

  local footer = "Esc close   Up/Down scroll"
  local max_scroll = math.max(0, #rows - list_h)
  if max_scroll > 0 then
    footer = footer .. "  " .. tostring(scroll + 1) .. "/" .. tostring(max_scroll + 1)
  end
  return present_panel("tree_sitter_status",
                       p,
                       {},
                       {
                         title = " Tree-sitter",
                         title_fg = colors.accent or colors.fg or 7,
                         footer = footer,
                       },
                       body_list,
                       spans)
end

-- ---------------------------------------------------------------------------
-- LSP manager modal
-- ---------------------------------------------------------------------------

local function lsp_manager(p)
  if not p then
    close("lsp_manager")
    return true
  end
  local colors = p.colors or {}
  local fg = colors.fg or 7
  local bg = colors.panel_bg or colors.bg or 0
  local selection_fg = colors.selection_fg or 0
  local selection_bg = colors.selection_bg or 6
  local comment = colors.comment or 8
  local accent = colors.accent or 6
  local error = colors.error or 1
  local inner_w = math.max(1, p.w - 2)

  local list_h = math.max(1, p.h - 3)
  local rows = p.rows or {}
  local scroll = math.max(0, p.scroll or 0)
  local selected = math.max(0, p.selected or 0)
  local label_w = math.max(1, p.label_w or 12)
  local state_x = (p.state_x or 0) - (p.x or 0) - 1 -- inner col of the state
  local body = {}
  local spans = {}
  local count = 0
  for j = 0, list_h - 1 do
    local idx = scroll + j
    if idx >= #rows then
      break
    end
    local row = rows[idx + 1]
    count = count + 1
    local is_selected = idx == selected
    local row_fg = is_selected and selection_fg or fg
    local row_bg = is_selected and selection_bg or bg
    local b = j + 1

    -- Compose the row: label at inner col 1, state at state_x, then buttons
    -- at their native rects so mouse clicks stay aligned. Columns are cells
    -- (wide glyphs count 2) so rows never drift out of alignment.
    local function emit(col, text, span_fg)
      local cur = cell_len(body[b] or "")
      if cur < col then
        body[b] = (body[b] or "") .. string.rep(" ", col - cur)
      end
      local start = #(body[b] or "")
      body[b] = body[b] .. text
      if span_fg then
        spans[b] = spans[b] or {}
        spans[b][#spans[b] + 1] = { start = start, len = #text, fg = span_fg, bg = row_bg }
      end
    end
    -- Background row span first (full width once padded later).
    spans[b] = { { start = 0, len = 65535, fg = row_fg, bg = row_bg } }
    emit(1, trunc_cells(row.label or "", label_w), row_fg)
    local state_col = math.max(1 + label_w, state_x)
    local state_w = math.max(1, (p.action_x or p.x + p.w - 31) - (p.state_x or 0) - 1)
    emit(state_col, trunc_cells(row.state or "", state_w), row.state_color or comment)
    for _, btn in ipairs(row.actions or {}) do
      local col = (btn.x or 0) - (p.x or 0) - 1
      if col >= 1 then
        local variant_fg = btn.variant == "danger" and error
          or btn.variant == "primary" and accent
          or (btn.variant == "secondary" or btn.variant == "muted") and comment
          or fg
        local avail = math.max(1, inner_w - col)
        local label = trunc_cells(btn.label or "", avail)
        if btn.focused then
          label = "[" .. label .. "]"
        else
          label = " " .. label .. " "
        end
        label = trunc_cells(label, math.max(1, (btn.w or #label) + (btn.focused and 0 or 0)))
        emit(col, label, btn.enabled and variant_fg or comment)
      end
    end
    if #(body[b] or "") < inner_w then
      body[b] = pad(body[b], inner_w)
    end
  end
  if count == 0 then
    body[1] = pad(" No servers registered", inner_w)
    spans[1] = { { start = 0, len = 65535, fg = comment, bg = bg } }
  end

  local inner_h = math.max(1, p.h - 2)
  local body_list = {}
  for i = 1, inner_h do
    body_list[i] = pad_cells(body[i] or "", inner_w)
  end

  local footer = tostring(#rows) .. " servers"
  return present_panel("lsp_manager",
                       p,
                       {},
                       {
                         title = " LSP Manager ",
                         title_fg = colors.accent or colors.fg or 7,
                         footer = footer,
                       },
                       body_list,
                       spans)
end

-- ---------------------------------------------------------------------------
-- Telescope (file finder with preview)
-- ---------------------------------------------------------------------------

-- Byte-safe tail clip: keeps the last n runes (never splits a UTF-8
-- sequence) and prefixes an ellipsis when text is cut.
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

  local function fill_row(b, row_bg, row_fg)
    if b < 1 or b > inner_h then
      return
    end
    body[b] = body[b] or ""
    span(b, 0, 65535, row_fg, row_bg)
  end

  -- Root line + query row.
  local root_row = bof(p.inner_y or 0)
  put(root_row, 1, left_clip(p.root or "", math.max(1, inner_w - 2)), comment, t_bg)
  local query_row = bof(p.query_y or 0)
  local query = p.query or ""
  local query_text = "  > " .. query
  if query == "" then
    query_text = query_text .. "type to filter files"
  end
  local query_focus = (p.focus or "results") == "query"
  local query_bg = query_focus and (colors.selection_bg or 6)
    or colors.bg_command or colors.bg or 0
  fill_row(query_row, query_bg, t_fg)
  put(query_row,
      (p.query_x or 0) - (p.x or 0) - 1,
      truncate(query_text, math.max(1, inner_w - 1)),
      t_fg,
      query_bg)
  if query_focus and jot.ui.set_cursor then
    local caret = (p.query_x or 0) + math.min(math.max(0, (p.query_w or 1) - 1),
                                             math.max(0, #("  > " .. query)))
    jot.ui.set_cursor(caret, p.query_y or 0)
  end

  -- Result list.
  local list_row0 = bof(p.list_y or 0)
  local list_col = (p.list_x or 0) - (p.x or 0) - 1
  local list_w = math.max(1, p.list_w or 1)
  local results = p.results or {}
  if #results == 0 then
    local empty = p.scan_pending and "Scanning files..."
      or (query == "" and "No files found in this workspace."
          or "No files match the current query.")
    put(list_row0 + math.max(0, math.floor((p.list_h or 0) / 2)),
        list_col,
        trunc_cells(empty, list_w),
        comment,
        t_bg)
  end
  for i, r in ipairs(results) do
    local b = list_row0 + i - 1
    local is_selected = (p.selected or -1) == (p.list_scroll or 0) + i - 1
    local icon = r.is_directory and "[D] " or "[F] "
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
    if is_selected then
      fill_row(b, t_sel_bg, t_sel_fg)
    end
    put(b, list_col, icon .. name, is_selected and t_sel_fg or t_fg,
        is_selected and t_sel_bg or t_bg)
    if parent_w > 0 and r.parent_path then
      put(b,
          list_col + math.max(0, list_w - parent_w),
          trunc_cells(left_clip(r.parent_path, parent_w), parent_w),
          comment,
          is_selected and t_sel_bg or t_bg)
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
  -- Native shows the selected relative path; approximate with first result.
  if footer == "" then
    footer = "Enter open   Esc close   Tab cycle   Up/Down move"
  end
  local footer_b = bof(p.footer_y or 0)
  if footer_b >= 1 and footer_b <= inner_h then
    put(footer_b, 1, truncate(footer, math.max(1, inner_w - 2)), comment, t_bg)
  end

  local body_list = {}
  for i = 1, inner_h do
    body_list[i] = pad_cells(body[i] or "", inner_w)
  end
  return present_panel("telescope",
                       p,
                       {},
                       {
                         border = "single",
                         title = p.title or " ",
                         title_fg = t_fg,
                       },
                       body_list,
                       spans)
end

-- ---------------------------------------------------------------------------

jot.ui.handler("command_palette", command_palette)
jot.ui.handler("quick_pick", quick_pick)
jot.ui.handler("popup", popup)
jot.ui.handler("save_prompt", save_prompt)
jot.ui.handler("quit_prompt", quit_prompt)
jot.ui.handler("tree_sitter_status", tree_sitter_status)
jot.ui.handler("lsp_manager", lsp_manager)
jot.ui.handler("telescope", telescope)

-- Exposed for tests / reuse; the loader ignores the return value.
return {
  close = close,
  present_panel = present_panel,
  match_spans = match_spans,
  command_palette = command_palette,
  quick_pick = quick_pick,
  popup = popup,
  save_prompt = save_prompt,
  quit_prompt = quit_prompt,
  tree_sitter_status = tree_sitter_status,
  lsp_manager = lsp_manager,
  telescope = telescope,
}