# jcode Theme Guide

Themes are Python scripts located in `~/.config/jcode/themes/`.

## Creating a Theme

1.  Create a file `~/.config/jcode/themes/my_theme.py`.
2.  Import `jcode_api` (ensure it's in path).
3.  Use `jcode_api.set_theme_color` to define colors.

```python
import jcode_api

# Define colors
BG_DARK = 235
FG_LIGHT = 250

# Apply
jcode_api.set_theme_color("default", FG_LIGHT, BG_DARK)
jcode_api.set_theme_color("line_num", 240, BG_DARK)
jcode_api.set_theme_color("keyword", 208, BG_DARK) # Orange
# ...
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

jcode uses standard 256-color codes (Xterm).
- 0-15: Standard ANSI colors (Black, Red, Green...)
- 16-231: 6x6x6 Color Cube
- 232-255: Grayscale (232=Black, 255=White)

Tip: Use `-1` for transparency (inherit terminal background), but be careful with rendering artifacts if the editor background is not -1.
