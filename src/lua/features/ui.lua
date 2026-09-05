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
-- LSP completion popup
-- ---------------------------------------------------------------------------

local function completion_kind_color(kind_name, colors)
  local k = (kind_name or ""):lower()
  if k == "function" or k == "method" then
    return colors.function_method or colors["function"] or colors.fg or 7
  end
  if k == "constructor" then
    return colors.function_constructor or colors["function"] or colors.fg or 7
  end
  if k == "class" or k == "struct" or k == "interface" or k == "typeparameter" then
    return colors.type or colors.fg or 7
  end
  if k == "keyword" then
    return colors.keyword or colors.fg or 7
  end
  if k == "variable" then
    return colors.variable or colors.fg or 7
  end
  if k == "field" or k == "property" then
    return colors.field or colors.fg or 7
  end
  if k == "constant" or k == "enum" or k == "enummember" or k == "value" then
    return colors.constant or colors.fg or 7
  end
  if k == "module" or k == "namespace" then
    return colors.module or colors.namespace or colors.fg or 7
  end
  if k == "snippet" or k == "string" then
    return colors.string or colors.fg or 7
  end
  if k == "number" or k == "unit" then
    return colors.number or colors.fg or 7
  end
  if k == "operator" then
    return colors.operator or colors.fg or 7
  end
  return colors.fg or 7
end

