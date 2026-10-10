-- HTML preview session: serve the file being edited and reload the browser
-- when it changes.
--
-- The markdown preview renders a document into a page the server holds. An HTML
-- file cannot work that way: it references its own stylesheets, scripts and
-- images by relative path, and those only resolve if the browser is talking to
-- a server rooted at the tree the file lives in. So this serves the real tree
-- (jot.preview.start{ file_root = ... }) and starts the browser on
-- `<root>/<relative path>` - the same URL `python -m http.server` at the project
-- root would give - with the previewed buffer's own text overriding the file on
-- disk, so unsaved edits are what the page shows.
--
-- Reload is the server's job: every served HTML page carries a small EventSource
-- client (injected natively), so pushing the buffer text and firing `reload` is
-- all this has to do. One session at a time, bound to a path.
local config = require("jot_html.config")
local browser = require("jot_md.browser")

local M = {}

local state = {
  running = false,
  started = false,   -- whether the browser has been opened for this server
  port = 0,
  url = nil,
  path = nil,        -- absolute path of the file being previewed
  root = nil,        -- the directory the server is rooted at
  rel = nil,         -- `path` relative to `root`, which is what the URL says
  last_text = nil,   -- last text pushed for the document
  debounce = nil,
  }

function M.state()
  return {
    running = state.running,
    port = state.port,
    url = state.url,
    path = state.path,
    }
end

function M.is_running()
  return state.running
end

function M.url()
  return state.url
end

-- `/` for the separators the URL needs, whatever the platform spells them.
local function to_url_path(path)
  return (path:gsub("\\", "/"))
end

-- Percent-encodes everything a URL path cannot carry literally (a space, a `#`,
-- a non-ASCII byte). Kept here rather than in the native side because the only
-- thing that knows the path is the session.
function M.encode_path(path)
  return (to_url_path(path):gsub("[^%w%-%._~/]", function(c)
                                               return string.format("%%%02X", string.byte(c))
  end))
end

function M.strip_trailing_slashes(path)
  local trimmed = path
  while #trimmed > 1 and (trimmed:sub(-1) == "/" or trimmed:sub(-1) == "\\") do
    trimmed = trimmed:sub(1, -2)
  end
  return trimmed
end

-- The directory part of a path, with no trailing separator.
function M.dirname(path)
  local normalized = M.strip_trailing_slashes(to_url_path(path))
  local dir = normalized:match("^(.*)/[^/]*$")
  if not dir or dir == "" then
    return "/"
  end
  return dir
end

