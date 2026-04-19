"""Built-in command registration entrypoint."""

from .editor import register_editor_commands
from .plugin_admin import register_plugin_admin_commands
from .search_replace import register_search_replace_commands


def register_builtin_commands(api):
    register_plugin_admin_commands(api)
    register_search_replace_commands(api)
    register_editor_commands(api)
