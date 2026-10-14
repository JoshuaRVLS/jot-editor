# Lua Runtime API

Jot embeds Lua and exposes the runtime as the global `jot` table. Scripts are
loaded after the editor, terminal, and initial buffer are ready. No package
import is required.

Lua line and column values are 1-based. Native C++ state remains private.

## The scripting dialect

The embedded engine is LuaJIT, which is Lua 5.1 plus the JIT, and the bundled
runtime is written to one dialect on top of it: **Lua 5.1 plus `utf8`**. That is
what a plugin may assume, and it is the same in a session, in a test and on
every platform, because there is only one engine to run on.

| | |
|---|---|
| Which release | `jot.engine` (`"LuaJIT 2.1.1788856981"`) |
| Beyond 5.1 | the JIT itself, `jit` (`jit.status()`, `jit.p` for profiling), `bit`, `ffi` through `require("ffi")`, the global `unpack` |
| Not in 5.1 | `//` and the other bitwise operators, `goto`, `math.type` and the integer subtype, `string.pack` / `string.unpack`, `table.pack`, `<close>` / `<const>` attributes, `coroutine.close` |
| Lua patterns | a literal `\0` stops the matcher, because 5.1 reads the pattern as a C string; 5.2 and newer allow one. `%z` is that byte |

`utf8` is the one library worth spelling out, because LuaJIT ships none: the
host installs the bundled implementation at
[runtime/lua/compat/utf8.lua](../runtime/lua/compat/utf8.lua) before any bundled
file or plugin runs, so `utf8.*` is always there. It answers what Lua 5.4's
library answers, including what each function does with bytes that are not
UTF-8: `codes` and `codepoint` raise, `len` returns nil plus the byte offset
where it stopped, and `offset` returns nil for a character that is not in the
subject.

Numbers are 5.1's: one type, no integer subtype, so `tostring(3.0)` is `"3"`.
A count shown next to text from elsewhere is worth formatting with
`string.format("%d", ...)` rather than left to `tostring`.

On the C side, [lua/lua_compat.h](../src/lua/lua_compat.h) defines the four
entry points LuaJIT lacks, `lua_rawlen`, `lua_absindex`, `lua_len` and
`lua_isinteger`, under those same names when the runtime's own header is older
than the version that added them, so a call site carries no `#ifdef` and reads
like the newer interface. [test/lua_engine_probe.py](../test/lua_engine_probe.py)
holds a session to the dialect above.

## Core

```lua
local text = jot.buffer.get_text()
jot.buffer.insert("hello")
local line, column = jot.cursor.get()
jot.cursor.set(line, column)
jot.file.open("src/main.cpp")
jot.file.save()
jot.editor.request_redraw()
```

Available core namespaces:

```text
jot.editor       execute, request_redraw, info
jot.buffer       get_text, set_text, get_selection, replace_selection,
                 insert, cursor, set_cursor, list, current, count,
                 text, lines, meta, selection, bookmarks, folds, tokens,
                 select, clear_selection, switch, filetype, get_line,
                 set_var, get_var, del_var
jot.cursor       get, set, select_all, select_line
jot.clipboard    copy, cut, paste, get
jot.motion       word_next, word_prev, line_start, line_end, file_start,
                 file_end, matching_bracket, select_function
jot.sidebar      info, set_view
jot.picker       active, info, items, accept, close
jot.events       subscribe, unsubscribe
jot.timer        set_timeout, set_interval, clear
jot.viewport     info, line_at, scroll_top, scroll_lines, scroll_col, reveal
jot.filetree     root, tree, children
jot.file         current_file, open, save, save_buffer, close, new,
                 open_workspace, recent
jot.pane         layout, list, split_horizontal, split_vertical,
                 focus_next, focus_previous, resize
jot.workspace    open, path, recent, execute, search
jot.symbols      list
jot.config       get, get_number, get_bool, set, unset, has, keys, path
jot.theme        list, apply, current, set_color, palette, get
jot.diagnostics  get
jot.marks        set, get, jump, del, list
jot.status       register, unregister
```

### Keymap groups

`register_keymap` (and `jot.keymap.set`) accepts **multi-chord sequences**:
write the chords space-separated, and pressing the prefix starts the sequence.
Run an action by pressing its next chord; subgroups nest (`"Ctrl+T W L"` →
`Ctrl+T`, then `W`, then `L`). `Esc` cancels, `Backspace` goes up a level.

```lua
register_keymap("Ctrl+T N", function() jot.file.new() end, "New file")
register_keymap("Ctrl+T D", function() jot.file.close() end, "Close file")
register_keymap("Ctrl+T", "", "Tabs") -- bare key + detail only = group title
```

Single-chord bindings run instantly and are unchanged; a chord only opens the
helper when it prefixes a longer sequence. A bare keymap that has a callback
wins over using the same chord as a group prefix.

## Runtime Actions

Action namespaces call the same command registry used by the editor. They are
runtime extension points and do not require recompiling the C++ core.

```text
jot.edit         undo, redo, insert_newline, delete, indent, outdent,
                 comment, duplicate, move_up, move_down, join, uppercase,
                 lowercase, replace, surround, increment, select_all,
                 select_line, search, format
jot.emmet        expand
jot.search       execute, info, matches, next, previous
jot.folds        execute, toggle, fold, unfold, all
jot.bookmarks    execute, toggle, next, previous
jot.terminal     execute, toggle, new, list, spawn, close, activate, write
jot.tasks        execute, show, list, run, rerun
jot.lsp          execute, clients, diagnostics, results, completions,
                 request_hover, request_definition, request_symbols,
                 request_completion, disabled, set_enabled, install,
                 remove, restart_all, definition, back, completion
jot.debugger     execute, configs, run_config, state, breakpoints,
                 toggle_breakpoint, has_breakpoint, request_stack,
                 request_variables, request_threads, continue, pause,
                 step_in, step_over, step_out, stop
jot.git          execute, info, status, stage, unstage, stage_all,
                 unstage_all, commit, refresh, diff
jot.treesitter   register_language, language_for_extension, status, parser,
                  parse, query, captures, set_query, set_capture_color,
                  disable_language, install_command, reload
jot.image        execute, open
jot.job          run, capture
jot.ai           prompts, prompt, actions, status, adapters, active, config,
                 chat.{open, toggle, send, stop, reset, set_adapter, messages},
                 inline, setup
jot.process      memory
```

Every command-backed namespace also provides `execute(command)`. This keeps
new native commands immediately available to Lua while typed bindings grow.

`jot.emmet.expand()` expands the Emmet abbreviation ending at the cursor,
returning `true` when it did (and starting a snippet session on it). The bundled
snippet keymap calls it from Tab, after the user's own triggers and before
`jot.edit.tab()`, which is the hook to replace if you want the gesture on a
different key:

```lua
-- Alt+E instead of Tab.
jot.keymap.set("Alt+E", function() jot.emmet.expand() end, "Emmet: expand", "editor")
```

## Native State And Actions

The following functions are real native bridges - they read or act on live
editor state rather than routing through the command registry, so data-heavy
features (status lines, git panels, custom task runners, symbol pickers) can
be written entirely in Lua.

### Zen mode

`jot.ui.toggle_zen()` flips zen focus mode and returns the new state
(`true` = on): the sidebar, right dock, tab strip and status line hide and the
pane area takes the whole window. It is persistent (`zen_mode` in
settings.conf), so a start or `:reload` lands in the same layout. Leaving zen
restores the layout from before. The built-in `F12` binding (features/zen.lua)
calls this and shows a toast; the viewport info exposes the current state as
`jot.viewport.info().zen`.

### Process

`jot.process.memory()` returns the editor process's current resident memory
usage in bytes (e.g. `134217728` for 128 MiB), or `nil` when the platform
could not be queried. The native side caches the reading for ~1 second, so
polling it every status-line frame is cheap. The built-in status line uses
it for its `mem` segment.

### Config (Lua-first, live-applied)

Configuration is **Lua-first**: `~/.config/jot/config.lua` (next to
`init.lua`) is loaded before `init.lua` and plugins on every startup and on
`:reload`, so the config file itself is Lua:

