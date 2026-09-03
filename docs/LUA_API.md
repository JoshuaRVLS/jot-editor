# Lua Runtime API

Jot embeds Lua 5.4 and exposes the runtime as the global `jot` table. Scripts
are loaded after the editor, terminal, and initial buffer are ready. No package
import is required.

Lua line and column values are 1-based. Native C++ state remains private.

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
jot.workspace    open, path, recent, execute
jot.symbols      list
jot.config       get, get_number, get_bool, set, unset, has, keys, path
jot.theme        list, apply, current, set_color, palette
jot.diagnostics  get
jot.marks        set, get, jump, del, list
jot.status       register, unregister
```

## Runtime Actions

Action namespaces call the same command registry used by the editor. They are
runtime extension points and do not require recompiling the C++ core.

```text
jot.edit         undo, redo, insert_newline, delete, indent, outdent,
                 comment, duplicate, move_up, move_down, join, uppercase,
                 lowercase, replace, surround, increment, select_all,
                 select_line, search, format
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
```

Every command-backed namespace also provides `execute(command)`. This keeps
new native commands immediately available to Lua while typed bindings grow.

## Native State And Actions

The following functions are real native bridges — they read or act on live
editor state rather than routing through the command registry, so data-heavy
features (status lines, git panels, custom task runners, symbol pickers) can
be written entirely in Lua.

### Config (Lua-first, live-applied)

Configuration is **Lua-first**: `~/.config/jot/config.lua` (next to
`init.lua`) is loaded before `init.lua` and plugins on every startup and on
`:reload`, so the config file itself is Lua:

```lua
-- ~/.config/jot/config.lua
jot.config.set("tab_size", 4)
jot.config.set("show_indent_guides", true)
jot.config.set("color_scheme", "monokai")
jot.config.set("auto_save", true)
jot.config.set("auto_save_interval_ms", 5000)
```

`jot.config.get(key[, default])` returns a string (`nil` when unset and no
default given); `get_number` and `get_bool` return typed values.
`jot.config.set(key, value)` accepts a string, number, or boolean, persists
it immediately, and **live-applies** every setting that maps to editor state
(no restart needed): `tab_size`, `show_indent_guides`,
`relative_line_numbers`, `auto_indent`, `smart_paste_indent`, `auto_save`,
`auto_save_interval_ms`, `render_fps`, `idle_fps`, `lsp_change_debounce_ms`,
`terminal_height`, `debugger_height`, `right_panel_width`,
`image_viewer_backend`, the `treesitter_*` paths, and `color_scheme`
(switches the theme). Keys that are read on every use (`prettier_on_save`,
`clang_format_on_save`, `auto_detect_indent`, `lsp_completion_*`) are live
automatically. `keys()`, `has(key)`, `unset(key)`, and `path()` complete the
surface.

Precedence (later wins): built-in defaults < `configs/settings.conf` (the
runtime-save overlay written by `jot.config.set`) < `config.lua` <
`init.lua`. `config.lua` re-asserts its values on every startup and reload,
so it is the source of truth for the keys it sets.

#### `:reload` / `:reloadconfig`

- `:reload` — re-reads `settings.conf`, re-runs `config.lua`, live-applies
  everything, then reloads Lua plugins (`init.lua` + `plugins/`) and
  tree-sitter policy. One command to pick up any config/plugin change.
- `:reloadconfig` — config only: `settings.conf` + `config.lua` + live
  apply, without touching plugins.
- `:reloadplugins` — existing: plugins only.

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
`{name, kind, detail, line, column}` rows — the same data the Outline panel
and symbol picker use. Build a picker or outline entirely in Lua with it:

```lua
jot.ui.picker("Symbols", function()
  local items = {}
  for _, s in ipairs(jot.symbols.list()) do
    items[#items + 1] = ("%s\t%s (line %d)"):format(s.name, s.kind, s.line)
  end
  return items
end, function(label)
  -- The select callback receives the picked item string; match it back to
  -- the symbol row and jump the cursor there.
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
the cursor), and `modified` for the current file — one call for status-line
and context-aware plugins. `jot.theme.current()` returns the active scheme
name.

### Buffer queries

Every accessor below takes an optional buffer argument (1-based index or
filepath; defaults to the current buffer):

- `jot.buffer.current()` / `jot.buffer.count()` — active buffer index and total
- `jot.buffer.text([buffer])` — full content as one string (`nil` for lazy
  large files)
- `jot.buffer.lines([buffer])` — the content as an array of line strings
  (`nil` for lazy large files)
- `jot.buffer.meta([buffer])` — `path`, `name`, `modified`, `line_count`
- `jot.buffer.selection([buffer])` — `active` plus 1-based `start_line`/
  `start_col`/`end_line`/`end_col` anchors
- `jot.buffer.bookmarks([buffer])` — 1-based bookmark line numbers
- `jot.buffer.folds([buffer])` — `{start, end, collapsed}` rows (1-based)

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
column, end_line, end_col, message, severity, severity_name}]}]}` — useful
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

- `lsp.hover` — `{contents, path, line, column}` (every hover answer)
- `lsp.definition` — `{path, line, column, locations: [...]}`
- `lsp.symbols` — `{path, symbols: [...]}`
- `lsp.completion` — `{path, anchor_line, anchor_col, items: [...]}`
- `git.refreshed` — the full repo snapshot (`root`, `branch`, all counts)
- `diagnostics.changed` — `{path, count, items: [...]}` per publish round
- `buffer.open` / `buffer.save` / `buffer.close` — `{path}`
- `theme.switched` — `{name}`
- `debugger.state_changed` — `{sessions: [{name, adapter, running, stopped,
  active_thread_id, active_frame_id}]}`, emitted (deduped) whenever any
  debugger session's observable state changes
- `config.changed` — `{key, value}` on every `jot.config.set`, or
  `{key, removed=true}` on `unset` — live config reactions without polling

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

- `window` — `width`, `height`, `status_height`, `tab_height`
- `sidebar` / `minimap` / `terminal` / `right_panel` — `visible` plus
  `width`/`height` where applicable
- `pane` — `x`, `y`, `width`, `height`, `buffer` (1-based), `active`
- `buffer` — `line_count`, `first_line`/`last_line` (visible range),
  `visible_lines`, `cursor_line`, `cursor_col`, `scroll_x`

`jot.viewport.line_at(y)` maps an absolute screen row back to the 1-based
buffer line rendered there in the focused pane (or `nil` when it is not
inside the text area).

Control mirrors scroll state: `scroll_top(line)` sets the first visible
line (clamped to the buffer), `scroll_lines(delta)` scrolls by rows,
`scroll_col(col)` sets the horizontal scroll, and `reveal()` scrolls the
cursor into view — all operate on the focused pane's buffer.

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

`jot.debugger.state()` returns the live session list — one entry per
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

- `jot.debugger.request_stack(cb[, session])` — `{session, thread_id,
  frames: [{id, name, path, line, column}]}` (1-based). While a sink waits,
  the native file-jump/cursor-move/scopes chain yields for that one response
  — peek the stack without moving the editor.
- `jot.debugger.request_variables(cb[, session])` — `{session, frame_id,
  variables: [{name, value, type, variables_reference}]}`. Drives the full
  scopes → variables chain (asking the adapter for the first expandable
  scope's contents) and delivers the leaf values.
- `jot.debugger.request_threads(cb[, session])` — `{session, threads:
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

`jot.theme.palette()` returns the current theme's full slot table — every
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
keys use — ideal for Lua macros and custom keymaps: `word_next()`,
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

- `jot.buffer.filetype([buffer])` — the extension string driving syntax
  highlighting (e.g. `".cpp"`); combine with
  `jot.treesitter.language_for_extension` for the language name.
- `jot.buffer.get_line(line[, buffer])` — one 1-based line as a string
  (`nil` when out of range) — lighter than `lines()` for per-line reads.

### Git diff panel

`jot.git.diff(path[, staged])` opens the native right-panel diff for one
file (the same one `:gitdiff` opens) and returns whether it opened.

### File tree

`jot.filetree.root()` returns the workspace root. `jot.filetree.tree()`
returns the exact tree the explorer sidebar renders (`{name, path, is_dir,
expanded, depth, children: [...]}` per node, nested) — same native data, no
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
`{line, column, len}` (1-based) — enough to build custom match lists, mini-maps,
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
severity}` rows. `accept()` runs the selected item and `close()` dismisses
the picker — useful for custom keymaps over native pickers.

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
is already open), optionally pre-filling the query — the same entry point as
`Ctrl+P`, so a Lua keymap can hand off to command completion. The palette is
a centered floating panel with a result counter, highlighted input row,
accent-bar selection, and an empty state; `Tab` completes, `Esc` closes, and
`Up`/`Down`/`PageUp`/`PageDown`/`Home`/`End` move the selection. Query
characters that matched a result are emphasized (bold, accent color) inside
each label. Closing the palette with `Esc` (or toggling it off) remembers
the input text, so the next `Ctrl+P` resumes the search where it was left;
closing with an empty input clears the memory.

The quick-pick panels share the same visual language: bordered panel with
result counter, divider under the input, accent-bar selection, and matched
query text highlighted inside labels (`PageUp`/`PageDown`/`Home`/`End` page
and jump the selection).

All floating modals — the command palette, quick picks, the tree-sitter
status modal, popups, and the LSP manager — use one design language driven
by theme slots: the editor behind the dialog is dimmed (faint attribute),
and the panel surface, border, and title row all come from the theme's
`bg_panel_border` slot, so switching colorschemes restyles every dialog at
once.

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

`lua/treesitter/registry.lua` is the sole language registry. Each entry contains
`name`, `extensions`, `aliases`, `url`, `source_subdir`, `symbol`,
`library_names`, and `query_file`. Queries live at
`lua/treesitter/queries/<language>/highlights.scm`, one directory per language.
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
caches together.

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
dropped safely.

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

## Native Boundary

Lua owns runtime composition, commands, actions, and event policy. C++ owns
terminal rendering, raw input, event-loop scheduling, buffer storage, process
transports, LSP/DAP protocol handling, Tree-sitter parser ownership, and
platform integration. Lua receives typed wrappers and never receives native
pointers.

Tree-sitter parser, tree, and query values are positive opaque integer handles.
Delete them with `delete_parser`, `delete_tree`, and `delete_query`. Handles
become invalid after `reload`; native functions return Lua errors for invalid
handles. The bundled runtime loads before user plugins and compiled catalog
data remains the fallback when no Lua override is present.

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
