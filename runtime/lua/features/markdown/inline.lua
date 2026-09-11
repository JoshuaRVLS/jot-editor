-- Inline markdown parser: escapes, code spans, links/images (inline and
-- reference), autolinks, emphasis, strikethrough, sub/superscript, ins/mark,
-- emoji shortcodes and hard breaks. Math is deliberately left as literal
-- `$...$` text so KaTeX's auto-render pass can typeset it client-side.
local M = {}

local ESCAPE_MAP = {
  ["&"] = "&amp;",
  ["<"] = "&lt;",
  [">"] = "&gt;",
  ['"'] = "&quot;",
}

function M.escape(text)
  return (tostring(text):gsub("[&<>\"]", ESCAPE_MAP))
end

-- Only http(s), mailto, ftp, protocol-relative and relative targets survive:
-- `javascript:` and `data:` links are dropped.
local function safe_url(url)
  local trimmed = tostring(url or ""):match("^%s*(.-)%s*$")
  if trimmed == "" then
    return nil
  end
  local scheme = trimmed:match("^([%a][%w+.%-]*):")
  if scheme then
    scheme = scheme:lower()
    if scheme ~= "http" and scheme ~= "https" and scheme ~= "mailto"
        and scheme ~= "ftp" and scheme ~= "file" then
      return nil
    end
  end
  return trimmed
end

local EMOJI = {
  smile = "😄", laughing = "😆", wink = "😉", heart = "❤️", thumbsup = "👍",
  thumbsdown = "👎", ["+1"] = "👍", ["-1"] = "👎", tada = "🎉", rocket = "🚀",
  fire = "🔥", sparkles = "✨", warning = "⚠️", white_check_mark = "✅",
  x = "❌", heavy_check_mark = "✔️", bulb = "💡", bug = "🐛", book = "📖",
  memo = "📝", eyes = "👀", clap = "👏", pray = "🙏", sob = "😭",
  joy = "😂", thinking = "🤔", sunglasses = "😎", cry = "😢", angry = "😠",
  heartpulse = "💗", star = "⭐", zap = "⚡", gear = "⚙️", link = "🔗",
  lock = "🔒", unlock = "🔓", key = "🔑", mag = "🔍", bell = "🔔",
  calendar = "📅", clock1 = "🕐", hourglass = "⌛", coffee = "☕",
  computer = "💻", package = "📦", gift = "🎁", trophy = "🏆", medal = "🏅",
  checkered_flag = "🏁", construction = "🚧", recycle = "♻️",
  arrow_right = "➡️", arrow_left = "⬅️", arrow_up = "⬆️", arrow_down = "⬇️",
  question = "❓", exclamation = "❗", information_source = "ℹ️",
  no_entry = "⛔", stop_sign = "🛑", hourglass_flowing_sand = "⏳",
}

local function is_space(c)
  return c == "" or c:match("%s") ~= nil
end

local function is_alnum(c)
  return c ~= "" and c:match("[%w]") ~= nil
end

local function is_punct(c)
  return c ~= "" and c:match("[%p]") ~= nil
end

-- Counts the run of `ch` starting at byte index `i`.
local function run_length(s, i, ch)
  local n = 0
  while s:sub(i + n, i + n) == ch do
    n = n + 1
  end
  return n
end

-- Balanced bracket scan: `s[i]` must be "[", returns the index of the matching
-- "]" and the inner text, or nil.
local function matching_bracket(s, i)
  local depth = 0
  local j = i
  local n = #s
  while j <= n do
    local c = s:sub(j, j)
    if c == "\\" then
      j = j + 2
    elseif c == "[" then
      depth = depth + 1
      j = j + 1
    elseif c == "]" then
      depth = depth - 1
      if depth == 0 then
        return j, s:sub(i + 1, j - 1)
      end
      j = j + 1
    else
      j = j + 1
    end
  end
  return nil
end

