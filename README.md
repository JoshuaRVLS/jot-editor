# jot

`jot` is a lightweight terminal code editor written in C++ with Python-based extensibility, a modeless editing flow, a workspace sidebar, and a built-in terminal panel.

![jot screenshot](screenshot.png)

## Overview

`jot` is designed to feel closer to a modern editor than a strict modal Vim clone:

- direct typing by default
- mouse selection and tab clicks
- multiple file tabs
- split panes
- sidebar workspace explorer
- command palette
- integrated terminal tabs
- Python plugins and themes

The project installs as `jot` and also installs a compatibility alias named `jcode`.

## Current Feature Set

### Editing

- Modeless default editing flow
- Multi-file tabs
- Split panes
- Undo / redo
- Copy / cut / paste
- Duplicate line
- Delete line
- Move line or selected block up/down
- Smart line-start movement
- Word-wise deletion
- Auto-closing brackets
- Auto-indent
- Optional indent auto-detection when opening files
- Selection indent / outdent
- Toggle comment
- Format document
- Trim trailing whitespace

### Navigation

- Go to line / column
- Matching bracket jump
- Select current function block
- Select current line
- Bookmarks
- Minimap

### Search and Commanding

- Search panel
- Command palette with tab completion
- Ex-style commands such as `:w`, `:q`, `:e`, `:line`, `:theme`, `:term`

### Workspace and UI

- Sidebar file explorer
- Open a folder as a workspace
- Mouse support in the explorer
- File tabs with close buttons
- Mouse-based text selection
- Double click word selection
- Triple click line selection
- Theme chooser

### Integrated Terminal

- Toggleable integrated terminal panel
- Multiple terminal tabs
- Create and close terminal instances
- Terminal focus switching with mouse
- Uses your native shell where available

### Extensibility

- Python plugin support
- Python theme support
- LSP-related Python modules included in `python/`

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
- A Unix-like environment with `termios`

Notes:

- The project uses raw terminal handling, not `ncurses`.
- CMake links `util` for PTY support used by the integrated terminal.

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
- `jcode` as a compatibility alias
- bundled Python runtime files
- default config templates

## Run

Open an empty editor:

```bash
jot
```

Open a file:

```bash
jot path/to/file.cpp
```

Open a folder as the current workspace:

```bash
jot path/to/project
```

You can use `jcode` the same way:

```bash
jcode path/to/project
```

When you launch with a folder, `jot` changes into that directory, loads it as the workspace root, and opens the sidebar automatically.

## Configuration

User config lives in:

```text
~/.config/jot/
```

Primary layout:

```text
~/.config/jot/
  configs/
    init.py
    settings.conf
    colors/
      my_theme.py
    plugins/
      my_plugin.py
  plugins/     # legacy path, still supported
  themes/      # legacy path, still supported
```

Bundled starter config in this repo:

```text
.configs/configs/
```

Important files:

- `~/.config/jot/configs/init.py`
- `~/.config/jot/configs/settings.conf`
- `~/.config/jot/configs/colors/`
- `~/.config/jot/configs/plugins/`

### Default Settings

Current built-in defaults include:

- `explorer_width=25`
- `minimap_width=15`
- `show_explorer=true`
- `show_minimap=true`
- `tab_size=2`
- `auto_indent=true`
- `auto_detect_indent=false`
- `show_line_numbers=true`
- `word_wrap=false`
- `cursor_style=block`
- `render_fps=120`
- `idle_fps=60`
- `terminal_height=10`

### Common Tuning

Example `settings.conf`:

```ini
tab_size=2
auto_indent=true
auto_detect_indent=true
render_fps=120
idle_fps=60
terminal_height=12
minimap_width=18
explorer_width=30
```

## Keybindings

## Global / Main Editing