```lua
-- ~/.config/jot/config.lua
jot.config.set("tab_size", 4)
jot.config.set("show_indent_guides", true)
jot.config.set("color_scheme", "jot")
jot.config.set("auto_save", true)
jot.config.set("auto_save_interval_ms", 5000)
```

`jot.config.get(key[, default])` returns a string (`nil` when unset and no
default given); `get_number` and `get_bool` return typed values.
`jot.config.set(key, value)` accepts a string, number, or boolean, persists
it immediately, and **live-applies** every setting that maps to editor state
(no restart needed): `tab_size`, `show_indent_guides`, `rainbow_brackets`,
`relative_line_numbers`, `highlight_cursor_line`, `auto_indent`,
`smart_paste_indent`, `auto_save`,
`auto_save_interval_ms`, `render_fps`, `idle_fps`, `lsp_enabled` (the global
language-server switch: off stops every client and keeps new ones from
attaching), `lsp_change_debounce_ms`,
`lsp_diagnostics_quiet_ms`,
`terminal_height`, `debugger_height`, `right_panel_width`,
`image_viewer_backend`, the `treesitter_*` paths, and `color_scheme`
(switches the theme). Keys that are read on every use (`prettier_on_save`,
`clang_format_on_save`, `reindent_on_save`, `auto_detect_indent`, `lsp_completion_*`,
`status_clock`, `status_coding_time`, `wakatime`, `word_highlight`) are live
automatically. `keys()`, `has(key)`, `unset(key)`, and `path()` complete the
surface.

Precedence (later wins): built-in defaults < `configs/settings.conf` (the
runtime-save overlay written by `jot.config.set`) < `config.lua` <
`init.lua`. `config.lua` re-asserts its values on every startup and reload,
so it is the source of truth for the keys it sets.

#### `:reload` / `:reloadconfig`

- `:reload` - re-reads `settings.conf`, re-runs `config.lua`, live-applies
  everything, then reloads Lua plugins (`init.lua` + `plugins/`) and
  tree-sitter policy. One command to pick up any config/plugin change.
- `:reloadconfig` - config only: `settings.conf` + `config.lua` + live
  apply, without touching plugins.
- `:reloadplugins` - existing: plugins only.

Both reload commands are also reachable from the command palette, and
`jot.editor.execute(":reload")` works from Lua.

```lua
jot.config.set("tab_size", 4)
local wrap = jot.config.get_bool("word_wrap", false)
for _, key in ipairs(jot.config.keys()) do print(key) end
```

### Git

`jot.git.info()` returns the repo snapshot (`root`, `branch`, and the
`dirty`/`staged`/`unstaged`/`untracked`/`deleted`/`renamed`/`conflict`
counts, plus `files`) or `nil` outside a repository. `jot.git.status()` lists
per-file entries (`path`, `rel`, `code`, and boolean flags);
`jot.git.status(path)` looks up one file (absolute or workspace-relative).
`stage`/`unstage` accept the same path forms; `stage_all`, `unstage_all`,
`commit(message)`, and `refresh` act on the whole repo. Paths come from
`jot.git.status()` directly.

```lua
local info = jot.git.info()
if info then jot.status.register("branch", { side = "right", priority = 30,
  text = function() return " " .. info.branch end }) end
for _, f in ipairs(jot.git.status() or {}) do
  if f.staged then print(f.rel) end
end
```

### Terminal tasks

`jot.tasks.list()` reloads `.jot/tasks.json` and the global config tasks and
returns `{name, command, cwd, source}` entries. `jot.tasks.run(name[,
force_new])` starts one in a terminal (returns `true` on success),
`jot.tasks.rerun()` repeats the last run, and `jot.tasks.show()` opens the
native task popup.

### Symbols

`jot.symbols.list([buffer])` runs the native symbol extractor over the given
buffer (1-based index or path; defaults to the current buffer) and returns
`{name, kind, detail, line, column}` rows - the same data the Outline panel
and symbol picker use. Build a picker or outline entirely in Lua with it:

```lua
jot.ui.picker("Symbols", function()
  local items = {}
  for _, s in ipairs(jot.symbols.list()) do
    items[#items + 1] = ("%s\t%s (line %d)"):format(s.name, s.kind, s.line)
  end
  return items
end, function(label)
  -- The select callback receives the picked item; for a plain string item
  -- that is the label; a `{label = ..., value = ...}` row receives `value`
  -- instead, so the display text and the payload stay apart. A row may also
  -- carry `detail` (right-aligned secondary text) and `fg` (its own ink as a
  -- "#rrggbb" string or xterm index, painted while the row is not selected).
  -- Match it back to the symbol row and jump the cursor there.
  for _, s in ipairs(jot.symbols.list()) do
    local prefix = ("%s\t%s"):format(s.name, s.kind)
    if label:sub(1, #prefix) == prefix then
      jot.cursor.set(s.line, s.column)
      break
    end
  end
end)
```

### Editor & theme state

`jot.editor.info()` returns `theme`, `path`, `buffer` (1-based index),
`buffers` (count), `line`, `column`, `line_count`, `word` (identifier under
the cursor), and `modified` for the current file - one call for status-line
and context-aware plugins. `jot.theme.current()` returns the active scheme
name.

### Buffer queries

Every accessor below takes an optional buffer argument (1-based index or
filepath; defaults to the current buffer):

- `jot.buffer.current()` / `jot.buffer.count()` - active buffer index and total
- `jot.buffer.text([buffer])` - full content as one string (`nil` for lazy
  large files)
- `jot.buffer.lines([buffer])` - the content as an array of line strings
  (`nil` for lazy large files)
- `jot.buffer.meta([buffer])` - `path`, `name`, `modified`, `line_count`
- `jot.buffer.selection([buffer])` - `active` plus 1-based `start_line`/
  `start_col`/`end_line`/`end_col` anchors
- `jot.buffer.bookmarks([buffer])` - 1-based bookmark line numbers
- `jot.buffer.folds([buffer])` - `{start, end, collapsed}` rows (1-based)

```lua
local m = jot.buffer.meta()
print(m.name, m.modified and "(modified)" or "", m.line_count .. " lines")
local sel = jot.buffer.selection()
if sel.active then print("selected lines", sel.start_line, "-", sel.end_line) end
```

### Clipboard

