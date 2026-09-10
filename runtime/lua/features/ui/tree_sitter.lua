-- Tree Sitter — part of the Lua UI kit.
-- Split out of features/ui.lua so each surface stays small and
-- focused; features/ui.lua is the orchestrator that requires
-- every module and registers the handlers.
local h = require("jot_ui.helpers")
local close = h.close
local trunc_cells = h.trunc_cells
local pad_cells = h.pad_cells
local truncate = h.truncate
local pad = h.pad
local present_panel = h.present_panel
local function tree_sitter_status(p)
  if not p then
    close("tree_sitter_status")
    return true
  end
  local colors = p.colors or {}
  local fg = colors.fg or 7
  local bg = colors.panel_bg or colors.bg or 0
  local comment = colors.comment or 8
  local inner_w = math.max(1, p.w - 2)

  local list_h = math.max(0, p.h - 4)
  local rows = p.rows or {}
  local scroll = math.max(0, p.scroll or 0)
  local hover = p.hover or -1
  local hover_bg = colors.selection_bg or colors.sidebar_sel_bg or bg
  local lang_w = math.max(12, math.min(24, math.floor(p.w / 3)))
  local body = {}
  local spans = {}
  local count = 0
  for j = 0, list_h - 1 do
    local idx = scroll + j
    if idx >= #rows then
      break
    end
    local row = rows[idx + 1]
    count = count + 1
    local b = j + 2 -- rows start on the float's second inner line
    -- Mouse-hover highlight (motion) tints the row background.
    local rb = idx == hover and hover_bg or bg
    if row.section then
      local text = " " .. trunc_cells(row.label or "", inner_w - 4)
      if row.detail and row.detail ~= "" then
        text = text .. " (" .. row.detail .. ")"
      end
      body[b] = pad_cells(text, inner_w)
      spans[b] = { { start = 0, len = 65535, fg = comment, bg = rb } }
    else
      local name = pad_cells(trunc_cells(row.label or "", lang_w), lang_w)
      local detail = trunc_cells(row.detail or "", math.max(1, inner_w - lang_w - 3))
      local name_fg = (row.color and row.color ~= 0) and row.color or fg
      local line = " " .. name .. " " .. detail
      body[b] = pad_cells(line, inner_w)
      spans[b] = {
        { start = 0, len = 65535, fg = fg, bg = rb },
        { start = 1, len = #name, fg = name_fg, bg = rb },
        { start = #name + 2, len = #detail, fg = comment, bg = rb },
      }
    end
  end
  if count == 0 then
    body[2] = pad(" " .. truncate("No languages registered", inner_w - 2), inner_w)
    spans[2] = { { start = 0, len = 65535, fg = comment, bg = bg } }
  end

  local inner_h = math.max(1, p.h - 2)
  local body_list = {}
  for i = 1, inner_h do
    body_list[i] = pad_cells(body[i] or "", inner_w)
  end

  local footer = "Esc close   Up/Down scroll"
  local max_scroll = math.max(0, #rows - list_h)
  if max_scroll > 0 then
    footer = footer .. "  " .. tostring(scroll + 1) .. "/" .. tostring(max_scroll + 1)
  end
  return present_panel("tree_sitter_status",
                       p,
                       {},
                       {
                         title = " Tree-sitter",
                         title_fg = colors.accent or colors.fg or 7,
                         footer = footer,
                       },
                       body_list,
                       spans)
end


return {
  tree_sitter_status = tree_sitter_status,
}
