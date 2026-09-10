# jot — features, keybindings & commands

The full guide to what jot does and how to drive it. For the one-paragraph
pitch and install steps, see the [README](../README.md).

## What makes jot different

- **Modeless by default.** No insert/normal mode juggling — just type. Mouse
  selection, tabs, split panes, a minimap, a command palette, and a workspace
  sidebar are all there out of the box.
- **A native C++ core.** Buffers, panes, syntax highlighting, LSP, debugging,
  Git, terminal emulation, and workspace state all run in the core — no
  runtime scripting dependency drives the editor itself.
- **Tree-sitter highlighting** with on-demand grammar installation, status and
  reload commands, fallback queries, and rich theme slots for semantic tokens.
- **Native LSP** for diagnostics, completion, hover, go-to-definition, document
  symbols, and full server lifecycle management (including lightweight
  install/remove helpers).
- **An integrated terminal** with multiple shell tabs and a task runner for
  local and project-level commands.
- **A debugger panel** speaking the Debug Adapter Protocol — GDB and LLDB
  launch/attach flows, breakpoints, threads, stack, variables, memory,
  disassembly, and output.
- **Git workflows** for status, diffs, staging, unstaging, committing, log,
  blame, and refresh — all from inside the editor.
- **Lua plugins and JSON colorschemes** for customization, with behavior owned
  by the C++ core.

## Running jot

```bash
jot                 # resume your last workspace, or show the home menu
jot file.cpp        # open a single file
jot path/to/project # open a folder as the workspace root
```

Workspace sessions are stored under `~/.config/jot/workspaces/`, so jot can
pick up where you left off.

### GUI frontend (`jot --gui`)

```bash
jot --gui           # same editor, but in a GPU window
jot --gui file.cpp
```

The SDL2/OpenGL frontend renders the exact same cell grid as the terminal
backend, but with a GPU and vsync'd to the monitor refresh, so typing is
smooth at any refresh rate (60/120/144Hz...). It uses the same editor core,
keybindings, LSP, panes and Lua UI — only the screen differs. Rendering is
direct OpenGL 3.3 core with a FreeType glyph atlas (Nerd Font icons
included; set `JOT_GUI_FONT` to override the font). The GUI frontend is
optional: it compiles in when SDL2 + FreeType are available (`JOT_GUI=OFF`
to disable) and the terminal build is unaffected.

## Feature tour

### Editing

- Modeless text entry — typing edits immediately.
- Undo/redo, copy/cut/paste, select all, mouse selection, double-click word
  selection, triple-click line selection.
- Smart multi-line paste that re-indents to the cursor.
- Auto-indent, auto-closing brackets, bracket matching and jumping, rainbow
  bracket colors, and an active bracket guide.
- Line helpers: duplicate, delete, move up/down, join; trim trailing
  whitespace or blank lines; uppercase/lowercase; sort/reverse/deduplicate/
  shuffle selected lines.
- Case-sensitive, whole-word, and regex replace; surround/unsurround;
  increment/decrement the number under the cursor; format document; word-wise
  deletion; smart line-start movement; file/page movement.

### Buffers, tabs, and panes

- Any number of open buffers, saved or unsaved.
- Pane-local file tabs with click-to-switch and close buttons, plus reopen of
  the last closed tab.
- Split panes in any direction; move focus or resize with keyboard or mouse.
- Open, save, save-as, close, quit (with force variants), and autosave with a
  configurable interval.

### Workspace and sidebar

- The workspace root is the folder you launched (or your current directory).
- A sidebar file explorer with tree indent guides: folders before files, a
  hidden-file toggle, expand/collapse all, reveal the active file (`E`),
  refresh, and parent-folder navigation.
- Create, rename, and delete files and folders from the tree.
- Recent files and workspace resume, plus a home menu for recent entries and
  quick actions.
- C++ assist: create matching header/source pairs, or generate missing source
  implementations from declarations.

### Search and navigation

- Per-buffer search with case/whole-word/regex options, and a replace panel
  (current match or all).
- Selection-scoped replace via `Ctrl+Shift+F` — or project-wide search when
  nothing is selected.
- Go to line, bookmarks, and a fuzzy file finder (telescope) with mouse
  support, syntax-highlighted previews, matched-character highlighting and
  per-language colored file icons in the results.
