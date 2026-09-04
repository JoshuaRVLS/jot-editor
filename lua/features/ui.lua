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
-- bottom row (opts.footer) with opts.footer_fg.
local function present_panel(name, p, rows, opts)
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
  local body = {}
  local spans_by_line = {}
  local shown = math.min(#rows, inner_h)
  for i = 1, shown do
    local row = rows[i]
    body[i] = row.text or ""
    if row.spans and #row.spans > 0 then
      spans_by_line[i] = row.spans
    end
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
    local spans = {}
    for _, m in ipairs(item.match or {}) do
      if m + 1 <= #label then
        spans[#spans + 1] = { start = m, len = 1, fg = is_selected and selection_fg or accent }
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

jot.ui.handler("command_palette", command_palette)
jot.ui.handler("quick_pick", quick_pick)
jot.ui.handler("popup", popup)
jot.ui.handler("save_prompt", save_prompt)
jot.ui.handler("quit_prompt", quit_prompt)

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
}