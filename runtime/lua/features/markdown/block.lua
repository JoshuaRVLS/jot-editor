-- Block-level markdown parser: headings (ATX + setext), thematic breaks,
-- fenced and indented code, blockquotes (with GitHub alerts), lists (nested,
-- loose/tight, task lists), GFM tables, definition lists, footnote and link
-- reference definitions, raw HTML blocks, and paragraphs.
--
-- Every emitted element carries a `data-line` attribute (the 1-based source
-- line) which is what drives the two-way scroll sync.
local inline = require("jot_md.inline")
local toc = require("jot_md.toc")

local M = {}

local function is_blank(text)
  return text == nil or text:match("^%s*$") ~= nil
end

-- Leading indentation in columns (tab = 4) plus the byte index of the first
-- non-indent character.
local function leading_spaces(text)
  local count = 0
  local i = 1
  while i <= #text do
    local c = text:sub(i, i)
    if c == " " then
      count = count + 1
    elseif c == "\t" then
      count = count + 4
    else
      break
    end
    i = i + 1
  end
  return count, i
end

local function strip_indent(text, want)
  local removed = 0
  local i = 1
  while removed < want and i <= #text do
    local c = text:sub(i, i)
    if c == " " then
      removed = removed + 1
    elseif c == "\t" then
      removed = removed + 4
    else
      break
    end
    i = i + 1
  end
  return text:sub(i)
end

local function is_thematic(text)
  local compact = text:gsub("%s", "")
  if #compact < 3 then
    return false
  end
  return compact:match("^%*+$") ~= nil
      or compact:match("^%-+$") ~= nil
      or compact:match("^_+$") ~= nil
end

-- Returns kind ("ul"/"ol"), indent columns, marker width, content, start number.
local function list_marker(text)
  local indent, pos = leading_spaces(text)
  local rest = text:sub(pos)
  local bullet, content = rest:match("^([%-%+%*])[ \t]+(.*)$")
  if bullet then
    return { kind = "ul", indent = indent, width = 1, content = content, bullet = bullet }
  end
  local num, delim, ocontent = rest:match("^(%d+)([%.)])[ \t]+(.*)$")
  if num then
    return {
      kind = "ol",
      indent = indent,
      width = #num + 1,
      content = ocontent,
      number = tonumber(num),
      bullet = delim,
    }
  end
  return nil
end

local DIAGRAM_LANGS = {
  mermaid = true, flow = true, flowchart = true, sequence = true, sequencediagram = true,
  gantt = true, classdiagram = true, statediagram = true, erdiagram = true, journey = true,
  pie = true, gitgraph = true, mindmap = true, timeline = true, quadrantchart = true,
  requirementdiagram = true, c4context = true, sankey = true, xy = true, block = true,
  packet = true, architecture = true, radar = true, treemap = true,
}

local function render_fence(info, code, line, ctx)
  local lang = (info:match("^%s*([^%s]+)") or ""):lower()
  local opts = ctx.options
  local data_line = string.format(' data-line="%d"', line)

  if opts.mermaid and DIAGRAM_LANGS[lang] then
    return "<div class=\"mermaid\"" .. data_line .. ">" .. inline.escape(code) .. "</div>\n"
  end
  if opts.flowchart and not opts.mermaid and (lang == "flow" or lang == "flowchart") then
    return "<div class=\"flowchart\"" .. data_line .. ">" .. inline.escape(code) .. "</div>\n"
  end
  if opts.plantuml and lang == "plantuml" then
    return "<div class=\"plantuml\"" .. data_line .. ">" .. inline.escape(code) .. "</div>\n"
  end
  if opts.echarts and lang == "echarts" then
    return "<div class=\"echarts\"" .. data_line .. ">" .. inline.escape(code) .. "</div>\n"
  end
  if opts.vega and (lang == "vega" or lang == "vega-lite") then
    return "<div class=\"vega\" data-vega-lite=\"" .. (lang == "vega-lite" and "1" or "0")
        .. "\"" .. data_line .. ">" .. inline.escape(code) .. "</div>\n"
  end

  local class = lang ~= "" and string.format(' class="language-%s"', inline.escape(lang)) or ""
  local pre = "<pre>" .. string.format("<code%s>", class) .. inline.escape(code) .. "</code></pre>"
  if not opts.code_copy then
    return pre .. "\n"
  end
  return "<div class=\"code-block\"" .. data_line .. ">"
      .. "<button class=\"copy-code\" type=\"button\" title=\"Copy code\">copy</button>"
      .. pre .. "</div>\n"
end

