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
-- A bare key with an empty action is a group title (which-key marks it with a
-- marker of its own, so the label carries no ellipsis), and every child carries the
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
  { "s", "statement" },
}

for _, op in ipairs(operators) do
  local base = op.key
  jot.keymap.set(base, "", op.label)

  -- Word and line need no inside/around: the selection IS the object.
  jot.keymap.set(base .. " w", ":" .. op.verb .. " word", op.label .. " word")
  jot.keymap.set(base .. " l", ":" .. op.verb .. " line", op.label .. " line")

  for _, qualifier in ipairs({ { "i", "inside" }, { "a", "around" } }) do
    local qkey, qword = qualifier[1], qualifier[2]
    jot.keymap.set(base .. " " .. qkey, "", op.label .. " " .. qword)
    for _, object in ipairs(objects) do
      local okey, oname = object[1], object[2]
      jot.keymap.set(base .. " " .. qkey .. " " .. okey,
                     ":" .. op.verb .. " " .. qword .. " " .. oname,
                     op.label .. " " .. qword .. " " .. oname)
    end
  end
end

-- Selection: expand, shape, collapse. The "visual" family.
jot.keymap.set("Alt+V", "", "Selection")
jot.keymap.set("Alt+V e", ":expand", "Expand to the enclosing syntax node")
jot.keymap.set("Alt+V c", ":shrink", "Shrink one level in")
jot.keymap.set("Alt+V k", ":keepprimary", "Keep only the primary selection")
jot.keymap.set("Alt+V r", ":rotatecaret", "Make the next selection the primary")
jot.keymap.set("Alt+V b", ":addcaretbelow", "Add a cursor on the line below")
jot.keymap.set("Alt+V a", ":addcaretabove", "Add a cursor on the line above")
jot.keymap.set("Alt+V l", ":splitlines", "One cursor per line of the selection")
jot.keymap.set("Alt+V m", ":selectoccurrences", "Every occurrence becomes a cursor")
-- Command strings, like every other leaf here: `jot.command` registers a named
-- command, it does not run an ex command, so a callback that called it left the
-- key doing nothing at all.
jot.keymap.set("Alt+V s", "", "Select an object")
for _, object in ipairs(objects) do
  local okey, oname = object[1], object[2]
  jot.keymap.set("Alt+V s " .. okey, ":textobject around " .. oname, "Select the " .. oname)
end
-- The statement is the object that comes up while editing rather than while
-- browsing, and it is the one that spans lines (a declaration, an `if`, a call
-- written over several rows), so it gets a chord without the menu: one press
-- selects it instead of walking out with expand.
--
-- Shift+S and not S: which-key matches the pressed key against a child
-- case-insensitively (a plain letter is canonicalised to its uppercase form),
-- and the uppercase children sort first. "Alt+V S" would therefore be the child
-- a plain `s` matched, and the object menu below it would never open. A shifted
-- letter canonicalises to "Shift+S", which is a token of its own, so the menu
-- keeps its `s` and the shortcut sits beside it.
jot.keymap.set("Alt+V Shift+S", ":textobject around statement", "Select the current statement")

-- Next/previous: one rule, many things (the unimpaired family). Diagnostics gain
-- a home here instead of an arbitrary chord; Alt+E stays as an alias.
local jumps = {
  { "f", ":nextfunction", ":prevfunction", "function" },
  { "c", ":nextclass", ":prevclass", "class" },
  { "d", ":diagnext", ":diagprev", "diagnostic" },
  -- Compile errors join the same family: a build's findings are walked exactly
  -- the way the language server's are, which is what makes :cnext findable.
  { "e", ":cnext", ":cprev", "compile error" },
}
-- The TODO-comment jump (`Alt+] t` / `Alt+[ t`) joins this family from its own
-- feature: features/todo_comments.lua owns both the command and the chord, so
-- the grammar above stays commands the native list can name.
jot.keymap.set("Alt+]", "", "Next")
jot.keymap.set("Alt+[", "", "Previous")
for _, jump in ipairs(jumps) do
  jot.keymap.set("Alt+] " .. jump[1], jump[2], "Next " .. jump[4])
  jot.keymap.set("Alt+[ " .. jump[1], jump[3], "Previous " .. jump[4])
