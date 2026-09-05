-- Inline diagnostics via anchored decorations (extmark-style).
--
-- Listens for DiagnosticChanged and re-applies two decorations per
-- diagnostic using the jot.decoration.* API:
--   * a low-priority span over the reported range (severity color),
--   * end-of-line virtual text ("⚠ message") in the severity color.
--
-- Decorations are anchored: they follow the text across edits, so inline
-- diagnostics stay glued to the code they describe. The native renderer
-- draws them over syntax but under selection and search matches.
--
-- Disable with: jot.config.set("decorations_inline_diagnostics", false)
-- (the native cursor-line inline diagnostic is suppressed while this is on).

local jot = jot

local ENABLED_KEY = "decorations_inline_diagnostics"

local SEVERITY_HL = {
  [1] = "diagnostic_error",
  [2] = "diagnostic_warning",
  [3] = "diagnostic_info",
  [4] = "diagnostic_hint",
}

local SEVERITY_GLYPH = {
  [1] = "⚠",
  [2] = "⚠",
  [3] = "ℹ",
  [4] = "…",
}

local function enabled()
  return jot.config.get(ENABLED_KEY, "true") ~= "false"
end

local function truncate(s, max)
  s = tostring(s or "")
  if #s <= max then
    return s
  end
  return s:sub(1, max - 3) .. "..."
end

local function apply_diagnostics(info)
  if not enabled() then
    return
  end
  local buffer = info.buffer
  if not buffer then
    return
  end
  jot.decoration.clear(buffer)
  local diagnostics = jot.diagnostics.get(buffer) or {}
  for _, d in ipairs(diagnostics) do
    local hl = SEVERITY_HL[d.severity] or "diagnostic_info"
    -- Span over the reported range (byte columns, 1-based like the API).
    local width = math.max(0, (d.end_col or (d.col + 1)) - d.col)
    jot.decoration.set(buffer, {
      row = d.line,
      col = d.col,
      width = width,
      hl = hl,
      priority = 10,
    })
    -- End-of-line virtual text on the diagnostic's start line.
    local msg = truncate(d.message, 60)
    jot.decoration.set(buffer, {
      row = d.line,
      col = 1,
      virt_text = " " .. (SEVERITY_GLYPH[d.severity] or "•") .. " " .. msg,
      virt_hl = hl,
      priority = 20,
    })
  end
end

jot.autocmd("DiagnosticChanged", apply_diagnostics)