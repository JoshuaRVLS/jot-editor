-- Default LSP hover UI.
--
-- Renders hover results (mouse dwell and cursor-keymap hovers) as a themed
-- float window instead of the native popup. The native engine keeps deciding
-- *when* a hover is shown or dismissed; this file owns everything visual:
-- content cleanup, wrapping, sizing, placement and colors. Edit this file (or
-- register your own handler with jot.lsp.hover_ui(fn) from config.lua) to
-- restyle hover without touching C++.
--
-- Handler contract:
--   fn(info)  -> truthy = hover consumed (native popup suppressed)
--   fn(nil)   -> native dismissed the hover; close the float

local jot = jot

-- Tune these to restyle the hover popup.
local HOVER_MAX_WIDTH = 96  -- wrap width for content
local HOVER_MAX_ROWS = 14   -- rows shown before the footer counter kicks in
local HOVER_BORDER = "single"

local win = nil -- current float handle (0 when none)
local buf = nil -- current scratch buffer handle

local function close_float()
  if win and win ~= 0 then
    jot.ui.float.close(win, true)
  end
  win = nil
  if buf and buf ~= 0 then
    jot.ui.buffer.delete(buf)
  end
  buf = nil
end

-- Byte-safe helpers over UTF-8 runes (stock Lua 5.4 utf8 library).

local function visual_len(s)
  local n = 0
  for _ in utf8.codes(s) do
    n = n + 1
  end
  return n
end

-- Returns (prefix of at most n runes, remainder).
local function take(s, n)
  if n <= 0 then
    return "", s
  end
  local out = {}
  local count = 0
  for pos, c in utf8.codes(s) do
    if count >= n then
      return table.concat(out), s:sub(pos)
    end
    out[count + 1] = utf8.char(c)
    count = count + 1
  end
  return table.concat(out), ""
end

-- Word-wraps one logical line so no output line exceeds `width` runes.
local function wrap_line(line, width)
  local out = {}
  local cur = ""
  for word in (line .. " "):gmatch("([^ ]+) +") do
    local word_len = visual_len(word)
    if word_len > width then
      if cur ~= "" then
        out[#out + 1] = cur
        cur = ""
      end
      local rest = word
      while visual_len(rest) > width do
        local head, tail = take(rest, width)
        out[#out + 1] = head
        rest = tail
      end
      cur = rest
    elseif visual_len(cur) + (cur == "" and 0 or 1) + word_len <= width then
      cur = cur == "" and word or (cur .. " " .. word)
    else
      out[#out + 1] = cur
      cur = word
    end
  end
  if cur ~= "" then
    out[#out + 1] = cur
  end
  if #out == 0 then
    out[1] = ""
  end
  return out
end

-- Lightweight markdown cleanup so hover reads well in a plain text float.
local function clean_line(ln)
  ln = ln:gsub("\r", "")
  if ln:match("^```") then
    return nil -- fence markers carry no content
  end
  -- Strip inline markers; keep the text between them.
  ln = ln:gsub("`", ""):gsub("%*%*", ""):gsub("%*", ""):gsub("_", "")
  -- Headings and list bullets are kept but unmarked for a cleaner look.
  ln = ln:gsub("^#+%s*", "")
  ln = ln:gsub("^[-*]%s+", "")
  return ln
end

-- Builds the display line list from raw markdown contents.
local function build_lines(contents)
  local logical = {}
  local blanks = 0
  for raw in (contents .. "\n"):gmatch("(.-)\n") do
    local ln = clean_line(raw)
    if not ln or ln == "" then
      blanks = blanks + 1
      if blanks <= 1 then
        logical[#logical + 1] = ""
      end
    else
      blanks = 0
      logical[#logical + 1] = ln
    end
  end
  -- Drop a single trailing blank row.
  while #logical > 0 and logical[#logical] == "" do
    logical[#logical] = nil
  end

  local lines = {}
  for _, ln in ipairs(logical) do
    local wrapped = wrap_line(ln, HOVER_MAX_WIDTH)
    for _, wl in ipairs(wrapped) do
      lines[#lines + 1] = wl
    end
  end
  if #lines == 0 then
    lines[1] = ""
  end
  return lines
end

local function present(info)
  close_float()
  if not info or not info.contents or info.contents == "" then
    return false
  end

  local lines = build_lines(info.contents)
  local width = 0
  for _, ln in ipairs(lines) do
    local w = visual_len(ln)
    if w > width then
      width = w
    end
  end
  width = math.min(HOVER_MAX_WIDTH, math.max(1, width))

  local total = #lines
  local shown = math.min(total, HOVER_MAX_ROWS)
  local body = {}
  for i = 1, shown do
    body[i] = lines[i]
  end

  buf = jot.ui.buffer.create(false, true)
  jot.ui.buffer.set_lines(buf, 0, -1, true, body)

  local fg = info.fg or 7
  local bg = info.bg or 0
  local h = shown + 2
  local w = width + 2
  -- jot.ui.float.open(buffer, config): the enter flag is not part of this
  -- binding (config is argument #2); focusable=false keeps it display-only.
  win = jot.ui.float.open(buf, {
    col = info.anchor_x or 0,
    row = info.anchor_y or 0,
    width = w,
    height = h,
    relative = "editor",
    anchor = "NW",
    border = HOVER_BORDER,
    focusable = false,
    mouse = false,
    hide = false,
    fg = fg,
    bg = bg,
    footer = total > shown and (shown .. "-" .. total .. "/" .. total) or nil,
  })
  if not win or win == 0 then
    jot.ui.buffer.delete(buf)
    buf = nil
    return false
  end
  return true
end

jot.lsp.hover_ui(function(info)
  if not info then
    close_float()
    return true
  end
  return present(info)
end)

-- Exposed for tests / reuse; the loader ignores the return value.
return {
  build_lines = build_lines,
  wrap_line = wrap_line,
  clean_line = clean_line,
  present = present,
  close = close_float,
}
