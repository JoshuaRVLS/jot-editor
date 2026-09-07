-- Helpers — part of the Lua UI kit.
-- Split out of features/ui.lua so each surface stays small and
-- focused; features/ui.lua is the orchestrator that requires
-- every module and registers the handlers.
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


return {
  close = close,
  rune_len = rune_len,
  rune_width = rune_width,
  cell_len = cell_len,
  take_cells = take_cells,
  trunc_cells = trunc_cells,
  pad_cells = pad_cells,
  truncate = truncate,
  pad = pad,
  match_spans = match_spans,
  present_panel = present_panel,
  title_with_count = title_with_count,
  -- Shared float registry: the same table close()/present_panel() maintain.
  surfaces = surfaces,
}
