# Architecture

jot is one C++17 engine with three front ends over it (terminal UI, SDL3/OpenGL
GUI, and Lua plugins). The directories under `src/` are the module boundaries.

```
apps/jot/        the binary: argument parsing, terminal setup, main()
src/
  base/            primitives that depend on nothing above them: string and
                   path helpers, atomic file writes, shell quoting, link
                   detection, the event loop, the task queue, process jobs
  jot/             the editor engine
    model/         plain data types (buffer, panes, theme, panels, ...)
    state/         the editor's state, split by subsystem (see below)
    editor/        collaborators: behaviour that owns its state
    editor/api/    fragments of the Editor class body (see below)
    app/           editor behaviour: buffers, files, panes, undo, folding, ...
    event/         input drains and timers
    integrations/  LSP, debugger, terminal and tree-sitter wiring
    workspace/     explorer, sidebar, git, right dock, tasks
    surfaces/      menus, popups, home screen, settings, theme chooser
    lua/           the Lua API surface (view structs in lua/view/)
    markdown/      the preview server (shared by the markdown and HTML previews)
  text/            pure text editing: cursor, lines, search, sort, the text
                   objects, the line providers, the insertion rules
  syntax/          language detection, highlighting, folding, the tree-sitter
                   runtime, the C++ and colour scanners, the symbol index
  features/        self-contained features (config, emmet, wakatime, http,
                   leetcode, previews)
  input/           key/mouse routing: modes, palette, commands
  render/          painting: frame, buffer, panels, popups, minimap
  dap/             the Debug Adapter Protocol client and its wire format
  terminal/        pty-backed terminal sessions and the panel that draws them
  tools/           subprocess and protocol clients (LSP, discord, telescope,
                   image viewer, workspace search)
  ui/              the cell grid, text handling, terminal + GUI backends
```

The module order, lowest first: `base`, `text`, `syntax`, `ui`, `render`,
`input`, `workspace`, `lsp`, `dap`, `terminal`, `git`, `extras`, `editor`,
`lua`. A lower module may not include a higher one.

`base/` is the floor: it may not include `editor.h`, a module header or a UI
header, and it is the first directory the module order fixes. Everything above
it may include it. Its include prefix is `base/` (`#include "base/string_util.h"`).
`text/` and `syntax/` are included with their directory name when the caller is
outside them (`#include "syntax/folding.h"`); inside a module a bare basename is
enough, because the module's own directory is on the include path.

## The editor's state

`src/jot/editor_state.h` is an umbrella: `EditorState` inherits the domain
groups in `src/jot/state/` and adds nothing of its own, so a member is reached
exactly as it was when this was one 500-line struct.

| file | holds |
| --- | --- |
| `state/pane_state.h` | buffers, split tree, tabs, pane tab strips/drags, scrollbar drags |
| `state/workspace_state.h` | sidebar, workspace session, git summary and panel |
| `state/panel_state.h` | bottom/right docks, minimap, terminal selection |
| `state/surface_state.h` | palette, pickers, prompts, menus, popup |
| `state/lsp_state.h` | clients, diagnostics, completion/hover/inlay |
| `state/input_state.h` | mouse selection, click/hover, keystroke bookkeeping |
| `state/view_state.h` | layout metrics, theme, paint caches, message line |
| `state/navigation_state.h` | jump history and the armed jump |
| `state/engine_state.h` | config, terminals, debugger, UI, Lua host |
| `state/web_index_state.h` | the workspace's CSS vocabulary and its scan's bookkeeping |

The groups are independent: no group includes another, so a translation unit
that only needs one slice can include it directly.

