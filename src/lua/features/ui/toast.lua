-- Toast notifications: the modern replacement for the statusline message
-- channel (set_message / set_transient_message stay intact but are deprecated;
-- they forward to this module through the native bridge) and the primary
-- surface for jot.toast.show / jot.notify.
--
-- Toasts render as small rounded frames stacked from the top-right corner of
-- the window, newest on top. Each toast slides in, its icon and border take
-- the level accent color from the active theme, and it auto-dismisses (or
-- dismisses on click). All logic lives here; the C++ side only stores this
-- module table (jot.toast.register) and forwards native calls to it.

local toast = {}

local toasts = {} -- ordered list, toasts[1] is the oldest (bottom)
local next_id = 1

local TICK_MS = 50 -- slide/dismiss resolution

local LEVELS = {
  info = { icon = "ℹ" },
  success = { icon = "✓" },
  warning = { icon = "⚠" },
  error = { icon = "✕" },
}

-- Safe config helpers: the stubbed test environment has no real jot.config,
-- and the access itself must be inside the pcall (arguments are evaluated
-- before pcall runs).
local function cfg_num(key, def)
  local ok, v = pcall(function() return jot.config.get_number(key, def) end)
  if ok and type(v) == "number" then
    return v
  end
  return def
end

-- Palette: active theme slots with toast.color_* overrides first, then
-- theme-derived colors, then fixed ANSI fallbacks.
local function palette()
  local theme = {}
  local ok = pcall(function() theme = jot.theme.palette() or {} end)
  if not ok or type(theme) ~= "table" then
    theme = {}
  end
  local function pick(cfg_key, slot, field, fallback)
    local c = cfg_num(cfg_key, -1)
    if c >= 0 then
      return c
    end
    local s = type(theme[slot]) == "table" and theme[slot][field]
    if type(s) == "number" then
      return s
    end
    return fallback
  end
  return {
    bg = pick("toast.color_bg", "default", "bg", 235),
    fg = pick("toast.color_fg", "default", "fg", 250),
    border = pick("toast.color_border", "panel_border", "fg", 240),
    title = pick("toast.color_title", "status_info", "fg", 251),
    info = pick("toast.color_info", "status_info", "fg", 215),
    success = pick("toast.color_success", "diagnostic_hint", "fg", 108),
    warning = pick("toast.color_warning", "diagnostic_warning", "fg", 178),
    error = pick("toast.color_error", "diagnostic_error", "fg", 167),
  }
end

local function window_size()
  local ok, info = pcall(function() return jot.viewport.info() end)
  if ok and type(info) == "table" and type(info.window) == "table" then
    return tonumber(info.window.width) or 80, tonumber(info.window.height) or 24
  end
  return 80, 24
end

local function request_redraw()
  pcall(jot.editor.request_redraw)
end

