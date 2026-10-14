#!/usr/bin/env python3
"""Probe: an event hook can say which files it wants, and a plugin can fire one.

Both reaction surfaces take the same two options. `pattern` filters a hook
against the path of the event it is offered, so a listener on BufOpen or
buffer.save hears about the files it named and nothing else, and `once` drops a
hook after its first run. Neither existed before this change: every hook ran for
every event of its name and the only filter a plugin had was an `if` inside the
callback, which is work the editor can skip instead.

The other half is a name the plugin owns. `jot.autocmd("User build", fn)` is dead
code without a way to fire it, so `jot.autocmd.exec("User build", {stage = ...})`
calls the hooks registered for that name, and `jot.events.emit(name, data)` does
the same on the bus. `jot.events.emit` answers how many subscribers took the
payload, which is what makes a filter checkable from Lua rather than a matter of
watching a log; `jot.autocmd.list` and `jot.events.list` say what is registered.

The scenes below run in one session, and each one appends its verdict to a log
while the editor is up: the two native BufOpen/BufSave scenes drive real files
through the pty, and the rest exercise the matcher from Lua, where a mismatch is
named as such in the log instead of only showing up as a missing line.

Usage: test/autocmd_probe.py [path-to-jot] [--dump]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary).
"""
from __future__ import annotations

import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

BINARY = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
WORK = "/tmp/jot_autocmd_probe_work"

