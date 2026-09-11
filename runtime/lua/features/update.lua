-- Self-update (features/update.lua).
--
-- Keeps a source-built jot in sync with its git clone. `:update` checks the
-- remote for new commits behind a small animated status panel; `:update run`
-- pulls, rebuilds the existing CMake build tree and reinstalls. A silent
-- check also runs shortly after boot (update.check_on_startup) but never
-- opens a panel: it only arms the statusline " ↑N" chip (plus the Ctrl+U
-- shortcut) when the repo is actually behind, so startup stays quiet.
--
-- Only meaningful for binaries built from a git checkout: the repo root is
-- resolved natively (jot.source_dir: $JOT_SOURCE_DIR override, then the
-- developer source dir baked into the build) and is empty for plain
-- installed binaries, which then get a friendly "no source clone" notice.
--
-- Pure Lua on top of jot.job.capture / jot.ui.float / jot.timer, so it keeps
-- the event loop responsive: every git call runs on the worker queue and the
-- panel animates independently.

local update = {}

local SPIN_FRAMES = { "⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏" }
local SPIN_MS = 90
local BOOT_CHECK_DELAY_MS = 2500
local AUTO_CLOSE_MS = 7000

-- Tuned config keys (all optional):
--   update.check_on_startup   boot-time silent check        (bool, default true)
--   update.build_dir          repo-relative build dir hint (string, optional)
local DEFAULTS = { "build-enabled", "build", "build-release", "build-debug" }

local panel = {
  open = false,
  busy = false,
  silent = false, -- boot-time check: statusline chip only, no panel/toast
  win = 0,
  buf = 0,
  timer = 0,
  close_timer = 0,
  spin = 0,
  repo = "",
  branch = "",
  sha = "",
  behind = 0,
  ahead = 0,
  level = "info", -- info | success | warning | error
}

-- Safe config reads (evaluated inside pcall: args evaluate before pcall).
-- Note: an explicit `and v or def` would mis-return `def` for `v=false`
-- (false is falsy), so the boolean must be branched on explicitly.
local function cfg_bool(key, def)
  local ok, v = pcall(function() return jot.config.get_bool(key, def) end)
  if ok and type(v) == "boolean" then
    return v
  end
  return def
end

local function cfg_str(key, def)
  local ok, v = pcall(function() return jot.config.get(key, def) end)
  return (ok and type(v) == "string" and v ~= "") and v or def
end

local function trim(s)
  return (s or ""):gsub("^%s+", ""):gsub("%s+$", "")
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

-- Full UI stack present? Headless unit runtimes stub pieces of jot.*, so the
-- module must not open floats or schedule timers there.
local function runtime_ready()
  local ok = pcall(function()
    return type(jot) == "table" and type(jot.job) == "table" and type(jot.job.capture) == "function"
      and type(jot.ui) == "table" and type(jot.ui.float) == "table"
      and type(jot.ui.float.open) == "function"
      and type(jot.ui.buffer) == "table" and type(jot.ui.buffer.create) == "function"
      and type(jot.timer) == "table" and type(jot.timer.set_timeout) == "function"
      and type(jot.viewport) == "table" and type(jot.viewport.info) == "function"
  end)
  return ok and true or false
end

-- Source clone detection. jot.source_dir is authoritative (natively resolved
-- and validated); a $JOT_SOURCE_DIR env fallback keeps older binaries working.
local function repo_root()
  local r = ""
  local ok = pcall(function()
    if type(jot.source_dir) == "string" then
      r = jot.source_dir
    end
  end)
  if ok and r ~= "" then
    return r
  end
  local ok2, env = pcall(os.getenv, "JOT_SOURCE_DIR")
  return (ok2 and type(env) == "string") and env or ""
end

local function is_windows()
  local ok, osname = pcall(os.getenv, "OS")
  return ok and osname == "Windows_NT"
end

