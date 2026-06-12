# Debugger

`jot` has a native debugger panel backed by Debug Adapter Protocol adapters.

Supported adapters:

- GDB: `gdb --interpreter=dap`
- LLDB: `lldb-dap`

## Commands

- `:debug <program> [args...]` launches with GDB.
- `:debuggdb <program> [args...]` launches with GDB.
- `:debuglldb <program> [args...]` launches with LLDB.
- `:debugconfig [name]` launches a session from `.jot/debug.json`, or from
  `~/.config/jot/configs/debug.json` when no local config exists.
- `:debugattach <pid>` attaches with GDB.
- `:debugpanel` toggles the debugger panel.
- `:debugstop`, `:debugrestart`, `:debugcontinue`, `:debugpause` control the
  active session.
- `:debugstep`, `:debugnext`, `:debugout` step into, over, and out.
- `:debugthreads` refreshes thread and stack views.
- `:debugmemory <expr|addr>` reads memory when the adapter supports it.
- `:debugdisasm [expr|addr]` disassembles near an address when supported.

Click the editor gutter marker column to toggle a source breakpoint.

## Config

Local project config:

```json
{
  "sessions": {
    "app": {
      "adapter": "gdb",
      "program": "./build/app",
      "args": ["--flag"],
      "cwd": ".",
      "env": {}
    }
  }
}
```

The panel is local only. Live debuggee processes are not restored across editor
restarts.
