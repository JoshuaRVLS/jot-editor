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

-- Nerd-font icon per diagnostic severity. Signature, doc and code sections
-- stay icon-free so the popup reads as plain tooltip text; only diagnostics
-- (which genuinely benefit from a severity marker) keep a glyph. Kept in one
-- table so themes/users can swap the set without touching the layout code.
local ICONS = {
  error = "",
  warning = "",
  info = "",
  hint = "",
}

-- Copy button label, pinned to the top-right of the popup and swapping while
-- the "copied" state lasts. Plain ASCII on purpose: the previous glyphs were
-- Material Design Nerd Font icons from the Private Use Area, which most
-- terminal fonts do not carry -- they rendered as question marks -- and they
-- were four bytes wide while the geometry below assumed three, so the
-- highlight span started inside the character.
local ICON_COPY = "copy"
local ICON_CHECK = "ok"
-- Removed at the user's request: the hover no longer carries a copy button, so
-- nothing is reserved for it. Kept at 0 rather than deleting the constant so
-- the wrap and width arithmetic below keep their shape.
local COPY_RESERVE = 0

local win = nil -- current float handle (0 when none)
local buf = nil -- current scratch buffer handle
-- Copy-button interaction state (resets on every present/close):
--   copy_state  "idle" | "copied" (checklist icon while copied)
--   copy_hover  pointer rests on the button cell
--   last_button_col / last_button_start / first_line_base / line1_content_spans
--   hold the geometry + colors of the previous present() so the button can be
--   redrawn in place (icon swap, hover highlight) without rebuilding the float.
local copy_state = "idle"
local copy_hover = false
local last_button_col = -1
local last_button_start = -1
local first_line_base = ""
local line1_content_spans = {}
local last_fg = 7
local last_footer_fg = 7
local last_info_fg = nil

local function close_float()
  if win and win ~= 0 then
    jot.ui.float.close(win, true)
  end
  win = nil
  if buf and buf ~= 0 then
    jot.ui.buffer.delete(buf)
  end
  buf = nil
  copy_state = "idle"
  copy_hover = false
  last_button_col = -1
  last_button_start = -1
end

