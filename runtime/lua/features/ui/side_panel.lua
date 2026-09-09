-- Side Panel — part of the Lua UI kit.
-- Split out of features/ui.lua so each surface stays small and
-- focused; features/ui.lua is the orchestrator that requires
-- every module and registers the handlers.
--
-- Serves every right-panel surface (debugger, git diff, symbols
-- outline, plugin panels). The native side tags each row with a
-- `kind`; the debugger panel (rows with kind, plus session tabs)
-- gets the rich treatment below — section headers, hexdump
-- memory rows, typed variable coloring, thread markers and a
-- key-hint footer — while the other panels keep the generic
-- list rendering.
local h = require("jot_ui.helpers")
local close = h.close
local cell_len = h.cell_len
local trunc_cells = h.trunc_cells
local pad_cells = h.pad_cells
local pad = h.pad
local present_panel = h.present_panel

local function side_panel(p)
  if not p then
    close("side_panel")
    return true
  end
  local colors = p.colors or {}
  local fg = colors.fg or 7
  local bg = colors.panel_bg or colors.bg or 0
  local comment = colors.comment or 8
  local accent = colors.accent or colors.fg or 7
  local selection_fg = colors.selection_fg or 0
  local selection_bg = colors.selection_bg or 6

  local inner_w = math.max(1, (p.w or 2) - 2)
  local inner_h = math.max(1, (p.h or 2) - 2)
  local rows = {}
  local function add(text, f, b, bold, spans)
    if #rows >= inner_h then
      return
    end
    local t = text or ""
    rows[#rows + 1] = {
      text = trunc_cells(t, inner_w),
      fg = f,
      bg = b,
      spans = spans or { { start = 0, len = 65535, fg = f, bg = b, bold = bold } },
    }
  end

  -- Section header: accent bold label with a dim middle-dot filler, e.g.
  -- "Variables ············".
  local function section(label)
    local fill = string.rep("\xc2\xb7", math.max(0, inner_w - cell_len(label) - 2))
    add(label .. " " .. fill, accent, bg, true, {
      { start = 0, len = 65535, fg = accent, bg = bg, bold = true },
      { start = cell_len(label) + 1, len = #fill, fg = comment, bg = bg, bold = false },
    })
  end

  -- Memory row: "0x…  ab cd …  ascii" -> aligned address / hex / ascii
  -- columns (address accent, bytes plain, ascii dim). Falls back to a flat
  -- row in very narrow panels.
  local function memory_row(r)
    local t = r.text or ""
    local parts = {}
    local start = 1
    while start <= #t do
      local s = t:find("  ", start, true)
      if not s then
        parts[#parts + 1] = t:sub(start)
        break
      end
      parts[#parts + 1] = t:sub(start, s - 1)
      start = s + 2
    end
    local addr = parts[1] or ""
    local bytes = parts[2] or ""
    local ascii = parts[3] or ""
    if inner_w < 28 then
      add(t, fg, bg, false)
      return
    end
    local addr_w = math.min(18, inner_w)
    local a = pad(addr, addr_w)
    local spans = {
      { start = 0, len = 65535, fg = fg, bg = bg },
      { start = 0, len = #a, fg = accent, bg = bg },
    }
    local text = a
    local rest_w = inner_w - addr_w
    if rest_w > 0 then
      local bytes_w = math.min(47, rest_w)
      local b = pad(bytes, bytes_w)
      text = text .. b
      spans[#spans + 1] = { start = addr_w, len = #b, fg = fg, bg = bg }
      local ascii_w = inner_w - addr_w - bytes_w
      if ascii_w > 0 then
        local c = trunc_cells(ascii, ascii_w)
        text = text .. c
        spans[#spans + 1] = { start = addr_w + #b, len = #c, fg = comment, bg = bg }
      end
    end
    add(text, fg, bg, false, spans)
  end

  -- Variable row: "name = value : type" -> name in variable color, value
  -- colored by its type (numbers, strings, bools, pointers), type dimmed.
  local function var_row(r)
    local t = r.text or ""
    local eq = t:find(" = ", 1, true)
    if not eq then
      return nil
    end
    local name = t:sub(1, eq - 1)
    local rest = t:sub(eq + 3)
    local colon = rest:find(" : ", 1, true)
    local value = rest
    local vtype = nil
    if colon then
      value = rest:sub(1, colon - 1)
      vtype = rest:sub(colon + 3)
    end
    local vfg = fg
    if vtype then
      local low = vtype:lower()
      if low == "bool" or low == "_Bool" then
        vfg = colors.keyword or fg
      elseif low:find("char", 1, true) or low:find("string", 1, true) then
        vfg = colors.string or fg
      elseif low:find("int", 1, true) or low:find("float", 1, true)
             or low:find("double", 1, true) or low:find("long", 1, true)
             or low:find("short", 1, true) then
        vfg = colors.number or fg
      elseif low:find("ptr", 1, true) or low:find("pointer", 1, true) then
        vfg = colors.builtin or colors.constant or fg
      end
    elseif value:find("0x", 1, true) then
      vfg = colors.constant or fg
    end
    return { name = name, value = value, vtype = vtype, vfg = vfg }
  end

  -- Debugger session tabs on the first interior row.
  if p.tabs and #p.tabs > 0 then
    local line, spans, col = "", {}, 0
    local active_fg = colors.fg_terminal_tab_focused or accent
    local active_bg = colors.bg_terminal_tab_focused or selection_bg
    local inactive_fg = colors.fg_terminal_tab_inactive or comment
    local inactive_bg = colors.bg_terminal_tab_inactive or bg
    for _, tab in ipairs(p.tabs) do
      local label = trunc_cells(tab.label or "", math.max(1, inner_w - col))
      local at = #line
      line = line .. label
      spans[#spans + 1] = { start = at, len = #label, fg = tab.active and active_fg or inactive_fg,
                            bg = tab.active and active_bg or inactive_bg, bold = tab.active }
      col = col + cell_len(label) + 1
      if col >= inner_w then
        break
      end
    end
    rows[#rows + 1] = { text = trunc_cells(line, inner_w), fg = fg, bg = bg,
                        spans = { { start = 0, len = 65535, fg = fg, bg = bg } } }
    for _, sp in ipairs(spans) do
      rows[#rows].spans[#rows[#rows].spans + 1] = sp
    end
  end

  -- Header row: the debugger passes "adapter  program" (adapter accent,
  -- program dimmed); other panels pass plain header text (bold).
  if p.header and p.header ~= "" then
    if p.tabs and #p.tabs > 0 then
      local sp = p.header:find("  ", 1, true)
      if sp then
        local adapter = p.header:sub(1, sp - 1)
        local program = p.header:sub(sp + 2)
        local head = trunc_cells(p.header, inner_w)
        add(head, fg, bg, false, {
          { start = 0, len = 65535, fg = fg, bg = bg },
          { start = 0, len = #adapter, fg = accent, bg = bg, bold = true },
          { start = sp + 1, len = #program, fg = comment, bg = bg },
        })
      else
        add(p.header, p.header_fg or 6, bg, true)
      end
    else
      add(p.header, p.header_fg or 6, bg, true)
    end
  end
  -- Empty-state note.
  if p.note and p.note ~= "" then
    add(p.note, p.note_fg or comment, bg, false)
  end

  local is_debugger = p.tabs and #p.tabs > 0
  for _, r in ipairs(p.rows or {}) do
    if #rows >= inner_h then
      break
    end
    if is_debugger and r.kind then
      local kind = r.kind
      if kind == "section" then
        section(r.text or "")
      elseif kind == "memory" then
        memory_row(r)
      elseif kind == "var" then
        local v = var_row(r)
        if not v then
          add(r.text, r.fg or fg, r.bg or bg, r.bold)
        else
          local text = v.name .. " = " .. v.value
          if v.vtype then
            text = text .. " : " .. v.vtype
          end
          local name_w = #v.name
          local spans = {
            { start = 0, len = 65535, fg = fg, bg = bg },
            { start = 0, len = name_w, fg = colors.variable or fg, bg = bg },
            { start = name_w, len = 3, fg = comment, bg = bg },
            { start = name_w + 3, len = #v.value, fg = v.vfg, bg = bg },
          }
          if v.vtype then
            spans[#spans + 1] = { start = name_w + 3 + #v.value + 3, len = #v.vtype,
                                  fg = comment, bg = bg }
          end
          add(text, fg, bg, false, spans)
        end
      elseif kind == "thread_active" then
        add(r.text, accent, bg, true)
      elseif kind == "thread" then
        add(r.text, fg, bg, false)
      elseif kind == "frame_active" then
        add(r.text, accent, bg, true)
      elseif kind == "frame" then
        add(r.text, r.fg or fg, r.bg or bg, r.bold)
      elseif kind == "instruction" then
        local sp = (r.text or ""):find("  ", 1, true)
        if sp then
          add(r.text, fg, bg, false, {
            { start = 0, len = 65535, fg = fg, bg = bg },
            { start = 0, len = sp - 1, fg = accent, bg = bg },
          })
        else
          add(r.text, fg, bg, false)
        end
      else
        -- frame / output / empty / config rows keep their native colors.
        add(r.text, r.fg or fg, r.bg or bg, r.bold)
      end
    else
      -- Generic panels: selection rows get the selection bar; outline rows
      -- carry a right-aligned line number detail.
      local line_w = math.min(math.floor(inner_w / 4), 7)
      local sel = r.selected
      local f = sel and selection_fg or (r.fg or fg)
      local b = sel and selection_bg or (r.bg or bg)
      local text = r.text or ""
      local detail = r.detail or ""
      if detail ~= "" then
        local name_w = math.max(1, inner_w - line_w)
        local name = trunc_cells(text, name_w)
        local gap = math.max(0, name_w - cell_len(name))
        local line = name .. string.rep(" ", gap) .. trunc_cells(detail, line_w)
        rows[#rows + 1] = {
          text = line,
          fg = f,
          bg = b,
          spans = {
            { start = 0, len = 65535, fg = f, bg = b, bold = sel },
            { start = 0, len = #name, fg = f, bg = b, bold = sel },
            { start = #name + gap, len = #line - #name - gap, fg = sel and selection_fg or comment,
              bg = b, bold = false },
          },
        }
      else
        rows[#rows + 1] = {
          text = trunc_cells(text, inner_w),
          fg = f,
          bg = b,
          spans = { { start = 0, len = 65535, fg = f, bg = b, bold = r.bold } },
        }
      end
    end
  end

  -- Debugger error line (native draws it over the bottom border; here it
  -- becomes the last content row in error colors).
  if p.error and p.error ~= "" and #rows < inner_h then
    add(p.error, colors.error or 15, colors.status_error_bg or 1, true)
  end

  -- Key-hint footer for live debug sessions.
  local footer = nil
  if is_debugger then
    footer = "F5 cont  F6 thr  F7/F8 frame  F9 bp  F10 over  F11 in"
  end

  return present_panel("side_panel",
                       p,
                       rows,
                       {
                         border = "single",
                         title = p.title or nil,
                         title_fg = accent,
                         footer = footer,
                         footer_fg = comment,
                       })
end


return {
  side_panel = side_panel,
}