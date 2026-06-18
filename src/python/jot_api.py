"""Python API for jot themes and trusted local plugins."""

import os
import sys
from pathlib import Path

from runtime.theme import ThemeRuntime

try:
    import _jot_internal as core
except ImportError:
    class MockCore:
        def show_message(self, *_args):
            return None

        def set_theme_color(self, *_args):
            return None

        def register_command(self, *_args):
            return None

        def register_keymap(self, *_args):
            return None

        def register_autocmd(self, *_args):
            return None

        def register_panel(self, *_args):
            return None

        def get_current_buffer(self):
            return ""

        def set_current_buffer(self, *_args):
            return None

        def get_selection(self):
            return ""

        def replace_selection(self, *_args):
            return None

        def insert_text(self, *_args):
            return None

        def get_cursor(self):
            return "0:0"

        def set_cursor(self, *_args):
            return None

        def current_file(self):
            return ""

        def open_file(self, *_args):
            return None

        def save_current_file(self):
            return None

        def execute_command(self, *_args):
            return None

        def run_job(self, *_args):
            return None

        def show_picker(self, *_args):
            return None

        def show_panel(self, *_args):
            return None

    core = MockCore()


CONFIG_HOME = Path(os.environ.get("JOT_CONFIG_HOME", Path.home() / ".config" / "jot"))
CONFIGS_DIR = CONFIG_HOME / "configs"
COLORS_DIR = CONFIGS_DIR / "colors"
LEGACY_THEMES_DIR = CONFIG_HOME / "themes"
RUNTIME_ROOT = Path(__file__).resolve().parent.parent
SOURCE_ROOT = RUNTIME_ROOT.parent
RUNTIME_THEME_DIRS = (
    RUNTIME_ROOT / "configs" / "colors",
    SOURCE_ROOT / ".configs" / "configs" / "colors",
)
JOT_API_VERSION = "2.0.0"
JOT_VERSION = os.environ.get("JOT_VERSION", "0.1.0")

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
    "Boolean": "constant",
    "Float": "number",
    "Identifier": "variable",
    "Function": "function",
    "Statement": "keyword",
    "Conditional": "keyword",
    "Repeat": "keyword",
    "Label": "keyword",
    "Operator": "operator",
    "Keyword": "keyword",
    "Exception": "keyword",
    "PreProc": "keyword",
    "Type": "type",
    "StorageClass": "keyword",
    "Structure": "type",
    "Typedef": "type",
    "Special": "builtin",
    "SpecialChar": "string",
    "Delimiter": "punctuation",
    "@comment": "comment",
    "@string": "string",
    "@string.escape": "string_escape",
    "@character": "string",
    "@number": "number",
    "@boolean": "constant",
    "@constant": "constant",
    "@constant.builtin": "builtin",
    "@constant.macro": "constant_macro",
    "@constant.predefined": "constant_macro",
    "@variable": "variable",
    "@variable.parameter": "parameter",
    "@variable.member": "field",
    "@property": "field",
    "@field": "field",
    "@function": "function",
    "@function.builtin": "builtin",
    "@function.method": "function_method",
    "@method": "function_method",
    "@method.call": "function_method",
    "@constructor": "function_constructor",
    "@function.constructor": "function_constructor",
    "@keyword": "keyword",
    "@keyword.control": "keyword_control",
    "@keyword.conditional": "keyword_control",
    "@keyword.repeat": "keyword_control",
    "@keyword.return": "keyword_control",
    "@keyword.storage": "keyword_storage",
    "@keyword.modifier": "keyword_storage",
    "@keyword.directive": "keyword_preproc",
    "@keyword.import": "keyword_preproc",
    "@operator": "operator",
    "@type": "type",
    "@type.builtin": "type_builtin",
    "@module": "module",
    "@namespace": "namespace",
    "@tag": "tag",
    "@tag.attribute": "attribute",
    "@attribute": "attribute",
    "@punctuation": "punctuation",
    "@punctuation.bracket": "punctuation_bracket",
    "@punctuation.delimiter": "punctuation_delimiter",
    "@punctuation.special": "punctuation",
    "LineNr": "line_num",
    "Cursor": "cursor",
    "CursorLine": "selection",
    "Visual": "selection",
    "StatusLine": "status",
    "StatusLineNC": "status",
    "StatusLineMsg": "status_message",
    "StatusLineLogo": "status_logo",
    "StatusLineFile": "status_file",
    "StatusLineInfo": "status_info",
    "StatusLineWarn": "status_warning",
    "StatusLineWarning": "status_warning",
    "StatusLineError": "status_error",
    "StatusLineMuted": "status_muted",
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


def config_path(*parts):
    return str(CONFIG_HOME.joinpath(*parts))


def colors_path(*parts):
    return str(COLORS_DIR.joinpath(*parts))


def show_message(message):
    core.show_message(str(message))


_plugin_callbacks = {}
_plugin_callback_counter = 0


def _callback_name(fn):
    global _plugin_callback_counter
    _plugin_callback_counter += 1
    callback = f"{fn.__module__}.{fn.__name__}.{_plugin_callback_counter}"
    _plugin_callbacks[callback] = fn
    return callback


def _reset_plugin_state():
    global _plugin_callback_counter
    _plugin_callbacks.clear()
    _plugin_callback_counter = 0


