-- Toast notifications: the modern replacement for the statusline message
-- channel (set_message / set_transient_message stay intact but are deprecated;
-- they forward to this module through the native bridge) and the primary
-- surface for jot.toast.show / jot.notify.
--
-- Toasts render as small rounded frames stacked from the top-right corner of
-- the window, newest on top. Each toast slides in, its icon and border take
-- the level accent color from the active theme, and it fades out (colors
-- dissolve toward the toast background while the frame drifts upward) before
-- being removed. Remaining toasts then drift down into the freed slot. All
-- logic lives here; the C++ side only stores this module table
-- (jot.toast.register) and forwards native calls to it.

local toast = {}

local toasts = {} -- ordered list, toasts[1] is the oldest (bottom)
local next_id = 1

local TICK_MS = 50 -- slide/dissolve/fade resolution

local LEVELS = {
  info = { icon = "ℹ" },
  success = { icon = "✓" },
  warning = { icon = "⚠" },
  error = { icon = "✕" },
}

-- Level label shown in the toast header when no title was given, so the
-- three (four with success) toast types read at a glance.
local LEVEL_NAMES = {
  info = "Info",
  success = "Success",
  warning = "Warning",
  error = "Error",
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
    time = pick("toast.color_time", "comment", "fg", 244),
    info = pick("toast.color_info", "status_info", "fg", 215),
    success = pick("toast.color_success", "diagnostic_hint", "fg", 108),
    warning = pick("toast.color_warning", "diagnostic_warning", "fg", 178),
    error = pick("toast.color_error", "diagnostic_error", "fg", 167),
  }
end

-- xterm-256 palette used by the fade blend (same mapping the native dim
-- uses). Built lazily on the first fade.
local PALETTE = nil

local function ensure_palette()
  if PALETTE then
    return
  end
  PALETTE = {}
  local base = {
    { 0, 0, 0 }, { 128, 0, 0 }, { 0, 128, 0 }, { 128, 128, 0 },
    { 0, 0, 128 }, { 128, 0, 128 }, { 0, 128, 128 }, { 192, 192, 192 },
  }
  for i = 0, 15 do
    local c = base[(i % 8) + 1]
    if i >= 8 then
      PALETTE[i] = { c[1] + 64, c[2] + 64, c[3] + 64 }
    else
      PALETTE[i] = { c[1], c[2], c[3] }
    end
  end
  local levels = { 0, 95, 135, 175, 215, 255 }
  for i = 16, 231 do
    local v = i - 16
    PALETTE[i] = {
      levels[math.floor(v / 36) + 1],
      levels[math.floor((v % 36) / 6) + 1],
      levels[(v % 6) + 1],
    }
  end
  for i = 232, 255 do
    local g = 8 + (i - 232) * 10
    PALETTE[i] = { g, g, g }
  end
end

-- Blends xterm-256 index `a` toward `b` by k in [0, 1], quantized back to
-- the palette. Used to dissolve a toast's colors into its background.
local function blend_index(a, b, k)
  if a == b or k <= 0 then
    return a
  end
  if k >= 1 then
    return b
  end
  ensure_palette()
  local ca = PALETTE[a] or { 0, 0, 0 }
  local cb = PALETTE[b] or { 0, 0, 0 }
  local r = math.floor(ca[1] + (cb[1] - ca[1]) * k + 0.5)
  local g = math.floor(ca[2] + (cb[2] - ca[2]) * k + 0.5)
  local bl = math.floor(ca[3] + (cb[3] - ca[3]) * k + 0.5)
  local best, best_d = 0, math.huge
  for i = 0, 255 do
    local c = PALETTE[i]
    local d = (r - c[1]) ^ 2 + (g - c[2]) ^ 2 + (bl - c[3]) ^ 2
    if d < best_d then
      best_d = d
      best = i
    end
  end
  return best
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

-- The embedded Lua is built without utf8.sub, so truncate by codepoint
-- count through utf8.offset + byte slicing instead.
local function take_codepoints(s, n)
  if utf8.len(s) <= n then
    return s
  end
  local b = utf8.offset(s, n + 1)
  if b then
    return s:sub(1, b - 1)
  end
  return s
end