-- Greedy word wrap into lines of at most `width` cells (ASCII-accurate).
local function wrap_text(text, width)
  local lines = {}
  for part in text:gmatch("[^\r\n]+") do
    local line = ""
    for word in part:gmatch("%S+") do
      local wc = utf8.len(word)
      if wc > width then
        -- Long token: hard-slice it so it never overflows the box.
        word = string.rep(" ", 0) .. word -- no-op, keeps parser simple below
        while utf8.len(word) > width do
          local slice = utf8.sub(word, 1, width)
          if line ~= "" then
            lines[#lines + 1] = line
          end
          lines[#lines + 1] = slice
          word = utf8.sub(word, width + 1)
        end
        line = word
      elseif line == "" then
        line = word
      elseif utf8.len(line) + 1 + wc <= width then
        line = line .. " " .. word
      else
        lines[#lines + 1] = line
        line = word
      end
    end
    lines[#lines + 1] = line
  end
  if #lines == 0 then
    lines[1] = ""
  end
  return lines
end

-- Truncates a string to at most n codepoints (renderer clips at the box edge).
local function shorten(s, n)
  if utf8.len(s) <= n then
    return s
  end
  return utf8.sub(s, 1, n)
end

-- Where the toast sits: top-right, stacked below its predecessors.
local function layout_toast(t)
  local ww, wh = window_size()
  local margin = math.max(1, cfg_num("toast.margin", 1))
  local gap = math.max(0, cfg_num("toast.gap", 1))
  local max_w = math.max(20, cfg_num("toast.max_width", 56))

  t.wrapped = wrap_text(t.message, math.max(8, max_w - 4))
  t.width = math.min(max_w, math.max(20, ww - margin * 2))
  local inner_rows = 1 + #t.wrapped -- title row + message rows
  t.height = math.max(3, math.min(wh - margin, inner_rows + 2)) -- + border
  t.col = math.max(0, ww - t.width - margin - 1)

  local row = margin
  for _, other in ipairs(toasts) do
    row = row + other.height + gap
  end
  t.final_row = row
end

local function request_dismiss(t)
  toast.dismiss(t.id)
end

local function tick_toast(t)
  t.ticks = t.ticks + 1

  -- Slide in: drift down by one row per tick for the first three ticks.
  if t.sliding then
    local remain = math.max(0, 3 - t.ticks)
    pcall(jot.ui.float.configure, t.win, { row = t.final_row + remain })
    if t.ticks >= 3 then
      t.sliding = false
    end
    request_redraw()
  end

  if t.ticks >= t.limit then
    toast.dismiss(t.id)
  end
end

local function paint_toast(t)
  local colors = palette()
  local inner = t.width - 2
  t.accent = colors[t.level] or colors.info

  local rows = {}
  local spans = {}
  local n = 0

  -- Title row: icon (accent) + title (or the first message line). The lead
  -- space is folded into the accent span, title text gets its own color.
  local title_text
  local first_body
  if t.title ~= "" then
    title_text = " " .. t.icon .. " " .. shorten(t.title, math.max(1, inner - 4))
    first_body = 1
  else
    title_text = " " .. t.icon .. " " .. shorten(t.wrapped[1], math.max(1, inner - 4))
    first_body = 2
  end
  n = n + 1
  rows[n] = title_text
  spans[n] = {
    { start = 1, len = #t.icon + 1, fg = t.accent, bg = -1 }, -- icon + trailing space
    { start = #t.icon + 3, len = 65535, fg = colors.title, bg = -1 },
  }

  -- Body rows.
  for i = first_body, #t.wrapped do
    n = n + 1
    rows[n] = " " .. t.wrapped[i] .. " "
    spans[n] = { { start = 0, len = 65535, fg = colors.fg, bg = -1 } }
  end

  local buf = jot.ui.buffer.create(false, true)
  local ok, err = pcall(jot.ui.buffer.set_lines, buf, 0, -1, true, rows)
  if not ok then
    jot.ui.buffer.delete(buf)
    return false
  end
  t.buf = buf

  local win = jot.ui.float.open(buf, {
    col = t.col,
    row = t.final_row + 3, -- start below the final spot; slides up
    width = t.width,
    height = t.height,
    relative = "editor",
    anchor = "NW",
    border = "rounded",
    focusable = false,
    mouse = true,
    hide = false,
    zindex = 100000,
    fg = colors.fg,
    bg = colors.bg,
    border_fg = t.accent, -- level accent frames the toast (modern edge)
    title_fg = colors.title,
    on_mouse = function() request_dismiss(t) end,
  })
  if not win or win == 0 then
    jot.ui.buffer.delete(buf)
    return false
  end
  t.win = win
  for i = 1, n do
    pcall(jot.ui.float.set_spans, win, i, spans[i])
  end
  return true
end

local function clear_timers(t)
  if t.timer and t.timer ~= 0 then
    pcall(jot.timer.clear, t.timer)
    t.timer = 0
  end
end

local function restack()
  local margin = math.max(1, cfg_num("toast.margin", 1))
  local gap = math.max(0, cfg_num("toast.gap", 1))
  local row = margin
  for _, other in ipairs(toasts) do
    other.final_row = row
    if other.win ~= 0 then
      pcall(jot.ui.float.configure, other.win, { row = row })
    end
    row = row + other.height + gap
  end
end

-- Removes a toast and tears down its float/buffer. Does the bookkeeping the
-- capped-queue eviction and the timer expiry share.
local function dismiss_internal(t)
  clear_timers(t)
  if t.win and t.win ~= 0 then
    pcall(jot.ui.float.close, t.win)
    t.win = 0
  end
  if t.buf and t.buf ~= 0 then
    pcall(jot.ui.buffer.delete, t.buf)
    t.buf = 0
  end
  local idx
  for i, other in ipairs(toasts) do
    if other.id == t.id then
      idx = i
      break
    end
  end
  if idx then
    table.remove(toasts, idx)
  end
  if toasts[1] then
    restack()
  end
  request_redraw()
  if type(t.on_dismiss) == "function" then
    pcall(t.on_dismiss, t.message)
  end
end

local function start_animation(t)
  t.sliding = true
  t.ticks = 0
  local ok, id = pcall(jot.timer.set_interval, TICK_MS, function() tick_toast(t) end)
  if ok then
    t.timer = id or 0
  else
    t.timer = 0
  end
end

-- Public API ---------------------------------------------------------------

function toast.show(opts)
  opts = opts or {}
  local message = tostring(opts.message or "")
  if message == "" then
    return 0
  end

  local level = LEVELS[opts.level] and opts.level or "info"
  local duration = tonumber(opts.duration_ms) or cfg_num("toast.duration_ms", 3000)
  if duration <= 0 then
    duration = cfg_num("toast.duration_ms", 3000)
  end

  -- Cap the visible stack: evict the oldest first.
  local max_visible = math.max(1, cfg_num("toast.max_visible", 5))
  while #toasts >= max_visible do
    dismiss_internal(toasts[1])
  end

  local t = {
    id = next_id,
    message = message,
    title = tostring(opts.title or ""),
    level = level,
    icon = LEVELS[level].icon,
    duration_ms = duration,
    limit = math.max(1, math.floor(duration / TICK_MS)),
    on_dismiss = opts.on_dismiss,
    win = 0,
    buf = 0,
    timer = 0,
  }
  next_id = next_id + 1

  layout_toast(t)
  if not paint_toast(t) then
    return 0
  end
  toasts[#toasts + 1] = t
  start_animation(t)
  return t.id
end

-- Dismisses the given toast, or the newest one when id is nil/0.
function toast.dismiss(id)
  if not id or id == 0 then
    local t = toasts[#toasts]
    if t then
      dismiss_internal(t)
    end
    return
  end
  for _, t in ipairs(toasts) do
    if t.id == id then
      dismiss_internal(t)
      return
    end
  end
end

function toast.clear()
  while #toasts > 0 do
    dismiss_internal(toasts[#toasts])
  end
end

function toast.info()
  return { count = #toasts, visible = #toasts }
end

-- Subscribes to the "toast.message" event bus so other producers can surface
-- toasts too. The deprecated statusline channel (set_message) delivers
-- directly to this module via the native bridge, so it carries no payload
-- duplication.
function toast.attach()
  local ok = pcall(function()
    if not jot.events or not jot.events.subscribe then
      return
    end
    jot.events.subscribe("toast.message", function(payload)
      if type(payload) ~= "table" or payload.message == nil then
        return
      end
      toast.show({
        message = payload.message,
        title = type(payload.title) == "string" and payload.title or nil,
        duration_ms = tonumber(payload.duration_ms) or 0,
      })
    end)
  end)
  return ok
end

return toast