- Picker for project-wide text search, diagnostics, and document symbols, plus
  a persistent outline panel (`:outline`).

### Syntax highlighting and folding

- Tree-sitter highlighting when the runtime and grammars are available, with a
  regex fallback for common file types.
- Commands to install grammars, inspect status, and reload parser/query caches.
- Rich token mapping (variables, parameters, types, functions, keywords,
  operators, etc.) with theme slots for both classic and Tree-sitter captures.
- Code folding with toggle/collapse/expand, mouse toggling, and persisted
  collapsed ranges.

The built-in fallback covers the usual suspects: C/C++, Python, JavaScript/
TypeScript (incl. JSX/TSX), HTML/XML, Rust, CSS, Java, Go, Markdown, JSON,
Shell, Ruby, and PHP.

### LSP

- One language server per language per workspace root, driven natively from
  C++ (no Python glue).
- Debounced file sync, diagnostics overlay, and next/previous diagnostic
  jumps.
- Completion with fuzzy filtering and `textEdit` support; hover on demand or
  on mouse hover; go-to-definition with a return stack (`:lspback`); LSP
  rename across all affected files (`:lsprename <new_name>`); find
  references in a jumpable quick-pick list (`:lsprefs`); code actions
  (quick fixes and refactors) offered at the cursor (`:lspactions`).
- Signature help popup, plus clangd-style inlay hints on already-written
  code: parameter names (`a: 1, b: 2`) before arguments and type hints after
  variable declarations (`auto x = 5` shows `x: int`), both as dimmed
  virtual text that shifts the line right as in VS Code. Hints refresh after
  edits and follow scrolling; disable all hints with
  `jot.config.set("lsp_inlay_hints", false)` or just the type hints with
  `jot.config.set("lsp_inlay_type_hints", false)`.
- Document symbols via LSP with regex fallback — as a picker or a persistent
  outline.
- Status, start/stop/restart, a manager, and install/remove helpers.

Default servers: `pylsp` (Python), `typescript-language-server --stdio`
(JS/TS), `vscode-html-language-server --stdio` (HTML), `clangd` (C/C++).
Install helpers also cover Rust, Go, Lua, and Bash.

### Integrated terminal and tasks

- A bottom terminal panel backed by a real PTY, with multiple tabs that stay
  alive while hidden.
- Drag the panel's top border to resize it live; it can grow to nearly the
  full window.
- **Fullscreen zoom** (like pane zoom): `Alt+Shift+Z` while focused, the `□`
  tab button, or `:termzoom` toggles the terminal across the whole pane
  area. `Esc` exits fullscreen too. While zoomed, the sidebar and right
  dock are hidden so nothing overlaps the fullscreen terminal.
- **Mouse selection**: click and drag in the terminal to highlight text
  (drag beyond the panel edges is clamped to the visible rows); releasing
  copies the selection to the system clipboard.
- `Esc` returns focus to the editor; the mouse switches/closes tabs or opens
  new ones.
- Local and global task files:

```text
<workspace>/.jot/tasks.json
~/.config/jot/configs/tasks.json
```

```json
{ "tasks": { "build": "cmake --build build -j" } }
```

Local tasks override global ones of the same name. `:task` lists them,
`:task <name>` runs (or reuses) a tab, `:tasknew` starts a fresh one, and
`:taskrerun` reruns the last.

### Image viewer

Open image files in a right-side viewer that uses real terminal graphics when
available — Kitty graphics first, Sixel (`img2sixel`) second, and a 256-color
cell preview as the fallback. Configure with `image_viewer_backend = auto`
(`kitty`, `sixel`, `cell`, or `off`).

### Debugger

- Native Debug Adapter Protocol client with GDB/LLDB launch commands and
  attach-to-PID.
- Session configs from `<workspace>/.jot/debug.json` or
  `~/.config/jot/configs/debug.json`:

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

- Click the gutter to toggle breakpoints; step, continue, pause, restart, and
  inspect threads, stack, variables, memory, disassembly, breakpoints, and
  output — all in the debugger panel.

### Git

**Git panel** (`:gitpanel`) — a native, lazygit-style git client in the
right dock with four views (switch with `2`/`3`/`4`/`5`, matching lazygit's
panel numbers):

