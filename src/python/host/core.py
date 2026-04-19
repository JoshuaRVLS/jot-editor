"""Thin wrappers around the embedded _jot_internal core module."""

import json


CORE_EXPORTS = (
    "enter_normal_mode",
    "enter_insert_mode",
    "enter_visual_mode",
    "move_cursor",
    "insert_char",
    "delete_char",
    "save_file",
    "open_file",
    "show_message",
    "get_mode",
    "get_cursor_x",
    "get_cursor_y",
    "get_line",
    "get_line_count",
    "get_current_file",
    "get_buffer_content",
    "set_buffer_content",
    "get_selected_text",
    "set_theme_color",
    "move_line_up",
    "move_line_down",
    "show_popup",
    "hide_popup",
    "clear_diagnostics",
    "set_diagnostics",
    "add_diagnostic",
    "execute_command",
    "execute_ex_command",
    "reload_plugins",
    "list_plugins",
    "list_buffers",
    "list_panes",
    "get_layout",
    "switch_buffer",
    "close_buffer",
    "new_buffer",
    "split_pane",
    "resize_pane",
    "focus_next_pane",
    "focus_prev_pane",
    "toggle_sidebar",
    "open_workspace",
    "toggle_terminal",
    "redraw",
    "request_quit",
    "save_and_quit",
    "toggle_minimap",
    "emit_event",
)


class JotCoreAPI:
    def __init__(self, core):
        self._core = core

    @staticmethod
    def _parse_json_blob(blob, fallback):
        if not blob:
            return fallback
        try:
            return json.loads(blob)
        except Exception:
            return fallback

    def enter_normal_mode(self):
        self._core.enter_normal_mode()

    def enter_insert_mode(self):
        self._core.enter_insert_mode()

    def enter_visual_mode(self):
        self._core.enter_visual_mode()

    def move_cursor(self, dx, dy):
        self._core.move_cursor(dx, dy)

    def insert_char(self, c):
        self._core.insert_char(c)

    def delete_char(self, forward=True):
        self._core.delete_char(forward)

    def save_file(self):
        self._core.save_file()

    def open_file(self, path):
        self._core.open_file(path)

    def show_message(self, msg):
        self._core.show_message(msg)

    def get_mode(self):
        return self._core.get_mode()

    def get_cursor_x(self):
        return self._core.get_cursor_x()

    def get_cursor_y(self):
        return self._core.get_cursor_y()

    def get_line(self, line):
        return self._core.get_line(line)

    def get_line_count(self):
        return self._core.get_line_count()

    def get_current_file(self):
        return self._core.get_current_file()

    def get_buffer_content(self):
        return self._core.get_buffer_content()

    def set_buffer_content(self, text):
        self._core.set_buffer_content(text)

    def get_selected_text(self):
        return self._core.get_selected_text()

    def set_theme_color(self, name, fg, bg):
        self._core.set_theme_color(name, fg, bg)

    def move_line_up(self):
        self._core.move_line_up()

    def move_line_down(self):
        self._core.move_line_down()

    def show_popup(self, text, x, y):
        self._core.show_popup(text, x, y)

    def hide_popup(self):
        self._core.hide_popup()

    def clear_diagnostics(self, filepath):
        self._core.clear_diagnostics(filepath)

    def set_diagnostics(self, filepath, diagnostics):
        self._core.set_diagnostics(filepath, diagnostics)

    def add_diagnostic(self, filepath, line, col, end_line, end_col, message, severity):
        self._core.add_diagnostic(filepath, line, col, end_line, end_col, message, severity)

    def execute_command(self, command_line):
        self._core.execute_command(command_line)

    def execute_ex_command(self, command_line):
        self._core.execute_ex_command(command_line)

    def reload_plugins(self):
        return self._core.reload_plugins() or 0

    def list_plugins(self):
        plugins = self._core.list_plugins()
        return list(plugins) if plugins else []

    def list_buffers(self):
        return self._parse_json_blob(self._core.list_buffers(), [])

    def list_panes(self):
        return self._parse_json_blob(self._core.list_panes(), [])

    def get_layout(self):
        return self._parse_json_blob(self._core.get_layout(), {})

    def switch_buffer(self, index):
        return bool(self._core.switch_buffer(int(index)))

    def close_buffer(self, index):
        return bool(self._core.close_buffer(int(index)))

    def new_buffer(self):
        self._core.new_buffer()

    def split_pane(self, direction="vertical"):
        self._core.split_pane(direction)

    def resize_pane(self, delta):
        return bool(self._core.resize_pane(int(delta)))

    def focus_next_pane(self):
        self._core.focus_next_pane()

    def focus_prev_pane(self):
        self._core.focus_prev_pane()

    def toggle_sidebar(self):
        self._core.toggle_sidebar()

    def open_workspace(self, path):
        self._core.open_workspace(path)

    def toggle_terminal(self):
        self._core.toggle_terminal()

    def redraw(self):
        self._core.request_redraw()

    def request_quit(self, force=False):
        self._core.request_quit(bool(force))

    def save_and_quit(self):
        self._core.save_and_quit()

    def toggle_minimap(self):
        self._core.toggle_minimap()

    def emit_event(self, event_name, payload=None):
        payload_json = json.dumps(payload or {})
        self._core.emit_event(event_name, payload_json)


def bind_core_exports(namespace, core):
    api = JotCoreAPI(core)
    namespace["_core_api"] = api
    for name in CORE_EXPORTS:
        namespace[name] = getattr(api, name)