# One log line per hook call, plus the list and matcher verdicts. The plugin
# writes it from its own callbacks, so a line present means the editor called
# Lua with that payload, not that a C++ test said so.
PLUGIN = r"""local log = os.getenv("JOT_PROBE_LOG")
local work = os.getenv("JOT_PROBE_WORK")

local function note(...)
  if not log then
    return
  end
  local parts = {}
  for i = 1, select("#", ...) do
    parts[i] = tostring((select(i, ...)))
  end
  local fh = io.open(log, "a")
  if fh then
    fh:write(table.concat(parts, " ") .. "\n")
    fh:close()
  end
end

-- The tag is the file name alone: the scenes assert which files reached which
-- hook, and the absolute prefix is the same for every one of them.
local function tag(path)
  return (tostring(path or ""):gsub("^.*/", ""))
end

-- 1. A native event, filtered by pattern. "all" is the control: it has no
-- pattern, so it fires for every file and shows what the editor offered.
jot.autocmd("BufOpen", function(e) note("open-txt", tag(e.path)) end, { pattern = "*.txt" })
jot.autocmd("BufOpen", function(e) note("open-all", tag(e.path)) end)

-- 2. A one-shot hook next to a plain one: the file is edited and saved twice
-- below, so "save-all" runs twice and "save-once" only for the first.
jot.autocmd("BufSave", function(e) note("save-all", tag(e.path)) end)
jot.autocmd("BufSave", function(e) note("save-once", tag(e.path)) end,
            { pattern = "*.txt", once = true })

-- 3. A name the plugin fires itself, through three hooks that filter
-- differently: the .rs one must never see a .txt payload.
jot.autocmd("User build", function(e) note("user", e.event, e.stage) end)
jot.autocmd("User build", function(e) note("user-txt", tag(e.path)) end, { pattern = "*.txt" })
jot.autocmd("User build", function(e) note("user-rs", tag(e.path)) end, { pattern = "*.rs" })

local rows = jot.autocmd.list("User build")
note("list", #rows, rows[1].event, "[" .. rows[1].pattern .. "]", tostring(rows[1].once),
     "[" .. rows[2].pattern .. "]", "[" .. rows[3].pattern .. "]")

jot.autocmd.exec("User build", { path = work .. "/notes.txt", stage = "start" })
note("after-exec", #jot.autocmd.list("User build"))

-- 4. once on a user event, fired twice.
jot.autocmd("User once", function(e) note("user-once", e.event) end, { once = true })
jot.autocmd.exec("User once")
jot.autocmd.exec("User once")
note("after-once", #jot.autocmd.list("User once"))

-- 5. The bus takes the same options, and emit() reports how many took it.
local txt = jot.events.subscribe("probe.hit", function(e) note("bus", tag(e.path), e.n) end,
                                 { pattern = "*.txt" })
jot.events.subscribe("probe.hit", function(e) note("bus-lua", tag(e.path)) end, { pattern = "*.lua" })
jot.events.subscribe("probe.hit", function(e) note("bus-deep") end, { pattern = "**/*.txt" })

note("emit", jot.events.emit("probe.hit", { path = work .. "/notes.txt", n = 7 }))
note("emit", jot.events.emit("probe.hit", { path = work .. "/code.lua", n = 8 }))
note("buslist", #jot.events.list("probe.hit"))
jot.events.unsubscribe("probe.hit", txt)
note("buslist", #jot.events.list("probe.hit"))
note("emit", jot.events.emit("probe.hit", { path = work .. "/notes.txt" }))

jot.events.subscribe("probe.once", function(e) note("once-bus", tag(e.path)) end, { once = true })
note("bus-once",
     jot.events.emit("probe.once", { path = "/a/b.txt" }),
     jot.events.emit("probe.once", { path = "/a/b.txt" }),
     #jot.events.list("probe.once"))

-- 6. The dialect itself, one pattern at a time: subscribe with the pattern,
-- emit the path, read the count back. A case that answers the wrong count is
-- named in the log rather than left to the reader to notice.
local cases = {
  { "*.txt", "notes.txt", 1 },
  { "*.txt", work .. "/sub/notes.txt", 1 },
  { "*.txt", "notes.txt.bak", 0 },
  { "*.txt", "notes.py", 0 },
  { "**", "a/b/c.txt", 1 },
  { "**/*.c", work .. "/src/deep/main.c", 1 },
  { "src/*.c", work .. "/src/deep/main.c", 0 },
  { "**/src/*.c", work .. "/src/main.c", 1 },
  -- A single star still stops at a separator, even behind a double one; the
  -- tail rule above is what takes a file name in any directory.
  { "**/src/*.c", work .. "/src/deep/main.c", 0 },
  { "a?c.txt", "abc.txt", 1 },
  { "a?c.txt", "a/c.txt", 0 },
  { "[ab]c.txt", "bc.txt", 1 },
  { "[!ab]c.txt", "cc.txt", 1 },
  { "[]x].txt", "].txt", 1 },
  { "a\\*b", "a*b", 1 },
  { "", "anything at all", 1 },
}

for _, case in ipairs(cases) do
  local key = jot.events.subscribe("probe.glob", function() end, { pattern = case[1] })
  local reached = jot.events.emit("probe.glob", { path = case[2] })
  jot.events.unsubscribe("probe.glob", key)
  local verdict = "ok"
  if reached ~= case[3] then
    verdict = "want " .. case[3] .. " got " .. reached
  end
  note("glob", "[" .. case[1] .. "]", tag(case[2]), verdict)
end
"""

# What the session must have logged. The scenes the plugin runs at load time
# come first (they need no keys), then the matcher cases, then the native
# scenes the pty drives. The two saves are what make the one-shot hook a check:
# both of them fire, and only one of them reaches the one-shot hook.
EXPECTED_BEFORE_GLOB = [
    # 1. The three User hooks and what list() said about them.
    "list 3 User build [] false [*.txt] [*.rs]",
    "user User build start",
    "user-txt notes.txt",
    "after-exec 3",
    # 5. once on a user event, and what list() said after the two firings.
    "user-once User once",
    "after-once 0",
    # 6. The bus: the .txt payload reaches two subscribers, the .lua one reaches
    # one, and removing the .txt subscription leaves one.
    "bus notes.txt 7",
    "bus-deep",
    "emit 2",
    "bus-lua code.lua",
    "emit 1",
    "buslist 3",
    "buslist 2",
    "bus-deep",
    "emit 1",
    # 7. once on the bus, then the list is empty again.
    "once-bus b.txt",
    "bus-once 1 0 0",
]
GLOB_CASES = 16
EXPECTED_AFTER_GLOB = [
    # 8. BufOpen for the command-line file: it is a .txt, so both hooks saw it.
    "open-txt notes.txt",
    "open-all notes.txt",
    # 9. BufSave, twice on that same file: the plain hook ran both times, the
    # one-shot hook only for the first.
    "save-all notes.txt",
    "save-once notes.txt",
    "save-all notes.txt",
    # 10. BufOpen for the two files the commands open: the .lua one reached only
    # the unfiltered hook, the .txt one reached both.
    "open-all code.lua",
    "open-txt other.txt",
    "open-all other.txt",
]
GLOB_OK = "ok"


