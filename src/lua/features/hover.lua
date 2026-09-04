-- Default LSP hover UI.
--
-- Renders hover results (mouse dwell and cursor-keymap hovers) as a themed
-- float window instead of the native popup. The native engine keeps deciding
-- *when* a hover is shown or dismissed; this file owns everything visual:
-- content cleanup, wrapping, sizing, placement, colors and code-fence syntax
-- highlighting. Edit this file (or register your own handler with
-- jot.lsp.hover_ui(fn) from config.lua) to restyle hover without touching C++.
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

-- Maps a markdown fence language tag to a file extension the syntax
-- highlighter understands (mirrors the native popup's mapping).
local function lang_to_ext(lang)
  lang = lang:lower():gsub("^%s+", ""):gsub("%s+$", "")
  local map = {
    ["c++"] = ".cpp", cpp = ".cpp", cc = ".cpp", cxx = ".cpp",
    c = ".c",
    ["python"] = ".py", py = ".py",
    ["javascript"] = ".js", js = ".js", jsx = ".jsx",
    ["typescript"] = ".ts", ts = ".ts", tsx = ".tsx",
    ["rust"] = ".rs", rs = ".rs",
    ["go"] = ".go", golang = ".go",
    ["bash"] = ".sh", sh = ".sh", ["shell"] = ".sh", zsh = ".sh",
    json = ".json", html = ".html", css = ".css", xml = ".xml",
    ["yaml"] = ".yaml", yml = ".yaml", toml = ".toml",
    ["markdown"] = ".md", md = ".md",
    cmake = ".cmake",
    ["make"] = ".make", makefile = ".make", dockerfile = ".dockerfile",
    lua = ".lua",
  }
  return map[lang] or ""
end

-- Lightweight markdown cleanup so hover reads well in a plain text float.
-- Inside code fences the line is kept verbatim (no marker stripping).
local function clean_line(ln, code)
  ln = ln:gsub("\r", "")
  if code then
    return ln
  end
  -- Strip inline markers; keep the text between them.
  ln = ln:gsub("`", ""):gsub("%*%*", ""):gsub("%*", ""):gsub("_", "")
  -- Headings and list bullets are kept but unmarked for a cleaner look.
  ln = ln:gsub("^#+%s*", "")
  ln = ln:gsub("^[-*]%s+", "")
  return ln
end

-- Splits raw markdown into logical lines {text, code, ext}. Fence lines are
-- consumed to track code blocks; their content lines are marked so the
-- highlighter can colorize them.
local function split_logical(contents)
  local logical = {}
  local in_code = false
  local code_ext = ""
  local blanks = 0
  for raw in (contents .. "\n"):gmatch("(.-)\n") do
    local fence = raw:match("^%s*```%s*([%w+#.%-]*)%s*$")
    if fence then
      in_code = not in_code
      code_ext = in_code and lang_to_ext(fence) or ""
    else
      local ln = clean_line(raw, in_code)
      if not ln or ln == "" then
        blanks = blanks + 1
        if blanks <= 1 then
          logical[#logical + 1] = { text = "", code = in_code, ext = code_ext }
        end
      else
        blanks = 0
        logical[#logical + 1] = { text = ln, code = in_code, ext = code_ext }
      end
    end
  end
  -- Drop a single trailing blank row.
  while #logical > 0 and logical[#logical].text == "" do
    logical[#logical] = nil
  end
  return logical
end

-- Highlights one code line via jot.syntax.highlight and maps the returned
-- token kinds to theme colors. Returns {{start, len, fg}, ...} with byte
-- offsets into `ln` (empty when nothing matches).
local function highlight_line(ln, ext, colors)
  local spans = {}
  if ext == "" or not colors or type(jot.syntax) ~= "table" then
    return spans
  end
  local ok, caps = pcall(jot.syntax.highlight, ext, ln)
  if not ok or type(caps) ~= "table" then
    return spans
  end
  for _, cap in ipairs(caps) do
    if type(cap) == "table" and cap.kind then
      local color = colors[cap.kind]
      if color then
        spans[#spans + 1] = { start = cap.start, len = cap.len, fg = color }
      end
    end
  end
  return spans
end

-- Builds the display line list from raw markdown contents (fences stripped,
-- code content kept as plain text).
local function build_lines(contents)
  local lines = {}
  for _, logical in ipairs(split_logical(contents)) do
    local wrapped = wrap_line(logical.text, HOVER_MAX_WIDTH)
    for _, wl in ipairs(wrapped) do
      lines[#lines + 1] = wl
    end
  end
  if #lines == 0 then
    lines[1] = ""
  end
  return lines
end

-- Like build_lines but also returns per-line syntax spans:
--   lines, spans = build_display(contents, colors)
-- spans[line] = {{start=, len=, fg=}, ...} (byte offsets, 1-based lines).
local function build_display(contents, colors)
  local lines = {}
  local spans = {}
  for _, logical in ipairs(split_logical(contents)) do
    local wrapped = wrap_line(logical.text, HOVER_MAX_WIDTH)
    for _, wl in ipairs(wrapped) do
      local idx = #lines + 1
      lines[idx] = wl
      if logical.code then
        local s = highlight_line(wl, logical.ext, colors)
        if #s > 0 then
          spans[idx] = s
        end
      end
    end
  end
  if #lines == 0 then
    lines[1] = ""
  end
  return lines, spans
end

local function present(info)
  close_float()
  if not info or not info.contents or info.contents == "" then
    return false
  end

  local lines, spans = build_display(info.contents, info.colors)
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
  local border_fg = info.border_fg or info.border or fg
  local footer_fg = (info.colors and info.colors.comment) or fg
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
    border_fg = border_fg,
    footer_fg = footer_fg,
    footer = total > shown and (shown .. "-" .. total .. "/" .. total) or nil,
  })
  if not win or win == 0 then
    jot.ui.buffer.delete(buf)
    buf = nil
    return false
  end
  for line_idx, s in pairs(spans) do
    if line_idx <= shown then
      jot.ui.float.set_spans(win, line_idx, s)
    end
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
  build_display = build_display,
  wrap_line = wrap_line,
  clean_line = clean_line,
  lang_to_ext = lang_to_ext,
  present = present,
  close = close_float,
}