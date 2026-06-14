# jot

`jot` is a modern terminal code editor written in C++17. It is built around a
modeless editing workflow: type directly, select with the keyboard or mouse,
keep multiple files open, split panes, browse a workspace tree, search across a
project, run terminal tasks, debug programs, and use native LSP features without
leaving the terminal.

![jot editor screenshot](assets/screenshot.png)

The installed binary name is `jot`.

## Highlights

- Modeless editor UX with mouse support, selections, tabs, split panes, minimap,
  status bar, command palette, and a workspace sidebar.
- Native C++ core for buffers, panes, syntax, LSP, debugging, Git, terminal
  emulation, and workspace state.
- Tree-sitter syntax highlighting with runtime grammar installation, status,
  reload, fallback queries, and richer theme slots for semantic captures.
- Native LSP support for diagnostics, completion, hover, definition jumps,
  document symbols, server lifecycle, and lightweight server install/remove
  helpers.
- Integrated terminal panel with multiple shell tabs plus local/global task
  runner support.
- GDB/LLDB Debug Adapter Protocol panel with launch/config/attach flows,
  breakpoints, threads, stack, variables, memory, disassembly, and output.
- Git workflow commands for status, diffs, staging, unstaging, committing, log,
  blame, and refresh.
- Python-backed colorschemes only; editor behavior is owned by the C++ core.

## Platform Support

Officially supported platforms:

- Linux x86_64 / arm64
- macOS Intel / Apple Silicon

Notes:

- `jot` relies on POSIX terminal APIs (`termios`, `poll`, PTY/forkpty) for the
  editor UI and integrated terminal.
- Linux and other non-Apple Unix builds link `libutil` for PTY support.
- macOS uses native system PTY APIs without a `libutil` link.
- Windows is not supported yet and is blocked at CMake configure time.

## Install

For a user-local install:

```bash
./install.sh
```

By default, the installer configures, builds, and installs to `$HOME/.local`.
It also attempts Tree-sitter runtime setup unless `--skip-treesitter` is passed.
Use `./install.sh --help` for prefix, build type, test, Tree-sitter, formatter,
and LSP tooling options.

