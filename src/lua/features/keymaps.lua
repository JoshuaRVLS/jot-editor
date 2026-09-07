-- Built-in editor keybinds (features/keymaps.lua).
--
-- VSCode-style line insertion: Ctrl+Enter opens a new line below the
-- cursor, Ctrl+Shift+Enter above it. The native modeless input handler
-- falls back to the same actions when Lua keymaps are unavailable, but
-- registering here keeps the bindings visible in the held-modifier helper
-- and lets user configs rebind them with the usual jot.keymap API.

jot.keymap.set("Ctrl+Enter", ":newlinebelow", "Insert line below")
jot.keymap.set("Ctrl+Shift+Enter", ":newlineabove", "Insert line above")