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
def add_diagnostic(filepath, line, col, end_line, end_col, message, severity): 
    core.add_diagnostic(filepath, line, col, end_line, end_col, message, severity)
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

def register_command(name, callback):
    cb_name = ""
    if isinstance(callback, str):
        cb_name = callback
    elif callable(callback):
        if hasattr(callback, "__name__"):
            cb_name = callback.__name__
            import __main__
            setattr(__main__, cb_name, callback)
    
    if cb_name:
        core.register_command(name, cb_name)

def show_input(prompt, callback):
    cb_name = ""
    if isinstance(callback, str):
        cb_name = callback
    elif callable(callback):
        if hasattr(callback, "__name__"):
            cb_name = callback.__name__
            import __main__
            setattr(__main__, cb_name, callback)
            
    if cb_name:
        core.show_input(prompt, cb_name)

def _internal_call_callback(cb_name, arg):
    import __main__
    if hasattr(__main__, cb_name):
        func = getattr(__main__, cb_name)
        if callable(func):
            try:
                func(arg)
            except Exception as e:
                print(f"Error in callback {cb_name}: {e}")
    else:
        # Search in jcode_api provided callbacks? No, usually in main or plugins
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
    def add_diagnostic(filepath, line, col, end_line, end_col, message, severity): 
        add_diagnostic(filepath, line, col, end_line, end_col, message, severity)

def on_keybind(key_str, mode="all"):
    def decorator(func):
        register_keybind(key_str, mode, func)
        return func
    return decorator
