# jot Theme Guide

Themes are native JSON data. The primary location is
`~/.config/jot/configs/colors/`; legacy `~/.config/jot/themes/` is also scanned.
Bundled themes live in `.configs/configs/colors/` and currently ship `dark`,
`light`, `tokyonight`, `nord`, `gruvbox`, `dracula`, `catppuccin`, `onedark`,
`monokai`, and `solarized`. Colors are xterm 256 indices, so the popular
palettes are faithful but coarse approximations of their original 24-bit
colors; tune any slot by copying the file to `~/.config/jot/configs/colors/`.

Apply with `:colorscheme light` or from Lua:

```lua
set_hl("Normal", { fg = 252, bg = 234 })
```

Theme files contain highlight groups mapped to foreground/background xterm
256-color values. Use `-1` or `null` for transparent/inherited values.

```json
{
  "Normal": {"fg": 252, "bg": 234},
  "Comment": {"fg": 244, "bg": 234},
  "Keyword": {"fg": 81, "bg": 234},
  "Visual": {"fg": 234, "bg": 110}
}
```

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
| `keyword` | generic keywords | — |
| `keyword.control` | `if`, `for`, `while`, `return`, `break` | `keyword` |
| `keyword.storage` | `static`, `const`, `class`, `struct` | `type` |
| `keyword.directive` | `#include`, `#define`, imports | `constant` |
| `string` | string literals | — |
| `string.escape` | `\n`, `\t` inside strings | `builtin` |
| `comment` | comments | — |
| `number` | numeric literals | — |
| `function` | function definitions and calls | — |
| `function.method` | method calls/definitions | `function` |
| `function.constructor` | `Foo()` constructions | `type` |
| `type` | type identifiers | — |
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
`Function`, `Type`, `Cursor`, `Visual`, `Search`, `StatusLine`,
`StatusLineMsg`, `StatusLineLogo`, `StatusLineFile`, `StatusLineInfo`,
`StatusLineWarn`, `StatusLineError`, `StatusLineMuted`, `FloatBorder`,
`WinSeparator`, `WinActiveBorder`, `TabLine`, `TabLineSel`, `TabLineFill`,
`TabClose`, `Sidebar`, `SidebarDir`, `SidebarSel`, `SidebarSelNC`,
`SidebarBorder`, `DiagnosticError`, `DiagnosticWarn`, `DiagnosticInfo`,
`DiagnosticHint`, `Pmenu`, `PmenuSel`, `TelescopeNormal`,
`TelescopeSelection`, `TelescopePreviewNormal`, `Terminal`, `TerminalTab`,
`TerminalTabActive`, `TerminalTabFocused`, `TerminalTabClose`,
`TerminalTabPlus`, `TerminalTabSeparator`.

Git status colors for file rows (git view), file tabs, and the diff panel use
`git_modified`, `git_added`, `git_untracked`, `git_deleted`, `git_renamed`,
and `git_conflict` (`fg` text / `bg` row tint). Themes that omit them fall
back to the built-in ANSI defaults, which do not match the colorscheme.

## Authoring a theme

### Inherit a base with `extends`

A theme only needs to list what it overrides. Start from any bundled theme and
keep its full look — file explorer, status bar, git colors, diagnostics, and
syntax — by extending it:

```json
{
  "extends": "tokyonight",
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
cp .configs/configs/colors/dark.json ~/.config/jot/configs/colors/mine.json
```

A minimal annotated theme:

```json
{
  "Normal": {"fg": 188, "bg": 234},          // plain text / editor background
  "Comment": {"fg": 103, "bg": 234},         // comments
  "keyword": {"fg": 176, "bg": 234},         // all keywords
  "keyword.control": {"fg": 141, "bg": 234}, // if/for/while - override control
  "string": {"fg": 151, "bg": 234},          // string literals
  "number": {"fg": 216, "bg": 234},          // numbers, constants fall back here
  "function": {"fg": 222, "bg": 234},        // function names
  "function.method": {"fg": 180, "bg": 234}, // method names (optional: keep = function)
  "type": {"fg": 117, "bg": 234},            // type identifiers
  "property": {"fg": 152, "bg": 234},        // obj.field members
  "punctuation": {"fg": 145, "bg": 234},     // dim the brackets/semicolons
  "tag": {"fg": 175, "bg": 234},             // HTML/JSX tags
  "attribute": {"fg": 216, "bg": 234}        // HTML/JSX tag attributes
}
```

Rules:

- Colors are xterm 256 indices (0-255). `fg`/`bg` of `-1` leaves that side
  untouched so a group can change only one side.
- Any slot you omit falls back per the table above; base slots are
  `default`, `keyword`, `string`, `comment`, `number`, `function`, `type`.
- JSON keys are matched against slot names directly — dotted and snake forms
  both work (`"keyword.control"` / `"keyword_control"`,
  `"constant.macro"` / `"constant_macro"`, `"property"` / `"field"`), and a
  leading `@` is ignored so tree-sitter capture names can be used as-is.
- `:colorscheme <name>` switches live; the choice persists in the settings
  file (`color_scheme`). Names resolve case-insensitively.
- After editing a theme file, switch away and back (`:colorscheme dark`,
  `:colorscheme mine`) or restart to reload it.
