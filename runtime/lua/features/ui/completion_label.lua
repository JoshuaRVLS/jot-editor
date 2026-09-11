-- Completion row labels for the LSP popup — a port of colorful-menu.nvim.
--
-- Upstream rebuilds each row from the completion item's own fields so it reads
-- like a declaration (`parse_config(…) -> Result<Config>`) instead of a bare name
-- with everything else jammed into one narrow column, then highlights the result
-- with tree-sitter. jot has no way to highlight a snippet it has not parsed, so
-- the same outcome is reached structurally: the label is split into name /
-- argument list / type / extra info, and each part gets a theme colour.
--
-- This module is pure: an item payload in, text plus byte-offset spans out. The
-- caller measures the widest type across the visible rows and passes that in,
-- which is what makes the type column line up.
--
-- Spans are byte offsets (the float painter maps them to cells itself), so the
-- fitting is done in cells and the span arithmetic in bytes.

local h = require("jot_ui.helpers")
local cell_len = h.cell_len
local trunc_cells = h.trunc_cells

local M = {}

-- Decorations servers prepend to `detail` that are not part of the type.
local DECORATORS = {
  "(method) ",
  "(function) ",
  "(property) ",
  "(variable) ",
  "(class) ",
  "(constant) ",
  "(field) ",
  "(module) ",
  "(parameter) ",
  "(constructor) ",
  "(enum) ",
  "(interface) ",
  "(struct) ",
  "(namespace) ",
}

-- Language keywords a signature starts with; the icon already says "function".
local SIGNATURE_PREFIXES = {
  "pub async fn ",
  "pub fn ",
  "async fn ",
  "fn ",
  "func ",
  "function ",
  "def ",
  "fun ",
}

