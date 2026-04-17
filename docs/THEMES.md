# jot Theme Guide

The primary theme location is `~/.config/jot/configs/colors/`.

Legacy themes in `~/.config/jot/themes/` still load.

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
| `command` | Command palette / input areas |
| `panel_border` | Borders of split panes and popups |
| `selection` | Highlighted text selection |
| `minimap` | Minimap dots |
| `telescope` | File finder text |
| `telescope_selected` | Selected item in file finder |

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

## Terminal Colors

jot uses standard 256-color codes (Xterm).
- 0-15: Standard ANSI colors (Black, Red, Green...)
- 16-231: 6x6x6 Color Cube
- 232-255: Grayscale (232=Black, 255=White)

Tip: Use `-1` for transparency (inherit terminal background), but be careful with rendering artifacts if the editor background is not -1.