- `Ctrl+Q`: quit
- `Ctrl+S`: save current file
- `Ctrl+Z`: undo
- `Ctrl+Y`: redo
- `Ctrl+A`: select all
- `Ctrl+C`: copy
- `Ctrl+X`: cut
- `Ctrl+V`: paste
- `Ctrl+B`: toggle sidebar
- `Ctrl+F`: toggle search panel
- `Ctrl+G`: open go-to-line prompt
- `Ctrl+P`: open command palette
- `Ctrl+M`: toggle minimap
- `Ctrl+T`: open theme chooser
- `Ctrl+N`: new buffer
- `Ctrl+W`: close current buffer
- `Ctrl+D`: duplicate current line
- `Ctrl+K`: delete current line
- `Ctrl+/`: toggle comment
- `Ctrl+Backspace`: delete previous word
- `Ctrl+Delete`: delete next word
- `Ctrl+Shift+F`: select surrounding function block
- `Ctrl+Shift+L`: select current line
- `Ctrl+\``: toggle integrated terminal

## Selection and Movement

- `Arrow keys`: move cursor
- `Shift+Arrow keys`: extend selection
- `Home`: smart line start
- `End`: line end
- `Page Up`: move up 10 lines
- `Page Down`: move down 10 lines
- `Alt+Up`: move current line or selected block up
- `Alt+Down`: move current line or selected block down
- `Tab`: indent selection, or insert indentation
- `Shift+Tab`: outdent selection
- `Esc`: clear selection, or release terminal focus

## Search Panel

- `Ctrl+F`: open / close search
- `Enter`: next match
- `Arrow Down`: next match
- `Arrow Up`: previous match
- `Tab`: toggle case-sensitive search
- `Esc`: close search

## Sidebar Explorer

- `Ctrl+B`: toggle sidebar
- `Arrow Up` / `k`: move up
- `Arrow Down` / `j`: move down
- `Arrow Right` / `l` / `Enter`: expand folder or open file
- `Arrow Left` / `h`: collapse folder or move to parent node
- `r`: refresh workspace tree
- `.`: show / hide dotfiles
- `Backspace`: open parent folder as workspace root
- `Esc`: return focus to the editor

## Integrated Terminal

- `Ctrl+\``: show / hide / focus the terminal panel
- `Ctrl+Shift+T`: create a new terminal tab
- `Esc`: release terminal focus and return to editor interaction
- mouse click on terminal tab: focus terminal tab
- mouse click on `+`: create terminal tab
- mouse click on terminal tab close button: close that terminal tab

## Mouse Support

- Click to place cursor
- Drag to select text
- Double click to select a word/token
- Triple click to select a full line
- Click file tabs to switch buffers
- Click file tab `x` to close a buffer
- Click minimap to reposition the viewport
- Click sidebar items to expand folders or open files
- Click terminal tabs to focus them

## Command Palette and Commands

Open the command palette with `Ctrl+P`.

Supported ex-style commands include:

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
- `:help`

Tab completion is supported in the command palette for commands and theme names.

## Plugins

`jot` supports Python plugins.

Plugin capabilities now include:

- single-file plugins and plugin packages
- user-defined commands with arguments
- `autocmd`-style event hooks
- plugin reload and plugin listing commands
- richer editor-state access from Python

Primary plugin paths:

- `~/.config/jot/configs/init.py`
- `~/.config/jot/configs/plugins/`

Legacy paths are still loaded:

- `~/.config/jot/plugins/`
- `~/.config/jot/*.py`

Read the full plugin guide in [docs/PLUGINS.md](/home/joshuarvl/Documents/jcode/docs/PLUGINS.md).

## Themes

Themes are Python files that define colors through the provided API.

Primary theme path:

- `~/.config/jot/configs/colors/`

Legacy theme path:

- `~/.config/jot/themes/`

Read the full theme guide in [docs/THEMES.md](/home/joshuarvl/Documents/jcode/docs/THEMES.md).

## Project Layout

Main directories:

```text
src/core/       editor state, panes, files, terminal integration
src/edit/       editing operations and cursor movement
src/input/      keyboard, mouse, command handling
src/render/     buffer, UI, minimap, overlay rendering
src/features/   syntax, config, bracket helpers, editor features
src/tools/      integrated terminal, telescope, image viewer
src/plugins/    embedded Python bridge
python/         Python-side API and bundled plugins
docs/           plugin and theme docs
```

## Notes

- `jot` currently uses a modeless editing-first workflow even though some legacy mode-related code still exists internally.
- The integrated terminal is a lightweight built-in terminal panel, not a full standalone terminal emulator.
- Mouse-heavy workflows and keyboard-heavy workflows are both supported.

## License

MIT
