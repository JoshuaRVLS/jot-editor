-- The inline assistant: a prompt about a piece of code, answered as a diff you
-- accept or throw away.
--
-- `:CodeCompanion <prompt>` (and Alt+Shift+A I) takes the selection - or the
-- line the caret is on when nothing is selected - asks the model to rewrite it,
-- and shows the answer as a unified diff against what is there now: removals in
-- the error colour, additions in the string colour. `y` (or Enter) replaces the
-- selection with it as one undoable edit, `n`/`Esc`/`q` throws it away, the
-- arrows, Page Up/Down and the wheel scroll a long answer.
--
-- The request is made non-streaming on purpose: the diff needs the whole
-- replacement before it can be computed or shown.
local M = {}

local config = require("jot_ai.config")
local http = require("jot_ai.http")
local textlib = require("jot.lib.text")

-- Above this many lines the diff is a plain before/after listing: the LCS table
-- costs lines x lines entries and a 2000-line pair would be four million of
-- them for a preview nobody reads.
local MAX_DIFF_LINES = 400

local state = {
  win = 0,
  buf = 0,
  lines = {},
  spans = {},
  top = 1,
  rows = 0,
  target = nil,
  reply = nil,
}

local function notify(message)
  jot.ui.show_message("[ai] " .. message)
end

local function split_lines(body)
  local out = textlib.split_lines(body)
  if #out == 0 then
    out[1] = ""
  end
  return out
end

-- The reply as the code it is: a model asked for a replacement often wraps it
-- in a fence anyway, and a fence is not part of the code.
function M.strip_fence(text)
  local trimmed = text:gsub("^%s+", ""):gsub("%s+$", "")
  local fence = trimmed:match("^```[%w%+#._%-]*\n(.-)\n```$")
  if fence then
    return fence
  end
  local single = trimmed:match("^```[%w%+#._%-]*\n(.-)```$")
  if single then
    return (single:gsub("\n$", ""))
  end
  return trimmed
end