Manual CMake install:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cmake --install build --prefix "$HOME/.local"
```

Installed files include:

- `$prefix/bin/jot`
- `$prefix/share/jot/python/`
- `$prefix/share/jot/configs/`

## Run

```bash
jot
jot path/to/file.cpp
jot path/to/project
```

Launch behavior:

- no argument: resume the most recent valid workspace session, or show the home
  menu when no session exists
- file argument: open that file in an editor session
- directory argument: change into that directory, use it as the workspace root,
  load the file tree, open the sidebar, and restore that workspace session

Workspace sessions are stored under:

```text
~/.config/jot/workspaces/
```

## Feature Set

### Editing

- Modeless text entry with direct typing by default.
- Undo/redo, copy/cut/paste, select all, mouse selection, double-click word
  selection, and triple-click line selection.
- Multi-line paste with optional smart indentation rebased to the cursor
  context.
- Auto-indent, optional indent detection, tab insertion, selection indent, and
  selection outdent.
- Auto-closing brackets, bracket matching, bracket jump, rainbow bracket colors,
  and active bracket guide rendering.
- Duplicate/delete current line, move current line or selection up/down, join
  lines, trim trailing whitespace, and trim blank lines in a selection.
- Uppercase/lowercase transforms for the selection or current word.
- Sort, sort descending, reverse, deduplicate, and shuffle selected lines.
- Replace commands for case-sensitive, case-insensitive, whole-word, and regex
  replacement.
- Surround/unsurround selection or current word.
- Increment/decrement number at cursor.
- Format document command.
- Word-wise deletion, smart line-start movement, file start/end movement, and
  page movement.
- Buffer statistics and current date/time insertion.

### Buffers, Tabs, And Panes

- Multiple open buffers, including saved files and unsaved buffers.
- Pane-local file tabs with click-to-switch and close buttons.
- Reopen last closed tab.
- Split panes left/right/up/down.
- Directional pane focus and pane resize by keyboard or mouse drag.
- Close current pane without necessarily closing the underlying buffer.
- Open, save, save-as, save-and-quit, close buffer, quit, and force quit
  commands.
- Autosave on/off/toggle/status plus configurable autosave interval.

### Workspace And Sidebar

- Workspace root from the current directory or a launched folder.
- Sidebar file explorer with folders before files, hidden-file toggle, expand
  all, collapse all, refresh, and parent-folder workspace navigation.
- Keyboard and mouse navigation for the sidebar.
- Create file, create folder, rename, and delete file/folder workflows.
- Recent files and recent workspace resume.
- Home menu with recent entries, new file, command palette, theme chooser, and
  recent prompt entry points.
- C++ helper commands for creating matching header/source pairs and generating
  missing source implementations from declarations.

### Search And Navigation

- Buffer-local search panel with next/previous match navigation.
- Search options for case sensitivity, whole-word matching, and regex mode.
- Replace panel with replace current and replace all actions.
- Selection-scoped replace from `Ctrl+Shift+F`, with replacements limited to
  the highlighted code.
- Go to line or line:column.
- Bookmarks.
- Telescope file finder.
- Project-wide text search picker.
- Diagnostics picker and next/previous diagnostic navigation.
- Document symbol/outline picker using LSP symbols when available and a regex
  fallback for supported buffers.

### Syntax Highlighting And Folding

- Tree-sitter parser-based highlighting when the runtime and grammar are
  available.
- Built-in regex fallback highlighting for common file types.
- Runtime Tree-sitter commands to install grammars, inspect status, and reload
  parser/query caches.
- Built-in and minimal query fallback paths for C++ so highlighting remains
  Tree-sitter-backed when runtime queries are incompatible.
- Rich Tree-sitter capture mapping for variables, parameters, fields, constants,
  builtins, operators, punctuation, control/storage/preprocessor keywords,
  methods, constructors, builtin types, macro constants, string escapes, tags,
  attributes, namespaces, and modules.
- Theme slots for both classic syntax colors and richer Tree-sitter captures.
- Code folding with gutter indicators, toggle/collapse/expand commands, fold
  all, unfold all, mouse toggling, and persisted collapsed ranges.

Built-in fallback syntax rules cover common file types including:

- C / C++: `.c`, `.cpp`, `.h`, `.hpp`
- Python: `.py`
- JavaScript / JSX / TypeScript / TSX: `.js`, `.jsx`, `.mjs`, `.cjs`,
  `.ts`, `.tsx`, `.mts`, `.cts`
- HTML / XML: `.html`, `.xml`
- Rust: `.rs`
- CSS: `.css`
- Java: `.java`
- Go: `.go`
- Markdown: `.md`
- JSON: `.json`
- Shell: `.sh`, `.bash`, `.zsh`
- Ruby: `.rb`
- PHP: `.php`

### LSP

- Native C++ LSP client ownership; Python is not used to drive editor behavior.
- One LSP client per language and workspace root.
- File open/change/save notifications with debounced document sync.
- Diagnostics overlay, diagnostics picker, and next/previous diagnostic jumps.
- Completion dropdown with fuzzy filtering, `textEdit` support, and plain-text
  snippet degradation.
- Manual and trigger-character completion requests.
- Hover popup by command or debounced mouse hover.
- Go to definition, preview/cross-file open, Ctrl-click definition requests when
  terminal mouse modifiers are available, and `:lspback` return stack.
- Document symbols from LSP with regex fallback.
- LSP status, start, stop, restart, manager, install, and remove commands.

Default language server commands:

- Python: `pylsp`
- JavaScript / JSX / TypeScript / TSX:
  `typescript-language-server --stdio`
- HTML: `vscode-html-language-server --stdio`
- C / C++: `clangd`

The LSP manager/install/remove helpers expose language entries for Python,
JavaScript/JSX/TypeScript/TSX, HTML, C++, Rust, Go, Lua, and Bash.

### Integrated Terminal And Tasks

- Bottom terminal panel backed by PTY and libvterm.
- Native shell launch using `$SHELL` when available, with fallback shell paths.
- Multiple terminal tabs.
- Mouse terminal tab switching, closing, and `+` tab creation.
- Terminal instances remain alive while hidden.
- `Esc` releases terminal focus back to the editor.
- Local and global task files:

```text
<workspace>/.jot/tasks.json
~/.config/jot/configs/tasks.json
```

Task schema:

```json
{ "tasks": { "build": "cmake --build build -j" } }
```

Local tasks override global tasks with the same name. `:task` lists tasks,
`:task <name>` runs or reuses a task tab, `:tasknew <name>` starts a fresh task
tab, and `:taskrerun` reruns the last task.

### Debugger

- Native Debug Adapter Protocol client integration.
- GDB and LLDB launch commands.
- `debug.json` configured sessions from the workspace or global config.
- Attach-to-PID command.
- Toggle breakpoints by clicking the editor gutter marker column.
- Continue, pause, restart, stop, step into, step over, and step out commands.
- Threads, stack, variables, memory, disassembly, breakpoints, and output views
  in the debugger panel.

Debug config locations:

```text
<workspace>/.jot/debug.json
~/.config/jot/configs/debug.json
```

Debug config shape:

```json
{
  "sessions": {
    "app": {
      "adapter": "gdb",
      "program": "./build/app",
      "args": [],
      "cwd": ".",
      "env": {}
    }
  }
}
```

### Git

- Git status summary in the editor state/status surfaces.
- Popup commands for `status`, unstaged diff, staged diff, recent log, and blame
  for the current line.
- Stage/unstage current or specified file.
- Stage all and unstage all.
- Commit staged changes with an explicit message.
- Refresh repository state.

Git operations are local only; push, pull, fetch, reset, checkout, and discard
are intentionally not exposed as editor commands.

### Themes

- Embedded Python runtime for colorscheme discovery and application.
- Bundled and user theme directories.
- `vim` compatibility alias for existing theme files.
- Neovim-style highlight group mapping for classic groups and Tree-sitter
  captures.
- Theme chooser, `:theme`, and `:colorscheme` commands.

User theme paths:

```text
~/.config/jot/configs/colors/
~/.config/jot/themes/
```

See [docs/THEMES.md](docs/THEMES.md) for theme authoring.

### UI And Mouse

- Top application/menu chrome, pane file tabs, editor panes, optional sidebar,
  optional minimap, optional right-side tool dock, bottom terminal/debugger
  panels, and two-row status/message area.
- Mouse click to place cursor, drag to select, edge auto-scroll while dragging,
  double-click word selection, and triple-click line selection.
- Mouse tab switching and close buttons.
- Mouse split resizing.
- Mouse minimap viewport jump.
- Mouse sidebar navigation.
- Mouse debugger breakpoint toggles.
- Context menus and menu-bar actions route to existing editor commands.

## Keybindings

### Core Editing

- `Ctrl+Q`: close pane or quit, prompting when unsaved buffers exist
- `Ctrl+S`: save current file
- `Ctrl+Shift+S`: save all modified saved files
- `Ctrl+Z` / `Ctrl+Y`: undo / redo
- `Ctrl+A`: select all
- `Ctrl+C` / `Ctrl+X` / `Ctrl+V`: copy / cut / paste
- `Ctrl+D`: duplicate current line
- `Ctrl+K`: delete current line
- `Ctrl+/`: toggle comment
- `Ctrl+Backspace`: delete previous word
- `Ctrl+Delete`: delete next word
- `Ctrl+Space`: request LSP completion
- `Ctrl+Shift+L`: select current line
- `Ctrl+Shift+U`: uppercase selection or word
- `Ctrl+Shift+N`: lowercase selection or word

### Navigation

- `Arrow keys`: move cursor
- `Shift+Arrow keys`: extend selection
- `Home` / `End`: smart line start / line end
- `Page Up` / `Page Down`: move by 10 lines
- `Alt+I` / `Alt+A`: smart line start / line end
- `Alt+G` / `Alt+Shift+G`: file start / file end
- `Alt+H` / `Alt+L`: move by word
- `Alt+Up` / `Alt+Down`: move current line or selection up/down
- `Tab`: indent selection or insert indentation
- `Shift+Tab`: outdent selection
- `Esc`: clear selection or release/close the active floating surface

### Buffers, Tabs, Panes, And Tools

- `Ctrl+Tab` / `Ctrl+Shift+Tab`: next / previous pane-local tab
- `Alt+,` / `Alt+.`: previous / next pane-local tab
- `Alt+1..9` / `Alt+0`: switch to tab 1..9 / last tab
- `Alt+W`: close current file tab
- `Alt+N`: new buffer
- `Alt+S`: save
- `Ctrl+B` or `Alt+B`: toggle sidebar
- `Ctrl+E`: Telescope file finder
- `Ctrl+F` or `Alt+F`: search panel
- `Ctrl+G`: go-to-line prompt
- `Ctrl+P` or `Alt+P`: command palette
- `Ctrl+R`: recent-file prompt
- `Ctrl+Shift+T`: reopen last closed tab
- `Ctrl+Shift+F`: replace inside selected text, or project-wide search when no
  text is selected
- `Ctrl+Shift+M`: diagnostics picker
- `Ctrl+Shift+O`: document symbols
- `Ctrl+M` or `Alt+M`: toggle minimap
- `Ctrl+T` or `Alt+T`: theme chooser
- `Ctrl+\`` / `Ctrl+X`: open, focus, or hide terminal panel

