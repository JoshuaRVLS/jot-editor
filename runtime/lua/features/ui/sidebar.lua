-- Sidebar — part of the Lua UI kit.
-- Split out of features/ui.lua so each surface stays small and
-- focused; features/ui.lua is the orchestrator that requires
-- every module and registers the handlers.
local h = require("jot_ui.helpers")
local close = h.close
local rune_width = h.rune_width
local cell_len = h.cell_len
local trunc_cells = h.trunc_cells
local pad_cells = h.pad_cells
local surfaces = h.surfaces
local function sidebar(p)
  if not p then
    close("sidebar")
    return true
  end
  local colors = p.colors or {}
  local bg = p.bg or colors.sidebar_bg or colors.bg or 0
  local border_fg = (p.resizing and colors.active_border) or colors.sidebar_border
      or colors.border or 8
  local fg = colors.sidebar_fg or colors.fg or 7
  local dir = colors.sidebar_dir or colors.comment or 8
  local comment = colors.comment or 8

  local x, y, w, h = p.x or 0, p.y or 0, math.max(1, p.w or 1), math.max(1, p.h or 1)
  local content_x = math.max(1, p.content_x or 1)
  local content_w = math.max(1, p.content_w or (w - content_x - 1))
  local rail_w = math.max(0, p.rail_w or 0)

  close("sidebar")
  -- Row compositor: each line collects placement ops (glyphs at a cell
  -- column, or background fills), then renders once. This keeps byte-offset
  -- spans accurate even when a row already carries the right border, wide
  -- glyphs, or multiple badges.
  local ops = {}
  local body, spans = {}, {}
  for i = 1, h do
    body[i] = ""
    ops[i] = {}
  end
  local function place(abs_x, abs_y, text, f, b, bold)
    local line = abs_y - y + 1
    if line < 1 or line > h or text == "" then
      return
    end
    local col = math.max(0, abs_x - x)
    if col < w then
      ops[line][#ops[line] + 1] = { col = col, text = text, f = f or 7, b = b or 0, bold = bold }
    end
  end
  local function place_fill(abs_x, abs_y, ww, f, b)
    local line = abs_y - y + 1
    if line < 1 or line > h then
      return
    end
    local col = math.max(0, abs_x - x)
    if col < w then
      ops[line][#ops[line] + 1] =
          { col = col, fill = true, w = math.max(1, ww or 1), f = f or 7, b = b or 0 }
    end
  end
  -- The two sides that face another region: the editor to the right, and the
  -- status line below. The top and left edges are the screen's, which delimit
  -- nothing, so they stay unpainted -- the same rule the panes follow
  -- (see pane_edges.h).
  --
  -- The bottom edge ends in a T (up + left + right): the editor's own bottom
  -- separator continues to the right of this panel, so the two form one line
  -- across the status line instead of stopping at the panel's edge.
  for i = 1, h - 1 do
    place(x + w - 1, y + i - 1, "│", border_fg, bg)
  end
  local bar_bg = colors.status_bg or bg
  place(x, y + h - 1, string.rep("─", math.max(0, w - 1)) .. "┴", border_fg, bar_bg)

  -- Activity rail (left, inside the frame): active view marker + label, and
  -- a separator between the rail and content.
  local function draw_rail(row_abs, label, active)
    local row = row_abs - y + 1
    if row < 1 or row > h then
      return
    end
    local rf = active and dir or comment
    place_fill(x + 1, row_abs, math.max(0, rail_w - 2), rf, bg)
    place(x + 1, row_abs, active and "▌" or " ", rf, bg, active)
    if rail_w >= 3 then
      place(x + 2, row_abs, label, rf, bg, active)
    end
  end
  if p.rail_explorer_row and p.rail_explorer_row >= 0 then
    draw_rail(y + p.rail_explorer_row, "󰉋 ", not p.git_view)
  end
  if p.rail_git_row and p.rail_git_row >= 0 then
    -- The git item launches the git panel (":gitpanel"), so its marker
    -- follows that panel rather than the sidebar's own view.
    draw_rail(y + p.rail_git_row, "git", p.git_panel_active)
  end
  if rail_w > 0 then
    for i = 2, h - 1 do
      place(x + math.max(0, rail_w - 1), y + i - 1, "│", border_fg, bg, false)
    end
  end  -- Rows: fill each rect, then symbol / text / badges on top.
  for _, r in ipairs(p.rows or {}) do
    if r.y and r.y >= y and r.y < y + h then
      place_fill(r.x or x, r.y, r.w or 1, r.fg or fg, r.bg or bg)
      if r.symbol and r.symbol ~= "" and r.symbol_x and r.symbol_x >= 0 then
        place(r.symbol_x, r.y, r.symbol, r.symbol_fg or r.fg, r.bg or bg, r.symbol_bold)
      end
      if r.guide and r.guide ~= "" and r.guide_x and r.guide_x >= 0 then
        -- Tree indent guides (│ bars and └ feet) in the comment color.
        place(r.guide_x, r.y, r.guide, r.guide_fg or r.fg, r.bg or bg, false)
      end
      if r.icon and r.icon ~= "" and r.icon_x and r.icon_x >= 0 then
        -- Per-language file glyph in its brand color (like the status line),
        -- placed ahead of the label at the column C++ computed.
        place(r.icon_x, r.y, r.icon, r.icon_fg or r.fg, r.bg or bg, false)
      end
      place(r.text_x or (r.x + 1), r.y, r.text or "", r.fg or fg, r.bg or bg, r.bold)
      if r.badge and r.badge ~= "" and r.badge_x and r.badge_x >= 0 then
        place(r.badge_x, r.y, r.badge, r.badge_fg or r.fg, r.bg or bg, true)
      end
      if r.badge2 and r.badge2 ~= "" and r.badge2_x and r.badge2_x >= 0 then
        place(r.badge2_x, r.y, r.badge2, r.badge2_fg or r.fg, r.bg or bg, true)
      end
    end
  end

  -- Footer (one row above the bottom border).
  if p.footer and p.footer ~= "" and p.footer_y and p.footer_y >= y and p.footer_y < y + h then
    place(p.footer_x or content_x + 1,
          p.footer_y,
          trunc_cells(p.footer, math.max(1, content_w)),
          p.footer_fg or comment,
          bg,
          false)
  end

  -- Byte index (1-based) of the codepoint occupying the given cell column.
  local function byte_at_cell(s, target)
    if target <= 0 then
      return 1
    end
    local cells, i = 0, 1
    while true do
      local next_i = utf8.offset(s, 2, i)
      if not next_i then
        return #s + 1
      end
      local cp = utf8.codepoint(s, i)
      local cw = rune_width(cp)
      if cells + cw >= target then
        return i
      end
      cells = cells + cw
      i = next_i
    end
  end

  -- Header: the workspace root (icon first) on the panel's first row. Placed as
  -- an op so it composes with the right-edge separator like any other row --
  -- there is no border row to bake it into.
  local hcol = math.max(1, (p.header_x or (content_x + 1)) - x)
  if p.header and p.header ~= "" then
    place(x + hcol - 1,
          y,
          trunc_cells(p.header, math.max(1, content_w)),
          p.header_fg or dir,
          bg,
          true)
  end

  -- Compose every row (1 .. h). The bounds used to be 2 .. h-1 because the first
  -- and last rows were the frame's top and bottom edges, composed separately;
  -- with no frame those rows are ordinary content, and skipping them left the
  -- right-edge separator one row short at each end (so it no longer met the
  -- pane's line at the bottom).
  for i = 1, h do
    local list = ops[i] or {}
    table.sort(list, function(a, b)
      if a.col ~= b.col then
        return a.col < b.col
      end
      return a.fill and not b.fill
    end)
    local text, glyphs, fills = "", {}, {}
    local cur = 0
    for _, op in ipairs(list) do
      if op.fill then
        fills[#fills + 1] = op
      else
        local tw = cell_len(op.text)
        if op.col > cur then
          text = text .. string.rep(" ", op.col - cur)
          cur = op.col
        end
        local at = #text
        text = text .. op.text
        glyphs[#glyphs + 1] = { start = at, len = #op.text, fg = op.f, bg = op.b, bold = op.bold }
        cur = cur + tw
      end
    end
    local ls = {}
    for _, op in ipairs(fills) do
      local start_at = byte_at_cell(text, op.col)
      if start_at <= #text then
        local end_at = byte_at_cell(text, op.col + op.w)
        ls[#ls + 1] = { start = start_at - 1,
                        len = math.min(65535, math.max(1, end_at - start_at)),
                        fg = op.f, bg = op.b, bold = false }
      end
    end
    for _, sp in ipairs(glyphs) do
      ls[#ls + 1] = sp
    end
    body[i] = pad_cells(text, w)
    if #ls > 0 then
      spans[i] = ls
    end
  end

  -- Reuse the persistent buffer/float across frames.
  local s = surfaces["sidebar"]
  local buf, win
  if s then
    buf, win = s.buf, s.win
    jot.ui.float.configure(win, {
      col = x,
      row = y,
      width = w,
      height = h,
      relative = "editor",
      anchor = "NW",
      border = "none",
      fg = fg,
      bg = bg,
    })
  else
    buf = jot.ui.buffer.create(false, true)
    win = jot.ui.float.open(buf, {
      col = x,
      row = y,
      width = w,
      height = h,
      relative = "editor",
      anchor = "NW",
      border = "none",
      focusable = false,
      mouse = false,
      hide = false,
      fg = fg,
      bg = bg,
    })
    if not win or win == 0 then
      jot.ui.buffer.delete(buf)
      return false
    end
    surfaces["sidebar"] = { win = win, buf = buf }
  end
  jot.ui.buffer.set_lines(buf, 0, -1, true, body)
  for i = 1, h do
    if spans[i] then
      jot.ui.float.set_spans(win, i, spans[i])
    end
  end
  return true
end


return {
  sidebar = sidebar,
}
