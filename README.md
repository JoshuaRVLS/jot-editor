# jot

`jot` is a terminal code editor written in C++. It aims for a modern, modeless workflow: type directly, use the mouse if you want, keep multiple files open, split panes, browse a workspace tree, open a built-in terminal, and extend the editor with Python.

![jot screenshot](screenshot.png)

## What It Is

`jot` is its own editor, focused on a modern terminal IDE workflow.

The current editing model is:

- direct text entry by default
- selections with `Shift+Arrow` or the mouse
- command palette for editor commands
- tabs for buffers
- panes for layout
- sidebar for workspace browsing
- integrated terminal for shell work
- native C++ LSP support for diagnostics
- Python for plugins, themes, and optional automation

The installed binary name is `jot`.
A compatibility alias named `jcode` is also installed.

## How `jot` Works

### 1. Startup Model

When you launch `jot`, it creates one editor session with:

- one empty buffer if no file is given
- interactive home menu (when launched with no CLI argument)
- one workspace root, defaulting to the current directory
- one visible pane
- one shared UI/state loop
- Python runtime initialized for plugins and themes
- no terminal panel until you open it
- no LSP client until you open a supported file

Launch behavior:

```bash
jot
jot path/to/file.cpp
jot path/to/project
```

If the argument is a folder, `jot`:

- changes the working directory to that folder
- treats it as the workspace root
- loads the file tree
- opens the sidebar automatically
- restores that workspace's previous session (open files/tabs, active file,
  cursor/scroll positions, preview tab state)

Workspace sessions are stored per-folder in:

```text
~/.config/jot/workspaces/
```

### Startup Home Menu

When you run `jot` with no argument, startup opens a full-screen home menu:

- colorful JOT ASCII banner
- recent files list
- Nerd Font file icons
- keyboard + mouse navigation

Home menu controls:

- `Up/Down` or `j/k`: move selection
- `Enter`: open selected item
- `1..9`: quick-open recent file
- `n`: new file
- `p`: command palette
- `t`: theme chooser
- `r`: open recent prompt
- `Esc`: hide home menu

### 2. Editor State

At runtime, the editor is built around a few main state objects:

- `buffers`: open files and unsaved tabs
- `panes`: the current split layout and which buffer each pane shows
- `file_tree`: the current workspace sidebar tree
- `integrated_terminals`: terminal tabs in the bottom panel
- `lsp_clients`: native language-server processes owned by the C++ core
- `python_api`: embedded Python bridge for plugins/themes

A buffer stores things like:

- file path
- text lines
- cursor position
- selection
- scroll offsets
- undo/redo state
- modified flag
- diagnostics
- syntax highlight cache

### 3. Input / Editing Model

The public workflow is modeless.

That means:

- typing inserts text directly
- `Backspace`, `Delete`, `Enter`, `Tab`, arrows, and selection keys work like a modern terminal editor
- `Esc` clears selection or releases focus from a sub-panel like the terminal/sidebar

Any legacy mode internals are implementation details; the user-facing flow is editing-first and modeless.

### 4. Layout Model

The screen is made from a few stacked regions:

- top tab bar for file buffers
- main work area for panes
- optional sidebar on the left
- optional minimap on the right
- optional integrated terminal panel at the bottom
- status/message area at the bottom

Pane layout supports:

- single pane
- horizontal split
- vertical split

Each pane points to a buffer. Closing a pane does not necessarily close the file. Closing a buffer only removes that file tab; if it is the last tab, `jot` resets to a fresh empty buffer.

### 5. Focus Model

`jot` has a few practical focus targets:

- editor pane
- sidebar
- integrated terminal
- command/search/input overlays

Important behavior:

- clicking in the editor returns typing focus to the editor
- opening the sidebar moves focus to the sidebar
- pressing `Esc` in the sidebar returns focus to the editor
- opening/focusing the integrated terminal routes keyboard input to the shell
- pressing `Esc` in the terminal releases terminal focus back to the editor

### 6. Buffers, Tabs, and Files

A buffer is an open document, which may or may not already exist on disk.

Current file model:

- tabs across the top represent buffers
- each tab can be clicked
- each tab has a close button
- unsaved buffers remain in memory until closed
- saving writes the active buffer to disk
- opening a file reuses or creates a buffer depending on the editor state

Useful related commands:

- `Ctrl+N`: new empty buffer
- `Ctrl+S`: save current buffer
- file tab close button: close a buffer
- `:w`, `:write`
- `:wq`, `:x`
- `:e <path>`
- `:bd`

### 7. Workspace Sidebar

The sidebar is a workspace file explorer rooted at the current workspace directory.

What it does:

- lists folders before files
- hides dotfiles by default
- expands and collapses directories
- opens files into buffers
- can move up to the parent folder as a new workspace root

Useful sidebar controls:

- `Ctrl+B`: toggle sidebar
- `j` / `k` or arrow keys: move
- `Enter` / `l` / right arrow: expand or open
- `h` / left arrow: collapse
- `r`: refresh tree
- `.`: show/hide hidden files
- `Backspace`: open parent folder as workspace root
- `Esc`: return focus to editor

Mouse support is enabled for the sidebar too.

### 8. Command Palette

`Ctrl+P` opens the command palette.

It behaves like a lightweight ex-style command line instead of a fuzzy GUI launcher:

- it accepts ex-style commands such as `:w`, `:q`, `:theme`, `:term`
- `Tab` completes commands and theme names
- plugin commands are included in completion
- some editor actions also prefill the palette, like `Ctrl+G` for `line`

Built-in command groups include:

- file and buffer commands
- pane split/navigation commands
- search/navigation commands
- theme switching
- terminal commands
- resize commands
- LSP lifecycle commands

### 9. Search and Navigation

The search panel is separate from the command palette.

- `Ctrl+F` toggles search
- matches are tracked as `(line, col)` positions
- search can be case-sensitive
- the current match can be stepped forward/backward

The editor also includes:

- go to line / column
- bookmarks
- matching bracket jump
- duplicate/delete line
- move line or selected block up/down
- toggle comment
- trim trailing whitespace
- format document
- selection indent/outdent

### 10. Integrated Terminal

The bottom terminal panel is a built-in shell view owned by the editor.

What it does:

- opens your native shell using `$SHELL` when possible
- falls back to standard shell paths if needed
- supports multiple terminal tabs
- keeps shell instances alive while hidden
- supports mouse-based tab switching and closing

How it behaves:

- `Ctrl+\`` toggles the terminal panel
- if no terminal exists yet, opening the panel creates one
- terminal tabs live at the top of the terminal panel
- clicking `+` creates a new terminal tab
- clicking a terminal tab switches focus
- `Esc` releases terminal focus and returns to the editor

Important limitation:

`jot`'s terminal panel is still lighter than a full standalone terminal emulator. It handles common shell interaction, but it is not yet a full drop-in replacement for a mature terminal app.

### 11. LSP Architecture

LSP is now owned by the C++ core, not by Python plugins.

Current native LSP flow:

1. you open a supported file
2. `jot` detects the language from the file extension
3. it searches upward for a workspace root
4. it starts one LSP client per `language + workspace root`
5. it sends `didOpen`, debounced `didChange`, and `didSave`
6. it polls the client and applies `publishDiagnostics`

Currently wired language servers:

- Python: `pylsp`
- JavaScript / TypeScript: `typescript-language-server --stdio`
- C / C++: `clangd`

Current LSP scope is mainly:

- process management
- workspace detection
- document sync
- diagnostics overlay

Useful LSP commands:

- `:lspstart`
- `:lspstatus`
- `:lspstop`
- `:lsprestart`

This part is still evolving. The architecture is in place, but it is not yet full VS Code parity for completion, hover, rename, code actions, and symbol UI.

### 12. Python Runtime: Plugins, Themes, Automation

Python is embedded for extension work, not for owning the core editor.

Python is currently responsible for:

- plugins
- themes
- optional user automation
- editor event hooks
- custom commands / keybinds

The Python runtime loads:

- bundled runtime support from `python/`
- user init files
- single-file plugins
- plugin packages with `plugin.py` or `__init__.py`

Primary user config/plugin paths:

```text
~/.config/jot/configs/init.py
~/.config/jot/configs/plugins/
~/.config/jot/configs/colors/
```

Legacy paths are still supported:

```text
~/.config/jot/plugins/
~/.config/jot/themes/
~/.config/jot/*.py
```

Useful plugin commands:

- `:PlugReload`
- `:PlugList`

See [docs/PLUGINS.md](/home/joshuarvl/Documents/jcode/docs/PLUGINS.md) for the plugin API and [docs/THEMES.md](/home/joshuarvl/Documents/jcode/docs/THEMES.md) for themes.