### Pane Layout

- `Ctrl+Alt+H/J/K/L`: split left/down/up/right
- `Ctrl+Alt+Arrow`: focus pane in that direction
- `Ctrl+Alt+Q`: close current pane
- `Ctrl+Shift+H/J/K/L`: resize pane
- `Ctrl+Arrow`: resize pane
- `Alt+H/J/K/L`: resize pane

### Search Panel

- `Enter`, `Arrow Down`, or `Ctrl+F`: next match
- `Arrow Up`: previous match
- `Tab`: toggle case-sensitive search, or switch fields when replace is open
- `Ctrl+H`: show/hide replace field
- `Ctrl+R`: replace current match
- `Ctrl+Shift+R`: replace all matches
- `Ctrl+W`: toggle whole-word search
- `Ctrl+E`: toggle regex search
- `Esc`: close search

### Sidebar Explorer

- `Arrow Up` / `k`: move up
- `Arrow Down` / `j`: move down
- `Page Up` / `Page Down`: fast scroll
- `Home` / `End`: first / last node
- `Arrow Right` / `l` / `Enter`: expand folder or open file
- `Arrow Left` / `h`: collapse folder or move to parent node
- `r`: refresh tree
- `a`: create file in selected folder
- `A`: create folder in selected folder
- `i`: generate missing C++ implementations
- `C`: create matching C++ header/source pair
- `d`, then `d` again: delete selected file or folder
- `.`: show/hide dotfiles
- `*`: expand all recursively
- `z`: collapse all
- `Backspace`: open parent folder as workspace root
- `Esc`: return focus to editor

