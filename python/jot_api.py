"""
Jot Editor Python API.

This module keeps the low-level wrapper intact and also exposes a
Neovim-inspired facade for config and themes.
"""

import os
import sys
from pathlib import Path

# Try to import internal module provided by C++ host
try:
    import _jot_internal as core
except ImportError:
    # Dummy mock for testing or fallback
    class MockCore:
        def __getattr__(self, name):
            return lambda *args: None
    core = MockCore()

CONFIG_HOME = Path(os.environ.get("JOT_CONFIG_HOME", Path.home() / ".config" / "jot"))
CONFIGS_DIR = CONFIG_HOME / "configs"
COLORS_DIR = CONFIGS_DIR / "colors"
PLUGINS_DIR = CONFIGS_DIR / "plugins"
LEGACY_THEMES_DIR = CONFIG_HOME / "themes"

_HIGHLIGHT_MAP = {
    "Normal": "default",
    "NormalFloat": "command",
    "Comment": "comment",
    "Constant": "number",
    "String": "string",
    "Character": "string",
    "Number": "number",
    "Boolean": "number",
    "Float": "number",
    "Identifier": "default",
    "Function": "function",
    "Statement": "keyword",
    "Conditional": "keyword",
    "Repeat": "keyword",
    "Label": "keyword",
    "Operator": "keyword",
    "Keyword": "keyword",
    "Exception": "keyword",
    "PreProc": "keyword",
    "Type": "type",
    "Special": "bracket_match",
    "LineNr": "line_num",
    "Cursor": "cursor",
    "CursorLine": "selection",
    "Visual": "selection",
    "StatusLine": "status",
    "StatusLineNC": "status",
    "FloatBorder": "panel_border",
    "WinSeparator": "panel_border",
    "Pmenu": "command",
    "PmenuSel": "selection",
    "TelescopeNormal": "telescope",
    "TelescopeSelection": "telescope_selected",
    "TelescopePreviewNormal": "telescope_preview",
}

# Expose core functions directly
def enter_normal_mode(): core.enter_normal_mode()
def enter_insert_mode(): core.enter_insert_mode()
def enter_visual_mode(): core.enter_visual_mode()
def move_cursor(dx, dy): core.move_cursor(dx, dy)
def insert_char(c): core.insert_char(c)
def delete_char(forward=True): core.delete_char(forward)
def save_file(): core.save_file()
def open_file(path): core.open_file(path)
def show_message(msg): core.show_message(msg)
def get_mode(): return core.get_mode()
def get_cursor_x(): return core.get_cursor_x()
def get_cursor_y(): return core.get_cursor_y()
def get_line(line): return core.get_line(line)
def get_line_count(): return core.get_line_count()
def set_theme_color(name, fg, bg): core.set_theme_color(name, fg, bg)
def move_line_up(): core.move_line_up()
def move_line_down(): core.move_line_down()
def show_popup(text, x, y): core.show_popup(text, x, y)
def hide_popup(): core.hide_popup()
def clear_diagnostics(filepath): core.clear_diagnostics(filepath)
def set_diagnostics(filepath, diagnostics): core.set_diagnostics(filepath, diagnostics) # New
def add_diagnostic(filepath, line, col, end_line, end_col, message, severity): 
    core.add_diagnostic(filepath, line, col, end_line, end_col, message, severity)

def config_path(*parts):
    return str(CONFIG_HOME.joinpath(*parts))

def colors_path(*parts):
    return str(COLORS_DIR.joinpath(*parts))

def plugins_path(*parts):
    return str(PLUGINS_DIR.joinpath(*parts))

def _normalize_theme_value(value):
    if isinstance(value, str):
        lookup = {
            "none": -1,
            "default": -1,
            "fg": -1,
            "bg": -1,
        }
        return lookup.get(value.lower(), -1)
    if value is None:
        return -1
    return int(value)

def set_hl(group, spec):
    slot = _HIGHLIGHT_MAP.get(group, group)
    fg = _normalize_theme_value(spec.get("fg"))
    bg = _normalize_theme_value(spec.get("bg"))
    set_theme_color(slot, fg, bg)

def apply_colorscheme(name):
    candidates = [
        COLORS_DIR / f"{name}.py",
        LEGACY_THEMES_DIR / f"{name}.py",
    ]
    for candidate in candidates:
        if candidate.exists():
            namespace = {"__file__": str(candidate), "__name__": "__main__"}
            with open(candidate, "r", encoding="utf-8") as handle:
                code = compile(handle.read(), str(candidate), "exec")
                exec(code, namespace, namespace)

            if callable(namespace.get("setup")):
                namespace["setup"](sys.modules[__name__])
            elif callable(namespace.get("apply")):
                namespace["apply"]()
            elif isinstance(namespace.get("theme"), dict):
                for group, hl in namespace["theme"].items():
                    set_hl(group, hl)

            return True
    show_message(f"Colorscheme '{name}' not found")
    return False

def command(command_line):
    command_line = command_line.strip()
    if not command_line:
        return

    if command_line.startswith(":"):
        command_line = command_line[1:].strip()

    if command_line in {"w", "write"}:
        save_file()
        return
    if command_line.startswith("colorscheme "):
        _, name = command_line.split(None, 1)
        apply_colorscheme(name.strip())
        return

    show_message(f"Unsupported command: {command_line}")
