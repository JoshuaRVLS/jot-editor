"""
Jot Editor Python API.

This module keeps the low-level wrapper intact and exposes a
high-level Jot facade for config and themes.
"""

import os
import sys
import json
import importlib
from pathlib import Path
from commands import register_builtin_commands
from host.core import bind_core_exports
from runtime.events import EventsRuntime
from runtime.palette import PaletteRuntime
from runtime.plugins import PluginRuntime
from runtime.theme import ThemeRuntime

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
JOT_API_VERSION = "1.1.0"
JOT_VERSION = os.environ.get("JOT_VERSION", "0.1.0")
PLUGIN_POLICY_STATE_FILE = CONFIGS_DIR / "plugin_policy.json"

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

bind_core_exports(globals(), core)


def _normalize_policy_mode(mode):
    text = str(mode or "").strip().lower()
    if text in {"off", "warn", "strict"}:
        return text
    return ""


def _load_policy_mode_from_state():
    env_mode = _normalize_policy_mode(os.environ.get("JOT_PLUGIN_POLICY", ""))
    if env_mode:
        return env_mode

    try:
        if not PLUGIN_POLICY_STATE_FILE.exists():
            return "off"
        data = json.loads(PLUGIN_POLICY_STATE_FILE.read_text(encoding="utf-8"))
        if isinstance(data, dict):
            return _normalize_policy_mode(data.get("mode", "")) or "off"
    except Exception:
        return "off"
    return "off"


def _save_policy_mode_to_state(mode):
    mode = _normalize_policy_mode(mode)
    if not mode:
        return False
    try:
        PLUGIN_POLICY_STATE_FILE.parent.mkdir(parents=True, exist_ok=True)
        PLUGIN_POLICY_STATE_FILE.write_text(
            json.dumps({"mode": mode}, indent=2) + "\n",
            encoding="utf-8",
        )
        return True
    except Exception:
        return False


os.environ["JOT_PLUGIN_POLICY"] = _load_policy_mode_from_state()

def config_path(*parts):
    return str(CONFIG_HOME.joinpath(*parts))

def colors_path(*parts):
    return str(COLORS_DIR.joinpath(*parts))

def plugins_path(*parts):
    return str(PLUGINS_DIR.joinpath(*parts))


def current_file():
    return get_current_file()

def set_hl(group, spec):
    if _theme_runtime is None:
        return
    _theme_runtime.set_hl(group, spec)

def list_colorschemes():
    if _theme_runtime is None:
        return []
    return _theme_runtime.list_colorschemes()

def apply_colorscheme(name):
    if _theme_runtime is None:
        return False
    return _theme_runtime.apply_colorscheme(name, sys.modules[__name__])

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
_palette_runtime = None
_events_runtime = None
_theme_runtime = None
_plugin_runtime = None


def _safe_call(func, *args, **kwargs):
    try:
        return func(*args, **kwargs)
    except Exception as exc:
        print(f"Plugin callback error in {getattr(func, '__name__', func)}: {exc}")
        return None


def _require_capability(capability):
    if _plugin_runtime is None:
        return True
    allowed, note = _plugin_runtime.check_capability(capability)
    if note:
        show_message(note)
    return bool(allowed)


def _invoke_with_optional_arg(func, arg):
    try:
        return func(arg)
    except TypeError as exc:
        msg = str(exc)
        signature_mismatch = (
            "positional argument" in msg
            or "required positional" in msg
            or "takes 0 positional arguments" in msg
            or "but 1 was given" in msg
        )
        if signature_mismatch:
            return func()
        raise


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
        register_palette_command(name, callback)


def create_user_command(name, callback):
    register_command(name, callback)


def register_palette_command(name, callback, completer=None, description=""):
    if _palette_runtime is None:
        return
    _palette_runtime.register(name, callback, completer, description)


def register_command_completer(name, completer):
    if _palette_runtime is None:
        return
    _palette_runtime.register_completer(name, completer)


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


_palette_runtime = PaletteRuntime(_internal_call_callback)
_events_runtime = EventsRuntime(_safe_call)
_theme_runtime = ThemeRuntime(
    _HIGHLIGHT_MAP,
    COLORS_DIR,
    LEGACY_THEMES_DIR,
    RUNTIME_THEME_DIRS,
    set_theme_color,
    show_message,
)


def _register_lazy_event(event_name, callback):
    if _events_runtime is None:
        return

    @_events_runtime.autocmd(event_name)
    def _lazy_event(payload):
        return callback(payload)


def _register_lazy_command(name, callback):
    if _palette_runtime is None:
        return
    _palette_runtime.register(name, callback, description="lazy plugin loader")


def _execute_palette_line(line):
    if _palette_runtime is None:
        return False
    return _palette_runtime.execute(
        line, lambda callback, arg: _safe_call(_invoke_with_optional_arg, callback, arg)
    )


def _host_runtime_info():
    return {
        "api_version": JOT_API_VERSION,
        "jot_version": JOT_VERSION,
        "python_version": f"{sys.version_info.major}.{sys.version_info.minor}.{sys.version_info.micro}",
    }