### Quick Pick And Completion

- Quick pick: type to filter, `Up/Down` to move, `Home/End` to jump,
  `Enter` to accept, `Backspace` to edit, `Esc` to close
- LSP completion: `Up/Down` to select, `Enter` or `Tab` to apply, `Esc` to close

### Integrated Terminal

- `Ctrl+\`` / `Ctrl+X`: show, hide, or focus terminal panel
- `Ctrl+Shift+T`: create a new terminal tab while terminal focus is active
- `Esc`: release terminal focus
- Mouse click terminal tab: focus tab
- Mouse click `+`: create tab
- Mouse click tab close button: close tab

## Command Reference

Open the command palette with `Ctrl+P` and run ex-style commands.

### File, Session, And Buffers

- `:q`, `:quit`, `:q!`, `:quit!`
- `:w`, `:write`, `:wq`, `:x`, `:xit`
- `:e <file>`, `:edit <file>`, `:open <file>`
- `:new`, `:enew`
- `:bd`, `:bdelete`, `:close`
- `:home`, `:resume`
- `:recent`, `:openrecent [index|query]`
- `:reopen`, `:reopenlast`
- `:autosave [on|off|toggle|status|<ms>]`

### Panes And UI

- `:sp`, `:split`, `:splith`
- `:vsp`, `:splitv`
- `:splitleft`, `:splitright`, `:splitup`, `:splitdown`
- `:spleft`, `:spright`, `:spup`, `:spdown`
- `:bn`, `:nextpane`, `:bp`, `:prevpane`
- `:focusleft`, `:focusright`, `:focusup`, `:focusdown`
- `:wincmd h|j|k|l`
- `:resizeleft`, `:resizeright`, `:resizeup`, `:resizedown`
- `:minimap`
- `:theme <name>`, `:colorscheme <name>`, `:colo <name>`
- `:help [topic]`, `:h [topic]`

