# Plugins

`jot` runs trusted local Lua plugins in an embedded Lua 5.4 runtime. API
functions are injected as globals; plugins do not import a package.

Load order:

```text
~/.config/jot/init.lua
~/.config/jot/plugins/*.lua
~/.config/jot/plugins/*/plugin.lua
```

Set `JOT_CONFIG_HOME` to use another config root. Example:

```lua
command("hello", function(arg) show_message("Hello " .. arg) end, "Say hello")
autocmd("BufSave", function(event) show_message("saved " .. event) end)
register_keymap("Alt+H", function() show_message("hi") end, "Say hi")
```

Available injected globals include `command`, `autocmd`, `register_keymap`,
`register_panel`, `show_message`, `get_current_buffer`, `set_current_buffer`,
`get_selection`, `replace_selection`, `insert_text`, `cursor`, `set_cursor`,
`current_file`, `open_file`, `save`, `execute`, `run_job`, `show_picker`, and
`show_panel`. The `jot` global contains runtime metadata such as
`jot.api_version` and the extended namespaces documented in
`docs/LUA_API.md`: `jot.buffer.set_var/get_var/del_var` (buffer-local
variables), `jot.diagnostics.get`, `jot.marks.*` (named marks),
`jot.job.capture` (async output capture), and `jot.status.register`
(custom status bar segments). `BufChange` autocmd callbacks now receive the
editing buffer's `buffer`, `line`, and `column` fields.

### Keymap groups (which-key helper)

`register_keymap` keys may be multi-chord **sequences** written with spaces;
pressing the prefix chord automatically pops up a which-key style helper above
the status line listing the possible next keys:

```lua
register_keymap("Ctrl+T N", function() jot.file.new() end, "New file")
register_keymap("Ctrl+T D", function() jot.file.close() end, "Close file")
-- Subgroups nest: Ctrl+T then W then L runs this.
register_keymap("Ctrl+T W L", function() show_message("layout!") end, "Toggle layout")
-- Optional: give the group a title with a bare keymap carrying only a detail.
register_keymap("Ctrl+T", "", "Tabs")
```

Press `Ctrl+T`: since it prefixes longer bindings, the helper opens above the
status line with `N — New file`, `D — Close file`, and `W ▸` (a subgroup).
Pressing the next chord runs the action (`N`/`D`) or descends (`W`, then `L`).
`Esc` closes, `Backspace` steps back up a level, and arrow keys move a
selection that `Enter` runs.

Rules:

- Single-chord keymaps behave exactly as before: a chord that *is* a binding
  runs instantly — the helper only appears when the chord is a prefix of
  longer sequences.
- A bare keymap with an action wins over using the same chord as a group
  prefix (registering `"Ctrl+T"` with a callback shadows a `"Ctrl+T …"`
  group). To label a group instead, register the bare key with no callback,
  only a detail, as above.
- The helper is shown/used in the editor and applies to `global`- and
  `editor`-mode keymaps.

### Held Ctrl (Windows Terminal)

Most terminals cannot tell when you merely *hold* Ctrl, because pressing a
modifier alone sends no bytes. Windows Terminal (and the classic Windows
console) does report bare modifier presses, so when jot runs there, **holding
Ctrl alone opens the helper listing every Ctrl+ binding** — the built-in ones
plus your Lua keymaps — and releasing Ctrl dismisses it. Pressing a letter
while holding Ctrl runs that binding as usual. In terminals that cannot
report modifier-only keys, prefix chords (the previous section) are the way
to reach multi-step keymaps.

Callbacks receive a string. Autocmd callbacks receive `event\nfile\nbuffer`.
Picker and panel callbacks return a Lua array of strings.

Use `:reloadplugins` while developing. `:plugins` lists loaded files, commands,
keymaps, panels, and errors.