-- A unified line diff of two texts: {{kind = "same"|"del"|"add", text = ...}}.
-- The walk itself is jot.lib.text.diff, shared with the undo tree panel; the
-- line budget above is this feature's own, because a preview is worth less
-- work than a diff you asked for.
function M.diff(before, after)
  local a, b = split_lines(before), split_lines(after)
  if #a + #b > MAX_DIFF_LINES then
    local out = { cut = true }
    for _, line in ipairs(a) do
      out[#out + 1] = { kind = "del", text = line }
    end
    for _, line in ipairs(b) do
      out[#out + 1] = { kind = "add", text = line }
    end
    return out
  end
  -- The budget was just checked, so the walk cannot refuse the pair; a refusal
  -- is still handled as a plain listing rather than an error.
  local rows = textlib.diff(a, b, { max_cells = MAX_DIFF_LINES * MAX_DIFF_LINES })
  if not rows then
    local out = { cut = true }
    for _, line in ipairs(a) do
      out[#out + 1] = { kind = "del", text = line }
    end
    for _, line in ipairs(b) do
      out[#out + 1] = { kind = "add", text = line }
    end
    return out
  end
  return rows
end

local function palette()
  local slots = jot.theme.palette() or {}
  local function fg(name, fallback)
    local slot = slots[name]
    if type(slot) == "table" and type(slot.fg) == "number" then
      return slot.fg
    end
    return fallback
  end
  local default = slots["default"] or {}
  return {
    fg = type(default.fg) == "number" and default.fg or 7,
    bg = type(default.bg) == "number" and default.bg or 0,
    border = fg("panel_border", type(default.fg) == "number" and default.fg or 7),
    muted = fg("comment", 8),
    added = fg("string", 2),
    removed = fg("status_error", 1),
  }
end

local function close()
  if state.win ~= 0 then
    jot.ui.float.close(state.win)
    state.win = 0
  end
  if state.buf ~= 0 then
    jot.ui.buffer.delete(state.buf)
    state.buf = 0
  end
  state.reply = nil
  state.target = nil
end

local function rows_of_width(text)
  return text
end

-- Paints the slice of the answer the float can show, from `state.top`.
local function paint()
  if state.win == 0 then
    return
  end
  local colors = palette()
  local body = {}
  local spans = {}
  for row = 1, state.rows do
    local index = state.top + row - 1
    local line = state.lines[index]
    if line == nil then
      body[row] = ""
    else
      body[row] = line.text
      if line.color then
        spans[row] = { { start = 0, len = #line.text, fg = line.color } }
      end
    end
  end
  jot.ui.float.set_lines(state.win, body)
  for row = 1, state.rows do
    jot.ui.float.set_spans(state.win, row, spans[row] or {})
  end
end

local function scroll(delta)
  local total = #state.lines
  local top = state.top + delta
  if top < 1 then
    top = 1
  end
  if top > math.max(1, total - state.rows + 1) then
    top = math.max(1, total - state.rows + 1)
  end
  if top ~= state.top then
    state.top = top
    paint()
  end
end

-- Replaces the target with the answer, as one edit the user can undo.
local function apply()
  local target = state.target
  local reply = state.reply
  close()
  if not target or not reply then
    return
  end
  jot.buffer.apply_edit(target.start_line, target.start_col, target.end_line, target.end_col, reply)
  notify("applied")
end

local function show(reply, before, target)
  local colors = palette()
  state.target = target
  state.reply = reply
  state.lines = {}
  local diff = M.diff(before, reply)
  local removed, added = 0, 0
  for _, row in ipairs(diff) do
    if row.kind == "del" then
      removed = removed + 1
    elseif row.kind == "add" then
      added = added + 1
    end
  end
  state.lines[#state.lines + 1] = {
    text = ("-%d +%d%s"):format(removed, added, diff.cut and "  (too long to align)" or ""),
    color = colors.muted,
  }
  for _, row in ipairs(diff) do
    local marker = row.kind == "del" and "- " or (row.kind == "add" and "+ " or "  ")
    local color = row.kind == "del" and colors.removed
        or (row.kind == "add" and colors.added or colors.fg)
    state.lines[#state.lines + 1] = { text = marker .. rows_of_width(row.text), color = color }
  end
  local viewport = jot.viewport.info() or {}
  local window = viewport.window or {}
  local width = math.min(110, math.max(40, (window.width or 100) - 8))
  local height = math.min(math.max(6, (window.height or 30) - 6), #state.lines + 2)
  state.rows = height - 2
  state.top = 1
  state.buf = jot.ui.buffer.create(false, true)
  state.win = jot.ui.float.open(state.buf, {
    relative = "editor",
    row = 2,
    col = 3,
    width = width,
    height = height,
    border = "single",
    focusable = true,
    fg = colors.fg,
    bg = colors.bg,
    border_fg = colors.border,
    footer_fg = colors.muted,
    footer = "y apply  ·  n discard  ·  arrows scroll",
  })
  if not state.win or state.win == 0 then
    jot.ui.buffer.delete(state.buf)
    state.buf = 0
    notify("could not open the preview")
    return
  end
  paint()
  jot.ui.float.on_key(state.win, function(event)
    local key = event.key
    if key == 121 or key == 13 or key == 89 then -- y / Enter
      apply()
      return true
    end
    if key == 110 or key == 27 or key == 113 or key == 78 then -- n / Esc / q
      close()
      notify("discarded")
      return true
    end
    if key == 1009 or key == 1016 then -- Down / PageDown
      scroll(key == 1016 and state.rows or 1)
      return true
    end
    if key == 1008 or key == 1015 then -- Up / PageUp
      scroll(key == 1015 and -state.rows or -1)
      return true
    end
    return true -- the preview is modal: nothing else reaches the editor
  end)
  jot.ui.float.on_mouse(state.win, function(event)
    if event.button == 65 then
      scroll(3)
    elseif event.button == 64 then
      scroll(-3)
    end
    return true
  end)
end

-- The code the prompt is about: the selection, or the caret's line.
local function target_of(index)
  local lines = jot.buffer.lines(index) or {}
  local selection = jot.buffer.selection(index)
  if selection and selection.active and selection.start_line then
    local out = {}
    for line = selection.start_line, selection.end_line do
      local text = lines[line] or ""
      if selection.start_line == selection.end_line then
        text = text:sub(selection.start_col, selection.end_col - 1)
      elseif line == selection.start_line then
        text = text:sub(selection.start_col)
      elseif line == selection.end_line then
        text = text:sub(1, selection.end_col - 1)
      end
      out[#out + 1] = text
    end
    return {
      text = table.concat(out, "\n"),
      start_line = selection.start_line,
      start_col = selection.start_col,
      end_line = selection.end_line,
      end_col = selection.end_col,
      what = "selection",
    }
  end
  local line = select(1, jot.cursor.get())
  local text = lines[line]
  if not text then
    return nil
  end
  return {
    text = text,
    start_line = line,
    start_col = 1,
    end_line = line,
    end_col = #text + 1,
    what = "line",
  }
end

-- Asks for a rewrite of the selection (or the caret's line) and previews it.
function M.run(prompt)
  if state.win ~= 0 then
    close()
  end
  if not prompt or prompt == "" then
    notify("what should the code become?")
    return false
  end
  local index = jot.buffer.current()
  if not index or index == 0 then
    notify("no buffer to rewrite")
    return false
  end
  local target = target_of(index)
  if not target or target.text == "" then
    notify("nothing to rewrite")
    return false
  end
  local resolved = config.resolve()
  local blocked = config.blocked(resolved)
  if blocked then
    notify(blocked)
    return false
  end
  resolved.stream = false
  local meta = jot.buffer.meta(index) or {}
  local path = meta.path ~= "" and meta.path or "untitled"
  local messages = {
    {
      role = "system",
      content = table.concat({
        resolved.system,
        "You rewrite the code you are given. Reply with the replacement text exactly as it",
        "should be pasted back: no explanation, no markdown fence, no surrounding commentary.",
      }, " "),
    },
    {
      role = "user",
      content = ("%s\n\n%s\n```\n%s\n```"):format(prompt, path, target.text),
    },
  }
  notify("asking " .. resolved.name .. " …")
  local answer = ""
  local handle, message = http.request(resolved, messages, jot.workspace.path() or "", function(event)
    if event.delta then
      answer = answer .. event.delta
      return
    end
    if event.done then
      local reply = M.strip_fence(answer)
      if reply == "" then
        notify("empty answer")
        return
      end
      show(reply, target.text, target)
      return
    end
    if event.error and event.error ~= "aborted" then
      notify("request failed: " .. event.error)
    end
  end)
  if not handle then
    notify(message or "the request could not start")
    return false
  end
  return true
end

function M.close()
  close()
end

M.split_lines = split_lines

return M