### Workspace

- `:find [dir]`, `:ff [dir]`
- `:mkfile <path>`
- `:mkdir <path>`
- `:rename <old_path> <new_path>`
- `:rm <path>`
- `:cpppair <path>`
- `:cppimpl [header-or-source]`

### Search, Navigation, And Editing

- `:search`
- `:grep <text>`, `:projectsearch <text>`, `:searchall <text>`
- `:diagnostics`, `:problems`
- `:diagnext`, `:diagnosticnext`, `:diagprev`
- `:symbols`, `:outline`
- `:line <line>[:col]`, `:goto <line>[:col]`
- `:format`, `:trim`, `:trimblank`
- `:upper`, `:lower`
- `:sortlines`, `:sortdesc`, `:reverselines`, `:uniquelines`,
  `:shufflelines`, `:joinlines`
- `:dupe`
- `:replace <from> <to>`, `:replacei <from> <to>`,
  `:replaceword <from> <to>`, `:replacere <pattern> <replacement>`
- `:surround <left> [right]`, `:unsurround`
- `:fold`, `:collapse`, `:unfold`, `:expand`, `:togglefold`, `:foldall`,
  `:unfoldall`
- `:incnum`, `:decnum`
- `:copypath`, `:copyname`
- `:datetime`, `:stats`

### LSP

- `:lspstart`, `:lspstatus`, `:lspstop`, `:lsprestart`
- `:lspmanager`
- `:lspinstall <python|typescript|javascript|jsx|tsx|cpp|rust|go|lua|bash|html>`
- `:lspremove <python|typescript|javascript|jsx|tsx|cpp|rust|go|lua|bash|html>`
- `:hover`, `:lsphover`
- `:definition`, `:lspdefinition`, `:lspdef`, `:gd`
- `:lspback`

### Tree-sitter

- `:tsinstall <language>`, `:treesitterinstall <language>`
- JavaScript / JSX: `:tsinstall javascript` or `:tsinstall jsx`
- TypeScript / TSX: `:tsinstall typescript` and `:tsinstall tsx`
- `:tsstatus`
- `:tsreload`, `:treesitterreload`

### Terminal And Tasks

- `:term`, `:terminal`
- `:termnew`, `:terminalnew`
- `:task [name]`
- `:tasknew <name>`
- `:taskrerun`

### Debugger

- `:debug <program> [args...]`
- `:debuggdb <program> [args...]`
- `:debuglldb <program> [args...]`
- `:debugconfig [name]`
- `:debugattach <pid>`
- `:debugpanel`
- `:debugstop`, `:debugrestart`
- `:debugcontinue`, `:debugpause`
- `:debugstep`, `:debugnext`, `:debugout`
- `:debugthreads`
- `:debugmemory <expr|addr>`
- `:debugdisasm [expr|addr]`

