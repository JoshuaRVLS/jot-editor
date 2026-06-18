# Plugins

`jot` has a trusted local Python plugin layer for editor customization.

Load order:

```text
~/.config/jot/init.py
~/.config/jot/plugins/*.py
~/.config/jot/plugins/*/plugin.py
```

Plugin files are user-local code and run inside the embedded Python
interpreter. Workspace/project files are not auto-loaded as plugins.
Set `JOT_CONFIG_HOME` to use a different config root for testing. Folder
plugins can import helper Python files from their own directory.

## Commands

Register commands with `@command`:

```python
from jot_api import command, jot

@command("hello", "Say hello from a plugin")
def hello(arg):
    jot.notify("Hello " + arg)
```

Run it with:

```text
:hello livestream
```

Commands appear in the command palette under `Plugin`.

## Keymaps

Use `jot.keymap.set()` from `init.py` or a plugin:

```python
from jot_api import jot

jot.keymap.set("Ctrl+Shift+P", ":plugins", desc="Show plugins")
jot.keymap.set("Alt+H", lambda arg: jot.notify("hi"), desc="Say hi")
```

Plugin keymaps override built-in keymaps, except emergency quit handling.
Set `JOT_KEYMAP_DEBUG=/tmp/jot-keys.log` before launching Jot to log the
candidate key names received by the plugin keymap dispatcher.
Some terminals report `Ctrl+Shift+<letter>` as plain `Ctrl+<letter>`; set
`JOT_KEYMAP_CTRL_SHIFT_FALLBACK=1` only on those terminals to make plain
`Ctrl+<letter>` also try shifted plugin keymaps before built-ins.

## Events

Autocmds run on editor events:

```python
from jot_api import autocmd, jot

@autocmd("BufSave")
def saved(event):
    jot.notify("saved " + event["file"])
```

Supported events: `EditorEnter`, `PluginReload`, `BufOpen`, `BufChange`,
`BufSave`. Event callbacks receive a dict with `event`, `file`, and `buffer`.

## Buffer API

Plugins can read, write, insert, and replace selected text:

```python
from jot_api import command, jot

@command("upper-selection", "Uppercase selected text")
def upper_selection(arg):
    selected = jot.buffer.get_selection()
    if selected:
        jot.buffer.replace_selection(selected.upper())
```

Useful helpers:

```python
jot.buffer.get_text()
jot.buffer.set_text(text)
jot.buffer.get_selection()
jot.buffer.replace_selection(text)
jot.buffer.insert_text(text)
jot.buffer.cursor()
jot.buffer.set_cursor(line, col)
jot.buffer.current_file()
```

Buffer mutations use normal undo, mark the file modified, refresh syntax, and
notify LSP.

## UI And Jobs

Plugins can open a picker:

```python
from jot_api import command, jot

@command("pick-demo", "Open plugin picker")
def pick_demo(arg):
    jot.ui.show_picker("Demo", ["one", "two"], lambda value: jot.notify(value))
```

Plugins can register a right-side text panel:

```python
from jot_api import command, jot

def lines(arg):
    return ["Plugin panel", "Current file:", jot.buffer.current_file()]

jot.ui.register_panel("demo", lines, "Demo")

@command("demo-panel", "Show demo panel")
def demo_panel(arg):
    jot.ui.show_panel("demo")
```

Plugins can run shell jobs in the integrated terminal:

```python
jot.job.run("npm test", label="tests")
```

## Reloading

Use these commands while developing plugins:

```text
:reloadplugins
:plugins
:pluginpanel demo
```

`:reloadplugins` reloads `init.py` and plugin files. `:plugins` shows loaded
files, commands, keymaps, panels, and load errors.
