-- Search — part of the Lua UI kit.
-- Split out of features/ui.lua so each surface stays small and
-- focused; features/ui.lua is the orchestrator that requires
-- every module and registers the handlers.
local h = require("jot_ui.helpers")
local close = h.close
local cell_len = h.cell_len
local trunc_cells = h.trunc_cells
local pad_cells = h.pad_cells
local pad = h.pad
local present_panel = h.present_panel
local function search_field_row(p, label, text, focused, input_w, colors)
  -- Mirrors the native geometry: "Find"/"Replace" label at col 1, input text
  -- from col label_w (9) so the natively-placed caret sits on this field.
  local selection_fg = colors.selection_fg or 0
  local selection_bg = colors.selection_bg or 6
  local comment = colors.comment or 8
  local fg = colors.fg or 7
  local bg = colors.panel_bg or colors.bg or 0
  -- "Find"/"Replace" label starts at interior col 0; the input text starts
  -- at the native label_w column (8 here) so the caret lands on this field.
  local line = pad_cells(label, 8)
  local field_start = #line -- byte offset of the input area
  local shown = trunc_cells(text or "", math.max(1, input_w))
  line = line .. shown
  local spans = {
    { start = 0, len = 65535, fg = fg, bg = bg },
  }
  if focused then
    spans[#spans + 1] = { start = field_start, len = 65535, fg = selection_fg, bg = selection_bg }
  end
  return { text = line, spans = spans }
end

local function search_panel(p)
  if not p then
    close("search_panel")
    return true
  end
  local colors = p.colors or {}
  local fg = colors.fg or 7
  local bg = colors.panel_bg or colors.bg or 0
  local comment = colors.comment or 8
  local accent = colors.accent or colors.fg or 7

  local inner_w = math.max(1, (p.w or 2) - 2)
  local label_w = 9
  local input_w = math.max(1, inner_w - label_w - 2)
  local rows = {}
  local focus_replace = p.replace_visible and p.focus_replace
  rows[#rows + 1] = search_field_row(p, "Find", p.query, not focus_replace, input_w, colors)
  if p.replace_visible then
    rows[#rows + 1] = search_field_row(p, "Replace", p.replace_text, focus_replace, input_w, colors)
  end
  -- Footer: state chips only, right-aligned. The chips report what the search is
  -- doing (case, whole word, regex, selection scope, match count); the key hints
  -- that used to sit to their left were instructions, not state.
  local parts = {}
  local offsets = {}
  local function chip(text, active)
    local col = cell_len(table.concat(parts))
    local padded = trunc_cells(text, math.max(1, inner_w - col))
    parts[#parts + 1] = padded
    offsets[#offsets + 1] = { start = #table.concat(parts) - #padded, len = #padded, fg = active and accent or comment }
  end
  chip(" " .. (p.case_sensitive and "Aa" or "aa") .. " ", p.case_sensitive)
  chip((p.whole_word and "W" or "w") .. " ", p.whole_word)
  if p.regex then
    chip(".* ", true)
  end
  if p.scoped_to_selection then
    chip("Sel ", true)
  end
  chip((p.count or "0/0") .. " ", false)
  local right = table.concat(parts)
  local line = string.rep(" ", math.max(0, inner_w - cell_len(right))) .. right
  -- Chip offsets on the final line: each chip sits at its part start inside the
  -- right-aligned run.
  local base = inner_w - cell_len(right)
  for _, sp in ipairs(offsets) do
    sp.start = sp.start + base
  end
  rows[#rows + 1] = { text = line, fg = comment, bg = bg, spans = offsets }
  return present_panel("search_panel",
                       p,
                       rows,
                       {
                         border = "single",
                         title = (p.scoped_to_selection and " Find in Selection" or " Find"),
                         title_fg = accent,
                       })
end


return {
  search_panel = search_panel,
  search_field_row = search_field_row,
}
