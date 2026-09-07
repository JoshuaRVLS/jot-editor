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
--
-- Squiggle style: jot.config.set("decorations_underline_style", "straight")
-- for a plain underline on terminals without SGR 4:3 curly-underline
-- support (older Alacritty, some Windows consoles); "none" hides them.
-- "wavy" (SGR 4:3) is the default.

local jot = jot

local ENABLED_KEY = "decorations_inline_diagnostics"
local STYLE_KEY = "decorations_underline_style"

local UNDERLINE_STYLE = {
  wavy = 2,     -- SGR 4:3 squiggle
  straight = 1, -- plain underline, supported by every terminal
  none = 0,
}

local SEVERITY_HL = {
  [1] = "diagnostic_error",
  [2] = "diagnostic_warning",
  [3] = "diagnostic_info",
  [4] = "diagnostic_hint",
}

local function enabled()
  return jot.config.get(ENABLED_KEY, "true") ~= "false"
end

local function underline_style()
  local style = (jot.config.get(STYLE_KEY, "wavy") or "wavy"):lower()
  return UNDERLINE_STYLE[style] or UNDERLINE_STYLE.wavy
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
  local underline = underline_style()
  if underline == 0 then
    return
  end
  for _, d in ipairs(diagnostics) do
    local hl = SEVERITY_HL[d.severity] or "diagnostic_info"
    -- Underline over the reported range (byte columns, 1-based):
    -- 2 = wavy squiggle (SGR 4:3), 1 = plain straight underline.
    local width = math.max(0, (d.end_col or (d.col + 1)) - d.col)
    if width > 0 then
      jot.decoration.set(buffer, {
        row = d.line,
        col = d.col,
        width = width,
        underline = underline,
        underline_hl = hl,
        priority = 10,
      })
    end
  end
end

jot.autocmd("DiagnosticChanged", apply_diagnostics)
