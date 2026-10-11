-- Zen focus mode (features/zen.lua).
--
-- F12 toggles between the normal layout and a distraction-free one: the
-- sidebar, right dock, tab strip and status line hide and the pane area takes
-- the whole window. It is persistent (the zen_mode config key), so the next
-- start lands the same way.
--
-- This module owns everything user-facing (key, toast, description); the
-- actual layout flip is native (jot.ui.toggle_zen) because only the C++
-- layout pass can reflow panes and reclaim the status-line rows. Rebinding
-- the key is a normal jot.keymap.set in config.lua.

local function toggle()
  local ok, zen = pcall(function() return jot.ui.toggle_zen() end)
  if not ok or type(zen) ~= "boolean" then
    return
  end
  pcall(function()
    jot.toast.show{
      message = zen and "Zen mode: on" or "Zen mode: off",
      title = "Focus",
      duration_ms = 1500,
    }
  end)
  return zen
end

jot.keymap.set("F12", toggle, "Zen: toggle focus mode")

-- Exposed so user configs can wrap/customize it.
return {
  toggle = toggle,
}