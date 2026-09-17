-- Preview session lifecycle: start/stop/toggle, live refresh on edits, and the
-- scroll-sync timers. One session at a time, bound to a file path so buffer
-- reordering can never re-target the preview by accident.
local config = require("jot_md.config")
local render = require("jot_md.render")
local browser = require("jot_md.browser")
local sync = require("jot_md.sync")

local M = {}

local state = {
  running = false,
  started = false,
  port = 0,
  url = nil,
  path = nil,        -- file being previewed
  body = nil,        -- last pushed body HTML
  last_text = nil,   -- last rendered source text
  last_line = nil,   -- last editor line pushed to the browser
  debounce = nil,
  poll = nil,
  push = nil,
  -- True between an edit and the render that catches up with it: what the
  -- status segment reports as live.
  pending = false,
}

-- Read-only snapshot (tests and status segments use it).
function M.state()
  return {
    running = state.running,
    port = state.port,
    url = state.url,
    path = state.path,
    live = state.pending,
  }
end

function M.is_running()
  return state.running
end

function M.url()
  return state.url
end

local function current_meta()
  local ok, meta = pcall(jot.buffer.meta)
  if not ok or type(meta) ~= "table" or not meta.path or meta.path == "" then
    return nil
  end
  return meta
end

local function read_text(path)
  local ok, text = pcall(jot.buffer.text, path)
  if not ok then
    return nil
  end
  return text
end

local function stop_timers()
  for _, key in ipairs({ "debounce", "poll", "push" }) do
    if state[key] then
      pcall(jot.timer.clear, state[key])
      state[key] = nil
    end
  end
end

local function interval_ms()
  return math.max(50, tonumber(config.get("refresh_interval")) or 100)
end

local function start_timers()
  stop_timers()
  local interval = interval_ms()
  state.poll = jot.timer.set_interval(interval, function()
    if state.running then
      sync.drain_browser_scroll()
    end
  end)
  state.push = jot.timer.set_interval(interval, function()
    if not state.running then
      return
    end
    -- Only follow the editor while it is showing the previewed file. The
    -- scroll line comes from whichever pane has focus, so without this a
    -- glance at another buffer drags the page to a line number that means
    -- nothing in it.
    local focused = sync.focused_path()
    if focused and focused ~= state.path then
      return
    end
    local line = sync.editor_line()
    if line and line ~= state.last_line and sync.should_push(line) then
      state.last_line = line
      sync.push_editor_scroll()
    end
  end)
end

-- Renders the previewed file and pushes the page when it changed.
-- Returns true when fresh content was produced.
function M.refresh(force)
  if not state.running then
    return false
  end
  local path = state.path
  local text = read_text(path)
  if text == nil then
    return false
  end
  if not force and text == state.last_text then
    return false
  end
  state.last_text = text
  state.pending = false

  local ok, meta = pcall(jot.buffer.meta, path)
  local name = ok and type(meta) == "table" and meta.name or nil
  local rendered = render.render(text, { path = path, name = name })

  jot.preview.set_page(rendered.html)
  if rendered.body ~= state.body then
    state.body = rendered.body
    jot.preview.set_content(rendered.body)
    jot.preview.notify("content", rendered.body)
  end
  return true
end

-- Coalesces the rapid BufChange firehose into one render per interval.
function M.schedule_refresh()
  if not state.running then
    return
  end
  if state.debounce then
    pcall(jot.timer.clear, state.debounce)
  end
  state.pending = true
  state.debounce = jot.timer.set_timeout(interval_ms(), function()
    state.debounce = nil
    M.refresh(false)
  end)
end

-- Starts the preview for the current buffer (or re-targets a running one).
function M.start(opts)
  opts = opts or {}
  local meta = current_meta()
  if not meta then
    jot.ui.show_message("markdown preview: no file to render")
    return false
  end
  local text = read_text(meta.path) or ""
  local rendered = render.render(text, meta)

  if not state.running then
    local ok, port_or_error = jot.preview.start({
      port = config.get("port"),
      host = config.get("host"),
    })
    if not ok then
      jot.ui.show_message("markdown preview: " .. tostring(port_or_error))
      return false
    end
    state.port = port_or_error
    state.url = browser.url(state.port)
    state.running = true
    start_timers()
  end

  state.path = meta.path
  state.last_text = text
  state.body = rendered.body
  state.last_line = nil
  state.pending = false
  jot.preview.set_page(rendered.html)
  jot.preview.set_content(rendered.body)
  jot.preview.notify("content", rendered.body)
  jot.preview.notify("title", rendered.title)

  if not state.started then
    state.started = true
    local opened = false
    if not opts.no_browser then
      opened = browser.open(state.url)
    end
    if config.get("echo_preview_url") or not opened then
      jot.ui.show_message("markdown preview: " .. state.url)
    else
      jot.ui.show_message("markdown preview started")
    end
  else
    jot.ui.show_message("markdown preview: " .. tostring(meta.name or meta.path))
  end
  return true
end

-- Switches a running preview to another file without restarting the server.
function M.retarget(path)
  if not state.running or state.path == path then
    return false
  end
  state.path = path
  state.last_text = nil
  state.last_line = nil
  return M.refresh(true)
end

function M.stop()
  stop_timers()
  pcall(jot.preview.stop)
  state.running = false
  state.started = false
  state.port = 0
  state.url = nil
  state.path = nil
  state.body = nil
  state.last_text = nil
  state.last_line = nil
  state.pending = false
  jot.ui.show_message("markdown preview stopped")
end

function M.toggle()
  if state.running then
    M.stop()
    return false
  end
  M.start()
  return true
end

-- Buffer lifecycle hooks (wired to autocmds in init.lua).

function M.on_buffer_change(event)
  if state.running then
    if not event or not event.path or event.path == state.path then
      M.schedule_refresh()
    end
  elseif config.get("auto_start") and event and config.is_markdown_path(event.path) then
    M.start()
  end
end

function M.on_buffer_save(event)
  if not state.running then
    return
  end
  if not event or not event.path or event.path == state.path then
    M.refresh(true)
  end
end

function M.on_buffer_open(event)
  if not event or not event.path then
    return
  end
  if state.running then
    if config.is_markdown_path(event.path) then
      M.retarget(event.path)
    end
  elseif config.get("auto_start") and config.is_markdown_path(event.path) then
    M.start()
  end
end

function M.on_buffer_close(event)
  if not state.running then
    return
  end
  if event and event.path and event.path ~= state.path then
    return
  end
  if config.get("auto_close") then
    M.stop()
  end
end

return M
