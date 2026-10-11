# jot Theme Guide

Themes are native JSON data. The primary location is
`~/.config/jot/configs/colors/`; legacy `~/.config/jot/themes/` is also scanned.

jot ships three themes: its own scheme -- a port of gruber-darker -- and a port
of kepano's Flexoki.

| Theme | Look |
|---|---|
| `jot` (default) - *gruber darker* | a port of John Gruber's gruber-darker: the `#181818` ground, the `#e4e4e4` ink, yellow keywords, quartz types, wisteria numbers, green strings, niagara-blue functions, brown comments, one raised `#282828` chrome |
| `flexoki-dark` | Flexoki's ink palette: black (`#100f0f`) paper, base-200 text, green keywords, cyan strings |
| `flexoki-light` | the same scheme on Flexoki paper (`#fffcf0`) with the 600-step accents |

### jot - gruber darker

The default scheme, and the one jot is known by. It is a port of
[gruber-darker](https://github.com/blazkowolf/gruber-darker.nvim), John Gruber's
working scheme, with its roles mapped onto jot's slots: the `#181818` near-black
ground, the `#e4e4e4` ink, and one yellow (`#ffdd33`) for the language itself.

The port reads its roles off the upstream scheme rather than guessing at them.
Yellow is the keyword and control-flow ink, and the accent a hand lands on: the
active pane border, the cursor line number, the focused terminal tab, the pin on
a tab, the cursor block. **Quartz** (`#95a99f`) takes types, constants and
directives, **wisteria** (`#9e95c7`) numbers, brackets and hints, **green**
(`#73d936`) strings and the info diagnostic, **niagara** (`#96a6c8`) functions,
methods, fields and renames, **brown** (`#cc8c3c`) comments, **red**
(`#f43841`) errors and deletions. The scheme is deliberately loud: the ground is
black and every ink is a bright one.

The neutrals are Gruber's own steps: the `#282828` chrome (sidebar, winbar, tab
strip, status line, popup menus), the `#453d41` band a selected row, a matched
bracket or a hovered row sits on, the `#101010` query field, and his `#52494e`
line numbers and borders. The frame separates the panes through a `#6a7180`
separator -- Gruber's blue-grey lifted one step, because a terminal window has
no shadows and his own separator step is too close to the ground to read. The
diagnostic bands are his red and yellow tinted over the ground (`#401e1f`
error, `#3a351c` warning), and the search band is his own light one
(`#f4f4ff` under the muted `#565f73`).

