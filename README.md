# jot

A lightweight, terminal-based code editor written in C++ with Python scripting support.

![jot screenshot](screenshot.png)

## Features
*   **Performance**: Written in C++ for speed.
*   **Python Plugins**: Extend functionality using Python.
*   **Syntax Highlighting**: Supports C++, Python, JS, etc.
*   **Themes**: Fully customizable color schemes.
*   **Telescope**: Fuzzy file finder (`Ctrl+P`).
*   **File Tree**: Sidebar explorer (`Ctrl+B`).
*   **Minimap**: Code overview on the right.

## Installation

### Dependencies
*   CMake
*   C++17 Compiler (GCC/Clang)
*   Python 3 check `python3-config`
*   ncurses? (Uses custom terminal handling or raw escape codes? (It uses termios + raw VT100 escapes, no ncurses dependency))

### Build
```bash
mkdir build
cd build
cmake ..
make -j$(nproc)
cmake --install . --prefix "$HOME/.local"
```

### Run
```bash
./jot [filename]
```

## Configuration

Configuration and plugins live in `~/.config/jot/`.

Primary layout:

```text
~/.config/jot/
  configs/
    init.py
    colors/
      jot_nvim.py
    plugins/
  plugins/         # legacy, still loaded
  themes/          # legacy, still loaded
```

*   **Plugins**: Read [docs/PLUGINS.md](docs/PLUGINS.md)
*   **Themes**: Read [docs/THEMES.md](docs/THEMES.md)
*   **Smoothness tuning** (`~/.config/jot/configs/settings.conf`):
    * `render_fps=120` for active editing redraw cadence
    * `idle_fps=60` for idle redraw cadence

## Keybindings
*   `Ctrl+Q`: Quit
*   `Ctrl+S`: Save
*   `Ctrl+F`: Find
*   `Ctrl+P`: Fuzzy Finder (Telescope)
*   `Ctrl+B`: Toggle Sidebar
*   `Ctrl+N`: New File/Tab
*   `Ctrl+W`: Close Tab
*   `Ctrl+Space`: Command Palette
*   `Ctrl+D`: Duplicate current line
*   `Ctrl+K`: Delete current line
*   `Ctrl+/`: Toggle comment on current line/selection
*   `Ctrl+Backspace`: Delete previous word
*   `Ctrl+Delete`: Delete next word
*   `m`: Toggle bookmark on current line
*   `[` / `]`: Jump to previous/next bookmark
*   `#`: Toggle comment on current line/selection
*   `Y`: Duplicate current line
*   `=`: Format document
*   `T`: Trim trailing whitespace across the file
*   `\`: Toggle auto-indent on/off
*   `+` / `-`: Increase/decrease tab size (1-8, persisted)
*   `Tab` (Search panel): Toggle case-sensitive search

Sidebar explorer (`Ctrl+B`) controls:
*   `j` / `k` or Arrow Up/Down: Move selection
*   `l` or Arrow Right: Expand folder / open file
*   `h` or Arrow Left: Collapse folder / jump to parent node
*   `Enter`: Toggle folder or open file
*   `r`: Refresh tree
*   `.`: Toggle hidden files
*   `Backspace`: Open parent folder as workspace root

## License
MIT
