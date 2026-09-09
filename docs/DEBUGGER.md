# Debugger

`jot` has a native debugger panel backed by Debug Adapter Protocol (DAP)
adapters. It talks to `gdb --interpreter=dap` or `lldb-dap` over stdio and
shows threads, the call stack, variables, memory and program output in the
right-hand panel.

## Quick start

1. Build your program with debug symbols (`-g` for gcc/clang, `/Zi` + a PDB
   for MSVC — on Linux `cmake -DCMAKE_BUILD_TYPE=Debug` is enough).
2. Start a session:

   ```text
   :debug ./build/app            launch with GDB
   :debuglldb ./build/app        launch with LLDB
   :debugconfig app              launch a saved session (see Config)
   :debugattach 1234             attach to a running process (GDB)
   ```

3. Set a breakpoint: click the gutter marker column on a line, or press
   `F9` with the cursor on the line. The red dot is the breakpoint; it
   turns solid when the adapter verifies it.
4. `F5` continues. The program runs until it hits a breakpoint (or crashes),
   then the debugger panel opens automatically and the editor jumps to the
   paused line. Inspect the stack and variables in the panel, then step:

   | Key       | Action         |
   |-----------|----------------|
   | `F5`      | Continue       |
   | `Shift+F5`| Stop session   |
   | `F9`      | Toggle breakpoint at cursor |
   | `F10`     | Step over      |
   | `F11`     | Step into      |
   | `Shift+F11`| Step out      |
   | `F6` / `Shift+F6` | Next / previous thread |
   | `F7` / `F8` | Previous / next stack frame |
   | `Ctrl+PageUp` / `Ctrl+PageDown` | Scroll output history |

## Commands

- `:debug <program> [args...]` launches with GDB.
- `:debuggdb <program> [args...]` launches with GDB.
- `:debuglldb <program> [args...]` launches with LLDB.
- `:debugconfig [name]` launches a session from `.jot/debug.json`, or from
  `~/.config/jot/configs/debug.json` when no local config exists. With no
  name it launches the first config.
- `:debugattach <pid>` attaches with GDB.
- `:debugpanel` toggles the debugger panel.
- `:debugstop`, `:debugrestart`, `:debugcontinue`, `:debugpause` control the
  active session.
- `:debugstep`, `:debugnext`, `:debugout` step into, over, and out.
- `:debugthreads` refreshes thread and stack views.
- `:debugmemory [expr] [bytes]` reads memory (see below).
- `:debugdisasm [expr|addr]` disassembles near an address when the adapter
  supports it (GDB and lldb-dap currently don't, so this reports a message).

All commands also appear in the command palette under **Debugger**.

## The memory viewer

`:debugmemory` reads raw memory from the paused program and renders a
hexdump in the panel. Pass an address literal or any expression the
debugger can evaluate to an address (expressions are evaluated first, then
the resulting address is read):

```text
:debugmemory $pc           read 128 bytes at the program counter (default)
:debugmemory &buf          read 128 bytes of a variable
:debugmemory buf           same — variables resolve through evaluation
:debugmemory 0x7fff1000    read 128 bytes at a raw address
:debugmemory &buf 64       read only 64 bytes
:debugmemory &buf 1024     read up to 1024 bytes (the hard cap)
```

Each row shows three columns:

```text
0x00007fffffffe000  48 65 6c 6c 6f 20 77 6f 72 6c 64 21 00 00 00 00  Hello world!....
└ address           └ one byte per pair, hex        └ ASCII, non-printable = .
```

The memory view only works while the program is stopped (paused at a
breakpoint). If the adapter does not support it you get a status message
("Debugger does not support memory view") — GDB 12+ and lldb-dap both do.
(Note: GDB's `readMemory` only accepts literal addresses, which is why
non-address inputs go through the `evaluate` request first.)

## Output scrollback

The "Disassembly / Output" section keeps the last ~64 KB of adapter output.
Scroll back with the mouse wheel over the panel, or with
`Ctrl+PageUp` / `Ctrl+PageDown`. The scroll position stays put while
scrolled up (new output is appended below) and snaps back to the bottom
when you scroll all the way down.

## Threads and frames

While paused, `F6` / `Shift+F6` switch between threads and `F7` / `F8` walk
the current thread's stack frames. Switching selects the frame: the editor
jumps to its source location, the active frame is highlighted in the panel
(bold), and the Variables section refreshes to that frame's scope.

## Config

Local project config (`.jot/debug.json`):

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

Fields: `adapter` (`gdb` or `lldb`), `program`, `args`, `cwd`, `env`, and
for attach sessions `attach: true` with `pid`. A user-level fallback lives
at `~/.config/jot/configs/debug.json`.

## How it works / troubleshooting

Sessions are live subprocesses: DAP messages flow over stdin/stdout, and
the panel is local only — sessions are not restored across editor restarts,
and the debuggee is terminated when you stop the session.

Every session writes a transcript to `~/.config/jot/logs/debug_<name>.log`
(SEND/RECV lines). If the panel says nothing and the session dies, check
the log — or run `gdb --interpreter=dap` by hand and paste the same
commands. Common causes:

- **"Debugger adapter missing"** — `gdb` (or `lldb-dap`) is not on `PATH`.
- **Breakpoints never hit** — the binary was built without `-g` debug
  symbols, or the source path the adapter reports doesn't match an open
  buffer.
- **Variables are empty** — nothing is paused yet; set a breakpoint and
  `F5` first.
- **"Debugger does not support …"** — the adapter's `initialize` response
  didn't advertise the capability (GDB < 12 has no `readMemory`).