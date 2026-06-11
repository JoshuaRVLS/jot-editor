"""Theme-only Python API for jot.

The embedded interpreter is reserved for colorscheme discovery and application.
It does not load user extension code or register editor commands/keybindings.
"""

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


def command(command_line):
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
        return command(command_line)

    @staticmethod
    def colorscheme(name):
        return apply_colorscheme(name)


class _Jot:
    def __init__(self):
        self.g = {}
        self.api = _Api()
        self.cmd = _Cmd()

    @staticmethod
    def notify(message, level="info"):
        show_message(f"[{level}] {message}")


jot = _Jot()
vim = jot