_plugin_runtime = PluginRuntime(
    _safe_call,
    register_event_trigger=_register_lazy_event,
    register_command_trigger=_register_lazy_command,
    execute_palette_line=_execute_palette_line,
    show_message=show_message,
    host_info=_host_runtime_info,
)


_raw_save_file = save_file
_raw_execute_command = execute_command
_raw_execute_ex_command = execute_ex_command
_raw_open_workspace = open_workspace
_raw_toggle_terminal = toggle_terminal
_raw_toggle_sidebar = toggle_sidebar
_raw_request_quit = request_quit
_raw_save_and_quit = save_and_quit


def save_file():
    if not _require_capability("write"):
        return
    return _raw_save_file()


def execute_command(command_line):
    if not _require_capability("commands"):
        return
    return _raw_execute_command(command_line)


def execute_ex_command(command_line):
    if not _require_capability("ex_commands"):
        return
    return _raw_execute_ex_command(command_line)


def open_workspace(path):
    if not _require_capability("workspace"):
        return
    return _raw_open_workspace(path)


def toggle_terminal():
    if not _require_capability("terminal"):
        return
    return _raw_toggle_terminal()


def toggle_sidebar():
    if not _require_capability("sidebar"):
        return
    return _raw_toggle_sidebar()


def request_quit(force=False):
    if not _require_capability("quit"):
        return
    return _raw_request_quit(force)


def save_and_quit():
    if not _require_capability("quit"):
        return
    return _raw_save_and_quit()


def _reset_runtime_state():
    _callback_registry.clear()
    if _events_runtime is not None:
        _events_runtime.clear()
    if _palette_runtime is not None:
        _palette_runtime.clear()
    if _plugin_runtime is not None:
        _plugin_runtime.clear()
    _register_builtin_plugin_commands()
    _autoload_optional_runtime_plugins()


def register_plugin_lifecycle(on_load=None, on_unload=None, on_reload=None, plugin_path=None):
    if _plugin_runtime is None:
        return
    _plugin_runtime.register_lifecycle(
        plugin_path=plugin_path,
        on_load=on_load,
        on_unload=on_unload,
        on_reload=on_reload,
    )


def register_plugin(
    name="",
    version="",
    author="",
    description="",
    depends=None,
    min_api="",
    max_api="",
    min_jot="",
    max_jot="",
    min_python="",
    max_python="",
    requires_unsafe=False,
    capabilities=None,
    setup=None,
    lazy_events=None,
    lazy_commands=None,
    eager=False,
    plugin_path=None,
):
    if _plugin_runtime is None:
        return
    _plugin_runtime.register_metadata(
        plugin_path=plugin_path,
        name=name,
        version=version,
        author=author,
        description=description,
        depends=depends,
        min_api=min_api,
        max_api=max_api,
        min_jot=min_jot,
        max_jot=max_jot,
        min_python=min_python,
        max_python=max_python,
        requires_unsafe=requires_unsafe,
        capabilities=capabilities,
        setup=setup,
        lazy_events=lazy_events,
        lazy_commands=lazy_commands,
        eager=eager,
    )


def plugin_health():
    if _plugin_runtime is None:
        return []
    return _plugin_runtime.health()


def plugin_health_summary():
    if _plugin_runtime is None:
        return "Plugins: runtime unavailable"
    return _plugin_runtime.health_summary()


def plugin_info(query=""):
    if _plugin_runtime is None:
        return []
    return _plugin_runtime.info(query)


def plugin_policy(mode=None):
    if _plugin_runtime is None:
        return "PluginPolicy: unavailable"
    if mode is None:
        return _plugin_runtime.policy_summary()
    if _plugin_runtime.set_policy_mode(mode):
        _save_policy_mode_to_state(mode)
        return _plugin_runtime.policy_summary()
    return f"PluginPolicy: invalid mode '{mode}' (use off|warn|strict)"


def plugin_audit(limit=20, query=""):
    if _plugin_runtime is None:
        return []
    return _plugin_runtime.audit(limit=limit, query=query)


def plugin_audit_clear():
    if _plugin_runtime is None:
        return
    _plugin_runtime.clear_audit()


def _plugin_runtime_on_loaded(path, ok=True, error="", load_ms=0.0):
    if _plugin_runtime is None:
        return
    _plugin_runtime.on_loaded(path, ok=ok, error=error, load_ms=load_ms)


def _plugin_runtime_before_reload():
    if _plugin_runtime is None:
        return
    _plugin_runtime.before_reload()


def _plugin_runtime_finalize_activation():
    if _plugin_runtime is None:
        return {"activated": 0, "lazy": 0, "failed": 0}
    return _plugin_runtime.finalize_activation()


def autocmd(event, pattern="*", group=None):
    if _events_runtime is None:
        def passthrough(func):
            return func
        return passthrough
    return _events_runtime.autocmd(event, pattern=pattern, group=group)


def on_startup(callback):
    if _events_runtime is None:
        return callback
    return _events_runtime.on_startup(callback)


