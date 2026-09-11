-- Opening the preview in a browser. The configured `markdown_preview_browser`
-- selects a specific browser; otherwise the platform opener is used. Launching
-- goes through jot.job.capture so it happens on a worker thread and never
-- blocks the editor.
local config = require("jot_md.config")

local M = {}

local function is_windows()
  return package.config ~= nil and package.config:sub(1, 1) == "\\"
end

-- name -> opening command prefix (the URL is appended, shell-quoted).
-- "open -a X" entries only make sense on macOS; they are harmless elsewhere
-- because the whole fallback chain is tried with `||`.
local BROWSERS = {
  firefox = "firefox",
  chrome = "google-chrome",
  chromium = "chromium",
  ["google-chrome"] = "google-chrome",
  edge = "microsoft-edge",
  brave = "brave",
  brave_browser = "brave",
  opera = "opera",
  vivaldi = "vivaldi",
  qutebrowser = "qutebrowser",
  safari = "open -a Safari",
  arc = "open -a Arc",
}

local function shell_quote(text)
  return "'" .. tostring(text):gsub("'", "'\\''") .. "'"
end

-- The shell command that opens `url`, honoring the configured browser.
function M.command(url)
  local quoted = shell_quote(url)
  local choice = tostring(config.get("browser") or ""):lower()

  if choice == "none" then
    return nil
  end
  if choice ~= "" then
    local prefix = BROWSERS[choice]
    if prefix then
      return prefix .. " " .. quoted
    end
    -- Unknown name: treat it as a literal command (custom browser).
    return choice .. " " .. quoted
  end

  if is_windows() then
    return 'cmd.exe /c start "" ' .. shell_quote(url)
  end
  local chain = {
    "xdg-open " .. quoted,
    "gio open " .. quoted,
    "open " .. quoted,
  }
  return table.concat(chain, " >/dev/null 2>&1 || ") .. " >/dev/null 2>&1"
end

-- The URL the preview is reachable at. Always the loopback alias so the
-- address is clickable no matter what the bind address was.
function M.url(port)
  return "http://127.0.0.1:" .. tostring(port) .. "/"
end

-- Launches the browser. Returns true when a launcher was dispatched.
function M.open(url)
  local command = M.command(url)
  if not command then
    return false
  end
  local ok = pcall(function()
    if jot.job and jot.job.capture then
      jot.job.capture(command, nil, function() end)
    else
      jot.job.run(command, nil, "markdown-preview")
    end
  end)
  return ok
end

return M