-- Re-renders just the copy button (icon + color) on the already-open float.
local function refresh_button()
  if not win or win == 0 or not buf or buf == 0 or last_button_start < 0 then
    return
  end
  local icon = copy_state == "copied" and ICON_CHECK or ICON_COPY
  jot.ui.buffer.set_lines(buf, 0, 1, true, { first_line_base .. " " .. icon })
  local spans = {}
  for _, sp in ipairs(line1_content_spans) do
    spans[#spans + 1] = sp
  end
  local fg = copy_state == "copied" and (last_info_fg or last_fg) or (copy_hover and last_fg or last_footer_fg)
  
  jot.ui.float.set_spans(win, 1, spans)
  -- Flush the repaint immediately so the icon swap is visible without
  -- waiting for the next input event (GUI repaints every frame anyway).
  if jot.pane and jot.pane.redraw then
    pcall(jot.pane.redraw)
  elseif jot.ui and jot.ui.redraw then
    pcall(jot.ui.redraw)
  end
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
  -- Strip inline markers; keep the text between them. Underscores between
  -- word characters (snake_case identifiers in diagnostics) are protected
  -- first so plain-text messages survive the markdown-italic strip intact.
  ln = ln:gsub("(%w)_(%w)", "%1\1%2")
  ln = ln:gsub("`", ""):gsub("%*%*", ""):gsub("%*", ""):gsub("_", "")
  ln = ln:gsub("\1", "_")
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

-- VSCode-style sectioning: splits logical lines into
--   { kind = "signature"|"doc"|"code"|"diagnostic"|"blank", text, code, ext }
-- The first fenced-free block is the signature (declaration servers echo),
-- diagnostics-looking lines (error/warning/error codes) lead their own
-- section, code fences keep their language for highlighting.
local function looks_diagnostic(text)
  local low = text:lower()
  if low:match("^error") or low:match("^e%d+") or low:match("%[e%d+%]") then
    return "error"
  end
  if low:match("^warning") or low:match("^w%d+") or low:match("%[w%d+%]") then
    return "warning"
  end
  if low:match("^info") or low:match("^note") or low:match("^help") then
    return "info"
  end
  if low:match("^hint") then
    return "hint"
  end
  return nil
end

local function build_sections(contents)
  local sections = {}
  local seen_code = false
  local seen_text = false
  for _, logical in ipairs(split_logical(contents)) do
    if logical.text == "" then
      sections[#sections + 1] = { kind = "blank", text = "", code = false, ext = "" }
    elseif logical.code then
      seen_code = true
      sections[#sections + 1] =
        { kind = "code", text = logical.text, code = true, ext = logical.ext }
    else
      local sev = looks_diagnostic(logical.text)
      if sev then
        sections[#sections + 1] =
          { kind = "diagnostic", severity = sev, text = logical.text, code = false, ext = "" }
      elseif not seen_text and not seen_code then
        seen_text = true
        sections[#sections + 1] =
          { kind = "signature", text = logical.text, code = false, ext = "" }
      else
        seen_text = true
        sections[#sections + 1] =
          { kind = "doc", text = logical.text, code = false, ext = "" }
      end
    end
  end
  -- Collapse leading/trailing blanks; keep single blanks between sections.
  while #sections > 0 and sections[1].kind == "blank" do
    table.remove(sections, 1)
  end
  while #sections > 0 and sections[#sections].kind == "blank" do
    sections[#sections] = nil
  end
  return sections
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

-- Sectioned display: like build_display but keeps the VSCode-style section
-- layout (signature line, doc prose, code fences, diagnostics) and returns
-- per-line spans, per-line section kinds, and per-line severities:
--   lines, spans, kinds, sevs = build_sectioned(contents, colors)
-- kinds[line] = "signature"|"doc"|"code"|"diagnostic"|"blank". Only
-- diagnostic sections carry a severity-icon prefix (folded into the line
-- text); present() paints that icon span in the severity accent color.
local function build_sectioned(contents, colors)
  local lines = {}
  local spans = {}
  local kinds = {}
  local sevs = {}
  for _, sec in ipairs(build_sections(contents)) do
    if sec.kind == "blank" then
      local idx = #lines + 1
      lines[idx] = ""
      kinds[idx] = "blank"
    else
      -- Only diagnostics get a severity glyph; signature / doc / code stay
      -- icon-free so the popup reads as plain tooltip text.
      local icon = sec.kind == "diagnostic" and ICONS[sec.severity] or nil
      local prefix = icon and (icon .. " ") or ""
      local pad = icon and 4 or 0 -- 3-byte icon + 1 space, or no indent
      local wrapped =
        wrap_line(sec.text, HOVER_MAX_WIDTH - COPY_RESERVE - (icon and 4 or 0))
      for wi, wl in ipairs(wrapped) do
        local idx = #lines + 1
        lines[idx] = prefix .. wl
        kinds[idx] = sec.kind
        if sec.severity then
          sevs[idx] = sec.severity
        end
        if wi == 1 and icon then
          -- Icon span covers the icon + trailing space (byte offsets: the
          -- icons above are 3 bytes in UTF-8, +1 for the space).
          spans[idx] = { { start = 0, len = 4, fg = -2 } }
        end
        if sec.code then
          local s = highlight_line(wl, sec.ext, colors)
          for _, sp in ipairs(s) do
            spans[idx] = spans[idx] or {}
            -- highlight_line returns byte offsets into wl; shift past the
            -- indent so spans line up with lines[idx]. First lines of a
            -- diagnostic carry the 3-byte icon + 1 space; everything else
            -- has no prefix.
            spans[idx][#spans[idx] + 1] = { start = sp.start + pad, len = sp.len, fg = sp.fg }
          end
        end
      end
    end
  end
  if #lines == 0 then
    lines[1] = ""
    kinds[1] = "blank"
  end
  return lines, spans, kinds, sevs
end

local function present(info)
  close_float()
  if not info or not info.contents or info.contents == "" then
    return false
  end

  local ui = info.ui or {}
  local lines, spans, kinds, sevs = build_sectioned(info.contents, info.colors)
  -- Resolve the -2 placeholder on icon spans to the severity accent color
  -- (only diagnostic lines carry icon spans).
  local sev_accent = {
    error = ui.error,
    warning = ui.warning,
    info = ui.info,
    hint = ui.hint,
  }
  for line_idx, s in pairs(spans) do
    local fg = info.fg or 7
    if kinds[line_idx] == "diagnostic" then
      fg = sev_accent[sevs[line_idx] or ""] or fg
    end
    for _, sp in ipairs(s) do
      if sp.fg == -2 then
        sp.fg = fg
      end
    end
  end
  local width = 0
  for _, ln in ipairs(lines) do
    local w = visual_len(ln)
    if w > width then
      width = w
    end
  end
  width = math.min(HOVER_MAX_WIDTH - COPY_RESERVE, math.max(1, width))

  local total = #lines
  local shown = math.min(total, HOVER_MAX_ROWS)
  local body = {}
  for i = 1, shown do
    body[i] = lines[i]
  end

  -- Copy button pinned to the top-right interior cell: pad the first line to
  -- `width`, then a gap and the icon. The padded base line and the button's
  -- byte offset are remembered so refresh_button() can swap the icon in place.
  local fg = info.fg or 7
  local bg = info.bg or 0
  local border_fg = info.border_fg or info.border or fg
  local footer_fg = (info.colors and info.colors.comment) or fg
  local first_len = visual_len(body[1] or "")
  first_line_base = (body[1] or "") .. string.rep(" ", math.max(0, width - first_len))
  -- No button column, so the pointer can never be "over" it and the copy
  -- action is unreachable.
  last_button_start = -1
  last_button_col = -1
  last_fg = fg
  last_footer_fg = footer_fg
  last_info_fg = (info.ui and info.ui.info) or nil

  buf = jot.ui.buffer.create(false, true)
  jot.ui.buffer.set_lines(buf, 0, -1, true, body)

  local h = shown + 2
  local w = width + 2 + COPY_RESERVE
  -- jot.ui.float.open(buffer, config): the enter flag is not part of this
  -- binding (config is argument #2); focusable=false keeps it display-only.
  win = jot.ui.float.open(buf, {
    col = info.anchor_x or 0,
    -- Open one row below the anchor line (the hovered code line / cursor
    -- row) instead of on it: starting the frame on the hovered row cuts that
    -- line mid-text and, when the server echoes the declaration it was asked
    -- about (header files), the same code appears again directly beneath -
    -- reading as a duplicated line. Keeping the anchor row fully visible
    -- above the frame makes the box read as a tooltip. When it does not fit
    -- below, the float compositor shifts the whole frame up to the editor
    -- bottom (whole rows only, never a mid-row cut).
    row = (info.anchor_y or -1) + 1,
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
  -- The popup is interactive: it consumes every mouse event over its area
  -- (hovering it never falls through to the editor below) and the copy
  -- button copies the raw hover contents to the clipboard, swapping its icon
  -- to a checklist for a moment.
  local function mouse_handler(ev)
    local over_button = ev.row == 1 and ev.col == last_button_col
    if ev.motion then
      if over_button ~= copy_hover then
        copy_hover = over_button
        refresh_button()
      end
      return true
    end
    if over_button and ev.pressed and copy_state ~= "copied" then
      copy_state = "copied"
      if jot.clipboard and jot.clipboard.set then
        jot.clipboard.set(info.contents or "")
      end
      refresh_button()
      if jot.timer and jot.timer.set_timeout then
        pcall(jot.timer.set_timeout, 1600, function()
          if copy_state == "copied" then
            copy_state = "idle"
            refresh_button()
          end
        end)
      end
      return true
    end
    return true -- consume everything over the tooltip
  end
  jot.ui.float.on_mouse(win, mouse_handler)
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
  build_sections = build_sections,
  build_sectioned = build_sectioned,
  wrap_line = wrap_line,
  clean_line = clean_line,
  lang_to_ext = lang_to_ext,
  present = present,
  close = close_float,
  -- Copy-button state for tests: { copied = bool, hover = bool }.
  get_state = function()
    return { copied = copy_state == "copied", hover = copy_hover }
  end,
}