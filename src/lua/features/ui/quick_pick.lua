-- Quick Pick — part of the Lua UI kit.
-- Split out of features/ui.lua so each surface stays small and
-- focused; features/ui.lua is the orchestrator that requires
-- every module and registers the handlers.
local h = require("jot_ui.helpers")
local close = h.close
local rune_len = h.rune_len
local truncate = h.truncate
local pad = h.pad
local match_spans = h.match_spans
local present_panel = h.present_panel
local title_with_count = h.title_with_count
local function quick_pick(p)
  if not p then
    close("quick_pick")
    return true
  end
  local colors = p.colors or {}
  local fg = colors.fg or 7
  local bg = colors.panel_bg or colors.bg or 0
  local border = colors.border or fg
  local selection_fg = colors.selection_fg or 0
  local selection_bg = colors.selection_bg or 6
  local comment = colors.comment or 8
  local accent = colors.accent or 6

  local inner_w = math.max(1, p.w - 2)
  local title = (p.title and p.title ~= "" and " " .. p.title) or " Quick Pick"
  title = title_with_count(title, tostring(#(p.items or {})) .. "/" .. tostring(p.all_count or 0), inner_w)

  local rows = {}
  local query = p.query or ""
  rows[1] = { text = pad(truncate("> " .. query, inner_w), inner_w), fg = selection_fg, bg = selection_bg }
  rows[2] = { text = string.rep("─", inner_w), fg = border, bg = bg }

  local items = p.items or {}
  local selected = math.max(0, p.selected or 0)
  local list_h = math.max(0, p.h - 5) -- input + divider + footer
  local start_idx = 0
  if #items > 0 then
    start_idx = math.max(0, selected - list_h + 1)
    if start_idx + list_h > #items then
      start_idx = math.max(0, #items - list_h)
    end
  end
  local detail_w = p.w >= 72 and math.max(16, math.floor(p.w / 3)) or 0
  local label_w = math.max(8, inner_w - detail_w - 3)
  for row = 1, list_h do
    local idx = start_idx + row
    if idx > #items then
      break
    end
    local item = items[idx]
    local is_selected = idx - 1 == selected
    local row_fg = is_selected and selection_fg or fg
    local row_bg = is_selected and selection_bg or bg
    local prefix = is_selected and " ▎" or "  "
    local label = truncate(item.label or "", label_w)
    local spans = match_spans(label, query, is_selected and selection_fg or accent)
    -- match_spans offsets are bytes into label; shift them past the prefix
    -- (which is multibyte when selected: " ▎") so they point into the row.
    for _, sp in ipairs(spans) do
      sp.start = sp.start + #prefix
    end
    local line = prefix .. label
    if detail_w > 0 and item.detail and item.detail ~= "" then
      line = line .. string.rep(" ", math.max(1, inner_w - rune_len(line) - detail_w))
        .. truncate(item.detail, detail_w)
    end
    rows[#rows + 1] = { text = pad(line, inner_w), fg = row_fg, bg = row_bg, spans = spans }
  end

  local footer
  if selected >= 0 and selected < #items and items[selected + 1].preview
      and items[selected + 1].preview ~= "" then
    footer = items[selected + 1].preview
  else
    footer = "Enter open   Esc close   Up/Down move   PgUp/PgDn page"
  end

  return present_panel("quick_pick", p, rows, { title = title, title_fg = accent, footer = footer })
end


return {
  quick_pick = quick_pick,
}