# Callback Registry
_callback_registry = {}

def register_callback(func):
    if not callable(func): return None
    name = func.__name__
    # Handle duplicates by appending ID if needed? For now simple overwrite or unique name
    # Let's use the function name, but maybe we should use a unique ID mapping
    # Actually, the C++ side expects a string name.
    # If we use a unique ID, we pass that to C++.
    
    # Simple approach: store in dict, ensure unique name?
    # Or just trust names are unique enough or use id(func)
    unique_name = f"{name}_{id(func)}"
    _callback_registry[unique_name] = func
    return unique_name

def register_keybind(key, mode, callback):
    cb_name = ""
    if isinstance(callback, str):
        cb_name = callback
    elif callable(callback):
        cb_name = register_callback(callback)
    
    if cb_name:
        core.register_keybind(key, mode, cb_name)

def register_command(name, callback):
    cb_name = ""
    if isinstance(callback, str):
        cb_name = callback
    elif callable(callback):
        cb_name = register_callback(callback)
    
    if cb_name:
        core.register_command(name, cb_name)

def show_input(prompt, callback):
    cb_name = ""
    if isinstance(callback, str):
        cb_name = callback
    elif callable(callback):
        cb_name = register_callback(callback)
            
    if cb_name:
        core.show_input(prompt, cb_name)

def _internal_call_callback(cb_name, arg):
    if cb_name in _callback_registry:
        try:
            _callback_registry[cb_name](arg)
        except Exception as e:
            print(f"Error in callback {cb_name}: {e}")
        return

    # Fallback to __main__ for legacy or string-based callbacks
    import __main__
    if hasattr(__main__, cb_name):
        func = getattr(__main__, cb_name)
        if callable(func):
            try:
                func(arg)
            except Exception as e:
                print(f"Error in callback {cb_name}: {e}")
    else:
        print(f"Callback {cb_name} not found")

# Event Callbacks
_buffer_open_callbacks = []
_buffer_change_callbacks = []
_buffer_save_callbacks = []

def on_buffer_open(callback):
    _buffer_open_callbacks.append(callback)
    return callback

def on_buffer_change(callback):
    _buffer_change_callbacks.append(callback)
    return callback

def on_buffer_save(callback):
    _buffer_save_callbacks.append(callback)
    return callback

# Internal hooks called by C++
def _on_buffer_open(filepath):
    for cb in _buffer_open_callbacks:
        try: cb(filepath)
        except: pass

def _on_buffer_change(filepath):
    # Retrieve content only if needed by callback? 
    # For now, just pass filepath. Callbacks can call get_buffer_content.
    for cb in _buffer_change_callbacks:
        try: cb(filepath)
        except: pass

def _on_buffer_save(filepath):
    for cb in _buffer_save_callbacks:
        try: cb(filepath)
        except: pass

def get_buffer_content():
    lines = []
    count = get_line_count()
    for i in range(count):
        lines.append(get_line(i))
    return "\n".join(lines)

# Editor Class wrapper for backward compatibility
class Editor:
    @staticmethod
    def enter_normal_mode(): enter_normal_mode()
    @staticmethod
    def enter_insert_mode(): enter_insert_mode()
    @staticmethod
    def enter_visual_mode(): enter_visual_mode()
    @staticmethod
    def move_cursor(dx, dy): move_cursor(dx, dy)
    @staticmethod
    def insert_char(c): insert_char(c)
    @staticmethod
    def delete_char(forward=True): delete_char(forward)
    @staticmethod
    def save_file(): save_file()
    @staticmethod
    def open_file(path): open_file(path)
    @staticmethod
    def show_message(msg): show_message(msg)
    @staticmethod
    def get_mode(): return get_mode()
    @staticmethod
    def get_cursor_x(): return get_cursor_x()
    @staticmethod
    def get_cursor_y(): return get_cursor_y()
    @staticmethod
    def set_theme_color(name, fg, bg): set_theme_color(name, fg, bg)
    @staticmethod
    def clear_diagnostics(filepath): clear_diagnostics(filepath)
    @staticmethod
    def set_diagnostics(filepath, diagnostics): set_diagnostics(filepath, diagnostics)
    @staticmethod
    def add_diagnostic(filepath, line, col, end_line, end_col, message, severity): 
        add_diagnostic(filepath, line, col, end_line, end_col, message, severity)

def on_keybind(key_str, mode="all"):
    def decorator(func):
        register_keybind(key_str, mode, func)
        return func
    return decorator

class _Keymap:
    @staticmethod
    def set(mode, lhs, rhs):
        register_keybind(lhs, mode, rhs)

class _Api:
    @staticmethod
    def nvim_set_hl(_, group, spec):
        set_hl(group, spec)

class _Cmd:
    def __call__(self, command_line):
        command(command_line)

    @staticmethod
    def colorscheme(name):
        apply_colorscheme(name)

class _Vim:
    def __init__(self):
        self.g = {}
        self.keymap = _Keymap()
        self.api = _Api()
        self.cmd = _Cmd()

    @staticmethod
    def notify(message, level="info"):
        show_message(f"[{level}] {message}")

vim = _Vim()
