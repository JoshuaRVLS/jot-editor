# jot

**VS Code, but in your terminal.**

A modern, modeless code editor written in C++17. Type and it edits — no modal
modes. Split panes, workspace sidebar, minimap, tree-sitter highlighting,
native LSP, integrated terminal, debugger, and Git — without leaving the CLI.

## Install

```bash
./install.sh          # user-local install to ~/.local
```

Or build with CMake:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cmake --install build --prefix "$HOME/.local"
```

## Run

```bash
jot                 # resume last workspace, or home menu
jot file.cpp        # open a file
jot path/to/project # open a folder as the workspace
```

## Docs

- [Features, keybindings & commands](docs/FEATURES.md) — what jot can do and how to drive it
- [Configuration](docs/FEATURES.md#configuration) — Lua-first settings
- [Themes](docs/THEMES.md) — authoring colorschemes
- [Lua API](docs/LUA_API.md) — plugins and scripting
- [Plugins](docs/PLUGINS.md)
- [Debugger](docs/DEBUGGER.md)
- [Tree-sitter](docs/TREE_SITTER.md)

## Platform support

Linux (x86_64/arm64) and macOS (Intel/Apple Silicon) are supported. Windows
10/11 + MSVC + Windows Terminal is experimental.

## License

MIT