-- Parses `(dest "title")` starting at the "(" (index i). Returns
-- dest, title, index-after-")" or nil.
local function parse_destination(s, i)
  local n = #s
  local j = i + 1
  while j <= n and s:sub(j, j):match("%s") do
    j = j + 1
  end
  local dest
  if s:sub(j, j) == "<" then
    local close = s:find(">", j + 1, true)
    if not close then
      return nil
    end
    dest = s:sub(j + 1, close - 1)
    j = close + 1
  else
    local start = j
    local depth = 0
    while j <= n do
      local c = s:sub(j, j)
      if c == "\\" then
        j = j + 2
      elseif c == "(" then
        depth = depth + 1
        j = j + 1
      elseif c == ")" then
        if depth == 0 then
          break
        end
        depth = depth - 1
        j = j + 1
      elseif c:match("%s") then
        break
      else
        j = j + 1
      end
    end
    dest = s:sub(start, j - 1)
  end
  -- Size extension (markdown-it-imsize): `![alt](img.png =200x100)`.
  while j <= n and s:sub(j, j):match("%s") do
    j = j + 1
  end
  if s:sub(j, j) == "=" then
    local size_start = j
    while j <= n and s:sub(j, j) ~= ")" and not s:sub(j, j):match("%s") do
      j = j + 1
    end
    dest = dest .. " " .. s:sub(size_start, j - 1)
  end

  local title
  while j <= n and s:sub(j, j):match("%s") do
    j = j + 1
  end
  local quote = s:sub(j, j)
  if quote == '"' or quote == "'" or quote == "(" then
    local closer = quote == "(" and ")" or quote
    local close = s:find(closer, j + 1, true)
    if close then
      title = s:sub(j + 1, close - 1)
      j = close + 1
    end
  end
  while j <= n and s:sub(j, j):match("%s") do
    j = j + 1
  end
  if s:sub(j, j) ~= ")" then
    return nil
  end
  return dest, title, j + 1
end

