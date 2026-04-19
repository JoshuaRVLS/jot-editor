# jot Plugin Development Guide

jot supports plugins written in Python and now has a more complete extension
runtime: plugin packages, user commands with arguments, reload support, and a
generic event system.

`jot_api` exposes a first-class namespace named `jot`.
`vim` still exists as a backward-compatibility alias for older configs.

Primary config is loaded from `~/.config/jot/configs/init.py`.
Plugin files are loaded from `~/.config/jot/configs/plugins/`.
Plugin directories are also supported. A plugin directory can contain either
`plugin.py` or `__init__.py`.

Legacy files in `~/.config/jot/*.py` and `~/.config/jot/plugins/` still load.

## Directory Structure

```
~/.config/jot/
  ├── configs/
  │   ├── init.py
  │   ├── colors/
  │   │   └── my_theme.py
  │   └── plugins/
  │       └── my_plugin.py
  │       └── git_tools/
  │           └── plugin.py
  ├── plugins/          # Legacy plugin dir
  └── themes/           # Legacy theme dir
```

## Creating a Plugin

Create a `.py` file in `~/.config/jot/configs/plugins/` or wire it from `init.py`.

Single-file plugin:

```python
from jot_api import jot

def my_command(_arg=""):
    jot.notify("Hello from Plugin!")

jot.keymap.set("all", "ctrl+h", my_command)
jot.api.nvim_create_user_command("HelloPlugin", my_command, {})
```

Package-style plugin:

```python
# ~/.config/jot/configs/plugins/git_tools/plugin.py
from jot_api import autocmd, create_user_command, current_file, jot

@autocmd("startup")
def on_start(_event):
    jot.notify("git_tools ready")

def blame_current_file(_arg=""):
    jot.notify(f"Blame: {current_file()}")

create_user_command("GitBlameFile", blame_current_file)
```

## API Reference

### Editor Control
Legacy compatibility methods (generally not needed in normal modeless usage):
*   `enter_normal_mode()`
*   `enter_insert_mode()`
*   `enter_visual_mode()`
*   `move_cursor(dx, dy)`
*   `insert_char(char)`
*   `delete_char(forward_bool)`
*   `save_file()`
*   `open_file(path)`
*   `show_message(text)`
*   `execute_command(":w")` or `command(":w")`

### State Inspection
*   `get_mode()` -> str (compatibility value)
*   `get_cursor_x()` -> int
*   `get_cursor_y()` -> int
*   `get_current_file()` -> str
*   `current_file()` -> str
*   `get_buffer_content()` -> str
*   `set_buffer_content(text)`
*   `get_selected_text()` -> str

### Theme/UI
*   `set_theme_color(name, fg, bg)`: Set color for a UI element.
    *   `name`: "keyword", "string", "comment", "background", etc.
    *   `fg`: Terminal color code (0-255).
    *   `bg`: Terminal color code (0-255). -1 for transparent/default.
*   `jot.api.nvim_set_hl(0, group, spec)`: Jot highlight-group mapping via compatibility API.
*   `jot.cmd("colorscheme my_theme")` or `jot.cmd.colorscheme("my_theme")`
*   `jot.notify(text, level="info")`
*   `jot.keymap.set(mode, lhs, rhs)`
*   `jot.api.nvim_create_user_command(name, callback, opts)`
*   `jot.api.nvim_create_autocmd(event, opts)`

## Events

Use `autocmd(event, pattern="*")` for event-driven plugins.

Built-in events:

*   `startup`
*   `buffer_open`
*   `buffer_change`
*   `buffer_save`

Example:

```python
from jot_api import autocmd, jot

@autocmd("buffer_save", pattern="*.py")
def on_python_save(event):
    jot.notify(f"saved {event['filepath']}")
```

## Commands

User commands receive a single string argument.

```python
from jot_api import create_user_command, jot

def grep_word(arg=""):
    jot.notify(f"search for: {arg}")

create_user_command("GrepWord", grep_word)
```

Inside jot:

```text
:GrepWord hello
```

## Plugin Management

Built-in plugin commands:

*   `:PlugReload`
*   `:PlugList`
*   `:PlugHealth`
*   `:PlugInfo [name-or-path-fragment]`
*   `:PlugPolicy [off|warn|strict]`
*   `:PlugAudit [limit] [query]`
*   `:PlugAuditClear`

## Plugin Lifecycle Hooks

Plugins can register lifecycle callbacks:

```python
from jot_api import register_plugin_lifecycle, jot

def on_load():
    jot.notify("plugin loaded")

def on_unload():
    jot.notify("plugin unloading")

def on_reload():
    jot.notify("plugin reloading")

register_plugin_lifecycle(
    on_load=on_load,
    on_unload=on_unload,
    on_reload=on_reload,
)
```

Plugins can also register metadata (name/version/author/dependencies):

```python
from jot_api import register_plugin

register_plugin(
    name="git_tools",
    version="0.2.0",
    author="you",
    description="Extra git commands for jot",
    depends=["lsp", "project"],
    min_api="1.1.0",
    min_python="3.10",
    capabilities=["commands", "ex_commands", "write"],
)
```

You can lazily activate plugin setup on events or commands:

```python
from jot_api import register_plugin

def setup(reason=""):
    # Heavy setup goes here (register keymaps/commands/etc.)
    pass

register_plugin(
    name="big_feature",
    depends=["git_tools"],
    setup=setup,
    lazy_events=["buffer_open", "startup"],
    lazy_commands=["bigfeature"],
    # eager=True forces startup activation even if lazy_* is set
)
```

Optional compatibility/safety gates:

* `min_api` / `max_api`: gate by `jot_api` runtime API version.
* `min_jot` / `max_jot`: gate by host editor version (`JOT_VERSION` env).
* `min_python` / `max_python`: gate by embedded Python version.
* `requires_unsafe=True`: activate only when `JOT_ALLOW_UNSAFE_PLUGINS=1`.
* `capabilities=[...]`: declare privileged actions this plugin needs.

Current capability names:
* `commands` - call `execute_command(...)`
* `ex_commands` - call `execute_ex_command(...)`
* `write` - call `save_file(...)`
* `workspace` - call `open_workspace(...)`
* `terminal` - call `toggle_terminal(...)`
* `sidebar` - call `toggle_sidebar(...)`
* `quit` - call `request_quit(...)` / `save_and_quit(...)`

Policy mode (`JOT_PLUGIN_POLICY`) defaults to `off` for backward compatibility.
Modes:
* `off`: no capability enforcement
* `warn`: allow but warn when undeclared capability is used
* `strict`: block undeclared capability usage
Policy mode is persisted in `~/.config/jot/configs/plugin_policy.json` and restored on startup.
Environment variable `JOT_PLUGIN_POLICY` overrides the persisted value.

Audit:
* Capability checks in `warn`/`strict` are recorded in an in-memory audit trail.
* Use `:PlugAudit` to inspect recent entries.
* Use `:PlugAudit 50 lsp` to filter by query.
* Use `:PlugAuditClear` to reset the trail.

You can also inspect runtime plugin health:

```python
from jot_api import plugin_health, plugin_health_summary, plugin_info

print(plugin_health_summary())
for item in plugin_health():
    print(item["name"], item["loaded"], item["last_load_ms"], item["last_error"])

print(plugin_info("git_tools"))
```

## Example: Auto-Save Plugin

```python
import jot_api
import threading
import time

def auto_save_loop():
    while True:
        time.sleep(30)
        jot_api.save_file()
        jot_api.show_message("Auto-saved!")

# threading.Thread(target=auto_save_loop).start()
```
