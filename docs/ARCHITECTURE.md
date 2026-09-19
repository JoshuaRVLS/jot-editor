# Architecture

jot is one C++17 engine with three front ends over it (terminal UI, SDL3/OpenGL
GUI, and Lua plugins). The directories under `src/` are the module boundaries.

```
src/
  apps/jot/        the binary: argument parsing, terminal setup, main()
  jot/             the editor engine
    model/         plain data types (buffer, panes, theme, panels, ...)
    state/         the editor's state, split by subsystem (see below)
    editor/        collaborators: behaviour that owns its state
    editor/api/    fragments of the Editor class body (see below)
    app/           editor behaviour: buffers, files, panes, undo, folding, ...
    event/         the loop, input drains, timers, task queue
    integrations/  LSP, debugger, terminal and tree-sitter wiring
    workspace/     explorer, sidebar, git, right dock, tasks
    surfaces/      menus, popups, home screen, settings, theme chooser
    lua/           the Lua API surface (view structs in lua/view/)
    markdown/      the preview server
  edit/            pure text editing (cursor, lines, search, sort, ...)
  features/        self-contained features (folding, colors, config, syntax)
  input/           key/mouse routing: modes, palette, commands
  render/          painting: frame, buffer, panels, popups, minimap
  tools/           subprocess and protocol clients (LSP, debugger, discord,
                   telescope, integrated terminal, workspace search)
  ui/              the cell grid, text handling, terminal + GUI backends
```

## The editor's state

`src/jot/editor_state.h` is an umbrella: `EditorState` inherits the domain
groups in `src/jot/state/` and adds nothing of its own, so a member is reached
exactly as it was when this was one 500-line struct.

| file | holds |
| --- | --- |
| `state/pane_state.h` | buffers, split tree, tabs, pane/scrollbar drags |
| `state/workspace_state.h` | sidebar, workspace session, git summary and panel |
| `state/panel_state.h` | bottom/right docks, minimap, terminal selection |
| `state/surface_state.h` | palette, pickers, prompts, menus, popup |
| `state/lsp_state.h` | clients, diagnostics, completion/hover/inlay |
| `state/input_state.h` | mouse selection, click/hover, keystroke bookkeeping |
| `state/view_state.h` | layout metrics, theme, paint caches, message line |
| `state/navigation_state.h` | jump history and the armed jump |
| `state/engine_state.h` | config, terminals, debugger, UI, Lua host |

The groups are independent: no group includes another, so a translation unit
that only needs one slice can include it directly.

## Collaborators

State whose behaviour is cohesive enough to own itself lives in
`src/jot/editor/*_controller.h` and is reached through the `Editor` it is built
with (`Editor` declares it a friend). Search and Discord presence are the two
so far; the state and the code that drives it sit in the same file (or, for
Discord, in the corresponding `jot/app/*.cpp`).

## The Editor class body

`src/jot/editor.h` is also an umbrella now: it keeps the class head (type
aliases, constants, the collaborators) and includes one *fragment* per region
of the class body from `src/jot/editor/api/`. A fragment is not a standalone
header -- no include guard, no includes -- it is class-scope text, exactly as if
it were written in `editor.h`. Nothing was renamed or moved to another class,
which is what keeps the ~200 files that call `Editor` unchanged.

Add to a fragment by opening the one whose banner names the subsystem; the map
is at the top of `editor.h`.

## Lua API surface

`src/jot/lua/api.h` keeps the `LuaAPI` class; the view structs it hands to Lua
surface handlers are in `src/jot/lua/view/` (surfaces, plugins, runtime
records, shared helpers).

## Tests and probes

- `test/` -- Catch2 suite (unit + headless engine), run with
  `ctest --test-dir build-tests`.
- `test/*_probe.py` -- real-pty probes that boot the binary and assert on the
  painted screen; `test/pty_screen.py` is the shared harness.
- `benchmarks/` -- the frame and fold-index benchmarks.
