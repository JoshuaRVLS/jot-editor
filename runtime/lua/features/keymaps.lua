-- Built-in editor keybinds (features/keymaps.lua).
--
-- VSCode-style line insertion: Ctrl+Enter opens a new line below the
-- cursor, Ctrl+Shift+Enter above it. The native modeless input handler
-- falls back to the same actions when Lua keymaps are unavailable, but
-- registering here keeps the bindings visible in the held-modifier helper
-- and lets user configs rebind them with the usual jot.keymap API.

jot.keymap.set("Ctrl+Enter", ":newlinebelow", "Insert line below")
jot.keymap.set("Ctrl+Shift+Enter", ":newlineabove", "Insert line above")

-- Debugger (DAP) control keys, VS Code-style. Start a session with
-- :debug <program>, :debugconfig <name> or :debugattach <pid>; these keys
-- then control the active session. F9 toggles a breakpoint on the current
-- line of the focused buffer.
jot.keymap.set("F5", ":debugcontinue", "Debug: continue")
jot.keymap.set("Shift+F5", ":debugstop", "Debug: stop")
jot.keymap.set("F9", function()
  local path = jot.buffer.current_file()
  local line = select(1, jot.buffer.cursor())
  if path and path ~= "" and line then
    jot.debugger.toggle_breakpoint(path, line)
  end
end, "Debug: toggle breakpoint")
jot.keymap.set("F10", ":debugnext", "Debug: step over")
jot.keymap.set("F11", ":debugstep", "Debug: step into")
jot.keymap.set("Shift+F11", ":debugout", "Debug: step out")
-- Thread / frame navigation (active only while paused).
jot.keymap.set("F6", function() jot.debugger.cycle_thread(1) end, "Debug: next thread")
jot.keymap.set("Shift+F6", function() jot.debugger.cycle_thread(-1) end, "Debug: previous thread")
jot.keymap.set("F7", function() jot.debugger.cycle_frame(-1) end, "Debug: previous frame")
jot.keymap.set("F8", function() jot.debugger.cycle_frame(1) end, "Debug: next frame")
-- Output history scrollback (mouse wheel over the panel works too).
jot.keymap.set("Ctrl+PageUp", function() jot.debugger.scroll_output(6) end, "Debug: scroll output up")
jot.keymap.set("Ctrl+PageDown", function() jot.debugger.scroll_output(-6) end, "Debug: scroll output down")