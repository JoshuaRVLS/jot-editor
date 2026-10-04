-- Highlights TODO-style comments - a port of folke/todo-comments.nvim, rebuilt
-- on jot's own APIs: decorations for the paint, the syntax highlighter for the
-- "only in comments" gate, the workspace walk for the search.
--
-- Disable with:   jot.config.set("todo_comments", false)
-- Extra keywords: jot.config.set("todo_comments_keywords", "SECURITY,REVIEW")
--                 (comma separated; each takes the default colour)
--
-- Alt+] t / Alt+[ t jump to the next/previous comment (the chord joins the
-- Alt+]/Alt+[ jump family, but is registered here because :TodoNext/:TodoPrev
-- are this feature's own commands).
-- :Todo lists them across the workspace, each row inked in its comment's
-- family colour; `keywords=TODO,FIX` filters it the way upstream's
-- `:TodoTelescope keywords=...` does.
--
-- The matched keyword wears a chip in its family's colour and nothing else:
-- the comment marker and the space before the word keep the comment's own ink.
-- The colon is hidden (painted in the theme's own background ink, so it stays
-- a real character), and the text after it takes the family ink. The colours
-- are resolved from the live theme at paint time, so a colourscheme switch
-- re-inks the chips instead of leaving the old palette on screen.
--
-- The comment gate uses jot.syntax.highlight, the regex fallback the renderer
-- keeps for languages without a tree-sitter grammar, so a keyword is only
-- painted when that line's own spans say comment. Two limits come with it: an
-- extension the fallback has no rules for fails open (the keyword is banded
-- anywhere, which is where upstream stands without a parser), and a multi-line
-- block comment's continuation lines are not recognized, because the fallback
-- sees one line at a time. The fail-open band does not carry: the family ink
-- runs into the lines below only while those lines' own spans call them
-- comments, so an unknown extension never re-inks the code under a TODO.

local jot = jot

-- This feature deletes only the decorations it created: jot.decoration.clear()
-- wipes every decoration on the buffer, which would take the diagnostics'
-- squiggles (or, the other way round, wipe these bands the moment the language
-- server publishes).
local owned = {}

-- Buffers already painted. CursorMoved fires on every key without the text
-- changing, so the first sight of a buffer is worth a paint and the rest are
-- left to BufChange (or to a config change, which forces).
local seen = {}

-- The last (enabled, keywords) pair the feature painted for. CursorMoved only
-- re-reads a buffer when this moved: the settings menu re-applies config live
-- but fires no event of its own, so the next cursor move is where a toggle
-- takes effect.
local last_signature = nil

-- Buffer -> last path painted, so a theme switch can repaint the buffers that
-- already carry bands without waiting for each one to be touched.
local paths = {}

local ENABLED_KEY = "todo_comments"
local KEYWORDS_KEY = "todo_comments_keywords"

-- Upstream's default keywords, each family mapping to a colour name. A custom
-- keyword added with todo_comments_keywords joins the `default` family.
local FAMILIES = {
  { key = "FIX", color = "error", alt = { "FIXME", "BUG", "FIXIT", "ISSUE" } },
  { key = "TODO", color = "info" },
  { key = "HACK", color = "warning" },
  { key = "WARN", color = "warning", alt = { "WARNING", "XXX" } },
  { key = "PERF", color = "default", alt = { "OPTIM", "PERFORMANCE", "OPTIMIZE" } },
  { key = "NOTE", color = "hint", alt = { "INFO" } },
  { key = "TEST", color = "test", alt = { "TESTING", "PASSED", "FAILED" } },
}

-- Colour name -> jot theme group. The diagnostic groups are the theme's own
-- ink for error/warning/info/hint; `default` (upstream's violet, what a custom
-- keyword gets) and `test` (upstream's magenta) take the two syntax inks that
-- stand nearest, so every family still comes from the theme.
local FAMILY_GROUP = {
  error = "diagnostic_error",
  warning = "diagnostic_warning",
  info = "diagnostic_info",
  hint = "diagnostic_hint",
  default = "type",
  test = "keyword",
}

-- Upstream ignores very long lines (highlight.max_line_len): a minified file
-- is one 50k line, and scanning it per keystroke is the whole cost for nothing.
local MAX_LINE_LEN = 400

-- Above the C/C++ dim (priority 1), below the diagnostic squiggle (10).
local BAND_PRIORITY = 8
local TEXT_PRIORITY = 7

local function enabled()
  return jot.config.get(ENABLED_KEY, "true") ~= "false"
end

local function extra_keywords()
  return tostring(jot.config.get(KEYWORDS_KEY, "") or "")
end

local function signature()
  return tostring(enabled()) .. "|" .. extra_keywords()
end

-- The keyword -> colour family map: upstream's defaults, plus whatever
-- todo_comments_keywords adds (merge_keywords, upstream calls it).
local function configured_keywords()
  local map = {}
  for _, family in ipairs(FAMILIES) do
    map[family.key] = family.color
    for _, alt in ipairs(family.alt or {}) do
      map[alt] = family.color
    end
  end
  for name in extra_keywords():gmatch("[^,%s]+") do
    if map[name] == nil then
      map[name] = "default"
    end
  end
  return map
end

-- Keywords are matched as Lua patterns, so any magic character in a custom
-- keyword (unlikely, but the setting is free text) has to be escaped.
local function pattern_escape(text)
  return (tostring(text or ""):gsub("([%%%^%$%(%)%.%[%]%*%+%-%?])", "%%%1"))
end

-- The keyword set as a matcher. Lua patterns have no alternation: the `(A|B)`
-- upstream's regex uses matches a literal `|`, so a pattern built by joining
-- the keywords would never match anything. Instead the line is scanned for
-- `word%f...:` candidates and the word is looked up in `names` - one pass per
-- line, and `%f[%w_]` is the boundary upstream's `<` spells, so `mytodo:` is
-- not a hit. A custom keyword with characters outside `[%w_]` (the setting is
-- free text) cannot be captured that way, so it gets a pattern of its own and
-- both kinds compete for the earliest match.
local function compile_matcher(keywords)
  local names, special, count = {}, {}, 0
  for keyword in pairs(keywords) do
    count = count + 1
    if keyword:match("^[%w_]+$") then
      names[keyword] = true
    else
      special[#special + 1] = {
        pattern = "%f[%w_]" .. pattern_escape(keyword) .. "%s*:",
        keyword = keyword,
      }
    end
  end
  if count == 0 then
    return nil
  end
  -- Longest first so FIXME is preferred over FIX when both sit at one start.
  table.sort(special, function(a, b)
    if #a.keyword ~= #b.keyword then
      return #a.keyword > #b.keyword
    end
    return a.keyword < b.keyword
  end)
  return { names = names, special = special }
end

-- The first whole word on the line that is a keyword and is followed by a
-- colon, as `start1, finish1, keyword`. `finish1` is the 1-based end of the
-- colon (whitespace before it included), which is what the band length needs.
local function find_word(line, names, at)
  local start1, finish1, word = line:find("%f[%w_]([%w_]+)%s*:", at)
  while start1 do
    if names[word] then
      return start1, finish1, word
    end
    start1, finish1, word = line:find("%f[%w_]([%w_]+)%s*:", start1 + 1)
  end
  return nil
end

local function find_match(line, matcher)
  if not matcher then
    return nil
  end
  local start1, finish1, keyword = find_word(line, matcher.names, 1)
  for _, entry in ipairs(matcher.special) do
    local s, f = line:find(entry.pattern)
    if s and (not start1 or s < start1 or (s == start1 and f > finish1)) then
      start1, finish1, keyword = s, f, entry.keyword
    end
  end
  return start1, finish1, keyword
end

-- Returns keyword, colour family, keyword start (0-based) and the colon's
-- offset (0-based). The colon is not part of the band: it keeps the comment's
-- own ink, so what is painted is the keyword alone.
local function match_line(line, matcher, keywords)
  if not matcher or line == "" or #line > MAX_LINE_LEN then
    return nil
  end
  local start1, finish1, keyword = find_match(line, matcher)
  if not start1 then
    return nil
  end
  return keyword, keywords[keyword] or "default", start1 - 1, finish1 - 1
end

-- The byte offset the comment holding `col` starts at, or false when the line
-- has spans and none of them is a comment there. nil means the editor cannot
-- tell (no rules for the extension); callers fail open, as upstream does with
-- no tree-sitter parser.
local function comment_start_at(ext, line, col)
  if ext == "" or type(jot.syntax) ~= "table" or type(jot.syntax.highlight) ~= "function" then
    return nil
  end
  local ok, spans = pcall(jot.syntax.highlight, ext, line)
  if not ok or type(spans) ~= "table" then
    return nil
  end
  if spans.rules == false then
    return nil
  end
  for _, span in ipairs(spans) do
    if span.kind == "comment" and col >= span.start and col < span.start + span.len then
      return span.start
    end
  end
  return false
end

local function extension_of(path)
  local ext = tostring(path or ""):match("(%.[%w_+]+)$")
  return ext or ""
end

--------------------------------------------------------------------- colours

-- "#rrggbb" -> r, g, b, or nil when the text is not a colour.
local function hex_rgb(text)
  local r, g, b = tostring(text or ""):match("^#(%x%x)(%x%x)(%x%x)$")
  if not r then
    return nil
  end
  return tonumber(r, 16), tonumber(g, 16), tonumber(b, 16)
end

local function relative_luminance(r, g, b)
  local function channel(value)
    local c = value / 255
    if c <= 0.03928 then
      return c / 12.92
    end
    return ((c + 0.055) / 1.055) ^ 2.4
  end
  return 0.2126 * channel(r) + 0.7152 * channel(g) + 0.0722 * channel(b)
end

-- The ink a band's text is written in: whichever of the theme's normal text
-- and background contrasts more with the band (upstream's maximize_contrast).
local function readable_ink(band_hex, normal)
  local r, g, b = hex_rgb(band_hex)
  if not r then
    return nil
  end
  local band_luminance = relative_luminance(r, g, b)
  local candidates = {
    normal and normal.fg or "#ffffff",
    normal and normal.bg or "#000000",
  }
  local best = candidates[1]
  local best_ratio = -1
  for _, candidate in ipairs(candidates) do
    local cr, cg, cb = hex_rgb(candidate)
    if cr then
      local candidate_luminance = relative_luminance(cr, cg, cb)
      local low, high = band_luminance, candidate_luminance
      if low > high then
        low, high = high, low
      end
      local ratio = (high + 0.05) / (low + 0.05)
      if ratio > best_ratio then
        best_ratio = ratio
        best = candidate
      end
    end
  end
  return best
end

-- Colour family -> { fg = band ink, ink = text written on the band }, resolved
-- from the live theme. A family whose group the theme does not name is left
-- out rather than painted with an invented colour. The second value is the
-- theme's own background: painting the colon in it hides the colon, while the
-- character stays in the buffer (backspace still deletes it).
local function family_accents()
  local normal = nil
  local accents = {}
  for family, group in pairs(FAMILY_GROUP) do
    local ok, resolved = pcall(jot.theme.get, group)
    if ok and type(resolved) == "table" and type(resolved.fg) == "string" then
      if not normal then
        local nok, colors = pcall(jot.theme.get, "normal")
        if nok and type(colors) == "table" then
          normal = colors
        end
      end
      accents[family] = { fg = resolved.fg, ink = readable_ink(resolved.fg, normal) }
    end
  end
  return accents, normal and normal.bg or nil
end

---------------------------------------------------------------- decorations

local function release(buffer)
  local ids = owned[buffer]
  if not ids then
    return
  end
  for _, id in ipairs(ids) do
    jot.decoration.delete(buffer, id)
  end
  owned[buffer] = {}
end

local function keep(buffer, id)
  if type(id) == "number" and id ~= 0 then
    local ids = owned[buffer]
    if not ids then
      ids = {}
      owned[buffer] = ids
    end
    ids[#ids + 1] = id
  end
end

-- Turns the lines of one buffer into decoration specs. Pure but for the comment
-- gate, which asks the syntax highlighter per matching line. `conceal` is the
-- ink the colon is painted in (the theme's own background, so it disappears).
--
-- `lines` is an array of strings (the unit tests) or an iterator returning
-- `index, text` (the editor's own scan): both yield the same specs, and the
-- iterator is what keeps a huge buffer from living in a Lua table while it is
-- scanned.
local function scan_lines(lines, ext, keywords, matcher, accents, conceal)
  local specs = {}
  -- The accent a match carries into the following comment lines (upstream's
  -- multiline): set by a painted keyword, cleared when the run or the comment
  -- ends.
  local carried = nil
  local next_line
  if type(lines) == "function" then
    next_line = lines
  else
    local at = 0
    next_line = function()
      at = at + 1
      local line = lines[at]
      if line == nil then
        return nil
      end
      return at, line
    end
  end
  while true do
    local index, line = next_line()
    if index == nil then
      break
    end
    local keyword, family, start0, colon0 = match_line(line, matcher, keywords)
    if keyword then
      local comment_start = comment_start_at(ext, line, start0)
      local accent = accents[family]
      if comment_start ~= false and accent then
        -- The chip is the keyword alone: no comment marker, no space before
        -- the word and no colon (see the conceal below).
        specs[#specs + 1] = {
          row = index,
          col = start0 + 1,
          width = colon0 - start0,
          bg = accent.fg,
          fg = accent.ink,
          priority = BAND_PRIORITY,
        }
        if conceal then
          specs[#specs + 1] = {
            row = index,
            col = colon0 + 1,
            width = 1,
            fg = conceal,
            priority = TEXT_PRIORITY,
          }
        end
        local text_start = colon0 + 1
        if text_start < #line then
          specs[#specs + 1] = {
            row = index,
            col = text_start + 1,
            width = #line - text_start,
            fg = accent.fg,
            priority = TEXT_PRIORITY,
          }
        end
        carried = { accent = accent, col = start0 }
      else
        carried = nil
      end
    -- The run continues only into a line the highlighter itself confirms is a
    -- comment (a start offset; 0 is truthy in Lua). A false answer means the
    -- line's spans hold no comment, and nil means the extension has no rules
    -- to tell: either way the run stops. The keyword's own fail-open band is
    -- one word, but a carried run repaints a whole line, and "we cannot tell"
    -- must not ink the code below every TODO.
    elseif carried and line ~= "" and comment_start_at(ext, line, carried.col) then
      specs[#specs + 1] = {
        row = index,
        col = 1,
        width = #line,
        fg = carried.accent.fg,
        priority = TEXT_PRIORITY,
      }
    else
      carried = nil
    end
  end
  return specs
end

--------------------------------------------------------------- buffer reads

-- jot.buffer.get_line is 1-based and takes the buffer. The buffer text is
-- empty while a file is still being opened (BufOpen fires before the text is
-- in), so the caller falls back to reading the file from disk.
local function current_id()
  local ok, value = pcall(jot.buffer.current)
  if not ok or value == nil then
    return nil
  end
  if type(value) == "number" then
    return value
  end
  if type(value) == "table" then
    return value.id or value.buffer or value.index
  end
  return nil
end

local function current_path()
  local candidates = {}
  if jot.buffer and type(jot.buffer.current_file) == "function" then
    candidates[#candidates + 1] = jot.buffer.current_file
  end
  if type(jot.current_file) == "function" then
    candidates[#candidates + 1] = jot.current_file
  end
  if jot.editor and type(jot.editor.current_file) == "function" then
    candidates[#candidates + 1] = jot.editor.current_file
  end
  for _, fn in ipairs(candidates) do
    local ok, value = pcall(fn)
    if ok and type(value) == "string" and value ~= "" then
      return value
    end
  end
  return ""
end

-- The lines of one text as an iterator returning `index, line`. The scan used
-- to hand every line to scan_lines as an array, so the buffer lived twice in
-- Lua for the whole pass: the joined text plus one string per line. A 200k-line
-- buffer therefore cost tens of MB on every open and every edit. One line is
-- alive at a time here instead.
local function text_line_iter(text)
  local at = 1
  local index = 0
  return function()
    if at > #text then
      return nil
    end
    local nl = text:find("\n", at, true)
    local line
    if nl then
      line = text:sub(at, nl - 1)
      at = nl + 1
    else
      line = text:sub(at)
      at = #text + 1
    end
    index = index + 1
    return index, line
  end
end

-- The same iterator over the buffer's own lines, one per-line read at a time.
-- Line 1 is read up front so a buffer whose load is still racing the open (line
-- 1 nil) can be told apart from an empty one, and handed back already primed:
-- the probe is a read the scan would have paid for anyway.
local function buffer_line_iter(buffer)
  local first = jot.buffer.get_line(1, buffer)
  if first == nil then
    return nil
  end
  local primed = tostring(first)
  local index = 0
  return function()
    index = index + 1
    if index == 1 then
      return 1, primed
    end
    local text = jot.buffer.get_line(index, buffer)
    if text == nil then
      return nil
    end
    return index, tostring(text)
  end
end

------------------------------------------------------------ the pending jump

-- A :Todo pick opens a file that may still be on the worker thread, so the
-- target is remembered and applied when its BufOpen arrives. A file that was
-- already open fires no BufOpen, so the jump is attempted at once as well. The
-- walk's path is spelled through the workspace root while the opened buffer's
-- is canonical, so the relative path is kept as the fallback comparison.
local pending_jump = nil

local function same_file(current, pending)
  if current == "" then
    return false
  end
  if current == pending.path then
    return true
  end
  local rel = pending.relative or ""
  if rel == "" or #current < #rel or current:sub(-#rel) ~= rel then
    return false
  end
  local before = current:sub(-#rel - 1, -#rel - 1)
  return before == "/" or before == ""
end

local function land_pending_jump()
  if not pending_jump then
    return
  end
  if same_file(current_path(), pending_jump) then
    jot.cursor.set(pending_jump.line, pending_jump.column)
    pending_jump = nil
  end
end

------------------------------------------------------------------- the pass

local retries = 0
local retry = function() end

local function apply(info, force)
  local buffer = info and info.buffer
  if not buffer or buffer < 0 then
    retry()
    return
  end
  local sig = signature()
  if not force and seen[buffer] and sig == last_signature then
    return
  end
  local path = (info and (info.filepath or info.path)) or ""
  if path == "" then
    path = current_path()
  end
  if not enabled() then
    release(buffer)
    paths[buffer] = path
    seen[buffer] = true
    last_signature = sig
    return
  end
  local lines = buffer_line_iter(buffer)
  if not lines and path ~= "" then
    -- BufOpen raced the load: read the file so the first paint still shows the
    -- comments, and retry shortly for the buffer's own text.
    local ok, text = pcall(jot.file.read, path)
    if ok and type(text) == "string" and text ~= "" then
      lines = text_line_iter(text)
    end
  end
  if not lines then
    retry()
    return
  end
  retries = 0

  local keywords = configured_keywords()
  local accents, conceal = family_accents()
  local specs = scan_lines(lines,
                           extension_of(path),
                           keywords,
                           compile_matcher(keywords),
                           accents,
                           conceal)
  paths[buffer] = path
  seen[buffer] = true
  last_signature = sig
  release(buffer)
  for _, spec in ipairs(specs) do
    keep(buffer, jot.decoration.set(buffer, spec))
  end
end

retry = function()
  if retries >= 8 or type(jot.set_timeout) ~= "function" then
    return
  end
  retries = retries + 1
  local function run()
    local id = current_id()
    if id then
      apply({ buffer = id, path = current_path() }, false)
    end
  end
  if not pcall(jot.set_timeout, run, 120) then
    pcall(jot.set_timeout, 120, run)
  end
end

-- The text only moves on an edit, so a cursor move is worth a rescan once per
-- buffer (or after a config change) and never again.
jot.autocmd("CursorMoved", function(info)
  apply(info, false)
end)
jot.autocmd("BufChange", function(info)
  apply(info, true)
end)
-- A file arriving is the moment the bands should show up, without touching
-- anything, so this one is worth the retry loop. The pending jump from :Todo
-- lands here too when the file still had to be read.
jot.autocmd("BufOpen", function()
  apply({ buffer = current_id(), path = current_path() }, true)
  land_pending_jump()
  retry()
end)

------------------------------------------------------------------- commands

local function jump(direction)
  local buffer = current_id()
  if not buffer then
    return false
  end
  if jot.buffer.get_line(1, buffer) == nil then
    return false
  end
  local ext = extension_of(current_path())
  local keywords = configured_keywords()
  local matcher = compile_matcher(keywords)
  local ok, row = pcall(jot.cursor.get)
  if not ok or type(row) ~= "number" then
    row = 1
  end
  -- Reads one line at a time, in the jump's direction, until the buffer runs
  -- out: the walk never needs the whole buffer in memory.
  local index = direction > 0 and row + 1 or row - 1
  while index >= 1 do
    local text = jot.buffer.get_line(index, buffer)
    if text == nil then
      break
    end
    local line = tostring(text)
    local keyword, _, start0 = match_line(line, matcher, keywords)
    if keyword and comment_start_at(ext, line, start0) ~= false then
      jot.cursor.set(index, start0 + 1)
      return true
    end
    index = index + direction
  end
  jot.notify("No more todo comments to jump to")
  return false
end

jot.command("TodoNext", function()
  jump(1)
end, "Next todo comment")
jot.command("TodoPrev", function()
  jump(-1)
end, "Previous todo comment")

-- `keywords=TODO,FIX` (upstream's spelling), comma separated and
-- case-sensitive. Anything else in the argument is ignored.
local function keyword_filter(arg)
  local filter = nil
  for key, value in tostring(arg or ""):gmatch("([%w_]+)%s*=%s*([^%s]+)") do
    if key == "keywords" then
      filter = {}
      for name in value:gmatch("[^,]+") do
        filter[name] = true
      end
    end
  end
  return filter
end

local function workspace_items(filter)
  local keywords = configured_keywords()
  local matcher = compile_matcher(keywords)
  if not matcher then
    return {}
  end
  local items = {}
  local lines_seen = {}
  for keyword in pairs(keywords) do
    if not filter or filter[keyword] then
      local ok, results = pcall(jot.workspace.search, keyword .. ":", 1000)
      if ok and type(results) == "table" then
        for _, result in ipairs(results) do
          local line = tostring(result.text or "")
          -- The walk is case-insensitive and has no word boundary, so a line
          -- only becomes a row when the real highlight pattern (and the
          -- comment gate) accepts it. This is also what keeps a TODO inside a
          -- string out of the list.
          local hit, family, start0 = match_line(line, matcher, keywords)
          if hit and comment_start_at(extension_of(result.path), line, start0) ~= false then
            local dedupe = tostring(result.path) .. ":" .. tostring(result.line)
            if not lines_seen[dedupe] then
              lines_seen[dedupe] = true
              items[#items + 1] = {
                path = tostring(result.path),
                relative = tostring(result.relative_path or result.path),
                line = tonumber(result.line) or 1,
                column = start0 + 1,
                text = line,
                family = family,
              }
            end
          end
        end
      end
    end
  end
  table.sort(items, function(a, b)
    if a.relative ~= b.relative then
      return a.relative < b.relative
    end
    if a.line ~= b.line then
      return a.line < b.line
    end
    return a.column < b.column
  end)
  local cap = 500
  if #items > cap then
    local kept = {}
    for i = 1, cap do
      kept[i] = items[i]
    end
    return kept
  end
  return items
end

-- A picker row carries the location in `value`, the readable line in `label`
-- and the family colour in `fg` (the picker resolves it), so the list is
-- colour-coded and the callback never parses its own display text. There is no
-- `detail`: the row already names the file and line, and a repeated label on
-- every row is noise.
local function picker_rows(items, accents)
  accents = accents or family_accents()
  local rows = {}
  for _, item in ipairs(items) do
    local accent = item.family and accents[item.family]
    rows[#rows + 1] = {
      label = string.format("%s:%d  %s", item.relative, item.line, item.text),
      value = string.format("%s\t%d\t%d\t%s", item.path, item.line, item.column, item.relative),
      fg = accent and accent.fg or nil,
    }
  end
  return rows
end

local function open_picked(value)
  local path, line, column, relative = tostring(value or ""):match("^(.-)\t(%d+)\t(%d+)\t(.*)$")
  if not path then
    return
  end
  pending_jump = {
    path = path,
    relative = relative,
    line = tonumber(line) or 1,
    column = tonumber(column) or 1,
  }
  jot.file.open(path)
  land_pending_jump()
end

jot.keymap.set("Alt+] t", ":TodoNext", "Next TODO comment")
jot.keymap.set("Alt+[ t", ":TodoPrev", "Previous TODO comment")

jot.command("Todo", function(arg)
  local items = workspace_items(keyword_filter(arg))
  if #items == 0 then
    jot.notify("No todo comments found")
    return
  end
  -- Rows are handed over as a function: show_picker's table branch refs the
  -- table without registering it as a callback, so only the function shape
  -- ever reaches the native item parser.
  jot.show_picker("Todo Comments", function()
    return picker_rows(items)
  end, open_picked)
end, "Search todo comments across the workspace")

-- A theme switch changes every band's ink, and the spans carry resolved
-- colours, so the painted buffers have to be repainted now: waiting for each
-- one's next edit would leave the old palette on screen.
if jot.events and type(jot.events.subscribe) == "function" then
  jot.events.subscribe("config.changed", function(info)
    local key = info and info.key
    if key == nil or key == ENABLED_KEY or key == KEYWORDS_KEY then
      apply({ buffer = current_id(), path = current_path() }, true)
    end
  end)
  jot.events.subscribe("theme.switched", function()
    for buffer, path in pairs(paths) do
      apply({ buffer = buffer, path = path }, true)
    end
  end)
end

-- Exposed so the keyword/matcher/colour logic can be exercised directly,
-- without an editor: test/test_lua_todo_comments.cpp drives these.
return {
  match_line = match_line,
  configured_keywords = configured_keywords,
  compile_matcher = compile_matcher,
  readable_ink = readable_ink,
  extension_of = extension_of,
  scan_lines = scan_lines,
  workspace_items = workspace_items,
  keyword_filter = keyword_filter,
  picker_rows = picker_rows,
  jump = jump,
}