def command(name, callback=None, detail="Plugin command"):
    def decorator(fn):
        cb = _callback_name(fn)
        core.register_command(str(name), cb, str(detail))
        return fn

    if callable(callback):
        return decorator(callback)
    if callback is not None:
        detail = callback
    return decorator


def autocmd(event, callback=None):
    def decorator(fn):
        def wrapped(payload=""):
            parts = str(payload or "").split("\n", 2)
            data = {
                "event": str(event),
                "file": parts[1] if len(parts) > 1 else "",
                "buffer": int(parts[2]) if len(parts) > 2 and parts[2].lstrip("-").isdigit() else -1,
            }
            return fn(data)

        cb = _callback_name(wrapped)
        core.register_autocmd(str(event), cb)
        return fn

    if callable(callback):
        return decorator(callback)
    return decorator


def get_current_buffer():
    return core.get_current_buffer()


def set_current_buffer(text):
    core.set_current_buffer(str(text))


def get_selection():
    return core.get_selection()


def replace_selection(text):
    core.replace_selection(str(text))


def insert_text(text):
    core.insert_text(str(text))


def get_cursor():
    raw = core.get_cursor()
    line, col = str(raw).split(":", 1)
    return int(line), int(col)


def set_cursor(line, col):
    core.set_cursor(int(line), int(col))


def current_file():
    return core.current_file()


def open_file(path):
    core.open_file(str(path))


def save_current_file():
    core.save_current_file()


def set_theme_color(name, fg, bg):
    core.set_theme_color(str(name), int(fg), int(bg))


_theme_runtime = ThemeRuntime(
    _HIGHLIGHT_MAP,
    COLORS_DIR,
    LEGACY_THEMES_DIR,
    RUNTIME_THEME_DIRS,
    set_theme_color,
    show_message,
)


def set_hl(group, spec):
    _theme_runtime.set_hl(group, spec)


def list_colorschemes():
    return _theme_runtime.list_colorschemes()


def apply_colorscheme(name):
    return _theme_runtime.apply_colorscheme(name, sys.modules[__name__])


def _run_command_line(command_line):
    command_line = str(command_line or "").strip()
    if command_line.startswith(":"):
        command_line = command_line[1:].strip()
    if command_line.startswith("colorscheme "):
        _, name = command_line.split(None, 1)
        apply_colorscheme(name.strip())
        return True
    return False


class _Api:
    @staticmethod
    def nvim_set_hl(_, group, spec):
        set_hl(group, spec)


class _Cmd:
    def __call__(self, command_line):
        return _run_command_line(command_line)

    @staticmethod
    def colorscheme(name):
        return apply_colorscheme(name)


class _Keymap:
    @staticmethod
    def set(keys, action, desc="", mode="global"):
        command_line = ""
        callback = ""
        if callable(action):
            callback = _callback_name(action)
        else:
            command_line = str(action)
        core.register_keymap(str(keys), callback, command_line, str(desc),
                             str(mode))


class _Autocmd:
    def __call__(self, event, callback=None):
        return autocmd(event, callback)

    @staticmethod
    def set(event, callback):
        def wrapped(payload=""):
            parts = str(payload or "").split("\n", 2)
            data = {
                "event": str(event),
                "file": parts[1] if len(parts) > 1 else "",
                "buffer": int(parts[2]) if len(parts) > 2 and parts[2].lstrip("-").isdigit() else -1,
            }
            return callback(data)

        cb = _callback_name(wrapped)
        core.register_autocmd(str(event), cb)


class _Buffer:
    @staticmethod
    def get_text():
        return get_current_buffer()

    @staticmethod
    def set_text(text):
        set_current_buffer(text)

    @staticmethod
    def get_selection():
        return get_selection()

    @staticmethod
    def replace_selection(text):
        replace_selection(text)

    @staticmethod
    def insert_text(text):
        insert_text(text)

    @staticmethod
    def cursor():
        return get_cursor()

    @staticmethod
    def set_cursor(line, col):
        set_cursor(line, col)

    @staticmethod
    def current_file():
        return current_file()


class _UI:
    @staticmethod
    def show_picker(title, items, on_select):
        if callable(items):
            items_callback = _callback_name(items)
        else:
            values = [str(item) for item in items]

            def _items(_arg=""):
                return values

            items_callback = _callback_name(_items)
        select_callback = _callback_name(on_select)
        core.show_picker(str(title), items_callback, select_callback)

    @staticmethod
    def register_panel(name, provider, title=""):
        callback = _callback_name(provider)
        core.register_panel(str(name), callback, str(title or name))

    @staticmethod
    def show_panel(name):
        core.show_panel(str(name))


class _Job:
    @staticmethod
    def run(command_line, cwd=None, label=None):
        core.run_job(str(command_line), "" if cwd is None else str(cwd),
                     "" if label is None else str(label))


class _Jot:
    def __init__(self):
        self.g = {}
        self.api = _Api()
        self.cmd = _Cmd()
        self.keymap = _Keymap()
        self.autocmd = _Autocmd()
        self.buffer = _Buffer()
        self.ui = _UI()
        self.job = _Job()

    command = staticmethod(command)
    on = staticmethod(autocmd)

    @staticmethod
    def notify(message, level="info"):
        show_message(f"[{level}] {message}")

    @staticmethod
    def execute(command_line):
        core.execute_command(str(command_line))

    @staticmethod
    def open_file(path):
        open_file(path)

    @staticmethod
    def save():
        save_current_file()


jot = _Jot()
vim = jot