- **2 Files** — conflicts, staged, unstaged and untracked sections with
  status-colored rows; `space` stages/unstages the selected file, `a`/`A`
  stage/unstage everything, `Enter` opens the file's diff (double-click
  too), `c` opens the commit-message prompt, `d` discards the file's
  working-tree changes (press twice to confirm), `s` stashes everything
  (including untracked), `y` copies the path.
- **3 Branches** — `space` checks out, `n` creates a new branch (prompt),
  `m` merges the selected branch into the current one, `d` deletes it
  (twice to confirm).
- **4 Commits** — recent history with hash/date/subject; `space` checks out
  a commit (detached HEAD), `y` copies the hash.
- **5 Stash** — `space` applies, `g` pops, `d` drops (twice to confirm).

Global panel keys: `j`/`k` (or arrows) move, `,`/`.` page, `<`/`>` (or
Home/End) jump to top/bottom, `f` fetches, `p`/`P` pull/push, `r`
refreshes, `?` lists the keys, `q`/`Esc` closes. The branch header shows
upstream drift (`↑1 ↓2`) and change counts; the panel auto-refreshes after
every action.

The diff view (`Enter` on a file, or `:gitdiff`/`:gitdiffstaged`) shows
the file's per-language icon and right-aligned change stats in the header
(`+N -M · L lines`), then the diff body with green/red tinted added and
deleted lines, accent bold hunk headers, dim meta lines, `j`/`k` scrolling
and a key-hint footer. Commit messages and branch names are typed in the command
palette, which the panel opens pre-filled (`:gitcommit <message>`,
`:gitcheckout -b <name>`, `:gitmerge <branch>`).

Status summaries, diffs (staged and unstaged), recent log, and line blame
stay one command away (`:gitstatus` `:gitdiff` `:gitdiffstaged` `:gitlog`
`:gitblame`), as do staging (`:gitstage` `:gitunstage` `:gitstageall`
`:gitunstageall`) and committing (`:gitcommit <message>`).

