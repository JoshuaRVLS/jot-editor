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
included). The GUI frontend is optional: it compiles in when SDL2 + FreeType
are available (`JOT_GUI=OFF` to disable) and the terminal build is
unaffected.

The typeface and its size are independent settings:

```vim
:font                      " pick from the installed fixed-width families
:font Fira Code            " or name one directly
:font DejaVu Sans Mono     " tab-completes from what is installed
```

`gui_font_family` holds the choice (empty = the font jot ships with) and
`gui_font_size` the size in px; both apply live and are also in `:settings`
(Ctrl+,). A family is matched however it is spelled — `FiraCode`,
`fira-code` and `Fira Code` are the same font — and its own regular, bold,
italic and bold-italic faces are loaded together, so bold and italic match
the family rather than falling back to a different typeface. Families that
ship only some of those styles render the missing ones with their regular
face. Only fixed-width families are listed, since a proportional face cannot
fill a cell grid. Lookups read `~/.local/share/fonts`, `~/.fonts`,
`/usr/local/share/fonts`, `/usr/share/fonts` (plus the macOS and Windows font
directories), and an unknown name is reported and ignored, leaving the
current font in place. `JOT_GUI_FONT` still overrides the regular face with
an explicit font file.

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

### Colour preview

Colour literals are painted in the colour they name, in any file type (a port
of the idea behind [nvim-colorizer.lua](https://github.com/catgoose/nvim-colorizer.lua)).

Detected:

- **Hex**: `#RGB`, `#RGBA`, `#RRGGBB`, `#RRGGBBAA` and `#AARRGGBB` (QML's
  alpha-first order), `0xRGB` / `0xRRGGBB` / `0xAARRGGBB` (Android), and bare
  `RRGGBB` with no `#`.
- **Named**: CSS/X11 names (`red`, `WhiteSmoke`), and Tailwind class suffixes
  (`text-orange-500`, `bg-slate-50`).
- **CSS functions**: `rgb()`, `rgba()`, `hsl()`, `hsla()`, `hwb()`, `lab()`,
  `lch()`, `oklch()`, `hsluv()`, `hsluvu()` and `color()` (srgb, srgb-linear,
  display-p3, a98-rgb, prophoto-rgb, rec2020). Percentages, the modern
  space-and-slash syntax, and `deg`/`grad`/`rad`/`turn` angles all work.
- **Terminal codes**: the `#xNN` palette shorthand, and ANSI escapes in any of
  their spellings (`\e[38;5;208m`, `\x1b[48;2;R;G;Bm`, `\033[…`, or a real ESC),
  plus LS_COLORS/SGR snippets (`=38;5;196`, `=01;34`, `=48;2;0;0;255`).
- **Variables**: CSS custom properties (`--brand: #ff8800`, `--x: 240,198,198`)
  resolved through `var(--brand)`, and Sass variables (`$brand: #ff8800`)
  resolved through `$brand`, including chains between them.
- **LaTeX**: xcolor expressions such as `red!30` (30% red mixed toward white).

Only whole literals match: `#fff` inside `#ffffff`, or `red` inside
`text-red-500`, is not a colour. Alpha channels are ignored — the opaque colour
is what gets shown.

`colorizer_mode` picks how it is shown:

| Mode | Effect |
| --- | --- |
| `background` (default) | The literal's own background becomes the colour, with the text flipped to black or white by contrast. |
| `foreground` | The literal is drawn in the colour; the background is untouched. |
| `virtualtext` | The text is left alone and a swatch is appended after the line. |

The preview is painted over the syntax colours but *under* the selection,
search matches, diagnostics and the cursor, so it never hides what you are
working on. It applies to the GUI and the terminal alike; the terminal needs a
24-bit-capable one (`COLORTERM=truecolor`/`24bit`, or a `*-direct` `TERM`) to
show the exact colour, and otherwise falls back to the closest xterm-256 entry
— the `truecolor` setting can force either path.

Every format has its own switch, so you only pay for what you want
(`colorizer_hex`, `colorizer_hex_alpha`, `colorizer_hex_qml`,
`colorizer_hex_no_hash`, `colorizer_hex_0x`, `colorizer_names`,
`colorizer_tailwind`, `colorizer_xcolor`, `colorizer_functions`,
`colorizer_xterm`, `colorizer_ls_colors`, `colorizer_css_vars`,
`colorizer_sass`). The basic ones are on by default; the rest — including the
looser hex forms, whose bare `RRGGBB` and `0x…` shapes are easy to mistake for
identifiers — are opt-in.

Colours are parsed over the visible window only and memoised per line, so
minified one-line files stay cheap. Variable definitions are the one exception:
they are indexed over the whole buffer, and only rebuilt after an edit (or when
the file changes), never per frame.

Note that a definition in another file is not followed: `@import`-ed or Sass
`@use`-d variables resolve only when the file defining them is the one being
rendered. Upstream follows imports with a file watcher per import; jot
deliberately does not, so a preview never depends on a file you cannot see.

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

  Completion rows are built from the item itself (a port of the idea behind
  [colorful-menu.nvim](https://github.com/xzbdmw/colorful-menu.nvim)): the
  label is split into name, parameter list and type, and the type is
  right-aligned into a column measured across the visible rows, so a row reads
  like `parse_config(…) -> Result<Config>` rather than a bare name with
  everything else crowded into a narrow column. The name takes the kind's
  colour, the parameter list is dimmed, and the type uses the theme's type
  colour. Which field carries the type differs per server, so there is a small
  table of per-server rules (`runtime/lua/features/ui/completion_label.lua`)
  covering clangd, gopls, rust-analyzer, zls, lua-language-server, the
  pyright/pylance/basedpyright family, typescript-language-server/vtsls and
  intelephense, with a generic fallback for everything else. Three switches:
  `completion_rich_labels`, `completion_align_type` and
  `completion_dim_arguments`.

  Two notes: the type is only available when the server sends `detail` or
  `labelDetails` in the initial response — jot never sends
  `completionItem/resolve`, so servers that only fill those lazily show a plain
  name — and a server with no profile still gets a correctly coloured row, just
  with less split out.
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
  alive while hidden. Each tab is labelled with the shell icon (the same glyph
  the file tree uses for shell scripts), so the strip reads as a row of shell
  sessions.
- Drag the rule above the panel (the editor's bottom border) to resize it
  live; it can grow to nearly the full window. The panel has no frame of  its own: the pane area inks the separator along its top, so its first row is the
  `Terminal`/`Problems` view tabs (each labelled with an icon: a terminal for the
  shell, a warning triangle for diagnostics) rather than a second border.
- The Problems list keeps every row on the panel's own background, colouring
  only the text by severity; the selected row is marked with an accent sliver
  instead of a selection fill, so a highlighted row's message stays readable.
- **Fullscreen zoom** (like pane zoom): `Alt+Shift+Z` while focused, or
  `:termzoom`, toggles the terminal across the whole pane area. `Esc` exits
  fullscreen too. While zoomed, the sidebar and right dock are hidden so
  nothing overlaps the fullscreen terminal.
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

### Markdown preview

`:MarkdownPreview` renders the current markdown buffer in your browser through
a tiny loopback HTTP server, and keeps the page and the editor in step in both
directions:

- **Live refresh** — every edit (debounced) and every save re-renders the
  document and pushes just the new body to the open page over Server-Sent
  Events, so the scroll position and any diagram state survive the update. The
  full page is only replaced when the shell itself changes (title, theme, CSS).
- **Two-way scroll sync** — while both sides are open, the editor's top visible
  line is pushed to the page and the page's scroll position is pulled back into
  the editor, so scrolling either one follows the other. The cadence is
  `markdown_preview_refresh_interval` (ms).
- **GFM by default** — headings with slug anchors, paragraphs, nested ordered
  and task lists, tables with alignment, blockquotes and GitHub-style alerts
  (`[!NOTE]`, `[!TIP]`, `[!IMPORTANT]`, `[!WARNING]`, `[!CAUTION]`), fenced code
  with a copy button, `==mark==`, `++ins++`, `H~2~O`, `E=mc^2^`, footnotes,
  reference links, autolinks, raw HTML passthrough and horizontal rules.
- **Optional extras**, each a `markdown_preview_option_*` switch: `highlightjs`
  (default on), `code_copy` (on), `source_map` (on), plus `katex` math,
  `mermaid` / `plantuml` / `flowchart` diagrams, `echarts` and `vega` charts,
  `emoji` shortcodes and a floating `toc` panel.
- **Customisable page** — `markdown_preview_theme` (`dark`/`light`/`auto`),
  `markdown_preview_page_title`, `markdown_preview_custom_css` and a Lua
  `preprocessor(text, path)` hook that can rewrite the source before parsing.

Local images resolve relative to the document (honouring
`markdown_preview_images_path` for a prefix) and are served by the same server.
`markdown_preview_browser` picks the browser to open (`none` to just print the
URL, or any name from the built-in list; the platform opener is used when
unset), and `markdown_preview_auto_start` / `markdown_preview_auto_close`
tie the session to markdown buffers opening and closing.

The preview is driven from Lua — `jot.md.start/stop/toggle/refresh`,
`jot.md.url()`, `jot.md.is_running()` and `jot.md.setup{}` — over the native
`jot.preview.*` transport, so a plugin can re-implement or extend any part of
it. See [LUA_API.md](LUA_API.md#markdown-preview).

### Snippets

A full snippet engine, ported from LuaSnip's model. Snippets are matched
against the text before the caret, expanded in place, and then navigated
placeholder by placeholder with the caret — typing in one occurrence of a
tabstop updates every mirror.

- **Formats** — every snippet-text form the LSP and VSCode define: `$1`,
  `${1}`, `${1:default}`, `${1|one,two|}` choices, `${1/(.*)/\U\1/}` transforms
  (with `\u`/`\l`/`\U`/`\L`/`\E` and the `/upcase`, `/downcase`, `/capitalize`,
  `/camelcase`, `/pascalcase`, `/snakecase`, `/kebabcase` format field), and the
  `TM_*` / `LS_*` variables (`${TM_FILENAME}`, `${TM_LINE_NUMBER}`, …).
- **Packs** — Lua snippet files (`snip_env` with `s()`, `t()`, `i()`, `c()`,
  `f()`, `d()`, `r()`, `rep()`, `fmt()`, …), VSCode `.json` / `.code-snippets`
  and snipMate `.snippets` files, looked up under `~/.config/jot/snippets`,
  `<workspace>/.jot/snippets` and anything in `snippet_paths`.
- **Triggers** — plain word-boundary triggers, Lua-pattern triggers
  (`regTrig`), function triggers, `priority`, `condition` / `show_condition`,
  autosnippets, and the `extends` graph so a filetype can inherit another's
  snippets.
- **Keymaps** — `Tab` expands a trigger or jumps to the next placeholder,
  `Shift+Tab` jumps back, and `Ctrl+E` / `Ctrl+Shift+E` cycle a choice while the
  session is live. All four fall back to the editor's own behaviour when no
  snippet applies, so `Tab` still indents and `Shift+Tab` still outdents.
- **LSP** — when a server answers a completion with `insertTextFormat = 2`,
  jot hands the snippet text to the engine, so accepting it expands with real
  tabstops, mirrors and choices instead of a flat text insert.
- **Commands** — `:Snippets` (pick one for the current filetype), `:Snippet
  <trigger>` (expand a literal trigger), `:SnippetList`, `:SnippetReload`,
  `:SnippetToggle`.

Everything is available from Lua as `jot.snip` (constructors, registry,
`expand` / `expand_or_jump`, `jump` / `change_choice`, `env`, loaders), so a
plugin can register snippets at runtime or take over the engine. See
[LUA_API.md](LUA_API.md#snippets).

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
panels, and a two-row status/message area.

Borders mark where one region ends and another begins, and only there: a region
draws a line only on the side facing another region, so the sidebar and the
editor share a single `│`, a split has one separator rather than two, and a lone
pane has no frame at all — focus is shown by the cursor and the active tab, never
by the border. Floats (hover, completion, dialogs, telescope, the tool dock's
panels) sit over buffer content on every side, so they keep a full box. Everything
is drawn with flat corners; the colour comes from the theme's `WinSeparator` /
`FloatBorder` / `SidebarBorder` slots.

The mouse is wired throughout —
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

### The keymap grammar (operators, selection, next/previous)

Bindings with a family shape live behind a prefix whose letter says what it is,
and the sequence after it is typed through, with no menu prompting you (a bare
key with no action is a group title). The object letters are the same for every operator,
so the grammar is learnt once:

| Sequence | Action |
| --- | --- |
| `Alt+D` / `Alt+Y` | Delete / yank, then `I`/`A` (inside / around), then the object |
| `Alt+D W` / `Alt+D L` | Delete the word / the line (objects that need no inside/around) |
| `Alt+D A F`, `Alt+D I A` | Delete around a function, inside an argument |
| `Alt+Y A C`, `Alt+Y W` | Yank around a class, yank the word |
| `Alt+V` | Selection: `E` expand, `C` shrink, `K` keep primary, `R` rotate, `A`/`B` cursor above/below, `L` split lines, `M` match all occurrences, `S` select an object |
| `Alt+]` / `Alt+[` | Next / previous: `F` function, `C` class, `D` diagnostic (`Alt+E` remains an alias) |

Objects available today are `F` function, `C` class or type, `A`
argument/parameter, `W` word, `L` line — the syntax objects come from tree-sitter,
so they need a grammar for the file type.

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

**Markdown:** `:MarkdownPreview` `:MarkdownPreviewStop`
`:MarkdownPreviewToggle`

**Snippets:** `:Snippets` `:Snippet <trigger>` `:SnippetList` `:SnippetReload`
`:SnippetToggle`

**Debugger:** `:debug <program>` `:debuggdb` `:debuglldb` `:debugconfig`
`:debugattach <pid>` `:debugpanel` `:debugstop|restart|continue|pause`
`:debugstep|next|out` `:debugthreads` `:debugmemory` `:debugdisasm`

**Git:** `:gitpanel` `:gitcheckout` `:gitmerge` `:lazygit` `:gitstatus`
`:gitdiff` `:gitdiffstaged` `:gitstage` `:gitunstage`
`:gitstageall` `:gitunstageall` `:gitcommit <msg>` `:gitlog` `:gitblame`
`:gitrefresh`

**Discord:** `:discord` (status) `:discord enable|disable|reconnect|disconnect`
`:discord assets` (which artwork keys to upload) — Rich Presence showing the
file, language, workspace, git branch and (when the remote is a browsable URL) a
"View Repository" button, with idling / editing / debugging states and an idle
timeout. On by default; every row is a template (see `discord_details_*` in
`:settings`). Discord artwork has to be uploaded to your Discord application
once — `packaging/discord-presence/ASSETS.md` walks through it, and
`:discord assets` lists exactly which keys the current window needs.

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
`cursor_style=block`, `cursor_blink_ms=500`, `render_fps=120`, `idle_fps=60`,
`auto_save=false`, `auto_save_interval_ms=2000`, `lsp_change_debounce_ms=120`,
`lsp_inlay_hints=true`, `lsp_inlay_type_hints=true`, `terminal_height=10`, and
`debugger_height=12`. The colour preview adds `colorizer=true`,
`colorizer_mode=background`, `colorizer_hex=true`, `colorizer_hex_alpha=false`,
`colorizer_hex_qml=false`, `colorizer_hex_no_hash=false`,
`colorizer_hex_0x=false`, `colorizer_names=true`, `colorizer_tailwind=false`,
`colorizer_xcolor=false`, `colorizer_functions=true`, `colorizer_xterm=false`,
`colorizer_ls_colors=false`, `colorizer_css_vars=false`,
`colorizer_sass=false`, `colorizer_only_in_strings=false` and
`colorizer_exclude_filetypes=` (a comma separated list of file-name suffixes to
skip, e.g. `.min.css,.map`), plus `truecolor=auto` for 24-bit output. LSP
completion rows add `completion_rich_labels=true`, `completion_align_type=true`
and `completion_dim_arguments=true`.

The markdown preview adds `markdown_preview_auto_start=false`,
`markdown_preview_auto_close=true`, `markdown_preview_refresh_interval=100`,
`markdown_preview_markdown_ext`, `markdown_preview_port=0`,
`markdown_preview_host`, `markdown_preview_theme=dark`,
`markdown_preview_page_title`, `markdown_preview_browser`,
`markdown_preview_echo_preview_url`, `markdown_preview_custom_css`,
`markdown_preview_images_path`, `markdown_preview_open_timeout_ms`, and the
`markdown_preview_option_*` switches listed above.

The snippet engine adds `snippet_enabled=true`, `snippet_auto_expand=false`,
`snippet_tab_key=Tab`, `snippet_backtab_key=Shift+Tab`,
`snippet_choice_next_key=Ctrl+E`, `snippet_choice_prev_key=Ctrl+Shift+E`,
`snippet_highlight=true`, `snippet_history=true`, `snippet_history_size=32`,
`snippet_load_vscode=true`, `snippet_load_snipmate=true`, `snippet_paths=`,
and `snippet_filetypes=` (a comma-separated `ext=filetype` override list, e.g.
`.tsx=typescriptreact`).

The caret is configured with two keys:

- `cursor_style` — `block` (the default) or `bar`, both blinking; `steady_bar` /
  `steady_block` keep that shape without blinking. The shape is emitted with the
  terminal's steady DECSCUSR form in the TUI and drawn directly in the GUI.
- `cursor_blink_ms` — half of the blink cycle, in milliseconds: the caret is
  visible for that long, then hidden for the same. `0` makes it solid. The
  phase is jot's own clock, shared by the terminal and GUI frontends, and it
  restarts visible whenever you type or move the caret.

The mouse wheel can scroll smoothly. `smooth_scroll` is off by default — the
wheel jumps a notch per event, as it always has — and `smooth_scroll=true`
eases a notch over a few frames instead, using neoscroll.nvim's model:
the same easing functions (`smooth_scroll_easing`, `linear` by default, plus
`quadratic`, `cubic`, `quartic`, `quintic`, `circular` and `sine`), the same
per-notch duration (100ms) scaled by `smooth_scroll_duration_multiplier` (1.0),
and the same behaviour for a burst — a notch arriving mid-animation extends it,
reversing eases it to a stop, and a held wheel can never fall more than two
notches behind the viewport. It only moves the viewport, so the caret and every
other navigation path keep scrolling instantly, and moving the viewport another
way (a jump, a fold, the caret pulled back into view) ends the animation where
it stands. The GUI frontend already slides its content pixel by pixel and is
left alone.

The caret's colours come from the theme (`cursor`, or `fg_cursor`/`bg_cursor`).
Where the caret is painted by jot — the GUI — it uses whichever of the two
contrasts with the cell underneath, so it stays visible over comments,
selections and dimmed text. In the TUI the hardware cursor is drawn by the
terminal emulator, which owns its colour; jot only sets its shape and position.

`render_margin` (default `0`) is the number of columns left unpainted on the
right edge. `0` uses the full width, so full-width rules and the bottom bar end
on the last column. Raise it to `1` only if a terminal corrupts the frame when
its last column is written: the renderer already disables autowrap and addresses
each row with an absolute cursor move, so the wrap hazard the margin guarded
against cannot trigger on a conforming terminal.

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

### Hot-path rules

The renderer repaints the whole cell grid every frame, so anything it touches
per row or per cell is a per-frame cost. A few rules keep that honest:

- **Per-row scratch is reused, never reallocated.** Buffers and vectors the row
  loop needs (visual columns, inlay-hint rows, colour spans, search hits) live
  outside the loop and are cleared per row, so their high-water mark is
  allocated once rather than once per visible row.
- **Per-buffer properties are memoized, not recomputed per row.** Deciding
  whether a `.h` is really C++, or resolving a buffer's absolute git-status
  path, both touch the filesystem. Both are cached on the buffer and
  recomputed only when the file or its content changes.
- **Grapheme walks are O(1) per step.** The text helpers fast-path ASCII, which
  is what almost every cell holds; the UTF-8 decoder only runs for bytes it is
  actually needed for.
- **Per-line caches are bounded.** The syntax cache holds one entry per line ever
  highlighted, so it is capped and dropped wholesale when it passes the budget;
  only the viewport is re-highlighted, which is one frame of work.
- **GL uniform locations are resolved once** at link time, and batched geometry
  is written straight into the vertex buffer rather than through per-float
  appends.

## Project layout

```text
apps/jot/        CLI entrypoint and executable target
benchmarks/      opt-in performance benchmark suite
cmake/           reusable CMake modules
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