end

-- ---------------------------------------------------------------------------
-- Code navigation (the LSP family).
--
-- Alt+G is already "file start", so the family lives under Alt+C ("code").
-- The four location lookups are genuinely different places in C++ -- a
-- declaration is usually the header, the definition the .cpp, the type
-- definition what an `auto` or a typedef really is, and the implementation the
-- override body -- which is why they get a key each instead of one "go to".
-- Alt+C h is clangd's switchSourceHeader, the CLion header/source flip.
-- ---------------------------------------------------------------------------
-- The status-line prompt (Emacs' minibuffer).
--
-- One prefix for the three commands that ask for a line instead of acting on
-- the buffer: Alt+Q. Its children are the Emacs keys people already reach for
-- (M-x compile, C-s, C-x C-f), spelled as chords because Alt+X, Alt+S and
-- Alt+F are taken -- and the prompt they open is the point, not the chord: a
-- build command, a search and a path all arrive typed, not chosen.
--
-- The natives for a build with no Lua are the ex commands (:compile :isearch
-- :findfile), which this family is a shortcut onto.
jot.keymap.set("Alt+Q", "", "Prompt")
jot.keymap.set("Alt+Q c", ":compile", "Compile command (any shell command)")
jot.keymap.set("Alt+Q s", ":isearch", "Incremental search on the status line")
jot.keymap.set("Alt+Q f", ":findfile", "Find a file by path")
jot.keymap.set("Alt+Q k", ":ckill", "Stop the running build")

jot.keymap.set("Alt+C", "", "Code")
jot.keymap.set("Alt+C d", ":gd", "Go to definition")
jot.keymap.set("Alt+C c", ":lspdecl", "Go to declaration")
jot.keymap.set("Alt+C t", ":lsptypedef", "Go to type definition")
jot.keymap.set("Alt+C i", ":lspimpl", "Go to implementation")
jot.keymap.set("Alt+C h", ":switchheader", "Switch header/source")
jot.keymap.set("Alt+C r", ":lsprefs", "Find references")
jot.keymap.set("Alt+C n", ":lsprename", "Rename symbol")
jot.keymap.set("Alt+C a", ":lspactions", "Code actions")
jot.keymap.set("Alt+C s", ":symbols", "Document symbols")
jot.keymap.set("Alt+C w", ":wsymbols", "Workspace symbols")
jot.keymap.set("Alt+C k", ":hover", "Show documentation")

-- ---------------------------------------------------------------------------
-- In-place edits: act on the word or the pair the caret is already in.
--
-- The operators above take three keys (Alt+D, a qualifier, an object) because
-- they work on any of several objects, one of them a tree-sitter node. The two
-- shapes below are the ones that come up mid-edit and want one chord: the word
-- the caret sits on, and whatever is inside the string or the brackets it is
-- in. Alt+X carries the pair family the way Alt+V carries selection, and with
-- no insert mode there is nothing else clearing one is called: the text goes
-- and the caret takes its place.
jot.keymap.set("Alt+Backspace", ":deleteword", "Delete the word at the caret")
jot.keymap.set("Alt+X", "", "Clear inside")
jot.keymap.set("Alt+X S", ":changeinside string", "Clear inside the string")
jot.keymap.set("Alt+X B", ":changeinside bracket", "Clear inside the brackets")

-- ---------------------------------------------------------------------------
-- Panes: Ctrl+W, the prefix Vim puts its window commands behind.
--
-- The family this replaces was Alt+Shift+<letter>, and it failed in ways no
-- change inside the editor could repair: a desktop that binds Alt+Shift
-- (hyprland, KDE, XKB's grp:alt_shift_toggle) takes the letter before the
-- terminal sees it; the GUI's SDL path reports Shift+Alt+letter as a lowercase
-- keycode, so a matcher keyed on the uppercase letter never fired there at all;
-- and the chords were hardcoded, so a user who did hit the conflict had nothing
-- to rebind. Ctrl+W is a single control byte, so it arrives identically from
-- every terminal and from --gui, and no window manager claims it -- and being
-- registered here it is listed by which-key and can be overridden from a config
-- like any other binding.
--
-- Ctrl+W is a group, not an action (Alt+W is the close-buffer chord): pressing
-- it opens the menu below, so the family is found rather than memorised. The
-- children follow :wincmd -- s/v split down/right, Shift+S/Shift+V up/left, a
-- shifted letter being a step of its own (which is what lets the two splits sit
-- beside the plain letters instead of shadowing them), h/j/k/l move the focus,
-- w/p walk the panes, c closes, o zooms, r rotates the layout, x swaps.
jot.keymap.set("Ctrl+W", "", "Pane")
jot.keymap.set("Ctrl+W s", ":splitdown", "Split down")
jot.keymap.set("Ctrl+W v", ":splitright", "Split right")
jot.keymap.set("Ctrl+W Shift+S", ":splitup", "Split up")
jot.keymap.set("Ctrl+W Shift+V", ":splitleft", "Split left")
jot.keymap.set("Ctrl+W h", ":wincmd h", "Focus the pane left")
jot.keymap.set("Ctrl+W j", ":wincmd j", "Focus the pane below")
jot.keymap.set("Ctrl+W k", ":wincmd k", "Focus the pane above")
jot.keymap.set("Ctrl+W l", ":wincmd l", "Focus the pane right")
jot.keymap.set("Ctrl+W w", ":nextpane", "Next pane")
jot.keymap.set("Ctrl+W p", ":prevpane", "Previous pane")
jot.keymap.set("Ctrl+W c", ":wincmd q", "Close the pane")
jot.keymap.set("Ctrl+W e", ":equalizepanes", "Equalize the panes")
jot.keymap.set("Ctrl+W o", ":panezoom", "Zoom the pane")
jot.keymap.set("Ctrl+W r", ":wincmd r", "Rotate the layout")
jot.keymap.set("Ctrl+W x", ":swappanes", "Swap two panes")

-- ---------------------------------------------------------------------------
-- Macros: Alt+Z, the recorder's prefix.
--
-- "q" is Vim's key, and here it starts the recording and stops it; "p" plays
-- the last register back; "l" lists what the registers hold. A macro is the
-- keystrokes, not the edits, so playing one re-runs it against whatever buffer
-- is current. The commands behind the chords (:macrorecord, :macroplay,
-- :macrolist) are the other way in, and the same three chords are handled
-- natively in src/input/modes/dispatch.cpp for a build without Lua.
-- ---------------------------------------------------------------------------
jot.keymap.set("Alt+Z", "", "Macro")
jot.keymap.set("Alt+Z q", ":macrorecord", "Record a macro / stop recording")
jot.keymap.set("Alt+Z p", ":macroplay", "Play the last macro")
jot.keymap.set("Alt+Z l", ":macrolist", "List the recorded macros")

-- The four splits also answer on Alt+Shift+Arrow, which is one chord instead of
-- two. Arrows and not letters: an arrow report carries its modifiers as a real
-- terminal sequence (CSI 1;4A) and as an arrow keycode in the GUI, so neither a
-- layout switch nor a window manager's Alt+Shift binding can turn the chord
-- into a different one. This is the same set the native handler keeps for a
-- build with no Lua (src/input/modes/dispatch.cpp).
jot.keymap.set("Alt+Shift+Left", ":splitleft", "Split left")
jot.keymap.set("Alt+Shift+Down", ":splitdown", "Split down")
jot.keymap.set("Alt+Shift+Up", ":splitup", "Split up")
jot.keymap.set("Alt+Shift+Right", ":splitright", "Split right")