-- Per-server presentation. Where each server keeps the type is not standardised,
-- so this table maps server id -> which field to read. The mappings follow
-- upstream's adapters and the servers' documented behaviour; the generic default
-- covers everything else, and a server whose fields differ still gets a
-- correctly coloured row (just with less of the type split out).
--
--   type                  field holding the type / signature annotation
--   extra                 field holding the import path, trait note, ...
--   align                 right-align the annotation into a column
--   args_from_label_detail  append labelDetails.detail when it is a bare "(...)"
--                           list (clangd's shape)
--   return_marker         how to pull a return type out of a full signature
local PROFILES = {
  ["rust-analyzer"] = { type = "detail", extra = "label_description", align = true, return_marker = "arrow" },
  clangd = { type = "detail", extra = "label_description", align = true, args_from_label_detail = true },
  gopls = { type = "detail", extra = "label_description", align = true },
  zls = { type = "detail", extra = "label_description", align = true },
  ["lua-language-server"] = { type = "detail", align = true, return_marker = "colon" },
  lua_ls = { type = "detail", align = true, return_marker = "colon" },
  basedpyright = { type = "detail", extra = "label_description", align = true, return_marker = "arrow" },
  pyright = { type = "detail", extra = "label_description", align = true, return_marker = "arrow" },
  pylance = { type = "detail", extra = "label_description", align = true, return_marker = "arrow" },
  pylsp = { type = "detail", extra = "label_description", align = true, return_marker = "arrow" },
  ["typescript-language-server"] = { type = "label_detail", extra = "label_description", align = false },
  vtsls = { type = "label_detail", extra = "label_description", align = false },
  ts_ls = { type = "label_detail", extra = "label_description", align = false },
  intelephense = { type = "label_description", align = false },
  dartls = { type = "detail", extra = "label_description", align = true },
  roslyn = { type = "detail", extra = "label_description", align = false },
}

-- Reading `detail` is what most servers fill with something useful; the
-- labelDetails description is where the import path or trait note lives.
local DEFAULT_PROFILE = { type = "detail", extra = "label_description", align = false }

local function trim(text)
  return (text:gsub("^%s+", ""):gsub("%s+$", ""))
end

-- Byte length of the UTF-8 sequence starting with `byte`.
local function utf8_seq_len(byte)
  if byte < 0x80 then
    return 1
  elseif byte < 0xE0 then
    return 2
  elseif byte < 0xF0 then
    return 3
  end
  return 4
end

function M.profile_for(server)
  if server == nil or server == "" then
    return DEFAULT_PROFILE
  end
  return PROFILES[server:lower()] or PROFILES[server] or DEFAULT_PROFILE
end

-- Splits a completion label into its name and a trailing argument list. Most
-- servers put the parentheses straight in the label ("parse(…)").
function M.split_signature(label)
  label = label or ""
  local open = label:find("(", 1, true)
  if not open then
    return label, ""
  end
  return label:sub(1, open - 1), label:sub(open)
end

-- Some servers pad the label (clangd sends " printf"), which would otherwise
-- show up as a stray space after the icon. Returns the trimmed label plus the
-- number of leading bytes removed, so match offsets can be shifted with it
-- instead of pointing one character off.
function M.trim_label(label)
  local raw = label or ""
  local lead = raw:match("^%s*") or ""
  local body = raw:sub(#lead + 1)
  local trailing = body:match("%s*$") or ""
  return body:sub(1, #body - #trailing), #lead
end

-- The annotation to show on the right for one item: the return type where the
-- server sends a whole signature, otherwise the type field verbatim. Returns ""
-- when there is nothing type-like, which renders as "no annotation" rather than
-- an empty column.
function M.normalize_type(raw, profile)
  if raw == nil or raw == "" then
    return ""
  end
  local text = trim((tostring(raw):gsub("%s+", " ")))
  if text == "" then
    return ""
  end

  -- "(method) foo(x: int) -> str": drop the decoration.
  for _, decorator in ipairs(DECORATORS) do
    if text:sub(1, #decorator) == decorator then
      text = trim(text:sub(#decorator + 1))
      break
    end
  end
  if text == "" then
    return ""
  end

  -- A whole signature: keep just the return type so the right column stays
  -- short instead of repeating the label.
  local marker = profile and profile.return_marker
  if marker == "arrow" then
    local _, arrow_end = text:find("%->")
    if arrow_end then
      local tail = trim(text:sub(arrow_end + 1))
      if tail ~= "" then
        text = tail
      end
    end
  elseif marker == "colon" then
    -- lua-language-server: "function foo(a: string): number" -- take what
    -- follows the parameter list's closing parenthesis.
    local tail = text:match("^.*%)%s*:%s*(.+)$")
    if tail then
      text = trim(tail)
    end
  end

  -- Drop a leading language keyword; the rest of the line is the useful part.
  for _, prefix in ipairs(SIGNATURE_PREFIXES) do
    if text:sub(1, #prefix) == prefix then
      text = trim(text:sub(#prefix + 1))
      break
    end
  end
  return text
end

local function item_field(item, name)
  if not name or name == "none" then
    return ""
  end
  local value = item[name]
  if value == nil then
    return ""
  end
  return tostring(value)
end

-- Turns one item into the parts a row is made of, before any width fitting.
function M.plan(item, colors, options)
  options = options or {}
  colors = colors or {}
  local profile = M.profile_for(options.server)
  local comment = colors.comment or 8
  local row_fg = colors.fg or 7
  local deprecated = item.deprecated and true or false

  -- Without the feature the whole label is one span in the item's kind colour,
  -- which is where this popup started.
  if options.rich == false then
    return {
      name = tostring(item.label or ""),
      args = "",
      type = "",
      extra = "",
      align = false,
      fg_name = deprecated and comment or row_fg,
      fg_args = comment,
      fg_type = comment,
      fg_extra = comment,
      match = item.match or {},
    }
  end

  -- Labels can carry padding (clangd sends " printf"); trim it and shift the
  -- match offsets with it, so a highlighted character stays on its character.
  local trimmed, lead_bytes = M.trim_label(item.label)
  local shifted_match = {}
  for _, m in ipairs(item.match or {}) do
    if m >= lead_bytes then
      shifted_match[#shifted_match + 1] = m - lead_bytes
    end
  end

  local name, args = M.split_signature(trimmed)
  -- clangd is the one server whose label is a bare name with the parameter list
  -- in labelDetails, so append it when it is exactly that shape.
  if profile.args_from_label_detail and args == "" then
    local candidate = item_field(item, "label_detail")
    if candidate:sub(1, 1) == "(" then
      args = candidate
    end
  end

  local type_text = M.normalize_type(item_field(item, profile.type), profile)
  local extra_text = trim(item_field(item, profile.extra))

  local plan = {
    name = name,
    args = args,
    type = type_text,
    extra = extra_text,
    align = profile.align ~= false,
    -- A deprecated item is drawn entirely dim, as it was before this feature.
    fg_name = deprecated and comment or row_fg,
    fg_args = deprecated and comment or (options.dim_arguments == false and row_fg or comment),
    -- The type reads as an annotation, in the colour the theme already uses for
    -- declarations rather than the row's own text colour.
    fg_type = deprecated and comment or (colors.type or row_fg),
    fg_extra = comment,
    match = shifted_match,
  }
  return plan
end

-- The right-hand annotation text for a plan, or "" when there is none.
function M.right_text(plan)
  if plan.type == "" then
    return plan.extra or ""
  end
  if plan.extra == "" then
    return plan.type
  end
  return plan.type .. "  " .. plan.extra
end

-- Fits one planned row into a `width`-cell row and returns its text plus spans.
-- `right_width` is the annotation column measured across the visible rows; the
-- left side is truncated first so the annotation survives, and the annotation
-- itself is clipped to that column (a type wider than its half of the popup is
-- better truncated than dropped). With `align = false` the annotation follows
-- the name directly instead of being pushed out to the column, which suits
-- servers whose `detail` is a whole sentence rather than a type.
function M.render(plan, colors, options)
  options = options or {}
  local width = math.max(1, options.width or 40)
  local align = options.align ~= false

  -- The annotation's column: measured by the caller, but never so wide that the
  -- name is squeezed out.
  local right_w = math.max(0, options.right_width or 0)
  right_w = math.min(right_w, math.max(0, width - 4))

  -- Build the annotation inside its column. The type wins over the extra info
  -- when both cannot fit, so a clipped row still shows the useful half.
  local type_txt = plan.type or ""
  local extra_txt = plan.extra or ""
  local separator = (type_txt ~= "" and extra_txt ~= "") and "  " or ""
  if cell_len(type_txt) > right_w then
    type_txt = trunc_cells(type_txt, right_w)
    extra_txt = ""
    separator = ""
  elseif cell_len(type_txt) + #separator + cell_len(extra_txt) > right_w then
    local room = right_w - cell_len(type_txt) - #separator
    extra_txt = room > 0 and trunc_cells(extra_txt, room) or ""
    if extra_txt == "" then
      separator = ""
    end
  end
  local right = type_txt .. separator .. extra_txt
  local gap = right ~= "" and 1 or 0
  local left_w = width - (right ~= "" and (right_w + gap) or 0)
  if right ~= "" and left_w < 4 then
    -- Refuse to crowd out the name: drop the annotation instead.
    right = ""
    right_w = 0
    gap = 0
    left_w = width
  end

  local head = plan.name .. plan.args
  if cell_len(head) > left_w then
    if left_w >= 2 then
      -- Mark the elision so a clipped name is not mistaken for the real one.
      head = trunc_cells(head, left_w - 1) .. "\u{2026}"
    else
      head = trunc_cells(head, left_w)
    end
  end

  local spans = {}
  local function span(start, len, fg)
    if len > 0 then
      spans[#spans + 1] = { start = start, len = len, fg = fg }
    end
  end

  -- The name occupies the head's first bytes; the argument list follows it.
  -- Match offsets are bytes into the raw label, and the rendered name is a
  -- prefix of it, so offsets past the truncation point simply drop.
  local name_len = math.min(#plan.name, #head)
  local cuts = {}
  for _, m in ipairs(plan.match) do
    if m >= 0 and m + 1 <= name_len then
      cuts[#cuts + 1] = m
    end
  end
  table.sort(cuts)

  local match_fg = (colors and colors.accent) or 6
  local pos = 0
  for _, m in ipairs(cuts) do
    if m > pos then
      span(pos, m - pos, plan.fg_name)
      pos = m
    end
    -- Step over the whole character, not one byte: a multi-byte match offset
    -- would otherwise be split and painted as two garbage cells.
    local len = utf8_seq_len(plan.name:byte(m + 1) or 0x41)
    len = math.min(len, name_len - m)
    span(m, len, match_fg)
    pos = m + len
  end
  if pos < name_len then
    span(pos, name_len - pos, plan.fg_name)
  end
  if #head > name_len then
    span(name_len, #head - name_len, plan.fg_args)
  end

  local text = head
  if right ~= "" then
    local used = cell_len(text)
    if align then
      -- Flush the annotation against the row's right edge, so annotations of
      -- different lengths end on the same column. right_w only reserves room
      -- (it is the widest annotation); it is not the position.
      local target = width - cell_len(right)
      if used < target then
        text = text .. string.rep(" ", target - used)
      end
    elseif used < width - cell_len(right) then
      -- Inline: a single space between the name and the annotation.
      text = text .. " "
    end
    local right_start = #text
    text = text .. right
    -- The annotation's parts are coloured separately once positioned. The
    -- offsets use the fitted text, so a clipped annotation is still coloured.
    local separator_bytes = separator ~= "" and #separator or 0
    if type_txt ~= "" then
      span(right_start, #type_txt, plan.fg_type)
    end
    if extra_txt ~= "" then
      span(right_start + #type_txt + separator_bytes, #extra_txt, plan.fg_extra)
    end
  end

  return { text = text, spans = spans }
end

return M