`PreviewServer` (`jot/markdown/preview_server.*`) is one HTTP/SSE server with two
modes, chosen by whether a file root was set: a *stored page* (the markdown
preview renders a document and serves it at `/`) or a *served tree* (the HTML
preview points the browser at a real path, injects a reload client into HTML
responses, and can override one document with a buffer's text). Both modes are
driven entirely from Lua over `jot.preview.*`; the server holds no policy.

Two of the groups hold the state around a workspace-wide scan that runs on the
worker queue: `cpp_defs_state.h` (declarations with no body, and signatures with
two) and `web_index_state.h` (the class names and custom properties the tree
declares). Both follow the same shape -- a running flag, a pending flag, and the
root plus an epoch that decide whether a landing result is still wanted -- and
both have their scan spelled out as a pure function in `src/features/` and their
editor half in `src/jot/app/` (`cpp_definitions.cpp`, `web_index.cpp`).

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

## Modal panels

A modal is one shape, whichever surface it is: the frame paints as usual, a
scrim (`UI::dim_rect`) covers the whole grid, and the panel paints over it --
natively, or as a Lua float in the pass that follows. `Editor::render_prompt_modal`
is the shared entry point for the save / rename / quit prompts (called from the
frame's tail and from the home screen's early return, which can raise the same
panel); the pickers take the same two steps one at a time.

Adding one is a multi-place change because the surface's *name* is what ties the
scrim to the panel:

- `is_modal_surface` / `modal_surface_open` in `jot/lua/api_float.cpp`: the
  float opens on `kModalFloatZindex` (above the chrome floats, which are
  recreated every frame and would otherwise out-rank it by creation order) and
  the background floats lose their input.
- The `modal_surface_open` lambda inside `LuaAPI::render_float_layer`, which is
  the same predicate again for the re-dim: floats repaint their own rectangles
  with `dim = false`, so without it the sidebar and status line would wipe the
  scrim over themselves every frame.
- `UIGui::paint_float_overlays`'s `is_modal_float` in `ui/gui/gui_render.cpp`:
  the GUI draws the scrim as a quad between two float layers, so its copy of the
  set decides what stays bright.
- The native input paths do not consult the set (they gate per surface): a
  prompt that should own the pointer while it is up says so in
  `input/mouse/dispatcher.cpp` and `input/mouse/panels.cpp`.

## Lua API surface

`src/jot/lua/api.h` keeps the `LuaAPI` class; the view structs it hands to Lua
surface handlers are in `src/jot/lua/view/` (surfaces, plugins, runtime
records, shared helpers).

## Tests and probes

- `test/` -- Catch2 suite (unit + headless engine), run with
  `ctest --test-dir build-tests`.
- `test/*_probe.py` -- real-pty probes that boot the binary and assert on the
  painted screen; `test/pty_screen.py` is the shared harness. `test/gui_screen.py`
  is the same for the GUI probes (`gui_scroll_probe.py`, `gui_ram_probe.py`): it
  runs the binary on a private Xvfb display, injects input with XTEST and reads
  the window back.
- `benchmarks/` -- the frame and fold-index benchmarks.
- `tools/leak_check.sh [build-dir] [filter]` -- builds the `JOT_SANITIZE=ON`
  tree and runs the suite under AddressSanitizer + LeakSanitizer, failing if
  anything leaks. It runs the suite as one process, not through ctest: leaks
  are reported at process exit, so a per-case run would hide them. The
  sanitizer tree is unoptimised, which trips the clock-sensitive
  "A slower period blinks more slowly" case; the script checks the leak
  report, not the suite's exit status.

## Owning native resources

Two ownership rules that no compiler enforces, and that the leak check above
is what verifies:

- A `FileBuffer` owns its tree-sitter tree and parser. They are raw pointers,
  so every path that drops a buffer -- `close_buffer`, a workspace switch, and
  `~Editor` -- has to call `FileBuffer::release_syntax()`. Clearing the buffer
  vector (or letting `~Editor` run) without it leaks the whole tree of every
  open file, which is where session-sized leaks came from.
- `EventLoop::~EventLoop` closes its handles and then has to pump the loop
  once more: `stop()` leaves libuv's stop flag set, so the close callbacks
  `close_all_handles()` queues never run, `uv_loop_close()` fails with EBUSY,
  and libuv's allocations plus every handle wrapper are left behind. The pump
  is non-blocking (`UV_RUN_NOWAIT`) because other code, such as the markdown
  preview server, puts its own handles on this loop.