The reference for the port is the Neovim one
([blazkowolf/gruber-darker.nvim](https://github.com/blazkowolf/gruber-darker.nvim)),
with [rexim's Emacs theme](https://github.com/rexim/gruber-darker-theme) behind
it; both descend from John Gruber's original BBEdit scheme.

The Flexoki pair is [Flexoki](https://stephango.com/flexoki) by kepano (MIT
licensed), ported slot by slot from the official VS Code and Helix themes: the
400-step accents on the dark surfaces, the 600-step accents on paper, and the
base steps (50-950) for text, line numbers, borders and every panel surface.
Accent roles follow the upstream themes rather than jot's -- keywords are green,
numbers purple, operators red -- which is what makes it recognisably Flexoki.

Every shipped theme is an exact 24-bit palette: every slot names a `#rrggbb`
colour, and jot paints it verbatim (SGR `38;2`/`48;2`, or the exact value in the
GUI) instead of rounding it to the nearest of 256 entries. A slot still accepts
an xterm 256 index for a theme written that way, and the two forms can be mixed
in one file. Tune any slot by copying a file to `~/.config/jot/configs/colors/`.
Every name the bundled catalog has been keyed on before -- `dark`, `light`, and
the `jot-dark` / `jot-light` pair that replaced them -- still resolves to the
jot theme that superseded them, so a `color_scheme` written before the change
keeps working. Any other name needs a file of its own (and a file you write
under one of those names is yours, not an alias).

Apply with `:colorscheme jot` or from Lua:

```lua
set_hl("Normal", { fg = "#e4e4e4", bg = "#181818" })
set_hl("Keyword", { fg = 215 }) -- an xterm index still works
```

Theme files contain highlight groups mapped to foreground/background colours:
an exact `"#rrggbb"` string, or an xterm 256 index as a number. A three-digit
`"#rgb"` is accepted and expanded, and an eight-digit `"#rrggbbaa"` drops its
alpha channel (every surface composites over an opaque background). A value that
is neither is ignored, leaving the slot at whatever it inherited. Use `-1` or
`null` for transparent/inherited values.

```json
{
  "Normal": {"fg": "#e4e4e4", "bg": "#181818"},
  "Comment": {"fg": "#cc8c3c", "bg": "#181818"},
  "Keyword": {"fg": "#ffdd33", "bg": "#181818"},
  "Visual": {"fg": 231, "bg": 240}
}
```

The same two forms are accepted wherever the Lua API takes a colour - `set_hl`,
`jot.theme.set_color`, and a decoration's `fg`, `bg`, `underline_fg`, `virt_fg`
and `virt_bg` (`jot.decoration.set`) - so a plugin or a theme can name the exact
colour it means instead of hunting for the nearest index. `jot.decoration.list()`
hands an exact colour back as the `#rrggbb` string it was set with, and a palette
index back as a number; a value that is neither is ignored, leaving that colour
unset rather than painting a wrong one.

Exact colours need a terminal that understands 24-bit colour (`COLORTERM`
reports `truecolor`/`24bit`, or `TERM` ends in `-direct`); elsewhere jot folds
each one down to the nearest palette entry - text, background and underline
alike - so the theme still reads correctly on a 256-colour terminal. The
`truecolor` setting forces the answer either way.

## Group names

Group names accept a few spellings. Neovim-style group names (`"Normal"`,
`"StatusLineInfo"`, `"Pmenu"`, `"TerminalTabFocused"`) are translated to their
jot slot; tree-sitter capture style (`"@keyword.control"`, `"@property"`) drops
the `@` and maps to the same slot as the dotted/snake form below.

### Syntax (tree-sitter and regex) slots

Each capture produced by a query maps to one of these slots. Slots marked
*inherited* fall back to a base color when a theme does not set them; setting
one explicitly in a theme overrides the fallback.

| Slot | What it colors | Falls back to |
|---|---|---|
| `keyword` | generic keywords | - |
| `keyword.control` | `if`, `for`, `while`, `return`, `break` | `keyword` |
| `keyword.storage` | `static`, `const`, `class`, `struct` | `type` |
| `keyword.directive` | `#include`, `#define`, imports | `constant` |
| `string` | string literals | - |
| `string.escape` | `\n`, `\t` inside strings | `builtin` |
| `comment` | comments | - |
| `number` | numeric literals | - |
| `function` | function definitions and calls | - |
| `function.method` | method calls/definitions | `function` |
| `function.constructor` | `Foo()` constructions | `type` |
| `type` | type identifiers | - |
| `type.builtin` | `int`, `float`, `auto` | `builtin` |
| `variable` | plain identifiers | `default` |
| `parameter` | function parameters | `default` |
| `property` | `obj.field`, members (alias: `field`) | `default` |
| `constant` | constants | `number` |
| `constant_macro` | `#define NAME`, preprocessor constants | `constant` |
| `builtin` | `True`/`None`, builtin functions | `type` |
| `operator` | `+`, `==`, `->` | `keyword` |
| `tag` | markup tags (`<div>`) | `keyword` |
| `attribute` | markup tag attributes (`class=`) | `type` |
| `namespace` | `namespace`, package qualifiers | `default` |
| `module` | imported module names | `default` |
| `punctuation` | generic punctuation | `default` |
| `punctuation.bracket` | `() [] {}` | `punctuation` |
| `punctuation.delimiter` | `, ; .` | `punctuation` |

### UI slots

`Normal`, `NormalFloat`, `LineNr`, `Comment`, `Keyword`, `String`, `Number`,
`Function`, `Type`, `Cursor`, `CursorLine`, `CursorLineNr`, `Visual`,
`Search`, `CurSearch`, `BracketMatch`, `WordHighlight`,
`WordHighlightStrong`, `StatusLine`,
`StatusLineMsg`, `StatusLineLogo`, `StatusLineFile`, `StatusLineInfo`,
`StatusLineWarn`, `StatusLineError`, `StatusLineMuted`, `FloatBorder`,
`WinSeparator`, `WinActiveBorder`, `TabLine`, `TabLineSel`, `TabLineFill`,
`TabClose`, `Sidebar`, `SidebarDir`, `SidebarSel`, `SidebarSelNC`,
`SidebarBorder`, `DiagnosticError`, `DiagnosticWarn`, `DiagnosticInfo`,
`DiagnosticHint`, `Pmenu`, `PmenuSel`, `TelescopeNormal`,
`TelescopeSelection`, `TelescopePreviewNormal`, `TelescopeQuery`,
`Terminal`, `TerminalTab`,
`TerminalTabActive`, `TerminalTabFocused`, `TerminalTabClose`,
`TerminalTabPlus`, `TerminalTabSeparator`, `Winbar`, `WinbarBreadcrumb`,
`WinbarSeparator`, `WinbarHover`.

`Winbar` is the breadcrumb row's own band and the ink of its file and symbol
crumbs, `WinbarBreadcrumb` the quieter labels above them, `WinbarSeparator` the
chevron between crumbs, and `WinbarHover` the band under the pointer.

`CursorLine` tints the row under the text cursor (`bg` only is enough) and
`CursorLineNr` colors its line number; `CurSearch` is the highlight of the
search result the cursor currently sits on (the next/previous match when
stepping), which reads brighter than the plain `Search` hits so the current
target never blends into the rest. `BracketMatch` boxes the bracket under the
caret and its partner while the pair is in view (`fg` colors the bracket
glyphs, `bg` the band behind both cells), and each bundled theme keeps it on
the same soft surface as its hover bands. `WordHighlight` and
`WordHighlightStrong` are the occurrence highlight: every other place the
identifier under the caret (or the text a selection marks) shows in the viewport
wears the plain band, and the caret's own word the strong one. Both are bands only, so a theme names `bg` and
leaves `fg` at `-1` to keep the token's own colour, the same spelling the git
slots use for the half they do not set. `DiagnosticError` and
`DiagnosticWarn` colour the squiggle, the line number and the inline message of
their severity, and their `bg` is the band the renderer paints across the
*whole* line that holds such a finding - the gutter, the code, and the space
past the end of the text - so a problem is findable while scrolling instead of
only where the squiggle sits. A theme that names no band leaves those rows on
the pane background; `DiagnosticInfo` and `DiagnosticHint` carry no band (their
`bg` is ignored), and selection, search hits and decorations still paint over
the band, while the occurrence highlight steps aside and leaves the row edge to
edge. The cursor-row tint can be turned off with
the `highlight_cursor_line` setting (`false`), and both `CurSearch` and
`CursorLine` fall back to sensible defaults when a theme omits them.

Git status colors for file rows (git view), file tabs, and the diff panel use
`git_modified`, `git_added`, `git_untracked`, `git_deleted`, `git_renamed`,
and `git_conflict` (`fg` text / `bg` row tint). Themes that omit them fall
back to the built-in ANSI defaults, which do not match the colorscheme.

The `Telescope*` slots paint the file finder. The picker is drawn on the editor's
own background (`Normal`): it fills both boxes, their frames included, and stops
at their borders -- the column between the two boxes keeps the float's own colour
-- so the frames sit on the buffer's ground and a border reads as a line on the
text rather than the edge of a lighter plate. `TelescopeNormal` therefore
supplies the picker's `fg` only, and a `bg` on it is ignored (like
`TelescopePreviewNormal`'s -- both are leftovers from when the panes were tinted
apart from the editor). `TelescopeSelection` is the band on the selected and
hovered row, the only thing painted over that background. The query field and the
file view are not surfaces of their own either, and `TelescopeQuery` supplies
only its `fg` as well.

## Authoring a theme

### Inherit a base with `extends`

A theme only needs to list what it overrides. Start from any bundled theme and
keep its full look - file explorer, status bar, git colors, diagnostics, and
syntax - by extending it:

```json
{
  "extends": "jot",
  "Comment": {"fg": 244},
  "keyword.control": {"fg": 214},
  "StatusLine": {"fg": 188, "bg": 236}
}
```

Any group the theme does not set is inherited from the base (`extends` chains
depth and cycles are guarded). Without `extends`, unlisted groups fall back to
the built-in ANSI defaults, which is why explorer and status bar colors would
otherwise not follow a minimal custom theme.

### Standalone theme

For a fully self-contained theme, copy a bundled theme as a starting point,
then tune the syntax slots:

```bash
mkdir -p ~/.config/jot/configs/colors
cp .configs/configs/colors/jot.json ~/.config/jot/configs/colors/mine.json
```

A minimal annotated theme:

```json
{
  "Normal": {"fg": "#e4e4e4", "bg": "#181818"},         // plain text / editor background
  "Comment": {"fg": "#cc8c3c", "bg": "#181818"},        // comments
  "keyword": {"fg": "#ffdd33", "bg": "#181818"},        // all keywords
  "keyword.control": {"fg": "#ffdd33", "bg": "#181818"}, // if/for/while - override control
  "string": {"fg": "#73d936", "bg": "#181818"},         // string literals
  "number": {"fg": "#9e95c7", "bg": "#181818"},         // numbers, constants fall back here
  "function": {"fg": "#96a6c8", "bg": "#181818"},       // function names
  "function.method": {"fg": "#96a6c8", "bg": "#181818"}, // method names (optional: keep = function)
  "type": {"fg": "#95a99f", "bg": "#181818"},           // type identifiers
  "property": {"fg": "#96a6c8", "bg": "#181818"},       // obj.field members
  "punctuation": {"fg": "#e4e4e4", "bg": "#181818"},    // punctuation carries no hue
  "tag": {"fg": "#ffdd33", "bg": "#181818"},            // HTML/JSX tags
  "attribute": {"fg": "#96a6c8", "bg": "#181818"}       // HTML/JSX tag attributes
}
```

### Bringing in a VSCode pack

`tools/vscode_themes_import.py <pack-dir> <output-dir>` converts a VSCode theme
pack (colors + TextMate scopes) into jot schemes; point the output at
`~/.config/jot/configs/colors` so the imports stay yours. jot's own scheme is
never overwritten by an import. The source's colours are written through as
hex, so an imported scheme keeps the 24-bit values it was designed with instead
of landing on the nearest palette entry.

Rules:

- Colors are exact `"#rrggbb"` strings; an xterm 256 index (0-255) is also
  accepted and can be mixed with hex values in one file. `fg`/`bg` of `-1`
  leaves that side untouched so a group can change only one side.
- Any slot you omit falls back per the table above; base slots are
  `default`, `keyword`, `string`, `comment`, `number`, `function`, `type`.
- JSON keys are matched against slot names directly - dotted and snake forms
  both work (`"keyword.control"` / `"keyword_control"`,
  `"constant.macro"` / `"constant_macro"`, `"property"` / `"field"`), and a
  leading `@` is ignored so tree-sitter capture names can be used as-is.
- `:colorscheme <name>` switches live; the choice persists in the settings
  file (`color_scheme`). Names resolve case-insensitively.
- After editing a theme file, switch away and back (`:colorscheme jot`,
  `:colorscheme mine`) or restart to reload it.