-- ── headings ────────────────────────────────────────────────────────────────
local function heading_html(raw, level, line, ctx)
  local text = raw
  local custom_id
  local body, id = text:match("^(.-)%s*{#([^}]+)}%s*$")
  if body then
    text = body
    custom_id = id
  end

  local html = inline.render(text, ctx)
  local anchor
  if custom_id then
    anchor = custom_id
  else
    local plain = text:gsub("`([^`]*)`", "%1")
        :gsub("%[([^%]]*)%]%b()", "%1")
        :gsub("[%*_~%^%+%=]", "")
    anchor = toc.slug(plain, ctx.slug_state)
  end

  ctx.headings[#ctx.headings + 1] = { level = level, id = anchor, text = text, html = html }
  return string.format('<h%d id="%s" data-line="%d">%s</h%d>\n',
                       level, inline.escape(anchor), line, html, level)
end

-- ── blocks ──────────────────────────────────────────────────────────────────
local function try_fence(lines, i, ctx)
  local ch, extra, info = lines[i].text:match("^%s*([`~])([`~]*)%s*(.*)$")
  if not ch then
    return nil
  end
  local fence_len = 1 + #extra
  if ch == "`" and info:find("`") then
    return nil
  end
  local body = {}
  local j = i + 1
  local closed = false
  while j <= #lines do
    local t = lines[j].text
    local close = t:match("^%s*(" .. ch .. "+)%s*$")
    if close and #close >= fence_len then
      closed = true
      break
    end
    body[#body + 1] = t
    j = j + 1
  end
  local consumed = (j - i) + (closed and 1 or 0)
  return consumed, render_fence(info, table.concat(body, "\n"), lines[i].line, ctx)
end

local function try_heading(lines, i, ctx)
  local hashes, rest = lines[i].text:match("^%s*(#+)[ \t]+(.*)$")
  if not hashes then
    hashes, rest = lines[i].text:match("^%s*(#+)$")
    rest = ""
  end
  if not hashes or #hashes > 6 then
    return nil
  end
  rest = rest:gsub("[ \t]+#+[ \t]*$", "")
  return 1, heading_html(rest, #hashes, lines[i].line, ctx)
end

-- Forward declarations: the block pass recurses back into itself and the
-- paragraph/definition-list scanners use `starts_block` to find block borders.
local parse_blocks
local starts_block

local function try_thematic(lines, i, ctx)
  if not is_thematic(lines[i].text) then
    return nil
  end
  return 1, string.format('<hr data-line="%d">\n', lines[i].line)
end

local function try_blockquote(lines, i, ctx)
  if not lines[i].text:match("^%s*>") then
    return nil
  end
  local inner = {}
  local j = i
  while j <= #lines do
    local t = lines[j].text
    if t:match("^%s*>") then
      inner[#inner + 1] = { text = t:gsub("^%s*> ?", ""), line = lines[j].line }
      j = j + 1
    elseif not is_blank(t) and #inner > 0 then
      inner[#inner + 1] = { text = t, line = lines[j].line }
      j = j + 1
    else
      break
    end
  end

  -- GitHub alerts: > [!NOTE] / [!TIP] / [!IMPORTANT] / [!WARNING] / [!CAUTION]
  local alert_kind, alert_rest
  if #inner > 0 then
    alert_kind, alert_rest = inner[1].text:match("^%[!(%u+)%]%s*(.*)$")
  end
  local body
  if alert_kind then
    local titles = { NOTE = "Note", TIP = "Tip", IMPORTANT = "Important",
                     WARNING = "Warning", CAUTION = "Caution" }
    local title = titles[alert_kind] or alert_kind
    inner[1] = { text = alert_rest, line = inner[1].line }
    body = string.format(
        '<blockquote class="alert alert-%s" data-line="%d"><p class="alert-title">%s</p>\n%s</blockquote>\n',
        alert_kind:lower(), lines[i].line, inline.escape(title), parse_blocks(inner, ctx))
  else
    body = string.format('<blockquote data-line="%d">\n%s</blockquote>\n',
                         lines[i].line, parse_blocks(inner, ctx))
  end
  return j - i, body
end

local function split_table_row(line)
  local trimmed = line:match("^%s*(.-)%s*$")
  trimmed = trimmed:gsub("^|", ""):gsub("|$", "")
  local cells = {}
  local buf = {}
  local i = 1
  while i <= #trimmed do
    local c = trimmed:sub(i, i)
    if c == "\\" and i < #trimmed then
      buf[#buf + 1] = trimmed:sub(i + 1, i + 1)
      i = i + 2
    elseif c == "|" then
      cells[#cells + 1] = table.concat(buf):match("^%s*(.-)%s*$")
      buf = {}
      i = i + 1
    else
      buf[#buf + 1] = c
      i = i + 1
    end
  end
  cells[#cells + 1] = table.concat(buf):match("^%s*(.-)%s*$")
  return cells
end

local function try_table(lines, i, ctx)
  if not lines[i + 1] or not lines[i].text:find("|", 1, true) then
    return nil
  end
  local delim = split_table_row(lines[i + 1].text)
  if #delim < 1 then
    return nil
  end
  local aligns = {}
  for _, cell in ipairs(delim) do
    local left = cell:sub(1, 1) == ":"
    local right = cell:sub(-1) == ":"
    if not cell:match("^:?%-+:?$") or #cell < 3 then
      return nil
    end
    if left and right then
      aligns[#aligns + 1] = "center"
    elseif right then
      aligns[#aligns + 1] = "right"
    elseif left then
      aligns[#aligns + 1] = "left"
    else
      aligns[#aligns + 1] = ""
    end
  end

  local header = split_table_row(lines[i].text)
  local rows = {}
  local j = i + 2
  while j <= #lines and not is_blank(lines[j].text) and lines[j].text:find("|", 1, true) do
    rows[#rows + 1] = { cells = split_table_row(lines[j].text), line = lines[j].line }
    j = j + 1
  end

  local out = { string.format('<div class="table-wrap" data-line="%d"><table>\n<thead>\n<tr>', lines[i].line) }
  for c = 1, #header do
    local align = aligns[c] and aligns[c] ~= "" and (' style="text-align:' .. aligns[c] .. '"') or ""
    out[#out + 1] = string.format("<th%s>%s</th>", align, inline.render(header[c], ctx))
  end
  out[#out + 1] = "</tr>\n</thead>\n<tbody>\n"
  for _, row in ipairs(rows) do
    out[#out + 1] = string.format('<tr data-line="%d">', row.line)
    for c = 1, #header do
      local align = aligns[c] and aligns[c] ~= "" and (' style="text-align:' .. aligns[c] .. '"') or ""
      out[#out + 1] = string.format("<td%s>%s</td>", align, inline.render(row.cells[c] or "", ctx))
    end
    out[#out + 1] = "</tr>\n"
  end
  out[#out + 1] = "</tbody>\n</table></div>\n"
  return j - i, table.concat(out)
end

local function try_list(lines, i, ctx)
  local first = list_marker(lines[i].text)
  if not first then
    return nil
  end
  local kind = first.kind
  local base_indent = first.indent
  local start_num = first.number
  local items = {}
  local loose = false
  local current = nil
  local content_indent = base_indent + first.width + 1
  local j = i

  while j <= #lines do
    local line = lines[j].text
    if is_blank(line) then
      local k = j + 1
      while k <= #lines and is_blank(lines[k].text) do
        k = k + 1
      end
      if k > #lines then
        j = k
        break
      end
      local k_indent = leading_spaces(lines[k].text)
      local k_marker = list_marker(lines[k].text)
      if k_indent > base_indent or (k_marker and k_marker.indent == base_indent and k_marker.kind == kind) then
        loose = true
        if current then
          current.lines[#current.lines + 1] = { text = "", line = lines[j].line }
        end
        j = j + 1
      else
        break
      end
    else
      local marker = list_marker(line)
      local indent = leading_spaces(line)
      if marker and marker.indent == base_indent and marker.kind == kind then
        current = { lines = { { text = marker.content, line = lines[j].line } }, marker = marker }
        items[#items + 1] = current
        content_indent = base_indent + marker.width + 1
        j = j + 1
      elseif current and indent > base_indent then
        current.lines[#current.lines + 1] = {
          text = strip_indent(line, math.min(content_indent, indent)),
          line = lines[j].line,
        }
        j = j + 1
      else
        break
      end
    end
  end

  local attrs = ""
  if kind == "ol" and start_num and start_num ~= 1 then
    attrs = string.format(' start="%d"', start_num)
  end
  local out = { string.format('<%s%s data-line="%d">\n', kind, attrs, lines[i].line) }
  for _, item in ipairs(items) do
    local inner = parse_blocks(item.lines, ctx)
    local checked, task_text = item.lines[1].text:match("^%[([ xX])%][ \t]+(.*)$")
    local checkbox = ""
    if checked then
      item.lines[1] = { text = task_text, line = item.lines[1].line }
      inner = parse_blocks(item.lines, ctx)
      checkbox = string.format(
          '<input type="checkbox" disabled%s> ', checked == " " and "" or " checked")
    end
    if not loose then
      local trimmed = inner:gsub("^%s+", ""):gsub("%s+$", "")
      local single = trimmed:match("^<p data%-line=\"%d+\">(.-)</p>$")
      if single then
        inner = single .. "\n"
      end
    end
    out[#out + 1] = "<li>" .. checkbox .. inner .. "</li>\n"
  end
  out[#out + 1] = string.format("</%s>\n", kind)
  return j - i, table.concat(out)
end

local function try_indented_code(lines, i, ctx)
  local indent = leading_spaces(lines[i].text)
  if indent < 4 then
    return nil
  end
  local body = {}
  local j = i
  local last_line = lines[i].line
  while j <= #lines do
    local t = lines[j].text
    if is_blank(t) then
      -- A blank only keeps the block alive if more indented code follows.
      local k = j + 1
      while k <= #lines and is_blank(lines[k].text) do
        k = k + 1
      end
      if k <= #lines and leading_spaces(lines[k].text) >= 4 then
        body[#body + 1] = ""
        last_line = lines[j].line
        j = j + 1
      else
        break
      end
    elseif leading_spaces(t) >= 4 then
      body[#body + 1] = strip_indent(t, 4)
      last_line = lines[j].line
      j = j + 1
    else
      break
    end
  end
  return j - i, render_fence("", table.concat(body, "\n"), last_line, ctx)
end

local function starts_html_block(text)
  return text:match("^%s*<!--") ~= nil
      or text:match("^%s*<%?") ~= nil
      or text:match("^%s*<!%u") ~= nil
      or text:match("^%s*</?%a[%w%-]*[%s/>]") ~= nil
end

local function try_html_block(lines, i, ctx)
  if not starts_html_block(lines[i].text) then
    return nil
  end
  local body = {}
  local j = i
  while j <= #lines and not is_blank(lines[j].text) do
    body[#body + 1] = lines[j].text
    j = j + 1
  end
  return j - i, table.concat(body, "\n") .. "\n"
end

local function try_definition_list(lines, i, ctx)
  if is_blank(lines[i].text) or not lines[i + 1] then
    return nil
  end
  if not lines[i + 1].text:match("^%s*:%s+") and not lines[i + 1].text:match("^%s*~%s+") then
    return nil
  end
  if starts_block(lines[i].text) then
    return nil
  end
  local out = { string.format('<dl data-line="%d">\n', lines[i].line) }
  local j = i
  while j <= #lines do
    local term = lines[j].text
    if is_blank(term) then
      break
    end
    local defin = lines[j + 1] and lines[j + 1].text:match("^%s*[:~]%s+(.*)$")
    if not defin then
      break
    end
    out[#out + 1] = "<dt>" .. inline.render(term, ctx) .. "</dt>\n"
    out[#out + 1] = "<dd>" .. inline.render(defin, ctx) .. "</dd>\n"
    j = j + 2
  end
  out[#out + 1] = "</dl>\n"
  return j - i, table.concat(out)
end

starts_block = function(text)
  if is_blank(text) then
    return true
  end
  if text:match("^%s*([`~])%1%1") then
    return true
  end
  if text:match("^%s*#+[ \t]") or text:match("^%s*#+$") then
    return true
  end
  if is_thematic(text) then
    return true
  end
  if text:match("^%s*>") then
    return true
  end
  if list_marker(text) then
    return true
  end
  if starts_html_block(text) then
    return true
  end
  if text:match("^%s*|") or text:match("^%s*%S.*|.*|") then
    return true
  end
  return false
end

local function try_paragraph(lines, i, ctx)
  -- Setext heading: a single text line underlined with === or ---.
  local underline = lines[i + 1] and lines[i + 1].text:match("^%s*(=+)[ \t]*$")
      or lines[i + 1] and lines[i + 1].text:match("^%s*(%-+)[ \t]*$")
  if underline and not starts_block(lines[i].text) and not is_blank(lines[i].text) then
    local level = underline:find("=", 1, true) and 1 or 2
    return 2, heading_html(lines[i].text, level, lines[i].line, ctx)
  end

  local buf = { lines[i].text }
  local j = i + 1
  while j <= #lines and not is_blank(lines[j].text) and not starts_block(lines[j].text) do
    buf[#buf + 1] = lines[j].text
    j = j + 1
  end
  return j - i, string.format('<p data-line="%d">%s</p>\n',
                              lines[i].line, inline.render(table.concat(buf, "\n"), ctx))
end

parse_blocks = function(lines, ctx)
  local out = {}
  local i = 1
  local n = #lines
  while i <= n do
    if is_blank(lines[i].text) then
      i = i + 1
    else
      local consumed, html
      consumed, html = try_fence(lines, i, ctx)
      if not consumed then
        consumed, html = try_heading(lines, i, ctx)
      end
      if not consumed then
        consumed, html = try_thematic(lines, i, ctx)
      end
      if not consumed then
        consumed, html = try_blockquote(lines, i, ctx)
      end
      if not consumed then
        consumed, html = try_table(lines, i, ctx)
      end
      if not consumed then
        consumed, html = try_list(lines, i, ctx)
      end
      if not consumed then
        consumed, html = try_indented_code(lines, i, ctx)
      end
      if not consumed then
        consumed, html = try_html_block(lines, i, ctx)
      end
      if not consumed then
        consumed, html = try_definition_list(lines, i, ctx)
      end
      if not consumed then
        consumed, html = try_paragraph(lines, i, ctx)
      end
      out[#out + 1] = html or ""
      i = i + (consumed or 1)
    end
  end
  return table.concat(out)
end

local function split_lines(text, first_line)
  local lines = {}
  local line_no = (first_line or 1) - 1
  local normalized = text:gsub("\r\n", "\n"):gsub("\r", "\n")
  for raw in (normalized .. "\n"):gmatch("(.-)\n") do
    line_no = line_no + 1
    lines[#lines + 1] = { text = raw, line = line_no }
  end
  return lines
end

-- Pulls link reference definitions and footnote definitions out of the
-- document, returning the remaining lines plus the footnote list.
local function extract_definitions(lines, ctx)
  local kept = {}
  local footnotes = {}
  local i = 1
  while i <= #lines do
    local text = lines[i].text
    local label, href, title = text:match("^%s*%[([^%]^]+)%]:%s*(%S+)%s*(.*)$")
    if label then
      title = title:match('^"(.*)"%s*$') or title:match("^'(.*)'%s*$") or title:match("^%((.*)%)%s*$")
      ctx.refs[label:lower()] = { href = href, title = title }
      i = i + 1
    else
      local fn_id, fn_text = text:match("^%s*%[%^([^%]]+)%]:%s*(.*)$")
      if fn_id then
        local body = { fn_text }
        local j = i + 1
        while j <= #lines do
          local t = lines[j].text
          if is_blank(t) then
            local k = j
            while k + 1 <= #lines and is_blank(lines[k + 1].text) do
              k = k + 1
            end
            if k + 1 <= #lines and leading_spaces(lines[k + 1].text) >= 2 then
              body[#body + 1] = ""
              j = j + 1
            else
              break
            end
          elseif leading_spaces(t) >= 2 then
            body[#body + 1] = strip_indent(t, math.min(4, leading_spaces(t)))
            j = j + 1
          else
            break
          end
        end
        footnotes[#footnotes + 1] = { id = fn_id, text = table.concat(body, "\n"), line = lines[i].line }
        ctx.footnote_ids[fn_id] = true
        i = j
      else
        kept[#kept + 1] = lines[i]
        i = i + 1
      end
    end
  end
  for index, fn in ipairs(footnotes) do
    ctx.footnote_numbers[fn.id] = index
  end
  return kept, footnotes
end

-- Renders a whole markdown document.
-- `opts.options` is the resolved option table; `opts.images_prefix` an optional
-- prefix for relative image URLs. Returns { html, toc, headings, title }.
function M.render(text, opts)
  opts = opts or {}
  local ctx = {
    refs = {},
    options = opts.options or {},
    images_prefix = opts.images_prefix,
    footnote_ids = {},
    footnote_numbers = {},
    headings = {},
    slug_state = {},
  }

  local lines = split_lines(text or "", 1)
  local kept, footnotes = extract_definitions(lines, ctx)
  local body = parse_blocks(kept, ctx)

  if #footnotes > 0 then
    local parts = { '<section class="footnotes">\n<ol>\n' }
    for _, fn in ipairs(footnotes) do
      local inner = parse_blocks(split_lines(fn.text, fn.line), ctx)
      parts[#parts + 1] = string.format(
          '<li id="fn-%s" data-line="%d">%s <a href="#fnref-%s" class="footnote-backref">↩</a></li>\n',
          inline.escape(fn.id), fn.line, inner, inline.escape(fn.id))
    end
    parts[#parts + 1] = "</ol>\n</section>\n"
    body = body .. table.concat(parts)
  end

  return {
    html = body,
    toc = toc.render(ctx.headings),
    headings = ctx.headings,
    title = ctx.headings[1] and ctx.headings[1].text or nil,
  }
end

return M
