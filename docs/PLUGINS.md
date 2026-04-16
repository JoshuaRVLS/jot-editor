# jcode Plugin Development Guide

jcode supports plugins written in Python.

Primary config is loaded from `~/.config/jcode/configs/init.py`.
Plugin files are loaded from `~/.config/jcode/configs/plugins/`.

Legacy files in `~/.config/jcode/*.py` and `~/.config/jcode/plugins/` still load.

## Directory Structure

```
~/.config/jcode/
  ├── configs/
  │   ├── init.py
  │   ├── colors/
  │   │   └── my_theme.py
  │   └── plugins/
  │       └── my_plugin.py
  ├── plugins/          # Legacy plugin dir
  └── themes/           # Legacy theme dir
```

## Creating a Plugin

Create a `.py` file in `~/.config/jcode/configs/plugins/` or wire it from `init.py`.

```python
from jcode_api import vim

def my_command():
    vim.notify("Hello from Plugin!")

vim.keymap.set("normal", "ctrl+h", my_command)
```

## API Reference

### Editor Control
*   `enter_normal_mode()`
*   `enter_insert_mode()`
*   `enter_visual_mode()`
*   `move_cursor(dx, dy)`
*   `insert_char(char)`
*   `delete_char(forward_bool)`
*   `save_file()`
*   `open_file(path)`
*   `show_message(text)`

### State Inspection
*   `get_mode()` -> str ("normal", "insert", "visual")
*   `get_cursor_x()` -> int
*   `get_cursor_y()` -> int

### Theme/UI
*   `set_theme_color(name, fg, bg)`: Set color for a UI element.
    *   `name`: "keyword", "string", "comment", "background", etc.
    *   `fg`: Terminal color code (0-255).
    *   `bg`: Terminal color code (0-255). -1 for transparent/default.
*   `vim.api.nvim_set_hl(0, group, spec)`: Neovim-style highlight mapping for common groups.
*   `vim.cmd("colorscheme my_theme")` or `vim.cmd.colorscheme("my_theme")`
*   `vim.notify(text, level="info")`
*   `vim.keymap.set(mode, lhs, rhs)`

## Example: Auto-Save Plugin

```python
import jcode_api
import threading
import time

def auto_save_loop():
    while True:
        time.sleep(30)
        jcode_api.save_file()
        jcode_api.show_message("Auto-saved!")

# threading.Thread(target=auto_save_loop).start()
```
