"""
JCode Editor Python API
This module provides Python bindings for the JCode editor.
"""

import sys

# Try to import internal module provided by C++ host
try:
    import _jcode_internal as core
except ImportError:
    # Dummy mock for testing or fallback
    class MockCore:
        def __getattr__(self, name):
            return lambda *args: None
    core = MockCore()

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
def add_diagnostic(filepath, line, col, message, severity): 
    core.add_diagnostic(filepath, line, col, message, severity)
def register_keybind(key, mode, callback):
    # Check if callback is a function name or callable
    cb_name = ""
    if isinstance(callback, str):
        cb_name = callback
    elif callable(callback):
        # We need to expose it to __main__ so C++ can find it if simpler lookup used,
        # BUT our new _jcode_internal.register_keybind probably expects a Name?
        # Actually in src/python_api.cpp we implemented py_register_keybind to call C++ register_keybind.
        # And C++ register_keybind stores method name.
        # So we really need a name.
        if hasattr(callback, "__name__"):
            cb_name = callback.__name__
            # Helper: inject into __main__ if needed?
            # Or reliance on user putting it there.
            import __main__
            setattr(__main__, cb_name, callback)
    
    if cb_name:
        core.register_keybind(key, mode, cb_name)

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

def on_keybind(key_str, mode="all"):
    def decorator(func):
        register_keybind(key_str, mode, func)
        return func
    return decorator
