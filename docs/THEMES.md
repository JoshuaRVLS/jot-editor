# jot Theme Guide

The primary theme location is `~/.config/jot/configs/colors/`.

Legacy themes in `~/.config/jot/themes/` still load.

## Bundled Themes

jot now ships with 10 popular default themes (stored in `.configs/configs/colors/`):

- `dracula`
- `gruvbox_dark`
- `nord`
- `solarized_dark`
- `monokai`
- `onedark`
- `tokyonight`
- `catppuccin_mocha`
- `kanagawa_dragon`
- `ayu_mirage`

Apply any of them with:

```python
from jot_api import vim
vim.cmd.colorscheme("tokyonight")
```

## Creating a Theme

1. Create `~/.config/jot/configs/colors/my_theme.py`.
2. Use the Neovim-style `vim.api.nvim_set_hl(...)` facade, or the lower-level `set_theme_color(...)`.

```python
from jot_api import vim

theme = {
    "Normal": {"fg": 252, "bg": 234},
    "LineNr": {"fg": 240, "bg": 234},
    "Comment": {"fg": 244, "bg": 234},
    "Keyword": {"fg": 81, "bg": 234},
    "String": {"fg": 114, "bg": 234},
    "Function": {"fg": 223, "bg": 234},
    "Type": {"fg": 110, "bg": 234},
    "Visual": {"fg": 234, "bg": 110},
    "StatusLine": {"fg": 252, "bg": 236},
    "FloatBorder": {"fg": 239, "bg": 234},
    "TelescopeSelection": {"fg": 234, "bg": 110},
}

for group, spec in theme.items():
    vim.api.nvim_set_hl(0, group, spec)
```

To load a theme from config:

```python
from jot_api import vim

vim.cmd.colorscheme("my_theme")
```

## Color Slots

| Slot Name | Description |
|-----------|-------------|
| `default` | Main text color and background |
| `line_num` | Line numbers in gutter |
| `cursor` | Cursor color (usually swap fg/bg) |
| `status` | Status bar |
| `status_message` | Status message text on line 2 |
| `command` | Command palette / input areas |
| `panel_border` | Borders of split panes and popups |
| `selection` | Highlighted text selection |
| `search_match` | Search result highlight |
| `minimap` | Minimap dots |
| `active_border` | Border of active pane |
| `sidebar` | Sidebar base foreground/background |
| `sidebar_directory` | Sidebar directory/file icon text |
| `sidebar_selected` | Focused sidebar selected row |
| `sidebar_selected_inactive` | Unfocused sidebar selected row |
| `sidebar_border` | Sidebar right border |
| `tab_active` | Active file tab |
| `tab_inactive` | Inactive file tab |
| `tab_close` | File tab close glyph (`x`) |
| `tab_separator` | File tab separator (`|`) |
| `diagnostic_error` | Diagnostic marker/tooltip error color |
| `diagnostic_warning` | Diagnostic marker/tooltip warning color |
| `diagnostic_info` | Diagnostic marker/tooltip info color |
| `diagnostic_hint` | Diagnostic marker/tooltip hint color |
| `terminal` | Integrated terminal foreground/background |
| `terminal_tab_inactive` | Inactive terminal tab |
| `terminal_tab_active` | Active terminal tab |
| `terminal_tab_focused` | Focused terminal tab |
| `terminal_tab_close` | Terminal tab close glyph (`x`) |
| `terminal_tab_plus` | New terminal tab button (`+`) |
| `terminal_tab_separator` | Terminal tab separator (`|`) |
| `telescope` | File finder text |
| `telescope_selected` | Selected item in file finder |
| `telescope_preview` | Preview pane text in file finder |

### Syntax Highlighting
| Slot Name | Description |
|-----------|-------------|
| `keyword` | `if`, `else`, `return`, `class`, etc. |
| `string` | `"String literals"` |
| `comment` | `// Comments` or `# Comments` |
| `number` | `123`, `3.14` |
| `function` | Function names, calls |
| `type` | Types (`int`, `bool`, ClassNames) |
| `bracket_match` | Matched bracket highlight |

### Extra Highlight Group Mapping

You can also style these Neovim-style groups in theme files:

- `Search`, `IncSearch`
- `StatusLineMsg`
- `WinActiveBorder`
- `TabLine`, `TabLineSel`, `TabLineFill`
- `TabClose`
- `DiagnosticError`, `DiagnosticWarn`, `DiagnosticInfo`, `DiagnosticHint`
- `Sidebar`, `SidebarDir`, `SidebarSel`, `SidebarSelNC`, `SidebarBorder`
- `Terminal`, `TerminalTab`, `TerminalTabActive`, `TerminalTabFocused`
- `TerminalTabClose`, `TerminalTabPlus`, `TerminalTabSeparator`

## Terminal Colors

jot uses standard 256-color codes (Xterm).
- 0-15: Standard ANSI colors (Black, Red, Green...)
- 16-231: 6x6x6 Color Cube
- 232-255: Grayscale (232=Black, 255=White)

Tip: Use `-1` for transparency (inherit terminal background), but be careful with rendering artifacts if the editor background is not -1.
