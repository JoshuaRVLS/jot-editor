# jot - features, keybindings & commands

The full guide to what jot does and how to drive it. For the one-paragraph
pitch and install steps, see the [README](../README.md).

## What makes jot different

- **Modeless by default.** No insert/normal mode juggling - just type. Mouse
  selection, tabs, split panes, a minimap, a command palette, and a workspace
  sidebar are all there out of the box.
- **A native C++ core.** Buffers, panes, syntax highlighting, LSP, debugging,
  Git, terminal emulation, and workspace state all run in the core - no
  runtime scripting dependency drives the editor itself.
- **Tree-sitter highlighting** with on-demand grammar installation, status and
  reload commands, fallback queries, and rich theme slots for semantic tokens.
- **Native LSP** for diagnostics, completion, hover, go-to-definition, document
  symbols, and full server lifecycle management (including lightweight
  install/remove helpers).
- **An integrated terminal** with multiple shell tabs and a task runner for
  local and project-level commands.
- **A debugger panel** speaking the Debug Adapter Protocol - GDB and LLDB
  launch/attach flows, breakpoints, threads, stack, variables, memory,
  disassembly, and output.
- **Git workflows** for status, diffs, staging, unstaging, committing, log,
  blame, and refresh - all from inside the editor.
- **A built-in AI assistant** - a CodeCompanion-style chat buffer, inline
  rewrites previewed as diffs, and adapters for OpenAI-compatible endpoints,
  Anthropic and any CLI on `PATH`, all in bundled Lua over `jot.job`.
- **Lua plugins and JSON colorschemes** for customization, with behavior owned
  by the C++ core. The shipped schemes (`jot` and the Flexoki pair) are exact
  24-bit palettes authored in hex; a theme file may also
  use xterm-256 indices, or mix the two.

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
keybindings, LSP, panes and Lua UI - only the screen differs. Rendering is
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
(Ctrl+,). A family is matched however it is spelled - `FiraCode`,
`fira-code` and `Fira Code` are the same font - and its own regular, bold,
italic and bold-italic faces are loaded together, so bold and italic match
the family rather than falling back to a different typeface. Families that
ship only some of those styles render the missing ones with their regular
face. Only fixed-width families are listed, since a proportional face cannot
fill a cell grid. Lookups read `~/.local/share/fonts`, `~/.fonts`,
`/usr/local/share/fonts`, `/usr/share/fonts` (plus the macOS and Windows font
directories), and an unknown name is reported and ignored, leaving the
current font in place.`JOT_GUI_FONT` still overrides the regular face with an
explicit font file.

In a terminal there is no font of jot's own to size -- the emulator owns the
glyphs -- so the zoom is asked of the terminal instead: `tui_text_scale` (1-4,
`:textzoom in|out|reset|N`) has the grid drawn in cells that cover that many
terminal cells each, through the text sizing protocol kitty 0.40+ implements,
and the whole UI scales together -- buffer, panels, status line -- without the
terminal's own size changing at any point. `Alt+=` / `Alt+-` step it while the
editor is up (`Ctrl+=` / `Ctrl+-` too, where the terminal reports those chords),
and `Alt+0` puts it back to 1:1. The wheel is not a zoom chord: `Ctrl+wheel`
scrolls the viewport sideways, the way `Shift+wheel` does. A terminal without
the protocol keeps the screen at 1:1 and says so rather than painting a grid it
cannot fill; `JOT_TEXT_SCALE_FORCE=1` forces the zoom anyway, for a terminal
whose support the probe cannot see (a multiplexer that strips the protocol, a
diagnostic run).

The GUI also shapes what the font joins. A programming face inks `->` and `!=`
through its own OpenType lookups, and a cell grid that resolves one codepoint
at a time never asks for them, so those runs are shaped as runs instead (with
HarfBuzz, the same shaping a terminal does) and painted as the glyph the font
returned - a ligature where the font has one, its connected alternates where it
has not. Only runs of adjacent punctuation sharing one colour are candidates;
identifiers, CJK and a colour seam between two spans keep the per-cell path.
`gui_ligatures` (on by default, also in `:settings`) turns it off, and a build
without harfbuzz simply never had it: `:settings` reports that rather than
swallowing the toggle.

Scrolling is a pixel slide, not a jump: a wheel notch retargets the slide and
the content carries on from the velocity it already has, so a burst reads as
one glide and turning the wheel around decelerates through the stop rather than
kicking the content back. The panel is reported every frame, so even the first
notch of a session has a frame to slide from, and only a step too big to be a
scroll (another buffer opened over the pane, a fold flip, a resize) snaps.

