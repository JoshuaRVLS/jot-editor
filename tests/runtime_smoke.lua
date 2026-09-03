-- Runtime smoke test for the jot.* Lua API surface.
--
-- Loaded as ~/.config/jot/init.lua by a headless editor run (see
-- tests/run_runtime_smoke.sh). Every call is pcall-wrapped so a broken or
-- missing API registers as a per-call failure instead of aborting the run;
-- native segfaults cannot be caught here and surface as a driver-level
-- crash. Results are written to the file given by JOT_SMOKE_OUT (the driver
-- passes one), ending with SMOKE_DONE.

local out_path = os.getenv("JOT_SMOKE_OUT") or "/tmp/jot-smoke-out.txt"
local logf = io.open(out_path, "w")
local pass, fail = 0, 0

local function log(s)
  if logf then logf:write(s, "\n") logf:flush() end
end

local function check(name, fn)
  local ok, res = pcall(fn)
  if ok then
    pass = pass + 1
    if type(res) == "table" then
      local c = 0
      for _ in pairs(res) do c = c + 1 end
      log(name .. "=ok(table," .. c .. ")")
    elseif type(res) == "string" then
      log(name .. "=ok(" .. res:sub(1, 40) .. ")")
    else
      log(name .. "=ok(" .. tostring(res) .. ")")
    end
  else
    fail = fail + 1
    log(name .. "=ERR:" .. tostring(res))
  end
end

-- Presence checks: the callable must exist before we call it.
local function present(name, value)
  if type(value) == "function" then
    pass = pass + 1
    log(name .. "=present")
  else
    fail = fail + 1
    log(name .. "=MISSING:" .. tostring(value))
  end
end

-- ---------------------------------------------------------------- viewport
check("viewport.info", function() return jot.viewport.info() end)
check("viewport.info.window", function() return jot.viewport.info().window end)
check("viewport.info.pane", function() return jot.viewport.info().pane end)
check("viewport.line_at", function() return jot.viewport.line_at(5) end)
check("viewport.scroll_top", function() jot.viewport.scroll_top(1) return true end)
check("viewport.scroll_lines", function() jot.viewport.scroll_lines(-2) return true end)
check("viewport.scroll_col", function() jot.viewport.scroll_col(1) return true end)
check("viewport.reveal", function() jot.viewport.reveal() return true end)

-- --------------------------------------------------------------- file tree
check("filetree.root", function() return jot.filetree.root() end)
check("filetree.tree", function() return jot.filetree.tree() end)
check("filetree.children", function() return jot.filetree.children("/nonexistent") end)

-- ----------------------------------------------------------------- buffer
check("buffer.current", function() return jot.buffer.current() end)
check("buffer.count", function() return jot.buffer.count() end)
check("buffer.list", function() return jot.buffer.list() end)
check("buffer.meta", function() return jot.buffer.meta() end)
check("buffer.text", function() return jot.buffer.text() end)
check("buffer.selection", function() return jot.buffer.selection() end)
check("buffer.bookmarks", function() return jot.buffer.bookmarks() end)
check("buffer.folds", function() return jot.buffer.folds() end)
check("buffer.tokens", function() return jot.buffer.tokens(1) end)
check("buffer.select", function() jot.buffer.select(1, 1, 1, 1) return true end)
check("buffer.clear_selection", function() jot.buffer.clear_selection() return true end)
check("buffer.var_roundtrip", function()
  jot.buffer.set_var("smoke", "v1")
  local v = jot.buffer.get_var("smoke")
  jot.buffer.del_var("smoke")
  return v
end)

-- ------------------------------------------------------------------ editor
check("editor.info", function() return jot.editor.info() end)
check("theme.current", function() return jot.theme.current() end)
check("theme.list", function() return jot.theme.list() end)
check("config.keys", function() return jot.config.keys() end)

-- ------------------------------------------------------ lua-first config
check("config.lua_loaded", function()
  return jot.config.get("smoke_lua_key") == "from_config_lua"
end)
check("config.live_apply", function()
  jot.config.set("tab_size", 4)
  local n = jot.config.get_number("tab_size")
  jot.config.set("tab_size", 3) -- restore what config.lua set
  return n == 4
end)
check("config.get", function() return jot.config.get("tab_size", "?") end)
check("config.set_roundtrip", function()
  jot.config.set("smoke_key", "42")
  local v = jot.config.get("smoke_key")
  jot.config.unset("smoke_key")
  return v
end)

-- ------------------------------------------------------------------- data
check("diagnostics.get", function() return jot.diagnostics.get() end)
check("marks.list", function() return jot.marks.list() end)
check("symbols.list", function() return jot.symbols.list() end)
check("git.info", function() return jot.git.info() end)
check("git.status", function() return jot.git.status() end)
check("tasks.list", function() return jot.tasks.list() end)
check("terminal.list", function() return jot.terminal.list() end)
check("workspace.path", function() return jot.workspace.path() end)
check("file.recent", function() return jot.file.recent() end)
check("picker.active", function() return jot.picker.active() end)
check("search.info", function() return jot.search.info() end)
check("search.matches", function() return jot.search.matches() end)
check("lsp.clients", function() return jot.lsp.clients() end)
check("lsp.diagnostics", function() return jot.lsp.diagnostics() end)
check("lsp.completions", function() return jot.lsp.completions() end)
check("lsp.results", function() return jot.lsp.results() end)

-- ------------------------------------------------------------ async/job
present("job.capture", jot.job and jot.job.capture)
present("lsp.request_hover", jot.lsp and jot.lsp.request_hover)
present("lsp.request_definition", jot.lsp and jot.lsp.request_definition)
present("lsp.request_symbols", jot.lsp and jot.lsp.request_symbols)
present("lsp.request_completion", jot.lsp and jot.lsp.request_completion)