`jot.clipboard.copy()`, `cut()`, and `paste()` invoke the native clipboard
actions (selection-aware; fall back to the whole line when nothing is
selected, matching the editor's behavior). `jot.clipboard.get()` reads the
current clipboard text (whatever `copy`/`cut` last stored).

### Integrated terminals

`jot.terminal.list()` returns `{index, label, active, current}` for every
open terminal. `spawn([label[, cwd]])` opens a shell (returns the new
terminal's 1-based index, or `0` when the shell failed to start);
`close([index])` and `activate([index])` operate on the given terminal or
the current one; `write(text[, index])` sends input to a terminal.

```lua
local id = jot.terminal.spawn("build")
if id > 0 then jot.terminal.write("make -j\r", id) end
```

### Workspace and recents

`jot.workspace.path()` returns the opened workspace root (`nil` when none).
`jot.workspace.recent()` lists recently opened workspaces and
`jot.file.recent()` lists recently edited files, so custom "jump back" and
home-screen features are pure Lua.

`jot.workspace.search(query[, limit])` walks the workspace for the same
case-insensitive substring match `:grep` uses (skipping `.git`, `node_modules`
and build directories) and returns `{path, relative_path, text, line, column}`
rows - 1-based positions of the first hit on each line, `text` the line itself,
up to `limit` rows (default 200, capped at 1000). It reads whole files, so a
feature that wants a broad walk (a TODO list, a custom grep UI) asks for the
ceiling it can live with.

### LSP clients and popups

`jot.lsp.clients()` returns `{language, root, running, initialized}` rows for
every live language server. `jot.ui.popup(text[, title])` shows the native
centered popup (the same overlay the editor uses for its own messages).

### LSP results on demand

Each `jot.lsp.request_*(callback)` call registers a one-shot sink, issues the
matching native request for the current buffer/cursor, and invokes the
callback on the main thread when the server answers. Results are 1-based.
When a sink is waiting, the native UI (hover popup, definition jump, symbols
picker) yields to Lua for that response.

```lua
-- peek definition without moving the cursor
jot.lsp.request_definition(function(res)
  local loc = res.locations[1]
  jot.ui.popup(("%s:%d"):format(loc.path, loc.line), " Definition ")
end)

-- hover text in your own panel
jot.lsp.request_hover(function(res)
  jot.buffer.set_var("hover", res.contents)
end)

-- rich LSP outline (hierarchy + docs) for the current file
jot.lsp.request_symbols(function(res)
  for _, s in ipairs(res.symbols) do
    print(s.kind, s.name, s.line .. ":" .. s.column)
  end
end)
```

`jot.lsp.diagnostics()` lists the last diagnostics each server published,
keyed by server: `{language, root, files: [{path, diagnostics: [{line,
column, end_line, end_col, message, severity, severity_name}]}]}` - useful
when multiple servers report on the same file and you need attribution.
`jot.lsp.results()` returns each server's last delivered answers
(`hover`, `definitions`, `symbols`) as a polling-friendly snapshot.

### Event bus

`jot.events.subscribe(name, fn)` registers an ambient listener and returns a
subscription key; `jot.events.unsubscribe(name, key)` removes it. Each event
payload includes an `event` field with the event name. Unlike the one-shot
`jot.lsp.request_*` sinks, the bus does not consume anything: native UI keeps
behaving normally and every matching native event reaches all subscribers.
Subscriptions are dropped on `:reloadplugins`.

Currently emitted native events:

- `lsp.hover` - `{contents, path, line, column}` (every hover answer)
- `lsp.definition` - `{path, line, column, locations: [...]}`
- `lsp.symbols` - `{path, symbols: [...]}`
- `lsp.completion` - `{path, anchor_line, anchor_col, items: [...]}`
- `git.refreshed` - the full repo snapshot (`root`, `branch`, all counts),
  emitted when the poll finds it changed
- `diagnostics.changed` - `{path, count, items: [...]}` per publish round
- `buffer.open` / `buffer.save` / `buffer.close` - `{path}`
- `theme.switched` - `{name}`
- `debugger.state_changed` - `{sessions: [{name, adapter, running, stopped,
  active_thread_id, active_frame_id}]}`, emitted (deduped) whenever any
  debugger session's observable state changes
- `config.changed` - `{key, value}` on every `jot.config.set`, or
  `{key, removed=true}` on `unset` - live config reactions without polling

```lua
local id = jot.events.subscribe("git.refreshed", function(ev)
  jot.status.register("branch", { side = "right", priority = 30,
    text = function() return " " .. (ev.branch or "") end })
end)

jot.events.subscribe("lsp.symbols", function(ev)
  print(("%d symbols in %s"):format(#ev.symbols, ev.path))
end)
-- ...
jot.events.unsubscribe("git.refreshed", id)
```

### Viewport

`jot.viewport.info()` mirrors the focused pane and its geometry so Lua
panels can match the editor pixel-for-pixel:

- `window` - `width`, `height`, `status_height`, `tab_height`
- `sidebar` / `minimap` / `terminal` / `right_panel` - `visible` plus
  `width`/`height` where applicable; `right_panel.width` is the rendered
  (screen-clamped) width, not the configured one
- `pane` - `x`, `y`, `width`, `height`, `buffer` (1-based), `active`
- `buffer` - `line_count`, `first_line`/`last_line` (visible range),
  `visible_lines`, `cursor_line`, `cursor_col`, `scroll_x`

`jot.viewport.line_at(y)` maps an absolute screen row back to the 1-based
buffer line rendered there in the focused pane (or `nil` when it is not
inside the text area).

Control mirrors scroll state: `scroll_top(line)` sets the first visible
line (clamped to the buffer), `scroll_lines(delta)` scrolls by rows,
`scroll_col(col)` sets the horizontal scroll, and `reveal()` scrolls the
cursor into view - all operate on the focused pane's buffer.

Selection on real buffers: `jot.buffer.select(start_line, start_col,
end_line, end_col[, buffer])` creates a visual selection (clamped to the
buffer) and `clear_selection([buffer])` removes it. Combine with
`jot.buffer.selection()` and `replace_selection` for Lua-driven
refactoring commands.

```lua
local v = jot.viewport.info()
local b = v.buffer
if b and (b.cursor_line < b.first_line or b.cursor_line > b.last_line) then
  jot.viewport.reveal()
end
local below = jot.viewport.line_at(v.pane.y + v.window.tab_height + b.visible_lines)
```

### Timers

`jot.timer.set_timeout(ms, fn)` schedules a one-shot native event-loop
timer and returns its id; `jot.timer.set_interval(ms, fn)` schedules a
repeating timer; `jot.timer.clear(id)` cancels one. Callbacks run on the
main thread (never from a worker) and receive no arguments. Timers are
cancelled automatically on `:reloadplugins` and shutdown.

```lua
-- debounce a status refresh: reset a 200ms timer on every keypress
autocmd("BufChange", function()
  if debounce_id then jot.timer.clear(debounce_id) end
  debounce_id = jot.timer.set_timeout(200, function()
    debounce_id = nil
    refresh_status()
  end)
end)
```

### Debugger live state

`jot.debugger.state()` returns the live session list - one entry per
started session with `name`, `adapter`, `program`, `running`, `stopped`,
`active_thread_id`, `active_frame_id`, `output` (rolled log), `last_error`,
plus `threads` (`{id, name, frames: [{id, name, path, line, column}]}`,
1-based) and `variables` (`{name, value, type, variables_reference}`) as
reported by the adapter.

`jot.debugger.breakpoints()` lists every breakpoint as
`{path, line, verified}` (1-based). `toggle_breakpoint(path, line)`
adds/removes one (syncing the live session, exactly like the editor's own
toggle) and returns whether it is now set; `has_breakpoint(path, line)`
queries without changing anything.

```lua
local frames = {}
for _, s in ipairs(jot.debugger.state()) do
  if s.stopped and s.threads[1] then
    local f = s.threads[1].frames[1]
    if f then frames[#frames + 1] = ("%s:%d %s"):format(f.path, f.line, f.name) end
  end
end
jot.ui.popup(table.concat(frames, "\n"), " Stopped frames ")
```

### Debugger requests on demand

`state()` is a snapshot of whatever the poll loop has already consumed; when
you need a *fresh* answer, use the one-shot request sinks. Each registers a
callback, issues the same native DAP request the editor uses, and delivers
the fresh result on the main thread when the adapter answers:

- `jot.debugger.request_stack(cb[, session])` - `{session, thread_id,
  frames: [{id, name, path, line, column}]}` (1-based). While a sink waits,
  the native file-jump/cursor-move/scopes chain yields for that one response - peek the stack without moving the editor.
- `jot.debugger.request_variables(cb[, session])` - `{session, frame_id,
  variables: [{name, value, type, variables_reference}]}`. Drives the full
  scopes → variables chain (asking the adapter for the first expandable
  scope's contents) and delivers the leaf values.
- `jot.debugger.request_threads(cb[, session])` - `{session, threads:
  [{id, name}]}`.

The optional `session` is a 1-based session index (default: the current
session). When no session is running, the request is a no-op and the
callback is not registered. Sinks are one-shot, auto-released, and dropped
on `:reloadplugins`.

```lua
jot.debugger.request_stack(function(res)  -- peek, no cursor jump
  local f = res.frames[1]
  if f then jot.ui.popup(("%s:%d %s"):format(f.path, f.line, f.name), " Top frame ") end
end)

jot.debugger.request_variables(function(res)  -- fresh locals, not the panel cache
  for _, v in ipairs(res.variables) do
    print(v.name, "=", v.value)
  end
end)
```

### Theme palette

`jot.theme.get(name)` resolves one theme slot or group name - `keyword`,
`normal`, `diagnostic_error`, a tree-sitter capture name, and so on - to
`{fg, bg}` as 24-bit `"#rrggbb"` strings, or `nil` when the theme does not
name it. The lookup accepts the same spellings decorations do (lowercase,
leading `@` stripped, dotted prefixes fall back to their parent), so feature
code can read the live palette for a single group; a half the group does not
set is omitted. A feature that wants to re-ink itself on a colour-scheme
switch subscribes to `theme.switched` and calls this again.

```lua
local info = jot.theme.get("diagnostic_info")
if info then jot.status.register("info", { fg = info.fg, text = " ready " }) end
```

`jot.theme.palette()` returns the current theme's full slot table - every
syntax and UI slot as `{fg, bg}` (e.g. `default`, `keyword`, `function`,
`status`, `sidebar`, `tab_active`, `git_added`, …) plus the
`syntax_*_explicit` booleans. Slots without a background color omit `bg`.
Combined with `set_color` this makes custom color-scheme tooling pure Lua:

```lua
local p = jot.theme.palette()
local fg = p.status.fg
local accent = p.git_added.fg
jot.status.register("accent", { side = "right", priority = 10, fg = accent,
  text = function() return " ready " end })
```

### Cursor motions

`jot.motion.*` runs the same native movement primitives the editor's own
keys use - ideal for Lua macros and custom keymaps: `word_next()`,
`word_prev()`, `line_start()` (smart start, like Home), `line_end()`,
`file_start()`, `file_end()`, `matching_bracket()` (jump to the paired
bracket), and `select_function()` (select the enclosing function body).

```lua
register_keymap("F8", function()
  jot.motion.word_next()
  jot.motion.matching_bracket()
  jot.motion.select_function()
end, "Select function after word")
```

### Sidebar

`jot.sidebar.info()` mirrors the explorer/git activity rail:
`{visible, width, view ("explorer"|"git"), selected, scroll}`.
`jot.sidebar.set_view("explorer"|"git")` switches the activity view and
ensures the sidebar is open.

### LSP manager actions

Beyond client state, the native server manager is reachable:
`jot.lsp.disabled()` lists disabled server names, `set_enabled(name, bool)`
enables/disables one live (the disabled set is saved in the workspace
session), `install(name)` / `remove(name)` start the native install/remove
flow (returning success), and `restart_all()` restarts every live client.

### Buffer extras

- `jot.buffer.filetype([buffer])` - the extension string driving syntax
  highlighting (e.g. `".cpp"`); combine with
  `jot.treesitter.language_for_extension` for the language name.
- `jot.buffer.get_line(line[, buffer])` - one 1-based line as a string
  (`nil` when out of range) - lighter than `lines()` for per-line reads.

### Git diff panel

`jot.git.diff(path[, staged])` opens the native right-panel diff for one
file (the same one `:gitdiff` opens) and returns whether it opened.

### Undo tree

`jot.undo.tree()` returns the history of the buffer in front as the tree it is:
`{states, current, last, writes, bytes}`. Each entry of `states` carries `seq`
(the number a panel shows), `time` (when that state was reached, in epoch
seconds), `saved` (`1` on the state the last write landed on, `2` on one written
since, `0` on one never saved), `current` (the state the buffer is on) and
`parent` (the seq behind it, `nil` for a root). `current` is the seq in front,
`last` is the highest seq handed out, `writes` counts the saves and `bytes`
reports what the history holds.

`jot.undo.lines(seq)` returns that state's text as an array of lines, or `nil`
when the state cannot be read. `jot.undo.goto(seq)` walks the buffer onto a
state and returns whether it moved; the walk is one restore per state between,
so it lands on exactly the text walking there would have produced.
`jot.undo.clear()` drops the history.

`:Undotree` is a bundled feature built on this table
([runtime/lua/features/undotree.lua](../runtime/lua/features/undotree.lua)): it
lists the states newest first with the cursor picked by `j`/`k`, diffs the one
under the cursor against the buffer with `jot.undo.lines`, and commits with
`jot.undo.goto` on `Enter`. `:UndotreeClear` is `jot.undo.clear`. A plugin can
read the same history:

```lua
-- How many branches the buffer has left behind, and where it sits now.
local tree = jot.undo.tree()
local parented = {}
for _, state in ipairs(tree.states) do
  if state.parent then parented[state.parent] = true end
end
local tips = 0
for _, state in ipairs(tree.states) do
  if not parented[state.seq] then tips = tips + 1 end
end
jot.ui.show_message(tips .. " tips; state " .. tree.current .. " is in front")
```

`jot.ui.panel(name, focus)` shows a registered panel's tab in the right dock;
`focus` (a boolean, off by default) moves the keyboard there, which a panel
that reads keys wants: those keys then arrive as panel events (see
[PLUGINS.md](PLUGINS.md)) and everything the panel does not use still reaches
the buffer. `jot.ui.close_panel()` closes the dock's plugin tab, exactly as the
focused panel's own `q` does.

### File tree

`jot.filetree.root()` returns the workspace root. `jot.filetree.tree()`
returns the exact tree the explorer sidebar renders (`{name, path, is_dir,
expanded, depth, children: [...]}` per node, nested) - same native data, no
disk re-walk. `jot.filetree.children(path)` returns the direct children of
one node.

```lua
local dirs = 0
local function count(node)
  if node.is_dir then dirs = dirs + 1 end
  for _, c in ipairs(node.children) do count(c) end
end
for _, n in ipairs(jot.filetree.tree()) do count(n) end
print(dirs .. " directories under " .. tostring(jot.filetree.root()))
```

### LSP completion items

`jot.lsp.request_completion(cb)` asks the server for completions at the
cursor (same native request as manual completion) and delivers the fresh
answer to a one-shot callback: `{path, anchor_line, anchor_col, items:
[{label, kind, kind_name, detail, documentation, insert_text, filter_text,
sort_text, insert_text_format, commit_characters, deprecated, preselect,
has_edit_range, edit_start_line, edit_start_char, edit_end_line,
edit_end_char}]}`. While the sink waits, the native completion popup yields
to Lua for that response.

```lua
jot.lsp.request_completion(function(res)
  local names = {}
  for _, item in ipairs(res.items) do
    if item.kind_name == "Function" then names[#names + 1] = item.label end
  end
  jot.ui.popup(table.concat(names, "\n"), " Functions ")
end)
```

`jot.lsp.completions()` is the polling-friendly snapshot of the editor's
current completion state: `visible`, `path`, `prefix`, `anchor_line`,
`anchor_col`, `selected`, `total`, and the currently filtered `items` rows.

### Search state and matches

`jot.search.info()` reports the live search panel state: `visible`, `query`,
`case_sensitive`, `whole_word`, `regex`, `replace`, `replace_text`, `scoped`,
`result_count`, and `result_index` (1-based position of the active match).
`jot.search.matches()` returns every current match as
`{line, column, len}` (1-based) - enough to build custom match lists, mini-maps,
and go-to-next/previous helpers without touching the native panel:

```lua
local info = jot.search.info()
if info.visible then
  print(("%d matches for %q"):format(info.result_count, info.query))
  local m = jot.search.matches()[info.result_index]
  if m then jot.cursor.set(m.line, m.column) end
end
```

### Quick-pick contents

While any quick pick (diagnostics, symbols, project search, or a Lua picker)
is open, `jot.picker.active()` tells you so; `jot.picker.info()` returns
`title`, `query`, `selected`, `visible`, `total`; `jot.picker.items()` lists
the currently shown `{label, detail, preview, filepath, line, column,
severity, fg}` rows. `accept()` runs the selected item and `close()` dismisses
the picker - useful for custom keymaps over native pickers.

### Syntax tokens

`jot.buffer.tokens(line[, buffer])` runs the same per-line highlighting pass
the renderer uses and returns the colored spans of that line:
`{start, end, token, name, text, fg}`. `start`/`end` are 0-based byte offsets
into the line; `name` is a canonical type (`keyword`, `string`, `comment`,
`number`, `type`, `function`, `variable`, `tag`, `namespace`, …); `fg` is the
active theme color index for that token, and `token` the raw engine id. Works
for both the regex highlighter and tree-sitter highlighting.

```lua
local row = {}
for _, t in ipairs(jot.buffer.tokens(jot.editor.info().line)) do
  if t.name == "function" then row[#row + 1] = t.text end
end
print("functions on this line:", table.concat(row, ", "))
```

## UI And Events

```lua
jot.ui.show_message("ready")
jot.ui.request_redraw()
jot.ui.command_palette(":reload")  -- open the command palette, pre-filled
jot.autocmd("CursorMoved", function(event)
  jot.ui.show_message(event.path .. ":" .. event.line)
end)
```

`jot.ui.command_palette([query])` opens the command palette (no-op when it
is already open), optionally pre-filling the query - the same entry point as
`Ctrl+P`, so a Lua keymap can hand off to command completion. The palette is
a bottom-anchored menu: the prompt row is the statusline slot, the results
fill the width above it as a column-major grid, and the selected row's
explanation box (its sentence, aliases and argument shape) stands on the
grid's top edge; with no results the grid keeps a row saying why it is empty.
`Tab` completes, `Esc` closes, `Enter` runs, `Up`/`Down`/`Left`/`Right` move
by row and column, and `PageUp`/`PageDown`/`Home`/`End` page and jump the
selection; a click runs the row under the pointer and the wheel walks rows.
Query characters that matched a result are emphasized (bold, accent color)
inside each label. Closing the palette with `Esc` (or toggling it off)
remembers the input text, so the next `Ctrl+P` resumes the search where it
was left; closing with an empty input clears the memory.

The quick-pick panels share the same visual language: bordered panel with
result counter, divider under the input, accent-bar selection, and matched
query text highlighted inside labels (`PageUp`/`PageDown`/`Home`/`End` page
and jump the selection).

All floating modals - the command palette, quick picks, the tree-sitter
status modal, popups, the LSP manager, and the save / rename / quit prompts - use one design language driven by theme slots: the editor behind the dialog is
dimmed, and the panel surface, border, and title row all come from the theme's
`bg_panel_border` slot, so switching colorschemes restyles every dialog at
once. A prompt is one of these: `quit_prompt` gets the rect and the colors and
paints its own row (the native painter, which runs when no handler is
registered, writes the same sentence).

Events currently include `BufOpen`, `BufChange`, `BufSave`, `BufClose`,
`CursorMoved`, `WorkspaceEnter`, `UIResize`, `DiagnosticChanged`, and
`Shutdown`. Event callbacks receive a table containing `event`, `path`, and
available buffer, line, and column fields. `BufChange` fires on every buffer
edit (typed characters, backspace, undo/redo, and other edit commands) and
carries the owning `buffer` index plus the cursor `line`/`column` after the
edit, so plugins can react per keystroke.

While a `BufChange` listener is registered, the event also carries a rich
edit delta computed against the buffer's previous state: `edit_start_line` /
`edit_start_col` (1-based insertion point), `edit_end_line` / `edit_end_col`
(exclusive end in the new text), `edit_inserted` / `edit_removed` (the text
added / replaced), and `edit_multiline`. Typing a character, deleting,
pasting a block, or undoing all produce precise deltas; very large (>100k
line) and lazy buffers omit the delta fields.

```lua
autocmd("BufChange", function(e)
  if e.edit_inserted == "(" then      -- smart-pair style hook
    jot.buffer.insert(")")
    jot.cursor.set(e.edit_start_line, e.edit_start_col)
  end
end)
```

## Introspection

```lua
print(jot.api_version)
local capabilities = jot.capabilities()
```

`jot.capabilities()` returns the available runtime namespaces. Use it when a
script must work across different Jot builds.

## Tree-sitter Runtime

`runtime/lua/treesitter/registry.lua` is the sole language registry. Each entry
contains `name`, `extensions`, `aliases`, `url`, `source_subdir`, `symbol`,
`library_names`, `query_file`, and `revision` (the grammar commit to build, read
from `runtime/lua/treesitter/lockfile.lua` and passed to
`jot.treesitter.register_language` as its ninth argument; empty means the
repository's default branch). Queries live at
`runtime/lua/treesitter/queries/<language>/highlights.scm`, one directory per
language.
The bundled registry covers every parser previously shipped by Jot. Lua loads
the registry and queries before syntax detection. A bundled query that is
missing or does not compile against the installed parser never disables the
language: runtime queries shipped with the parser (or regex syntax) are used
instead.

Parser installs use this layout:

```text
${XDG_DATA_HOME:-$HOME/.local/share}/jot/treesitter/
  parsers/<dynamic parser library>
  queries/<language>/highlights.scm
```

Set `JOT_TREESITTER_PREFIX` for an explicit writable root. `JOT_TREESITTER_PATH`
and `JOT_TREESITTER_QUERY_PATH` remain search-path overrides. `:tsreload` reloads
Lua registry/query policy and clears native parser, tree, query, and library
caches together. `:tsupdate [language ...]` rebuilds installed parsers at their
pinned revision.

## Diagnostics

`jot.diagnostics.get([buffer])` returns an array of the LSP/workspace
diagnostics for a buffer. `buffer` is optional: omit it for the current
buffer, or pass a 1-based buffer index or a filepath. Each entry has
`line`, `col`, `end_line`, `end_col` (1-based), `message`, `severity`
(1=Error, 2=Warning, 3=Info, 4=Hint), and `severity_name`.

```lua
for _, d in ipairs(jot.diagnostics.get()) do
  if d.severity == 1 then
    print(('%d:%d %s'):format(d.line, d.col, d.message))
  end
end
```

## Buffer Variables

`jot.buffer.set_var(name, value[, buffer])` stores a buffer-local variable
(value may be a string, number, or boolean; numbers/booleans are stored as
their `tostring` form). `jot.buffer.get_var(name[, buffer])` returns the
string value or `nil`. `jot.buffer.del_var(name[, buffer])` removes it.
The optional `buffer` argument is a 1-based index or filepath; the current
buffer is used when omitted. Values live in native per-buffer state, so they
are independent per buffer, survive edits, and are dropped when the buffer
closes.

```lua
autocmd("BufOpen", function(e)
  if e.path:match("%.lua$") then
    jot.buffer.set_var("ft", "lua")
  end
end)
```

## Marks

`jot.marks.set("a")` records the current cursor position under a single
letter. Lowercase names (`a`-`z`) are buffer-local: they are stored on the
buffer where they were set, and reading/jumping them from another buffer
sees that buffer's own marks. Uppercase names (`A`-`Z`) are global marks
that travel with their file across buffers. `set` on a global mark requires
a file-backed buffer.

- `jot.marks.set(name)` - record the current position; returns `true` on success
- `jot.marks.get(name)` - `{buffer, path, line, col, global}` (1-based), or `nil`
- `jot.marks.jump(name)` - open the owning file if needed and move the cursor
- `jot.marks.del(name)` - remove the mark
- `jot.marks.list()` - array of defined marks (`name`, `global`, `path`, `line`, `col`)

```lua
register_keymap("Alt+M", function() jot.marks.set("m") end, "Set mark m")
register_keymap("Alt+'", function() jot.marks.jump("m") end, "Jump to mark m")
```

## Shell Jobs

`jot.job.run(command[, cwd])` runs a command in an integrated terminal
(existing behavior). `jot.job.capture(command[, cwd], callback)` runs a
shell command on a worker thread (so the UI never blocks) and invokes
`callback` on the main thread when it finishes with
`{output=..., exit_code=..., ok=...}`. Both stdout and stderr are captured
and merged into `output`. The callback is one-shot and automatically
released. If the plugin set is reloaded while a job runs, the callback is
dropped safely. The call returns `true` when the job was queued and `false`
when it was refused, so a caller that writes scratch files for the command
knows whether anything will read them.

```lua
jot.job.capture("git diff --stat", function(result)
  if result.ok then
    show_message("diff:\n" .. result.output)
  else
    show_message("git failed (" .. result.exit_code .. ")")
  end
end)
```

## Status Line Segments

`jot.status.register(name, config)` renders plugin text in the status bar on
every frame. `config` fields:

- `text` (required) - function returning the segment string; may return a
  second value, an xterm color index (0-255) for its foreground
- `side` - `"left"` or `"right"` (default `"right"`)
- `priority` - drop order when the window is narrow (default 50)
- `fg` - optional static xterm color index; defaults to the theme's status
  text color when absent

`jot.status.unregister(name)` removes a segment. Segments drop before the
built-in status items when space runs out.

```lua
jot.status.register("branch", {
  side = "right",
  priority = 30,
  fg = 117,
  text = function() return " main" end,
})
```

## AI assistant

The bundled assistant (`runtime/lua/features/ai/*`, loaded by
`src/lua/api/api_runtime.cpp`) is a Lua module tree over `jot.job`,
`jot.timer`, `jot.ui` and `jot.buffer`, so a plugin can drive it or replace
parts of it:

- `jot.ai.setup{ adapter=, model=, prompts={ name = "…" } }` - set the adapter
  or model for the session and add prompts to the library.
- `jot.ai.prompts` - the prompt library (`explain`, `fix`, `tests`,
  `refactor`, `document`, `faster` by default, plus anything a config added).
- `jot.ai.prompt(name)` - run a named prompt through the inline assistant.
- `jot.ai.inline(prompt)` - rewrite the selection (or the caret's line) and
  preview it as a diff the user accepts with `y`.
- `jot.ai.chat.open() / toggle() / send() / stop() / reset()` - drive the chat
  buffer; `set_adapter(name)` pins an adapter to the session and
  `messages()` returns the transcript (`{role=, content=}` pairs).
- `jot.ai.actions()` - the prompts/adapters/chat-commands picker.
- `jot.ai.status()` - what is talking to what, as a popup.
- `jot.ai.adapters()` - the adapter names; `jot.ai.active()` - the resolved
  adapter (kind, endpoint, model, stream, temperature, max tokens, system
  prompt); `jot.ai.config` - the whole adapter registry, so a plugin can add an
  entry for a private gateway with `jot.ai.config.adapters.mine = {...}`.

The pieces that decide bytes are exposed for tests as well:
`jot.ai.http.payload(adapter, messages)`, `jot.ai.http.url(adapter)`,
`jot.ai.http.text_of(kind, frame)`, `jot.ai.http.error_of(frame)`,
`jot.ai.context.expand(text)`, `jot.ai.inline.diff(before, after)` and
`jot.ai.chat.render_lines(messages, status, prompt_text)`.

## Markdown Preview

Two layers: `jot.preview` is the native transport (a loopback HTTP server plus
an SSE broadcast), and `jot.md` is the bundled Lua feature that renders the
document and drives the session. The transport never parses markdown and the
feature never touches a socket, so either half can be replaced. The same
transport serves the HTML preview (`jot.html_preview`), which uses its file-root
mode instead of a stored page - see below.

### `jot.preview` (native)

- `jot.preview.start{ host = "127.0.0.1", port = 0, file_root = nil }` - binds
  the server (`0` picks a free port). Returns `true, port` or `nil, error`.
  `file_root` is a directory, not a page: it switches the server into *serving a
  tree*, described under [HTML preview](#html-preview).
- `jot.preview.stop()` - unbinds the server and closes every client.
- `jot.preview.status()` - `{ running = bool, port = int, clients = int }`.
- `jot.preview.set_page(html)` / `jot.preview.page()` - the full page served at
  `GET /`; the page shell is replaced as a whole.
- `jot.preview.set_content(body)` - the rendered document body kept for the
  initial `GET /` response (the SSE `content` event carries it to live clients).
- `jot.preview.set_document(rel_path, text)` - with a file root, the text to
  serve *in place of the file on disk* at that relative path, which is how a
  buffer's unsaved changes reach the browser. `("", "")` clears it. Only the
  edited document is overridden; every other path still comes from disk.
- `jot.preview.notify(event, data)` - broadcast an SSE event to every open
  client (the feature uses `content` and `title`).
- `jot.preview.sync(line)` - broadcast `event: sync` with a 1-based line, used
  to tell the page to scroll (data is sent raw, so several `data:` lines form a
  multi-line payload).
- `jot.preview.take_scroll()` - consumes and returns the last line the page
  asked the editor to scroll to (1-based), or `nil`.

The server also serves `GET /image?path=...` for local images referenced by the
document.

With a file root set, every `GET /<path>` is a file under that root (a request
that tries to climb out of it is refused), and a response whose content type is
HTML has a live-reload client appended before its `</body>` - an `EventSource`
on `/events` that calls `location.reload()` when the editor sends
`jot.preview.notify("reload", ...)`.

```lua
local ok, port = jot.preview.start({ port = 0 })
if ok then
  jot.preview.set_page("<html><body><div id='x'></div></body></html>")
  jot.preview.notify("content", "<p>hello</p>")
end
```

### `jot.md` (bundled feature)

- `jot.md.setup(opts)` - applies options; scalars go into the `markdown_preview_*`
  config keys, `options = { mermaid = true, ... }` into the
  `markdown_preview_option_*` switches, and `preprocessor = function(text, path)`
  stays on the module. Edits apply live.
- `jot.md.start([opts])`, `jot.md.stop()`, `jot.md.toggle()`, `jot.md.refresh([force])`
- `jot.md.is_running()`, `jot.md.url()`, `jot.md.state()` - `{ running, port, url, path }`
- `jot.md.render(text, { path, name })` - pure renderer returning
  `{ html, body, toc, title, options }` without touching the transport; handy
  for tests and for embedding a preview elsewhere.
- `jot.md.config` - the option module (`get`, `option`, `setup`, `is_markdown_path`).

```lua
jot.md.setup({
  auto_start = true,
  theme = "light",
  options = { mermaid = true, toc = true },
  preprocessor = function(text)
    return text:gsub("\r\n", "\n")
  end,
})
```

## HTML Preview

`jot.html_preview` is the bundled feature that opens the current HTML file in a
browser and reloads it as you edit. It shares the transport and the browser
launcher with the markdown preview; what is its own is the decision of *what the
server is pointed at*. A page is a tree - its `<link>`, `<script>` and `<img>`
references are relative paths - so the session roots the server at the workspace
(or the file's own directory) and sends the browser to the file's path inside
it, rather than rendering one page the server holds.

- `jot.html_preview.setup(opts)` - applies options into the `html_preview_*`
  config keys (`auto_start`, `auto_close`, `refresh_interval`, `port`, `host`,
  `root`, `browser`, `open_browser`, `echo_preview_url`, `html_ext`).
- `jot.html_preview.start([opts])`, `stop()`, `toggle()`, `refresh([force])` - `refresh` pushes the buffer's text again and reloads the page.
- `jot.html_preview.is_running()`, `url()`, `state()` - `{ running, port, url, path }`.
- `jot.html_preview.config` - the option module (`get`, `setup`, `is_html_path`).
- `jot.html_preview` also exposes the pure URL helpers (`relative_to`,
  `url_path`, `dirname`, `encode_path`), which is where a wrong URL would come
  from.

```lua
jot.html_preview.setup({
  auto_start = true,      -- open the preview with an HTML buffer
  root = "dir",          -- serve the file's own directory, not the workspace
  open_browser = false,   -- serve it and print the URL (e.g. over SSH)
})
```

```lua
-- The same transport by hand: serve the tree, override one document.
local ok, port = jot.preview.start({ file_root = "/w/site" })
if ok then
  jot.preview.set_document("index.html", "<h1>unsaved</h1>")
  jot.preview.notify("reload", "index.html")
end
```

The browser launcher is `jot_md.browser`, shared by both previews; `open(url,
choice)` takes an optional browser name so each feature can name its own setting.

## Snippets

The snippet engine is the bundled Lua feature reachable as `jot.snip`, mirroring
LuaSnip's `ls` surface. Snippet packs are loaded from `~/.config/jot/snippets`,
`<workspace>/.jot/snippets` and `snippet_paths`.

### Constructors

`jot.snip.s` / `sn` / `t` / `i` / `f` / `d` / `c` / `r` / `rep` / `fmt` / `isn` /
`ms` (and their long names `snippet`, `snippet_node`, `text_node`, `insert_node`,
`function_node`, `dynamic_node`, `choice_node`, `restore_node`, `repeat_node`,
`format_node`, `indent_snippet_node`, `multi_snippet`). `jot.snip.parse_snippet(context, text, opts)`
parses an LSP/VSCode snippet body directly.

```lua
jot.snip.add_snippets("lua", {
  jot.snip.s("forp", {
    jot.snip.t("for "), jot.snip.i(1, "k"), jot.snip.t(", "),
    jot.snip.i(2, "v"), jot.snip.t(" in "), jot.snip.i(3, "pairs"),
    jot.snip.t(" do\n\t"), jot.snip.i(0), jot.snip.t("\nend"),
  }),
})
```

### Registry

- `jot.snip.add_snippets(ft, snippets[, opts])` - `ft` may be a filetype or a
  list; `snippets` a list or a keyed map. `snippets` may also be a snippet
  definition table (`{ trig, nodes, snippetType, priority, condition, ... }`).
- `jot.snip.filetype_extend(ft, parents)` - include another filetype's snippets.
- `jot.snip.get_snippets(ft[, include_auto])`, `jot.snip.get_autosnippets(ft)`
- `jot.snip.cleanup(ft)`, `jot.snip.refresh_notify(ft)`, `jot.snip.invalidate_snippets()`
- `jot.snip.reload()` - re-read every snippet pack.

### Expansion and navigation

- `jot.snip.expand([opts])` - expand the trigger before the caret. Passing
  `{ snippet = <def> }` expands that snippet regardless of the trigger.
- `jot.snip.expand_or_jump()`, `jot.snip.expandable()`, `jot.snip.expand_auto()`
- `jot.snip.jump(dir[, absolute])`, `jot.snip.jumpable(dir)`,
  `jot.snip.locally_jumpable(dir)`, `jot.snip.jump_destination(dir)`
- `jot.snip.change_choice(dir)`, `jot.snip.choice_active()`,
  `jot.snip.current_node()`, `jot.snip.locate_node(pos)`,
  `jot.snip.active()`, `jot.snip.current_index()`, `jot.snip.region()`,
  `jot.snip.exit_out_of_region()`

### Environment

- `jot.snip.env.get(name, ctx)`, `jot.snip.env.extend(name, value)`,
  `jot.snip.env.resolve(text, ctx)`, `jot.snip.env.namespace(name, opts)`.

### Loaders and setup

- `jot.snip.loaders.from_lua(root)`, `from_vscode(root)`, `from_snipmate(root)`,
  `load_all()`, `reload()`
- `jot.snip.setup({ enabled = true, auto_expand = false, keymaps = {...},
  snip_env = {...}, paths = "..." })` - scalars write the `snippet_*` config
  keys, the rest stay on the module.
- `jot.snip.install_keymaps()` / `jot.snip.uninstall_keymaps()`
- `jot.snip.lsp` - the LSP snippet-completion bridge; `jot.lsp.register_snippet_handler(fn)`
  (see below) hands `fn` the snippet text of an LSP completion item.

### `jot.lsp.register_snippet_handler`

```lua
jot.lsp.register_snippet_handler(function(item)
  -- item = { text, kind, label, offset }
  return true -- claimed, or false to let the editor insert it as plain text
end)
```

The bundled engine registers one on load, so LSP completions with
`insertTextFormat = 2` expand with real placeholders out of the box; passing
`nil` unregisters it.

## Native Boundary

Lua owns runtime composition, commands, actions, and event policy. C++ owns
terminal rendering, raw input, event-loop scheduling, buffer storage, process
transports, LSP/DAP protocol handling, Tree-sitter parser ownership, and
platform integration. Lua receives typed wrappers and never receives native
pointers.

The boundary is a floor rather than a ceiling: `jot.screen`, `jot.input` and
`jot.term` expose the cell grid, the event stream and the terminal's own bytes
(see The Raw Layer below), so what C++ keeps is the machinery that produces
frames and events, not the decision about what they contain. A script can paint
any cell on screen and see any key or click before the editor does.

Tree-sitter parser, tree, and query values are positive opaque integer handles.
Delete them with `delete_parser`, `delete_tree`, and `delete_query`. Handles
become invalid after `reload`; native functions return Lua errors for invalid
handles. The bundled runtime loads before user plugins and compiled catalog
data remains the fallback when no Lua override is present.

## Policy Providers

Some of what the editor does is policy rather than mechanism: how a command's
argument is read, what a passage is called, which spellings count. `jot.policy`
lets a module own a named piece of that. The native call site asks the provider
first and keeps its own implementation as the fallback, so a decision moves to
Lua one piece at a time and a build without the module behaves as it did before.

`jot.policy.set(name, table)` registers `table` under `name` and returns `true`;
`set(name, nil)` hands the name back. `jot.policy.has(name)` is `true` while a
provider owns it, and `jot.policy.names()` lists the names in use, sorted. The
table's shape is whatever the name's contract calls for; a provider is not
verified on registration, so a call that is missing or answers with something
the native side cannot use is treated as no answer at all.

One name exists today. `bible.reference` is the reference grammar and label of
the Bible reader, ported from `src/extras/bible.cpp` into
`runtime/lua/features/bible/reference.lua` and registered as the module loads;
the C++ copy answers when the module is absent, fails to load, or answers with
something unusable. The contract is two functions:

```lua
jot.policy.set("bible.reference", {
  -- The command's argument in, a reference out, or nil when it is not one.
  parse = function(text)
    if text:match("^gospeljn 3:16$") then
      return { id = "JHN", chapter = 3, has_verse = true,
               verse_first = 16, verse_last = 16 }
    end
    return nil
  end,
  -- A reference in, the name the tab strip and the toast print out.
  label = function(ref)
    return "John " .. ref.chapter .. ":" .. ref.verse_first
  end,
})
```

A book travels as the id its requests use (`"JHN"`), never as an index or a
pointer, so the native canon stays the authority on which books exist: a
reference is accepted only once its id resolves there, and a provider therefore
cannot open a book the reader cannot fetch. A provider that raises costs a
fallback rather than the command.

A provider may carry plain fields instead of functions, which is a table of
entries rather than a decision. The native side reads the entry it needs when it
builds something, not while it paints, so a table of a few hundred rows costs
nothing per frame. `settings.catalog` is the first of these, owned by
`features/settings/catalog.lua`: the settings menu asks it for each row's label
and type.

```lua
jot.policy.set("settings.catalog", {
  tab_size = { label = "Indent width", type = "int", step = 1, min = 1, max = 16 },
  colorizer_mode = { label = "Color preview mode", type = "enum",
                     options = { "background", "foreground", "virtualtext" } },
})
```

`label` is the row's text; `type` is `"bool"`, `"int"`, `"string"` or
`"enum"`; `options` are the values an enum row walks; `step`, `min` and `max`
are an int row's stepper and its clamp. Declaring one key is enough, and the
rows you leave out keep theirs. A key with no entry still gets a row, labelled
with the key itself, which is what a plugin's own config key gets. An edited
catalog is live the next time the menu opens, and `:reloadplugins` picks up a
file edited on disk.

The native tables in src/surfaces/settings.cpp stay as the fallback for a build
or a test with no Lua runtime. `tools/settings_catalog_import.py` converts them
into the Lua module, so the two can be asked to agree entry for entry.

`text.comments` is the second, owned by `features/comments/styles.lua`, and it
is what `Ctrl+/` reads for the file type it is commenting. Keys are extensions
with their dot, and a field the entry leaves out keeps the native answer, so
naming one field is enough:

```lua
jot.policy.set("text.comments", {
  [".probe"] = { line = "#" },
})
```

`line` is the per-line prefix, `suffix` a per-line closer for a type whose line
comment wraps (`<!--` / `-->`), and `block_open` / `block_close` the type's real
block form, which a multi-line selection wraps in one pair. A file type with no
entry gets the C-family form. `tools/comment_style_import.py` converts
src/syntax/comment_style_data.h into that module the same way as the settings
catalog, and the native table there answers when the module is absent or leaves
a field out. Providers are dropped with the rest of the
runtime on `:reloadplugins`, and the bundled module registers again as the
reload reaches it, so an edit to `reference.lua` is live after a reload and not
only after a restart. `jot.lua.reload()` with no name is the same work.

## Floating Windows

`jot.ui.buffer` and `jot.ui.float` provide native Jot buffer and floating-window
calls. Handles are stable positive integers and scratch buffers are independent
from editor files. Line ranges are zero-based, end-exclusive; `-1` means the
end.

Buffer functions: `jot.ui.buffer.create`, `set_lines`, `get_lines`, and
`delete`. Float functions: `jot.ui.float.open`, `set_lines`, `get_lines`,
`configure`, `get_config`, `close`, `is_valid`, `buffer`, `focus`, `current`,
`on_key`, and `on_mouse`.

```lua
local buf = jot.ui.buffer.create(false, true)
jot.ui.buffer.set_lines(buf, 0, -1, true, {"Build complete", "Press q to close"})
local win = jot.ui.float.open(buf, {
  relative = "cursor", row = 1, col = 2, width = 32, height = 4,
  border = "rounded", title = " Status ", zindex = 100,
  on_key = function(event)
    return event.key == "q" and jot.ui.float.close(event.window)
  end,
})
```

Config supports `relative` (`editor`, `cursor`, `win`, `mouse`), `row`, `col`,
`width`, `height`, `anchor` (`NW`, `NE`, `SW`, `SE`), `border` (`none`,
`single`, `double`, `rounded`, `custom`), `border_chars` (8 cells in
top/right/bottom/left/top-left/top-right/bottom-right/bottom-left order),
`zindex`, `focusable`, `mouse`, `hide`, `style_minimal`, `style`, `fg`, `bg`,
`title`, `footer`, `on_key`, and `on_mouse`. Floats clamp to the paintable
editor area above the status row. `win` uses the active pane; `mouse` is
reserved for future mouse-anchor support.

Convenience calls are available as `jot.ui.float.open(buf, config)`,
`set_lines(win, lines)`, `get_lines(win)`, `configure(win, config)`,
`close(win)`, `on_key(win, callback)`, and `on_mouse(win, callback)`. Callbacks
receive a table and return truthy to consume the event. They are protected,
main-thread only, and released when their window or buffer closes.

A key callback gets `{key=..., ctrl=..., shift=..., alt=..., window=...}` where
`key` is the **byte or codepoint** of the key (so `y` is 121, `Enter` is 13,
`Esc` is 27) and the modifiers are booleans; a mouse callback gets
`{button=..., row=..., col=..., ...}`. `jot.ui.float.set_spans(win, row, spans)`
paints colored runs on a row, where each span is `{start=, len=, fg=}` in bytes
of that row's text.

## The Raw Layer

`jot.screen`, `jot.input` and `jot.term` are the bottom of the stack. Every
surface the editor has (floats, the UI kit, the sidebar, the status line, the
panes) is written on top of them, so a script that needs something the editor
has no concept of builds it here: a canvas, a chart, a game, an overlay, an
input mode of its own. Cells are grid cells (one character each, two for a wide
glyph), 0-based, and clipped to the grid.

```lua
jot.screen.on_paint(function(w, h)                      -- runs on every painted frame
  jot.screen.write(2, 1, "LUA", { fg = "#ff8800", bold = true })
  jot.screen.fill(2, 2, 12, 1, "-", { fg = 8 })
end)

jot.input.on_key(function(event)
  if event.name == "F2" then
    return true                                         -- consumed: nothing else sees it
  end
end)
```

### jot.screen

`jot.screen.size()` returns the grid's `width, height`. `jot.screen.cell(x, y)`
returns one cell as `{text=, fg=, bg=, fg_rgb=, bg_rgb=, bold=, italic=, dim=,
reverse=, underline=, underline_fg=}`, or `nil` when the coordinates are off the
grid. `jot.screen.text(x, y, w)` returns the text of `w` cells from `(x, y)`
(the second half of a wide glyph reads as a space).

`jot.screen.write(x, y, text, opts)` paints `text` from that cell and returns the
number of cells painted. Grapheme clusters stay whole, a wide cluster takes two
cells, a newline ends the run, and text past the right edge or on a row off the
grid is not painted. `jot.screen.fill(x, y, w, h, glyph, opts)` paints one
character over a rect, `glyph` defaulting to a space (a table in that slot is
read as `opts`).

`opts` is the same table everywhere: `fg` and `bg` take an xterm-256 index or an
exact colour as `"#rrggbb"` (whatever the theme and `set_hl` take), and an
absent field means the theme's default foreground or background. `bold`,
`italic`, `dim` and `reverse` are booleans, and `underline` is `0` (none), `1`
(straight) or `2` (wavy) with `underline_fg` for its colour.

`jot.screen.on_paint(fn)` registers the painter. It runs after the editor has
drawn everything it knows about and before the frame is emitted, so its cells
land over the buffer, the sidebar and a picker alike. It runs on every frame
that paints (the home screen and the modals included) and not on an idle frame
that paints nothing. A frame starts by clearing the grid, so cells belong in the
painter rather than somewhere else that writes once. Returning `true` asks for
another frame immediately, which is the animation loop; returning nothing lets
the editor idle again, so a painter that animates returns `true` while it is
moving only. A painter that raises an error is dropped and logged rather than
retried on every frame, and `on_paint(nil)` unregisters it.

`jot.screen.redraw()` queues a repaint, the same request a timer makes
(`jot.timer.set_interval(16, jot.screen.redraw)` is the frame clock for a script
that prefers its own timing over the painter's return value).

### jot.input

`jot.input.on_key(fn)` and `jot.input.on_mouse(fn)` register the raw event
hooks; passing `nil` in place of the function removes one. Both receive a table
and consume the event by returning a truthy value.

A key hook gets `{key=, char=, original_key=, name=, ctrl=, shift=, alt=}`.
`key` is the coded key (a printable key is its own code, `Enter` is 13, `Esc` is
27, the function keys live in a high range), `char` is the character for a
printable key and `nil` otherwise, and `name` is the canonical chord the rest of
the system uses (`"F2"`, `"Ctrl+K"`, `"S"`), which is the field to match on. A
mouse hook gets `{x=, y=, button=, pressed=, released=, motion=, ctrl=, shift=,
alt=}`, with `x`/`y` in grid cells and `button` the raw SGR button code (`0`
left, `1` middle, `2` right, `64`/`65` the vertical wheel, `66`/`67` the
horizontal one, `0x20` set on motion).

The key hook sits above every mode: it is the first thing a key reaches after a
live macro recording has logged it, so it also sees the chords the editor keeps
for itself (a modal prompt's answer, `Ctrl+Q`, text zoom). A hook that consumes
every key is a locked keyboard, so the script owns letting the keys it does not
want through. The mouse hook sits above the floats, so a consumed click or wheel
notch never reaches a pane, a picker or a hit test.

### jot.term

`jot.term.write(bytes)` appends raw bytes to this frame's output, after the cells
and before the caret, which is how a script emits a protocol sequence the cell
model cannot express (a title, an OSC 8 link, a kitty graphics placement). A
sequence that only sets state is safe; one that moves the cursor or paints text
is overwritten by the next frame.

`jot.term.info()` returns `{width=, height=, columns=, rows=, cell_scale=,
truecolor=, cell_pixel_width=, cell_pixel_height=}`: the grid size, the
terminal's own size in cells (they differ only while a text scale is in effect),
whether the terminal takes 24-bit colour, and its cell size in device pixels
(`0` when the terminal does not report one).

## Writing A Feature In Lua

A feature can be files under the config directory instead of code compiled into
the binary. Nothing here needs a rebuild or a restart.

```
~/.config/jot/
  init.lua                     runs first, after config.lua
  config.lua                   settings
  plugins/*.lua                one file per plugin, loaded in name order
  plugins/<name>/plugin.lua    a plugin that is a directory
  lua/<module>.lua             modules for require, not loaded on their own
```

`package.path` is given the config dir, its `lua/` and its `plugins/`
directories (each with `?.lua` and `?/init.lua`), ahead of the built-in path, so
`require("mystuff.util")` resolves out of `~/.config/jot/lua/mystuff/util.lua`
and a user file wins over a bundled one of the same name.

`jot.lua.dir()` returns that config directory and `jot.lua.path()` the search
path in force. `jot.lua.reload(name)` drops `name` and everything loaded under it
from `package.loaded` and requires it again, returning `(true, module)` or
`(false, error)`, which is the loop for iterating on a module:

```lua
local ok, mod = jot.lua.reload("mystuff")
```

`jot.lua.reload()` with no name re-runs `init.lua` and every plugin file, the
same work as `:reloadplugins`. `:reload` also re-applies `config.lua` and the
tree-sitter grammar set.

The pieces to build with are already there: `jot.command` and `jot.keymap` for
entry points, `jot.autocmd` and `jot.events` for reactions, `jot.buffer` and
`jot.pane` for content, `jot.ui.float` (with `jot.ui.handler` for a whole
surface) for boxes, `jot.policy` for a decision the native side would otherwise
keep, `jot.decoration` and `jot.status` for text and the status
line, `jot.timer` and `jot.job` for time and processes, `jot.file` and
`jot.workspace` for the disk, and the raw layer above when none of them fits.
`jot.capabilities()` lists the namespaces this build answers to.
