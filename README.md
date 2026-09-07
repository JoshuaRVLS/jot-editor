# jot

**VS Code, but in your terminal.**

<img width="1925" height="1019" alt="{85DF00B6-9481-45AB-9786-67D7F77591DC}" src="https://github.com/user-attachments/assets/2f0f6632-c6bd-4f2f-882d-3c6ff0a925a3" />
<img width="1912" height="1008" alt="{2104253A-E5D9-407D-B911-4F53E86A9816}" src="https://github.com/user-attachments/assets/0081284e-e348-4f3a-9d18-e73d43537185" />
<img width="582" height="239" alt="{6092B372-FA46-42FE-B498-F7EE0B201366}" src="https://github.com/user-attachments/assets/521e7c42-2429-48a6-b802-7ac81c6013d0" />

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
