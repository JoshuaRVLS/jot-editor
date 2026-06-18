# Plugins

`jot` loads trusted local Python plugins from:

```text
~/.config/jot/plugins/*.py
```

Plugins run in the embedded Python interpreter during startup. They are local
user code, not sandboxed workspace code.

## Commands

Register commands with the `@command` decorator:

```python
from jot_api import command, show_message

@command("hello", "Say hello from a plugin")
def hello(arg):
    show_message("Hello " + arg)
```

Run the command from the command palette or ex prompt:

```text
:hello livestream
```

Registered commands appear in command palette completion under the `Plugin`
category.

## Buffer Access

Plugins can read and replace the current buffer:

```python
from jot_api import command, get_current_buffer, set_current_buffer

@command("upperbuf", "Uppercase current buffer")
def upperbuf(arg):
    set_current_buffer(get_current_buffer().upper())
```

Replacing the buffer uses the editor's normal undo path and marks the buffer as
modified.
