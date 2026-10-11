# jot

**VS Code, but in your terminal.**
<img width="2533" height="1372" alt="image" src="https://github.com/user-attachments/assets/b77080c2-8370-42ab-89f5-dac1c1de6e3c" />
Just try it. **Still in development**.

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

On Linux, the latest release also carries a one-file build and a package:
`jot-linux-x86_64.AppImage` (`chmod +x` it, run it - the libraries jot links
against travel inside, so there is nothing to install first) and
`jot-<version>-1-x86_64.pkg.tar.zst` for Arch and its derivatives
(`sudo pacman -U jot-*.pkg.tar.zst`, which pulls in what the binary needs;
libtermkey is in the AUR these days, as install.sh notes).
`jot-linux-x86_64.tar.gz` is the same tree for a manual install, built against
the Ubuntu runner's libraries - on Arch, reach for the package or the AppImage
instead.

On Windows, run `jot-windows-x86_64-setup.exe` from the latest release. It
installs for the current user - no admin prompt - and does the rest itself: the
binary, the bundled colour schemes, Lua runtime and clangd payload, `jot` on
your `PATH`, a Start menu entry. `jot-windows-x86_64.zip` is the same tree for a
portable copy: unpack it anywhere and run `bin\jot.exe`. Settings live in
`%APPDATA%\jot`.

## Run

```bash
jot                 # resume last workspace, or home menu
jot file.cpp        # open a file
jot path/to/project # open a folder as the workspace
add --gui flag to use the GUI.
```

## Docs

- [Features, keybindings & commands](docs/FEATURES.md) - what jot can do and how to drive it
- [Configuration](docs/FEATURES.md#configuration) - Lua-first settings
- [Themes](docs/THEMES.md) - authoring colorschemes
- [Architecture](docs/ARCHITECTURE.md) - module layout for contributors
- [Lua API](docs/LUA_API.md) - plugins and scripting
- [Plugins](docs/PLUGINS.md)
- [Debugger](docs/DEBUGGER.md)
- [Tree-sitter](docs/TREE_SITTER.md)
- [LeetCode workspace](docs/LEETCODE.md)

## Platform support

Linux (x86_64/arm64) and macOS (Intel/Apple Silicon) are supported. Windows
10/11 + MSVC + Windows Terminal is experimental.

## License

MIT
