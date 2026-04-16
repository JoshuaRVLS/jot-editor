# jcode

A lightweight, terminal-based code editor written in C++ with Python scripting support.

![jcode screenshot](screenshot.png)

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
./jcode [filename]
```

## Configuration

Configuration and plugins live in `~/.config/jcode/`.

Primary layout:

```text
~/.config/jcode/
  configs/
    init.py
    colors/
      jcode_nvim.py
    plugins/
  plugins/         # legacy, still loaded
  themes/          # legacy, still loaded
```

*   **Plugins**: Read [docs/PLUGINS.md](docs/PLUGINS.md)
*   **Themes**: Read [docs/THEMES.md](docs/THEMES.md)

## Keybindings
*   `Ctrl+Q`: Quit
*   `Ctrl+S`: Save
*   `Ctrl+F`: Find
*   `Ctrl+P`: Fuzzy Finder (Telescope)
*   `Ctrl+B`: Toggle Sidebar
*   `Ctrl+N`: New File/Tab
*   `Ctrl+W`: Close Tab
*   `Ctrl+Space`: Command Palette
*   `m` (Normal mode): Toggle bookmark on current line
*   `[` / `]` (Normal mode): Jump to previous/next bookmark
*   `#` (Normal mode): Toggle comment on current line/selection
*   `Y` (Normal mode): Duplicate current line
*   `=` (Normal mode): Format document
*   `T` (Normal mode): Trim trailing whitespace across the file
*   `\` (Normal mode): Toggle auto-indent on/off
*   `+` / `-` (Normal mode): Increase/decrease tab size (1-8, persisted)
*   `Tab` (Search panel): Toggle case-sensitive search

## License
MIT