def _emit_event(event, payload=None):
    if _events_runtime is None:
        return
    _events_runtime.emit(event, payload)


def _command_palette_suggestions(seed):
    if _palette_runtime is None:
        return []
    return _palette_runtime.suggestions(seed)


def _command_palette_execute(line):
    if _palette_runtime is None:
        return False
    return _palette_runtime.execute(
        line, lambda callback, arg: _safe_call(_invoke_with_optional_arg, callback, arg)
    )


def on_buffer_open(callback):
    if _events_runtime is None:
        return callback
    return _events_runtime.on_buffer_open(callback)


def on_buffer_change(callback):
    if _events_runtime is None:
        return callback
    return _events_runtime.on_buffer_change(callback)


def on_buffer_save(callback):
    if _events_runtime is None:
        return callback
    return _events_runtime.on_buffer_save(callback)


# Internal hooks called by C++
def _on_buffer_open(filepath):
    if _events_runtime is None:
        return
    _events_runtime.dispatch_buffer_open(filepath)


def _on_buffer_change(filepath):
    if _events_runtime is None:
        return
    _events_runtime.dispatch_buffer_change(filepath)


def _on_buffer_save(filepath):
    if _events_runtime is None:
        return
    _events_runtime.dispatch_buffer_save(filepath)

# Editor Class wrapper for backward compatibility
class Editor:
    @staticmethod
    def enter_normal_mode(): _core_api.enter_normal_mode()
    @staticmethod
    def enter_insert_mode(): _core_api.enter_insert_mode()
    @staticmethod
    def enter_visual_mode(): _core_api.enter_visual_mode()
    @staticmethod
    def move_cursor(dx, dy): _core_api.move_cursor(dx, dy)
    @staticmethod
    def insert_char(c): _core_api.insert_char(c)
    @staticmethod
    def delete_char(forward=True): _core_api.delete_char(forward)
    @staticmethod
    def save_file(): _core_api.save_file()
    @staticmethod
    def open_file(path): _core_api.open_file(path)
    @staticmethod
    def show_message(msg): _core_api.show_message(msg)
    @staticmethod
    def get_mode(): return _core_api.get_mode()
    @staticmethod
    def get_cursor_x(): return _core_api.get_cursor_x()
    @staticmethod
    def get_cursor_y(): return _core_api.get_cursor_y()
    @staticmethod
    def get_current_file(): return _core_api.get_current_file()
    @staticmethod
    def get_buffer_content(): return _core_api.get_buffer_content()
    @staticmethod
    def set_buffer_content(text): _core_api.set_buffer_content(text)
    @staticmethod
    def get_selected_text(): return _core_api.get_selected_text()
    @staticmethod
    def set_theme_color(name, fg, bg): _core_api.set_theme_color(name, fg, bg)
    @staticmethod
    def clear_diagnostics(filepath): _core_api.clear_diagnostics(filepath)
    @staticmethod
    def set_diagnostics(filepath, diagnostics): _core_api.set_diagnostics(filepath, diagnostics)
    @staticmethod
    def add_diagnostic(filepath, line, col, end_line, end_col, message, severity):
        _core_api.add_diagnostic(filepath, line, col, end_line, end_col, message, severity)
    @staticmethod
    def execute_command(command_line): _core_api.execute_command(command_line)

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


def _register_builtin_plugin_commands():
    register_builtin_commands(
        {
            "register_command": register_command,
            "register_keybind": register_keybind,
            "register_palette_command": register_palette_command,
            "register_command_completer": register_command_completer,
            "reload_plugins": reload_plugins,
            "list_plugins": list_plugins,
            "show_message": show_message,
            "list_colorschemes": list_colorschemes,
            "open_file": open_file,
            "save_file": save_file,
            "split_pane": split_pane,
            "focus_next_pane": focus_next_pane,
            "focus_prev_pane": focus_prev_pane,
            "toggle_terminal": toggle_terminal,
            "toggle_sidebar": toggle_sidebar,
            "request_quit": request_quit,
            "save_and_quit": save_and_quit,
            "toggle_minimap": toggle_minimap,
            "apply_colorscheme": apply_colorscheme,
            "execute_ex_command": execute_ex_command,
            "get_buffer_content": get_buffer_content,
            "set_buffer_content": set_buffer_content,
            "get_current_file": get_current_file,
            "plugin_health_summary": plugin_health_summary,
            "plugin_info": plugin_info,
            "plugin_policy": plugin_policy,
            "plugin_audit": plugin_audit,
            "plugin_audit_clear": plugin_audit_clear,
        }
    )


_register_builtin_plugin_commands()
def _autoload_optional_runtime_plugins():
    # Built-in optional runtime plugins should load without requiring user config.
    # Failures stay non-fatal so editor startup is resilient.
    for mod_name in ():
        try:
            if mod_name in sys.modules:
                importlib.reload(sys.modules[mod_name])
            else:
                importlib.import_module(mod_name)
        except Exception as exc:
            print(f"Optional runtime plugin load failed ({mod_name}): {exc}")


_autoload_optional_runtime_plugins()