-- Appends one styled part to a row, tracking byte offsets for spans. The row
-- text gets cell-padded by present_panel afterwards, so trailing content can
-- stop at any cell column without breaking span alignment.
local function add_part(parts, offsets, text, fg)
  if text == nil or text == "" then
    return
  end
  offsets[#offsets + 1] = { start = #table.concat(parts), len = #text, fg = fg }
  parts[#parts + 1] = text
end

local function lsp_completion(p)
  if not p then
    close("lsp_completion")
    return true
  end
  local colors = p.colors or {}
  local fg = colors.fg or 7
  local bg = colors.panel_bg or colors.bg or 0
  local selection_fg = colors.selection_fg or 0
  local selection_bg = colors.selection_bg or 6
  local comment = colors.comment or 8

  -- Native passes the content box; the float wraps it in a one-cell border.
  local f = {
    x = p.x - 1,
    y = p.y - 1,
    w = p.w + 2,
    h = p.h + 2,
    colors = p.colors,
  }
  local content_w = math.max(1, (p.w or 2))
  local items = p.items or {}
  local total = math.max(1, p.total or 0)
  local rows = {}
  local meta_w = math.max(8, math.floor(content_w / 3))
  local label_w = math.max(1, content_w - meta_w - 1)
  for i, it in ipairs(items) do
    local sel = (p.start or 0) + i - 1 == (p.selected or 0)
    local row_fg = sel and selection_fg or fg
    local row_bg = sel and selection_bg or bg
    local parts = {}
    local offsets = {}
    local icon = it.kind_icon or " "
    -- Reserve the first cell as a gutter; icon + label stay within label_w.
    local name = trunc_cells((it.label or ""), math.max(1, label_w - cell_len(icon) - 1))
    parts[#parts + 1] = " "
    add_part(parts, offsets, icon, completion_kind_color(it.kind_name, colors))
    add_part(parts, offsets, name, it.deprecated and comment or row_fg)
    local meta = it.kind_name or ""
    if it.deprecated then
      meta = meta == "" and "deprecated" or (meta .. " deprecated")
    end
    local detail = (it.detail ~= nil and it.detail ~= "") and it.detail or it.documentation or ""
    if detail ~= "" then
      meta = meta == "" and detail or (meta .. "  " .. detail)
    end
    local line = table.concat(parts)
    -- Right-align the meta column by cells (wide glyphs count 2).
    local cur = cell_len(line)
    local meta_col = math.max(cur + 1, content_w - meta_w)
    if cur < meta_col then
      line = line .. string.rep(" ", meta_col - cur)
    end
    local meta_off = #line
    local meta_text = trunc_cells(meta, math.max(1, content_w - meta_col))
    line = line .. meta_text
    if meta_text ~= "" then
      offsets[#offsets + 1] = { start = meta_off, len = #meta_text, fg = sel and selection_fg or comment }
    end
    rows[#rows + 1] = {
      text = line,
      fg = row_fg,
      bg = row_bg,
      spans = offsets,
    }
  end
  -- Footer row: position counter + typed prefix + filtered hint.
  local footer = tostring((p.selected or 0) + 1) .. "/" .. tostring(total)
  if p.prefix and p.prefix ~= "" then
    footer = footer .. "  " .. trunc_cells(p.prefix, math.max(1, content_w - 10))
  end
  if p.filtered then
    footer = footer .. "  filtered"
  end
  rows[#rows + 1] = { text = footer, fg = comment, bg = bg }
  return present_panel("lsp_completion", f, rows, { border = "single" })
end

-- ---------------------------------------------------------------------------
-- Home screen (startup surface)
-- ---------------------------------------------------------------------------

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

-- ---------------------------------------------------------------------------
-- Search panel (Ctrl+F)
-- ---------------------------------------------------------------------------

local function search_field_row(p, label, text, focused, input_w, colors)
  -- Mirrors the native geometry: "Find"/"Replace" label at col 1, input text
  -- from col label_w (9) so the natively-placed caret sits on this field.
  local selection_fg = colors.selection_fg or 0
  local selection_bg = colors.selection_bg or 6
  local comment = colors.comment or 8
  local fg = colors.fg or 7
  local bg = colors.panel_bg or colors.bg or 0
  -- "Find"/"Replace" label starts at interior col 0; the input text starts
  -- at the native label_w column (8 here) so the caret lands on this field.
  local line = pad_cells(label, 8)
  local field_start = #line -- byte offset of the input area
  local shown = trunc_cells(text or "", math.max(1, input_w))
  line = line .. shown
  local spans = {
    { start = 0, len = 65535, fg = fg, bg = bg },
  }
  if focused then
    spans[#spans + 1] = { start = field_start, len = 65535, fg = selection_fg, bg = selection_bg }
  end
  return { text = line, spans = spans }
end

local function search_panel(p)
  if not p then
    close("search_panel")
    return true
  end
  local colors = p.colors or {}
  local fg = colors.fg or 7
  local bg = colors.panel_bg or colors.bg or 0
  local comment = colors.comment or 8
  local accent = colors.accent or colors.fg or 7

  local inner_w = math.max(1, (p.w or 2) - 2)
  local label_w = 9
  local input_w = math.max(1, inner_w - label_w - 2)
  local rows = {}
  local focus_replace = p.replace_visible and p.focus_replace
  rows[#rows + 1] = search_field_row(p, "Find", p.query, not focus_replace, input_w, colors)
  if p.replace_visible then
    rows[#rows + 1] = search_field_row(p, "Replace", p.replace_text, focus_replace, input_w, colors)
  end
  -- Footer: key hints left, toggles + result count right.
  local hint = p.replace_visible
      and (p.scoped_to_selection and "Enter next  Up prev  Tab field  ^R one  ^R+Shift all in sel"
            or "Enter next  Up prev  Tab field  ^R one  ^R+Shift all")
      or "Enter next  Up prev  Tab case  ^H replace  ^E regex"
  local parts = {}
  local offsets = {}
  local function chip(text, active)
    local col = cell_len(table.concat(parts))
    local padded = trunc_cells(text, math.max(1, inner_w - col))
    parts[#parts + 1] = padded
    offsets[#offsets + 1] = { start = #table.concat(parts) - #padded, len = #padded, fg = active and accent or comment }
  end
  chip(" " .. (p.case_sensitive and "Aa" or "aa") .. " ", p.case_sensitive)
  chip((p.whole_word and "W" or "w") .. " ", p.whole_word)
  if p.regex then
    chip(".* ", true)
  end
  if p.scoped_to_selection then
    chip("Sel ", true)
  end
  chip((p.count or "0/0") .. " ", false)
  local right = table.concat(parts)
  local hint_w = math.max(1, inner_w - cell_len(right) - 1)
  local line = trunc_cells(hint, hint_w) .. string.rep(" ", math.max(0, inner_w - hint_w - cell_len(right))) .. right
  -- Recompute the chip offsets on the final line: hints are ASCII + pad, so
  -- each chip's byte position is (inner_w - cell_len(right)) + its part start.
  local base = inner_w - cell_len(right)
  for _, sp in ipairs(offsets) do
    sp.start = sp.start + base
  end
  rows[#rows + 1] = { text = line, fg = comment, bg = bg, spans = offsets }
  return present_panel("search_panel",
                       p,
                       rows,
                       {
                         border = "single",
                         title = (p.scoped_to_selection and " Find in Selection" or " Find"),
                         title_fg = accent,
                       })
end

-- ---------------------------------------------------------------------------
-- Status line (bottom strip, 2 rows)
-- ---------------------------------------------------------------------------

local function status_line(p)
  if not p then
    close("status_line")
    return true
  end
  local colors = p.colors or {}
  local status_fg = colors.status_fg or 7
  local status_bg = colors.status_bg or 0
  local message_fg = colors.status_message or status_fg
  local muted_fg = colors.status_muted_fg or colors.comment or 8

  local w = math.max(1, p.w or 1)
  local h = math.max(1, p.h or 2)

  -- Split the native model into left / right segment lists and mirror the
  -- native layout: right side drops to half the width, left gets the rest,
  -- the file segment truncates first when space runs low.
  local left, right = {}, {}
  for _, s in ipairs(p.segments or {}) do
    local seg = {
      text = s.text or "",
      fg = s.fg or status_fg,
      bg = s.bg or status_bg,
      bold = s.bold or false,
      optional = s.optional or false,
      priority = s.priority or 100,
    }
    if s.side == "left" then
      left[#left + 1] = seg
    else
      right[#right + 1] = seg
    end
  end

  local function block_width(list)
    local n = 0
    for i, s in ipairs(list) do
      n = n + cell_len(s.text)
      if i > 1 then
        n = n + 1 -- powerline separator
      end
    end
    return n
  end

  local function drop_to_fit(list, max_w)
    while block_width(list) > max_w do
      local rem
      for i, s in ipairs(list) do
        if s.optional and (not rem or s.priority < list[rem].priority) then
          rem = i
        end
      end
      if not rem then
        break
      end
      table.remove(list, rem)
    end
  end

  drop_to_fit(right, math.max(0, math.floor(w / 2)))
  local right_w = block_width(right)
  local min_gap = w >= 40 and 2 or 1
  local left_budget = math.max(0, w - right_w - (right_w > 0 and min_gap or 0))
  drop_to_fit(left, left_budget)
  if block_width(left) > left_budget and #left > 2 then
    for i = #left, 1, -1 do
      if (left[i].text or ""):find("jot") then
        table.remove(left, i)
        break
      end
    end
  end
  if block_width(left) > left_budget and #left > 2 then
    local excess = block_width(left) - left_budget
    left[1].text = trunc_cells(left[1].text, math.max(4, cell_len(left[1].text) - excess))
  end
  while block_width(left) > left_budget and #left > 0 do
    local last = left[#left]
    local target = cell_len(last.text) - (block_width(left) - left_budget)
    if target <= 0 then
      table.remove(left)
    else
      last.text = trunc_cells(last.text, target)
      break
    end
  end
  right_w = block_width(right)
  local right_x = math.max(0, w - right_w)

  -- Compose a segment block into text + byte-offset spans. The powerline
  -- separator between segments uses the color transition of its neighbors.
  local function compose_block(list)
    local text, spans = "", {}
    for i, s in ipairs(list) do
      if i > 1 then
        local at = #text
        text = text .. "\u{E0B0}"
        spans[#spans + 1] = { start = at, len = 3, fg = list[i - 1].bg, bg = s.bg, bold = true }
      end
      local at = #text
      text = text .. s.text
      spans[#spans + 1] = { start = at, len = #s.text, fg = s.fg, bg = s.bg, bold = s.bold }
    end
    return text, spans
  end

  -- Row 1: segmented bar (file + cursor left, diagnostics/git/LSP right).
  local row1_text, row1_spans = "", {}
  local ltext, lspans = compose_block(left)
  row1_text = row1_text .. ltext
  for _, sp in ipairs(lspans) do
    row1_spans[#row1_spans + 1] = sp
  end
  local lw = cell_len(ltext)
  if lw < right_x then
    local at = #row1_text
    local pad = string.rep(" ", right_x - lw)
    row1_text = row1_text .. pad
    row1_spans[#row1_spans + 1] = { start = at, len = #pad, fg = status_fg, bg = status_bg }
  end
  if right_w > 0 then
    local rtext, rspans = compose_block(right)
    local base = #row1_text
    for _, sp in ipairs(rspans) do
      row1_spans[#row1_spans + 1] =
          { start = base + sp.start, len = sp.len, fg = sp.fg, bg = sp.bg, bold = sp.bold }
    end
    row1_text = row1_text .. rtext
  end
  local rw1 = cell_len(row1_text)
  if rw1 < w then
    local at = #row1_text
    local pad = string.rep(" ", w - rw1)
    row1_text = row1_text .. pad
    row1_spans[#row1_spans + 1] = { start = at, len = #pad, fg = status_fg, bg = status_bg }
  end
  table.insert(row1_spans, 1, { start = 0, len = 65535, fg = status_fg, bg = status_bg })

  -- Row 2: transient message (bold) or the workspace context label (muted).
  local has_message = p.message and p.message ~= ""
  local row2_text = trunc_cells(has_message and "  " .. p.message or (p.context or ""),
                                math.max(0, w))
  local row2_spans = {
    { start = 0, len = 65535, fg = status_fg, bg = status_bg },
    { start = 0, len = #row2_text, fg = has_message and message_fg or muted_fg, bg = status_bg,
      bold = has_message },
  }

  local body = { trunc_cells(row1_text, w), row2_text }
  for i = 1, h do
    body[i] = pad_cells(body[i] or "", w)
  end

  -- Reuse the existing strip buffer/float across frames; only create once.
  local s = surfaces["status_line"]
  local buf, win
  if s then
    buf, win = s.buf, s.win
    jot.ui.float.configure(win, {
      col = p.x or 0,
      row = p.y or 0,
      width = w,
      height = h,
      relative = "editor",
      anchor = "NW",
      border = "none",
      strip = true,
      fg = status_fg,
      bg = status_bg,
    })
  else
    buf = jot.ui.buffer.create(false, true)
    win = jot.ui.float.open(buf, {
      col = p.x or 0,
      row = p.y or 0,
      width = w,
      height = h,
      relative = "editor",
      anchor = "NW",
      border = "none",
      focusable = false,
      mouse = false,
      hide = false,
      strip = true,
      fg = status_fg,
      bg = status_bg,
    })
    if not win or win == 0 then
      jot.ui.buffer.delete(buf)
      return false
    end
    surfaces["status_line"] = { win = win, buf = buf }
  end
  jot.ui.buffer.set_lines(buf, 0, -1, true, body)
  jot.ui.float.set_spans(win, 1, row1_spans)
  jot.ui.float.set_spans(win, 2, row2_spans)
  return true
end

-- ---------------------------------------------------------------------------
-- Sidebar / explorer panel
-- ---------------------------------------------------------------------------

local function sidebar(p)
  if not p then
    close("sidebar")
    return true
  end
  local colors = p.colors or {}
  local bg = p.bg or colors.sidebar_bg or colors.bg or 0
  local border_fg = (p.resizing and colors.active_border) or colors.sidebar_border
      or colors.border or 8
  local fg = colors.sidebar_fg or colors.fg or 7
  local dir = colors.sidebar_dir or colors.comment or 8
  local comment = colors.comment or 8

  local x, y, w, h = p.x or 0, p.y or 0, math.max(1, p.w or 1), math.max(1, p.h or 1)
  local content_x = math.max(1, p.content_x or 1)
  local content_w = math.max(1, p.content_w or (w - content_x - 1))
  local rail_w = math.max(0, p.rail_w or 0)

  close("sidebar")
  -- Row compositor: each line collects placement ops (glyphs at a cell
  -- column, or background fills), then renders once. This keeps byte-offset
  -- spans accurate even when a row already carries the right border, wide
  -- glyphs, or multiple badges.
  local ops = {}
  local body, spans = {}, {}
  for i = 1, h do
    body[i] = ""
    ops[i] = {}
  end
  local function place(abs_x, abs_y, text, f, b, bold)
    local line = abs_y - y + 1
    if line < 1 or line > h or text == "" then
      return
    end
    local col = math.max(0, abs_x - x)
    if col < w then
      ops[line][#ops[line] + 1] = { col = col, text = text, f = f or 7, b = b or 0, bold = bold }
    end
  end
  local function place_fill(abs_x, abs_y, ww, f, b)
    local line = abs_y - y + 1
    if line < 1 or line > h then
      return
    end
    local col = math.max(0, abs_x - x)
    if col < w then
      ops[line][#ops[line] + 1] =
          { col = col, fill = true, w = math.max(1, ww or 1), f = f or 7, b = b or 0 }
    end
  end
  -- Panel frame (closed box, rounded corners), mirroring the native paint.
  place(x, y, "╭" .. string.rep("─", math.max(0, w - 2)) .. "╮", border_fg, bg)
  for i = 2, h - 1 do
    local row = y + i - 1
    place(x, row, "│", border_fg, bg)
    place(x + w - 1, row, "│", border_fg, bg)
  end
  place(x, y + h - 1, "╰" .. string.rep("─", math.max(0, w - 2)) .. "╯", border_fg, bg)

  -- Activity rail (left, inside the frame): active view marker + label, and
  -- a separator between the rail and content.
  local function draw_rail(row_abs, label, active)
    local row = row_abs - y + 1
    if row < 1 or row > h then
      return
    end
    local rf = active and dir or comment
    place_fill(x + 1, row_abs, math.max(0, rail_w - 2), rf, bg)
    place(x + 1, row_abs, active and "▌" or " ", rf, bg, active)
    if rail_w >= 3 then
      place(x + 2, row_abs, label, rf, bg, active)
    end
  end
  if p.rail_explorer_row and p.rail_explorer_row >= 0 then
    draw_rail(y + p.rail_explorer_row, "󰉋 ", not p.git_view)
  end
  if p.rail_git_row and p.rail_git_row >= 0 then
    draw_rail(y + p.rail_git_row, " ", p.git_view)
  end
  if rail_w > 0 then
    for i = 2, h - 1 do
      place(x + math.max(0, rail_w - 1), y + i - 1, "│", border_fg, bg, false)
    end
  end  -- Rows: fill each rect, then symbol / text / badges on top.
  for _, r in ipairs(p.rows or {}) do
    if r.y and r.y >= y and r.y < y + h then
      place_fill(r.x or x, r.y, r.w or 1, r.fg or fg, r.bg or bg)
      if r.symbol and r.symbol ~= "" and r.symbol_x and r.symbol_x >= 0 then
        place(r.symbol_x, r.y, r.symbol, r.symbol_fg or r.fg, r.bg or bg, r.symbol_bold)
      end
      place(r.text_x or (r.x + 1), r.y, r.text or "", r.fg or fg, r.bg or bg, r.bold)
      if r.badge and r.badge ~= "" and r.badge_x and r.badge_x >= 0 then
        place(r.badge_x, r.y, r.badge, r.badge_fg or r.fg, r.bg or bg, true)
      end
      if r.badge2 and r.badge2 ~= "" and r.badge2_x and r.badge2_x >= 0 then
        place(r.badge2_x, r.y, r.badge2, r.badge2_fg or r.fg, r.bg or bg, true)
      end
    end
  end

  -- Footer (one row above the bottom border).
  if p.footer and p.footer ~= "" and p.footer_y and p.footer_y >= y and p.footer_y < y + h then
    place(p.footer_x or content_x + 1,
          p.footer_y,
          trunc_cells(p.footer, math.max(1, content_w)),
          p.footer_fg or comment,
          bg,
          false)
  end

  -- Byte index (1-based) of the codepoint occupying the given cell column.
  local function byte_at_cell(s, target)
    if target <= 0 then
      return 1
    end
    local cells, i = 0, 1
    while true do
      local next_i = utf8.offset(s, 2, i)
      if not next_i then
        return #s + 1
      end
      local cp = utf8.codepoint(s, i)
      local cw = rune_width(cp)
      if cells + cw >= target then
        return i
      end
      cells = cells + cw
      i = next_i
    end
  end

  -- Compose every content row (2 .. h-1): merge placement ops sorted by
  -- column; bounded background fills are emitted first, glyph spans on top
  -- so text always wins. Byte offsets stay exact even with wide glyphs.
  for i = 2, h - 1 do
    local list = ops[i] or {}
    table.sort(list, function(a, b)
      if a.col ~= b.col then
        return a.col < b.col
      end
      return a.fill and not b.fill
    end)
    local text, glyphs, fills = "", {}, {}
    local cur = 0
    for _, op in ipairs(list) do
      if op.fill then
        fills[#fills + 1] = op
      else
        local tw = cell_len(op.text)
        if op.col > cur then
          text = text .. string.rep(" ", op.col - cur)
          cur = op.col
        end
        local at = #text
        text = text .. op.text
        glyphs[#glyphs + 1] = { start = at, len = #op.text, fg = op.f, bg = op.b, bold = op.bold }
        cur = cur + tw
      end
    end
    local ls = {}
    for _, op in ipairs(fills) do
      local start_at = byte_at_cell(text, op.col)
      if start_at <= #text then
        local end_at = byte_at_cell(text, op.col + op.w)
        ls[#ls + 1] = { start = start_at - 1,
                        len = math.min(65535, math.max(1, end_at - start_at)),
                        fg = op.f, bg = op.b, bold = false }
      end
    end
    for _, sp in ipairs(glyphs) do
      ls[#ls + 1] = sp
    end
    body[i] = pad_cells(text, w)
    if #ls > 0 then
      spans[i] = ls
    end
  end

  -- Top border row with the header baked in (the native paint draws the
  -- header on the border row after the frame).
  local hcol = math.max(2, (p.header_x or (content_x + 1)) - x)
  local htxt = (p.header and p.header ~= "")
      and trunc_cells(p.header, math.max(1, content_w))
      or ""
  -- The label (icon first) is baked into the border dash run. Reserve a
  -- couple of cells of dash after the corner so a leading icon never butts
  -- against the frame; hand-written headers with their own leading space
  -- are fine (blank cells here read as intentional padding, not a notch).
  local hcells = cell_len(htxt)
  local lead = math.min(math.max(hcol - 1, 2), w - 2)
  local tail = math.max(0, (w - 2) - lead - hcells)
  local top = "╭" .. string.rep("─", lead) .. htxt .. string.rep("─", tail) .. "╮"
  body[1] = pad_cells(top, w)
  -- Border glyphs are 3-byte UTF-8, so the border prefix is 3 + 3*lead
  -- bytes; spans must never slice mid-rune or the corner renders as `?`.
  local border_bytes = 3 + 3 * lead
  local top_spans = { { start = 0, len = border_bytes, fg = border_fg, bg = bg,
                        bold = p.resizing } }
  if htxt ~= "" then
    top_spans[#top_spans + 1] =
        { start = border_bytes, len = #htxt, fg = p.header_fg or dir, bg = bg, bold = true }
  end
  top_spans[#top_spans + 1] = { start = border_bytes + #htxt,
                                len = #top - border_bytes - #htxt,
                                fg = border_fg, bg = bg, bold = p.resizing }
  if #top_spans > 0 then
    spans[1] = top_spans
  end

  -- Bottom border row.
  if h > 1 then
    body[h] = pad_cells("╰" .. string.rep("─", math.max(0, w - 2)) .. "╯", w)
    spans[h] = { { start = 0, len = 65535, fg = border_fg, bg = bg, bold = p.resizing } }
  end

  -- Reuse the persistent buffer/float across frames.
  local s = surfaces["sidebar"]
  local buf, win
  if s then
    buf, win = s.buf, s.win
    jot.ui.float.configure(win, {
      col = x,
      row = y,
      width = w,
      height = h,
      relative = "editor",
      anchor = "NW",
      border = "none",
      fg = fg,
      bg = bg,
    })
  else
    buf = jot.ui.buffer.create(false, true)
    win = jot.ui.float.open(buf, {
      col = x,
      row = y,
      width = w,
      height = h,
      relative = "editor",
      anchor = "NW",
      border = "none",
      focusable = false,
      mouse = false,
      hide = false,
      fg = fg,
      bg = bg,
    })
    if not win or win == 0 then
      jot.ui.buffer.delete(buf)
      return false
    end
    surfaces["sidebar"] = { win = win, buf = buf }
  end
  jot.ui.buffer.set_lines(buf, 0, -1, true, body)
  for i = 1, h do
    if spans[i] then
      jot.ui.float.set_spans(win, i, spans[i])
    end
  end
  return true
end

-- ---------------------------------------------------------------------------
-- Right side panels (git diff / outline / debugger / plugin)
-- ---------------------------------------------------------------------------

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

-- ---------------------------------------------------------------------------
-- Context menu
-- ---------------------------------------------------------------------------

local function menu_rows(p, list, selected)
  -- Renders an item list as a bordered panel: enabled rows are selectable,
  -- disabled rows are dimmed and skip the selection bar.
  local colors = p.colors or {}
  local fg = colors.fg or 7
  local bg = colors.panel_bg or colors.bg or 0
  local selection_fg = colors.selection_fg or 0
  local selection_bg = colors.selection_bg or 6
  local comment = colors.comment or 8
  local inner_w = math.max(1, p.w - 2)
  local rows = {}
  for i, item in ipairs(list) do
    local enabled = item.enabled ~= false
    local sel = enabled and (i - 1) == (selected or 0)
    local label = trunc_cells(item.label or "", math.max(1, inner_w - 4))
    rows[#rows + 1] = {
      text = " " .. label,
      fg = sel and selection_fg or (enabled and fg or comment),
      bg = sel and selection_bg or bg,
    }
  end
  return rows
end

local function context_menu(p)
  if not p then
    close("context_menu")
    return true
  end
  local rows = menu_rows(p, p.items or {}, p.selected)
  return present_panel("context_menu", p, rows, {})
end

-- ---------------------------------------------------------------------------
-- Menu bar dropdown
-- ---------------------------------------------------------------------------

local function menu_dropdown(p)
  if not p then
    close("menu_dropdown")
    return true
  end
  local colors = p.colors or {}
  local accent = colors.accent or colors.fg or 7
  local rows = menu_rows(p, p.items or {}, p.selected)
  local label = (p.menu_label or "") ~= "" and (" " .. p.menu_label .. " ") or nil
  return present_panel("menu_dropdown",
                       p,
                       rows,
                       { border = "single", title = label, title_fg = accent })
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
jot.ui.handler("lsp_completion", lsp_completion)
jot.ui.handler("context_menu", context_menu)
jot.ui.handler("menu_dropdown", menu_dropdown)
jot.ui.handler("search_panel", search_panel)
jot.ui.handler("home_screen", home_screen)
jot.ui.handler("status_line", status_line)
jot.ui.handler("sidebar", sidebar)
jot.ui.handler("side_panel", side_panel)

-- Exposed for tests / reuse; the loader ignores the return value.
return {
  close = close,
  present_panel = present_panel,
  match_spans = match_spans,
  status_line = status_line,
  sidebar = sidebar,
  side_panel = side_panel,
  command_palette = command_palette,
  quick_pick = quick_pick,
  popup = popup,
  save_prompt = save_prompt,
  quit_prompt = quit_prompt,
  tree_sitter_status = tree_sitter_status,
  lsp_manager = lsp_manager,
  telescope = telescope,
  lsp_completion = lsp_completion,
  context_menu = context_menu,
  menu_dropdown = menu_dropdown,
  search_panel = search_panel,
  home_screen = home_screen,
}
