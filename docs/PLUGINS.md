# jcode Plugin Development Guide

jcode supports plugins written in Python. Plugins are loaded from `~/.config/jcode/*.py`.

## Directory Structure

```
~/.config/jcode/
  ├── config.py         # Main configuration (auto-loaded)
  ├── my_plugin.py      # Custom plugin
  ├── jcode_api.py      # API Wrapper (required for type hinting/docs)
  └── themes/
      └── my_theme.py   # Custom themes
```

## Creating a Plugin

Create a `.py` file in `~/.config/jcode/`.

```python
import jcode_api

def my_command():
    jcode_api.show_message("Hello from Plugin!")

# Register a command
# Usage in editor: :my_command (if command palette supports it)
# Or binding:
jcode_api.register_keybind("ctrl+h", "normal", "my_command_func")

# Map function names to global scope if needed for C++ to call them by name?
# Currently C++ calls registered commands internal map?
# Actually, register_keybind maps "key" to "command_name". 
# But how does C++ know "command_name" -> python function?
# The current implementations of `register_keybind` in python_api.cpp might need inspection.
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