The frontend keeps what it allocates small: the quad batch is a 256 KiB working
set that flushes when it fills instead of a buffer sized for the largest grid it
might draw, the glyph atlas lives on the GPU alone (every glyph is uploaded from
FreeType's own bitmap), and the terminal renderer's row diff baseline is never
allocated. A slide retains its viewports while it moves and drops them as it
settles, so scrolling a long way does not grow the process.

## Feature tour

### Editing

- Modeless text entry - typing edits immediately.
- Undo/redo, copy/cut/paste, select all, mouse selection, double-click word
  selection, triple-click line selection.
- Saves that leave the file whole: the bytes go to a temporary beside the file
  and are renamed over it, so a full disk or a killed process cannot truncate
  what was on disk. Trailing whitespace comes off on the way out
  (`trim_trailing_whitespace_on_save`, on by default), in the buffer as well as
  in the bytes; a file where those spaces are content (markdown's hard line
  break) keeps them, and `:trim` still cleans it on request.
- Saves that rebase indentation on `tab_size` with the editor's own block
  rules (`reindent_on_save`, on by default), no external formatter involved: a
  body written one level out is pulled back in, and a file at another width
  lands at the configured one. The languages are the C family, Lua, Python,
  JavaScript and TypeScript (JSX included), Java, C#, Kotlin, Scala, Dart,
  Groovy, Go, Swift, Rust, Zig, proto, thrift, CSS/SCSS/LESS and JSON; a
  language whose blocks are keywords or tags (Ruby, shell, the markup formats)
  has no brace rules here and is left alone. The file's first line is the
  baseline, so a fragment keeps its column; a wrapped expression keeps the
  column its author gave it, a braceless body keeps its step in, and a line
  inside a multi-line string or comment is left alone. Both cleanups are edits
  like any other, so an undo takes the save's own rewrite back first: Ctrl+Z
  after a save that reindented (or trimmed) a file restores the text as it was
  written while the file keeps what the save put on disk, and the buffer then
  reads as modified because the two disagree. A save that changed no text takes
  no step, and on-save formatting (`prettier_on_save`,
  `clang_format_on_save`) works the same way.
- Smart multi-line paste that re-indents to the cursor.
- Auto-indent, bracket matching and jumping, rainbow bracket colors
  (`rainbow_brackets`), an active bracket guide, and a highlight on the bracket
  under the caret together with its partner (the `BracketMatch` theme group). The
  indent guides (`show_indent_guides`) are hairlines mixed into the row's own
  background -- the active scope's guide a shade firmer than the rest -- so the
  indent column reads as a continuous line rather than a grid of bars.
- The identifier under the caret is highlighted at every other place it
  appears in the viewport, VS Code's occurrence highlight: the caret's own word
  wears the strong band and the rest of the uses the plain one, from the
  `WordHighlight` and `WordHighlightStrong` theme groups. Only whole words
  count, so the `int` inside `printf` is not a use of `int`; a caret parked one
  cell past a word still answers for that word, the same rule the word motions
  use. A selection is the answer while one is up: the text it marks is what the
  other occurrences are found from, matched as the text it is (so a prefix being
  renamed lights the identifiers that carry it) rather than as a whole word. The
  highlight also leaves a row that carries a diagnostic band alone, which stays
  edge to edge rather than having a word clipped out of it, and
  `word_highlight=false` turns it off. While you are typing, the tint holds
  back until the typing pauses (`word_highlight_delay_ms`, VS Code's
  occurrencesHighlightDelay): a highlight that re-lights on every keystroke's
  repaint is noise, so it waits for the pause and the frame loop asks for the
  repaint that ends the wait. Moving the caret or opening a file is not typing,
  so the highlight comes up at once there; `0` restores paint-on-every-frame.
- Smart indent on Enter. A bracket left open on the line lines the new line up
  under the argument after it, so a wrapped call stays aligned:
  `res = call(arg,` continues at `arg`'s column. In the C family a control
  statement does not indent on its own — its `{` belongs on the next line at the
  statement's own indent (the brace style the project's clang-format enforces) —
  while a label (`case 1:`, `public:`) introduces the body under it. Typing one
  of those labels, or a `#` that starts a preprocessor directive, pulls the line
  back out to where clang-format would put it (`IndentCaseLabels: false`, the
  access modifier's offset, column 0). Brackets inside a string or a comment are
  text and decide nothing.
- Auto-closing pairs, at every caret: typing `(`, `[`, `{`, `"` or `'` opens
  the pair and leaves the caret inside it, typing the closer when it is already
  there steps over it instead of doubling it, and Backspace between an empty
  pair removes both. A selection wraps instead of being replaced. A quote pairs
  only where it opens a string: not inside one already open, so a closing quote
  after a brace stays single, and not beside a word (`don't`, `word"`).
- Markup tags (`.html`, `.htm`, `.jsx`, `.tsx`): typing `>` on an opening tag
  writes the closing tag for you, Enter between a pair splits it onto three
  indented lines, and renaming a tag - from either end, including a name being
  retyped from empty - carries the partner tag along with it.
- Emmet abbreviations on Tab (`div.card>ul>li*3`, `m10-20`), in markup and
  style sheets (see the `emmet` setting below).
- The workspace's own CSS vocabulary, offered as you type: every class name the
  tree's markup and style sheets use, completed inside `class="..."` /
  `className="..."`, and every custom property they declare, completed inside
  `var(--)`. The scan runs on a worker thread once per workspace open and again
  on every save; the names are filtered by the same fuzzy pass as an LSP response
  and are offered *alongside* whatever a server answers, so a plain HTML/CSS
  project gets completions for its own vocabulary with no server installed.
- Line helpers: duplicate, delete, move up/down, join; trim trailing
  whitespace or blank lines; uppercase/lowercase; sort/reverse/deduplicate/
  shuffle selected lines.
- Case-sensitive, whole-word, and regex replace; surround/unsurround;
  increment/decrement the number under the cursor; format document; word-wise
  deletion; smart line-start movement; file/page movement.

### Undo tree

- `:Undotree` (`Alt+Shift+U T`) opens the buffer's history in the right dock:
  every state the buffer has been in, newest first, with the state it is on
  starred and how long ago each one was reached. It is the
  [undotree](https://github.com/jiaoshijie/undotree) idea, written in the
  bundled Lua runtime on top of the editor's own history.
- The history is a *tree*, not a pair of stacks: editing after an undo no longer
  throws the other branch away. The branch you undid past stays in the list (it
  ends in an `o`), and going back to it is a jump, not a retype. `Ctrl+Y` still
  redoes along the newest child; the panel is for the branches it cannot reach.
- Nothing moves until you say so. `j`/`k` put a cursor on a state without
  touching the buffer, `g` `j` puts the cursor on that state's parent, and the
  lines the state under the cursor would bring back are listed under it (`-` for
  what the buffer holds now, `+` for what the state holds). `Enter` (or a click
  on the row) is what walks the buffer there, one step per state between, so the
  buffer lands on exactly the text walking there would have produced.
- `J` and `K` move the buffer itself, applying the state they land on: `K`
  steps back to the parent, `J` forward to the newest child, so the tree's undo
  and redo are the editor's own. `S` re-reads the history, `q`/`Esc` closes the
  tab. These are the [undotree](https://github.com/jiaoshijie/undotree) keys
  that have a counterpart here; its `p` switches to a separate diff window,
  which this panel has no need for because the changes are listed in it.
- The dock holds the keyboard while the panel is up, and hands back every key
  it does not use: typing still edits the buffer with the tree on screen. A
  prompt or the command palette opened on top of it keeps its own keys.
- It is a Lua feature like any other
  ([runtime/lua/features/undotree.lua](../runtime/lua/features/undotree.lua)):
  the states come from the `jot.undo` namespace
  ([Lua API](LUA_API.md#undo-tree)), so a plugin can draw its own view of the
  same history, and the panel's keys arrive as ordinary panel events.
  `:UndotreeClear` drops the buffer's history.

### Macros

- A macro is the *keystrokes* between its start and its stop, filed under a
  register and replayed through the same key path a live key takes -- so a
  replayed chord is matched by whatever the keymap says when it is replayed (a
  rebinding of your own included), and the edits land in whatever buffer is
  current. It is not a recording of edits: play a register over another file and
  it does its work there.
- `Alt+Z q` starts a recording and stops it again, `Alt+Z p` plays the last
  register back, and `Alt+Z l` lists the registers with their lengths. On the
  command line the same state is `:macrorecord [a-z]`, `:macrostop`,
  `:macroplay [a-z] [count]` and `:macrolist`, so a register can be replayed
  three times over with `:macroplay z 3`. A build without Lua gets the three
  chords from the native prefix in `input/modes/dispatch.cpp`.
- Registers are the letters `a`-`z`, each holding its own list, and starting a
  recording on one throws away what it held: a fresh take, not an append. An
  empty register reports that instead of replaying something else, and the status
  line names the register being recorded and then how many keys it took. The
  recorder's own chords stay out of the recording -- otherwise playing a macro
  would stop the recording it was played from -- a play inside a play is refused
  rather than left to recurse, and a play that somehow loops stops at a reported
  keystroke ceiling instead of freezing the editor.

### The status-line prompt (the minibuffer)

- The prompt sits on the editor's own status row, Emacs' minibuffer: Enter hands
  the line over, Esc abandons it, Tab completes, the arrows move the caret inside
  what has been typed, and every printable byte extends it. The prompt owns the
  keyboard while it is up, so a stray chord cannot edit the buffer underneath the
  line being typed on. `:isearch`, `:findfile` and the `:compile` prompt are the
  three that use it -- the ex commands are the whole way in for a build without
  Lua -- and `Alt+Q s` / `Alt+Q f` / `Alt+Q c` are the chords onto them
  (`Alt+Q k` stops a running build).
- Incremental search walks the buffer as the query is typed: each keystroke
  restarts from where the search began, so deleting a character walks back to the
  shorter query's match, while `Ctrl+S` and `Ctrl+R` move to the next and previous
  hit without touching the query. `Alt+R` switches the pattern between literal and
  regexp and the label says which is on, and a query with an uppercase letter is
  matched case-sensitively -- Emacs' own rule. The hit wears the `CurSearch` band,
  the view scrolls to reach it, and Esc puts the caret back where the search
  started.
- Find file takes a path completed from the directory typed so far: Tab extends
  to the shared prefix and then walks the candidates, a directory candidate is
  offered with its trailing slash, and dotfiles appear only once the dot is typed.
  A relative path is resolved against the workspace root, so `src/render/frame.cpp`
  means the file in this project.
- `:compile` with no argument opens the same prompt pre-filled with the last
  command (or `build_command` from the config) and hands the line to `/bin/sh`
  exactly as typed; see Compilation above.

### Buffers, tabs, and panes

- Any number of open buffers, saved or unsaved.
- Pane-local file tabs with click-to-switch and close buttons, plus reopen of
  the last closed tab. In a split every pane draws its own strip
  (`pane_tabs` = `auto` / `always` / `off`); drag a tab onto another pane's
  strip to move it there, against a pane edge to split that pane around it, or
  within its own strip to reorder.
- Split panes in any direction; move focus or resize with keyboard or mouse.
- A breadcrumb winbar over every code pane: the workspace root, the folders
  down to the file, then the symbols enclosing the caret. Every crumb opens its
  own drop-down, and a folder row opens the folder's listing beside it as a
  cascade (`winbar` = `off` / `auto` / `on`).
- Open, save, save-as, close, quit (with force variants), and autosave with a
  configurable interval. The save-as, rename and quit prompts are modal panels
  over the dimmed editor: the buffer behind one stays visible (dimmed) while the
  panel owns the keyboard and the pointer, and the quit panel spells out its
  keys (`(y/n)`).

### Workspace and sidebar

- The workspace root is the folder you launched (or your current directory).
- A sidebar file explorer with tree indent guides: folders before files, a
  hidden-file toggle, expand/collapse all, reveal the active file (`E`),
  refresh, and parent-folder navigation.
- Create, rename, and delete files and folders from the tree.
- Recent files and workspace resume, plus a home menu for recent entries and
  quick actions.
- The home screen opens projects, not just recents: it lists the folders that
  look like projects around the launch directory (a repository or build marker
  is enough) and the folder you are in, newest first, and prints the key that
  opens each row at the right edge of that row. Type to filter the whole screen
  on a row's name, its folder or the age it was last touched (`/` starts a
  query with a letter that is also a shortcut, `Esc` takes it back), `Enter`
  opens the top match, and `f` pins the selected folder or file to the top of
  its list so it stays there even after the marker that found it is gone. The
  header is a three-row block wordmark, one accent per letter; the lists lead
  every row with the same per-language icons the explorer and the status line
  use, each in its own color, and the section titles carry a glyph and an
  accent too. A grid too short for the big mark falls back to a single-line
  wordmark so the rows stay with the lists. A bare `jot` starts here, and
  `:resume` still reopens the last workspace on demand.
- C++ assist: create matching header/source pairs, or generate missing source
  implementations from declarations.

### Search and navigation

- Per-buffer search with case/whole-word/regex options, and a replace panel
  (current match or all).
- Selection-scoped replace via `Ctrl+Shift+F` - or project-wide search when
  nothing is selected.
- Go to line, bookmarks, and a fuzzy file finder (telescope) with mouse
  support. A bookmark is a line marked in the buffer (`Alt+C B`, `:bookmark`)
  and walked with `Alt+] B` / `Alt+[ B` (`:bookmarknext` / `:bookmarkprev`),
  wrapping at the ends like the diagnostic and compile walks -- the per-file
  counterpart of the jumplist, which remembers where you were where a bookmark
  remembers where you will go back to. The picker is two straight-bordered
  boxes on one background: the
  result list on the left -- files only, each row the file's own path from the
  picker's scope, behind a `>` pointer and a band on the selected row -- and a
  small editor showing the selected file on the right, with line numbers. Each
  box names itself in its top border (`Results`, `Preview`), the finder and the
  folder it is scoped to are centred in the list box's foot, and the prompt --
  with `N / M` at its right end -- is that box's last interior row, under the
  rows it filtered. The picker's background stops at its borders: the column
  between the two boxes keeps the float's own colour, so they read as two panes
  rather than one slab with two frames in it. The selection band follows hover
  and click, so the row under the pointer is the one the view shows.
- Picker for project-wide text search, diagnostics, and document symbols, plus
  a persistent outline panel (`:outline`).

### Harpoon

- `Alt+Shift+H a` marks the file in front on the project's list, `Alt+Shift+H m`
  shows the list, and `Alt+Shift+H 1`..`Alt+Shift+H 9` jump to a slot: the
  [harpoon](https://github.com/ThePrimeagen/harpoon) idea, written in the bundled
  Lua runtime on top of `jot.file` and `jot.cursor`. `Alt+Shift+H n` /
  `Alt+Shift+H p` step through the marks and clamp at the ends,
  `Alt+Shift+H d` drops the file in front, `Alt+Shift+H c` empties the list. The
  same work is on the command line as `:HarpoonAdd`, `:HarpoonMenu`,
  `:HarpoonJump <n>`, `:HarpoonNext`, `:HarpoonPrev`, `:HarpoonRemove` and
  `:HarpoonClear`.
- A mark is a file *and* the position it was last left at, so a jump lands where
  you were rather than at the top of the file. Marking a file that is already on
  the list refreshes that position instead of adding a second row for it.
- The menu is a float over the pane: the slot, the path relative to the project,
  and the line when it is past the first. `j`/`k` or the arrows move, `Enter` and
  the digits jump, `d` drops the row under the cursor, `q`/`Esc` closes, the
  wheel scrolls and a click jumps to the row it lands on.
- Lists are per project, keyed by the workspace root, and kept in the cache
  directory the other Lua features use (`jot/harpoon/lists.json` under
  `$XDG_CACHE_HOME`, `%LOCALAPPDATA%` on Windows), so the next session opens with
  the same marks. The whole thing is also on `jot` as
  `jot.harpoon.list("term"):add()`, for chords of your own and for more than one
  list ([Lua API](LUA_API.md#harpoon)); `harpoon_nav_wrap`
  makes `prev`/`next` wrap instead of stopping at the ends.
- It is a Lua feature like any other
  ([runtime/lua/features/harpoon.lua](../runtime/lua/features/harpoon.lua)): the
  marks live in a module the runtime loads, so a config can rebind the chords
  freely. harpoon2 refreshes a mark on `BufLeave`, which this editor does not
  have, so the position follows `CursorMoved` instead and is written out with the
  next change to the list, when the menu closes, and on `Shutdown`.

### Syntax highlighting and folding

- Tree-sitter highlighting when the runtime and grammars are available, with a
  regex fallback for common file types.
- Commands to install grammars, inspect status, and reload parser/query caches.
- Grammar revisions are pinned. `runtime/lua/treesitter/lockfile.lua` carries the
  commit nvim-treesitter last verified each grammar at (167 of the 180 registry
  languages; the rest are forks it does not ship), and `:tsinstall` builds that
  commit rather than whatever the default branch holds that day. Two machines
  installing the same language get the same parser, and a grammar repository
  that lands a broken release cannot break an install here. `:tsupdate`
  rebuilds every installed parser at its pin (`:tsupdate <language>` for one),
  and for a grammar with no upstream pin it says so instead of pretending the
  default branch is an update. The pinning data is generated, not hand-written:
  `tools/treesitter_lockfile_import.py` rewrites it from upstream's lockfile.
- `:tsplayground` opens the parse tree around the cursor line, and the five
  query kinds a language can ship (highlights, folds, indents, injections,
  textobjects) asked what they match there, each entry carrying the byte range
  the query reported. A missing colour, a fold that ignores a block or an
  indent that lands in the wrong column then has its answer on screen: a
  section reading `no query file` means the language has no such query
  installed, `none` that the query matched nothing on that line.
- Rich token mapping (variables, parameters, types, functions, keywords,
  operators, etc.) with theme slots for both classic and Tree-sitter captures.
- The query language as the files in the grammar repositories actually write it,
  not just bare node patterns. Predicates decide whether a pattern's match
  counts at all (`#eq?`, `#match?` with a regex, `#lua-match?` with a Lua
  pattern, `#contains?`, `#any-of?`, `#kind-eq?`, `#has-parent?`,
  `#has-ancestor?`, plus the `any-` and `not-` forms), directives move and rank
  the captures that do (`#offset!` in rows and columns, `#set! priority`
  pattern-wide or for one capture), and a file's modeline says how it merges
  with the query around it: `;; extends` adds its patterns to the one it would
  replace, `;; inherits: a,b,(c)` pulls those languages' queries in ahead of its
  own, and a parenthesized language is skipped when it has no query. A predicate
  this build does not know is treated as satisfied rather than as a failure, so
  a query file written for another editor still highlights.
- Embedded languages: a language's `injections.scm` names the nodes that hold a
  different language -- a SQL string in Python, CSS in HTML, a shell here-doc, a
  fenced code block in Markdown -- and that text is parsed with its own grammar
  and coloured by its own query, over the host's colours inside the region. The
  language can be a literal (`(#set! injection.language "sql")`), the text of an
  `@injection.language` capture (a fenced block's info string, a variable's
  name) or an `@injection.filename` capture's extension. A region whose language
  has no parser installed keeps the host's colouring. Work stays with the
  visible line: only the regions touching it are parsed, and a line's colours
  are cached, so a long block costs one parse per line rather than one per
  frame. Regions named by `(#set! injection.combined)` are parsed one by one
  rather than as one document.
- Query-driven indentation: a language's `indents.scm` decides where the line
  Enter creates starts, in place of the text rules. The grammar knows what the
  text heuristics have to guess - the line after a control statement or a
  branch steps in, the line after the block's closer steps back out, a line
  inside a multi-line argument list keeps the indentation it was given - and the
  file names those nodes (`@indent.begin`, `@indent.branch`, `@indent.end`,
  `@indent.auto`, `@indent.dedent`, `@indent.zero`), with `(#set! indent.immediate)`
  and `(#set! indent.start_at_same_line)` for a block written before its body
  exists. `@indent.align` is not applied: a line an aligned block would place at
  its opening delimiter's column gets its enclosing block's level instead.
- Syntax objects from the language's `textobjects.scm`: which node is the
  function, the class, the parameter, the comment, the statement, and what each
  one's inside is. The query names them (`@function.inner` / `@function.outer`,
  and Helix's `@function.inside` / `@function.around` spellings), so a language
  the bundled table has never heard of still has working text objects. The same
  query drives select-inside/around, delete/yank-inside/around and the
  next/previous class walk and next/previous function jump; a language with no
  query keeps the bundled node-name table.
- Code folding with toggle/collapse/expand, mouse toggling, and persisted
  collapsed ranges. A language's `folds.scm` (the same file nvim-treesitter
  ships, read from the query paths `:tsinstall` writes to) decides the ranges
  when there is one, so constructs the text does not spell with braces or an
  indent step -- a multi-line string argument, a table literal, a here-doc --
  fold like the grammar says they should; without one, the bracket/indent
  detector keeps folding as before. The keyboard has it twice over: `Alt+C Z` then `A` toggles,
  `C` folds, `O` unfolds, `M` folds every block and `R` unfolds every block,
  while `Alt+C Shift+Z` toggles the block at the caret in one chord -- the one
  pressed while editing rather than while skimming. A file can also be skimmed
  by its structure instead of folded: `Alt+C O` opens the outline panel and
  `Alt+C S` the symbol picker.

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
`text-red-500`, is not a colour. Alpha channels are ignored - the opaque colour
is what gets shown.

`colorizer_mode` picks how it is shown:

| Mode | Effect |
| --- | --- |
| `background` (default) | The literal's own background becomes the colour, with the text flipped to black or white by contrast. |
| `foreground` | The literal is drawn in the colour; the background is untouched. |
| `virtualtext` | The text is left alone and a swatch is appended after the line. |

The preview only runs in web-authoring filetypes - by default `.html`, `.htm`,
`.jsx`, `.tsx`, `.css`, `.scss`, `.sass`, `.less`, `.mdx`, `.vue`, `.svelte`,
`.astro` and `.php` - because a hex literal in a C++ comment or a log line is
rarely a colour anyone wants painted. `colorizer_filetypes` replaces that list
(file-name suffixes, like `colorizer_exclude_filetypes` below); an empty value
restores previews everywhere.

The preview is painted over the syntax colours but *under* the selection,
search matches, diagnostics and the cursor, so it never hides what you are
working on. It applies to the GUI and the terminal alike; the terminal needs a
24-bit-capable one (`COLORTERM=truecolor`/`24bit`, or a `*-direct` `TERM`) to
show the exact colour, and otherwise falls back to the closest xterm-256 entry - the `truecolor` setting can force either path.

Every format has its own switch, so you only pay for what you want
(`colorizer_hex`, `colorizer_hex_alpha`, `colorizer_hex_qml`,
`colorizer_hex_no_hash`, `colorizer_hex_0x`, `colorizer_names`,
`colorizer_tailwind`, `colorizer_xcolor`, `colorizer_functions`,
`colorizer_xterm`, `colorizer_ls_colors`, `colorizer_css_vars`,
`colorizer_sass`). The basic ones are on by default; the rest - including the
looser hex forms, whose bare `RRGGBB` and `0x…` shapes are easy to mistake for
identifiers - are opt-in.

Colours are parsed over the visible window only and memoised per line, so
minified one-line files stay cheap. Variable definitions are the one exception:
they are indexed over the whole buffer, and only rebuilt after an edit (or when
the file changes), never per frame.

Note that a definition in another file is not followed: `@import`-ed or Sass
`@use`-d variables resolve only when the file defining them is the one being
rendered. Upstream follows imports with a file watcher per import; jot
deliberately does not, so a preview never depends on a file you cannot see.

### TODO comments

TODO-style comments wear the colour of what they mean (a port of
[todo-comments.nvim](https://github.com/folke/todo-comments.nvim)):

- The keyword alone gets a background chip in the family colour, the colon is
  hidden, and the text after the colon is re-inked in the same colour, so a
  comment reads `TODO`, `FIXME`, `HACK`, `WARN`, `PERF`, `NOTE` or `TEST` at a
  glance. The chip's own text uses whichever of the theme's text and background
  colours contrasts most with it.
- A comment run carries its colour onto the following comment lines, and only
  text the syntax highlighter calls a comment is painted - `TODO:` inside a
  string stays plain, and the run stops at a line the highlighter cannot
  confirm (a file type the fallback has no rules for still gets the keyword
  chip, but not an inked line under it).
- `Alt+] T` / `Alt+[ T` jump to the next/previous comment (the same family as
  function, class and diagnostic jumps).
- `:Todo` lists every TODO-style comment across the workspace in the picker,
  with file, line and the comment text, each row inked in its comment's family
  colour; `:Todo keywords=TODO,FIX` narrows the list to those keywords, and
  picking a row opens the file at the comment.

The colours come from the live theme (`error`, `warning`, `info` and `hint`
map to the diagnostic groups; a custom keyword joins the default family), so a
colour-scheme switch re-inks the chips instead of leaving the old palette on
screen. Upstream's keyword set is kept whole, including the aliases (`FIXME`,
`ISSUE`, `XXX`, `OPTIM`, `INFO`, `PASSED`, …).

`todo_comments=true` (the default) keeps the highlighting on - set it to
`false` to turn it off. `todo_comments_keywords` (comma separated) adds
keywords of your own, each taking the default family colour, the way upstream's
`merge_keywords` does.

### LSP

- One language server per language per workspace root, driven natively from
  C++ (no Python glue).
- C and C++ are handed their project's compile flags. clangd finds a
  `compile_commands.json` beside the sources itself; jot also points it at the
  database a configure leaves in a build directory (`build/`, `out/`, …), which
  clangd does not look in, and, when a workspace has no database at all,
  generates one under jot's data directory (C++20 and gnu17 per file, the
  workspace's `src/` and `include/` on the include path). Without flags a server
  parses with the compiler's default standard, where valid modern C++ reads as
  syntax errors - a constrained member as a missing `;`, a concept as an unknown
  type - and the project itself is never written to.
- Debounced file sync, diagnostics overlay, and next/previous diagnostic
  jumps. A line that holds an error or a warning is drawn three ways: the
  squiggle over the reported range, the line number in the severity colour, and
  a band across the whole row (`DiagnosticError`/`DiagnosticWarn`'s `bg`, when
  the theme sets one) from the gutter out past the end of the text, so a
  finding is findable while scrolling instead of only where the squiggle sits.
  Its message rides the end of that line in the severity colour -- the message
  alone, with no severity icon in front of it. What a server finds while you
  type waits for the typing to pause (`lsp_diagnostics_quiet_ms`, default 700;
  `0` paints each answer the moment it arrives), because the squiggle, the band
  and the message landing under the hands on every publish is noise -- and a
  save paints the freshest findings at once, since Ctrl+S is the user asking
  for the truth now.
- Completion with fuzzy filtering and `textEdit` support, including the
  `additionalTextEdits` an item carries: accepting a symbol whose file does not
  import it yet writes the import with it (typescript/vtsls auto-import, and
  clangd's `#include`). The edits are positions in the document the request saw,
  so they are remapped across the insert - an edit below the caret follows the
  lines the insert added, and one that would land inside the text being
  completed is dropped rather than written at a guessed column. A function
  completion is written as an empty call: accepting `add` inserts `add()` with
  the caret between the parentheses, so the parameters are yours to type instead
  of the server's argument placeholders (whether the call arrives as a snippet
  or as plain text). A declaration-shaped item (a body after the parens,
  anything that is not a call) keeps its text.
- Hover on demand or
  on mouse hover; go-to-definition with a return stack (`:lspback`), from the
  keyboard or from `Ctrl+click`; LSP
  rename across all affected files (`:lsprename <new_name>`); find
  references in a jumpable quick-pick list (`:lsprefs`); code actions
  (quick fixes and refactors) offered at the cursor (`:lspactions`).
- The whole location family, not just the definition: `:declaration`
  (usually the header), `:typedefinition` (what an `auto`, a typedef or a
  template instantiation really is) and `:implementation` (the override
  body) all land like a definition does -- same preview tab, same return
  stack. `:switchheader` flips between a C++ source and its paired header
  using clangd's `switchSourceHeader`, and reports "no paired
  header/source" rather than guessing. All four are in the `Alt+C` family.

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
  `labelDetails` in the initial response - jot never sends
  `completionItem/resolve`, so servers that only fill those lazily show a plain
  name - and a server with no profile still gets a correctly coloured row, just
  with less split out.
- Signature help popup, with the parameter the caret is filling highlighted on
  the label and a footer counting the overloads or the position in the
  parameter list. Both spellings of the active parameter are read, since clangd
  reports it on the result rather than on the signature. Plus clangd-style inlay
  hints on already-written
  code: parameter names (`a: 1, b: 2`) before arguments and type hints after
  variable declarations (`auto x = 5` shows `x: int`), both as dimmed
  virtual text that shifts the line right as in VS Code. Hints refresh after
  edits and follow scrolling; disable all hints with
  `jot.config.set("lsp_inlay_hints", false)` or just the type hints with
  `jot.config.set("lsp_inlay_type_hints", false)`.
- Document symbols via LSP with regex fallback - as a picker or a persistent
  outline.
- Status, start/stop/restart, a manager, and install/remove helpers for 100+
  servers (`:lspinstall <server>`, `:lspremove <server>`). One global switch,
  `lsp_enabled` (default true, a row under Code intelligence in `:settings`):
  off stops every running server and keeps new ones from attaching until it is
  turned back on, which re-attaches the buffers already open. It is the one
  setting for a machine that cannot spare the servers, or a tree where they
  should not run at all.
- **Windows installs use the catalog too**: there is no POSIX shell there, so
  every manager family in the catalog renders a second script out of cmd.exe
  steps - `github`, `payload`, `npm`, `pypi`, `golang`, `cargo`, `nuget`, `gem`,
  `opam`, `composer`, `luarocks`, `openvsx` and `generic` - built on `curl.exe`
  and `tar.exe` (both shipped with Windows 10) and run as a batch file so a
  failed step aborts the install instead of reporting success. Servers land in
  `%LOCALAPPDATA%/jot/lsp/bin`. A managed bin that is a `.cmd` launcher, which is
  what npm publishes, is spawned through `cmd /c`, the only way Windows runs
  one; the package managers themselves, which are often batch shims there, are
  invoked so the install does not stop at the first one. A server that is a
  script rather than a program (a `.jar`, a `.js` inside a `.vsix`, a `.phar`)
  gets a generated launcher that starts its interpreter on the file the catalog
  names, the same thing the POSIX side does with its wrappers. A package with no
  Windows source at all says so instead of generating a script that cannot run
  there.
- **Bundled servers**: a release package vendors clangd under
  `share/jot/payload/clangd`, so C/C++ support needs no system package and no
  network. Opening a C/C++ buffer installs it on the spot (once per binary per
  session, and only when the package ships it): the shipped binary is linked
  (copied on Windows) into the per-user LSP dir (`~/.local/share/jot/lsp/bin`,
  or `%LOCALAPPDATA%/jot/lsp/bin`) and the usual receipt is written, after
  which the waiting buffer attaches.
  `:lspinstall cpp` does the same by hand and `:lspremove cpp` removes that
  link and the receipt only, never the shipped copy. A payload may sit beside the executable or at the compiled-in prefix,
  and `JOT_LSP_PAYLOAD_DIR` overrides the lookup (authoritatively, which is
  how a packager or a build without the payload says "none here"). Any server
  a package ships as `payload/<binary-name>/` is installed this way; the rest
  keep using the registry's download managers. Source builds carry no payload
  and behave exactly as before.

Default servers: `pylsp` (Python), `typescript-language-server --stdio`
(JS/TS), `vscode-html-language-server --stdio` (HTML), `clangd` (C/C++).
Install helpers also cover Rust, Go, Lua, and Bash.

### C++ definition checks

- A workspace-wide scan pairs every function declaration with its bodies, so
  the two mistakes a compiler only reports at link time show up while editing:
  a function declared -- in a header or in a source file of its own -- with no
  implementation anywhere becomes a warning, and a signature implemented more
  than once becomes an error (the linker's `multiple definition of f`). A folder
  of standalone programs is
  exempt from the repeated-body error: when every source file holds its own
  `main`, the files are compiled one at a time and no two of them share a link,
  so the helpers they repeat are not the linker's error to report.
- Runs on the worker thread when a workspace is opened, after every save, and
  when unsaved declarations change (without requiring a save), or on demand with `:cppcheck` -- which also focuses the Problems list, announces
  what it counted, and lands the caret on the next finding. `:cppcheck next` and
  `:cppcheck prev` step through the findings from the caret, opening each file as
  they go and wrapping at either end; issued while a scan is still running they
  record the direction and walk the rows that scan lands with. `:cppcheck on|off`
  and the `cpp_definitions` setting turn the checks off; turning them off drops
  their rows again.
- The Problems view leads with a header row: the total, the severity tally, and
  how many findings each file holds (most-affected file first), truncated to the
  panel's width. The findings start under it, and a press on the header is inert
  rather than opening the row that would have been there without it.
- Findings publish as diagnostics in the same per-file store the language
  servers feed, so the Problems dock, the explorer's per-file badges, the
  workspace diagnostics picker (`:diagnostics`), the inline decorations and the
  gutter all show them with no extra wiring. A missing body's row sits on the
  whole declared name (clangd-style), so the squiggle lands on the unresolved
  symbol rather than on the whole signature.
- The rules are C++'s own: `= 0`, `= delete` and `= default` need no body, and
  a class-body, `inline`, `constexpr`, template or `static`/anonymous-namespace
  body is allowed to repeat (a `static` one is private to its file). The same
  signature under two `#ifdef`/`#else` branches is an ordinary way to write one
  implementation, so conditional bodies never count as duplicates of each
  other. Files the build system chooses between by name -- `foo_win32.cpp` and
  `foo_unix.cpp` -- are alternatives, not duplicates, and a body in a header
  that is *not* inline is reported as a hint: the same link error, waiting for
  its second translation unit.
- What it deliberately does not do is type checking: return types are not part
  of the match key (a wrong return type still counts as implementing the
  declaration), and reachability -- whether the definition is seen where the
  call is -- is left to clangd.

### Compilation (`:compile`)

- `:compile` asks for the command on the status line -- the `M-x compile`
  prompt, pre-filled with the last command (or `build_command` from the
  config) -- and `:make` is the alias for it. The line is the shell's, not the
  editor's: quotes, pipes and `&&` go to `/bin/sh` as typed, so `make`,
  `cmake --build build` and `cargo test -- --nocapture` are all just lines the
  prompt hands over. It runs in the workspace root rather than wherever jot
  was started, which is also what the relative paths in its output resolve
  against.
- The output lands in a `[Compile]` split *below* the pane being read: that
  pane keeps the file it was showing, and the log opens with the command, the
  time it started and (when it ends) the exit -- `Compilation finished` or
  `Compilation exited abnormally with code N`. The log is shown in that split
  rather than opened in it: no tab strip lists it (the pane spends no row on
  one) and the keys that walk tabs, `Alt+<digit>` and `Ctrl+Tab` included,
  step over it, so a build's output never becomes one of the opened files. `:compile` again stops the
  running build first, and `:ckill` (with `:compilekill` / `:compilestop`)
  stops it by hand; the output read so far stays in the log.
- The job runs under a pseudo-terminal of its own, which is what lets the same
  prompt run a program rather than only a compiler: `:compile ./build/app`
  gets a stdin (a command that asks a question reads end-of-input, the way the
  silent job's `/dev/null` answered it, instead of waiting for a keyboard the
  log does not have) and sees a tty, so a tool that colourises does it without
  being told. On a platform with no pty the job falls back to the silent pipe
  and only the colour environment is forced, so the log still arrives coloured;
  the tty itself is the part missing there.
- The colours a command prints are shown rather than spelled out: the escape
  sequences are read as the output arrives (text/ansi_spans.h) and become
  coloured spans on the log's rows, from the sixteen base colours through the
  `38;5;N` palette entries to exact `38;2;R;G;B` values, with bold, faint and
  underline carried across as the attributes they are. A colour set once stays
  set for the rows that follow, the way it does on a terminal. A carriage
  return rewriting a row -- a progress bar -- leaves only the state it ended
  on, and a command whose last line has no final newline still gets that row.
- Findings are parsed out of the output the way a reader's eye would pick
  them: gcc/clang's `file:line:col: error:`, rustc's message-plus-arrow pair,
  a Python traceback frame, the parenthesised `(line, col)` form msvc and tsc
  print, and paths with spaces in them. A row that names a problem is tinted
  with the severity colour the editor's own findings use, so the errors are
  the rows the eye lands on.
- `:cnext` / `:cprev` walk those findings (the same rule `:diagnext` uses:
  the next one in this file, else the first in a file that sorts after it,
  wrapping at the ends), and `Enter` on a row of the log opens the finding on
  that row -- in a pane that already holds text, not in the log, so the caret
  lands in the source. `Ctrl+O` comes back, because a compile jump is a jump
  like any other.

### Integrated terminal and tasks

- A bottom terminal panel backed by a real PTY, with multiple tabs that stay
  alive while hidden. Each tab is labelled with the shell icon (the same glyph
  the file tree uses for shell scripts), so the strip reads as a row of shell
  sessions. A custom tab name (a task, a plugin) is elided with an ellipsis
  once it exceeds its share of the strip -- a quarter of the row, with a
  12-cell floor -- so one long name cannot push the other tabs and the `+`
  off it. The terminal keeps the full name internally (`get_label`, the Lua
  API), so only the drawn label shortens.
- Drag the rule above the panel to resize it live; it can grow to nearly the
  full window. That rule is the panel's own top row -- part of the height it
  reserves -- so while it is up the pane area ends above the rule and the
  editor's last row stays a code row; with the panel closed there is no rule at
  all. Under it, the first row is the `Terminal`/`Problems` view tabs (each
  labelled with an icon: a terminal for the shell, a warning triangle for
  diagnostics) rather than a second border.
- The Problems list keeps every row on the panel's own background, colouring
  only the text by severity; the selected row is marked with an accent sliver
  instead of a selection fill, so a highlighted row's message stays readable.
  Its first row is a header -- how many findings, of which severities, and how
  many each file holds -- and the findings start under it, so the row a click
  opens is the row under the pointer.
- The panel owns its own rows and nothing else. A press, a drag or the wheel
  anywhere else goes to what is under the pointer -- the buffer, the explorer,
  a dock -- so a click in the code brings focus and the keys back to editing
  while the list is up, and the list still answers while its own rows are hit.
- **Fullscreen zoom** (the terminal's own; pane zoom is `Ctrl+W o`):
  `Alt+Shift+Z` while focused, or
  `:termzoom`, toggles the terminal across the whole pane area. `Esc` exits
  fullscreen too. While zoomed, the sidebar and right dock are hidden so
  nothing overlaps the fullscreen terminal.
- **Floating terminal**: `Alt+Shift+T` or `:termfloat` opens a second shell in a
  box centered over the active pane's text rows. It takes 85% by 75% of that
  area by default, `terminal_float_width` and `terminal_float_height` being a
  percentage each, and it stays inside the pane's text rows: below the
  breadcrumb and above the status line, so nothing paints over its frame. The
  box owns the keys while it is up, the wheel walks its scrollback and a
  click-drag selects text to copy. `Esc`, the chord again, or a click outside
  the box hides it and leaves the shell running, working directory and
  scrollback included, so the next toggle returns to the same prompt; a shell
  that has exited restarts on the next key where it was first started.
- **Mouse selection**: click and drag in the terminal to highlight text
  (drag beyond the panel edges is clamped to the visible rows); releasing
  copies the selection to the system clipboard.
- **Ctrl+click a link** in the shell's own output opens it in the desktop's
  opener, the way a buffer's link does: the same `http://`, `https://`,
  `mailto:` and `file://` rules (a sentence's trailing dot stays out of the
  URL), the same status message, and the same report when the machine has no
  opener. The click is the link's, so it leaves no selection behind, and
  `Ctrl+hover` underlines exactly the URL the click would open -- in the
  docked panel and in the floating box, which stays up across the click.
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

Open image files in a viewer inside the pane that holds them, drawn with the
kitty graphics protocol at the picture's own resolution: one image pixel per
screen pixel until the picture is bigger than the pane, when it is scaled down
once with its shape intact. A smaller picture is never enlarged.

jot requires that protocol. On startup it sends the protocol's own handshake - a
query placement followed by a device-attributes request - and refuses to run on
a terminal that does not answer it, with an error naming what to use instead.
Terminals that implement it: kitty, WezTerm, Ghostty, Konsole.
`JOT_KITTY_PROTOCOL=1` sets the answer without asking (a session behind a
multiplexer that strips APC codes, a test harness); `0` refuses without asking.

The cell-art preview (ImageMagick or `chafa`) and the Sixel conversion
(`img2sixel`) stay as the paths a terminal without a working kitty placement
falls back on, and `image_viewer_backend = auto` (`kitty`, `sixel`, `cell`, or
`off`) selects the renderer.

### Markdown preview

`:MarkdownPreview` renders the current markdown buffer in your browser through
a tiny loopback HTTP server, and keeps the page and the editor in step in both
directions:

- **Live refresh** - every edit (debounced) and every save re-renders the
  document and pushes just the new body to the open page over Server-Sent
  Events, so the scroll position and any diagram state survive the update. The
  full page is only replaced when the shell itself changes (title, theme, CSS).
- **Two-way scroll sync** - while both sides are open, the editor's top visible
  line is pushed to the page and the page's scroll position is pulled back into
  the editor, so scrolling either one follows the other. The cadence is
  `markdown_preview_refresh_interval` (ms).
- **GFM by default** - headings with slug anchors, paragraphs, nested ordered
  and task lists, tables with alignment, blockquotes and GitHub-style alerts
  (`[!NOTE]`, `[!TIP]`, `[!IMPORTANT]`, `[!WARNING]`, `[!CAUTION]`), fenced code
  with a copy button, `==mark==`, `++ins++`, `H~2~O`, `E=mc^2^`, footnotes,
  reference links, autolinks, raw HTML passthrough and horizontal rules.
- **Optional extras**, each a `markdown_preview_option_*` switch: `highlightjs`
  (default on), `code_copy` (on), `source_map` (on), plus `katex` math,
  `mermaid` / `plantuml` / `flowchart` diagrams, `echarts` and `vega` charts,
  `emoji` shortcodes and a floating `toc` panel.
- **Customisable page** - `markdown_preview_theme` (`dark`/`light`/`auto`),
  `markdown_preview_page_title`, `markdown_preview_custom_css` and a Lua
  `preprocessor(text, path)` hook that can rewrite the source before parsing.

Local images resolve relative to the document (honouring
`markdown_preview_images_path` for a prefix) and are served by the same server.
`markdown_preview_browser` picks the browser to open (`none` to just print the
URL, or any name from the built-in list; the platform opener is used when
unset), and `markdown_preview_auto_start` / `markdown_preview_auto_close`
tie the session to markdown buffers opening and closing.

The preview is driven from Lua - `jot.md.start/stop/toggle/refresh`,
`jot.md.url()`, `jot.md.is_running()` and `jot.md.setup{}` - over the native
`jot.preview.*` transport, so a plugin can re-implement or extend any part of
it. See [LUA_API.md](LUA_API.md#markdown-preview).

### HTML preview

`:HtmlPreview` opens the current HTML buffer in the browser and reloads it as
you edit. It is the same loopback server as the markdown preview, pointed at the
problem HTML actually has: a page is not one document but a tree.

- **The real tree is served** - the server is rooted at the workspace (or at the
  file's own directory, with `html_preview_root = "dir"`), and the browser is
  sent to the page's path inside it. Its `<link href="styles/site.css">`, its
  `<img src="../img/logo.png">` and its ES module imports resolve exactly as
  they would over any other static server, instead of 404ing against a one-page
  server.
- **Unsaved edits are what the page shows** - the buffer's text overrides the
  file on disk for the one path being edited. Everything else still comes from
  the file system, so an unsaved markup change previews without the page's
  stylesheet disappearing.
- **Reload on change** - each served HTML page carries a small injected
  EventSource client, so an edit (debounced by `html_preview_refresh_interval`,
  default 150 ms) and every save reload the page. A change that leaves the text
  where the page already was is not a reload, and the request path is confined
  to the root: a URL that tries to climb out of it is refused.

Opening the browser is `html_preview_open_browser` (false serves it and prints
the URL instead - the useful setting over SSH), `html_preview_browser` picks
which one, and `html_preview_auto_start` / `html_preview_auto_close` tie the
session to HTML buffers opening and closing.

Driven from Lua the same way the markdown preview is - `jot.html_preview`
(`start`, `stop`, `toggle`, `refresh`, `url()`, `is_running()`, `state()`,
`setup{}`). See [LUA_API.md](LUA_API.md#html-preview).

### Snippets

A full snippet engine, ported from LuaSnip's model. Snippets are matched
against the text before the caret, expanded in place, and then navigated
placeholder by placeholder with the caret - typing in one occurrence of a
tabstop updates every mirror.

- **Formats** - every snippet-text form the LSP and VSCode define: `$1`,
  `${1}`, `${1:default}`, `${1|one,two|}` choices, `${1/(.*)/\U\1/}` transforms
  (with `\u`/`\l`/`\U`/`\L`/`\E` and the `/upcase`, `/downcase`, `/capitalize`,
  `/camelcase`, `/pascalcase`, `/snakecase`, `/kebabcase` format field), and the
  `TM_*` / `LS_*` variables (`${TM_FILENAME}`, `${TM_LINE_NUMBER}`, …).
- **Packs** - Lua snippet files (`snip_env` with `s()`, `t()`, `i()`, `c()`,
  `f()`, `d()`, `r()`, `rep()`, `fmt()`, …), VSCode `.json` / `.code-snippets`
  and snipMate `.snippets` files, looked up under `~/.config/jot/snippets`,
  `<workspace>/.jot/snippets` and anything in `snippet_paths`.
- **Triggers** - plain word-boundary triggers, Lua-pattern triggers
  (`regTrig`), function triggers, `priority`, `condition` / `show_condition`,
  autosnippets, and the `extends` graph so a filetype can inherit another's
  snippets.
- **Keymaps** - `Tab` expands a trigger or jumps to the next placeholder,
  `Shift+Tab` jumps back, and `Ctrl+E` / `Ctrl+Shift+E` cycle a choice while the
  session is live. All four fall back to the editor's own behaviour when no
  snippet applies, so `Tab` still indents and `Shift+Tab` still outdents.
- **LSP** - when a server answers a completion with `insertTextFormat = 2`,
  jot hands the snippet text to the engine, so accepting it expands with real
  tabstops, mirrors and choices instead of a flat text insert.
- **Commands** - `:Snippets` (pick one for the current filetype), `:Snippet
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
  output - all in the debugger panel.

### Git

**Git panel** (`:gitpanel`) - a native, lazygit-style git client in the
right dock with four views (switch with `2`/`3`/`4`/`5`, matching lazygit's
panel numbers):

- **2 Files** - conflicts, staged, unstaged and untracked sections with
  status-colored rows; `space` stages/unstages the selected file, `a`/`A`
  stage/unstage everything, `Enter` opens the file's diff (double-click
  too), `c` opens the commit-message prompt, `d` discards the file's
  working-tree changes (press twice to confirm), `s` stashes everything
  (including untracked), `y` copies the path.
- **3 Branches** - `space` checks out, `n` creates a new branch (prompt),
  `m` merges the selected branch into the current one, `d` deletes it
  (twice to confirm).
- **4 Commits** - recent history with hash/date/subject; `space` checks out
  a commit (detached HEAD), `y` copies the hash.
- **5 Stash** - `space` applies, `g` pops, `d` drops (twice to confirm).

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
is an external binary, not bundled - install it with `brew install lazygit`,
`apt install lazygit`, or the script on lazygit's README. Running `:lazygit`
again re-focuses the open tab; quit lazygit with `q` to return to the
editor.

### AI assistant

`:CodeCompanionChat` (`Alt+Shift+A C`, `:Cc`) opens a chat as a real buffer in
a tab: the transcript is markdown, you type after the `>` marker under the last
`## user`, and `Alt+Shift+A S` sends everything from that marker to the end, so
a prompt can be several lines. The answer streams in as it arrives, the tab is
marked `[Chat].md` (a rendered view: `:w` on it is refused, `:w <name>` keeps a
copy), and the chat survives switching tabs. This is
[codecompanion.nvim](https://github.com/olimorris/codecompanion.nvim)'s shape
without the plugin: a chat buffer, prompt-line context, an inline rewrite, and
adapters.

**Context in the prompt.** `#buffer`, `#selection`, `#diagnostics` and
`#file:<path>` attach that code to the message - the sentence keeps a short
label (`(src/render/frame.cpp) why is this rebuilt`) and the text follows under
it in a fenced block, capped at 400 lines per file with the block saying how
much was dropped. `/buffer`, `/selection`, `/diagnostics` and `/file <path>` do
the same on their own line, which is how a long path gets in without breaking
the sentence. A `#word` or `/line` the feature does not know is left exactly as
typed, so `#include <vector>` is still a sentence. `/help` in the prompt region
lists the chat's own commands (`/status`, `/new`, `/stop`, `/model <name>`,
`/adapter <name>`).

**Inline rewrite.** `:CodeCompanion <text>` (or `Alt+Shift+A I`) takes the
selection, or the line the caret is on, and shows the model's replacement as a
diff against what is there now: removals in the error colour, additions in the
string colour, a `-N +M` header. `y` or `Enter` applies it as one undoable edit,
`n`/`Esc`/`q` discards it, the arrows, Page Up/Down and the wheel scroll a long
answer. `:CodeCompanionPrompt <name>` runs one of the bundled prompts (`explain`,
`fix`, `tests`, `refactor`, `document`, `faster`) and
`:CodeCompanionActions` (`Alt+Shift+A A`) lists the prompts, the adapters and the
chat commands in one picker. `Alt+Shift+A H` and `:CodeCompanionStatus` show
what is talking to what.

**Adapters.** A provider is described by *wire protocol*, not by vendor:
`openai` (chat-completions: OpenAI, Ollama, OpenRouter, DeepSeek, Groq,
Mistral, xAI, Gemini's compatibility endpoint, LM Studio, llama.cpp, vLLM, a
gateway), `anthropic` (the messages API) and `cmd` (any CLI on `PATH` that
answers on stdout, e.g. `claude -p`). `ai_adapter` picks the entry;
`ai_model`, `ai_base_url`, `ai_key_env`, `ai_command`, `ai_stream`,
`ai_temperature`, `ai_max_tokens` and `ai_system_prompt` override it, so a
provider that copies one of the three shapes needs no code at all:

```lua
jot.config.set("ai_adapter", "ollama")   -- kind=openai, no token needed
jot.config.set("ai_model", "qwen2.5-coder")
```

The request is a `curl` on the worker queue (the same reason the git status and
workspace scans are: an endpoint that takes three seconds must not take three
seconds of keystrokes with it), writing the reply into a scratch file the frame
loop reads as it grows - which is what makes an answer appear token by token.
Streaming SSE is parsed per adapter kind, a provider error is reported with the
provider's own message and the prompt is put back so a retry is another send,
and `Alt+Shift+A X` stops an answer mid-flight.

The token is never a setting: it is read from the environment variable
`ai_key_env` names (`OPENAI_API_KEY` and so on by default) at the moment of the
request, and `:settings` shows the variable, not the secret. Nothing leaves the
process except that request; there is no telemetry, no session upload, and the
file paths and code only travel when you attach them with `#`/`/` or select
them for an inline rewrite.

### UI and mouse

The chrome is yours to arrange: menu bar, pane tabs, editor panes, optional
sidebar, optional minimap, a right-side tool dock, bottom terminal/debugger
panels, and a two-row status/message area. The dock's plugin tab hosts every
Lua panel and is labelled after the one it is showing - opening `:LeetPanel`
puts `LeetCode` on the tab - and falls back to `Plugin` when no panel is up.

Borders mark where one region ends and another begins, and only there: a region
draws a line only on the side facing another region, so the sidebar and the
editor share a single `│`, a split has one separator rather than two, and a lone
pane has no frame at all - focus is shown by the cursor and the active tab, never
by the border. Floats (hover, completion, dialogs, telescope, the tool dock's
panels) sit over buffer content on every side, so they keep a full box. Everything
is drawn with flat corners; the colour comes from the theme's `WinSeparator` /
`FloatBorder` / `SidebarBorder` slots.

The row's left-hand end is the file, and while the screen is split it carries the
pane's place among the panes (`2/3`) right after the file name -- the one chip
that answers "where am I" once two panes show a file each. A zoomed pane appends
`zoom`, so the chip does not read as a split whose other panes vanished.

The status row's right-hand end can carry two time chips: the local wall clock
(`status_clock`, 24-hour `HH:MM`) and how long was coded today in the workspace
(`status_coding_time`). Both ship off -- the bar is the file's news, and neither
chip is about the file -- and a session that wants them turns them on. The
second one has two sources, and which is speaking reads off its glyph - a clock
outline for WakaTime's own total for today, a timer for the local store. Both chips are optional and both are small, so a
narrow terminal gives them up before the diagnostics, git and LSP chips. They
are also the only thing on screen that moves with nothing typed, which is why
the frame loop watches them: each frame formats the two labels and asks for a
repaint only when one of them would come out different, so the bar ticks once a
second while the coding total is still moving and an idle editor paints nothing
except when a label actually moved.

### Features written in Lua

A feature does not have to be compiled in. The editor's own machinery is
reachable from Lua: commands, keymaps, autocmds, buffers, panes, floats, the
decoration layer, status-line segments, timers, jobs, the file and workspace
APIs, and the LSP/DAP/git bridges. Underneath all of them are three namespaces
that are the bottom of the stack.

`jot.screen` is the cell grid itself: read any cell, write text or a filled
rect anywhere with colours (`"#ff8800"` or a palette index) and the cell
attributes, and register a painter that runs after the editor has drawn, on
every frame, over anything. `jot.input` is the raw event stream: a key hook that
sees a chord before any mode does (including a modal prompt's answer) and
consumes it, and a mouse hook above the floats. `jot.term` appends raw bytes to
the frame, for the protocol sequences a cell grid cannot express.

A feature is files under the config directory rather than code in the binary.
`package.path` is pointed at `~/.config/jot`, its `lua/` and its `plugins/`
directories first, so `require("mystuff.util")` resolves there, and
`jot.lua.reload("mystuff")` re-runs a module in place. The loop is: edit a Lua
file, `:reloadplugins` (or a keymap calling `jot.lua.reload()`), look at the
screen. No rebuild, no restart.

### Coding time

The chip is `status_coding_time`, and what it shows is the time coded today for
the workspace in front of you, from one of two places:

- **WakaTime** (`wakatime` in the Extensions category of `:settings`) is the one
  that survives across machines and editors. Turning it on makes the editor
  report heartbeats to `wakatime-cli`, exactly as the [plugin spec][waka]
  describes: a save, a file change, or two minutes on the same file. The cli
  auto-detects the language, project and branch, so JOT passes it the absolute
  path and the caret and otherwise stays out of the way (`--entity`, `--time`,
  `--lineno`, `--cursorpos`, `--lines-in-file`, `--write` on a save). It is also
  the cli that reads `~/.wakatime.cfg`, so a machine already set up for WakaTime
  needs nothing here; `wakatime_api_key` and `wakatime_api_url` override what it
  finds, for a self-hosted or WakaTime-compatible server. The cli is installed
  rather than assumed: the first enable with no `wakatime-cli` on `PATH` fetches
  the release build for this machine (linux/darwin/windows, amd64/arm64/386/arm/
  riscv64) and unpacks it into `~/.wakatime/` (`$WAKATIME_HOME` when set) as
  plain `wakatime-cli`, the name every other WakaTime plugin looks for, so a
  copy on `PATH` is left alone and runs after an install go to the editor's own.
  That download is the one thing turning the integration on does behind the
  user's back, which is why the setting is off by default; it is bounded, it
  happens off the keystroke path, and what came of it is on the message line. A
  machine that cannot reach the release - offline, or an architecture with no
  build - is told so once and keeps using the local store; a later settings
  apply tries again.
- **The local store** (always on) is what keeps the chip worth showing with the
  integration off. Totals are kept per workspace and day under
  `configs/coding_time.tsv`, so reopening the editor later today continues
  today's number rather than restarting a session timer nobody reads. Time is
  credited between two activity ticks, and any gap past two minutes is dropped,
  so an editor left open overnight does not log eight hours of nothing. One
  project is one line: the key is the workspace path, canonicalized (a trailing
  separator dropped, a relative spelling resolved against the directory the
  editor was started in), and rows already in the file that fold onto the same
  project and day add up. A row past a day (86400000 ms) is dropped rather than
  reported: a day cannot hold more than a day, and the store carried impossible
  24 and 67 hour days while the counter was credited against the wrong clock.

The chip shows WakaTime's answer whenever it carries a total, and the locally
stored one otherwise. A total that reports no time at all (`0 secs`) does not
count as one: that is what the cli prints while the heartbeats it is holding have
not reached the server yet, and what an account with nothing logged says, so the
chip keeps the local total rather than sitting on a zero through real work. The
glyph beside the number says which of the two is speaking.

Today's WakaTime total is fetched at most once a minute, on the worker queue, so
a slow API never stalls a keystroke; a heartbeat is spawned detached for the same
reason. The local total is written out at most once a minute while it is moving,
and once more when the editor shuts down. A session ended by a signal -- a
`kill`, a service manager stopping it, the terminal being closed -- shuts down
the same way instead of dying where it stands, so today's total and the
workspace session survive it and the terminal is handed back.

[waka]: https://wakatime.com/help/creating-plugin

The mouse is wired throughout - click to place the cursor, drag to select (with edge auto-scroll), double/
triple-click for word/line selection (double-click stops at `.`, so `ext`
in `ext.begin()` selects just `ext`), `Ctrl+D` to select the next occurrence
(`Alt+Click` adds a caret, `Esc` clears extra carets), click tabs, drag split
dividers, jump
the minimap viewport, navigate the sidebar, and toggle breakpoints.

`Ctrl+click` goes to the definition of the symbol under the pointer, and a
chain is read per element rather than per cell: in `counter.stored`, `p->field`
and `outer::thing` the receiver answers for itself, `.`/`->` name the member on
their right, a `::` colon names the qualifier on its left, and a click anywhere
inside a name asks about that name. The `Ctrl+hover` underline covers exactly
that same element, so what is underlined is what the click will jump to.

A link under the pointer is the exception, the way it is in VSCode: `Ctrl+click`
hands an `http://`, `https://`, `mailto:` or `file://` URL to the desktop's own
opener (`xdg-open`, `gio open` or `open`) instead of asking about a definition,
and the `Ctrl+hover` underline covers the whole URL rather than the first name
inside it. The caret stays where it was, and no language server is needed -- the
URL travels to the machine's browser, not through a token. A machine with no
opener gets a status message naming the URL rather than a silent click. The tail
that belongs to the prose is not part of the link (`see https://example.com.`
opens without the dot), while a bracket the URL opens itself stays (a Wikipedia
link keeps its `)`). The same rules read the integrated terminal's own output: a
URL a command printed opens on `Ctrl+click` there too, with `Ctrl+hover`
underlining exactly it, in the docked panel and the floating box alike.

**Zen focus mode** (`F12` or `:zen`) strips the chrome: the sidebar, the right
dock, the workspace tab strip and the status line all hide, and the pane area
takes the whole window - a distraction-free full-width view. It is persistent
(`zen_mode` in settings.conf, also a `:settings` row): F12 writes the key, so
the next start (and `:reload`) lands in the same layout, opening a workspace
cannot put the chrome back, and the chrome keys (`Ctrl+B`, `Ctrl+Shift+B`)
report instead of taking effect while it is on. Leaving zen restores the layout
it was entered from, and the F12 binding is rebindable via the usual
`jot.keymap` API.

## Keybindings

### Core editing

| Shortcut | Action |
| --- | --- |
| Typing | Insert text at the cursor |
| `Esc` | Clear selection / close the active floating surface |
| `Ctrl+Z` / `Ctrl+Y` | Undo / redo (`Ctrl+Shift+Z` redoes too) |
| `Ctrl+A` | Select all |
| `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Copy / cut / paste |
| `Ctrl+D` | Select next occurrence (multi-cursor) |
| `Ctrl+Shift+D` | Duplicate current line |
| `Ctrl+K` | Delete current line |
| `Ctrl+/` | Toggle comment (138 languages, per-extension comment markers) |
| `Ctrl+Backspace` / `Ctrl+Delete` | Delete previous / next word (from the caret's edge) |
| `Alt+Backspace` | Delete the whole word the caret is on |
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
| `Alt+V` | Selection: `E` expand, `C` shrink, `K` keep primary, `R` rotate, `A`/`B` cursor above/below, `L` split lines, `M` match all occurrences, `W` the word, `Shift+L` the line, `S` the object menu, `Shift+S` the current statement |
| `Alt+]` / `Alt+[` | Next / previous: `F` function, `C` class, `D` diagnostic (`Alt+E` remains an alias), `B` bookmark, `T` TODO comment |
| `Alt+C` | Code: `D` definition, `C` declaration, `T` type definition, `I` implementation, `H` switch header/source, `R` references, `N` rename, `A` code actions, `S` symbols, `W` workspace symbols, `K` documentation, `F` format the document, `O` outline panel, `P` matching bracket, `B` bookmark the line |
| `Alt+C Z` | Fold, Vim's `z`: then `A` toggle, `C` fold, `O` unfold, `M` fold every block, `R` unfold every block (the toggle also answers on `Alt+C Shift+Z`) |
| `Alt+X` | Clear inside: `S` the string, `B` the brackets (then type the replacement); `J` join with the next line, `U` remove the pair around, `W` wrap in a pair (`(`, `[`, `{`, `"`, `'`) |
| a count, then the chord | The number typed after the prefix applies to the chord that ends the sequence -- `Alt+D 3 L` deletes three lines, `Alt+V 3 Shift+L` selects three, `Alt+X 3 J` joins three, `Alt+] 3 F` walks three functions (Vim's `3dd`, `3yy`) |

A count is Vim's, spelled the same way: type the digits after the prefix and
before the chord that acts. It is read by the line operations (`Alt+D`/`Alt+Y`
`L`, `Alt+V Shift+L`, `Alt+X J`) and by the walks (`Alt+]` / `Alt+[` over
functions, classes, diagnostics and compile errors), and it is ignored
everywhere else -- a digit never becomes an argument for a command that takes
a string, so `Alt+C N` cannot rename a symbol to `3`. The count shows on the
status line as it is typed (`Count: 3`) and Backspace takes it back a digit at a
time before it steps out of the sequence. Deleting with a count takes whole
lines as one undo step and one register entry, leaving no blank line behind;
selecting with a count (`Alt+V 3 Shift+L`) is what to reach for when the
replacement is longer than one line, since typing over the selection replaces
all of it.

Objects available today are `F` function, `C` class or type, `A`
argument/parameter, `S` statement, `W` word, `L` line - the syntax objects come
from tree-sitter, so they need a grammar for the file type. The statement is the
one that is reached mid-edit rather than while browsing, and the one that
usually spans rows (a declaration, an `if`, a call split over lines), so it also
has `Alt+V Shift+S`: one press selects the whole statement where walking out
with expand takes a press per level.

### In-place edits

The things that come up mid-edit get a chord instead of a sequence: the word
the caret is standing on (`Alt+Backspace`), the inside of the string or the
bracket pair it is standing in (`Alt+X S`, `Alt+X B`), the line the caret ends
on when a statement has been split over several rows (`Alt+X J`), and the pair
around the caret's selection or word (`Alt+X W`, `Alt+X U`). There is no insert
mode to change into, so clearing one leaves the caret where the text was and
typing takes its place.

A pair character typed over a selection goes around it instead of replacing it:
`"` on a marked word quotes it (`"word"`), `'` and `(` likewise, so the word is
bracketed without a trip through a chord. The selection ends there and the caret
is left inside the pair, which is where a pair typed with nothing selected leaves
it -- the closer typed next steps over its twin, so `(` then `)` brackets a word
and carries on after it. Everything else typed over a selection still replaces
it.

`Alt+X W` is typed as the pair that should go around: `Alt+X W "` wraps in
quotes, `Alt+X W (` in parentheses, `[`, `{` and `'` likewise. The selection is
wrapped when there is one and the word at the caret when there is not, and
`Alt+X U` takes the innermost pair back off -- a string, a call, an `if`
condition -- reading the three literal kinds the way `Alt+X S` does. Anything
the five chords do not cover is still `:surround <left> [right]` from the
palette, where a quote is written `\"` (the command line reads a bare `"` as
the start of a quoted run, and one that never closes reports its usage).

`Alt+Backspace` takes the span `Ctrl+D` would have selected, which is what makes
it different from `Ctrl+Backspace` and `Alt+D W`: those eat from the caret's edge
to the word's boundary, so a caret in the middle of a word leaves its tail
behind. A live selection goes first, and with no word under the caret it deletes
backwards the way `Ctrl+Backspace` does rather than doing nothing.

`Alt+X B` clears the innermost pair the caret is inside, so `call(f(g(a)))` with
the caret on the `a` clears `g(a)'s` inside and leaves the outer call alone. The
pair may close rows below, which is how a multi-line call collapses to one line
with its brackets intact. Brackets inside string literals and comments are not
pairs: `puts("(")` has one pair, and an apostrophe in a comment cannot pair with
a bracket on the next line. A pair with nothing between it reports instead of
editing, so the key cannot dirty a file for a no-op. `Alt+X S` reads the three
literal kinds the same way and clears the innermost one containing the caret,
and a caret that is not inside any literal reports rather than clearing the one
it just left.

### Navigation

| Shortcut | Action |
| --- | --- |
| Arrow keys | Move cursor |
| `Shift+Arrow` | Extend selection |
| `Ctrl+Left` / `Ctrl+Right` | Previous / next word (`Ctrl+Shift` extends the selection) |
| `Ctrl+Up` / `Ctrl+Down` | Scroll the view a line, leaving the caret where it is |
| `Ctrl+Home` / `Ctrl+End` | File start / end (`Ctrl+Shift` extends the selection) |
| `Home` / `End` | Smart line start / end |
| `Page Up` / `Page Down` | Move by 10 lines |
| `Alt+I` / `Alt+A` | Smart line start / end |
| `Alt+G` / `Alt+Shift+G` | File start / end |
| `Alt+H` / `Alt+L` | Previous / next word |
| `Alt+Up` / `Alt+Down` | Move line or selection up / down |
| `Tab` / `Shift+Tab` | Indent / outdent selection |
| `Alt+C P` | Jump to the bracket matching the one at the caret |
| `Alt+C B` | Bookmark this line (press again to clear it) |
| `Alt+] B` / `Alt+[ B` | Next / previous bookmark in this file (the walk wraps) |
| `Alt+C Z A` / `Alt+C Shift+Z` | Fold or unfold the block at the caret |

A word is what a reader would call one, not what a tokenizer would: the word
motions stop inside identifiers as well as between them, so `alpha_beta` is two
stops and `fooBarBaz` is three (one at each capital), and a digit run is a stop
of its own (`abc123def` walks as `abc`, `123`, `def`). "Next word" from the end
of a line is the first word of the next one -- blank lines and indentation
included -- rather than a wall, and "previous word" is the same motion
backwards from the start of one.

`Ctrl+Up`/`Ctrl+Down` are the reading pair: the view scrolls a line at a time
and the caret stays put (the wheel's own animation), so reading on does not
move the place the next edit will land.


### Buffers, tabs, panes, and tools

| Shortcut | Action |
| --- | --- |
| `Ctrl+Tab` / `Ctrl+Shift+Tab` | Next / previous tab in this pane's own history |
| `Alt+,` / `Alt+.` | Previous / next tab on the workspace strip |
| `Alt+1..9` / `Alt+0` | Switch to strip position 1..9 / the last tab |
| `Alt+R`, then a letter | Jump to the tab painted with that letter |
| `Alt+W` | Close current file tab |
| drag a pane tab | Move it to another pane, split a pane around it, or reorder |
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
| `:Undotree` / `Alt+Shift+U T` | Undo tree in the right dock (Alt+Shift+U S jumps to the state in front, `:UndotreeClear` drops the history) |
| `Alt+Shift+H a` / `m` | Harpoon: mark the file in front / show the mark list |
| `Alt+Shift+H 1..9` | Harpoon: jump to that slot (the menu's digits do the same) |
| `Alt+Shift+H n` / `p` | Harpoon: next / previous mark (clamps at the ends, `harpoon_nav_wrap` wraps) |
| `Alt+Shift+H d` / `c` | Harpoon: drop the file in front / empty this project's list |
| `Ctrl+M` or `Alt+M` | Toggle minimap |
| `Ctrl+T` or `Alt+T` | Theme chooser |
| `` Ctrl+` `` | Open / focus / hide terminal panel |
| `Alt+Shift+T` | Open / hide the floating terminal (Esc hides it) |
| `F12` | Toggle zen focus mode (hide chrome, full width) |
| `Alt+Shift+A C` | AI: open or close the chat |
| `Alt+Shift+A N` | AI: start a fresh chat |
| `Alt+Shift+A S` | AI: send the prompt |
| `Alt+Shift+A X` | AI: stop the answer being written |
| `Alt+Shift+A I` | AI: rewrite the selection or line |
| `Alt+Shift+A A` / `P` | AI: prompts, adapters and chat commands picker |
| `Alt+Shift+A H` | AI: adapter, model and endpoint |
| `:settings` or `Ctrl+,` (GUI) | Open the settings menu: every config key under a section heading (Editor, Appearance, Code intelligence, ...), searchable by typing, booleans toggle, ints step, enums list their choices, values edit inline; Lua-registered keys included |

### The workspace tab strip

The strip shares row 0 with the full-height side columns: it spans the pane
area between the explorer and the right dock (the whole width when neither is
up), and paints only its tabs, so the cells beside the last tab keep the
editor's own background. Every open buffer appears once, in the workspace's own
order, whichever pane is showing it. That is the one place the buffers are
addressable -- `Alt+1..9` and a click pick a *position on the strip*, while
`Ctrl+Tab` keeps walking the focused pane's history. Clicking a tab whose buffer
another pane already shows takes focus to that pane; the dot next to such a tab
is that marker, and the pin glyph marks a tab held at the front of the order.
The strip is workspace chrome, not part of the home screen: while the launcher
is up the row carries no tabs, and its cells answer no clicks, until the editor
is back. It is also a lone-pane affordance: split the pane area and the strip
steps aside, because every pane then draws its own list on row 0 (see below) and
a workspace strip over the groups would be the same files inked twice.
`pane_tabs = off` keeps it up in a split, as the only list left.

`Alt+R` arms the letter picker: every tab is painted with the letter it answers
to (the file's initials first, then the home row), and the next key switches to
that tab (any other key just puts the letters away). Dragging a tab reorders the
strip; the tab menu (right-click) offers Save, Close, Pin/Unpin and Close Other
Tabs.

Settings: `tabline` (`true`) shows or hides the row, `tabline_auto_hide` (`0`)
hides it again once there are that many buffers or fewer (`0` never hides it),
and `tabline_insert` (`after_current`, or `start` / `end`) decides where a buffer
the strip has not seen before lands in the order.

### Pane tab strips (editor groups)

When the pane area is split, every pane draws its own tab strip on its first
row, and that row is row 0 itself: the workspace strip hands it over, since a
strip above the groups would list the same files a second time. Each pane gets
its own list, with the file glyph, the modified dot and a close control on
every tab. Clicking a tab switches that pane and
focuses it, the wheel scrolls a window of tabs when they overflow (`‹N` /
`›+N` chips report the rest), and the middle button closes a buffer. The strip
spends its row only where the workspace strip cannot speak for the pane, so a
lone pane with the workspace strip on top never paints the same list twice --
and in a split every group takes the row, a group holding a single tab
included, because that is where its file is named and where a dragged tab lands
to join it. `pane_tabs` picks: `auto` (the default) is that rule, `always`
shows the row for every pane, and `off` never (which leaves row 0 to the
workspace strip).

Dragging a tab is how the layout gets arranged, VSCode-style: releasing over
another pane's strip moves the tab into that pane's list, releasing over a
pane's body joins it at the end, and releasing against a pane's edge splits
that pane in that direction and opens the tab in the new group. A pane whose
last tab leaves closes with it, and a drop back into the tab's own pane
reorders the list.

### The breadcrumb winbar

A code pane spends its first row on a breadcrumb: the workspace root, the
folders down to the file, the file, and then the symbols enclosing the caret
(`Widget › render_thing`). The tail - the file and the current symbol - is inked
and bold; the rest stays quiet. The row is the pane's own, so a split pays it
twice and the panes line up either way. `winbar` picks the mode: `off` never
shows it, `auto` (the default) shows it for code files only, `on` for every named
file. It is a window option, so one pane can run a different mode from the rest
(`jot.window.set_option("winbar", "off")` for the focused one): the pane that
turns the row off gives the row back to its text, and the panes beside it keep
theirs.

Every crumb is a press away from its siblings: a folder crumb lists what is
inside it, a file crumb its folder, a symbol crumb the symbols sharing its scope,
with the row the chain is already on marked by a dot. A **folder row wears a
chevron and opens its own panel *beside* the one that offered it** - the cascade
keeps every level it stepped through on screen, so the panels read as the path
you walked (dropbar's model). The cascade follows the pointer: moving the
selection onto a folder row opens that folder, moving onto a file row drops the
levels below it again.

| Key / mouse | Action |
| --- | --- |
| press a crumb | open its drop-down (press it again to put it away) |
| click a row | open the file / jump to the symbol |
| hover a folder row | open that folder's panel beside it |
| `j` / `k`, `↓` / `↑` | move the deepest panel's selection |
| `Enter`, `l`, `→`, space | open the selected folder / file or symbol |
| `h`, `←` | step back one level |
| `Esc`, click outside it, press its crumb again | put the whole cascade away |
| wheel | walk the rows of the panel under the pointer |

### Pane layout

| Shortcut | Action |
| --- | --- |
| `Ctrl+W` (alone) | Start the pane family: the chord that follows picks it (listed in the table below) |
| `Ctrl+W` then `h`/`j`/`k`/`l` | Focus the pane left/down/up/right |
| `Ctrl+W` then `s` / `v` | Split the pane in two: below / right |
| `Ctrl+W` then `Shift+S` / `Shift+V` | Split above / left |
| `Alt+Shift+Arrow` | Split left/down/up/right (one chord, no prefix) |
| `Ctrl+W` then `w` / `p` | Next / previous pane |
| `Ctrl+W` then `c`, or `Ctrl+Q` | Close the focused pane |
| `Ctrl+W` then `o` / `x` / `e` / `r` | Zoom / swap / equalize / rotate the layout |
| `Alt+H/J/K/L` | Focus pane (or explorer) left/down/up/right |
| `Ctrl+Alt+Arrow` | Focus pane in that direction |
| `Ctrl+Shift+H/J/K/L` | Resize pane |
| `Ctrl+Shift+Arrow` | Resize pane |
| drag a divider | Resize that split (the pointer turns into a resize arrow over it) |
| double-click a divider | Put that one split back in the middle |

The bare `Ctrl+Arrow` chords are navigation (`Ctrl+Left`/`Ctrl+Right` move by
word, `Ctrl+Up`/`Ctrl+Down` scroll the view), which is why the resize arrows
carry the Shift the letters already had. `Alt+H` and `Alt+L` are the two word
motions as well: with no pane (or explorer) in that direction to focus, the
chord moves the caret instead of reporting, so they are live on a single-pane
screen. `Alt+J`/`Alt+K` have no second meaning and say `No pane in that
direction`.

The pane family is the `Ctrl+W` prefix, Vim's window prefix, so the whole
vocabulary is one prefix rather than a chord per action: press `Ctrl+W` and then
the key of the one you want (the table below is the list). `Ctrl+W` is
a control byte, so every terminal and `--gui` deliver it the same way, and no
window manager claims it.

It replaces `Alt+Shift+<letter>`, which could not be made to work: a desktop that
binds `Alt+Shift` (hyprland, KDE, XKB's `grp:alt_shift_toggle`, Windows) eats the
letter before the terminal sees it; the GUI's SDL path reports
`Shift+Alt+<letter>` as a lowercase keycode, so a handler keyed on the uppercase
letter never fired there; and the chords were compiled in, so a conflict left
nothing to rebind. `Alt+Shift+L` was worse than that: it is the LeetCode menu
prefix, and the Lua keymap path runs before the built-in handler, so the split
never happened even where the terminal delivered the chord.

Every pane action is also an ex command, which is the way round a still-broken
chord: `:split`, `:splitleft`, `:splitright`, `:splitup`, `:splitdown`,
`:wincmd h|j|k|l|=|z|x|q|r`, `:nextpane`, `:prevpane`, `:equalizepanes`,
`:panezoom`, `:swappanes`.

Shortcuts are global: the explorer having focus (which is where `jot <folder>`
starts, and where opening a file from the tree leaves it) does not stop `Alt+S`,
`Alt+W`, `Alt+B` or the pane chords, and neither does the Problems list. What the
focused panel owns is the *plain* keys -- a bare letter belongs to the tree or the
list under the cursor, never to the buffer behind it.

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
`Backspace` to edit, `Esc` to close. The finder's prompt owns the keyboard: a
printable key always goes into the query, `j`/`k` included, which are list
motions only while the focus is off the prompt (`Tab` cycles it). In LSP
completion, `Enter` or `Tab`
applies and `Esc` closes; the popup's own box owns the mouse, so the wheel
walks the list three rows there (the palette's step) and a click takes the row
under the pointer -- neither scrolls the code behind the list or moves the
caret through it. In the file finder, `Backspace` on an empty query
walks up a folder, but it floors at the workspace the picker opened in -- the
workspace is the folder root, and `:find src` scopes the picker to a subfolder
of it instead of a second way out. The typed characters light up in each item's
label (nvim-cmp's abbr-match highlight). The popup lists only the items the
typed word leads into (filter text, else insert text, else label -- nvim-cmp's
order); the server answers fuzzily on its own, and the previous keystroke's rows
are still in hand while the next request is in flight, so without that rule the
list carries identifiers the word has nothing to do with, and a word neither the
server nor the held rows lead into shows no popup at all. While the popup is up,
the selected item's remaining insert text previews dimmed at the caret as ghost
text (`lsp_completion_ghost_text` to disable), which waits
`lsp_completion_ghost_delay_ms` (default 150, 0 for at once) after the last
keystroke that changed the word: a preview that redrew with every keystroke is a
flicker, and the wait costs one frame at the pause, not a repaint per frame
while typing. Three rules keep it from being noise on top of that: only an item
whose insert text really continues the typed word previews anything; only the
pane that owns the popup paints it, so a split does not draw the same word
twice; and it is painted only where the caret owns the rest of the row -- so it
never lands after a closing bracket the editor auto-closed (`printf(|)`) or over
a trailing `;`, where it would sit past a character accepting would insert
before. Snippets preview their first line, since the rest cannot be drawn
inline.

The rows are then ranked by what this user reaches for. Every accepted item
counts one use of its name, per language, in `configs/completion_usage.tsv`
under the config home: the file is read on the first completion and written back
when the editor shuts down, and `lsp_completion_learn` (default true) turns the
whole thing off. Two signals decide the order among the rows the typed word
leads into. The first is the habit: 30 points per acceptance, and it stops paying
past ten, so one name cannot bury the rest. A habit fades as well as counts. Each
count is halved for every `lsp_completion_learn_half_life_days` (default 14, 0 to
keep every count forever) since that name was last used, and a name worn down
below half a use is forgotten: what the user reached for last month stops
outranking what they are reaching for now, and the file does not grow forever. The
second signal is the file being edited: a word already in the buffer scores 90, or
150 when it is on the caret's own line, so the name the code is already using
leads. Relevance sits after the
match itself and before the server's own hints, which is what lets a habit beat
a shorter label while neither can lift a row over an exact match. What is
learned is the name, not the label: a server decorates labels (clangd prefixes
each one with a space, or with a bullet for the symbols it would add an include
for), so the name is taken from the text the server matches the typed word
against and cut at the first character that cannot be part of it.

## Command reference

Open the palette with `Ctrl+P` and type an ex-style command. The prompt lives
in the statusline row at the bottom of the screen, like Neovim's cmdline, and
the fuzzy command/argument matches fill the width above it as a grid of
columns (Helix's command menu): each row carries several commands, filled
column by column, so a screenful of commands is one glance instead of a
scroll. `↑`/`↓` walk the column, `←`/`→` the row across its columns,
`PageUp`/`PageDown` move a screenful, and `Home`/`End` reach the ends of the
list; the wheel walks rows three at a time, hovering selects the row under the
pointer, and a click runs it. Whichever row is selected, an explanation box
stands on the grid's top edge saying what the command does, with its aliases
and its argument shape (`Aliases: write, save`, `Args: [path]`); a command
without a curated entry shows its one-line detail instead, so the box never
goes blank. The buffer stays fully visible while you type.

**Files & sessions:** `:q` `:w` `:wq` `:x` `:e` `:new` `:bd` `:home`
`:resume` `:recent` `:reopen` `:autosave` `:findfile [path]` (`:projectfile`,
`:ff`) - type a path on the status line, Tab completing it

**Panes & UI:** `:sp` `:vsp` `:splitleft|right|up|down` `:bn` `:bp`
`:focusleft|right|up|down` `:wincmd` `:resize*` `:minimap` `:theme`
`:colorscheme` `:zen` `:help`

**Workspace:** `:find` / `:ff [dir]` `:mkfile` `:mkdir` `:rename` `:rm`
`:cpppair` `:cppimpl` `:cppcheck [on|off|next|prev]`

**Search & edit:** `:search` `:grep` `:diagnostics` `:diagnext` `:symbols`
`:outline` `:line` `:goto` `:format` `:trim` `:upper` `:lower`
`:matchbracket` (`:percent`) `:bookmark` (`:bm`) `:bookmarknext` (`:bmnext`)
`:bookmarkprev` (`:bmprev`)
`:sortlines|desc|reverselines|uniquelines|shufflelines|joinlines` `:dupe`
`:replace*` `:surround` `:fold*` `:incnum` `:decnum` `:copypath` `:copyname`
`:Undotree` `:UndotreeClear`
`:isearch` (incremental search typed on the status line)
`:datetime` `:stats` `:Todo [keywords=TODO,FIX]` `:TodoNext` `:TodoPrev`

**LSP:** `:lspinstall` `:lspremove` `:lspstatus` `:hover` `:definition`
`:gd` `:lspback` `:lsprename <name>` `:lsprefs` `:lspactions` `:declaration`
`:typedefinition` `:implementation` `:switchheader`

**Tree-sitter:** `:tsinstall <lang>` (e.g. `:tsinstall javascript` or
`:tsinstall jsx`) `:tsupdate [lang]` `:tsstatus` `:tsplayground` `:tsreload`

**AI:** `:CodeCompanionChat` / `:Cc` `:CodeCompanionNewChat`
`:CodeCompanionSend` `:CodeCompanionStop` `:CodeCompanionActions`
`:CodeCompanionStatus` `:CodeCompanion <text>` (rewrite the selection or line)
`:CodeCompanionPrompt <name>`

**Build:** `:compile [command]` (no argument asks for the command on the status
line, pre-filled with the last one) `:make` `:cnext` `:cprev` `:ckill`
(alias `:compilekill`, `:compilestop`)

**Macros:** `:macrorecord [register]` (alias `:macrorec`, `Alt+Z q`)
`:macrostop` `:macroplay [register] [count]` (`Alt+Z p`) `:macrolist` (alias
`:macros`, `Alt+Z l`)

**Terminal & tasks:** `:term` `:termnew` `:termfloat` `:task [name]` `:tasknew <name>`
`:taskrerun`

**Markdown:** `:MarkdownPreview` `:MarkdownPreviewStop`
`:MarkdownPreviewToggle`

**HTTP client:** `:rest [name|last]` - run the request at the cursor in a
`.http` / `.rest` file (the IntelliJ / rest.nvim request syntax) through curl
and land the answer in a `[Response]` tab. `:rest <name>` runs a `# @name` /
`### name` request, `:rest last` re-sends the previous one. `{{var}}` fills
from `@var = value` in the same file, `{{$uuid}}` `{{$timestamp}}`
`{{$datetime}}` `{{$randomInt}}`, or the environment; `< ./body.json` takes the
body from a file. An unresolved `{{var}}` refuses the run rather than sending
it wrong.

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
`:discord assets` (which artwork keys to upload) - Rich Presence showing the
file, language, workspace, git branch and (when the remote is a browsable URL) a
"View Repository" button, with idling / editing / debugging states and an idle
timeout. On by default; every row is a template (see `discord_details_*` in
`:settings`). Discord artwork has to be uploaded to your Discord application
once - `packaging/discord-presence/ASSETS.md` walks through it, and
`:discord assets` lists exactly which keys the current window needs. The
emulated IPC servers several clients ship (Vesktop and the mods embedding arRPC)
are stricter than Discord itself about frame bodies, and `:discord status`
reports the reason the peer gave the last time it hung up.

## Configuration

User config lives in `~/.config/jot/` and is **Lua-first** - `config.lua`
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
jot.config.set("color_scheme", "jot")
jot.config.set("auto_save", true)
jot.config.set("auto_save_interval_ms", 5000)
```

`:reload` re-reads `settings.conf`, re-runs `config.lua`, live-applies it, and
reloads Lua plugins and Tree-sitter; `:reloadconfig` is the config-only
variant. A bundled starter config lives in `.configs/configs/`.

Five keys are a property of one buffer or one window rather than of the
session, and those can be given a local value of their own: `tab_size` and
`auto_indent` on a buffer, `relative_line_numbers`, `show_indent_guides` and
`winbar` on a window (a pane). The code that uses them - the indent paths, the
column math, the render walk, the caret, the mouse hit test - reads the local
value when there is one and the session's when there is not, so a 4-space file
open next to a 2-space one indents by four and the other by two:

```lua
jot.buffer.set_option("tab_size", 4)          -- this file only
jot.window.set_option("show_indent_guides", false)  -- this pane only
```

A local value stays in memory with the buffer or the pane it was set on (it is
not written to `settings.conf`), `jot.config.get` with a scope table reads it,
and `jot.config.info(key)` says which scope a key takes. The settings menu keeps
editing the session value of every key. See the options section of
docs/LUA_API.md for the whole surface.

Built-in defaults include `explorer_width=25`, `minimap_width=15`,
`tab_size=2`, `show_line_numbers=true`, `rainbow_brackets=true`,
`relative_line_numbers=true`, `cursor_style=block`, `cursor_blink_ms=500`,
`render_fps=120`, `idle_fps=60`, `word_highlight=true`,
`auto_save=false`, `auto_save_interval_ms=2000`, `lsp_enabled=true`,
`lsp_change_debounce_ms=120`,
`lsp_diagnostics_quiet_ms=700`, `lsp_inlay_hints=true`,
`lsp_inlay_type_hints=true`, `cpp_definitions=true`
(the C++ declaration/definition checks), `terminal_height=10`,
`terminal_float_width=85` and `terminal_float_height=75` (the floating
terminal's box, a percentage of the active pane's text rows each),
`debugger_height=12`, `status_clock=true`, `status_coding_time=true`, and
`todo_comments=true` with `todo_comments_keywords=` (the TODO comment bands).
The Extensions category adds `wakatime=false`, `wakatime_api_key=` and
`wakatime_api_url=`. The colour preview adds `colorizer=true`,
`colorizer_mode=background`, `colorizer_hex=true`, `colorizer_hex_alpha=false`,
`colorizer_hex_qml=false`, `colorizer_hex_no_hash=false`,
`colorizer_hex_0x=false`, `colorizer_names=true`, `colorizer_tailwind=false`,
`colorizer_xcolor=false`, `colorizer_functions=true`, `colorizer_xterm=false`,
`colorizer_ls_colors=false`, `colorizer_css_vars=false`,
`colorizer_sass=false`, `colorizer_only_in_strings=false`,
`colorizer_filetypes=` (the file-name suffixes the preview runs in, the web set
named above by default) and `colorizer_exclude_filetypes=` (a comma separated
list of file-name suffixes to skip, e.g. `.min.css,.map`), plus `truecolor=auto`
for 24-bit output. LSP completion rows add `completion_rich_labels=true`,
`completion_align_type=true` and `completion_dim_arguments=true`.

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

The AI assistant adds `ai_adapter=openai`, `ai_model=` (the adapter's own when
empty), `ai_base_url=`, `ai_key_env=` (the variable the token is read from),
`ai_command=` (the CLI for the `cmd` adapter), `ai_system_prompt=`,
`ai_stream=true`, `ai_temperature=0.2` and `ai_max_tokens=0` (no limit; the
Anthropic kind uses 4096 when it is 0, since its API requires one).

The caret is configured with two keys:

- `cursor_style` - `block` (the default) or `bar`, both blinking; `steady_bar` /
  `steady_block` keep that shape without blinking. The shape is emitted with the
  terminal's steady DECSCUSR form in the TUI and drawn directly in the GUI.
- `cursor_blink_ms` - half of the blink cycle, in milliseconds: the caret is
  visible for that long, then hidden for the same. `0` makes it solid. The
  phase is jot's own clock, shared by the terminal and GUI frontends. Typing
  lands it solid and holds it there for 700 ms, and the cycle runs from the end
  of that pause, so the half after it is a whole one rather than whatever
  fragment a keystroke-timed clock had left. A mouse click is not a typing
  burst: it re-anchors the phase with no hold, so the caret shows at the clicked
  cell and blinks a normal half from there. A hidden caret is put back on its
  own cell first and hidden second: the terminal's cursor is still *somewhere*
  while DECTCEM hides it, and left at the last cell a row write moved it to (the
  bottom-right corner of the status row) it flashed up there whenever the
  terminal repainted the cursor on its own. The same rule covers the size probe
  that reads the viewport with a cursor-position query: it has to park the cursor
  in the bottom-right corner to ask, so it hides the cursor for the round trip
  and puts back exactly the visibility it found, instead of sending the caret to
  the corner every couple of seconds.

The mouse wheel can scroll smoothly. `smooth_scroll` is off by default - the
wheel jumps a notch per event, as it always has - and `smooth_scroll=true`
eases a notch over a few frames instead, using neoscroll.nvim's model:
the same easing functions (`smooth_scroll_easing`, `linear` by default, plus
`quadratic`, `cubic`, `quartic`, `quintic`, `circular` and `sine`), the same
per-notch duration (100ms) scaled by `smooth_scroll_duration_multiplier` (1.0),
and the same behaviour for a burst - a notch arriving mid-animation extends it,
reversing eases it to a stop, and a held wheel can never fall more than two
notches behind the viewport. It only moves the viewport, so the caret and every
other navigation path keep scrolling instantly, and moving the viewport another
way (a jump, a fold, the caret pulled back into view) ends the animation where
it stands. The GUI frontend already slides its content pixel by pixel and is
left alone.

Sideways is a separate gesture with no animation: a horizontal wheel report
(buttons 66/67), or `Shift+wheel` / `Ctrl+wheel` where the terminal only sends
vertical ones, moves `scroll_x` four cells per notch and leaves the caret, the
vertical position and the text scale alone.

The caret's colours come from the theme (`cursor`, or `fg_cursor`/`bg_cursor`).
Where the caret is painted by jot - the GUI - it uses whichever of the two
contrasts with the cell underneath, so it stays visible over comments,
selections and dimmed text. In the TUI the hardware cursor is drawn by the
terminal emulator, which owns its colour; jot only sets its shape and position.

`render_margin` (default `0`) is the number of columns left unpainted on the
right edge. `0` uses the full width, so full-width rules and the bottom bar end
on the last column. Raise it to `1` only if a terminal corrupts the frame when
its last column is written: the renderer already disables autowrap and addresses
each row with an absolute cursor move, so the wrap hazard the margin guarded
against cannot trigger on a conforming terminal.

Emmet abbreviations expand on Tab in markup and style sheets. Typing an
abbreviation and pressing Tab replaces it with the markup or declarations it
stands for, with the caret left where the next thing goes - inside the element
the expansion built, or in the value it did not fill:

```text
div.card>ul>li.item$*3     <div class="card">
                             <ul>
                               <li class="item1"></li>
                               <li class="item2"></li>
                               <li class="item3"></li>
                             </ul>
                           </div>
a                          <a href=""></a>          (the implied href)
m10-20                     margin: 10px 20px;
d:f                        display: flex;
c#fff                     color: #fff;
p5+m10                     padding: 5px;
                             margin: 10px;
```

It works in `.html`, `.htm`, `.jsx`, `.tsx`, `.css`, `.scss`, `.sass` and
`.less`. Anything that is not an abbreviation is refused and the Tab goes to
indentation, so a word in a comment or an expression in JSX is left alone;
`emmet=false` turns the whole thing off. The expansion is emitted as a snippet,
so Tab after it walks the stops it left behind. The CSS side knows the usual
property shorthands (`m`, `p`, `w`, `h`, `d`, `pos`, `bg`, `fz`, `ta`, `jc`,
`ai`, `bdr`, `bxz`, …) and value keywords (`d:f` is `flex`, `pos:a` is
`absolute`, `ta:c` is `center`), takes a bare number as the property's own unit
(`10` is `10px`, `10p` is `10%`, `z10` is `10`), and `!` for `!important`.

### The workspace's CSS vocabulary

The class names a project uses and the custom properties it declares are spread
across every file in the tree, which is exactly what a language server attached
to one file cannot see - and what plain HTML and CSS have no server for at all.
jot reads the tree itself: once when a workspace opens, and again after every
save. The result is offered where the names belong, and nowhere else.

```html
<div class="▏">           <!-- every class name in the workspace -->
```

```css
.card { color: var(--▏); }  /* every custom property in the workspace */
```

What is read: `class="..."`, `class='...'` and `className="..."` in markup
(`.html`, `.htm`, `.jsx`, `.tsx`, `.vue`, `.svelte`, `.astro`, and the usual
templating extensions), every `.name` in a selector position in a style sheet
(`.css`, `.scss`, `.sass`, `.less`), and every `--name:` declaration. What is
skipped: `node_modules`, `build`, `dist`, `target`, `vendor` and the other
directories a generated name lives in, attributes that are commented out,
templated values (`class={x}`, `{{ loop.index }}`), and any dotted word - a
class name has no `.` in it, and skipping the dot is what keeps `icon.svg` and
`a.b` out of the list.

The names are filtered by the same pass as a server's response, so a typed
prefix narrows them the way any completion does, and they join whatever a server
answered rather than replacing it: in a React project `ts_ls` supplies the
symbols and jot supplies the class names the server's own file scope missed.

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

### Windows releases

`jot-windows-x86_64-setup.exe` installs for the current user
(`%LOCALAPPDATA%\Programs\jot`), so there is no admin prompt: it writes
`bin\jot.exe` and `share\jot`, adds the `bin` directory to the user's `PATH`,
creates a Start menu entry and (optionally) a desktop shortcut, and registers
itself under Apps & features with an uninstaller that leaves `%APPDATA%\jot`
intact. `jot-windows-x86_64.zip` carries the same tree for a portable copy:
unpack it anywhere and run `bin\jot.exe`. Both are built by
`packaging/windows/build-static-exe.sh` (the static exe, import-gated) and
`packaging/windows/build-installer.sh` (the NSIS installer around it).

### Windows / MSVC (experimental)

```bat
cmake --preset windows-msvc-vs2026-vcpkg
cmake --build --preset windows-msvc-vs2026-vcpkg-debug
```

The generated solution lands at `build\vs2026-x64\jot.sln` - open it directly
or open the folder and pick the preset. Use 64-bit MSVC; the Windows target
does not support `-A win32`. Lua is fetched at a pinned version when missing,
and `libuv`/`utf8proc` are fetched automatically unless you provide them via
vcpkg (set `JOT_FETCH_DEPS=OFF` to opt out).

Installed files:

- `$prefix/bin/jot`
- `$prefix/share/jot/lua/`
- `$prefix/share/jot/configs/`

Every artifact uses that layout (`bin/jot` above `share/jot`) and ships it
complete: the colour schemes, the Lua runtime and any vendored LSP payload are
found relative to the binary itself, so a release archive unpacked anywhere and
a per-user install into an arbitrary directory both work with nothing copied
into place by hand. The prefix compiled in at build time
(`JOT_DEFAULT_DATA_DIR`) is only a fallback for a system-wide install; the
lookup itself is `src/base/exe_path.h`. What the user owns stays in
`%APPDATA%\jot` on Windows and `~/.config/jot` elsewhere.

## Building from source

Requirements:

- CMake 3.16+
- A C++17 compiler
- LuaJIT 2.1 development files (or let `JOT_FETCH_DEPS` fetch and build them)
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
src/base/        primitives: strings, files, shells, the event loop, task queue
src/text/        text editing, cursor movement, selection, clipboard, search
src/syntax/      languages, highlighting, folding, tree-sitter, colour scanners
src/ui/          raw terminal and UI abstraction
src/render/      buffer drawing, minimap, overlays, panels, UI views
src/input/       keyboard, mouse, command palette, command dispatch
src/tools/       subprocess and protocol clients: telescope, image viewer, discord
src/workspace/   explorer, sidebar, right dock, tasks, project search
src/lsp/         the language server protocol client and completion ranking
src/dap/         the debug adapter protocol client
src/terminal/    pty-backed terminal sessions and their panel
src/git/         repository state, the git client, the panel and its painters
src/extras/      optional features: WakaTime, coding time, the Bible, LeetCode
src/editor/      the editor engine: state, buffers, panes, integrations, surfaces
src/lua/         C++ bridge for the embedded Lua plugin/theme API
runtime/lua/     bundled Lua features (ui kit, snippets, previews, the AI
                 assistant under features/ai/) plus lib/, the shipped runtime
                 library (json, base64, text, fs, inspect, iter, version,
                 table, utf8), all embedded into the binary
docs/            user-facing documentation
test/           unit tests
```

Build graph highlights: `jot_engine` is the aggregated static engine target, and
each module under `src/` is one static library behind it (`jot_base`, `jot_text`,
`jot_syntax`, `jot_ui`, `jot_render`, `jot_input`, `jot_tools`, `jot_workspace`,
`jot_lsp`, `jot_dap`, `jot_terminal`, `jot_git`, `jot_extras`, `jot_editor`,
`jot_lua_api`), each compiled from its own object library. A source may include
its own module and the modules below it in that list, never one above;
`tools/check_module_deps.py` enforces the order in CI.

## Notes and limitations

- The workflow is modeless by design - typing edits text directly, selection is
  mouse- or `Shift+Arrow`-driven, and common commands use standard Ctrl/Alt
  shortcuts.
- The integrated terminal suits normal shell/task workflows but is not meant
  to replace a full standalone terminal emulator.
- Lua globals are injected for plugins; no `require` ceremony needed.
- Windows support is experimental; Linux/macOS is the primary target.
