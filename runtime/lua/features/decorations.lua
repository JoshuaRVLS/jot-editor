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

-- End-of-line message for a diagnostic, in the severity colour. The lead marker
-- is a plain dot, not a corner: the message sits beside the code, so anything
-- with a stem in it (a "|-" / corner glyph) would imply a line below that is
-- not there. The renderer draws only the first virt_text it finds on a row, so
-- the map below keeps exactly one per line.
local VIRT_TEXT_KEY = "diagnostics_virtual_text"
local VIRT_TEXT_PREFIX = "  ● "

local function virtual_text_enabled()
  return jot.config.get(VIRT_TEXT_KEY, "true") ~= "false"
end

local function one_line(text)
  return (text or ""):gsub("%s+", " "):gsub("^%s+", ""):gsub("%s+$", "")
end

local function underline_style()
  local style = (jot.config.get(STYLE_KEY, "wavy") or "wavy"):lower()
  return UNDERLINE_STYLE[style] or UNDERLINE_STYLE.wavy
end

local function apply_diagnostics(info)
  local want_underline = enabled()
  local want_text = virtual_text_enabled()
  if not want_underline and not want_text then
    return
  end
  local buffer = info.buffer
  if not buffer then
    return
  end
  jot.decoration.clear(buffer)
  local diagnostics = jot.diagnostics.get(buffer) or {}
  local underline = underline_style()

  -- The most severe diagnostic on a line owns that line's message: an error
  -- must not be hidden behind a warning reported on the same line.
  local loudest = {}
  if want_text then
    for _, d in ipairs(diagnostics) do
      local current = loudest[d.line]
      if not current or (d.severity or 4) < (current.severity or 4) then
        loudest[d.line] = d
      end
    end
  end

  for _, d in ipairs(diagnostics) do
    local hl = SEVERITY_HL[d.severity] or "diagnostic_info"
    local width = math.max(0, (d.end_col or (d.col + 1)) - d.col)
    -- Underline over the reported range (byte columns, 1-based):
    -- 2 = wavy squiggle (SGR 4:3), 1 = plain straight underline.
    local deco = {
      row = d.line,
      col = d.col,
      width = width,
      priority = 10,
    }
    if want_underline and underline ~= 0 and width > 0 then
      deco.underline = underline
      deco.underline_hl = hl
    end
    if want_text and loudest[d.line] == d then
      deco.virt_text = VIRT_TEXT_PREFIX .. one_line(d.message)
      deco.virt_hl = hl
    end
    if deco.underline or deco.virt_text then
      jot.decoration.set(buffer, deco)
    end
  end
end

jot.autocmd("DiagnosticChanged", apply_diagnostics)