-- `path` as seen from `root`, or nil when it is not under it. Both are absolute
-- paths from the editor, so a plain prefix test is enough; the separator is
-- required so `/w/ab.html` is not read as living under `/w/a`.
function M.relative_to(root, path)
  local base = M.strip_trailing_slashes(to_url_path(root))
  local target = to_url_path(path)
  if base == "/" then
    return target:sub(2)
  end
  if target:sub(1, #base + 1) ~= base .. "/" then
    return nil
  end
  return target:sub(#base + 2)
end

-- Where the server is rooted and where the file sits in it. The workspace root
-- when the file is inside it (so a sibling directory's asset resolves the way it
-- does for any other static server), the file's own directory otherwise.
local function resolve_root(path)
  local preference = tostring(config.get("root") or ""):lower()
  if preference ~= "dir" then
    local ok, workspace = pcall(jot.workspace.path)
    if ok and type(workspace) == "string" and workspace ~= "" then
      if M.relative_to(workspace, path) then
        return M.strip_trailing_slashes(to_url_path(workspace))
      end
    end
  end
  return M.dirname(path)
end

-- The path part of the preview URL: `path` as seen from `root`, encoded, or
-- nil when the file does not live under the root (a preview that cannot be
-- addressed). Exported because it is the one thing about the URL that can be
-- wrong in a way the browser reports as a 404 rather than as a bad preview.
function M.url_path(root, path)
  local rel = M.relative_to(root, path)
  if not rel or rel == "" then
    return nil
  end
  return M.encode_path(rel)
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

-- The text currently in memory for the previewed file, which is what is served.
local function document_text(path)
  local text = read_text(path)
  if text ~= nil then
    return text
  end
  -- A buffer that is no longer open (the file was closed while the preview ran):
  -- fall back to what the last push held.
  return state.last_text or ""
end

-- Pushes the buffer's text for the previewed file and reloads the browser when
-- it changed. Returns true when a reload was sent.
function M.refresh(force)
  if not state.running or not state.path then
    return false
  end
  local text = document_text(state.path)
  if not force and text == state.last_text then
    return false
  end
  state.last_text = text
  jot.preview.set_document(state.rel, text)
  jot.preview.notify("reload", state.rel)
  return true
end

-- Coalesces the BufChange firehose into one reload per interval: a reload is a
-- document load in the browser, so re-fetching per keystroke would be a page
-- flicker rather than a preview.
function M.schedule_refresh()
  if not state.running then
    return
  end
  if state.debounce then
    pcall(jot.timer.clear, state.debounce)
  end
  local interval = math.max(50, tonumber(config.get("refresh_interval")) or 150)
  state.debounce = jot.timer.set_timeout(interval, function()
                                         state.debounce = nil
                                         M.refresh(false)
  end)
end

-- Starts (or re-targets) the preview for the current buffer.
function M.start(opts)
  opts = opts or {}
  local meta = current_meta()
  if not meta or not config.is_html_path(meta.path) then
    jot.ui.show_message("html preview: not an HTML buffer")
    return false
  end

  local root = resolve_root(meta.path)
  local rel = M.relative_to(root, meta.path)
  local url_path = M.url_path(root, meta.path)
  if not rel or rel == "" or not url_path then
    jot.ui.show_message("html preview: cannot place the file under " .. tostring(root))
    return false
  end

  local retargeting = state.running and state.path ~= meta.path
  if not state.running then
    local ok, port_or_error = jot.preview.start({
      port = config.get("port"),
      host = config.get("host"),
      file_root = root,
      })
    if not ok then
      jot.ui.show_message("html preview: " .. tostring(port_or_error))
      return false
    end
    state.port = port_or_error
    state.running = true
    state.started = false
  end

  state.path = meta.path
  state.root = root
  state.rel = rel
  state.url = "http://127.0.0.1:" .. tostring(state.port) .. "/" .. url_path
  state.last_text = nil

  -- The document override and the first page load happen before the browser is
  -- pointed at the URL, so the very first request already serves the buffer.
  M.refresh(true)

  if not state.started then
    state.started = true
    local opened = false
    if config.get("open_browser") and not opts.no_browser then
      -- The launcher is shared with the markdown preview; the browser choice is
      -- this feature's own setting.
      opened = browser.open(state.url, config.get("browser"))
    end
    if config.get("echo_preview_url") or not opened then
      jot.ui.show_message("html preview: " .. state.url)
    else
      jot.ui.show_message("html preview started")
    end
  elseif retargeting then
    jot.ui.show_message("html preview: " .. tostring(meta.name or meta.path))
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
  local root = resolve_root(path)
  local rel = M.relative_to(root, path)
  local url_path = M.url_path(root, path)
  if not rel or rel == "" or not url_path then
    return false
  end
  state.root = root
  state.rel = rel
  state.url = "http://127.0.0.1:" .. tostring(state.port) .. "/" .. url_path
  M.refresh(true)
  jot.ui.show_message("html preview: " .. state.url)
  return true
end

function M.stop()
  if state.debounce then
    pcall(jot.timer.clear, state.debounce)
    state.debounce = nil
  end
  pcall(jot.preview.set_document, "", "")
  pcall(jot.preview.stop)
  state.running = false
  state.started = false
  state.port = 0
  state.url = nil
  state.path = nil
  state.root = nil
  state.rel = nil
  state.last_text = nil
  jot.ui.show_message("html preview stopped")
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
  if not state.running then
    return
  end
  if not event or not event.path or event.path == state.path then
    M.schedule_refresh()
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
    if config.is_html_path(event.path) then
      M.retarget(event.path)
    end
  elseif config.get("auto_start") and config.is_html_path(event.path) then
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
