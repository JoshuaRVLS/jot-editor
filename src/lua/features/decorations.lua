-- Inline diagnostics via anchored decorations (extmark-style).
--
-- Listens for DiagnosticChanged and re-applies one decoration per
-- diagnostic using the jot.decoration.* API: a wavy underline over the
-- reported range in the severity color (VSCode-style squiggle — the text
-- itself keeps its syntax colors; only the underline is colored).
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

local function enabled()
  return jot.config.get(ENABLED_KEY, "true") ~= "false"
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
    -- Wavy underline over the reported range (byte columns, 1-based).
    -- underline=1 would be a straight underline; 2 is the wavy squiggle.
    local width = math.max(0, (d.end_col or (d.col + 1)) - d.col)
    if width > 0 then
      jot.decoration.set(buffer, {
        row = d.line,
        col = d.col,
        width = width,
        underline = 2,
        underline_hl = hl,
        priority = 10,
      })
    end
  end
end

jot.autocmd("DiagnosticChanged", apply_diagnostics)