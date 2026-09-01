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
jot.editor       execute, request_redraw
jot.buffer       get_text, set_text, get_selection, replace_selection,
                 insert, cursor, set_cursor, list, switch
jot.cursor       get, set, select_all, select_line
jot.file         current_file, open, save, save_buffer, close, new,
                 open_workspace
jot.pane         layout, list, split_horizontal, split_vertical,
                 focus_next, focus_previous, resize
jot.workspace    open, execute
jot.config       execute
```

## Runtime Actions

Action namespaces call the same command registry used by the editor. They are
runtime extension points and do not require recompiling the C++ core.

```text
jot.edit         undo, redo, insert_newline, delete, indent, outdent,
                 comment, duplicate, move_up, move_down, join, uppercase,
                 lowercase, replace, surround, increment, select_all,
                 select_line, search, format
jot.search       execute, next, previous
jot.folds        execute, toggle, fold, unfold, all
jot.bookmarks    execute, toggle, next, previous
jot.terminal     execute, toggle, new
jot.tasks        execute, list, run
jot.lsp          execute, definition, back, completion
jot.debugger     execute, continue, pause, step_in, step_over, step_out, stop
jot.git          execute, refresh, stage_all
jot.treesitter   execute, status, reload
jot.image        execute, open
```

Every command-backed namespace also provides `execute(command)`. This keeps
new native commands immediately available to Lua while typed bindings grow.

## UI And Events

```lua
jot.ui.show_message("ready")
jot.ui.request_redraw()
jot.autocmd("CursorMoved", function(event)
  jot.ui.show_message(event.path .. ":" .. event.line)
end)
```

Events currently include `BufOpen`, `BufChange`, `BufSave`, `BufClose`,
`CursorMoved`, `WorkspaceEnter`, `UIResize`, `DiagnosticChanged`, and
`Shutdown`. Event callbacks receive a table containing `event`, `path`, and
available buffer, line, and column fields.

## Introspection

```lua
print(jot.api_version)
local capabilities = jot.capabilities()
```

`jot.capabilities()` returns the available runtime namespaces. Use it when a
script must work across different Jot builds.

## Native Boundary

Lua owns runtime composition, commands, actions, and event policy. C++ owns
terminal rendering, raw input, event-loop scheduling, buffer storage, process
transports, LSP/DAP protocol handling, Tree-sitter parser ownership, and
platform integration. Lua receives typed wrappers and never receives native
pointers.