## Current Feature Set

### Editing

- modeless editing flow
- undo / redo
- copy / cut / paste
- duplicate line
- delete line
- move line or selected block up/down
- smart line-start movement
- word-wise deletion
- auto-closing brackets
- auto-indent
- optional indent auto-detection
- selection indent / outdent
- toggle comment
- trim trailing whitespace
- format document

### Navigation and UI

- multi-file tabs
- reopen closed tab
- recent files quick-open
- split panes
- workspace sidebar
- minimap
- bookmarks
- go to line / column
- matching bracket jump
- mouse selection
- double click word selection
- triple click line selection
- theme chooser

### Terminal and Workspace

- open folder as workspace
- integrated terminal panel
- multiple terminal tabs
- native shell launching
- mouse terminal-tab support

### Extensibility

- Python plugins
- Python themes
- native C++ LSP client foundation
- diagnostics overlay from LSP

## Supported Syntax Highlighting

Built-in syntax rules exist for common file types including:

- C / C++: `.c`, `.cpp`, `.h`, `.hpp`
- Python: `.py`
- JavaScript / TypeScript: `.js`, `.ts`
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

## Build

### Dependencies

- CMake 3.14+
- C++17 compiler
- Python 3 development headers and `python3-config`
- a Unix-like environment with `termios`

Notes:

- the UI uses raw terminal handling, not `ncurses`
- the integrated terminal uses PTY support and links `util`

### Build Commands

```bash
mkdir -p build
cd build
cmake ..
make -j"$(nproc)"
```

### Install

```bash
cmake --install . --prefix "$HOME/.local"
```

That installs:

- `jot`
- `jcode` compatibility alias
- bundled Python runtime files
- default config templates

## Run

Open an empty session:

```bash
jot
```

Open a file:

```bash
jot path/to/file.cpp
```

Open a folder as a workspace:

```bash
jot path/to/project
```

You can use `jcode` the same way.

## Configuration

User config lives in:

```text
~/.config/jot/
```

Current layout:

```text
~/.config/jot/
  configs/
    init.py
    settings.conf
    colors/
      my_theme.py
    plugins/
      my_plugin.py
  plugins/     # legacy path
  themes/      # legacy path
```

Bundled starter config in this repo:

```text
.configs/configs/
```

### Default Settings

Built-in defaults include:

- `explorer_width=25`
- `minimap_width=15`
- `show_explorer=true`
- `show_minimap=true`
- `tab_size=2`
- `auto_indent=true`
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

Example `settings.conf`:

```ini
tab_size=2
auto_indent=true
auto_save=false
auto_save_interval_ms=2000
auto_detect_indent=true
render_fps=120
idle_fps=60
terminal_height=12
minimap_width=18
explorer_width=30
lsp_change_debounce_ms=120
```

## Keybindings

### Core Editing

- `Ctrl+Q`: quit
- `Ctrl+S`: save current file
- `Ctrl+Z`: undo
- `Ctrl+Y`: redo
- `Ctrl+A`: select all
- `Ctrl+C`: copy
- `Ctrl+X`: cut
- `Ctrl+V`: paste
- `Ctrl+N`: new buffer
- `Ctrl+D`: duplicate current line
- `Ctrl+K`: delete current line
- `Ctrl+/`: toggle comment
- `Ctrl+Backspace`: delete previous word
- `Ctrl+Delete`: delete next word
- `Ctrl+Space`: request LSP suggestions (completion dropdown)
- `Ctrl+Shift+F`: select current function block
- `Ctrl+Shift+L`: select current line

### Navigation

- `Arrow keys`: move cursor
- `Shift+Arrow keys`: extend selection
- `Home`: smart line start
- `End`: line end
- `Page Up`: move up 10 lines
- `Page Down`: move down 10 lines
- `Alt+Up`: move current line or selected block up
- `Alt+Down`: move current line or selected block down
- `Tab`: indent selection or insert indentation
- when completion dropdown is open: `Up/Down` navigate, `Enter/Tab` apply, `Esc` close
- `Shift+Tab`: outdent selection
- `Esc`: clear selection or release sub-panel focus

### Panels and Tools

- `Ctrl+B`: toggle sidebar
- `Ctrl+F`: toggle search
- `Ctrl+G`: open go-to-line prompt via command palette
- `Ctrl+P`: open command palette
- `Ctrl+R`: open recent-file prompt
- `Ctrl+Shift+T`: reopen last closed tab
- `Ctrl+M`: toggle minimap
- `Ctrl+T`: open theme chooser
- `Ctrl+\``: toggle integrated terminal

