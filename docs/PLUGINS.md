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

Callbacks receive a string. Autocmd callbacks receive `event\nfile\nbuffer`.
Picker and panel callbacks return a Lua array of strings.

Use `:reloadplugins` while developing. `:plugins` lists loaded files, commands,
keymaps, panels, and errors.
