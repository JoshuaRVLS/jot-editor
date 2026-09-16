-- Popup — part of the Lua UI kit.
-- Split out of features/ui.lua so each surface stays small and
-- focused; features/ui.lua is the orchestrator that requires
-- every module and registers the handlers.
local h = require("jot_ui.helpers")
local close = h.close
local truncate = h.truncate
local pad = h.pad
local present_panel = h.present_panel
local function popup(p)
  if not p then
    close("popup")
    return true
  end
  local colors = p.colors or {}
  local fg = colors.fg or 7
  local bg = colors.panel_bg or colors.bg or 0
  local comment = colors.comment or 8
  local accent = colors.accent or 6

  local inner_w = math.max(1, p.w - 2)
  local inner_h = math.max(1, p.h - 2)
  local lines = p.lines or {}
  local scroll = math.max(0, p.scroll or 0)
  local rows = {}
  for i = 1, inner_h do
    local idx = scroll + i
    if idx > #lines then
      break
    end
    rows[#rows + 1] = { text = pad(truncate(lines[idx] or "", inner_w), inner_w), fg = fg, bg = bg }
  end
  local footer
  if #lines > inner_h then
    local last = math.min(#lines, scroll + inner_h)
    footer = tostring(scroll + 1) .. "-" .. tostring(last) .. "/" .. tostring(#lines)
  end
  return present_panel("popup",
                       p,
                       rows,
                       { title = p.title and p.title ~= "" and " " .. p.title or nil,
                         title_fg = accent,
                         footer = footer })
end

-- ---------------------------------------------------------------------------
-- Save-as / quit prompts
-- ---------------------------------------------------------------------------

local function save_prompt(p)
  if not p then
    close("save_prompt")
    return true
  end
  local colors = p.colors or {}
  local fg = colors.fg or 7
  local bg = colors.panel_bg or colors.bg or 0
  local inner_w = math.max(1, p.w - 2)
  local rows = {
    { text = pad(truncate(" Filename: " .. (p.input or ""), inner_w), inner_w), fg = fg, bg = bg },
  }
  return present_panel("save_prompt", p, rows, { title = " Save As", title_fg = fg })
end

local function rename_prompt(p)
  if not p then
    close("rename_prompt")
    return true
  end
  local colors = p.colors or {}
  local fg = colors.fg or 7
  local bg = colors.panel_bg or colors.bg or 0
  local inner_w = math.max(1, p.w - 2)
  local rows = {
    { text = pad(truncate(" New name: " .. (p.input or ""), inner_w), inner_w), fg = fg, bg = bg },
  }
  return present_panel("rename_prompt", p, rows, { title = " Rename Symbol", title_fg = fg })
end

local function quit_prompt(p)
  if not p then
    close("quit_prompt")
    return true
  end
  local colors = p.colors or {}
  local fg = colors.fg or 7
  local bg = colors.panel_bg or colors.bg or 0
  local inner_w = math.max(1, p.w - 2)
  local rows = {
    { text = pad(truncate(" Unsaved changes! Quit anyway?", inner_w), inner_w), fg = fg, bg = bg },
  }
  return present_panel("quit_prompt", p, rows, {})
end


return {
  popup = popup,
  save_prompt = save_prompt,
  quit_prompt = quit_prompt,
  rename_prompt = rename_prompt,
}
