-- Statusline — part of the Lua UI kit.
-- Split out of features/ui.lua so each surface stays small and
-- focused; features/ui.lua is the orchestrator that requires
-- every module and registers the handlers.
local h = require("jot_ui.helpers")
local close = h.close
local cell_len = h.cell_len
local trunc_cells = h.trunc_cells
local pad_cells = h.pad_cells
local pad = h.pad
local surfaces = h.surfaces
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
      -- Leading glyph (file-type icon / git mark) painted with its own
      -- color; kept out of `text` so truncation never eats it.
      symbol = s.symbol or "",
      symbol_fg = s.symbol_fg or s.fg or status_fg,
    }
    if s.side == "left" then
      left[#left + 1] = seg
    else
      right[#right + 1] = seg
    end
  end

  -- Editor process memory usage (Lua-owned segment). The value comes from
  -- a tiny native bridge (jot.process.memory, cached ~1 s in C++); the
  -- formatting, placement and drop priority all live here. Marked optional
  -- with the lowest priority so it is the first segment to yield when the
  -- bar fills up.
  local ok_mem, mem_bytes = pcall(function() return jot.process.memory() end)
  if ok_mem and type(mem_bytes) == "number" and mem_bytes > 0 then
    local mb = mem_bytes / (1024 * 1024)
    local value
    if mb >= 1024 then
      value = string.format("%.1fG", mb / 1024)
    elseif mb >= 100 then
      value = string.format("%.0fM", mb)
    else
      value = string.format("%.1fM", mb)
    end
    right[#right + 1] = {
      text = " mem " .. value .. " ",
      fg = muted_fg,
      bg = status_bg,
      bold = false,
      optional = true,
      priority = 5,
      symbol = "",
      symbol_fg = muted_fg,
    }
  end

  local function block_width(list)
    local n = 0
    for i, s in ipairs(list) do
      n = n + cell_len(s.symbol) + cell_len(s.text)
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
  -- A segment's `symbol` (glyph, own color) is emitted before its label.
  local function compose_block(list)
    local text, spans = "", {}
    for i, s in ipairs(list) do
      if i > 1 then
        local at = #text
        text = text .. "\u{E0B0}"
        spans[#spans + 1] = { start = at, len = 3, fg = list[i - 1].bg, bg = s.bg, bold = true }
      end
      local at = #text
      local prefix = s.symbol ~= "" and s.symbol or ""
      text = text .. prefix .. s.text
      if prefix ~= "" then
        spans[#spans + 1] =
          { start = at, len = #prefix, fg = s.symbol_fg, bg = s.bg, bold = s.bold }
      end
      spans[#spans + 1] =
        { start = at + #prefix, len = #s.text, fg = s.fg, bg = s.bg, bold = s.bold }
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

  -- The bar is a single row (status_height == 1), and the editor hands the
  -- message / workspace context that used to be a second row over as segments,
  -- so row 1 carries everything. Rows 2+ are only filled when the strip is
  -- taller than that, which keeps a legacy two-row layout from losing its
  -- message.
  local body = { trunc_cells(row1_text, w) }
  local spans_by_row = { row1_spans }
  if h >= 2 then
    local has_message = p.message and p.message ~= ""
    local row2_text = trunc_cells(has_message and "  " .. p.message or (p.context or ""),
                                  math.max(0, w))
    body[2] = row2_text
    spans_by_row[2] = {
      { start = 0, len = 65535, fg = status_fg, bg = status_bg },
      { start = 0, len = #row2_text, fg = has_message and message_fg or muted_fg,
        bg = status_bg, bold = has_message },
    }
  end
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
  for i = 1, h do
    if spans_by_row[i] then
      jot.ui.float.set_spans(win, i, spans_by_row[i])
    end
  end
  return true
end


return {
  status_line = status_line,
}
