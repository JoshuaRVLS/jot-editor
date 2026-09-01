# jot Theme Guide

Themes are native JSON data. The primary location is
`~/.config/jot/configs/colors/`; legacy `~/.config/jot/themes/` is also scanned.
Bundled themes live in `.configs/configs/colors/`.

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

All compatibility highlight groups documented by the editor remain supported,
including syntax captures such as `@keyword.control`, `@function.method`, and
`@punctuation.bracket`. See the color slot names in the source theme defaults
and use standard 0-255 xterm color codes.