### Search Panel

- `Enter`: next match
- `Arrow Down`: next match
- `Arrow Up`: previous match
- `Tab`: toggle case-sensitive search
- `Ctrl+W`: toggle whole-word search
- `Ctrl+F`: next match while search panel is open
- open search with selection active: uses selection text as initial query
- `Esc`: close search

### Sidebar Explorer

- `Arrow Up` / `k`: move up
- `Arrow Down` / `j`: move down
- `Page Up` / `Page Down`: fast scroll through tree
- `Home` / `End`: jump to first/last node
- `Arrow Right` / `l` / `Enter`: expand folder or open file
- `Arrow Left` / `h`: collapse folder or move to parent node
- `r`: refresh workspace tree
- `.`: show / hide dotfiles
- `*`: expand all folders recursively
- `z`: collapse all folders
- `Backspace`: open parent folder as workspace root
- `Esc`: return focus to editor

### Integrated Terminal

- `Ctrl+\``: show, hide, or focus the terminal panel
- `Ctrl+Shift+T`: create a new terminal tab while terminal focus is active
- `Esc`: release terminal focus
- mouse click terminal tab: focus terminal tab
- mouse click `+`: create terminal tab
- mouse click terminal tab close button: close terminal tab

### Mouse

- click: place cursor
- drag: select text
- drag past top/bottom edge: auto-scroll selection
- double click: select word/token
- triple click: select full line
- click file tab: switch buffer
- click file tab `x`: close buffer
- click minimap: reposition viewport
- click sidebar items: expand folders or open files
- click terminal tabs: switch terminal instances

## Command Reference

Open the command palette with `Ctrl+P` and use ex-style commands such as:

- `:q`, `:quit`
- `:q!`, `:quit!`
- `:w`, `:write`
- `:wq`, `:x`, `:xit`
- `:e <file>`, `:edit <file>`, `:open <file>`
- `:new`, `:enew`
- `:bd`, `:bdelete`, `:close`
- `:sp`, `:split`, `:splith`
- `:vsp`, `:splitv`
- `:bn`, `:nextpane`
- `:bp`, `:prevpane`
- `:minimap`
- `:term`, `:terminal`
- `:termnew`, `:terminalnew`
- `:search`
- `:format`
- `:trim`
- `:line <line>[:col]`
- `:goto <line>[:col]`
- `:theme <name>`
- `:colorscheme <name>`
- `:recent`
- `:openrecent [index|query]`
- `:reopen`
- `:autosave [on|off|toggle|status|<ms>]`
- `:resizeleft`, `:resizeright`, `:resizeup`, `:resizedown`
- `:lspstart`, `:lspstatus`, `:lspstop`, `:lsprestart`
- `:help`

Plugin commands registered from Python also appear in command completion.

## Project Layout

```text
apps/jot/      CLI entrypoint (`main.cpp`) and executable target
cmake/         reusable CMake modules (toolchain/config helpers)
include/jot/   public header surface for external consumers
src/core/       editor state, panes, buffers, workspace, terminal and LSP wiring
src/edit/       text editing, cursor movement, selection, clipboard, search
src/input/      keyboard, mouse, command palette, dispatch logic
src/render/     editor drawing, minimap, overlays, UI panels
src/features/   syntax, config, bracket helpers, editing features
src/tools/      integrated terminal, telescope, image viewer, native LSP client
src/plugins/    embedded Python bridge for plugins/themes
src/ui/         raw terminal and UI abstraction layer
src/CMakeLists.txt
python/         Python-side runtime helpers and bundled scripts
docs/           additional project docs
tests/          unit test scaffolding
```

Build graph highlights:

- `jot_engine`: aggregated static engine target for the app
- `jot_core`, `jot_edit`, `jot_features`, `jot_input`, `jot_render`, `jot_tools`, `jot_plugins`, `jot_ui`: module static libraries for ownership boundaries and reuse

## Notes and Limitations

- the user-facing workflow is modeless; legacy mode-related source files are internal compatibility details
- the integrated terminal is improving, but it is still lighter than a full terminal emulator
- native C++ LSP is in active development; diagnostics are the most mature part right now
- Python is meant for extension and customization, not for owning the editor core

## License

MIT