### Git

- `:gitstatus`
- `:gitdiff [file]`
- `:gitdiffstaged [file]`
- `:gitstage [file]`
- `:gitunstage [file]`
- `:gitstageall`
- `:gitunstageall`
- `:gitcommit <message>`
- `:gitlog`
- `:gitblame`
- `:gitrefresh`

## Configuration

User config lives in:

```text
~/.config/jot/
```

Current layout:

```text
~/.config/jot/
  configs/
    settings.conf
    colors/
      my_theme.py
  themes/      # legacy colorscheme path
```

Bundled starter config in this repo:

```text
.configs/configs/
```

Built-in defaults include:

- `explorer_width=25`
- `minimap_width=15`
- `show_explorer=true`
- `show_minimap=true`
- `tab_size=2`
- `auto_indent=true`
- `smart_paste_indent=true`
- `auto_save=false`
- `auto_save_interval_ms=2000`
- `auto_detect_indent=false`
- `show_line_numbers=true`
- `word_wrap=false`
- `cursor_style=block`
- `render_fps=120`
- `idle_fps=60`
- `lsp_change_debounce_ms=120`
- `terminal_height=10`
- `debugger_height=12`

Example `settings.conf`:

```ini
tab_size=2
auto_indent=true
smart_paste_indent=true
auto_save=false
auto_save_interval_ms=2000
auto_detect_indent=true
render_fps=120
idle_fps=60
terminal_height=12
debugger_height=12
minimap_width=18
explorer_width=30
lsp_change_debounce_ms=120
```

## Build Requirements

- CMake 3.14+
- C++17 compiler
- Python 3 development headers and `python3-config`
- libvterm development headers (`vterm` pkg-config package)
- libtermkey development headers (`termkey` pkg-config package)
- libuv development headers (`libuv` pkg-config package)
- Unix-like environment with POSIX terminal APIs

Notes:

- The UI uses raw terminal handling, not ncurses.
- Editor keyboard input is decoded with libtermkey for reliable modifier and
  advanced shortcut handling.
- The integrated terminal uses PTY support and libvterm.
- Async editor I/O, timers, child process pipes, and file-tree notifications use
  libuv.
- Tree-sitter runtime support is optional at build time but recommended.

## Project Layout

```text
apps/jot/        CLI entrypoint and executable target
cmake/           reusable CMake modules
include/jot/     public C++ API headers
src/core/        editor state, buffers, panes, workspace, LSP, debugger, terminal
src/edit/        text editing, cursor movement, selection, clipboard, search
src/features/    syntax, folding, config, bracket helpers, C++ assist
src/input/       keyboard, mouse, command palette, command dispatch
src/render/      buffer drawing, minimap, overlays, panels, UI views
src/tools/       integrated terminal, DAP client, LSP client, search helpers
src/python/      Python-side theme runtime
src/python_bridge/
                  C++ bridge for theme-facing Python API
src/ui/          raw terminal and UI abstraction
docs/            user-facing documentation
tests/           unit test scaffolding
```

Build graph highlights:

- `jot_engine`: aggregated static engine target for the app
- `jot_core`, `jot_edit`, `jot_features`, `jot_input`, `jot_render`,
  `jot_tools`, `jot_python_bridge`, `jot_ui`: module libraries for ownership
  boundaries and reuse

## Notes And Limitations

- The user-facing workflow is modeless; legacy mode-related source files are
  internal compatibility details.
- The integrated terminal is useful for normal shell/task workflows, but it is
  not intended to be a complete replacement for a mature standalone terminal
  emulator.
- Python is reserved for themes. Plugins, keybindings, editor commands, and
  behavior hooks are not loaded from Python.
- Windows is not supported yet.

## License

MIT