For the full lazygit experience on top, `:lazygit` opens the
[lazygit](https://github.com/jesseduffield/lazygit) TUI in an integrated
terminal tab rooted at the workspace (or the current file's directory). It
is an external binary, not bundled — install it with `brew install lazygit`,
`apt install lazygit`, or the script on lazygit's README. Running `:lazygit`
again re-focuses the open tab; quit lazygit with `q` to return to the
editor.

### UI and mouse

The chrome is yours to arrange: menu bar, pane tabs, editor panes, optional
sidebar, optional minimap, a right-side tool dock, bottom terminal/debugger
panels, and a two-row status/message area. The mouse is wired throughout —
click to place the cursor, drag to select (with edge auto-scroll), double/
triple-click for word/line selection (double-click stops at `.`, so `ext`
in `ext.begin()` selects just `ext`), `Ctrl+D` to select the next occurrence
(`Alt+Click` adds a caret, `Esc` clears extra carets), click tabs, drag split
dividers, jump
the minimap viewport, navigate the sidebar, and toggle breakpoints.

**Zen focus mode** (`F12` or `:zen`) strips the chrome: sidebar, right
panel, and status line hide, and the pane area narrows to the
`zen_content_width` config (default 100 columns) and centers — a
distraction-free editing column. Toggling back restores the exact layout
from before, and the F12 binding is rebindable via the usual `jot.keymap`
API.

## Keybindings

### Core editing

| Shortcut | Action |
| --- | --- |
| Typing | Insert text at the cursor |
| `Esc` | Clear selection / close the active floating surface |
| `Ctrl+Z` / `Ctrl+Y` | Undo / redo |
| `Ctrl+A` | Select all |
| `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Copy / cut / paste |
| `Ctrl+D` | Select next occurrence (multi-cursor) |
| `Ctrl+Shift+D` | Duplicate current line |
| `Ctrl+K` | Delete current line |
| `Ctrl+/` | Toggle comment (138 languages, per-extension comment markers) |
| `Ctrl+Backspace` / `Ctrl+Delete` | Delete previous / next word |
| `Ctrl+Enter` / `Ctrl+Shift+Enter` | Insert line below / above |
| `Alt+Enter` / `Alt+Shift+Enter` | Terminal fallback: line below / above |
| `Ctrl+Space` | LSP completion |
| `Ctrl+Shift+L` | Select current line |
| `Ctrl+Shift+U` / `Ctrl+Shift+N` | Uppercase / lowercase selection or word |

### Navigation

| Shortcut | Action |
| --- | --- |
| Arrow keys | Move cursor |
| `Shift+Arrow` | Extend selection |
| `Home` / `End` | Smart line start / end |
| `Page Up` / `Page Down` | Move by 10 lines |
| `Alt+I` / `Alt+A` | Smart line start / end |
| `Alt+G` / `Alt+Shift+G` | File start / end |
| `Alt+H` / `Alt+L` | Previous / next word |
| `Alt+Up` / `Alt+Down` | Move line or selection up / down |
| `Tab` / `Shift+Tab` | Indent / outdent selection |

### Buffers, tabs, panes, and tools

| Shortcut | Action |
| --- | --- |
| `Ctrl+Tab` / `Ctrl+Shift+Tab` | Next / previous pane-local tab |
| `Alt+,` / `Alt+.` | Previous / next tab |
| `Alt+1..9` / `Alt+0` | Switch to tab 1..9 / last tab |
| `Alt+W` | Close current file tab |
| `Alt+N` | New buffer |
| `Alt+S` | Save |
| `Ctrl+B` or `Alt+B` | Toggle sidebar |
| `Ctrl+F` or `Alt+F` | Search panel |
| `Ctrl+G` | Go-to-line prompt |
| `Ctrl+P` or `Alt+P` | Command palette |
| `Ctrl+E` | File finder (telescope) |
| `Ctrl+R` | Recent-file prompt |
| `Ctrl+Shift+T` | Reopen last closed tab |
| `Ctrl+Shift+F` | Replace in selection, or project-wide search |
| `Ctrl+Shift+M` | Diagnostics picker |
| `Ctrl+Shift+O` | Document symbol picker |
| `:gitpanel` | Open the git panel (2-5 switch views) |
| `:outline` | Persistent outline panel |
| `Ctrl+M` or `Alt+M` | Toggle minimap |
| `Ctrl+T` or `Alt+T` | Theme chooser |
| `` Ctrl+` `` | Open / focus / hide terminal panel |
| `F12` | Toggle zen focus mode (hide chrome, center buffer) |
| `:settings` or `Ctrl+,` (GUI) | Open the settings menu (all config keys: booleans toggle, values edit inline; Lua-registered keys included) |

### Pane layout

| Shortcut | Action |
| --- | --- |
| `Alt+H/J/K/L` | Focus pane (or explorer) left/down/up/right |
| `Alt+Shift+H/J/K/L` | Split left/down/up/right |
| `Ctrl+Alt+Arrow` | Focus pane in that direction |
| `Alt+Shift+Q` or `Ctrl+Q` | Close current pane |
| `Ctrl+Shift+H/J/K/L` | Resize pane |
| `Ctrl+Arrow` | Resize pane |

### Search panel

`Enter` / `↓` / `Ctrl+F` next match · `↑` previous · `Tab` case-sensitivity or
replace field · `Ctrl+H` show/hide replace · `Ctrl+R` replace current ·
`Ctrl+Shift+R` replace all · `Ctrl+W` whole-word · `Ctrl+E` regex · `Esc` close

### Sidebar explorer

`↑`/`k` up · `↓`/`j` down · `PgUp`/`PgDn` fast scroll · `Home`/`End` first/last
· `→`/`l`/`Enter` expand or open · `←`/`h` collapse or parent · `r` refresh ·
`a` create file · `A` create folder · `i` generate C++ implementations ·
`C` create header/source pair · `d d` delete · `.` toggle dotfiles ·
`*` expand all · `z` collapse all · `Backspace` open parent as root · `Esc` back
to editor

### Picker and completion

Type to filter, `↑`/`↓` to move, `Home`/`End` to jump, `Enter` to accept,
`Backspace` to edit, `Esc` to close. In LSP completion, `Enter` or `Tab`
applies and `Esc` closes. The typed characters light up in each item's
label (nvim-cmp's abbr-match highlight), and the selected item's remaining
insert text previews dimmed at the caret as ghost text
(`lsp_completion_ghost_text` to disable).

## Command reference

Open the palette with `Ctrl+P` and type an ex-style command. The prompt
lives in the statusline row at the bottom of the screen, like Neovim's
cmdline, with the fuzzy command/argument completion list popping up above
it -- the buffer stays fully visible while you type.

**Files & sessions:** `:q` `:w` `:wq` `:x` `:e` `:new` `:bd` `:home`
`:resume` `:recent` `:reopen` `:autosave`

**Panes & UI:** `:sp` `:vsp` `:splitleft|right|up|down` `:bn` `:bp`
`:focusleft|right|up|down` `:wincmd` `:resize*` `:minimap` `:theme`
`:colorscheme` `:zen` `:help`

**Workspace:** `:find` / `:ff [dir]` `:mkfile` `:mkdir` `:rename` `:rm`
`:cpppair` `:cppimpl`

**Search & edit:** `:search` `:grep` `:diagnostics` `:diagnext` `:symbols`
`:outline` `:line` `:goto` `:format` `:trim` `:upper` `:lower`
`:sortlines|desc|reverselines|uniquelines|shufflelines|joinlines` `:dupe`
`:replace*` `:surround` `:fold*` `:incnum` `:decnum` `:copypath` `:copyname`
`:datetime` `:stats`

**LSP:** `:lspinstall` `:lspremove` `:lspstatus` `:hover` `:definition`
`:gd` `:lspback` `:lsprename <name>` `:lsprefs` `:lspactions`

**Tree-sitter:** `:tsinstall <lang>` (e.g. `:tsinstall javascript` or
`:tsinstall jsx`) `:tsstatus` `:tsreload`

**Terminal & tasks:** `:term` `:termnew` `:task [name]` `:tasknew <name>`
`:taskrerun`

**Debugger:** `:debug <program>` `:debuggdb` `:debuglldb` `:debugconfig`
`:debugattach <pid>` `:debugpanel` `:debugstop|restart|continue|pause`
`:debugstep|next|out` `:debugthreads` `:debugmemory` `:debugdisasm`

**Git:** `:gitpanel` `:gitcheckout` `:gitmerge` `:lazygit` `:gitstatus`
`:gitdiff` `:gitdiffstaged` `:gitstage` `:gitunstage`
`:gitstageall` `:gitunstageall` `:gitcommit <msg>` `:gitlog` `:gitblame`
`:gitrefresh`

**Discord:** `:discord` (status) `:discord enable|disable|reconnect|disconnect`
— Rich Presence showing the file, language, workspace, git branch and (when the
remote is a browsable URL) a "View Repository" button, with idling / editing /
debugging states and an idle timeout. On by default; every row is a template
(see `discord_details_*` in `:settings`). Discord artwork has to be uploaded to
your Discord application once — `packaging/discord-presence/ASSETS.md` walks
through it.

## Configuration

User config lives in `~/.config/jot/` and is **Lua-first** — `config.lua`
defines settings (loaded before `init.lua` and plugins) and applies them
**live**, no restart needed. `configs/settings.conf` is just the runtime-save
overlay written by `jot.config.set` from Lua.

```text
~/.config/jot/
  config.lua    # Lua-first config (loaded first, re-run on :reload)
  init.lua      # startup script
  plugins/      # *.lua or plugin.lua
  configs/
    settings.conf   # runtime-save overlay for jot.config.set
    colors/
      my_theme.json
  themes/       # legacy colorscheme path
```

Example `config.lua`:

```lua
jot.config.set("tab_size", 4)
jot.config.set("show_indent_guides", true)
jot.config.set("color_scheme", "monokai")
jot.config.set("auto_save", true)
jot.config.set("auto_save_interval_ms", 5000)
```

`:reload` re-reads `settings.conf`, re-runs `config.lua`, live-applies it, and
reloads Lua plugins and Tree-sitter; `:reloadconfig` is the config-only
variant. A bundled starter config lives in `.configs/configs/`.

Built-in defaults include `explorer_width=25`, `minimap_width=15`,
`tab_size=2`, `show_line_numbers=true`, `relative_line_numbers=true`,
`cursor_style=bar`, `render_fps=120`, `idle_fps=60`, `auto_save=false`,
`auto_save_interval_ms=2000`, `lsp_change_debounce_ms=120`,
`lsp_inlay_hints=true`, `lsp_inlay_type_hints=true`, `terminal_height=10`, and
`debugger_height=12`.

See [THEMES.md](THEMES.md) for authoring colorschemes and
[LUA_API.md](LUA_API.md) for the scripting API.

## Platform notes

- The Linux/macOS UI and integrated terminal rely on POSIX terminal APIs
  (`termios`, `poll`, PTY/`forkpty`); Linux links `libutil` for PTY support,
  macOS uses its native PTY APIs.
- Windows uses a Win32 console backend for the editor UI and a ConPTY backend
  for the integrated terminal (Windows 10 1809+). ConPTY is loaded
  dynamically, so older systems fail gracefully rather than crash.
- The terminal emulator (libvterm) is bundled under `third_party/libvterm`, so
  POSIX and Windows share the same renderer with no platform dependency.

### Windows / MSVC (experimental)

```bat
cmake --preset windows-msvc-vs2026-vcpkg
cmake --build --preset windows-msvc-vs2026-vcpkg-debug
```

The generated solution lands at `build\vs2026-x64\jot.sln` — open it directly
or open the folder and pick the preset. Use 64-bit MSVC; the Windows target
does not support `-A win32`. Lua is fetched at a pinned version when missing,
and `libuv`/`utf8proc` are fetched automatically unless you provide them via
vcpkg (set `JOT_FETCH_DEPS=OFF` to opt out).

Installed files:

- `$prefix/bin/jot`
- `$prefix/share/jot/lua/`
- `$prefix/share/jot/configs/`

## Building from source

Requirements:

- CMake 3.16+
- A C++17 compiler
- Lua 5.3+ development headers (or let CMake fetch Lua 5.4.7)
- `vterm`, `termkey`, and `libuv` development packages
- A Unix-like environment with POSIX terminal APIs

Notes: the UI talks to the raw terminal (no ncurses); libtermkey decodes
keyboard input so modifiers and advanced shortcuts are reliable; the
integrated terminal uses PTY support (`forkpty` on POSIX, ConPTY on Windows)
with the bundled libvterm; libuv powers async I/O, timers, child-process
pipes, and file-tree notifications; Tree-sitter runtime support is optional
but recommended.

### Benchmarks

The benchmark suite is opt-in and separate from the tests, so normal builds
stay fast. It exercises non-interactive paths like line providers, folding,
text measurement, symbol extraction, and workspace search.

```bash
cmake -S . -B build -DJOT_BUILD_BENCHMARKS=ON
cmake --build build --target jot_benchmarks -j
./build/benchmarks/jot_benchmarks
```

Output reports per-case iteration counts and min/average/max runtimes in
milliseconds. Compare runs on the same build type and machine.

## Project layout

```text
apps/jot/        CLI entrypoint and executable target
benchmarks/      opt-in performance benchmark suite
cmake/           reusable CMake modules
include/jot/     public C++ API headers
src/jot/        editor state, buffers, panes, workspace, LSP, debugger, terminal
src/edit/        text editing, cursor movement, selection, clipboard, search
src/features/    syntax, folding, config, bracket helpers, C++ assist
src/input/       keyboard, mouse, command palette, command dispatch
src/render/      buffer drawing, minimap, overlays, panels, UI views
src/tools/       integrated terminal, DAP client, LSP client, search helpers
src/jot/lua/  C++ bridge for the embedded Lua plugin/theme API
src/ui/          raw terminal and UI abstraction
docs/            user-facing documentation
test/           unit tests
```

Build graph highlights: `jot_engine` is the aggregated static engine target;
`jot_core`, `jot_edit`, `jot_features`, `jot_input`, `jot_render`,
`jot_tools`, `jot_lua_bridge`, and `jot_ui` are the module libraries behind it.

## Notes and limitations

- The workflow is modeless by design — typing edits text directly, selection is
  mouse- or `Shift+Arrow`-driven, and common commands use standard Ctrl/Alt
  shortcuts.
- The integrated terminal suits normal shell/task workflows but is not meant
  to replace a full standalone terminal emulator.
- Lua globals are injected for plugins; no `require` ceremony needed.
- Windows support is experimental; Linux/macOS is the primary target.
