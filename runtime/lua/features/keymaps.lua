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

-- ---------------------------------------------------------------------------
-- The keymap grammar: operators, selection, next/previous.
--
-- Thirty unrelated chords are not memorisable, so the bindings with a family
-- shape live behind a prefix whose letter says what it is, and what follows is
-- chosen from the menu which-key shows. Vim's d/y + i/a + object grammar is the
-- one most people already have in their fingers, and it is what makes the
-- tree-sitter textobjects composable: "delete around function" is Alt+D a f
-- rather than a chord per verb and object.
--
-- A bare key with an empty action is a group title, and every child carries the
-- description which-key prints, so a binding can be found without memorising it.
-- Both properties are already part of jot.keymap.set; nothing here needs new
-- input machinery.

-- Operators. There is deliberately no "change": with no insert mode, changing an
-- object is deleting it and typing, so a second verb would be the same command
-- under another name. Every leaf is a single command, which is what the keymap
-- API is built for (and what the docs show).
local operators = {
  { key = "Alt+D", verb = "deleteobject", label = "Delete" },
  { key = "Alt+Y", verb = "yankobject", label = "Yank" },
}

-- Objects that take an inside/around qualifier, because the syntax tree has a
-- meaningful interior for them.
local objects = {
  { "f", "function" },
  { "c", "class" },
  { "a", "argument" },
}

for _, op in ipairs(operators) do
  local base = op.key
  jot.keymap.set(base, "", op.label .. " ...")

  -- Word and line need no inside/around: the selection IS the object.
  jot.keymap.set(base .. " w", ":" .. op.verb .. " word", op.label .. " word")
  jot.keymap.set(base .. " l", ":" .. op.verb .. " line", op.label .. " line")

  for _, qualifier in ipairs({ { "i", "inside" }, { "a", "around" } }) do
    local qkey, qword = qualifier[1], qualifier[2]
    jot.keymap.set(base .. " " .. qkey, "", op.label .. " " .. qword .. " ...")
    for _, object in ipairs(objects) do
      local okey, oname = object[1], object[2]
      jot.keymap.set(base .. " " .. qkey .. " " .. okey,
                     ":" .. op.verb .. " " .. qword .. " " .. oname,
                     op.label .. " " .. qword .. " " .. oname)
    end
  end
end

-- Selection: expand, shape, collapse. The "visual" family.
jot.keymap.set("Alt+V", "", "Selection ...")
jot.keymap.set("Alt+V e", ":expand", "Expand to the enclosing syntax node")
jot.keymap.set("Alt+V c", ":shrink", "Shrink one level in")
jot.keymap.set("Alt+V k", ":keepprimary", "Keep only the primary selection")
jot.keymap.set("Alt+V r", ":rotatecaret", "Make the next selection the primary")
jot.keymap.set("Alt+V b", ":addcaretbelow", "Add a cursor on the line below")
jot.keymap.set("Alt+V a", ":addcaretabove", "Add a cursor on the line above")
jot.keymap.set("Alt+V l", ":splitlines", "One cursor per line of the selection")
jot.keymap.set("Alt+V m", ":selectoccurrences", "Every occurrence becomes a cursor")
jot.keymap.set("Alt+V s", "", "Select an object ...")
for _, object in ipairs(objects) do
  local okey, oname = object[1], object[2]
  jot.keymap.set("Alt+V s " .. okey, function()
    jot.command("textobject around " .. oname)
  end, "Select the " .. oname)
end

-- Next/previous: one rule, many things (the unimpaired family). Diagnostics gain
-- a home here instead of an arbitrary chord; Alt+E stays as an alias.
local jumps = {
  { "f", ":nextfunction", ":prevfunction", "function" },
  { "c", ":nextclass", ":prevclass", "class" },
  { "d", ":diagnext", ":diagprev", "diagnostic" },
}
jot.keymap.set("Alt+]", "", "Next ...")
jot.keymap.set("Alt+[", "", "Previous ...")
for _, jump in ipairs(jumps) do
  jot.keymap.set("Alt+] " .. jump[1], jump[2], "Next " .. jump[4])
  jot.keymap.set("Alt+[ " .. jump[1], jump[3], "Previous " .. jump[4])
end
