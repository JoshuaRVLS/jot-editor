-- Lsp — part of the Lua UI kit.
-- Split out of features/ui.lua so each surface stays small and
-- focused; features/ui.lua is the orchestrator that requires
-- every module and registers the handlers.
local h = require("jot_ui.helpers")
local close = h.close
local cell_len = h.cell_len
local trunc_cells = h.trunc_cells
local pad_cells = h.pad_cells
local truncate = h.truncate
local pad = h.pad
local present_panel = h.present_panel
local function lsp_status(p)
  if not p then
    close("lsp_status")
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
    body[2] = pad(" " .. truncate("No LSP servers active", inner_w - 2), inner_w)
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
  return present_panel("lsp_status",
                       p,
                       {},
                       {
                         title = " LSP",
                         title_fg = colors.accent or colors.fg or 7,
                         footer = footer,
                       },
                       body_list,
                       spans)
end

-- ---------------------------------------------------------------------------
-- LSP manager modal
-- ---------------------------------------------------------------------------

local function lsp_manager(p)
  if not p then
    close("lsp_manager")
    return true
  end
  local colors = p.colors or {}
  local fg = colors.fg or 7
  local bg = colors.panel_bg or colors.bg or 0
  local selection_fg = colors.selection_fg or 0
  local selection_bg = colors.selection_bg or 6
  local comment = colors.comment or 8
  local accent = colors.accent or 6
  local error = colors.error or 1
  local inner_w = math.max(1, p.w - 2)

  local list_h = math.max(1, p.h - 3)
  local rows = p.rows or {}
  local scroll = math.max(0, p.scroll or 0)
  local selected = math.max(0, p.selected or 0)
  local label_w = math.max(1, p.label_w or 12)
  local state_x = (p.state_x or 0) - (p.x or 0) - 1 -- inner col of the state
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
    local is_selected = idx == selected
    local row_fg = is_selected and selection_fg or fg
    local row_bg = is_selected and selection_bg or bg
    local b = j + 1

    -- Compose the row: label at inner col 1, state at state_x, then buttons
    -- at their native rects so mouse clicks stay aligned. Columns are cells
    -- (wide glyphs count 2) so rows never drift out of alignment.
    local function emit(col, text, span_fg)
      local cur = cell_len(body[b] or "")
      if cur < col then
        body[b] = (body[b] or "") .. string.rep(" ", col - cur)
      end
      local start = #(body[b] or "")
      body[b] = body[b] .. text
      if span_fg then
        spans[b] = spans[b] or {}
        spans[b][#spans[b] + 1] = { start = start, len = #text, fg = span_fg, bg = row_bg }
      end
    end
    -- Background row span first (full width once padded later).
    spans[b] = { { start = 0, len = 65535, fg = row_fg, bg = row_bg } }
    emit(1, trunc_cells(row.label or "", label_w), row_fg)
    local state_col = math.max(1 + label_w, state_x)
    local state_w = math.max(1, (p.action_x or p.x + p.w - 31) - (p.state_x or 0) - 1)
    emit(state_col, trunc_cells(row.state or "", state_w), row.state_color or comment)
    for _, btn in ipairs(row.actions or {}) do
      local col = (btn.x or 0) - (p.x or 0) - 1
      if col >= 1 then
        local variant_fg = btn.variant == "danger" and error
          or btn.variant == "primary" and accent
          or (btn.variant == "secondary" or btn.variant == "muted") and comment
          or fg
        local avail = math.max(1, inner_w - col)
        local label = trunc_cells(btn.label or "", avail)
        if btn.focused then
          label = "[" .. label .. "]"
        else
          label = " " .. label .. " "
        end
        label = trunc_cells(label, math.max(1, (btn.w or #label) + (btn.focused and 0 or 0)))
        emit(col, label, btn.enabled and variant_fg or comment)
      end
    end
    if #(body[b] or "") < inner_w then
      body[b] = pad(body[b], inner_w)
    end
  end
  if count == 0 then
    body[1] = pad(" No servers registered", inner_w)
    spans[1] = { { start = 0, len = 65535, fg = comment, bg = bg } }
  end

  local inner_h = math.max(1, p.h - 2)
  local body_list = {}
  for i = 1, inner_h do
    body_list[i] = pad_cells(body[i] or "", inner_w)
  end

  local footer = tostring(#rows) .. " servers"
  return present_panel("lsp_manager",
                       p,
                       {},
                       {
                         title = " LSP Manager ",
                         title_fg = colors.accent or colors.fg or 7,
                         footer = footer,
                       },
                       body_list,
                       spans)
end

local function completion_kind_color(kind_name, colors)
  local k = (kind_name or ""):lower()
  if k == "function" or k == "method" then
    return colors.function_method or colors["function"] or colors.fg or 7
  end
  if k == "constructor" then
    return colors.function_constructor or colors["function"] or colors.fg or 7
  end
  if k == "class" or k == "struct" or k == "interface" or k == "typeparameter" then
    return colors.type or colors.fg or 7
  end
  if k == "keyword" then
    return colors.keyword or colors.fg or 7
  end
  if k == "variable" then
    return colors.variable or colors.fg or 7
  end
  if k == "field" or k == "property" then
    return colors.field or colors.fg or 7
  end
  if k == "constant" or k == "enum" or k == "enummember" or k == "value" then
    return colors.constant or colors.fg or 7
  end
  if k == "module" or k == "namespace" then
    return colors.module or colors.namespace or colors.fg or 7
  end
  if k == "snippet" or k == "string" then
    return colors.string or colors.fg or 7
  end
  if k == "number" or k == "unit" then
    return colors.number or colors.fg or 7
  end
  if k == "operator" then
    return colors.operator or colors.fg or 7
  end
  return colors.fg or 7
end

-- Appends one styled part to a row, tracking byte offsets for spans. The row
-- text gets cell-padded by present_panel afterwards, so trailing content can
-- stop at any cell column without breaking span alignment.
local function add_part(parts, offsets, text, fg)
  if text == nil or text == "" then
    return
  end
  offsets[#offsets + 1] = { start = #table.concat(parts), len = #text, fg = fg }
  parts[#parts + 1] = text
end

local function lsp_completion(p)
  if not p then
    close("lsp_completion")
    return true
  end
  local colors = p.colors or {}
  local fg = colors.fg or 7
  local bg = colors.panel_bg or colors.bg or 0
  local selection_fg = colors.selection_fg or 0
  local selection_bg = colors.selection_bg or 6
  local comment = colors.comment or 8

  -- Native passes the content box; the float wraps it in a one-cell border.
  local f = {
    x = p.x - 1,
    y = p.y - 1,
    w = p.w + 2,
    h = p.h + 2,
    colors = p.colors,
  }
  local content_w = math.max(1, (p.w or 2))
  local items = p.items or {}
  local total = math.max(1, p.total or 0)
  local rows = {}
  local meta_w = math.max(8, math.floor(content_w / 3))
  local label_w = math.max(1, content_w - meta_w - 1)
  for i, it in ipairs(items) do
    local sel = (p.start or 0) + i - 1 == (p.selected or 0)
    local row_fg = sel and selection_fg or fg
    local row_bg = sel and selection_bg or bg
    local parts = {}
    local offsets = {}
    local icon = it.kind_icon or " "
    -- Reserve the first cell as a gutter; icon + label stay within label_w.
    local name = trunc_cells((it.label or ""), math.max(1, label_w - cell_len(icon) - 1))
    parts[#parts + 1] = " "
    add_part(parts, offsets, icon, completion_kind_color(it.kind_name, colors))
    local name_off = #table.concat(parts)
    add_part(parts, offsets, name, it.deprecated and comment or row_fg)
    -- nvim-cmp's CmpItemAbbrMatch: highlight the characters the typed
    -- prefix consumed in the label (offsets are bytes into the raw label;
    -- the rendered name is a prefix of it, so out-of-range offsets drop).
    if not it.deprecated and it.match and #it.match > 0 then
      local match_fg = sel and selection_fg or (colors.accent or 6)
      for _, m in ipairs(it.match) do
        if m >= 0 and m + 1 <= #name then
          offsets[#offsets + 1] = { start = name_off + m, len = 1, fg = match_fg }
        end
      end
    end
    local meta = it.kind_name or ""
    if it.deprecated then
      meta = meta == "" and "deprecated" or (meta .. " deprecated")
    end
    local detail = (it.detail ~= nil and it.detail ~= "") and it.detail or it.documentation or ""
    if detail ~= "" then
      meta = meta == "" and detail or (meta .. "  " .. detail)
    end
    local line = table.concat(parts)
    -- Right-align the meta column by cells (wide glyphs count 2).
    local cur = cell_len(line)
    local meta_col = math.max(cur + 1, content_w - meta_w)
    if cur < meta_col then
      line = line .. string.rep(" ", meta_col - cur)
    end
    local meta_off = #line
    local meta_text = trunc_cells(meta, math.max(1, content_w - meta_col))
    line = line .. meta_text
    if meta_text ~= "" then
      offsets[#offsets + 1] = { start = meta_off, len = #meta_text, fg = sel and selection_fg or comment }
    end
    rows[#rows + 1] = {
      text = line,
      fg = row_fg,
      bg = row_bg,
      spans = offsets,
    }
  end
  -- Footer row: position counter + typed prefix + filtered hint.
  local footer = tostring((p.selected or 0) + 1) .. "/" .. tostring(total)
  if p.prefix and p.prefix ~= "" then
    footer = footer .. "  " .. trunc_cells(p.prefix, math.max(1, content_w - 10))
  end
  if p.filtered then
    footer = footer .. "  filtered"
  end
  rows[#rows + 1] = { text = footer, fg = comment, bg = bg }
  return present_panel("lsp_completion", f, rows, { border = "single" })
end


-- Signature help popup: shows the active function's parameters while typing
-- a call, with the parameter being filled highlighted. The native side owns
-- request timing and geometry (anchored above the caret); this handler only
-- paints the rows it is given: the signature label (with a highlight span
-- over the active parameter), the documentation lines and a footer.
local function lsp_signature(p)
  if not p then
    close("lsp_signature")
    return true
  end
  local colors = p.colors or {}
  local accent = colors.accent or 6
  local f = {
    x = p.x - 1,
    y = p.y - 1,
    w = p.w + 2,
    h = p.h + 2,
    colors = p.colors,
  }
  local content_w = math.max(1, p.w or 2)
  local body = {}
  local spans = {}
  local lines = p.lines or {}
  local hl_start = p.label_hl_start or -1
  local hl_len = p.label_hl_len or -1
  for i, ln in ipairs(lines) do
    local text = ln.text or ""
    if cell_len(text) > content_w then
      text = trunc_cells(text, content_w)
    end
    body[i] = text
    local row_spans = {}
    -- Only the signature label (first row) carries the active-parameter
    -- highlight; spans are byte offsets into the row text, exactly like the
    -- match highlights in the palette / quick-pick rows.
    if i == 1 and hl_start >= 0 and hl_len > 0 and hl_start < #text then
      local kept = math.min(hl_len, #text - hl_start)
      if kept > 0 then
        row_spans[#row_spans + 1] = { start = hl_start, len = kept, fg = accent }
      end
    end
    spans[i] = row_spans
  end
  return present_panel("lsp_signature", f, nil, { border = "single" }, body, spans)
end

return {
  lsp_status = lsp_status,
  lsp_manager = lsp_manager,
  lsp_completion = lsp_completion,
  lsp_signature = lsp_signature,
  completion_kind_color = completion_kind_color,
  add_part = add_part,
}