def main() -> int:
    positional = [arg for arg in sys.argv[1:] if not arg.startswith("--")]
    binary = positional[0] if positional else BINARY
    dump = "--dump" in sys.argv
    if not os.path.exists(binary):
        print("autocmd probe: SKIP - no binary at %s" % binary)
        return 2

    shutil.rmtree(WORK, ignore_errors=True)
    os.makedirs(WORK)
    with open(os.path.join(WORK, "notes.txt"), "w") as fh:
        fh.write("notes\n")
    with open(os.path.join(WORK, "code.lua"), "w") as fh:
        fh.write("-- code\n")
    with open(os.path.join(WORK, "other.txt"), "w") as fh:
        fh.write("other\n")
    os.makedirs(os.path.join(WORK, "cfg"), exist_ok=True)
    with open(os.path.join(WORK, "cfg", "init.lua"), "w") as fh:
        fh.write(PLUGIN)
    log = os.path.join(WORK, "events.log")

    # Two edits and saves of the same .txt file (the one-shot scene), then the
    # two files the pattern scenes open. jot has no mode to type an ex line
    # into: an ex command is typed into the command palette, Ctrl+P, which is
    # how a user reaches both of these.
    cp = b"\x10"
    phases = [
        (0.6, b"x"),
        (0.8, cp + b":w\r"),
        (0.6, b"x"),
        (0.8, cp + b":w\r"),
        (0.8, cp + b":edit code.lua\r"),
        (0.8, cp + b":edit other.txt\r"),
    ]
    screen = run_in_pty(binary, [os.path.join(WORK, "notes.txt")], b"", settle=3.0, after=1.0,
                        cfg=os.path.join(WORK, "cfg"), cwd=WORK, phases=phases,
                        env={"JOT_PROBE_LOG": log, "JOT_PROBE_WORK": WORK})

    lines = []
    if os.path.exists(log):
        with open(log) as fh:
            lines = [line.rstrip("\n") for line in fh if line.strip()]
    if dump:
        print("=== log ===")
        for line in lines:
            print("   " + line)

    failures = []
    glob_lines = [line for line in lines if line.startswith("glob ")]
    head = lines[:len(EXPECTED_BEFORE_GLOB)]
    tail = lines[len(EXPECTED_BEFORE_GLOB) + len(glob_lines):]
    for name, got, want in (("hook", head, EXPECTED_BEFORE_GLOB),
                            ("native", tail, EXPECTED_AFTER_GLOB)):
        if got != want:
            # The first difference is the one worth naming: a missing or extra
            # hook call shifts everything after it.
            for index in range(max(len(got), len(want))):
                left = got[index] if index < len(got) else "<nothing>"
                right = want[index] if index < len(want) else "<nothing>"
                if left != right:
                    failures.append("%s line %d is %r, wanted %r" % (name, index + 1, left, right))
                    break
            failures.append("the %s block has %d lines, wanted %d" % (name, len(got), len(want)))
    if len(glob_lines) != GLOB_CASES:
        failures.append("the matcher ran %d cases, wanted %d" % (len(glob_lines), GLOB_CASES))
    for line in glob_lines:
        if not line.endswith(GLOB_OK):
            failures.append("matcher case %s" % line)

    if failures:
        print("autocmd probe: FAIL")
        for failure in failures:
            print("  - %s" % failure)
        print("--- log ---")
        for line in lines:
            print("   " + line)
        print("--- screen ---")
        print(screen.text())
        return 1
    print("autocmd probe: PASS (%d hook lines, %d matcher cases)"
          % (len(head) + len(tail), len(glob_lines)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