-- Theme-driven palette (fallbacks mirror toast.lua).
local function palette()
  local theme = {}
  local ok = pcall(function() theme = jot.theme.palette() or {} end)
  if not ok or type(theme) ~= "table" then
    theme = {}
  end
  local function pick(slot, field, fallback)
    local s = type(theme[slot]) == "table" and theme[slot][field]
    return type(s) == "number" and s or fallback
  end
  return {
    bg = pick("default", "bg", 235),
    fg = pick("default", "fg", 250),
    title = pick("status_info", "fg", 251),
    info = pick("status_info", "fg", 215),
    success = pick("diagnostic_hint", "fg", 108),
    warning = pick("diagnostic_warning", "fg", 178),
    error = pick("diagnostic_error", "fg", 167),
  }
end

-- One-line message detail (never spills the panel).
local function one_line(s)
  s = tostring(s or "")
  s = s:gsub("\r", " "):gsub("\n", " ")
  if #s > 96 then
    s = s:sub(1, 93) .. "..."
  end
  return s
end

-- Rebuilds the four content rows of the panel from the current state.
-- Row layout: title / spinner+primary / detail / hint.
local function content_rows(colors)
  local accent = colors[panel.level] or colors.info
  local spin = panel.busy and SPIN_FRAMES[(panel.spin % #SPIN_FRAMES) + 1] or "⠿"
  local primary = panel.primary or ""
  if panel.busy and primary == "" then
    primary = "Working…"
  end
  return {
    { text = "  jot update", fg = colors.title },
    { text = " " .. spin .. "  " .. primary, fg = accent },
    { text = (panel.detail or "") ~= "" and (" " .. (panel.detail or "")) or "", fg = colors.fg },
    { text = (panel.hint or "") ~= "" and (" " .. (panel.hint or "")) or "", fg = colors.warning },
  }, accent
end

local function panel_lines(rows)
  local lines = {}
  for i = 1, 4 do
    lines[i] = (rows[i] and rows[i].text or "") .. " "
  end
  return lines
end

local function repaint()
  if not panel.open then
    return
  end
  local colors = palette()
  local rows, accent = content_rows(colors)
  local lines = panel_lines(rows)
  pcall(jot.ui.buffer.set_lines, panel.buf, 0, -1, true, lines)
  for i = 1, 4 do
    if rows[i] and rows[i].text ~= "" then
      -- Row spans: whole row in the row color. Byte offsets are 0-based.
      pcall(jot.ui.float.set_spans, panel.win, i,
            { { start = 0, len = #rows[i].text, fg = rows[i].fg, bg = -1 } })
    end
  end
  pcall(jot.ui.float.configure, panel.win, { border_fg = accent })
  request_redraw()
end

-- Availability indicator ---------------------------------------------------
-- While the repo is behind origin we surface it twice outside the panel:
--   * a statusline chip (" ↑N" in the warning color, right side);
--   * a which-key / held-modifier entry: Ctrl+U runs `:update run` directly.
-- Both appear only when updates are actually available and disappear as soon
-- as the repo is up to date again (check, or a finished :update run).
local ind = { status = false, keymap = false }

local function update_indicator()
  local on = (panel.behind or 0) > 0 and not panel.busy
  if on and not ind.status then
    local colors = palette()
    local fg = colors.warning
    local n = panel.behind
    local ok = pcall(jot.status.register, "update", {
      side = "right",
      priority = 90,
      fg = fg,
      text = function()
        local b = panel.behind or 0
        return b > 0 and (" ↑" .. b) or ""
      end,
    })
    ind.status = ok
    local ok2 = pcall(function()
      jot.keymap.set("Ctrl+U", ":update run", "Update jot to latest (pull & rebuild)")
    end)
    ind.keymap = ok2
    if ok or ok2 then
      request_redraw()
    end
    return
  end
  if not on and (ind.status or ind.keymap) then
    if ind.status then
      pcall(jot.status.unregister, "update")
      ind.status = false
    end
    if ind.keymap then
      pcall(jot.keymap.remove, "Ctrl+U")
      ind.keymap = false
    end
    request_redraw()
  end
end

local function clear_timers()
  if panel.timer and panel.timer ~= 0 then
    pcall(jot.timer.clear, panel.timer)
    panel.timer = 0
  end
  if panel.close_timer and panel.close_timer ~= 0 then
    pcall(jot.timer.clear, panel.close_timer)
    panel.close_timer = 0
  end
end

local function close_panel()
  if not panel.open then
    return
  end
  clear_timers()
  if panel.win and panel.win ~= 0 then
    pcall(jot.ui.float.close, panel.win)
    panel.win = 0
  end
  if panel.buf and panel.buf ~= 0 then
    pcall(jot.ui.buffer.delete, panel.buf)
    panel.buf = 0
  end
  panel.open = false
  request_redraw()
end

-- Spinner tick while busy; also re-renders the current state.
local function spin_tick()
  panel.spin = panel.spin + 1
  repaint()
end

local function open_panel()
  if panel.open then
    repaint()
    return true
  end
  local ww, _ = window_size()
  local width = math.min(64, math.max(44, ww - 10))
  local height = 6 -- 2 borders + 4 content rows
  local okc, buf = pcall(jot.ui.buffer.create, false, true)
  if not okc then
    return false
  end
  pcall(jot.ui.buffer.set_lines, buf, 0, -1, true, { " ", " ", " ", " " })
  local colors = palette()
  local okw, win = pcall(jot.ui.float.open, buf, {
    col = math.max(2, math.floor((ww - width) / 2)),
    row = 2,
    width = width,
    height = height,
    relative = "editor",
    anchor = "NW",
    border = "single",
    focusable = false,
    mouse = true,
    hide = false,
    zindex = 100000,
    fg = colors.fg,
    bg = colors.bg,
    border_fg = colors.info,
    on_mouse = function()
      -- Clicking dismisses once idle; during a job it is ignored.
      if not panel.busy then
        close_panel()
      end
    end,
  })
  if not okw or not win or win == 0 then
    pcall(jot.ui.buffer.delete, buf)
    return false
  end
  panel.win = win
  panel.buf = buf
  panel.open = true
  panel.busy = false
  panel.silent = false -- an open panel is always interactive
  panel.spin = 0
  panel.primary = ""
  panel.detail = ""
  panel.hint = ""
  local ok, id = pcall(jot.timer.set_interval, SPIN_MS, spin_tick)
  if ok then
    panel.timer = id or 0
  end
  repaint()
  return true
end

local function arm_auto_close(ms)
  if panel.close_timer and panel.close_timer ~= 0 then
    pcall(jot.timer.clear, panel.close_timer)
    panel.close_timer = 0
  end
  local ok, id = pcall(jot.timer.set_timeout, ms or AUTO_CLOSE_MS, close_panel)
  if ok then
    panel.close_timer = id or 0
  end
end

-- When no panel is open (e.g. :update on a binary without a source clone)
-- the result goes to the transient message channel instead of thin air.
local function notify_result(level, primary, detail)
  local line = primary
  if detail and detail ~= "" then
    line = line .. " — " .. one_line(detail)
  end
  pcall(jot.notify, line, 6000)
end

local function show_result(level, primary, detail, hint)
  panel.busy = false
  panel.level = level
  panel.primary = primary
  panel.detail = one_line(detail)
  panel.hint = hint or "click to dismiss"
  update_indicator()
  if panel.open then
    repaint()
    arm_auto_close()
    return
  end
  if panel.silent then
    -- Boot-time checks never toast: the statusline chip (and the Ctrl+U
    -- shortcut it arms) is the whole surface, appearing only when updates
    -- are actually available.
    return
  end
  notify_result(level, primary, detail)
end

local function start_job(primary, detail, cmd, cb)
  panel.busy = true
  panel.level = "info"
  panel.primary = primary
  panel.detail = detail
  panel.hint = ""
  repaint()
  local ok = pcall(jot.job.capture, cmd, panel.repo, cb)
  if not ok then
    show_result("error", "Could not start git", "Check that jot.job is available", nil)
  end
end

local function git(args, cb)
  start_job("", "", "git " .. args, cb)
end

-- Pipeline: branch -> remote -> fetch -> counts. Every step is async so the
-- panel can animate; failures land in show_result instead of crashing. In
-- silent (boot) mode failures just fold the panel away: an offline machine or
-- a misconfigured repo must never nag the user on every startup.
local function check_chain(silent)
  local function quiet()
    panel.busy = false
    update_indicator()
    if panel.open then
      close_panel()
    end
  end
  local function fail(msg)
    if silent then
      quiet()
      return
    end
    show_result("error", "Update check failed", msg, nil)
  end

  git("rev-parse --abbrev-ref HEAD", function(res)
    if res.exit_code ~= 0 then
      fail("Not a git checkout? (" .. one_line(res.output) .. ")")
      return
    end
    panel.branch = trim(res.output)
    git("remote get-url origin", function(r)
    if r.exit_code ~= 0 then
      if silent then
        quiet()
        return
      end
      show_result("warning", "No git remote configured",
                  "jot has no origin to fetch from in " .. panel.repo, nil)
      return
    end
      local branch = panel.branch
      start_job("Fetching origin…", "looking for new commits", "git fetch --quiet --prune origin",
        function(f)
          if f.exit_code ~= 0 then
            if silent then
              quiet()
              return
            end
            show_result("error", "Could not reach the remote",
                        one_line(f.output), nil)
            return
          end
          git("rev-list --count HEAD..origin/" .. branch .. " 2>/dev/null; echo --; "
              .. "git rev-list --count origin/" .. branch .. "..HEAD 2>/dev/null; echo --; "
              .. "git rev-parse --short HEAD", function(c)
            local parts = {}
            for part in (c.output or ""):gmatch("[^\n]+") do
              if trim(part) ~= "" and trim(part) ~= "--" then
                parts[#parts + 1] = trim(part)
              end
            end
            local behind = tonumber(parts[1])
            local ahead = tonumber(parts[2])
            local sha = parts[3] or ""
            panel.sha = sha
            panel.behind = behind or 0
            panel.ahead = ahead or 0
            if behind == nil then
              -- rev-list printed nothing: no origin/<branch> yet.
              if silent then
                quiet()
                return
              end
              show_result("info", "Up to date",
                          "origin/" .. branch .. " does not exist yet — push it from here",
                          "click to dismiss")
              return
            end
            if behind > 0 then
              local p = (behind == 1) and "commit" or "commits"
              show_result("warning",
                          behind .. " new " .. p .. " available",
                          "origin/" .. branch .. " is ahead of " .. sha
                            .. " — run :update run to pull & rebuild",
                          "click to dismiss")
            elseif silent then
              quiet()
            else
              local a = ""
              if ahead > 0 then
                a = " (" .. ahead .. " local ahead)"
              end
              show_result("success", "Up to date",
                          sha .. " on " .. branch .. a, "click to dismiss")
            end
          end)
        end)
    end)
  end)
end

local function check_update(silent)
  if panel.busy then
    return
  end
  if not runtime_ready() then
    if not silent then
      notify_result("info", "Update UI unavailable", "headless runtime")
    end
    return
  end
  panel.repo = repo_root()
  if panel.repo == "" then
    if not silent then
      show_result("info", "Nothing to update",
                  "This jot has no source clone to fetch from. Install from "
                    .. "the release build instead.", "click to dismiss")
    end
    return
  end
  panel.silent = silent
  if silent then
    -- Boot-time check: run the git chain headless; only the statusline
    -- chip (and the Ctrl+U shortcut) surface the result.
    check_chain(true)
    return
  end
  if not open_panel() then
    return
  end
  panel.primary = "Checking for updates…"
  panel.detail = ""
  panel.hint = ""
  check_chain(false)
end

local function run_update()
  if panel.busy then
    return
  end
  if not runtime_ready() then
    notify_result("info", "Update UI unavailable", "headless runtime")
    return
  end
  panel.repo = repo_root()
  if panel.repo == "" then
    show_result("info", "Nothing to update",
                "This jot has no source clone to rebuild from.", "click to dismiss")
    return
  end
  if is_windows() then
    show_result("info", "Windows source update not supported yet",
                "Rebuild jot from the checkout manually.", "click to dismiss")
    return
  end
  if not open_panel() then
    return
  end
  panel.behind = 0
  panel.ahead = 0
  update_indicator()

  -- Prefer the configured build dir; otherwise scan the common ones for an
  -- existing CMake tree so the rebuild never reconfigures from scratch.
  local hint = cfg_str("update.build_dir", "")
  local candidates = {}
  if hint ~= "" then
    candidates[#candidates + 1] = hint
  end
  for _, d in ipairs(DEFAULTS) do
    if d ~= hint then
      candidates[#candidates + 1] = d
    end
  end
  local probe = ""
  for _, d in ipairs(candidates) do
    probe = probe .. "[ -f \"" .. d .. "/CMakeCache.txt\" ] && { BD=\"" .. d .. "\"; break; }; "
  end

  local script = table.concat({
    "set -e",
    "git pull --rebase --ff-only",
    "BD=\"\"",
    probe .. "true",
    "if [ -z \"$BD\" ]; then echo \"__NO_BUILD_TREE__\"; exit 3; fi",
    "cmake --build \"$BD\" -j",
    "cmake --install \"$BD\"",
    "git rev-parse --short HEAD",
  }, "\n")

  start_job("Pulling + rebuilding…", "this can take a minute",
            "sh -c " .. string.format("%q", script) .. "", function(res)
    local out = res.output or ""
    if res.exit_code == 0 then
      local sha = trim(out:match("([^\n]*)$") or "")
      show_result("success", "Updated to " .. sha,
                  "Restarting with the new build…", "restarting")
      -- Give the success state a moment to paint, then swap in the fresh
      -- binary (self-restart replays the launch args, so the same files or
      -- workspace reopen automatically).
      pcall(jot.timer.set_timeout, 900, function()
        local okr, started = pcall(jot.restart)
        if not okr or not started then
          show_result("error", "Auto-restart skipped",
                      "Update applied — relaunch jot manually to load it.",
                      "click to dismiss")
        end
      end)
    elseif res.exit_code == 3 then
      show_result("error", "No build tree found",
                  "Nothing in the clone matches a configured CMake build dir. "
                    .. "Set update.build_dir in settings.conf.", "click to dismiss")
    else
      local lines = {}
      for part in out:gmatch("[^\n]+") do
        if trim(part) ~= "" then
          lines[#lines + 1] = trim(part)
        end
      end
      local tail = table.concat(lines, " | ")
      show_result("error", "Update failed", one_line(tail), "click to dismiss")
    end
  end)
end

local function boot_check()
  if not cfg_bool("update.check_on_startup", true) then
    return
  end
  if repo_root() == "" then
    return
  end
  local ok = pcall(jot.timer.set_timeout, BOOT_CHECK_DELAY_MS,
                   function() check_update(true) end)
  if not ok then
    -- No timer available (test runtime): check immediately, silently.
    check_update(true)
  end
end

-- Public entry points --------------------------------------------------------

function update.check()
  check_update(false)
end

function update.run()
  run_update()
end

function update.info()
  return {
    repo = repo_root(),
    open = panel.open,
    busy = panel.busy,
    branch = panel.branch,
    sha = panel.sha,
    behind = panel.behind,
    ahead = panel.ahead,
  }
end

function update.close()
  close_panel()
end

-- Command surface: :update / :update run. The native ex branch calls this
-- handler (see Editor::execute_ex_command_tail) so typed commands reach the
-- module exactly like builtin ones.
pcall(function()
  jot.register_update_handler(function(args)
    local a = trim(args)
    if a == "run" or a == "build" or a == "install" then
      run_update()
    elseif a == "close" or a == "cancel" then
      close_panel()
    else
      check_update(false)
    end
  end)
end)

boot_check()

return update
