"""
Jot Editor Python API.

This module keeps the low-level wrapper intact and exposes a
high-level Jot facade for config and themes.
"""

import fnmatch
import json
import os
import sys
from collections import defaultdict
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
RUNTIME_ROOT = Path(__file__).resolve().parent.parent
RUNTIME_THEME_DIRS = (
    RUNTIME_ROOT / "configs" / "colors",
    RUNTIME_ROOT / ".configs" / "configs" / "colors",
)

_HIGHLIGHT_MAP = {
    "Normal": "default",
    "NormalFloat": "command",
    "Search": "search_match",
    "IncSearch": "search_match",
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
    "StatusLineMsg": "status_message",
    "FloatBorder": "panel_border",
    "WinSeparator": "panel_border",
    "WinActiveBorder": "active_border",
    "TabLine": "tab_inactive",
    "TabLineSel": "tab_active",
    "TabLineFill": "tab_separator",
    "TabClose": "tab_close",
    "Pmenu": "command",
    "PmenuSel": "selection",
    "DiagnosticError": "diagnostic_error",
    "DiagnosticWarn": "diagnostic_warning",
    "DiagnosticInfo": "diagnostic_info",
    "DiagnosticHint": "diagnostic_hint",
    "Sidebar": "sidebar",
    "SidebarDir": "sidebar_directory",
    "SidebarSel": "sidebar_selected",
    "SidebarSelNC": "sidebar_selected_inactive",
    "SidebarBorder": "sidebar_border",
    "Terminal": "terminal",
    "TerminalTab": "terminal_tab_inactive",
    "TerminalTabActive": "terminal_tab_active",
    "TerminalTabFocused": "terminal_tab_focused",
    "TerminalTabClose": "terminal_tab_close",
    "TerminalTabPlus": "terminal_tab_plus",
    "TerminalTabSeparator": "terminal_tab_separator",
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
def get_current_file(): return core.get_current_file()
def get_buffer_content(): return core.get_buffer_content()
def set_buffer_content(text): core.set_buffer_content(text)
def get_selected_text(): return core.get_selected_text()
def set_theme_color(name, fg, bg): core.set_theme_color(name, fg, bg)
def move_line_up(): core.move_line_up()
def move_line_down(): core.move_line_down()
def show_popup(text, x, y): core.show_popup(text, x, y)
def hide_popup(): core.hide_popup()
def clear_diagnostics(filepath): core.clear_diagnostics(filepath)
def set_diagnostics(filepath, diagnostics): core.set_diagnostics(filepath, diagnostics) # New
def add_diagnostic(filepath, line, col, end_line, end_col, message, severity): 
    core.add_diagnostic(filepath, line, col, end_line, end_col, message, severity)
def execute_command(command_line): core.execute_command(command_line)
def reload_plugins(): return core.reload_plugins() or 0
def list_plugins():
    plugins = core.list_plugins()
    return list(plugins) if plugins else []

def config_path(*parts):
    return str(CONFIG_HOME.joinpath(*parts))

def colors_path(*parts):
    return str(COLORS_DIR.joinpath(*parts))

def plugins_path(*parts):
    return str(PLUGINS_DIR.joinpath(*parts))


def current_file():
    return get_current_file()

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

def list_colorschemes():
    names = set()
    for directory in (COLORS_DIR, LEGACY_THEMES_DIR, *RUNTIME_THEME_DIRS):
        if not directory.exists():
            continue
        for file in directory.glob("*.py"):
            if file.name == "__init__.py":
                continue
            names.add(file.stem)
    return sorted(names)

def apply_colorscheme(name):
    candidates = [COLORS_DIR / f"{name}.py", LEGACY_THEMES_DIR / f"{name}.py"]
    for runtime_dir in RUNTIME_THEME_DIRS:
        candidates.append(runtime_dir / f"{name}.py")
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

    execute_command(command_line)
# Callback Registry
_callback_registry = {}
_event_callbacks = defaultdict(list)
_buffer_open_callbacks = []
_buffer_change_callbacks = []
_buffer_save_callbacks = []


def _safe_call(func, *args, **kwargs):
    try:
        return func(*args, **kwargs)
    except Exception as exc:
        print(f"Plugin callback error in {getattr(func, '__name__', func)}: {exc}")
        return None


def _invoke_with_optional_arg(func, arg):
    try:
        return func(arg)
    except TypeError:
        return func()


def register_callback(func):
    if not callable(func):
        return None
    unique_name = f"{func.__name__}_{id(func)}"
    _callback_registry[unique_name] = func
    return unique_name


def register_keybind(key, mode, callback):
    cb_name = callback if isinstance(callback, str) else register_callback(callback)
    if cb_name:
        core.register_keybind(key, mode, cb_name)


def register_command(name, callback):
    cb_name = callback if isinstance(callback, str) else register_callback(callback)
    if cb_name:
        core.register_command(name, cb_name)


def create_user_command(name, callback):
    register_command(name, callback)


def show_input(prompt, callback):
    cb_name = callback if isinstance(callback, str) else register_callback(callback)
    if cb_name:
        core.show_input(prompt, cb_name)


def _internal_call_callback(cb_name, arg):
    if cb_name in _callback_registry:
        return _safe_call(_invoke_with_optional_arg, _callback_registry[cb_name], arg)

    import __main__

    if hasattr(__main__, cb_name):
        func = getattr(__main__, cb_name)
        if callable(func):
            return _safe_call(_invoke_with_optional_arg, func, arg)

    print(f"Callback {cb_name} not found")
    return False


def _reset_runtime_state():
    _callback_registry.clear()
    _event_callbacks.clear()
    _buffer_open_callbacks.clear()
    _buffer_change_callbacks.clear()
    _buffer_save_callbacks.clear()
    _register_builtin_plugin_commands()


def autocmd(event, pattern="*", group=None):
    event_name = event.lower()

    def decorator(func):
        _event_callbacks[event_name].append(
            {"callback": func, "pattern": pattern or "*", "group": group}
        )
        return func

    return decorator


def on_startup(callback):
    @autocmd("startup")
    def _wrapped(_event):
        return callback()

    return callback


def _matches_event_pattern(pattern, payload):
    if not pattern or pattern == "*":
        return True
    candidate = (
        payload.get("filepath")
        or payload.get("command")
        or payload.get("name")
        or ""
    )
    return fnmatch.fnmatch(candidate, pattern)


def _emit_event(event, payload=None):
    event_name = (event or "").lower()
    if isinstance(payload, str):
        try:
            payload = json.loads(payload or "{}")
        except json.JSONDecodeError:
            payload = {}
    payload = payload or {}
    payload.setdefault("event", event_name)

    for entry in list(_event_callbacks.get(event_name, [])):
        if _matches_event_pattern(entry["pattern"], payload):
            _safe_call(entry["callback"], payload)


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
    for cb in list(_buffer_open_callbacks):
        _safe_call(cb, filepath)
    _emit_event("buffer_open", {"filepath": filepath})


def _on_buffer_change(filepath):
    for cb in list(_buffer_change_callbacks):
        _safe_call(cb, filepath)
    _emit_event("buffer_change", {"filepath": filepath})


def _on_buffer_save(filepath):
    for cb in list(_buffer_save_callbacks):
        _safe_call(cb, filepath)
    _emit_event("buffer_save", {"filepath": filepath})

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
    def get_current_file(): return get_current_file()
    @staticmethod
    def get_buffer_content(): return get_buffer_content()
    @staticmethod
    def set_buffer_content(text): set_buffer_content(text)
    @staticmethod
    def get_selected_text(): return get_selected_text()
    @staticmethod
    def set_theme_color(name, fg, bg): set_theme_color(name, fg, bg)
    @staticmethod
    def clear_diagnostics(filepath): clear_diagnostics(filepath)
    @staticmethod
    def set_diagnostics(filepath, diagnostics): set_diagnostics(filepath, diagnostics)
    @staticmethod
    def add_diagnostic(filepath, line, col, end_line, end_col, message, severity): 
        add_diagnostic(filepath, line, col, end_line, end_col, message, severity)
    @staticmethod
    def execute_command(command_line): execute_command(command_line)

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

    @staticmethod
    def nvim_create_autocmd(event, opts):
        pattern = opts.get("pattern", "*")
        callback = opts.get("callback")
        group = opts.get("group")
        if callback:
            return autocmd(event, pattern=pattern, group=group)(callback)
        return None

    @staticmethod
    def nvim_create_user_command(name, callback, _opts=None):
        create_user_command(name, callback)

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

# Preferred Jot namespace for plugins/themes.
jot = _Vim()

# Backward-compatibility alias for existing configs/plugins.
vim = jot


def _plugin_reload_command(_arg=""):
    count = reload_plugins()
    show_message(f"Reloaded {count} plugin file(s)")


def _plugin_list_command(_arg=""):
    plugins = list_plugins()
    if not plugins:
        show_message("No user plugins loaded")
        return
    show_message("Plugins: " + ", ".join(Path(p).name for p in plugins[:6]))

def _register_builtin_plugin_commands():
    register_command("PlugReload", _plugin_reload_command)
    register_command("PlugList", _plugin_list_command)


_register_builtin_plugin_commands()