-- ----------------------------------------------------------------- events
local subid = nil
check("events.subscribe", function()
  subid = jot.events.subscribe("git.refreshed", function(ev) return true end)
  return subid
end)
check("events.unsubscribe", function()
  if subid then
    jot.events.unsubscribe("git.refreshed", subid)
    return true
  end
  return false
end)
check("events.multi_sub", function()
  local a = jot.events.subscribe("lsp.hover", function() end)
  local b = jot.events.subscribe("lsp.hover", function() end)
  jot.events.unsubscribe("lsp.hover", a)
  jot.events.unsubscribe("lsp.hover", b)
  return true
end)

-- ----------------------------------------------------------- capabilities
check("capabilities.events", function() return jot.capabilities().events end)
check("capabilities.viewport", function() return jot.capabilities().viewport end)
check("capabilities.filetree", function() return jot.capabilities().filetree end)
check("capabilities.lsp", function() return jot.capabilities().lsp end)
check("capabilities.timer", function() return jot.capabilities().timer end)

-- ------------------------------------------------------------------ timer
check("timer.set_timeout", function()
  local id = jot.timer.set_timeout(30, function() end)
  return id ~= nil and id > 0
end)
check("timer.set_interval", function()
  local id = jot.timer.set_interval(50, function() end)
  return id ~= nil and id > 0
end)
check("timer.clear", function()
  local id = jot.timer.set_interval(20, function() end)
  jot.timer.clear(id)
  return true
end)
-- Liveness marker: this fires inside the real event loop AFTER the script
-- finishes (the driver keeps the editor alive a few seconds). The callback
-- appends TIMER_FIRED to the output file; the driver asserts it, proving the
-- native timer -> Lua callback path end to end.
jot.timer.set_timeout(150, function()
  local f = io.open(out_path, "a")
  if f then f:write("TIMER_FIRED\n") f:close() end
end)

-- ---------------------------------------------------------------- debugger
check("debugger.state", function() return jot.debugger.state() end)
check("debugger.breakpoints", function() return jot.debugger.breakpoints() end)
check("debugger.toggle_breakpoint", function()
  local p = (jot.editor.info().path or "/tmp/nonexistent.cpp")
  jot.debugger.toggle_breakpoint(p, 3)
  local on = jot.debugger.has_breakpoint(p, 3)
  jot.debugger.toggle_breakpoint(p, 3) -- toggle back off
  return on
end)
check("debugger.has_breakpoint", function()
  return jot.debugger.has_breakpoint("/tmp/nonexistent.cpp", 1) == false
end)
check("debugger.request_stack", function()
  jot.debugger.request_stack(function(res) return true end)
  return true
end)
check("debugger.request_variables", function()
  jot.debugger.request_variables(function(res) return true end)
  return true
end)
check("debugger.request_threads", function()
  jot.debugger.request_threads(function(res) return true end)
  return true
end)

-- ---------------------------------------------------------- theme palette
check("theme.palette", function() return jot.theme.palette() end)
check("theme.palette.slots", function()
  local p = jot.theme.palette()
  return p.default and p.default.fg ~= nil and p.status and p.status.bg ~= nil
end)

-- ------------------------------------------------- buffer.lines / clip.get
check("buffer.lines", function() return jot.buffer.lines() end)
check("clipboard.get", function() return jot.clipboard.get() end)
check("events.debugger_sub", function()
  local id = jot.events.subscribe("debugger.state_changed", function() end)
  jot.events.unsubscribe("debugger.state_changed", id)
  return true
end)
check("events.config_sub", function()
  local id = jot.events.subscribe("config.changed", function() end)
  jot.events.unsubscribe("config.changed", id)
  return true
end)

-- ---------------------------------------------------------------- motion
check("motion.word_next", function() jot.motion.word_next() return true end)
check("motion.word_prev", function() jot.motion.word_prev() return true end)
check("motion.line_start", function() jot.motion.line_start() return true end)
check("motion.line_end", function() jot.motion.line_end() return true end)
check("motion.file_start", function() jot.motion.file_start() return true end)
check("motion.file_end", function() jot.motion.file_end() return true end)
check("motion.matching_bracket", function()
  jot.motion.matching_bracket() return true
end)
check("motion.select_function", function()
  jot.motion.select_function() return true
end)

-- --------------------------------------------------------------- sidebar
check("sidebar.info", function() return jot.sidebar.info() end)

-- ---------------------------------------------------- command palette
check("ui.command_palette", function()
  jot.ui.command_palette(":reload")
  jot.ui.command_palette()
  return true
end)
check("sidebar.set_view", function()
  jot.sidebar.set_view("explorer")
  jot.sidebar.set_view("git")
  jot.sidebar.set_view("explorer")
  return true
end)

-- ------------------------------------------------------- lsp manager
check("lsp.disabled", function() return jot.lsp.disabled() end)
check("lsp.set_enabled", function()
  jot.lsp.set_enabled("clangd", true)
  return true
end)
check("lsp.restart_all", function() jot.lsp.restart_all() return true end)

-- ---------------------------------------------------- buffer extras
check("buffer.filetype", function() return jot.buffer.filetype() end)
check("buffer.get_line", function() return jot.buffer.get_line(1) end)

-- ---------------------------------------------------------- git.diff
check("git.diff", function() return jot.git.diff("/nonexistent") end)

log("PASS=" .. pass)
log("FAIL=" .. fail)
log("SMOKE_DONE")
if logf then logf:close() end