-- `![alt](src "title" =200x100)` / `![alt][ref]` -> <img>
local function render_image(s, i, ctx)
  local close, alt = matching_bracket(s, i)
  if not close then
    return nil
  end
  local dest, title, next_i
  if s:sub(close + 1, close + 1) == "(" then
    dest, title, next_i = parse_destination(s, close + 1)
  elseif s:sub(close + 1, close + 1) == "[" then
    local rclose, ref = matching_bracket(s, close + 1)
    if rclose then
      ref = (ref == "" and alt or ref):lower()
      local def = ctx.refs and ctx.refs[ref]
      if def then
        dest, title, next_i = def.href, def.title, rclose + 1
      end
    end
  end
  if not dest then
    return nil
  end

  -- Optional size suffix in the destination: `=WxH`, `=Wx`, `=xH`.
  local width, height
  local size = dest:match("%s*=([%d]*x[%d]*)$")
  if size then
    dest = dest:sub(1, #dest - #size - 1)
    local w, h = size:match("^(%d*)x(%d*)$")
    width = (w ~= "" and w or nil)
    height = (h ~= "" and h or nil)
  end

  local url = safe_url(dest) or safe_url((ctx.images_prefix or "") .. dest)
  if not url then
    return nil
  end
  local attrs = { 'src="' .. M.escape(url) .. '"', 'alt="' .. M.escape(alt) .. '"' }
  if title and title ~= "" then
    attrs[#attrs + 1] = 'title="' .. M.escape(title) .. '"'
  end
  if width then
    attrs[#attrs + 1] = 'width="' .. M.escape(width) .. '"'
  end
  if height then
    attrs[#attrs + 1] = 'height="' .. M.escape(height) .. '"'
  end
  return "<img " .. table.concat(attrs, " ") .. ">", next_i
end

-- `[text](href "title")`, `[text][ref]`, `[text][]`, `[^note]`
local function render_link(s, i, ctx, render_inline)
  if s:sub(i + 1, i + 1) == "^" then
    local close = s:find("]", i + 2, true)
    if not close then
      return nil
    end
    local id = s:sub(i + 2, close - 1)
    if not ctx.footnote_ids or not ctx.footnote_ids[id] then
      return nil
    end
    local index = ctx.footnote_numbers and ctx.footnote_numbers[id] or 0
    return '<sup class="footnote-ref"><a href="#fn-' .. M.escape(id) .. '" id="fnref-'
        .. M.escape(id) .. '">' .. M.escape(tostring(index)) .. "</a></sup>", close + 1
  end

  local close, label = matching_bracket(s, i)
  if not close then
    return nil
  end
  local dest, title, next_i
  if s:sub(close + 1, close + 1) == "(" then
    dest, title, next_i = parse_destination(s, close + 1)
  elseif s:sub(close + 1, close + 1) == "[" then
    local rclose, ref = matching_bracket(s, close + 1)
    if rclose then
      ref = (ref == "" and label or ref):lower()
      local def = ctx.refs and ctx.refs[ref]
      if def then
        dest, title, next_i = def.href, def.title, rclose + 1
      end
    end
  end
  if not dest then
    return nil
  end
  local url = safe_url(dest)
  if not url then
    return nil
  end
  local attrs = 'href="' .. M.escape(url) .. '"'
  if title and title ~= "" then
    attrs = attrs .. ' title="' .. M.escape(title) .. '"'
  end
  if url:match("^https?://") then
    attrs = attrs .. ' target="_blank" rel="noopener noreferrer"'
  end
  local inner = render_inline(label, ctx)
  return "<a " .. attrs .. ">" .. inner .. "</a>", next_i
end

-- `<http://x>`, `<user@host>` and raw inline HTML tags.
local function render_angle(s, i)
  local close = s:find(">", i + 1, true)
  if not close then
    return nil
  end
  local inner = s:sub(i + 1, close - 1)
  if inner:match("^[%a][%w+.%-]*://[^%s]+$") then
    return '<a href="' .. M.escape(inner) .. '" target="_blank" rel="noopener noreferrer">'
        .. M.escape(inner) .. "</a>", close + 1
  end
  if inner:match("^[%w%.%_%+%-]+@[%w%.%-]+%.%a+$") then
    return '<a href="mailto:' .. M.escape(inner) .. '">' .. M.escape(inner) .. "</a>", close + 1
  end
  if inner:match("^/") or inner:match("^[%a][%w%-]*") then
    -- Inline HTML: pass through untouched (this is what markdown allows).
    return s:sub(i, close), close + 1
  end
  return nil
end

local function find_emphasis_close(s, from, delim, needed)
  local i = from
  while true do
    local p = s:find(delim, i, true)
    if not p then
      return nil
    end
    local run = run_length(s, p, delim)
    local before = s:sub(p - 1, p - 1)
    local after = s:sub(p + run, p + run)
    local can_close = before ~= "" and not is_space(before)
    if can_close and delim == "_" and is_alnum(before) and is_alnum(after) then
      can_close = false
    end
    if can_close and run >= needed and p > from then
      return p, run
    end
    i = p + run
  end
end

local function render_emphasis(s, i, ctx, render_inline)
  local delim = s:sub(i, i)
  local run = run_length(s, i, delim)
  local after = s:sub(i + run, i + run)
  if is_space(after) then
    return nil
  end
  local before = s:sub(i - 1, i - 1)
  if delim == "_" and is_alnum(before) and is_alnum(after) then
    return nil
  end
  local use = run > 3 and 3 or run
  local p, close_run = find_emphasis_close(s, i + use, delim, use)
  if not p then
    if use > 1 then
      use = 1
      p, close_run = find_emphasis_close(s, i + 1, delim, 1)
      if not p then
        return nil
      end
    else
      return nil
    end
  end
  close_run = close_run or use
  local inner = render_inline(s:sub(i + use, p - 1), ctx)
  local html
  if use >= 3 then
    html = "<strong><em>" .. inner .. "</em></strong>"
  elseif use == 2 then
    html = "<strong>" .. inner .. "</strong>"
  else
    html = "<em>" .. inner .. "</em>"
  end
  return html, p + (close_run < use and close_run or use)
end

local function render_code_span(s, i)
  local ticks = run_length(s, i, "`")
  local closer = string.rep("`", ticks)
  local p = s:find(closer, i + ticks, true)
  if not p then
    return nil
  end
  local code = s:sub(i + ticks, p - 1)
  code = code:gsub("^ (.*) $", "%1"):gsub("\n", " ")
  return "<code>" .. M.escape(code) .. "</code>", p + ticks
end

-- Renders one inline run to HTML. `ctx` carries link references, footnote
-- bookkeeping and the resolved option switches.
local render_inline

render_inline = function(s, ctx)
  local out = {}
  local n = #s
  local i = 1

  local function emit(chunk)
    if chunk then
      out[#out + 1] = chunk
    end
  end

  while i <= n do
    local c = s:sub(i, i)
    local start_i = i
    local html, next_i

    if c == "\\" then
      local nxt = s:sub(i + 1, i + 1)
      if nxt ~= "" and is_punct(nxt) then
        emit(M.escape(nxt))
        i = i + 2
      else
        emit(M.escape(c))
        i = i + 1
      end
    elseif c == "`" then
      html, next_i = render_code_span(s, i)
    elseif c == "!" and s:sub(i + 1, i + 1) == "[" then
      html, next_i = render_image(s, i + 1, ctx)
    elseif c == "[" then
      html, next_i = render_link(s, i, ctx, render_inline)
    elseif c == "<" then
      html, next_i = render_angle(s, i)
    elseif c == "~" and s:sub(i + 1, i + 1) == "~" then
      local p = find_emphasis_close(s, i + 2, "~", 2)
      if p then
        html = "<del>" .. render_inline(s:sub(i + 2, p - 1), ctx) .. "</del>"
        next_i = p + 2
      end
    elseif c == "~" then
      local close = s:find("~", i + 1, true)
      local inner = close and s:sub(i + 1, close - 1) or nil
      if inner and inner ~= "" and not inner:match("%s") then
        html = "<sub>" .. render_inline(inner, ctx) .. "</sub>"
        next_i = close + 1
      end
    elseif c == "^" then
      local close = s:find("^", i + 1, true)
      local inner = close and s:sub(i + 1, close - 1) or nil
      if inner and inner ~= "" and not inner:match("%s") then
        html = "<sup>" .. render_inline(inner, ctx) .. "</sup>"
        next_i = close + 1
      end
    elseif c == "=" and s:sub(i + 1, i + 1) == "=" then
      local close = s:find("==", i + 2, true)
      if close then
        html = "<mark>" .. render_inline(s:sub(i + 2, close - 1), ctx) .. "</mark>"
        next_i = close + 2
      end
    elseif c == "+" and s:sub(i + 1, i + 1) == "+" then
      local close = s:find("++", i + 2, true)
      if close then
        html = "<ins>" .. render_inline(s:sub(i + 2, close - 1), ctx) .. "</ins>"
        next_i = close + 2
      end
    elseif c == "*" or c == "_" then
      html, next_i = render_emphasis(s, i, ctx, render_inline)
    elseif c == ":" and ctx.options.emoji then
      local code = s:match("^:([%w_+%-]+):", i)
      if code and EMOJI[code] then
        html = EMOJI[code]
        next_i = i + #code + 2
      end
    elseif c == "\n" then
      -- Two trailing spaces (or a backslash, handled above) force a hard break.
      if s:sub(i - 2, i - 1) == "  " then
        local last = out[#out] or ""
        if #last >= 2 then
          out[#out] = last:sub(1, #last - 2)
        end
        emit("<br>\n")
      else
        emit("\n")
      end
      i = i + 1
    else
      emit(M.escape(c))
      i = i + 1
    end

    if html then
      emit(html)
      i = next_i
    elseif next_i then
      i = next_i
    elseif i == start_i then
      -- No handler matched: emit the byte literally and step forward (a
      -- failed `*`/`[`/`~`/... attempt must never stall the scan).
      emit(M.escape(c))
      i = i + 1
    end
  end

  return table.concat(out)
end

-- Public entry point. `ctx` fields:
--   refs            table: lowercase label -> { href, title }
--   footnote_ids    table: id -> true for defined footnotes
--   footnote_numbers table: id -> 1-based display index
--   options         resolved option table (emoji, ...)
--   images_prefix   optional prefix for relative image sources
function M.render(text, ctx)
  ctx = ctx or {}
  ctx.refs = ctx.refs or {}
  ctx.options = ctx.options or {}
  if text == nil or text == "" then
    return ""
  end
  return render_inline(text, ctx)
end

return M