local function drop_codepoints(s, n)
  local b = utf8.offset(s, n + 1)
  if b then
    return s:sub(b)
  end
  return ""
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
        while utf8.len(word) > width do
          local slice = take_codepoints(word, width)
          if line ~= "" then
            lines[#lines + 1] = line
          end
          lines[#lines + 1] = slice
          word = drop_codepoints(word, width)
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

-- Right edge the toast stack may touch: the window edge, or the left edge
-- of the right panel when it is visible.
local function toast_right_edge(ww)
  local right_edge = ww
  local ok, info = pcall(function() return jot.viewport.info() end)
  if ok and type(info) == "table" and type(info.right_panel) == "table"
     and info.right_panel.visible and tonumber(info.right_panel.width or 0) > 0 then
    right_edge = math.max(1, ww - tonumber(info.right_panel.width))
  end
  return right_edge
end

-- Recomputes one toast's size/column from the current window size: the
-- message re-wraps, the box shrinks/grows with the window, and the column
-- stays pinned to the top-right edge (or the right panel's left edge).
local function toast_geometry(t, ww, wh, margin, gap, max_w, right_edge)
  t.wrapped = wrap_text(t.message, math.max(8, max_w - 4))
  t.width = math.min(max_w, math.max(20, math.min(right_edge, ww) - margin * 2))
  -- One header row (logo + level/title + time) plus one row per wrapped
  -- message line -- the logo never shares a row with the message -- plus
  -- the two border rows.
  local content_rows = 1 + #t.wrapped
  t.height = math.max(3, math.min(wh - margin, content_rows + 2))
  t.col = math.max(margin, right_edge - t.width - margin - 1)
end

-- Where the toast sits: top-right, stacked below its predecessors. When the
-- right panel (git / debugger / outline dock) is visible the stack shifts
-- left of it, so toasts never hide the panel's top rows — a panel with few
-- rows (e.g. the git branches view) would otherwise look empty while the
-- toast is up.
local function layout_toast(t)
  local ww, wh = window_size()
  local margin = math.max(1, cfg_num("toast.margin", 1))
  local gap = math.max(0, cfg_num("toast.gap", 0))
  local max_w = math.max(20, cfg_num("toast.max_width", 56))
  toast_geometry(t, ww, wh, margin, gap, max_w, toast_right_edge(ww))

  local row = margin
  for _, other in ipairs(toasts) do
    row = row + other.height + gap
  end
  t.final_row = row
end

-- Builds the toast's buffer rows and per-line color spans from a palette.
-- The fade reuses this with blended colors; only the span colors change.
local function content_rows_spans(t, colors)
  local inner = t.width - 2
  local accent = colors[t.level] or colors.info

  local rows = {}
  local spans = {}
  local n = 0

  -- Header row: the logo inside a bracketed badge (like [✕]) in the level
  -- accent, then the level/title one space later (no wide gap), with the
  -- time right-aligned on the same row. Byte offsets are 0-based:
  -- row = [0]=space [1]=badge ([ + icon + ]) [n+1]=gap [n+2..]=heading.
  -- The message starts on its own row below -- the logo never shares a row
  -- with the message text.
  local badge = "[" .. t.icon .. "]"
  local heading =
      take_codepoints(t.heading, math.max(1, inner - 6 - utf8.len(t.time or "")))
  local head = " " .. badge .. " " .. heading
  if t.time ~= "" then
    local pad_cells = math.max(1, inner - utf8.len(head) - utf8.len(t.time))
    head = head .. string.rep(" ", pad_cells) .. t.time
  end
  n = n + 1
  rows[n] = head
  spans[n] = {
    { start = 1, len = #badge, fg = accent, bg = -1 },            -- bracketed logo badge
    { start = #badge + 2, len = #heading, fg = colors.title, bg = -1 }, -- heading
  }
  if t.time ~= "" then
    spans[n][#spans[n] + 1] = { start = #head - #t.time, len = #t.time, fg = colors.time, bg = -1 }
  end

  -- Body rows: the message owns its rows.
  for i = 1, #t.wrapped do
    n = n + 1
    rows[n] = " " .. t.wrapped[i] .. " "
    spans[n] = { { start = 0, len = 65535, fg = colors.fg, bg = -1 } }
  end

  return rows, spans, accent
end

local function request_dismiss(t)
  toast.dismiss(t.id)
end

local function paint_toast(t)
  local colors = palette()
  t.palette = colors
  t.accent = colors[t.level] or colors.info
  local rows, spans = content_rows_spans(t, colors)

  local buf = jot.ui.buffer.create(false, true)
  local ok, err = pcall(jot.ui.buffer.set_lines, buf, 0, -1, true, rows)
  if not ok then
    jot.ui.buffer.delete(buf)
    return false
  end
  t.buf = buf

  local win = jot.ui.float.open(buf, {
    col = t.col,
    row = t.row, -- entry offset already applied by the caller
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
  for i = 1, #spans do
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

-- Recomputes the stack positions after a toast is removed. Toasts drift to
-- their new slots in tick_toast (one row per tick) instead of jumping.
local function restack()
  local margin = math.max(1, cfg_num("toast.margin", 1))
  local gap = math.max(0, cfg_num("toast.gap", 0))
  local row = margin
  for _, other in ipairs(toasts) do
    other.final_row = row
    row = row + other.height + gap
  end
end

-- Window resize: recompute every visible toast's geometry (column, width,
-- height and stack slot) and re-paint its float, so the stack stays pinned
-- to the window's top-right corner instead of hanging at the old size.
local function relayout_all()
  if #toasts == 0 then
    return
  end
  local ww, wh = window_size()
  local margin = math.max(1, cfg_num("toast.margin", 1))
  local gap = math.max(0, cfg_num("toast.gap", 0))
  local max_w = math.max(20, cfg_num("toast.max_width", 56))
  local right_edge = toast_right_edge(ww)
  local row = margin
  for _, t in ipairs(toasts) do
    toast_geometry(t, ww, wh, margin, gap, max_w, right_edge)
    t.final_row = row
    t.row = row -- snap: a mid-slide row would be stale after a resize
    row = row + t.height + gap
  end
  for _, t in ipairs(toasts) do
    if t.win and t.win ~= 0 then
      local colors = t.palette or palette()
      local rows, spans = content_rows_spans(t, colors)
      pcall(jot.ui.buffer.set_lines, t.buf, 0, -1, true, rows)
      pcall(jot.ui.float.configure, t.win, {
        col = t.col,
        row = t.row,
        width = t.width,
        height = t.height,
      })
      for i = 1, #spans do
        pcall(jot.ui.float.set_spans, t.win, i, spans[i])
      end
    end
  end
  request_redraw()
end

-- Removes a toast and tears down its float/buffer. Does the bookkeeping the
-- capped-queue eviction and the fade completion share.
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

-- Fade-out: dissolve the toast's colors into its background while it drifts
-- a couple of rows upward, then remove it. Duration in ticks from toast.fade_ms.
local function fade_limit()
  local ms = cfg_num("toast.fade_ms", 250)
  return math.max(1, math.min(10, math.floor(ms / TICK_MS)))
end

local function begin_fade(t)
  if t.fading then
    return
  end
  t.fading = true
  t.fade_ticks = 0
  t.fade_limit = fade_limit()
end

local function apply_fade(t)
  local k = t.fade_ticks / t.fade_limit
  local colors = {}
  for key, v in pairs(t.palette) do
    colors[key] = blend_index(v, t.palette.bg, k)
  end
  local _, spans = content_rows_spans(t, colors)
  local accent = colors[t.level] or colors.info
  pcall(jot.ui.float.configure, t.win, {
    row = t.row - math.min(t.fade_ticks, 2),
    fg = colors.fg,
    bg = colors.bg,
    border_fg = accent,
  })
  for i = 1, #spans do
    pcall(jot.ui.float.set_spans, t.win, i, spans[i])
  end
  request_redraw()
end

local function tick_toast(t)
  t.ticks = t.ticks + 1

  if t.fading then
    t.fade_ticks = t.fade_ticks + 1
    if t.fade_ticks >= t.fade_limit then
      dismiss_internal(t)
      return
    end
    apply_fade(t)
    return
  end

  -- Drift one row per tick toward the final slot: upward during the entry
  -- slide, downward when a toast below was removed (restack).
  if t.row < t.final_row then
    t.row = t.row + 1
    pcall(jot.ui.float.configure, t.win, { row = t.row })
    request_redraw()
  elseif t.row > t.final_row then
    t.row = t.row - 1
    pcall(jot.ui.float.configure, t.win, { row = t.row })
    request_redraw()
  end

  if t.ticks >= t.limit then
    begin_fade(t)
  end
end

local function start_animation(t)
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

  -- Cap the visible stack: evict the oldest first (instantly -- the cap is
  -- about visible count, so the evicted toast does not linger in a fade).
  local max_visible = math.max(1, cfg_num("toast.max_visible", 5))
  while #toasts >= max_visible do
    dismiss_internal(toasts[1])
  end

  -- Header heading: the caller's title, or the level name so the toast type
  -- (Info / Warning / Error) is always visible.
  local heading = tostring(opts.title or "")
  if heading == "" then
    heading = LEVEL_NAMES[level] or "Info"
  end
  -- Time stamp shown at the top right of the header row.
  local ok_time, now = pcall(os.date, "%H:%M")

  local t = {
    id = next_id,
    message = message,
    title = tostring(opts.title or ""),
    heading = heading,
    time = ok_time and type(now) == "string" and now or "",
    level = level,
    icon = LEVELS[level].icon,
    duration_ms = duration,
    limit = math.max(1, math.floor(duration / TICK_MS)),
    on_dismiss = opts.on_dismiss,
    win = 0,
    buf = 0,
    timer = 0,
    fading = false,
    fade_ticks = 0,
    fade_limit = 1,
    row = 0,
  }
  next_id = next_id + 1

  layout_toast(t)
  t.row = t.final_row + 3 -- start below the final spot; slides up
  if not paint_toast(t) then
    return 0
  end
  toasts[#toasts + 1] = t
  start_animation(t)
  return t.id
end

-- Dismisses the given toast, or the newest one when id is nil/0. The toast
-- fades out before it is removed; a second call during the fade is a no-op.
function toast.dismiss(id)
  if not id or id == 0 then
    local t = toasts[#toasts]
    if t then
      begin_fade(t)
    end
    return
  end
  for _, t in ipairs(toasts) do
    if t.id == id then
      begin_fade(t)
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
    -- Keep visible toasts anchored when the window resizes.
    if jot.autocmd then
      jot.autocmd("UIResize", relayout_all)
    end
  end)
  return ok
end

return toast