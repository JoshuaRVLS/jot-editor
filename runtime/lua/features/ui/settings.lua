-- The :settings surface (Ctrl+, in the GUI): the Lua painter for the native
-- settings model, drawing into the cells the native layout recorded.
local h = require("jot_ui.helpers")
local close = h.close
local cell_len = h.cell_len
local trunc_cells = h.trunc_cells
local pad_cells = h.pad_cells
local title_with_count = h.title_with_count
local present_panel = h.present_panel

local CHECK = "\u{F00C}"  -- nf-fa-check: the choice the config holds
local SEARCH = ">"        -- the search bar's prompt mark

-- One row built out of positioned pieces, spans returned for the renderer.
-- cut() marks a shortened label, as the native painter's truncate() does.
local function truncate_marked(s, n)
  if n <= 0 then
    return ""
  end
  if cell_len(s) <= n then
    return s
  end
  if n <= 3 then
    return trunc_cells(s, n)
  end
  return trunc_cells(s, n - 3) .. "..."
end

local function row_builder()
  local parts = {}
  local used = 0
  local b = {}
  function b.text(t, style)
    parts[#parts + 1] = { text = t, style = style }
    used = used + cell_len(t)
    return b
  end
  -- Puts the next piece at absolute cell `col`, filling with spaces up to it.
  function b.at(col, t, style)
    if col > used then
      b.text(string.rep(" ", col - used))
    end
    return b.text(t, style)
  end
  function b.finish(width)
    local body = {}
    for _, part in ipairs(parts) do
      body[#body + 1] = part.text
    end
    local text = table.concat(body)
    local spans = {}
    local start = 0
    for _, part in ipairs(parts) do
      if part.style and not part.style.plain then
        local len = #part.text
        if len > 0 then
          spans[#spans + 1] = {
            start = start,
            len = len,
            fg = part.style.fg,
            bg = part.style.bg,
            bold = part.style.bold,
          }
        end
      end
      -- Span offsets are bytes into the rendered line (the float renderer
      -- maps them back to cells), so advance by the byte length: a multi-byte
      -- glyph (a row chevron, an enum's, the prompt mark) is one cell but
      -- three bytes, and a cell-based advance shifts every later span's ink
      -- onto the wrong text.
      start = start + #part.text
    end
    return pad_cells(text, width), spans
  end
  return b
end

local function settings(p)
  if not p then
    close("settings")
    close("settings_options")
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
  local title = (p.title and p.title ~= "" and " " .. p.title) or " Settings"
  local total = tonumber(p.all_count) or 0
  local matches = tonumber(p.match_count) or total
  local count_text = tostring(total) .. " keys"
  if p.query and p.query ~= "" then
    count_text = tostring(matches) .. "/" .. tostring(total)
  end
  title = title_with_count(title, count_text, inner_w)

  -- The row's own 0 cell is p.x + 1, so absolute columns are offset by one.
  local row0 = p.x + 1

  local rows = {}

  -- Search bar: the prompt mark, the query (or what to type), match count on
  -- the right once a query is narrowing the list.
  do
    local b = row_builder()
    b.text(" ")
    b.at(1, SEARCH, { fg = comment, bg = bg })
    if (p.query or "") == "" then
      b.at(3, trunc_cells("Search " .. tostring(total) .. " settings", inner_w - 4),
           { fg = comment, bg = bg })
    else
      local cnt = tostring(matches) .. "/" .. tostring(total)
      b.at(3, trunc_cells(p.query, math.max(1, inner_w - 5 - cell_len(cnt))),
           { fg = accent, bg = bg, bold = true })
      -- Same column the native painter puts it on: one blank cell inside the
      -- right border.
      b.at(inner_w - 2 - cell_len(cnt), cnt, { fg = comment, bg = bg })
    end
    local text, spans = b.finish(inner_w)
    rows[#rows + 1] = { text = text, fg = fg, bg = bg, spans = spans }
  end

  rows[#rows + 1] = { text = string.rep("─", inner_w), fg = border, bg = bg }

  local items = p.items or {}
  local list_h = math.max(0, p.h - 5) -- search bar + divider + footer
  local key_w = math.max(18, math.floor(p.w * 0.55))

  for row = 1, list_h do
    local item = items[row]
    if not item then
      break
    end
    local is_selected = item.selected
    local is_editing = is_selected and item.editing
    -- A section header rides the row machinery with no value and no
    -- affordances (the native side sends it with none); only its ink differs.
    local row_fg = item.header and comment or (is_selected and selection_fg or fg)
    local row_bg = is_selected and selection_bg or bg
    -- The affordances take the row's own ink: on the selection bar the accent
    -- colour can vanish into the fill (a theme may ink its accent and its
    -- selection background with the same value).
    local grip_fg = is_selected and selection_fg or comment
    local value_fg = is_selected and selection_fg or accent

    local b = row_builder()
    b.at(0, is_selected and "\u{F054}" or " ")
    b.at(1, truncate_marked(item.label or "", key_w - 3))
    local val_col = math.max(1, (tonumber(item.value_x) or (p.x + 1 + key_w)) - row0)
    -- The value band's width comes from the native layout; a caller without one
    -- (the UI-kit test's stub) gets the whole inner row.
    local val_w = tonumber(item.value_w) or inner_w
    local down = tonumber(item.step_down_x) or -1
    local up = tonumber(item.step_up_x) or -1
    if is_editing then
      b.at(val_col, trunc_cells("> " .. (item.edit_input or ""), val_w),
           { fg = selection_fg, bg = selection_bg, bold = true })
    else
      -- An enum row cycles with chevrons, an int row steps with nf-fa-minus /
      -- plus; the pieces go in cell order so they land on the recorded cells.
      if item.type == "enum" and down >= 0 then
        b.at(down - row0, "\u{2039}", { fg = grip_fg, bg = row_bg })
      end
      b.at(val_col, truncate_marked(item.value or "", val_w), { fg = value_fg, bg = row_bg })
      if up >= 0 then
        if item.type == "enum" then
          b.at(up - row0, "\u{203A}", { fg = grip_fg, bg = row_bg })
        else
          b.at(down - row0, "\u{F068}", { fg = grip_fg, bg = row_bg })
          b.at(up - row0, "\u{F067}", { fg = grip_fg, bg = row_bg })
        end
      end
    end
    local text, spans = b.finish(inner_w)
    rows[#rows + 1] = { text = text, fg = row_fg, bg = row_bg, spans = spans }
  end

  local ok = present_panel("settings", p, rows, { title = title, title_fg = accent })

  -- The choices drop-down: a second float, so it sits over the list it came
  -- from rather than pushing it around.
  if p.dropdown_open then
    local options = p.dropdown_options or {}
    local box = {
      x = p.dropdown_x,
      y = p.dropdown_y,
      w = p.dropdown_w,
      h = p.dropdown_h,
      colors = colors,
    }
    local inner_box_w = math.max(1, p.dropdown_w - 2)
    local box_rows = {}
    local first = tonumber(p.dropdown_scroll) or 0
    for i = 1, math.max(0, p.dropdown_h - 2) do
      local index = first + i - 1
      local option = options[index + 1]
      if not option then
        break
      end
      local cursor = index == (tonumber(p.dropdown_index) or 0)
      local current = option == p.dropdown_value
      local b = row_builder()
      b.text(" ")
      b.at(1, current and CHECK or " ", { fg = cursor and selection_fg or accent })
      b.at(3, truncate_marked(option, inner_box_w - 3))
      local text, spans = b.finish(inner_box_w)
      box_rows[#box_rows + 1] = {
        text = text,
        fg = cursor and selection_fg or fg,
        bg = cursor and selection_bg or bg,
        spans = spans,
      }
    end
    present_panel("settings_options", box, box_rows, { title_fg = accent },
                  nil, nil)
  else
    close("settings_options")
  end
  return ok
end


return {
  settings = settings,
}
